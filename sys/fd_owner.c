/* sys/fd_owner.c — Base-NEXS bootstrap I/O session.
 * The only hosted descriptor provider. Descriptors are NOT authority tokens.
 * Native VFS Chan/Dev and NXEC per-domain session cutover remain pending.
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "include/nexs_sys.h"
#include "nexs_hal.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_value.h"
#include "../core/include/nexs_common.h"
#include "../core/include/nexs_checked.h"
#include "../runtime/include/nexs_source_status.h"
#include <stdatomic.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
/* The freestanding libc implementation provides ferror, but its minimal
 * stdio header does not yet publish the standard declaration. This is the
 * exact portable C library signature; it does not fabricate I/O status. */
extern int ferror(FILE *stream);
extern const char *nexs_embedded_lookup(const char *path);

/* A descriptor slot only refers to an OPEN DESCRIPTION. dup shares that
 * description, its stream/cursor, and the single close owner.
 * All list/slot transitions are a bounded nonwaiting CAS; no VFS, user
 * callback, allocation, stream IO or registry access happens under it.
 * A borrowing operation owns a lease until it drops the final reference. */
typedef struct NexsOpenFile {
    FILE *stream;
    const char *embedded;
    size_t embedded_size;
    size_t position;
    uint32_t last_io;              /* protected by this description's io CAS */
    _Atomic size_t refs;           /* leases survive slot close/dup races */
    unsigned int flags;
    unsigned int standard_stream; /* stdin/out/err are not fclose-owned */
    char path[REG_PATH_MAX];
    _Atomic uint32_t io_lock;     /* private to ONE open description */
} NexsOpenFile;

typedef struct NexsFdSlot {
    NexsOpenFile *open;
} NexsFdSlot;

typedef struct NexsIoSession {
    NexsFdSlot *slots;
    size_t capacity;
    _Atomic uint32_t slot_lock;
    NexsOpenFile std_streams[3];
    unsigned int initialized;
} NexsIoSession;

/* Transitional bootstrap owner, never published as an authority source.
 * P2 will bind a NexsIoSession to each explicit RuntimeInstance/Task. */
static NexsIoSession s_io;
static char g_errstr[REG_PATH_MAX] = "";

/* Portable bounded length: freestanding libc does not implement strnlen.
 * An unterminated path is rejected without scanning beyond its ABI span. */
static size_t fd_path_length(const char *path, size_t capacity) {
    if (path == NULL)
        return capacity;
    size_t length = 0u;
    for (size_t i = 0u; i < capacity && path[i] != '\0'; ++i)
        length += 1u;
    return length;
}

static void set_errstr(const char *msg) {
    if (msg != NULL) {
        (void)strncpy(g_errstr, msg, sizeof(g_errstr) - 1u);
        g_errstr[sizeof(g_errstr) - 1u] = '\0';
    } else {
        g_errstr[0] = '\0';
    }
}

static int slots_enter(void) {
    uint32_t expected = 0u;
    return atomic_compare_exchange_strong_explicit(
        &s_io.slot_lock, &expected, 1u,
        memory_order_acquire, memory_order_relaxed);
}

static void slots_leave(void) {
    atomic_store_explicit(&s_io.slot_lock, 0u, memory_order_release);
}

static int open_enter(NexsOpenFile *open) {
    uint32_t expected = 0u;
    return atomic_compare_exchange_strong_explicit(
        &open->io_lock, &expected, 1u,
        memory_order_acquire, memory_order_relaxed);
}

static void open_leave(NexsOpenFile *open) {
    atomic_store_explicit(&open->io_lock, 0u, memory_order_release);
}

static int fd_slot_valid(int fd) {
    return fd >= 0 && (size_t)fd < s_io.capacity;
}

/* The slots can be reallocated only while holding slot_lock for a bounded
 * copy. Provider allocation and memory release are always outside CAS. */
static int fd_reserve(size_t minimum) {
    if (minimum == 0u || minimum > (size_t)INT_MAX)
        return -1;
    if (!slots_enter())
        return -1;
    size_t current = s_io.capacity;
    slots_leave();
    if (minimum <= current)
        return 0;
    size_t capacity = current == 0u ? 8u : current;
    for (size_t i = 0u;
         i < sizeof(size_t) * CHAR_BIT && capacity < minimum; ++i) {
        if (capacity > (size_t)INT_MAX / 2u)
            capacity = minimum;
        else
            capacity *= 2u;
    }
    size_t bytes = 0u;
    if (nx_size_mul(capacity, sizeof(NexsFdSlot), &bytes) != NX_STATUS_OK)
        return -1;
    NexsFdSlot *replacement = nexs_alloc(bytes);
    if (replacement == NULL)
        return -1;
    memset(replacement, 0, bytes);
    if (!slots_enter()) {
        nexs_free(replacement, bytes);
        return -1;
    }
    NexsFdSlot *former = NULL;
    size_t former_capacity = 0u;
    if (minimum > s_io.capacity) {
        former = s_io.slots;
        former_capacity = s_io.capacity;
        if (former != NULL)
            memcpy(replacement, former,
                   former_capacity * sizeof(*former));
        s_io.slots = replacement;
        s_io.capacity = capacity;
        replacement = NULL;
    }
    slots_leave();
    if (former != NULL)
        nexs_free(former, former_capacity * sizeof(*former));
    if (replacement != NULL)
        nexs_free(replacement, bytes);
    return 0;
}

static void open_release_resource(NexsOpenFile *open) {
    if (open == NULL || open->standard_stream != 0u)
        return;
    if (open->stream != NULL)
        (void)fclose(open->stream);
    nexs_free(open, sizeof(*open));
}

/* All slot-level borrow/dup increments are done before releasing the
 * nonwaiting session lock. The atomic last lease owns resource retirement.
 * No global lock is ever acquired for a hosted stream close/flush. */
static NexsOpenFile *open_unref(NexsOpenFile *open) {
    if (open == NULL)
        return NULL;
    size_t prior = atomic_fetch_sub_explicit(
        &open->refs, 1u, memory_order_acq_rel);
    return prior == 1u ? open : NULL;
}

static NexsOpenFile *open_borrow(int fd) {
    if (!slots_enter())
        return NULL;
    NexsOpenFile *open = fd_slot_valid(fd) ? s_io.slots[fd].open : NULL;
    if (open != NULL) {
        if (atomic_load_explicit(&open->refs, memory_order_relaxed) ==
            SIZE_MAX)
            open = NULL;
        else
            (void)atomic_fetch_add_explicit(&open->refs, 1u,
                                            memory_order_relaxed);
    }
    slots_leave();
    return open;
}

static void open_drop(NexsOpenFile *open) {
    open_release_resource(open_unref(open));
}

/* Removed /sys/fd/<n> as a second mutable authority-shaped index.
 * fd2path resolves against this ONE live descriptor session instead. */

int sysio_init(void) {
    if (s_io.initialized != 0u)
        return -1;
    if (fd_reserve(3u) != 0)
        return -1;
    FILE *standard[] = {stdin, stdout, stderr};
    const char *paths[] = {"/dev/stdin","/dev/stdout","/dev/stderr"};
    for (size_t i = 0u; i < 3u; ++i) {
        NexsOpenFile *file = &s_io.std_streams[i];
        memset(file, 0, sizeof(*file));
        file->stream = standard[i];
        file->flags = i == 0u ? NEXS_OREAD : NEXS_OWRITE;
        file->standard_stream = 1u;
        atomic_init(&file->refs, 1u);
        memcpy(file->path, paths[i], strlen(paths[i]) + 1u);
        atomic_init(&file->io_lock, 0u);
        s_io.slots[i].open = file;
    }
    s_io.initialized = 1u;
    return 0;
}

static NexsOpenFile *open_new(FILE *stream, const char *embedded,
                              const char *path, int flags) {
    if (path == NULL)
        return NULL;
    size_t length = fd_path_length(path, REG_PATH_MAX);
    if (length == 0u || length >= REG_PATH_MAX)
        return NULL;
    NexsOpenFile *file = nexs_alloc(sizeof(*file));
    if (file == NULL)
        return NULL;
    memset(file, 0, sizeof(*file));
    file->stream = stream;
    file->embedded = embedded;
    file->embedded_size = embedded == NULL ? 0u : strlen(embedded);
    file->flags = (unsigned int)flags;
    atomic_init(&file->refs, 1u);
    memcpy(file->path, path, length + 1u);
    atomic_init(&file->io_lock, 0u);
    return file;
}

static int fd_install(NexsOpenFile *file) {
    if (file == NULL)
        return -1;
    /* At most the integer bit width worth of growth transitions. A
     * concurrent writer never causes an unbounded retry/lock wait. */
    for (size_t attempts = 0u; attempts < sizeof(int) * CHAR_BIT; ++attempts) {
        if (!slots_enter())
            return -1;
        size_t chosen = s_io.capacity;
        for (size_t i = 3u; i < s_io.capacity &&
                            chosen == s_io.capacity; ++i) {
            if (s_io.slots[i].open == NULL)
                chosen = i;
        }
        if (chosen < s_io.capacity) {
            s_io.slots[chosen].open = file;
            slots_leave();
            return (int)chosen;
        }
        size_t next = s_io.capacity + 1u;
        slots_leave();
        if (fd_reserve(next) != 0)
            return -1;
    }
    return -1;
}

static int open_mode(const char *path, int mode, const char **fmode) {
    if (path == NULL || fd_path_length(path, REG_PATH_MAX) >= REG_PATH_MAX ||
        (mode & ~(NEXS_OTRUNC | 0x0f)) != 0)
        return -1;
    int base = mode & 0x0f;
    if (base == NEXS_OREAD) {
        if ((mode & NEXS_OTRUNC) != 0)
            return -1;
        *fmode = "r";
    } else if (base == NEXS_OWRITE) {
        /* Append mode forces end-of-file even after an explicit seek.
         * Write-only without truncation must never silently append. */
        *fmode = (mode & NEXS_OTRUNC) != 0 ? "w" : "r+";
    } else if (base == NEXS_ORDWR) {
        *fmode = (mode & NEXS_OTRUNC) != 0 ? "w+" : "r+";
    } else {
        return -1;
    }
    return 0;
}

int nexs_open(const char *path, int mode) {
    const char *fmode = NULL;
    if (s_io.initialized == 0u || open_mode(path, mode, &fmode) != 0) {
        set_errstr("open: invalid request");
        return -1;
    }
    int base = mode & 0x0f;
    char platform_path[REG_PATH_MAX];
    const char *real_path = base == NEXS_OREAD
            ? nexs_platform_source_path(path, platform_path,
                                        sizeof(platform_path))
            : path;
    if (real_path == NULL) {
        set_errstr("open: invalid source path");
        return -1;
    }
    FILE *stream = fopen(real_path, fmode);
    const char *embedded = NULL;
    if (stream == NULL) {
        nx_status source_status = nexs_source_status_from_errno(errno);
        if (base == NEXS_OREAD &&
            nexs_source_embedded_admissible(source_status))
            embedded = nexs_embedded_lookup(path);
        if (embedded == NULL) {
            set_errstr("open: filesystem provider refused resource");
            return -1;
        }
    }
    NexsOpenFile *file = open_new(stream, embedded, path, mode);
    if (file == NULL) {
        if (stream != NULL)
            (void)fclose(stream);
        set_errstr("open: resource admission failed");
        return -1;
    }
    int fd = fd_install(file);
    if (fd < 0) {
        open_release_resource(file);
        set_errstr("open: descriptor admission failed");
    }
    return fd;
}

int nexs_create(const char *path, int mode, int perm) {
    (void)perm; /* TODO: canonical NXEC/VFS ACL controls permission. */
    if (s_io.initialized == 0u || path == NULL) {
        set_errstr("create: invalid path");
        return -1;
    }
    int base = mode & 0x0f;
    if ((mode & ~(NEXS_OTRUNC | 0x0f)) != 0 ||
        (base != NEXS_OREAD && base != NEXS_OWRITE &&
         base != NEXS_ORDWR)) {
        set_errstr("create: invalid mode");
        return -1;
    }
    const char *fmode = base == NEXS_OWRITE ? "w" : "w+";
    if (fd_path_length(path, REG_PATH_MAX) >= REG_PATH_MAX) {
        set_errstr("create: path too long");
        return -1;
    }
    FILE *stream = fopen(path, fmode);
    if (stream == NULL) {
        /* Never pretend a 4096-byte per-FD shadow buffer is a persistent
         * file. The actual NXOLMO VFS must own freestanding creation. */
        set_errstr("create: no writable filesystem provider");
        return -1;
    }
    NexsOpenFile *file = open_new(stream, NULL, path, mode);
    if (file == NULL) {
        (void)fclose(stream);
        set_errstr("create: resource admission failed");
        return -1;
    }
    int fd = fd_install(file);
    if (fd < 0) {
        open_release_resource(file);
        set_errstr("create: descriptor admission failed");
    }
    return fd;
}

int nexs_close(int fd) {
    if (!slots_enter()) {
        set_errstr("close: session busy");
        return -1;
    }
    NexsOpenFile *file = fd_slot_valid(fd) ? s_io.slots[fd].open : NULL;
    if (file == NULL) {
        slots_leave();
        set_errstr("close: invalid descriptor");
        return -1;
    }
    s_io.slots[fd].open = NULL;
    NexsOpenFile *dead = open_unref(file);
    slots_leave();
    open_release_resource(dead);
    return 0;
}

int nexs_pread(int fd, char *buffer, int n, int64_t offset) {
    if (buffer == NULL || n <= 0) {
        set_errstr("read: invalid span");
        return -1;
    }
    NexsOpenFile *file = open_borrow(fd);
    if (file == NULL) {
        set_errstr("read: unavailable descriptor");
        return -1;
    }
    int count = -1;
    if ((file->flags & 0x0f) == NEXS_OWRITE || !open_enter(file)) {
        set_errstr("read: access denied or resource busy");
    } else if (file->embedded != NULL) {
        size_t position = offset >= 0 ? (size_t)offset : file->position;
        size_t remaining = position < file->embedded_size
                            ? file->embedded_size - position : 0u;
        size_t amount = remaining < (size_t)n ? remaining : (size_t)n;
        if (amount != 0u)
            memcpy(buffer, file->embedded + position, amount);
        if (offset < 0)
            file->position += amount;
        count = (int)amount;
        open_leave(file);
    } else if (file->stream != NULL) {
        long restore = -1L;
        int valid_offset = 1;
        if (offset >= 0) {
            restore = ftell(file->stream);
            valid_offset = restore >= 0L &&
                           (uint64_t)offset <= (uint64_t)LONG_MAX &&
                           fseek(file->stream, (long)offset, SEEK_SET) == 0;
        }
        if (valid_offset != 0) {
            size_t received = fread(buffer, 1u, (size_t)n, file->stream);
            count = ferror(file->stream) != 0 ? -1 : (int)received;
            if (count >= 0)
                file->last_io = 1u;
        }
        if (restore >= 0L &&
            fseek(file->stream, restore, SEEK_SET) != 0)
            count = -1;
        if (count < 0)
            set_errstr("read: host stream failed");
        open_leave(file);
    } else {
        set_errstr("read: missing provider");
        open_leave(file);
    }
    open_drop(file);
    return count;
}

int nexs_pwrite(int fd, const char *buffer, int n, int64_t offset) {
    if (buffer == NULL || n <= 0) {
        set_errstr("write: invalid span");
        return -1;
    }
    NexsOpenFile *file = open_borrow(fd);
    if (file == NULL) {
        set_errstr("write: unavailable descriptor");
        return -1;
    }
    int count = -1;
    if ((file->flags & 0x0f) == NEXS_OREAD ||
        file->embedded != NULL || !open_enter(file)) {
        set_errstr("write: access denied or resource busy");
    } else if (file->stream != NULL) {
        long restore = -1L;
        int valid_offset = 1;
        if (offset >= 0) {
            restore = ftell(file->stream);
            valid_offset = restore >= 0L &&
                           (uint64_t)offset <= (uint64_t)LONG_MAX &&
                           fseek(file->stream, (long)offset, SEEK_SET) == 0;
        }
        /* C update streams require a positioning barrier when changing
         * from read to write. This is per-open-description, so aliases
         * share the same state. Unseekable write-only pipes avoid fseek. */
        if (valid_offset != 0 && offset < 0 && file->last_io == 1u &&
            fseek(file->stream, 0L, SEEK_CUR) != 0)
            valid_offset = 0;
        if (valid_offset != 0) {
            size_t written = fwrite(buffer, 1u, (size_t)n, file->stream);
            count = written == (size_t)n && fflush(file->stream) == 0
                        ? (int)written : -1;
            if (count >= 0)
                file->last_io = 2u;
        }
        if (restore >= 0L &&
            fseek(file->stream, restore, SEEK_SET) != 0)
            count = -1;
        if (count < 0)
            set_errstr("write: host stream failed");
        open_leave(file);
    } else {
        set_errstr("write: missing provider");
        open_leave(file);
    }
    open_drop(file);
    return count;
}

int64_t nexs_seek(int fd, int64_t offset, int whence) {
    NexsOpenFile *file = open_borrow(fd);
    if (file == NULL || whence < 0 || whence > 2) {
        open_drop(file);
        set_errstr("seek: invalid descriptor/whence");
        return -1;
    }
    if (!open_enter(file)) {
        open_drop(file);
        set_errstr("seek: resource busy");
        return -1;
    }
    int64_t position = -1;
    if (file->embedded != NULL) {
        size_t base = whence == 0 ? 0u :
                      whence == 1 ? file->position : file->embedded_size;
        uint64_t wanted = 0u;
        if (base <= (size_t)INT64_MAX && (offset >= 0
                ? (uint64_t)offset <= (uint64_t)INT64_MAX - base
                : (uint64_t)(-(offset + 1)) + 1u <= base)) {
            wanted = offset >= 0 ? base + (uint64_t)offset :
                     base - ((uint64_t)(-(offset + 1)) + 1u);
            if (wanted <= file->embedded_size) {
                file->position = (size_t)wanted;
                position = (int64_t)wanted;
            }
        }
    } else if (file->stream != NULL &&
               offset >= (int64_t)LONG_MIN &&
               offset <= (int64_t)LONG_MAX &&
               fseek(file->stream, (long)offset,
                     whence == 0 ? SEEK_SET : whence == 1
                     ? SEEK_CUR : SEEK_END) == 0) {
        long current = ftell(file->stream);
        position = current >= 0L ? (int64_t)current : -1;
    }
    open_leave(file);
    open_drop(file);
    if (position < 0)
        set_errstr("seek: offset rejected");
    return position;
}

int nexs_dup(int oldfd, int newfd) {
    if (newfd < -1 || newfd == INT_MAX) {
        set_errstr("dup: invalid destination");
        return -1;
    }
    /* Dynamic admission is outside the CAS. A concurrent close/replace
     * is rechecked under the lock before the reference is published. */
    for (size_t attempt = 0u; attempt < sizeof(int) * CHAR_BIT; ++attempt) {
        if (newfd >= 0 && fd_reserve((size_t)newfd + 1u) != 0) {
            set_errstr("dup: capacity admission failed");
            return -1;
        }
        if (!slots_enter()) {
            set_errstr("dup: session busy");
            return -1;
        }
        NexsOpenFile *source = fd_slot_valid(oldfd)
                                ? s_io.slots[oldfd].open : NULL;
        if (source == NULL ||
            atomic_load_explicit(&source->refs, memory_order_relaxed) ==
            SIZE_MAX) {
            slots_leave();
            set_errstr("dup: invalid source");
            return -1;
        }
        if (oldfd == newfd) {
            slots_leave();
            return oldfd;
        }
        int chosen = newfd;
        if (chosen < 0) {
            for (size_t i = 3u; i < s_io.capacity && chosen < 0; ++i) {
                if (s_io.slots[i].open == NULL)
                    chosen = (int)i;
            }
        }
        if (chosen >= 0 && fd_slot_valid(chosen)) {
            NexsOpenFile *replaced = s_io.slots[chosen].open;
            (void)atomic_fetch_add_explicit(&source->refs, 1u,
                                            memory_order_relaxed);
            s_io.slots[chosen].open = source;
            NexsOpenFile *dead = open_unref(replaced);
            slots_leave();
            open_release_resource(dead);
            return chosen;
        }
        size_t next = s_io.capacity + 1u;
        slots_leave();
        if (fd_reserve(next) != 0) {
            set_errstr("dup: provider exhausted");
            return -1;
        }
    }
    set_errstr("dup: admission retry budget exhausted");
    return -1;
}

int nexs_fd2path(int fd, char *buffer, int nbuf) {
    if (buffer == NULL || nbuf <= 0) {
        set_errstr("fd2path: invalid output");
        return -1;
    }
    NexsOpenFile *file = open_borrow(fd);
    if (file == NULL) {
        set_errstr("fd2path: invalid descriptor");
        return -1;
    }
    size_t length = fd_path_length(file->path, REG_PATH_MAX);
    if (length >= (size_t)nbuf) {
        open_drop(file);
        set_errstr("fd2path: output too small");
        return -1; /* no truncated path can impersonate a valid handle */
    }
    memcpy(buffer, file->path, length + 1u);
    open_drop(file);
    return 0;
}

int nexs_remove(const char *path) {
    if (path == NULL || remove(path) != 0) {
        set_errstr("remove: resource not removed");
        return -1;
    }
    return 0;
}

int nexs_pipe(int ends[2]) {
    if (ends == NULL) {
        set_errstr("pipe: missing output");
        return -1;
    }
    int native[2] = {-1,-1};
    if (pipe(native) != 0) {
        set_errstr("pipe: provider creation failed");
        return -1;
    }
    FILE *read_stream = fdopen(native[0], "r");
    if (read_stream == NULL) {
        (void)close(native[0]);
        (void)close(native[1]);
        return -1;
    }
    FILE *write_stream = fdopen(native[1], "w");
    if (write_stream == NULL) {
        (void)fclose(read_stream);
        (void)close(native[1]);
        return -1;
    }
    NexsOpenFile *reader = open_new(read_stream, NULL,
                                    "/dev/pipe/r", NEXS_OREAD);
    NexsOpenFile *writer = open_new(write_stream, NULL,
                                    "/dev/pipe/w", NEXS_OWRITE);
    if (reader == NULL || writer == NULL) {
        if (reader != NULL) open_release_resource(reader);
        else (void)fclose(read_stream);
        if (writer != NULL) open_release_resource(writer);
        else (void)fclose(write_stream);
        set_errstr("pipe: resource admission failed");
        return -1;
    }
    int fd0 = fd_install(reader);
    if (fd0 < 0) {
        open_release_resource(reader);
        open_release_resource(writer);
        set_errstr("pipe: descriptor admission failed");
        return -1;
    }
    int fd1 = fd_install(writer);
    if (fd1 < 0) {
        (void)nexs_close(fd0);
        open_release_resource(writer);
        set_errstr("pipe: destination admission failed");
        return -1;
    }
    ends[0] = fd0;
    ends[1] = fd1;
    return 0;
}

Value nexs_stat(const char *path) {
  if (path == NULL) return val_err(4, "stat: missing path");
  struct stat sb;
  if (stat(path, &sb) != 0)
      return val_err(4, "stat: file not found");
  char buffer[REG_PATH_MAX];
  (void)snprintf(buffer, sizeof(buffer),
           "path=%s size=%lld mode=%o mtime=%lld type=%s",
           path, (long long)sb.st_size, (unsigned int)(sb.st_mode & 0777),
           (long long)sb.st_mtime,
           S_ISDIR(sb.st_mode) ? "dir" :
           S_ISREG(sb.st_mode) ? "file" : "other");
  return val_str(buffer);
}

int nexs_chdir(const char *path) {
    if (path == NULL || chdir(path) != 0) {
        set_errstr("chdir: refused");
        return -1;
    }
    return 0;
}

/* The script API swaps the previous error with a caller-supplied string.
 * Errors remain bootstrap-session-local pending P2 TaskContext migration. */
void nexs_errstr(char *buffer, int size) {
    if (buffer == NULL || size <= 0)
        return;
    char previous[REG_PATH_MAX];
    memcpy(previous, g_errstr, sizeof(previous));
    set_errstr(buffer);
    (void)strncpy(buffer, previous, (size_t)size - 1u);
    buffer[size - 1] = '\0';
}
