/*
 * basereg.h — NEXS Runtime Public Header
 * ========================================
 * Espone tutta la superficie API del runtime NEXS:
 *   1. Buddy Allocator  (memory pool 1MB, MIN_BLOCK=32)
 *   2. Value System     (tipi estesi: INT FLOAT STR ARR FN ERR)
 *   3. DynArray         (array tipizzati a crescita esponenziale)
 *   4. RegKey / Reg     (registro gerarchico — il "linker regedit")
 *   5. Lexer / AST      (tokenizer e nodi AST per l'interprete)
 *   6. Evaluator        (API pubblica dell'albero di valutazione)
 *   7. Plan 9 Syscalls  (file I/O, process control, namespace)
 *
 * Compilazione:
 *   cc -O2 -std=c11 -Wall -Wextra -Iinclude -o nexs src/[all].c
 *
 * Non ci sono dipendenze esterne oltre la libc.
 */

#ifndef BASEREG_H
#define BASEREG_H
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   Visibility / Export Macro
   ========================================================= */

#ifndef NEXS_API
#  if defined(_WIN32) || defined(__CYGWIN__)
#    ifdef NEXS_BUILDING
#      define NEXS_API __declspec(dllexport)
#    else
#      define NEXS_API __declspec(dllimport)
#    endif
#  elif defined(__GNUC__) && __GNUC__ >= 4
#    define NEXS_API __attribute__((visibility("default")))
#  else
#    define NEXS_API
#  endif
#endif

/* =========================================================
   SEZIONE 1 — COSTANTI GLOBALI
   ========================================================= */

#define NEXS_VERSION_MAJOR 0
#define NEXS_VERSION_MINOR 2
#define NEXS_VERSION_PATCH 0

/* Buddy Allocator */
#define POOL_SIZE (4 * 1024 * 1024)        /* 4 MB */
#define MIN_BLOCK 32                       /* blocco minimo 32 byte */
#define NUM_LEAVES (POOL_SIZE / MIN_BLOCK) /* 32768 foglie */
#define TREE_NODES (2 * NUM_LEAVES - 1)    /* 65535 nodi totali */

/* DynArray */
#define MAX_ARRAYS 64
#define NAME_LEN 64

/* Registry */
#define REG_PATH_MAX 256 /* lunghezza massima di un percorso registro */
#define REG_CHILDREN 16  /* figli diretti massimi per nodo (espandibile) */
#define REG_ROOT "/"     /* radice del registro */

/* Diritti di accesso registro (stile capability) */
#define RK_READ  (1 << 0)
#define RK_WRITE (1 << 1)
#define RK_EXEC  (1 << 2)
#define RK_ADMIN (1 << 3) /* crea/elimina sottochiavi */
#define RK_ALL   (RK_READ | RK_WRITE | RK_EXEC | RK_ADMIN)

/* Interprete */
#define MAX_TOKENS 4096
#define MAX_IDENT_LEN NAME_LEN
#define MAX_STR_LEN 512
#define MAX_CALL_DEPTH 128 /* stack di chiamata massimo */
#define MAX_PARAMS 16      /* parametri per funzione */

/* Plan 9 File Descriptors */
#define NEXS_MAX_FDS 64    /* massimo file descriptor aperti */

/* =========================================================
   SEZIONE 2 — BUDDY ALLOCATOR
   ========================================================= */

/*
 * Stati dell'albero buddy:
 *   BNODE_FREE  (0) — blocco completamente libero
 *   BNODE_SPLIT (1) — blocco diviso (i figli contengono dati)
 *   BNODE_USED  (2) — blocco occupato interamente
 */
typedef enum { BNODE_FREE = 0, BNODE_SPLIT = 1, BNODE_USED = 2 } BuddyNodeState;

extern uint8_t memory_pool[POOL_SIZE];
extern uint8_t buddy_tree[TREE_NODES];

/* Arrotonda x alla potenza di 2 successiva >= MIN_BLOCK */
NEXS_API size_t buddy_next_pow2(size_t x);

/* Alloca 'size' byte dal pool. Ritorna NULL se OOM. */
NEXS_API void *buddy_alloc(size_t size);

/* Libera il blocco puntato da ptr (deve essere stato allocato con buddy_alloc). */
NEXS_API void buddy_free(void *ptr);

/* Alloca e azzera (equivalente a calloc). Chiama die() se fallisce. */
NEXS_API void *xmalloc(size_t size);

/* Alias di buddy_free per simmetria con xmalloc. */
NEXS_API void xfree(void *ptr);

/* Stampa su 'out' le statistiche del pool (blocchi liberi/usati per livello). */
NEXS_API void buddy_dump_stats(FILE *out);

/* =========================================================
   SEZIONE 3 — SISTEMA DI VALORI
   ========================================================= */

typedef enum {
  TYPE_NIL = 0,   /* valore assente / non inizializzato */
  TYPE_INT = 1,   /* intero a 64 bit con segno */
  TYPE_FLOAT = 2, /* double IEEE 754 */
  TYPE_STR = 3,   /* stringa C null-terminated (buddy_alloc'd) */
  TYPE_ARR = 4,   /* DynArray* (array dinamico tipizzato) */
  TYPE_FN = 5,    /* puntatore ad ASTNode funzione */
  TYPE_ERR = 6,   /* valore di errore: codice + messaggio */
  TYPE_BOOL = 7,  /* 0 = false, 1 = true */
  TYPE_REF = 8,   /* riferimento a RegKey (percorso registro) */
} ValueType;

/*
 * Value — il tipo fondamentale del runtime.
 * Ogni variabile, slot di array, parametro e risultato è un Value.
 * La memoria di 'data' appartiene al buddy pool (non usare malloc/free).
 */
typedef struct {
  ValueType type;
  void *data;    /* punta al dato buddy_alloc'd; NULL per NIL/BOOL */
  int64_t ival;  /* per TYPE_INT e TYPE_BOOL: valore inline (evita alloc) */
  double fval;   /* per TYPE_FLOAT: valore inline */
  int err_code;  /* per TYPE_ERR: codice errore (0 = ok) */
  char *err_msg; /* per TYPE_ERR: messaggio (buddy_alloc'd) */
} Value;

/* Costruttori rapidi */
NEXS_API Value val_nil(void);
NEXS_API Value val_bool(int b);
NEXS_API Value val_int(int64_t n);
NEXS_API Value val_float(double f);
NEXS_API Value val_str(const char *s); /* copia s nel buddy pool */
NEXS_API Value val_err(int code, const char *msg);
NEXS_API Value val_ref(const char *path); /* riferimento a percorso registro */

/* Predicati */
NEXS_API int val_is_truthy(const Value *v);
NEXS_API int val_is_error(const Value *v);
NEXS_API int val_equal(const Value *a, const Value *b);

/* Operazioni aritmetiche — ritornano TYPE_ERR se i tipi sono incompatibili */
NEXS_API Value val_add(const Value *a, const Value *b);
NEXS_API Value val_sub(const Value *a, const Value *b);
NEXS_API Value val_mul(const Value *a, const Value *b);
NEXS_API Value val_div(const Value *a, const Value *b);
NEXS_API Value val_mod(const Value *a, const Value *b);
NEXS_API Value val_lt(const Value *a, const Value *b);
NEXS_API Value val_gt(const Value *a, const Value *b);
NEXS_API Value val_le(const Value *a, const Value *b);
NEXS_API Value val_ge(const Value *a, const Value *b);
NEXS_API Value val_eq(const Value *a, const Value *b);
NEXS_API Value val_ne(const Value *a, const Value *b);
NEXS_API Value val_and(const Value *a, const Value *b);
NEXS_API Value val_or(const Value *a, const Value *b);
NEXS_API Value val_not(const Value *a);

/* Conversioni */
NEXS_API int64_t val_to_int(const Value *v);
NEXS_API double val_to_float(const Value *v);
NEXS_API const char *val_type_name(ValueType t);

/* Stampa un Value su 'out' */
NEXS_API void val_print(const Value *v, FILE *out);

/* Libera le risorse buddy_alloc'd di un Value */
NEXS_API void val_free(Value *v);

/* Copia profonda di un Value (il dato viene buddy_alloc'd di nuovo) */
NEXS_API Value val_clone(const Value *v);

/* =========================================================
   SEZIONE 4 — DYNARRAY
   ========================================================= */

typedef struct {
  char name[NAME_LEN];
  Value *items;    /* buddy_alloc'd */
  size_t size;     /* numero di elementi (inclusi i NULL tra gap) */
  size_t capacity; /* slot allocati */
} DynArray;

/* Tabella globale degli array (per compatibilità col REPL base) */
extern DynArray *g_arrays[MAX_ARRAYS];
extern size_t g_array_count;

NEXS_API DynArray *arr_get_or_create(const char *name);
NEXS_API DynArray *arr_get(const char *name);
NEXS_API void arr_ensure_cap(DynArray *arr, size_t index);
NEXS_API void arr_set(DynArray *arr, size_t index, Value val);
NEXS_API Value arr_get_at(DynArray *arr, size_t index);
NEXS_API void arr_delete(DynArray *arr, size_t index);
NEXS_API void arr_print(DynArray *arr, FILE *out);
NEXS_API void arr_free(DynArray *arr);

/* =========================================================
   SEZIONE 5 — REGISTRO GERARCHICO (REGEDIT LINKER)
   ========================================================= */

typedef struct RegKey RegKey;

struct RegKey {
  char name[NAME_LEN];     /* nome del segmento (es. "local", "x", "add") */
  char path[REG_PATH_MAX]; /* percorso completo (es. "/local/x") */
  Value val;               /* valore contenuto */
  uint8_t rights;          /* RK_READ | RK_WRITE | RK_EXEC | RK_ADMIN */
  RegKey *parent;          /* padre nel registro */
  RegKey *children;        /* lista figli (linked via 'next') */
  RegKey *next;            /* fratello successivo nella lista del padre */
};

typedef struct {
  RegKey *root; /* nodo "/" */
  size_t total_keys;
} Registry;

extern Registry g_registry;

NEXS_API void reg_init(void);
NEXS_API RegKey *reg_mkpath(const char *path, uint8_t rights);
NEXS_API RegKey *reg_lookup(const char *path);
NEXS_API RegKey *reg_resolve(const char *name, const char *scope_path);
NEXS_API int reg_set(const char *path, Value val, uint8_t rights);
NEXS_API Value reg_get(const char *path);
NEXS_API int reg_delete(const char *path);
NEXS_API void reg_ls(const char *path, FILE *out);
NEXS_API void reg_ls_recursive(const char *path, FILE *out, int depth);

/* Sposta/rinomina una chiave e tutti i suoi figli. */
NEXS_API int reg_move(const char *src, const char *dst);

/*
 * "Mount" — inserisce il sottoalbero di 'src_path' come figlio di 'dst_path'.
 * Simile al mount(2) di Plan 9: rende visibile un namespace sotto un altro punto.
 * Il flag 'before' controlla l'ordine: 1 = prima dei figli esistenti, 0 = dopo.
 */
NEXS_API int reg_mount(const char *src_path, const char *dst_path, int before);

/*
 * "Unmount" — rimuove un mount precedente.
 * Se src_path è NULL, rimuove tutti i mount da dst_path.
 */
NEXS_API int reg_unmount(const char *src_path, const char *dst_path);

/*
 * "Bind" — lega src_path a dst_path (union mount in stile Plan 9).
 * flag: 0=replace, 1=before, 2=after
 */
NEXS_API int reg_bind(const char *src_path, const char *dst_path, int flag);

/* Scope management */
NEXS_API char *reg_push_scope(const char *fn_name);
NEXS_API void reg_pop_scope(const char *scope_path);

/* Stampa il percorso completo di una chiave */
NEXS_API void reg_key_print(RegKey *k, FILE *out);

/* =========================================================
   SEZIONE 6 — LEXER
   ========================================================= */

typedef enum {
  TK_EOF = 0,

  /* Letterali */
  TK_INT,     /* 42, -7 */
  TK_FLOAT,   /* 3.14 */
  TK_STRING,  /* "hello" */
  TK_IDENT,   /* nome variabile/funzione */
  TK_REGPATH, /* /path/nel/registro */

  /* Operatori */
  TK_PLUS,    /* +  */
  TK_MINUS,   /* -  */
  TK_STAR,    /* *  */
  TK_SLASH,   /* /  (solo se non inizia un path) */
  TK_PERCENT, /* %  */
  TK_EQ,      /* =  */
  TK_EQEQ,    /* == */
  TK_NEQ,     /* != */
  TK_LT,      /* <  */
  TK_GT,      /* >  */
  TK_LE,      /* <= */
  TK_GE,      /* >= */
  TK_AND,     /* && */
  TK_OR,      /* || */
  TK_NOT,     /* !  */
  TK_DOT,     /* .  */

  /* Delimitatori */
  TK_LBRACKET, /* [ */
  TK_RBRACKET, /* ] */
  TK_LBRACE,   /* { */
  TK_RBRACE,   /* } */
  TK_LPAREN,   /* ( */
  TK_RPAREN,   /* ) */
  TK_COMMA,    /* , */
  TK_NEWLINE,  /* \n (statement terminator) */

  /* Parole chiave */
  TK_KW_FN,     /* fn */
  TK_KW_RET,    /* ret */
  TK_KW_IF,     /* if */
  TK_KW_ELSE,   /* else */
  TK_KW_LOOP,   /* loop */
  TK_KW_BREAK,  /* break */
  TK_KW_CONT,   /* cont (continue) */
  TK_KW_DEL,    /* del */
  TK_KW_OUT,    /* out (print) */
  TK_KW_REG,    /* reg (accesso diretto al registro) */
  TK_KW_LS,     /* ls  (lista registro) */
  TK_KW_IMPORT, /* import */
  TK_KW_PROC,   /* proc (processo) */
  TK_KW_AND,    /* and */
  TK_KW_OR,     /* or */
  TK_KW_NOT,    /* not */
  TK_KW_TRUE,   /* true */
  TK_KW_FALSE,  /* false */
  TK_KW_NIL,    /* nil */
} TokenKind;

typedef struct {
  TokenKind kind;
  char text[MAX_STR_LEN]; /* testo grezzo del token */
  int64_t ival;           /* per TK_INT */
  double fval;            /* per TK_FLOAT */
  int line;               /* numero di riga (per errori) */
  int col;                /* colonna */
} Token;

typedef struct {
  const char *src; /* sorgente completo */
  size_t pos;      /* posizione corrente */
  size_t len;      /* lunghezza sorgente */
  int line;
  int col;
  Token peeked; /* lookahead di un token */
  int has_peek;
} Lexer;

NEXS_API void lexer_init(Lexer *lex, const char *src);
NEXS_API Token lexer_next(Lexer *lex);
NEXS_API Token lexer_peek(Lexer *lex);
NEXS_API const char *token_kind_name(TokenKind k);

/* =========================================================
   SEZIONE 7 — AST
   ========================================================= */

typedef enum {
  AST_PROGRAM,      /* lista di statement */
  AST_ASSIGN,       /* name = expr  o  name[idx] = expr */
  AST_INDEX_ASSIGN, /* arr[idx] = val */
  AST_IDENT,        /* nome variabile */
  AST_INDEX,        /* arr[idx] */
  AST_REG_ACCESS,   /* reg /path */
  AST_INT_LIT,      /* letterale intero */
  AST_FLOAT_LIT,    /* letterale float */
  AST_STR_LIT,      /* letterale stringa */
  AST_BOOL_LIT,     /* true / false */
  AST_NIL_LIT,      /* nil */
  AST_BINOP,        /* a OP b */
  AST_UNOP,         /* !a  -a */
  AST_FN_DECL,      /* fn name(p1 p2) { body } */
  AST_FN_CALL,      /* name(arg1 arg2) */
  AST_IF,           /* if cond { then } [else { els }] */
  AST_LOOP,         /* loop { body } */
  AST_BREAK,        /* break */
  AST_CONT,         /* cont */
  AST_RET,          /* ret expr */
  AST_DEL,          /* del name[idx] */
  AST_OUT,          /* out expr */
  AST_REG_SET,      /* reg /path = expr */
  AST_REG_LS,       /* ls /path */
  AST_BLOCK,        /* { stmt... } */
} ASTKind;

typedef struct ASTNode ASTNode;
struct ASTNode {
  ASTKind kind;
  Token tok;               /* token sorgente (per messaggi di errore) */
  Value litval;            /* per nodi letterale */
  char name[NAME_LEN];     /* per IDENT, FN_DECL, FN_CALL, ASSIGN */
  char path[REG_PATH_MAX]; /* per REG_ACCESS, REG_SET */
  char op[4];              /* per BINOP/UNOP: "+", "-", "==", ecc. */
  char params[MAX_PARAMS][NAME_LEN]; /* per FN_DECL */
  int n_params;
  ASTNode *left;             /* operando sinistro / condizione / lvalue */
  ASTNode *right;            /* operando destro / body / rhs */
  ASTNode *args[MAX_PARAMS]; /* argomenti di chiamata */
  int n_args;
  ASTNode *children; /* primo figlio (per PROGRAM, BLOCK) */
  ASTNode *next;     /* fratello successivo in una lista */
  ASTNode *alt;      /* ramo else per IF */
};

NEXS_API ASTNode *ast_alloc(ASTKind kind, Token tok);
NEXS_API void ast_print(ASTNode *node, FILE *out, int depth);
NEXS_API void ast_free(ASTNode *node);

/* =========================================================
   SEZIONE 8 — PARSER
   ========================================================= */

typedef struct {
  Lexer *lex;
  Token cur;
  Token peek;
  int had_error;
  char error_msg[256];
} Parser;

NEXS_API void parser_init(Parser *p, Lexer *lex);
NEXS_API ASTNode *parse_program(Parser *p);
NEXS_API ASTNode *parse_stmt(Parser *p);

/* =========================================================
   SEZIONE 9 — EVALUATOR (INTERPRETE TREE-WALKING)
   ========================================================= */

typedef enum {
  CTRL_NONE = 0,
  CTRL_BREAK,
  CTRL_CONT,
  CTRL_RET,
  CTRL_ERR,
} CtrlSignal;

typedef struct {
  CtrlSignal sig;
  Value ret_val; /* valore di ritorno per CTRL_RET */
} EvalResult;

typedef struct {
  char scope[REG_PATH_MAX]; /* percorso scope corrente in /local/ */
  int call_depth;
  int debug; /* 1 = stampa ogni passo di valutazione */
  FILE *out; /* output stream (default: stdout) */
  FILE *err; /* error stream (default: stderr) */
} EvalCtx;

NEXS_API void eval_ctx_init(EvalCtx *ctx);
NEXS_API EvalResult eval(EvalCtx *ctx, ASTNode *node);
NEXS_API EvalResult eval_str(EvalCtx *ctx, const char *src);
NEXS_API EvalResult eval_file(EvalCtx *ctx, const char *path);

/* =========================================================
   SEZIONE 10 — PLAN 9 SYSCALLS (FILE I/O)
   ========================================================= */

/*
 * Tabella dei file descriptor — ogni fd è un indice in questa tabella.
 * Modello ispirato a Plan 9: open/create/close/pread/pwrite.
 */
typedef struct {
  FILE *fp;              /* handle C stdio */
  char path[REG_PATH_MAX]; /* percorso del file */
  int in_use;            /* 1 = fd attivo */
  int flags;             /* O_READ, O_WRITE, O_RDWR */
} NexsFd;

/* Flags per open (stile Plan 9) */
#define NEXS_OREAD   0
#define NEXS_OWRITE  1
#define NEXS_ORDWR   2
#define NEXS_OTRUNC  16

extern NexsFd g_fd_table[NEXS_MAX_FDS];

/* Inizializza la fd table (stdin=0, stdout=1, stderr=2) */
NEXS_API void sysio_init(void);

/* Plan 9 syscall: open */
NEXS_API int nexs_open(const char *path, int mode);

/* Plan 9 syscall: create */
NEXS_API int nexs_create(const char *path, int mode, int perm);

/* Plan 9 syscall: close */
NEXS_API int nexs_close(int fd);

/* Plan 9 syscall: pread — read n bytes at offset (offset<0 = current pos) */
NEXS_API int nexs_pread(int fd, char *buf, int n, int64_t offset);

/* Plan 9 syscall: pwrite — write n bytes at offset (offset<0 = current pos) */
NEXS_API int nexs_pwrite(int fd, const char *buf, int n, int64_t offset);

/* Plan 9 syscall: seek */
NEXS_API int64_t nexs_seek(int fd, int64_t offset, int whence);

/* Plan 9 syscall: dup — duplicate fd */
NEXS_API int nexs_dup(int oldfd, int newfd);

/* Plan 9 syscall: fd2path — get path from fd */
NEXS_API int nexs_fd2path(int fd, char *buf, int nbuf);

/* Plan 9 syscall: remove — delete a file */
NEXS_API int nexs_remove(const char *path);

/* Plan 9 syscall: pipe — create a pipe[2] */
NEXS_API int nexs_pipe(int fd[2]);

/* Plan 9 syscall: stat — file metadata as Value */
NEXS_API Value nexs_stat(const char *path);

/* Plan 9 syscall: chdir */
NEXS_API int nexs_chdir(const char *path);

/* =========================================================
   SEZIONE 11 — PLAN 9 SYSCALLS (PROCESS CONTROL)
   ========================================================= */

/* Plan 9 syscall: sleep — sleep msec milliseconds */
NEXS_API int nexs_sleep(int msec);

/* Plan 9 syscall: exec — execute a NEXS file (replaces current eval) */
NEXS_API int nexs_exec(EvalCtx *ctx, const char *path);

/* Plan 9 syscall: exits — terminate with status message */
NEXS_API void nexs_exits(const char *status);

/* Plan 9 syscall: alarm — set alarm timer (msec), returns old alarm */
NEXS_API int nexs_alarm(int msec);

/* Plan 9 syscall: errstr — get/set error string */
NEXS_API void nexs_errstr(char *buf, int nbuf);

/* Plan 9 syscall: rfork — fork process attributes */
NEXS_API int nexs_rfork(int flags);

/* Plan 9 syscall: await — wait for child, returns status string */
NEXS_API int nexs_await(char *buf, int nbuf);

/* rfork flags (Plan 9 style) */
#define NEXS_RFPROC   (1 << 0)  /* create new process */
#define NEXS_RFNOWAIT (1 << 1)  /* don't wait for children */
#define NEXS_RFNAMEG  (1 << 2)  /* new namespace group */
#define NEXS_RFMEM    (1 << 3)  /* share memory */
#define NEXS_RFFDG    (1 << 4)  /* copy fd table */

/* =========================================================
   SEZIONE 12 — REPL / ENTRY POINT
   ========================================================= */

NEXS_API void nexs_repl(void);
NEXS_API void die(const char *msg) __attribute__((noreturn));
NEXS_API void nexs_warn(const char *fmt, ...);
NEXS_API void nexs_runtime_init(void);
NEXS_API void nexs_print_version(FILE *out);

/* =========================================================
   SEZIONE 13 — MACRO DI UTILITÀ
   ========================================================= */

#define NEXS_ASSERT(ptr, msg)                                                  \
  do {                                                                         \
    if (!(ptr))                                                                \
      die(msg);                                                                \
  } while (0)

#define REG_PATH(buf, ...) nexs_path_join((buf), sizeof(buf), __VA_ARGS__, NULL)

#define PROPAGATE_ERR(v)                                                       \
  do {                                                                         \
    if (val_is_error(&(v)))                                                    \
      return (EvalResult){CTRL_ERR, (v)};                                      \
  } while (0)

#define IS_CTRL(er) ((er).sig != CTRL_NONE)

/* Wrappers per i tipi Value comuni */
#define VAL_INT(n) val_int((int64_t)(n))
#define VAL_FLOAT(f) val_float((double)(f))
#define VAL_STR(s) val_str(s)
#define VAL_BOOL(b) val_bool(b)
#define VAL_NIL() val_nil()
#define VAL_ERR(c, m) val_err((c), (m))
#define VAL_TRUE val_bool(1)
#define VAL_FALSE val_bool(0)

/* Percorsi registro standard */
#define REG_LOCAL "/local"
#define REG_FN    "/fn"
#define REG_SYS   "/sys"
#define REG_MOD   "/mod"
#define REG_ENV   "/env"
#define REG_TYPE  "/type"

/* =========================================================
   SEZIONE 14 — FUNZIONI DI UTILITÀ INTERNA
   ========================================================= */

NEXS_API void nexs_path_join(char *buf, size_t bufsz, ...);
NEXS_API const char *nexs_path_basename(const char *path);
NEXS_API void nexs_path_dirname(const char *path, char *out, size_t outsz);
NEXS_API void nexs_trim(char *s);
NEXS_API char *buddy_strdup(const char *s);

/* =========================================================
   SEZIONE 15 — BUILT-IN REGISTRATION
   ========================================================= */

/* Tipo per le funzioni built-in registrate in /sys/ */
typedef Value (*BuiltinFn)(Value *args, int n_args);

/* Registra un built-in nel registro /sys/ */
NEXS_API void reg_register_builtin(const char *name, BuiltinFn fn);

/* Registra tutti i built-in standard (str, int, float, len, type, etc) */
NEXS_API void builtins_register_all(void);

/* Registra i built-in Plan 9 I/O (open, close, read, write, etc) */
NEXS_API void sysio_register_builtins(void);

/* Registra i built-in Plan 9 process (sleep, exec, exits, etc) */
NEXS_API void sysproc_register_builtins(void);

#ifdef __cplusplus
}
#endif

#endif /* BASEREG_H */
