# NEXS dev-stable — Stato Corrente (Sessione 2 - COMPLETATA)

**Branch:** `dev-stable`
**Data:** 2026-05-06
**Stato:** Fase 3 COMPLETATA ✅

---

## 1. Lavoro Completato (Fase 3)

Tutti i punti della **Fase 3** (`NEXS_STABLE_MERGE_INSTRUCTIONS.md`) sono stati implementati e verificati.

### Modifiche consolidate:
- [x] **3.1 Builtins TextBuffer**: Aggiunti `tb_create`, `tb_load`, `tb_to_str`, `tb_insert_char`, `tb_delete_char` e `sys_bundled`.
- [x] **3.2 `compiler/dep_scan.c`**: Refattorizzazione completa (uso di `core/utils.c`).
- [x] **3.3 Fix `kernel/msg.c`**: Separatore IPC cambiato in `\x1f`.
- [x] **3.4 Fix `registry/reg_ipc.c`**: Walker del registry reso iterativo.
- [x] **Bugfix Baremetal**: Risolta l'assenza di `strcat` in `lang/builtins.c` sostituendolo con `strcpy`.

---

## 2. Esito Verifiche

- **Build**: `make-release.sh` completato con successo per tutti i target.
- **Test**:
  - `example/test.nx`: OK
  - `example/example_lib.nx`: OK
  - Test manuale TextBuffer: OK

---

## 3. Prossimi Passi — FASE 4

La prossima sessione dovrà iniziare dalla **Fase 4** delle istruzioni:
**File:** `NEXS_STABLE_MERGE_INSTRUCTIONS.md`, sezione `## 4. FASE 4 — Terminal syscalls`

Punti chiave Fase 4:
- Aggiunta `term_size` e `set_fg_pid` in `sys/sysio.c`.
- Integrazione delle nuove syscall nel dispatcher.
- Verifica con l'editor `nxed`.
