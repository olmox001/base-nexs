# NEXS dev-stable — Stato corrente

**Branch:** `dev-stable`
**Data:** 2026-05-05
**Ultima commit:** `32b501d` — bugfix compiler/driver.c AOT

---

## Fasi completate

| Fase | Commit | Stato |
|------|--------|-------|
| 0. Setup iniziale (branch, verifica build) | `db2159f` | ✅ |
| 1. HAL refactoring: `hal/common/console.c`, `timer.c`, `hal_internal.h` | `db2159f` | ✅ |
| 2. Kernel IPC seL4, Capabilities, sys_brk | `4027ee3` | ✅ |
| Bugfix AOT driver (`compiler/driver.c`) | `32b501d` | ✅ |

---

## Bugfix AOT — Cosa è stato fixato

**Problema 1 (hosted):** `make-release.sh` AOT build falliva con `undefined symbols: _g_hal_driver, _nexs_hal_print, _nexs_hal_putc, _nexs_hal_getc`.

**Causa:** `compiler/driver.c` generava il comando `gcc` senza `hal/common/console.c` e `hal/common/timer.c`.

**Problema 2 (baremetal):** Dopo il fix del problema 1, il build baremetal falliva con `multiple definition` perché `hal/amd64/uart.c` e `hal/amd64/apic.c` già definiscono le stesse funzioni.

**Fix definitivo:** `hal/common/console.c` e `hal/common/timer.c` vanno inclusi **solo** nel ramo hosted (non baremetal). In `driver.c`:
- `is_baremetal == false` → aggiunge `hal/common/console.c hal/common/timer.c hal/hal_hosted.c`
- `is_baremetal == true` → aggiunge solo `kernel/libc_stub.c` + file arch-specifici

---

## Da dove ricominciare — FASE 3

**File:** `NEXS_STABLE_MERGE_INSTRUCTIONS.md`, sezione `## 3. FASE 3`

### Checklist Fase 3:

- [ ] **3.1** Builtins TextBuffer in `lang/builtins.c`:
  - Aggiungere `builtin_tb_create`, `builtin_tb_load`, `builtin_tb_to_str`, `builtin_tb_insert_char`, `builtin_tb_delete_char`, `builtin_sys_bundled`
  - Aggiungere le registrazioni in `builtins_init()`
  - Fonte: `git diff main..origin/devtest -- lang/builtins.c`
  - Aggiungere **prima** della funzione `builtins_init()`, non sovrascrivere nulla di esistente

- [ ] **3.2** `compiler/dep_scan.c` — sostituzione diretta da devtest (rimuove `path_dirname`/`path_join` locali, usa quelle di `core/utils.c`):
  - `git show origin/devtest:compiler/dep_scan.c > compiler/dep_scan.c`

- [ ] **3.3** Fix bug `kmsg_encode` in `kernel/msg.c` — separatore `:` → `\x1F`:
  - Sostituire in `kmsg_encode`: `snprintf(buf, ..., "%s\x1f%s\x1f%s\x1f%lld\x1f%u", ...)`
  - Sostituire in `kmsg_decode`: tutte le 5 chiamate `strtok(p, ":")` → `strtok(p, "\x1f")`
  - **Verifica prima:** `grep -n "kmsg_encode\|kmsg_decode\|strtok" kernel/msg.c`

- [ ] **3.4** Fix bug `walk_and_enable_pipes` in `registry/reg_ipc.c` — ricorsione → iterativa:
  - Convertire con stack esplicito `RegKey *stack[1024]`
  - Pattern: stesso usato in `registry.c` per `regkey_free_recursive`
  - **Verifica prima:** `grep -n "walk_and_enable_pipes" registry/reg_ipc.c`

- [ ] **3.5** Test: `make clean && make && ./nexs example/test.nx && ./nexs example/example_lib.nx`

---

## Struttura file chiave modificati nelle fasi 0-2

```
hal/
  common/
    console.c       ← NUOVO (fase 1) — g_hal_driver, nexs_hal_putc/getc/print
    timer.c         ← NUOVO (fase 1) — g_hal_ticks, hal_timer_ticks/sleep_ms
  include/
    hal_internal.h  ← NUOVO (fase 1) — HalDriver struct
    nexs_mmu.h      ← AGGIORNATO (fase 1)
    nexs_timer.h    ← NUOVO (fase 1)
  hal_hosted.c      ← INVARIATO (registra driver hosted via constructor)

kernel/
  ipc.c             ← NUOVO (fase 2) — IPC seL4-style, Endpoint queue
  cap.c             ← NUOVO (fase 2) — CNode, cap_insert/revoke
  sys_brk.c         ← NUOVO (fase 2) — heap management, NEXS_HEAP_BASE configurabile
  vfs_server.c      ← NUOVO (fase 2, DISABILITATO con #ifdef NEXS_VFS_SERVER_ENABLED)
  include/
    nexs_ipc.h      ← NUOVO (fase 2)
    nexs_cap.h      ← NUOVO (fase 2)
    nexs_proc.h     ← AGGIORNATO (fase 2) — aggiunti mmu_root, heap_start/end, cspace, ipc_msg/badge, reply_ep

core/include/
  nexs_fd.h         ← NUOVO (fase 2)
  nexs_alloc.h      ← AGGIORNATO (fase 2) — guard NEXS_BAREMETAL per stdio, aggiunto xrealloc

compiler/
  driver.c          ← AGGIORNATO (bugfix) — hal/common incluso solo in hosted build

Makefile            ← AGGIORNATO (fasi 1+2) — KERNEL_SRCS con ipc.c, cap.c, sys_brk.c
```

---

## Stato build

```
make clean && make   → Build OK -> ./nexs
./nexs example/test.nx → 42, 3.14, NEXS, ... OK (nessuna regressione)
AOT hosted:  ./nexs --compile example/minios/boot.nx -o /tmp/t → Done ✅
AOT baremetal: ./nexs --compile ... --target baremetal-amd64 → Done ✅
```

---

## Regole architetturali da rispettare

1. No C++ (.cpp / .hpp)
2. `hal/common/*.c` solo in hosted; baremetal usa `hal/amd64/uart.c` + `apic.c` direttamente
3. Nuovi kernel file → solo in `KERNEL_SRCS` nel Makefile, mai in `SRCS`
4. `vfs_server.c` rimane disabilitato (richiede TYPE_MAP non ancora stabile)
5. Non sovrascrivere funzioni già presenti in main — verifica sempre con `grep -n` prima
