#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>

#include "../../runtime/include/nexs_runtime.h"
#include "../../lang/include/nexs_eval.h"
#include "../../core/include/nexs_common.h"
#include "../../core/include/nexs_alloc.h"
#include "../../lang/include/nexs_lex.h"
#include "../../lang/include/nexs_ast.h"
#include "nexsd_protocol.h"
#include "../../compiler/include/nexs_compiler.h"
#include "../../lang/include/nexs_fn.h"

#define MAX_SYMBOLS 16384

/* Prototype */
void scan_symbols_in_ast(ASTNode *node, const char *filename);

typedef struct {
    char name[NAME_LEN];
    char filename[512];
    int line;
    int col;
    int n_params;
    int is_c;
} Symbol;

Symbol symbol_table[MAX_SYMBOLS];
int symbol_count = 0;

void add_symbol(const char *name, const char *file, int line, int col, int params, int is_c) {
    for (int i = 0; i < symbol_count; i++) {
        if (strcmp(symbol_table[i].name, name) == 0 && strcmp(symbol_table[i].filename, file) == 0) return;
    }
    if (symbol_count < MAX_SYMBOLS) {
        strncpy(symbol_table[symbol_count].name, name, NAME_LEN - 1);
        strncpy(symbol_table[symbol_count].filename, file, 511);
        symbol_table[symbol_count].line = line;
        symbol_table[symbol_count].col = col;
        symbol_table[symbol_count].n_params = params;
        symbol_table[symbol_count].is_c = is_c;
        symbol_count++;
    }
}

/* Deep C Scanner: trova la registrazione E la definizione della funzione */
void scan_c_file_deep(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char *content = NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    content = malloc(sz + 1);
    fread(content, 1, sz, f);
    content[sz] = '\0';
    fclose(f);

    char abs_path[1024];
    realpath(path, abs_path);

    char *p = content;
    while ((p = strstr(p, "fn_register_builtin_sig("))) {
        char *start = p;
        p += 24;
        while (*p && (*p == ' ' || *p == '\t' || *p == '"')) p++;
        char nx_name[NAME_LEN] = {0};
        int i = 0;
        while (*p && *p != '"' && i < NAME_LEN - 1) nx_name[i++] = *p++;
        
        /* Cerca il nome della funzione C (secondo argomento) */
        p = strchr(p, ',');
        if (p) {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            char c_fn_name[NAME_LEN] = {0};
            i = 0;
            while (*p && *p != ',' && *p != ' ' && *p != '\t' && *p != ')' && i < NAME_LEN - 1)
                c_fn_name[i++] = *p++;
            
            /* Ora cerca la definizione di c_fn_name nel file */
            char pattern[128];
            snprintf(pattern, sizeof(pattern), "Value %s(", c_fn_name);
            char *def = strstr(content, pattern);
            if (!def) {
                snprintf(pattern, sizeof(pattern), "Value\n%s(", c_fn_name);
                def = strstr(content, pattern);
            }
            
            int line_num = 1;
            if (def) {
                char *it = content;
                while (it < def) { if (*it == '\n') line_num++; it++; }
            } else {
                /* Fallback alla riga di registrazione */
                char *it = content;
                while (it < start) { if (*it == '\n') line_num++; it++; }
            }
            add_symbol(nx_name, abs_path, line_num, 1, 0, 1);
        }
    }
    free(content);
}

void scan_project_c(const char *root) {
    const char *dirs[] = { "lang", "sys", "hal", "registry", "core", "runtime" };
    for (int i = 0; i < 6; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", root, dirs[i]);
        DIR *d = opendir(path);
        if (!d) continue;
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strstr(ent->d_name, ".c")) {
                char fpath[1024];
                snprintf(fpath, sizeof(fpath), "%s/%s", path, ent->d_name);
                scan_c_file_deep(fpath);
            }
        }
        closedir(d);
    }
}

int count_nx_files(const char *root) {
    int count = 0;
    DIR *d = opendir(root);
    if (!d) return 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_type == DT_DIR) {
            if (ent->d_name[0] != '.') {
                char path[1024];
                snprintf(path, sizeof(path), "%s/%s", root, ent->d_name);
                count += count_nx_files(path);
            }
        } else if (strstr(ent->d_name, ".nx")) count++;
    }
    closedir(d);
    return count;
}

void handle_client(int client_fd) {
    char *buffer = malloc(BUF_SIZE);
    ssize_t n = read(client_fd, buffer, BUF_SIZE - 1);
    if (n <= 0) { free(buffer); return; }
    buffer[n] = '\0';

    if (strcmp(buffer, CMD_SHUTDOWN) == 0) {
        printf("Spegnimento ordinato richiesto...\n");
        unlink(SOCKET_PATH);
        unlink(LOCK_FILE);
        exit(0);
    }

    char filename[MAX_FILENAME];
    n = read(client_fd, filename, MAX_FILENAME);
    if (n <= 0) { free(buffer); return; }
    
    /* Read the rest of the content */
    int total_read = 0;
    while (total_read < BUF_SIZE - 1) {
        ssize_t rd = read(client_fd, buffer + total_read, BUF_SIZE - 1 - total_read);
        if (rd <= 0) break;
        total_read += rd;
    }
    buffer[total_read] = '\0';
    char *content = buffer;

    /* Check if file ends with .nx */
    int flen = strlen(filename);
    int is_nx = (flen > 3 && strcmp(filename + flen - 3, ".nx") == 0);

    if (!is_nx) {
        write(client_fd, "OK\n", 3);
        free(buffer);
        return;
    }

    /* Reset runtime to baseline (built-ins) */
    void nexs_runtime_reset_to_baseline(void);
    nexs_runtime_reset_to_baseline();

    /* Reset symbol table to baseline (C symbols) */
    int nx_start = 0;
    while (nx_start < symbol_count && symbol_table[nx_start].is_c) nx_start++;
    symbol_count = nx_start;

    NexsDepEntry deps[128];
    int n_deps = nexs_scan_deps(filename, deps, 128);

    int pipe_fds[2];
    pipe(pipe_fds);
    int old_stderr = dup(STDERR_FILENO);
    dup2(pipe_fds[1], STDERR_FILENO);
    close(pipe_fds[1]);

    /* Register dependencies in fn_table so the parser can resolve calls */
    void register_fns_in_fn_table(ASTNode *node);
    for (int i = 0; i < n_deps; i++) {
        if (deps[i].src) {
            Lexer dlex; Parser dpar;
            lexer_init(&dlex, deps[i].src);
            parser_init(&dpar, &dlex, deps[i].path);
            ASTNode *dprog = parse_program(&dpar);
            register_fns_in_fn_table(dprog);
            scan_symbols_in_ast(dprog, deps[i].path);
            ast_free_safe(dprog);
        }
    }

    g_nexs_lint_mode = 1;
    Lexer lex;
    Parser par;
    lexer_init(&lex, content);
    parser_init(&par, &lex, filename);
    ASTNode *prog = parse_program(&par);
    
    /* Scansione simboli (NeXs) */
    scan_symbols_in_ast(prog, filename);

    nexs_free_deps(deps, n_deps);

    if (par.had_error) fprintf(stderr, "%s\n", par.error_msg);
    ast_free_safe(prog);
    fflush(stderr);
    dup2(old_stderr, STDERR_FILENO);
    close(old_stderr);
    
    char err_buf[16384];
    ssize_t err_n = read(pipe_fds[0], err_buf, sizeof(err_buf) - 1);
    close(pipe_fds[0]);

    char sym_buf[16384];
    int sym_pos = snprintf(sym_buf, sizeof(sym_buf), "---SYMBOLS---\n");
    for (int i = 0; i < symbol_count; i++) {
        sym_pos += snprintf(sym_buf + sym_pos, sizeof(sym_buf) - sym_pos,
                            "%s:%s:%d:%d:%d:%d\n", symbol_table[i].name, symbol_table[i].filename,
                            symbol_table[i].line, symbol_table[i].col, symbol_table[i].n_params, symbol_table[i].is_c);
    }

    if (err_n > 0) { err_buf[err_n] = '\0'; write(client_fd, err_buf, (size_t)err_n); }
    else { write(client_fd, "OK\n", 3); }
    write(client_fd, sym_buf, (size_t)strlen(sym_buf));
    free(buffer);
}

static int baseline_fn_count = 0;
void nexs_runtime_reset_to_baseline(void) {
    extern int g_fn_count;
    g_fn_count = baseline_fn_count;
}

void register_fns_in_fn_table(ASTNode *node) {
    if (!node) return;
    if (node->kind == AST_FN_DECL) {
        char params[MAX_PARAMS][NAME_LEN];
        memset(params, 0, sizeof(params));
        for (int i = 0; i < node->n_params; i++) strncpy(params[i], node->params[i], NAME_LEN - 1);
        fn_register(node->name, NULL, params, node->n_params);
    }
    register_fns_in_fn_table(node->left);
    register_fns_in_fn_table(node->right);
    if (node->kind == AST_BLOCK || node->kind == AST_PROGRAM) {
        for (int i = 0; i < node->n_args; i++) register_fns_in_fn_table(node->args[i]);
    }
}

void scan_symbols_in_ast(ASTNode *node, const char *filename) {
    if (!node) return;
    if (node->kind == AST_FN_DECL) add_symbol(node->name, filename, node->tok.line, node->tok.col, node->n_params, 0);
    scan_symbols_in_ast(node->left, filename);
    scan_symbols_in_ast(node->right, filename);
    if (node->kind == AST_BLOCK || node->kind == AST_PROGRAM) {
        for (int i = 0; i < node->n_args; i++) scan_symbols_in_ast(node->args[i], filename);
    }
}

int main(int argc, char *argv[]) {
    /* Controllo istanza singola */
    int lock_fd = open(LOCK_FILE, O_RDWR | O_CREAT, 0666);
    if (lock_fd < 0 || lockf(lock_fd, F_TLOCK, 0) < 0) {
        fprintf(stderr, "Un'altra istanza di nexsd è già in esecuzione.\n");
        return 0;
    }

    nexs_runtime_init();
    
    /* Modalità Standby se non ci sono file .nx */
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    while (count_nx_files(cwd) == 0) {
        printf("Nessun file .nx trovato. standby per 60 secondi...\n");
        sleep(60);
    }

    scan_project_c(cwd);
    extern int g_fn_count;
    baseline_fn_count = g_fn_count;
    printf("nexsd indexed %d C built-ins\n", symbol_count);

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    unlink(SOCKET_PATH);
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 5);
    
    printf("nexsd ready and monitoring...\n");

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;
        handle_client(client_fd);
        close(client_fd);
    }
    return 0;
}
