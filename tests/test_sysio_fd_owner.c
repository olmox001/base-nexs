/* Actual product runtime/sysio open-description ownership regression.
 * No fake FILE pointers, mock VFS providers or duplicated FD implementation.
 */
#define _POSIX_C_SOURCE 200809L
#include "nexs_runtime.h"
#include "nexs_sys.h"
#include "nexs_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sched.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "SYSIO_FD_OWNER_FAIL line %d: %s\n", \
            __LINE__, #test); return 1; \
} } while (0)

static char fixture_path[] = "build/assurance/nexs_fd_owner_XXXXXX";

static int setup_file(void) {
    static const char payload[] = "0123456789abcdef";
    int native = mkstemp(fixture_path);
    CHECK(native >= 0);
    CHECK(write(native,payload,sizeof(payload) - 1u) ==
          (ssize_t)(sizeof(payload) - 1u));
    CHECK(close(native) == 0);
    return 0;
}

static int shared_open_description_semantics(void) {
    int original = nexs_open(fixture_path, NEXS_OREAD);
    CHECK(original >= 3);
    int alias = nexs_dup(original, -1);
    CHECK(alias >= 3 && alias != original);
    CHECK(nexs_dup(original, original) == original);
    char data[32] = {0};
    CHECK(nexs_pread(original, data, 3, -1) == 3 &&
          memcmp(data,"012",3u) == 0);
    CHECK(nexs_pread(alias,data,3,-1) == 3 &&
          memcmp(data,"345",3u) == 0);
    /* Explicit-position pread must not alter the shared sequential cursor. */
    CHECK(nexs_pread(original,data,2,0) == 2 &&
          memcmp(data,"01",2u) == 0);
    CHECK(nexs_pread(alias,data,3,-1) == 3 &&
          memcmp(data,"678",3u) == 0);
    CHECK(nexs_close(original) == 0);
    CHECK(nexs_close(original) == -1);
    CHECK(nexs_pread(alias,data,3,-1) == 3 &&
          memcmp(data,"9ab",3u) == 0);
    CHECK(nexs_seek(alias,2,0) == 2);
    CHECK(nexs_pread(alias,data,3,-1) == 3 &&
          memcmp(data,"234",3u) == 0);
    CHECK(nexs_close(alias) == 0);
    CHECK(nexs_pread(alias,data,3,-1) == -1);
    return 0;
}

static int replacement_and_offset_lifetime(void) {
    int fd0 = nexs_open(fixture_path, NEXS_OREAD);
    int fd1 = nexs_open(fixture_path, NEXS_OREAD);
    CHECK(fd0 >= 3 && fd1 >= 3 && fd1 != fd0);
    char data[5] = {0};
    CHECK(nexs_pread(fd0,data,4,-1) == 4 &&
          memcmp(data,"0123",4u) == 0);
    CHECK(nexs_pread(fd1,data,4,-1) == 4 &&
          memcmp(data,"0123",4u) == 0);
    CHECK(nexs_dup(fd0,fd1) == fd1);
    CHECK(nexs_pread(fd1,data,4,-1) == 4 &&
          memcmp(data,"4567",4u) == 0);
    CHECK(nexs_close(fd0) == 0);
    CHECK(nexs_pread(fd1,data,4,-1) == 4 &&
          memcmp(data,"89ab",4u) == 0);
    CHECK(nexs_close(fd1) == 0);
    return 0;
}

static int write_only_explicit_offset_no_append(void) {
    int owner = nexs_open(fixture_path, NEXS_OWRITE);
    CHECK(owner >= 3);
    CHECK(nexs_pwrite(owner,"Q",1,2) == 1);
    CHECK(nexs_close(owner) == 0);
    owner = nexs_open(fixture_path, NEXS_OREAD);
    CHECK(owner >= 3);
    char contents[17] = {0};
    CHECK(nexs_pread(owner,contents,16,-1) == 16);
    CHECK(memcmp(contents,"01Q3456789abcdef",16u) == 0);
    CHECK(nexs_close(owner) == 0);
    owner = nexs_open(fixture_path, NEXS_OWRITE);
    CHECK(owner >= 3);
    CHECK(nexs_pwrite(owner,"R",1,-1) == 1);
    CHECK(nexs_close(owner) == 0);
    owner = nexs_open(fixture_path, NEXS_OREAD);
    CHECK(owner >= 3);
    CHECK(nexs_pread(owner,contents,16,-1) == 16);
    CHECK(memcmp(contents,"R1Q3456789abcdef",16u) == 0);
    CHECK(nexs_close(owner) == 0);
    return 0;
}

static int descriptor_growth_and_live_path(void) {
    enum { FDS = 144 };
    int fds[FDS];
    for (int i = 0; i < FDS; ++i) {
        fds[i] = nexs_open(fixture_path,NEXS_OREAD);
        CHECK(fds[i] >= 3);
    }
    CHECK(fds[FDS - 1] > 64);
    for (int i = 0; i < FDS; ++i) {
        char path[128];
        CHECK(nexs_fd2path(fds[i],path,(int)sizeof(path)) == 0);
        CHECK(strcmp(path,fixture_path) == 0);
        char too_small[4] = {'Q','Q','Q','Q'};
        CHECK(nexs_fd2path(fds[i],too_small,(int)sizeof(too_small)) == -1);
        CHECK(memcmp(too_small,"QQQQ",4u) == 0);
        CHECK(nexs_close(fds[i]) == 0);
    }
    CHECK(nexs_open(NULL,NEXS_OREAD) == -1);
    CHECK(nexs_open(fixture_path,15) == -1);
    CHECK(nexs_dup(-4,-1) == -1);
    CHECK(nexs_dup(-4,4) == -1);
    CHECK(nexs_dup(3,-2) == -1);
    /* Retired per-FD paths must not constitute a second live registry. */
    CHECK(reg_lookup("/sys/fd/3") == NULL);
    return 0;
}

static int live_pipe_dup_and_close(void) {
    int ends[2] = {-1,-1};
    CHECK(nexs_pipe(ends) == 0 && ends[0] >= 3 && ends[1] >= 3);
    int writer_alias = nexs_dup(ends[1], -1);
    CHECK(writer_alias >= 3 && writer_alias != ends[1]);
    CHECK(nexs_close(ends[1]) == 0);
    CHECK(nexs_pwrite(writer_alias,"pipe\n",5,-1) == 5);
    char buffer[16] = {0};
    CHECK(nexs_pread(ends[0],buffer,5,-1) == 5 &&
          memcmp(buffer,"pipe\n",5u) == 0);
    CHECK(nexs_close(writer_alias) == 0);
    CHECK(nexs_close(ends[0]) == 0);
    return 0;
}

/* Race a REAL pipe read against descriptor close. The alias keeps the
 * provider alive whether the contested read borrowed before or after close,
 * so we exercise both legal linearization orders without brittle sleeps. */
typedef struct ReaderRace {
    int fd;
    _Atomic uint32_t started;
    int result;
    char byte;
} ReaderRace;

static void *concurrent_reader(void *argument) {
    ReaderRace *race = (ReaderRace *)argument;
    atomic_store_explicit(&race->started, 1u, memory_order_release);
    race->result = nexs_pread(race->fd, &race->byte, 1, -1);
    return NULL;
}

static int concurrent_close_vs_borrow(void) {
    for (int round = 0; round < 96; ++round) {
        int ends[2];
        CHECK(nexs_pipe(ends) == 0);
        int alias = nexs_dup(ends[0], -1);
        CHECK(alias >= 3);
        ReaderRace race = {ends[0], 0u, -777, '?'};
        pthread_t reader;
        CHECK(pthread_create(&reader,NULL,concurrent_reader,&race) == 0);
        for (int retry = 0; retry < 100000 &&
            atomic_load_explicit(&race.started,memory_order_acquire) == 0u;
            ++retry)
            (void)sched_yield();
        CHECK(atomic_load_explicit(&race.started,memory_order_acquire) == 1u);
        int closed = -1;
        for (int retry = 0; retry < 64 && closed != 0; ++retry) {
            closed = nexs_close(ends[0]);
            if (closed != 0)
                (void)sched_yield();
        }
        CHECK(closed == 0);
        CHECK(nexs_pwrite(ends[1],"R",1,-1) == 1);
        CHECK(pthread_join(reader,NULL) == 0);
        CHECK(race.result == -1 || race.result == 1);
        if (race.result == 1) {
            CHECK(race.byte == 'R');
        } else {
            char remaining = '?';
            CHECK(nexs_pread(alias,&remaining,1,-1) == 1);
            CHECK(remaining == 'R');
        }
        CHECK(nexs_close(alias) == 0);
        CHECK(nexs_close(ends[1]) == 0);
    }
    return 0;
}

static int create_is_actual_host_file(void) {
    int owner = nexs_create(fixture_path, NEXS_ORDWR,0600);
    CHECK(owner >= 3);
    CHECK(nexs_pwrite(owner,"new-data",8,-1) == 8);
    CHECK(nexs_seek(owner,0,0) == 0);
    char buffer[16] = {0};
    CHECK(nexs_pread(owner,buffer,8,-1) == 8 &&
          memcmp(buffer,"new-data",8u) == 0);
    CHECK(nexs_seek(owner,0,0) == 0);
    CHECK(nexs_pread(owner,buffer,1,-1) == 1 && buffer[0] == 'n');
    CHECK(nexs_pwrite(owner,"X",1,-1) == 1);
    CHECK(nexs_seek(owner,0,0) == 0);
    CHECK(nexs_pread(owner,buffer,8,-1) == 8 &&
          memcmp(buffer,"nXw-data",8u) == 0);
    CHECK(nexs_close(owner) == 0);
    int reopened = nexs_open(fixture_path,NEXS_OREAD);
    CHECK(reopened >= 3);
    CHECK(nexs_pread(reopened,buffer,8,-1) == 8 &&
          memcmp(buffer,"nXw-data",8u) == 0);
    CHECK(nexs_close(reopened) == 0);
    return 0;
}

int main(void) {
    CHECK(nexs_runtime_init() == 0);
    CHECK(setup_file() == 0);
    CHECK(shared_open_description_semantics() == 0);
    CHECK(replacement_and_offset_lifetime() == 0);
    CHECK(write_only_explicit_offset_no_append() == 0);
    CHECK(descriptor_growth_and_live_path() == 0);
    CHECK(live_pipe_dup_and_close() == 0);
    CHECK(concurrent_close_vs_borrow() == 0);
    CHECK(create_is_actual_host_file() == 0);
    CHECK(unlink(fixture_path) == 0);
    puts("SYSIO_FD_OWNER_PASS shared_cursor=1 dup2=1 close_once=1 "
         "dynamic_over_64=1 real_pipe=1 race_96=1 real_create=1");
    return 0;
}
