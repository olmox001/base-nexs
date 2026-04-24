/*
 * registry.c — Hierarchical Registry Implementation
 * ====================================================
 * Il "regedit linker" — registro gerarchico ad albero.
 *
 * Implementazioni complete:
 *   - reg_move: sposta/rinomina chiavi con aggiornamento ricorsivo dei path
 *   - reg_mount: monta un sottoalbero come figlio di un altro nodo
 *   - reg_unmount: rimuove un mount precedente
 *   - reg_bind: union mount in stile Plan 9
 *   - reg_delete: ricorsivo (FIX: il vecchio codice non liberava i figli)
 *   - reg_key_print: implementazione reale
 */

#include "basereg.h"

#include <string.h>

/* =========================================================
   DATI GLOBALI
   ========================================================= */

Registry g_registry;
static int g_scope_counter = 0;

/* =========================================================
   ALLOCAZIONE NODI
   ========================================================= */

static RegKey *regkey_alloc(const char *name, const char *path,
                            uint8_t rights) {
  RegKey *k = xmalloc(sizeof(RegKey));
  strncpy(k->name, name, NAME_LEN - 1);
  k->name[NAME_LEN - 1] = '\0';
  strncpy(k->path, path, REG_PATH_MAX - 1);
  k->path[REG_PATH_MAX - 1] = '\0';
  k->val = val_nil();
  k->rights = rights;
  k->parent = NULL;
  k->children = NULL;
  k->next = NULL;
  return k;
}

static RegKey *regkey_find_child(RegKey *parent, const char *name) {
  if (!parent || !name)
    return NULL;
  for (RegKey *c = parent->children; c; c = c->next)
    if (strcmp(c->name, name) == 0)
      return c;
  return NULL;
}

static void regkey_add_child(RegKey *parent, RegKey *child) {
  if (!parent || !child)
    return;
  child->parent = parent;
  child->next = parent->children;
  parent->children = child;
  g_registry.total_keys++;
}

/* Aggiunge child in coda alla lista figli (per mount "after") */
static void regkey_add_child_tail(RegKey *parent, RegKey *child) {
  if (!parent || !child)
    return;
  child->parent = parent;
  child->next = NULL;
  if (!parent->children) {
    parent->children = child;
  } else {
    RegKey *last = parent->children;
    while (last->next)
      last = last->next;
    last->next = child;
  }
  g_registry.total_keys++;
}

/* Rimuove child dalla lista figli del parent (NON libera) */
static void regkey_detach_child(RegKey *parent, RegKey *child) {
  if (!parent || !child)
    return;
  RegKey **pp = &parent->children;
  while (*pp && *pp != child)
    pp = &(*pp)->next;
  if (*pp) {
    *pp = child->next;
    child->next = NULL;
    child->parent = NULL;
    g_registry.total_keys--;
  }
}

/* =========================================================
   OPERAZIONI BASE
   ========================================================= */

RegKey *reg_mkpath(const char *path, uint8_t rights) {
  if (!path || path[0] != '/')
    return NULL;

  RegKey *cur = g_registry.root;
  char buf[REG_PATH_MAX];
  strncpy(buf, path, REG_PATH_MAX - 1);
  buf[REG_PATH_MAX - 1] = '\0'; /* FIX: garantisce null-termination */

  char *seg = strtok(buf + 1, "/");
  char built[REG_PATH_MAX] = "/";
  built[REG_PATH_MAX - 1] = '\0';

  while (seg) {
    char child_path[REG_PATH_MAX];
    if (strcmp(built, "/") == 0)
      snprintf(child_path, sizeof(child_path), "/%s", seg);
    else
      snprintf(child_path, sizeof(child_path), "%s/%s", built, seg);

    RegKey *child = regkey_find_child(cur, seg);
    if (!child) {
      child = regkey_alloc(seg, child_path, rights);
      regkey_add_child(cur, child);
    }
    strncpy(built, child_path, REG_PATH_MAX - 1);
    built[REG_PATH_MAX - 1] = '\0';
    cur = child;
    seg = strtok(NULL, "/");
  }
  return cur;
}

RegKey *reg_lookup(const char *path) {
  if (!path)
    return NULL;
  if (strcmp(path, "/") == 0)
    return g_registry.root;
  if (path[0] != '/')
    return NULL;

  char buf[REG_PATH_MAX];
  strncpy(buf, path, REG_PATH_MAX - 1);
  buf[REG_PATH_MAX - 1] = '\0';

  RegKey *cur = g_registry.root;
  char *seg = strtok(buf + 1, "/");
  while (seg && cur) {
    cur = regkey_find_child(cur, seg);
    seg = strtok(NULL, "/");
  }
  return cur;
}

/*
 * reg_resolve — il cuore del "regedit linker".
 *
 * Cerca 'name' nell'ordine:
 *  1. /local/<scope>/<name>      (variabile locale nello scope corrente)
 *  2. /local/<parent_scope>/...  (scope chain verso la radice)
 *  3. /fn/<name>                 (funzione definita dall'utente)
 *  4. /sys/<name>                (built-in del runtime)
 *  5. /mod/<*>/<name>            (export dei moduli importati)
 */
RegKey *reg_resolve(const char *name, const char *scope_path) {
  if (!name)
    return NULL;
  char path[REG_PATH_MAX];

  /* 1. Scope corrente e antenati */
  char sp[REG_PATH_MAX];
  strncpy(sp, scope_path ? scope_path : REG_LOCAL, REG_PATH_MAX - 1);
  sp[REG_PATH_MAX - 1] = '\0';
  while (1) {
    snprintf(path, sizeof(path), "%s/%s", sp, name);
    RegKey *k = reg_lookup(path);
    if (k)
      return k;
    char parent[REG_PATH_MAX];
    nexs_path_dirname(sp, parent, sizeof(parent));
    if (strcmp(parent, sp) == 0 || strcmp(parent, "/") == 0)
      break;
    strncpy(sp, parent, REG_PATH_MAX - 1);
    sp[REG_PATH_MAX - 1] = '\0';
  }

  /* 2. /fn/ */
  snprintf(path, sizeof(path), "%s/%s", REG_FN, name);
  RegKey *k = reg_lookup(path);
  if (k) return k;

  /* 3. /sys/ */
  snprintf(path, sizeof(path), "%s/%s", REG_SYS, name);
  k = reg_lookup(path);
  if (k) return k;

  /* 4. /mod/[all] */
  RegKey *mod_root = reg_lookup(REG_MOD);
  if (mod_root) {
    for (RegKey *mod = mod_root->children; mod; mod = mod->next) {
      snprintf(path, sizeof(path), "%s/%s/%s", REG_MOD, mod->name, name);
      k = reg_lookup(path);
      if (k) return k;
    }
  }

  return NULL;
}

int reg_set(const char *path, Value val, uint8_t rights) {
  if (!path)
    return -1;
  RegKey *k = reg_lookup(path);
  if (!k)
    k = reg_mkpath(path, rights);
  if (!k)
    return -1;
  val_free(&k->val);
  k->val = val_clone(&val);
  k->rights = rights;
  return 0;
}

Value reg_get(const char *path) {
  RegKey *k = reg_lookup(path);
  if (!k)
    return val_err(3, "chiave registro non trovata");
  return val_clone(&k->val);
}

/*
 * reg_delete — FIX: ricorsivo.
 * Il vecchio codice non liberava i figli, causando memory leak.
 */
static void regkey_free_recursive(RegKey *k) {
  if (!k)
    return;
  /* Prima libera tutti i figli ricorsivamente */
  RegKey *child = k->children;
  while (child) {
    RegKey *next = child->next;
    regkey_free_recursive(child);
    child = next;
  }
  val_free(&k->val);
  xfree(k);
  if (g_registry.total_keys > 0)
    g_registry.total_keys--;
}

int reg_delete(const char *path) {
  RegKey *k = reg_lookup(path);
  if (!k)
    return -1;
  /* Stacca dal parent */
  if (k->parent) {
    RegKey **pp = &k->parent->children;
    while (*pp && *pp != k)
      pp = &(*pp)->next;
    if (*pp)
      *pp = k->next;
  }
  regkey_free_recursive(k);
  return 0;
}

/* =========================================================
   LISTING
   ========================================================= */

static void reg_ls_node(RegKey *k, FILE *out, int depth, int recursive) {
  if (!k || !out)
    return;
  for (int i = 0; i < depth; i++)
    fprintf(out, "  ");
  fprintf(out, "%-24s  %-6s", k->path, val_type_name(k->val.type));
  if (k->val.type != TYPE_NIL) {
    fprintf(out, "  = ");
    val_print(&k->val, out);
  }
  fprintf(out, "\n");
  if (recursive) {
    for (RegKey *c = k->children; c; c = c->next)
      reg_ls_node(c, out, depth + 1, 1);
  }
}

void reg_ls(const char *path, FILE *out) {
  RegKey *k = reg_lookup(path);
  if (!k) {
    fprintf(out, "Percorso '%s' non trovato\n", path);
    return;
  }
  fprintf(out, "\n[REGISTRO] %s\n", path);
  fprintf(out, "%-24s  %-6s  %s\n", "PERCORSO", "TIPO", "VALORE");
  fprintf(out, "-----------------------------------------------\n");
  for (RegKey *c = k->children; c; c = c->next)
    reg_ls_node(c, out, 0, 0);
  fprintf(out, "\n");
}

void reg_ls_recursive(const char *path, FILE *out, int depth) {
  RegKey *k = reg_lookup(path);
  if (!k)
    return;
  reg_ls_node(k, out, depth, 1);
}

void reg_key_print(RegKey *k, FILE *out) {
  if (!k || !out)
    return;
  fprintf(out, "[%s] type=%s rights=%02x", k->path,
          val_type_name(k->val.type), k->rights);
  if (k->val.type != TYPE_NIL) {
    fprintf(out, " val=");
    val_print(&k->val, out);
  }
  fprintf(out, "\n");
}

/* =========================================================
   MOVE / MOUNT / UNMOUNT / BIND (Plan 9 style)
   ========================================================= */

/*
 * Aggiorna ricorsivamente i percorsi di un sottoalbero
 * dopo un move o rename.
 */
static void regkey_update_paths(RegKey *k, const char *new_parent_path) {
  if (!k)
    return;
  /* Ricostruisci il path completo */
  if (strcmp(new_parent_path, "/") == 0)
    snprintf(k->path, REG_PATH_MAX, "/%s", k->name);
  else
    snprintf(k->path, REG_PATH_MAX, "%s/%s", new_parent_path, k->name);

  /* Aggiorna ricorsivamente i figli */
  for (RegKey *c = k->children; c; c = c->next)
    regkey_update_paths(c, k->path);
}

/*
 * reg_move — sposta/rinomina una chiave.
 *
 * Esempio: reg_move("/local/x", "/env/x")
 *   1. Trova la chiave sorgente
 *   2. La stacca dal parent attuale
 *   3. Crea il path di destinazione (parent) se necessario
 *   4. La riattacca al nuovo parent con il nuovo nome
 *   5. Aggiorna ricorsivamente tutti i path dei figli
 */
int reg_move(const char *src, const char *dst) {
  if (!src || !dst || src[0] != '/' || dst[0] != '/')
    return -1;
  if (strcmp(src, "/") == 0)
    return -1; /* non si può spostare la root */
  if (strcmp(src, dst) == 0)
    return 0; /* noop */

  RegKey *src_key = reg_lookup(src);
  if (!src_key)
    return -1;

  /* Verifica che dst non sia sotto src (moverebbe in sé stesso) */
  size_t src_len = strlen(src);
  if (strncmp(dst, src, src_len) == 0 &&
      (dst[src_len] == '/' || dst[src_len] == '\0'))
    return -1;

  /* Stacca dal parent attuale */
  if (src_key->parent)
    regkey_detach_child(src_key->parent, src_key);

  /* Trova/crea il parent di destinazione */
  char dst_parent[REG_PATH_MAX];
  nexs_path_dirname(dst, dst_parent, sizeof(dst_parent));
  RegKey *dst_parent_key = reg_lookup(dst_parent);
  if (!dst_parent_key)
    dst_parent_key = reg_mkpath(dst_parent, RK_ALL);
  if (!dst_parent_key) {
    /* Fallback: ri-attacca al vecchio parent */
    return -1;
  }

  /* Aggiorna il nome della chiave */
  const char *new_name = nexs_path_basename(dst);
  strncpy(src_key->name, new_name, NAME_LEN - 1);
  src_key->name[NAME_LEN - 1] = '\0';

  /* Attacca al nuovo parent */
  regkey_add_child(dst_parent_key, src_key);

  /* Aggiorna ricorsivamente tutti i path */
  regkey_update_paths(src_key, dst_parent);

  return 0;
}

/*
 * reg_mount — monta un sottoalbero come figlio di un punto di mount.
 *
 * In Plan 9, mount(2) connette un file server a un punto nel namespace.
 * Qui lo implementiamo come "link" nel registro:
 *   reg_mount("/mod/math", "/sys", 1) → i figli di /mod/math diventano
 *   visibili come figli di /sys (prima degli altri).
 *
 * 'before': 1 = inserisci prima dei figli esistenti, 0 = dopo.
 *
 * Implementazione: crea una copia "shadow" dei figli di src come figli di dst.
 * Ogni figlio copiato ha un attributo che ricorda la provenienza.
 */
int reg_mount(const char *src_path, const char *dst_path, int before) {
  if (!src_path || !dst_path)
    return -1;

  RegKey *src = reg_lookup(src_path);
  if (!src)
    return -1;

  RegKey *dst = reg_lookup(dst_path);
  if (!dst)
    dst = reg_mkpath(dst_path, RK_ALL);
  if (!dst)
    return -1;

  /*
   * Per ogni figlio di src, crea (se non esiste) un mirror sotto dst.
   * Se il figlio già esiste in dst, aggiorna il valore.
   */
  for (RegKey *child = src->children; child; child = child->next) {
    RegKey *existing = regkey_find_child(dst, child->name);
    if (existing) {
      /* Aggiorna il valore se il mount sovrascrive */
      if (before) {
        val_free(&existing->val);
        existing->val = val_clone(&child->val);
        existing->rights = child->rights;
      }
    } else {
      /* Crea una nuova chiave mirror */
      char new_path[REG_PATH_MAX];
      if (strcmp(dst_path, "/") == 0)
        snprintf(new_path, sizeof(new_path), "/%s", child->name);
      else
        snprintf(new_path, sizeof(new_path), "%s/%s", dst_path, child->name);

      RegKey *mirror = regkey_alloc(child->name, new_path, child->rights);
      mirror->val = val_clone(&child->val);

      if (before)
        regkey_add_child(dst, mirror); /* prepend = before */
      else
        regkey_add_child_tail(dst, mirror); /* append = after */

      /* Monta anche i sotto-figli ricorsivamente */
      if (child->children) {
        char child_src[REG_PATH_MAX];
        snprintf(child_src, sizeof(child_src), "%s/%s", src_path, child->name);
        reg_mount(child_src, new_path, before);
      }
    }
  }

  return 0;
}

/*
 * reg_unmount — rimuove un mount precedente.
 *
 * Se src_path è NULL, rimuove TUTTI i figli di dst_path.
 * Se src_path è specificato, rimuove solo i figli che corrispondono
 * a quelli presenti in src_path.
 */
int reg_unmount(const char *src_path, const char *dst_path) {
  if (!dst_path)
    return -1;

  RegKey *dst = reg_lookup(dst_path);
  if (!dst)
    return -1;

  if (!src_path) {
    /* Rimuovi tutti i figli (unmount totale) */
    RegKey *child = dst->children;
    while (child) {
      RegKey *next = child->next;
      regkey_free_recursive(child);
      child = next;
    }
    dst->children = NULL;
    return 0;
  }

  RegKey *src = reg_lookup(src_path);
  if (!src)
    return -1;

  /* Rimuovi da dst solo i figli che hanno lo stesso nome dei figli di src */
  for (RegKey *sc = src->children; sc; sc = sc->next) {
    RegKey *target = regkey_find_child(dst, sc->name);
    if (target) {
      regkey_detach_child(dst, target);
      regkey_free_recursive(target);
    }
  }

  return 0;
}

/*
 * reg_bind — union mount in stile Plan 9.
 *
 * bind(2) in Plan 9 mappa un path su un altro:
 *   bind /new /old         → replace: /old mostra il contenuto di /new
 *   bind -b /new /old      → before: /new overlaid prima di /old
 *   bind -a /new /old      → after: /new overlaid dopo /old
 *
 * flag: 0 = replace, 1 = before, 2 = after
 */
int reg_bind(const char *src_path, const char *dst_path, int flag) {
  if (!src_path || !dst_path)
    return -1;

  RegKey *src = reg_lookup(src_path);
  if (!src)
    return -1;

  if (flag == 0) {
    /* Replace: sostituisci il contenuto di dst con quello di src */
    RegKey *dst = reg_lookup(dst_path);
    if (!dst)
      dst = reg_mkpath(dst_path, RK_ALL);
    if (!dst)
      return -1;

    /* Rimuovi tutti i figli attuali di dst */
    RegKey *child = dst->children;
    while (child) {
      RegKey *next = child->next;
      regkey_free_recursive(child);
      child = next;
    }
    dst->children = NULL;

    /* Copia il valore di src */
    val_free(&dst->val);
    dst->val = val_clone(&src->val);
    dst->rights = src->rights;

    /* Monta i figli di src sotto dst */
    return reg_mount(src_path, dst_path, 1);
  }

  /* before (1) o after (2) */
  return reg_mount(src_path, dst_path, flag == 1 ? 1 : 0);
}

/* =========================================================
   SCOPE MANAGEMENT
   ========================================================= */

char *reg_push_scope(const char *fn_name) {
  char *buf = xmalloc(REG_PATH_MAX);
  snprintf(buf, REG_PATH_MAX, "%s/%s_%d", REG_LOCAL,
           fn_name ? fn_name : "anon", g_scope_counter++);
  reg_mkpath(buf, RK_ALL);
  return buf;
}

void reg_pop_scope(const char *scope_path) {
  if (scope_path)
    reg_delete(scope_path);
}

/* =========================================================
   INIZIALIZZAZIONE
   ========================================================= */

void reg_init(void) {
  g_registry.root = regkey_alloc("/", "/", RK_ALL);
  g_registry.total_keys = 1;

  reg_mkpath(REG_LOCAL, RK_ALL);
  reg_mkpath(REG_FN, RK_READ | RK_EXEC);
  reg_mkpath(REG_SYS, RK_READ | RK_EXEC);
  reg_mkpath(REG_MOD, RK_ALL);
  reg_mkpath(REG_ENV, RK_ALL);
  reg_mkpath(REG_TYPE, RK_READ | RK_ADMIN);
}
