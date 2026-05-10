# NEXS — Istruzioni per Claude Code

## Crea ramo `dev-stable` da `main` (v0.1.7)

**Obiettivo:** Creare un ramo `dev-stable` che integri il meglio di `devtest` e `dev-termimgtestmultitasking`, con piena compatibilità host (Linux/macOS) **e** baremetal (amd64 + arm64 via QEMU).

-----

## 0. Setup iniziale

```bash
git clone https://github.com/olmox001/base-nexs.git
cd base-nexs
git checkout main
git checkout -b dev-stable
git fetch origin
```

Verifica che `main` compili pulito prima di toccare qualsiasi cosa:

```bash
make clean && make
./nexs example/test.nx
# Deve stampare: 42, 3.14, NEXS, ... senza errori
```

-----

## 1. FASE 1 — Refactoring HAL: elimina duplicazioni

**Fonte:** `devtest` branch
**Priorità:** Alta — deve essere fatto prima degli altri moduli kernel perché tutti dipendono dal HAL.

### 1.1 Crea `hal/common/console.c` e `hal/common/timer.c`

Copia da `devtest`:

```bash
mkdir -p hal/common
git show origin/devtest:hal/common/console.c > hal/common/console.c
git show origin/devtest:hal/common/timer.c > hal/common/timer.c
git show origin/devtest:hal/include/hal_internal.h > hal/include/hal_internal.h
```

### 1.2 Aggiorna `hal/include/nexs_mmu.h` e `nexs_timer.h`

```bash
git show origin/devtest:hal/include/nexs_mmu.h > hal/include/nexs_mmu.h
git show origin/devtest:hal/include/nexs_timer.h > hal/include/nexs_timer.h
```

### 1.3 NON prendere da `devtest`

- ❌ `hal/module/driver.hpp` — C++, viola le regole del progetto
- ❌ `hal/module/uart_cpp.cpp` — C++
- ❌ `kernel/libcxx_stub.cpp` — C++
- ❌ `stub.zip` — artefatto binario

### 1.4 Aggiorna il Makefile

Nel `Makefile`, aggiungi `hal/common/console.o` e `hal/common/timer.o` alla lista degli oggetti compilati. Non toccare le regole baremetal esistenti.

### 1.5 Test

```bash
make clean && make
# Deve compilare senza errori o warning nuovi
```

-----

## 2. FASE 2 — Kernel: IPC seL4-style, Capabilities, sys_brk

**Fonte:** `devtest` branch
**Nota:** Questi file vanno copiati e adattati, NON semplicemente sovrascrivendoli, perché dipendono da `TYPE_MAP` che non esiste ancora in `core/value.c`. Leggi attentamente le note per ogni file.

### 2.1 Copia file kernel nuovi da `devtest`

```bash
git show origin/devtest:kernel/ipc.c > kernel/ipc.c
git show origin/devtest:kernel/cap.c > kernel/cap.c
git show origin/devtest:kernel/sys_brk.c > kernel/sys_brk.c
git show origin/devtest:kernel/include/nexs_ipc.h > kernel/include/nexs_ipc.h
git show origin/devtest:kernel/include/nexs_cap.h > kernel/include/nexs_cap.h
git show origin/devtest:core/include/nexs_fd.h > core/include/nexs_fd.h
git show origin/devtest:core/include/nexs_alloc.h > core/include/nexs_alloc.h
```

### 2.2 Fix obbligatorio in `kernel/sys_brk.c`

Il file hardcoda l’heap a `0x40000000` (1GB). Questo funziona solo con identity map esatta. Sostituisci la costante con un valore configurabile:

Nel file `kernel/sys_brk.c`, trova:

```c
p->heap_start = 0x40000000ULL;
```

Sostituisci con:

```c
#ifndef NEXS_HEAP_BASE
#define NEXS_HEAP_BASE 0x40000000ULL
#endif
p->heap_start = NEXS_HEAP_BASE;
```

### 2.3 `kernel/vfs_server.c` — copia ma DISABILITA per ora

`vfs_server.c` usa `val_map_get()` e `val_map_set()` che richiedono `TYPE_MAP`, non ancora nel value system stabile. Copia il file ma wrappalo con una guardia:

```bash
git show origin/devtest:kernel/vfs_server.c > kernel/vfs_server.c
```

All’inizio di `kernel/vfs_server.c`, aggiungi:

```c
#ifdef NEXS_VFS_SERVER_ENABLED
/* ... tutto il contenuto originale ... */
#endif /* NEXS_VFS_SERVER_ENABLED */
```

Non definire `NEXS_VFS_SERVER_ENABLED` da nessuna parte per ora. Sarà abilitato in una fase futura.

### 2.4 Aggiorna `kernel/include/nexs_proc.h`

Da `devtest`, `NexsProc` ha nuovi campi (`cspace`, `ipc_msg`, `ipc_badge`, `heap_start`, `heap_end`). Applica solo i campi effettivamente usati da `ipc.c` e `cap.c`:

```bash
git show origin/devtest:kernel/include/nexs_proc.h > /tmp/nexs_proc_devtest.h
```

Confronta con la versione attuale e aggiungi manualmente solo:

```c
/* Da devtest — IPC fields */
Value ipc_msg;
uint32_t ipc_badge;
/* Da devtest — Capability space */
struct { Cap *caps; uint32_t size; uint32_t count; } cspace;
/* Da devtest — Heap management */
uint64_t heap_start;
uint64_t heap_end;
```

### 2.5 Aggiorna il Makefile

Aggiungi alla lista degli oggetti kernel:

```makefile
kernel/ipc.o kernel/cap.o kernel/sys_brk.o
```

NON aggiungere `kernel/vfs_server.o` (è disabilitato).

### 2.6 Test

```bash
make clean && make
./nexs example/test.nx # regressione
```

-----

## 3. FASE 3 — Language: builtins TextBuffer + dep_scan + bug fixes C

**Fonte:** `devtest` branch
**Nota:** `eval.c` e `builtins.c` hanno molte modifiche. Porta solo le addizioni nette, NON sovrascrivere l’intera versione di `main` che ha fix propri.

### 3.1 Builtins TextBuffer

`main` ha già `services/textbuf.nx` come libreria `.nx`. Da `devtest`, `lang/builtins.c` aggiunge le stesse operazioni come **builtin C nativi** (`tb_create`, `tb_load`, `tb_to_str`, `tb_insert_char`, `tb_delete_char`) e `sys_bundled`. Questo migliora le performance dell’editor. Sono pure addizioni, non riscrivono nulla di esistente.

Procedura: usa `git diff` per estrarre solo i blocchi e aggiungili manualmente in fondo alla sezione builtins di `lang/builtins.c`, **prima** della funzione `builtins_init()`:

```bash
git diff main..origin/devtest -- lang/builtins.c | grep -A 30 "^+static Value builtin_tb_create"
# copia il blocco, ripeti per: builtin_tb_load, tb_to_str, tb_insert_char, tb_delete_char, sys_bundled
```

Poi dentro `builtins_init()`, aggiungi le registrazioni corrispondenti:

```bash
git diff main..origin/devtest -- lang/builtins.c | grep "^+.*register_builtin_sig.*tb_\|^+.*register_builtin_sig.*sys_bundled"
```

### 3.2 `compiler/dep_scan.c` — rimuovi duplicazione

Da `devtest`, `dep_scan.c` rimuove le funzioni `path_dirname` e `path_join` locali usando quelle già in `core/utils.c` via `nexs_utils.h`. Sostituzione sicura:

```bash
git show origin/devtest:compiler/dep_scan.c > compiler/dep_scan.c
```

### 3.3 Fix bug `kmsg_encode` — separatore fragile

In `kernel/msg.c`, `kmsg_encode` usa `:` come separatore nel formato `verb:arg0:arg1:n:pid`. Se `arg0` o `arg1` contengono `:` (es. path Windows, URL), `strtok` in `kmsg_decode` spezza il messaggio in modo errato.

Apri `kernel/msg.c` e sostituisci il separatore con il carattere ASCII `\x1F` (Unit Separator), che non può apparire in path NEXS:

```c
/* In kmsg_encode — sostituisci tutte le ":" con "\x1f" */
snprintf(buf, sizeof(buf), "%s\x1f%s\x1f%s\x1f%lld\x1f%u",
m->verb, m->arg0, m->arg1,
(long long)m->n, (unsigned)m->sender_pid);

/* In kmsg_decode — sostituisci strtok(p, ":") con strtok(p, "\x1f") */
tok = strtok(p, "\x1f");
```

Applica la stessa modifica a tutte e 5 le chiamate `strtok` in `kmsg_decode`.

### 3.4 Fix bug `walk_and_enable_pipes` — ricorsione profonda

In `registry/reg_ipc.c`, `walk_and_enable_pipes()` è ricorsiva e può causare stack overflow su registry molto profondi. Cerca la funzione e convertila in iterativa con uno stack esplicito, seguendo lo stesso pattern già usato in `registry.c` per `regkey_free_recursive`:

```c
static void walk_and_enable_pipes(RegKey *root) {
if (!root) return;
#define PIPE_STACK_MAX 1024
RegKey *stack[PIPE_STACK_MAX];
int top = 0;
stack[top++] = root;
while (top > 0) {
RegKey *node = stack[--top];
/* ... logica di upgrade pipe esistente ... */
if (node->next && top < PIPE_STACK_MAX) stack[top++] = node->next;
if (node->children && top < PIPE_STACK_MAX) stack[top++] = node->children;
}
#undef PIPE_STACK_MAX
}
```

### 3.5 Test

```bash
make clean && make
./nexs example/test.nx
./nexs example/example_lib.nx
```

-----

## 4. FASE 4 — Terminal syscalls: `term_size` + `set_fg_pid`

**⚠️ SCOPERTA IMPORTANTE:** Analizzando `sys/sysio.c` in `main`, risulta che le funzioni `rawon`, `rawoff`, `readkey`, `readkey_nb`, `term_at`, `term_cls`, `term_flush`, `term_cursor_move`, `term_erase_eol`, `term_cursor_show`, `bi_readbyte`, `bi_chr` sono **già presenti** nel ramo main. Non vanno portate.

Mancano solo **2 funzioni** rispetto a `dev-termimgtestmultitasking`:

### 4.1 Aggiungi `set_fg_pid` e focus gating

In `sys/sysio.c`, subito dopo la dichiarazione delle variabili statiche esistenti, aggiungi:

```c
/* Focus gating — PID del processo in foreground */
static int s_fg_pid = -1;

static int nexs_is_fg(void) {
#ifdef NEXS_BAREMETAL
return 1; /* baremetal: sempre in foreground */
#else
if (s_fg_pid < 0) return 1; /* nessun fg impostato = tutti visibili */
return ((int)getpid() == s_fg_pid);
#endif
}

static Value bi_set_fg_pid(Value *args, int n) {
if (n < 1) return val_nil();
s_fg_pid = (int)val_to_int(&args[0]);
return val_nil();
}
```

Poi aggiorna le funzioni `bi_term_at`, `bi_term_cls`, `bi_term_flush` esistenti aggiungendo all’inizio di ognuna:

```c
if (!nexs_is_fg()) return val_nil();
```

Registra il nuovo builtin in `sysio_register_builtins()`:

```c
fn_register_builtin_sig("set_fg_pid", bi_set_fg_pid,
SIG("set_fg_pid(pid int)") "nil");
```

### 4.2 Aggiungi `term_size`

In `sys/sysio.c`, aggiungi dopo le funzioni `term_*` esistenti:

```c
static Value bi_term_size(Value *args, int n) {
(void)args; (void)n;
#ifdef NEXS_BAREMETAL
return val_str("80x24");
#else
struct winsize ws;
if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
char buf[32];
snprintf(buf, sizeof(buf), "%dx%d", (int)ws.ws_col, (int)ws.ws_row);
return val_str(buf);
}
return val_str("80x24"); /* fallback */
#endif
}
```

Aggiungi `#include <sys/ioctl.h>` in cima al file se non già presente (controlla con `grep ioctl sys/sysio.c`).

Registra in `sysio_register_builtins()`:

```c
fn_register_builtin_sig("term_size", bi_term_size,
SIG("term_size()") "str");
```

### 4.3 Verifica guardie baremetal esistenti

Controlla che le funzioni `rawon`/`rawoff`/`readkey` già presenti abbiano le guardie `#ifndef NEXS_BAREMETAL`. Se mancano, aggiungile seguendo il pattern:

```bash
grep -n "NEXS_BAREMETAL" sys/sysio.c | head -20
```

Se nessuna guardia è presente su queste funzioni, aggiungile: `rawon`/`rawoff` → `return val_nil()` su baremetal; `readkey` → usa `nexs_hal_getc()`.

### 4.4 Test

```bash
make clean && make
cat > /tmp/test_term.nx << 'EOF'
sz = term_size()
out "Dimensioni: " + sz
set_fg_pid(0)
out "Focus gating OK"
set_fg_pid(-1)
EOF
./nexs /tmp/test_term.nx
# Output atteso: "Dimensioni: NNxNN" e "Focus gating OK"
```

-----

## 5. FASE 5 — PM: multitasking reale, mangle, session manager

**Fonte:** `dev-termimgtestmultitasking` branch
**Contesto:** `services/pm/init.nx` in main è una versione **stub minimale** (solo `pm_spawn/alloc/kill/ps` senza `rfork`, senza isolamento). Va sostituita completamente.

### 5.1 Copia il PM riscritto

```bash
git show origin/dev-termimgtestmultitasking:services/pm/init.nx > services/pm/init.nx
git show origin/dev-termimgtestmultitasking:services/pm/core.nx > services/pm/core.nx 2>/dev/null || true
```

### 5.2 Fix OBBLIGATORIO: `exits()` dal processo figlio

Il bug dichiarato è: *“exit not work”*. `pm_spawn` fa `rfork()` e il figlio chiama `exits("")`, ma il PM non aggiorna correttamente il proprio stato.

In `services/pm/init.nx`, nella funzione `pm_spawn`, nella sezione `if pid_fork == 0 { ... }`, sostituisci la parte finale con:

```nexs
# Fix: scrivi stato exited nel registry PRIMA di exits()
base = "/sys/" + str(uid) + "/" + str(new_pid)
reg_set(base + "/state" "exited")
pm_exit(new_pid)

# Torna alla sessione 0 (shell principale)
reg /sys/pm/session_active = 0
term_cls()

exits("")
```

### 5.3 Fix OBBLIGATORIO: whitelist mangle

Il problema del multibuffer è che `rfork()` su hosted condivide la fn-table. Il `pm_mangle` rinomina le funzioni user-defined per evitare collisioni, ma la whitelist dei builtin deve essere completa.

Dopo aver completato le fasi 3 e 4, verifica che la variabile `PM_SYS_PREFIXES` in `services/pm/init.nx` contenga tutti i nuovi builtin aggiunti. Apri il file e aggiungi alla stringa i prefissi mancanti:

```
tb_create|tb_load|tb_to_str|tb_insert_char|tb_delete_char|sys_bundled|set_fg_pid|term_size
```

Per verificare quali builtin sono registrati nel runtime:

```bash
./nexs -e 'x = 1' 2>/dev/null # poi nel REPL usa :fn per listare tutti
```

### 5.4 Aggiorna `services/ui/init.nx`

```bash
git show origin/dev-termimgtestmultitasking:services/ui/init.nx > services/ui/init.nx
```

`services/ui/init.nx` usa solo `term_at`, `term_cls`, `term_flush` che sono già in main. Verifica che non usi funzioni non disponibili:

```bash
grep -o '[a-z_]*(' services/ui/init.nx | sort -u
# Controlla che ogni funzione chiamata sia definita o sia un builtin
```

### 5.5 Test

```bash
./nexs example/minios/boot.nx
# Shell deve avviarsi
# Digita: ps → deve mostrare processi da PID 100+
# Digita: ls → deve funzionare
```

-----

## 6. FASE 6 — Sistema immagini `.nxtimg`

**Fonte:** `dev-termimgtestmultitasking` branch
**Note:** Tutto userspace `.nx` + script Python, zero dipendenze kernel.

### 6.1 Copia file

```bash
git show origin/dev-termimgtestmultitasking:scripts/compile_to_nxtimg.py > scripts/compile_to_nxtimg.py
chmod +x scripts/compile_to_nxtimg.py

mkdir -p example/minios/logos
git show origin/dev-termimgtestmultitasking:example/minios/logos/logo1.nxtimg > example/minios/logos/logo1.nxtimg
git show origin/dev-termimgtestmultitasking:example/minios/logos/logo2.nxtimg > example/minios/logos/logo2.nxtimg
git show origin/dev-termimgtestmultitasking:example/minios/logos_config.nx > example/minios/logos_config.nx
git show origin/dev-termimgtestmultitasking:example/minios/termimg.nx > example/minios/termimg.nx
git show origin/dev-termimgtestmultitasking:example/minios/logo.nx > example/minios/logo.nx

mkdir -p services/logos
git show origin/dev-termimgtestmultitasking:services/logos/init.nx > services/logos/init.nx
git show origin/dev-termimgtestmultitasking:services/ui/img.nx > services/ui/img.nx

git show origin/dev-termimgtestmultitasking:scripts/compile-and-test-logos.sh > scripts/compile-and-test-logos.sh
chmod +x scripts/compile-and-test-logos.sh
```

### 6.2 Verifica `ui_draw_image()` in `services/ui/init.nx`

Controlla che la funzione `ui_draw_image` sia presente:

```bash
grep -n "fn ui_draw_image" services/ui/init.nx
```

Se manca (la versione di `dev-termimgtestmultitasking` la ha in `services/ui/img.nx`), aggiungi in fondo a `services/ui/init.nx`:

```nexs
exec("services/ui/img.nx")
```

### 6.3 Aggiorna `boot.nx`

In `example/minios/boot.nx`, dopo la riga `exec("services/ui/init.nx")`, aggiungi:

```nexs
exec("services/logos/init.nx")
```

### 6.4 Aggiunta alla shell

In `example/minios/shell.nx`, nel dispatcher comandi, aggiungi il supporto per `termimg`:

```bash
grep -n "termimg\|nxed\|cmd ==" example/minios/shell.nx | head -20
# Individua il pattern del dispatcher e aggiungi il caso termimg nello stesso stile
```

### 6.5 Test

```bash
./nexs example/minios/boot.nx
# Dalla shell minios:
# termimg sd02/logos/logo1.nxtimg
# Deve stampare l'immagine ANSI nel terminale
```

-----

## 7. FASE 7 — stdlib.nx unificata

**Contesto:** `main` ha `modules/stdlib.nx` (auto-caricato dal runtime) e `services/stdlib.nx` (caricato da `boot.nx`). Devono essere allineati e arricchiti.

### 7.1 Base da `dev-termimgtestmultitasking`

```bash
git show origin/dev-termimgtestmultitasking:services/stdlib.nx > services/stdlib.nx
```

### 7.2 Integra funzioni da `devtest`

```bash
git show origin/devtest:modules/stdlib.nx > /tmp/stdlib_devtest.nx
```

Confronta e aggiungi da `devtest` le funzioni non già presenti nella versione `dev-termimgtestmultitasking`. Funzioni da aggiungere tipicamente: `sys_shutdown()`, versione migliorata di `replace_all()`, `arr_join()` se mancante.

Attenzione: `kout()` esiste in entrambe con logica diversa. Usa la versione di `dev-termimgtestmultitasking` (più completa, filtra per PM sessions).

### 7.3 Sincronizza `modules/stdlib.nx`

```bash
cp services/stdlib.nx modules/stdlib.nx
```

Il runtime carica `modules/stdlib.nx` automaticamente all’avvio. I due file devono essere identici.

### 7.4 Test

```bash
./nexs -e 'out input("test> ")'
# Deve funzionare (input() è definita in stdlib.nx)
./nexs -e 'out join(arr_create_anon() ",")'
# Deve stampare stringa vuota senza errori
```

-----

## 8. FASE 8 — NXED v4.0 e shell aggiornata

**Fonte:** `dev-termimgtestmultitasking` branch

```bash
git show origin/dev-termimgtestmultitasking:example/minios/nxed_editor.nx > example/minios/nxed_editor.nx
git show origin/dev-termimgtestmultitasking:example/minios/shell.nx > example/minios/shell.nx
```

Dopo la copia, verifica che nessuna funzione referenziata sia indefinita:

```bash
# Lista tutte le chiamate a funzioni nella shell
grep -oP '(?<=\b)[a-z_]+(?=\()' example/minios/shell.nx | sort -u > /tmp/shell_calls.txt
# Confronta con funzioni definite in services/
grep -oh "^fn [a-z_]*" services/*.nx services/**/*.nx 2>/dev/null | sed 's/fn //' | sort -u > /tmp/defined_fns.txt
comm -23 /tmp/shell_calls.txt /tmp/defined_fns.txt
# L'output devono essere solo builtin C (open, read, write, ecc.) — nessuna funzione .nx mancante
```

-----

## 9. FASE 9 — Bug fix C stabili da entrambi i rami

**Nota:** Questa fase non era nelle istruzioni originali ed è stata aggiunta dall’analisi del codice sorgente di main.

### 9.1 Fix `fs/regfs.c` — CRC32 incompleto

Il CRC32 in `regfs.c` è calcolato solo sull’header, non sui record dati. Questo significa che la verifica di integrità non rileva corruzione nei dati salvati.

Dopo aver scritto tutti i record, ricalcola il CRC su tutto il contenuto e aggiorna l’header:

```c
/* Dopo il loop di scrittura record in regfs_save(), prima di fclose(): */
/* Riavvolgi, leggi tutto, ricalcola CRC, aggiorna header */
fseek(ctx.fp, sizeof(RegfsHeader), SEEK_SET);
uint32_t data_crc = 0;
uint8_t byte;
while (fread(&byte, 1, 1, ctx.fp) == 1)
data_crc = crc32_byte(data_crc, byte);
/* Aggiorna il campo crc32 nell'header su disco */
fseek(ctx.fp, offsetof(RegfsHeader, crc32), SEEK_SET);
fwrite(&data_crc, sizeof(uint32_t), 1, ctx.fp);
```

### 9.2 Fix `kernel/vfs.c` — `path_to_ino` O(n)

`path_to_ino()` fa una scansione lineare del registry per ogni `open()`. Aggiungi una hash map semplice (array di `{path, ino}` con lookup per stringa) o almeno limita la ricerca ai soli figli diretti invece di tutto `/vfs/inode/`.

La soluzione minima non richiede una hash map: sostituisci il lookup con una `reg_lookup` diretta costruendo il path:

```c
static uint64_t path_to_ino(const char *path) {
/* Cerca /vfs/path/<path>/ino direttamente invece di scan lineare */
char reg[REG_PATH_MAX];
/* Normalizza il path come chiave registry (sostituisci / con _) */
char safe[REG_PATH_MAX];
/* ... normalizzazione ... */
snprintf(reg, sizeof(reg), "/vfs/path/%s", safe);
Value v = reg_get(reg);
if (v.type == TYPE_INT) { uint64_t ino = (uint64_t)v.ival; val_free(&v); return ino; }
val_free(&v);
return 0;
}
```

E in `ino_publish()`, registra anche il path inverso:

```c
snprintf(reg, sizeof(reg), "/vfs/path/%s", safe_path);
reg_set(reg, val_int((int64_t)ino), RK_READ);
```

### 9.3 Pulizia `TASK_SCOPE.MD`

Il file `TASK_SCOPE.MD` contiene l’email personale originale con il prompt per Claude Code. Va rimosso o svuotato prima di qualsiasi pubblicazione:

```bash
# Sostituisci il contenuto con una descrizione pulita del progetto
cat > TASK_SCOPE.MD << 'EOF'
# NEXS Task Scope

See PLAN.md for the full implementation roadmap.
See STATO.md for current project status.
EOF
```

### 9.4 Test

```bash
make clean && make
./nexs example/test.nx # regressione
./nexs example/example_lib.nx
```

-----

## 10. FASE 10 — Developer tooling

**Fonte:** `devtest` branch
**Note:** Completamente indipendente dal runtime, nessun rischio di destabilizzazione.

### 10.1 Language Server `nexsd`

```bash
mkdir -p tools/nexsd
git show origin/devtest:tools/nexsd/main.c > tools/nexsd/main.c
git show origin/devtest:tools/nexsd/nexsd_protocol.h > tools/nexsd/nexsd_protocol.h
git show origin/devtest:tools/nexsd/client.py > tools/nexsd/client.py
git show origin/devtest:tools/nexsd/Makefile > tools/nexsd/Makefile
```

### 10.2 VSCode Extension

```bash
mkdir -p tools/vscode-nexs/syntaxes tools/vscode-nexs/snippets
git show origin/devtest:tools/vscode-nexs/extension.js > tools/vscode-nexs/extension.js
git show origin/devtest:tools/vscode-nexs/package.json > tools/vscode-nexs/package.json
git show origin/devtest:tools/vscode-nexs/language-configuration.json > tools/vscode-nexs/language-configuration.json
git show origin/devtest:tools/vscode-nexs/syntaxes/nexs.tmLanguage.json > tools/vscode-nexs/syntaxes/nexs.tmLanguage.json
git show origin/devtest:tools/vscode-nexs/snippets/snippets.json > tools/vscode-nexs/snippets/snippets.json
git show origin/devtest:scripts/install-extension.sh > scripts/install-extension.sh
chmod +x scripts/install-extension.sh
```

### 10.3 GitHub Actions linting

```bash
mkdir -p .github/workflows
git show origin/devtest:.github/nexs-matcher.json > .github/nexs-matcher.json
git show origin/devtest:.github/workflows/nexs-linter.yml > .github/workflows/nexs-linter.yml
```

### 10.4 Architecture mapper

```bash
git show origin/devtest:scratch/mapper.py > scratch/mapper.py
chmod +x scratch/mapper.py
```

### 10.5 Compila e testa `nexsd`

```bash
cd tools/nexsd && make
./nexsd &
python3 client.py
kill %1
cd ../..
```

-----

## 11. FASE 11 — Documentazione e pulizia finale

### 11.1 Copia documentazione

```bash
git show origin/devtest:nexs_full_map.md > nexs_full_map.md
git show origin/dev-termimgtestmultitasking:LOGO_IMPLEMENTATION.md > docs/LOGO_IMPLEMENTATION.md
git show origin/dev-termimgtestmultitasking:docs/LOGO_SYSTEM.md > docs/LOGO_SYSTEM.md
```

### 11.2 Aggiorna `STATO.md`

Documenta tutte le fasi completate, i bug fixati (kmsg, regfs CRC, walk_pipes, vfs O(n)), e lo stato baremetal.

### 11.3 Elimina artefatti obsoleti

```bash
rm -f test.out
rm -f scratch/bt_crash.c scratch/crash_repro.exp
rm -f scratch/test_crash.nx scratch/test_crash_2.nx
rm -f "example/nxed_editor copy.nx" # spazio nel nome — artefatto
```

-----

## 12. TEST SUITE COMPLETO

Esegui questi test nell’ordine. Ogni fase deve passare prima di procedere.

### 12.1 Build pulita

```bash
make clean && make 2>&1 | grep -E "error:|warning:" | grep -v "may be truncated"
# Zero errori.
```

### 12.2 Test regressione linguaggio

```bash
./nexs example/test.nx
# Output atteso: 42, 3.14, NEXS, 50, 3, 1, 10, Hello World!, 12, hello world, false, true, false, true, true, 300, 7, 120, positivo, 45

./nexs example/example_lib.nx
# Output atteso: messaggi [Main] con versione lib e risultati quadrato/potenza
```

### 12.3 Test terminal syscalls

```bash
cat > /tmp/test_term.nx << 'EOF'
sz = term_size()
out "Dimensioni: " + sz
set_fg_pid(-1)
rawon()
k = readkey()
rawoff()
out "Key: " + k
term_cls()
term_at(1 1 "OK")
term_flush()
sleep(300)
term_cls()
out "Test term OK"
EOF
./nexs /tmp/test_term.nx
```

### 12.4 Test PM spawn

```bash
cat > /tmp/test_pm.nx << 'EOF'
exec("services/stdlib.nx")
exec("services/pm/init.nx")
out "PM stato: " + str(reg_get("/sys/pm/status"))
pid = pm_spawn(1 "example/minios/shell.nx" "")
out "Spawned PID: " + str(pid)
EOF
./nexs /tmp/test_pm.nx
```

### 12.5 Test minios completo

```bash
./nexs example/minios/boot.nx
# Test manuali: ps, ls, cd /, pwd, nxed, termimg sd02/logos/logo1.nxtimg
```

### 12.6 Test compiler

```bash
./nexs --compile example/test.nx -o /tmp/test_compiled
/tmp/test_compiled
# Output identico a ./nexs example/test.nx
```

### 12.7 Test baremetal amd64 (QEMU)

```bash
make baremetal-amd64 2>&1 | tail -5
dd if=/dev/zero of=/tmp/disk.img bs=1M count=64
qemu-system-x86_64 -kernel build/baremetal-amd64/nexs.elf \
-serial stdio -display none -m 256M \
-drive file=/tmp/disk.img,format=raw \
-no-reboot 2>&1 | head -30
```

### 12.8 Test baremetal arm64 (QEMU)

```bash
make baremetal-arm64 2>&1 | tail -5
qemu-system-aarch64 -M virt -cpu cortex-a57 \
-kernel build/baremetal-arm64/nexs.elf \
-serial stdio -display none -m 128M \
-no-reboot 2>&1 | head -30
```

-----

## 13. COSA NON PORTARE (lista definitiva)

|File/Componente |Motivo |
|-------------------------------------------------------------------------|---------------------------------------------------------|
|`hal/module/driver.hpp` |C++ — viola regola no-external-deps |
|`hal/module/uart_cpp.cpp` |C++ |
|`kernel/libcxx_stub.cpp` |C++ |
|`stub.zip` |Binario, non versionabile |
|`kernel/vfs_server.c` (abilitato) |Richiede `TYPE_MAP` non ancora stabile |
|`experimental/hal_bc/` |Spostato in `experimental/` per un motivo |
|`services/regfs.nx` (devtest) |Versione devtest è meno stabile di main |
|Rinomina `services/` → `library/` |Troppo invasiva, rompe compatibilità con script esistenti|
|`example/nxed_editor copy.nx` |File con spazio nel nome — già rimosso in fase 11 |
|`rawon/rawoff/readkey/term_at/cls/flush` da `dev-termimgtestmultitasking`|Già presenti in main — non sovrascrivere |

-----

## 14. COMMIT STRATEGY

```bash
git add -A && git commit -m "phase 1: HAL common console/timer, remove duplication"
git add -A && git commit -m "phase 2: kernel IPC seL4, cap system, sys_brk"
git add -A && git commit -m "phase 3: builtins TextBuffer, dep_scan cleanup, kmsg fix, walk_pipes iterative"
git add -A && git commit -m "phase 4: add term_size, set_fg_pid, baremetal guards audit"
git add -A && git commit -m "phase 5: PM multitasking full rewrite, mangle, session manager, exits() fix"
git add -A && git commit -m "phase 6: .nxtimg image system, termimg viewer, logos"
git add -A && git commit -m "phase 7: stdlib.nx unified"
git add -A && git commit -m "phase 8: NXED v4.0, shell updated"
git add -A && git commit -m "phase 9: regfs CRC fix, vfs path_to_ino O(1), TASK_SCOPE cleanup"
git add -A && git commit -m "phase 10: nexsd language server, VSCode extension, CI linting"
git add -A && git commit -m "phase 11: docs, STATO.md updated, artefatti rimossi"
```

-----

## 15. REGOLE ARCHITETTURALI DA RISPETTARE

Durante tutto il lavoro, rispetta queste regole del progetto:

1. **No C++** — zero file `.cpp` o `.hpp` nel build principale
1. **No memoria condivisa** — ogni comunicazione cross-process via `sendmessage`/`receivemessage`
1. **Ogni errore → registry** — `/sys/errors/<timestamp>/msg = "..."`
1. **Funzioni max 60 righe** — se più lunga, splitta
1. **Codice vecchio → `old/`** — mai `git rm` a meno che il file sia dichiaratamente da eliminare
1. **IPC keyword = nome C** — `sendmessage` ↔ `nexs_sendmessage` ↔ registrato come `"sendmessage"`
1. **Test QEMU prima di dichiarare baremetal OK**
1. **Build pulita = zero errori, warning “truncation” esistenti tollerati**
1. **Non sovrascrivere funzioni già presenti in main** — verifica sempre con `grep -n` prima di aggiungere

-----

## Note finali per Claude Code

- Leggi sempre il file intero prima di modificarlo
- Prima di portare qualsiasi funzione da un altro ramo, verifica con `grep -n "nome_funzione" file.c` che non esista già in main
- Dopo ogni `str_replace`, rileggi il file per verificare la coerenza
- Se un test fallisce in una fase, NON procedere alla fase successiva
- In caso di dubbio su un conflitto tra le due branch, preferisci la versione più conservativa
- Il ramo deve sempre compilare su Linux host — questo è il requisito minimo
- Usa `grep -n` per trovare esattamente le righe da modificare prima di usare `str_replace`