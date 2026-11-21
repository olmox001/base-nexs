# P1 Lexer / Literal and Path Integrity — Executed Verification

This document records a verified, independently stageable part of P1.
It is NOT a claim that P1 allocator/value/VM/registry rearchitecture is
complete. It replaces silent truncation and the unsafe lexical number
conversion path, without duplicating the existing language implementation.

## Replacements and source-level invariants

- lang/include/nexs_lex.h, lang/lexer.c: retain a borrowed source slice
  for TK_STRING and TK_REGPATH; keep only a small non-authoritative token
  preview for diagnostics and fixed ABI identifiers. No 8 KiB inline Token
  is created for every AST node, and the lexer does not allocate per token.
- lexer_token_copy: bounded, complete decoding into a caller-owned buffer;
  never returns success with truncated data; rejects embedded NUL and
  invalid/out-of-range octal escapes. C-style \033 remains supported to
  preserve GUI/TTY ANSI sequence output from existing NEXS services.
- lang/parser.c: every path-bearing AST construction and literal uses
  lexer_token_copy and propagates overflow as a parser failure; import
  filenames and registry paths can exceed the original 63-char token
  preview. Full string literals are passed through val_str without
  truncation. The AST copies no borrowed source pointers into retained
  function bodies; ast_free continues to own all AST values.
- lang/parser.c, lang/eval.c: AST_PTR_SET target path uses the owned Value
  slot, not the unrelated name[NAME_LEN] field; eval reads the actual full
  target. No second pointer implementation was introduced.
- Numeric tokens consume the entire lexeme and check boundaries without
  errno or host-only strtoll. Signed 64-bit min/max are valid; values
  outside the range are errors. Invalid decimal literals such as 1.2.3
  are not silently interpreted as a prefix.
- Current Registry/RegKey.name still limits each *component* of a registry
  path to NAME_LEN - 1. The lexer now rejects an overlength registry
  component instead of creating a silently truncated, unreachable key.
  Complete removal of this old limit requires the P3 canonical NXREG
  migration, not an unreviewed widening of every RegKey allocation.

## Executed verification

- tests/test_lexer_long_literals.py: PASS 18/18 dedicated tests.
  Exercises literal sizes 63, 64, 128, 256, 512 and 8191; overlimit
  rejection; NUL and unterminated literals; escaped newline/quotes/octals;
  malformed/overflowing numbers; exact int64 min/max; long import paths,
  registry paths and pointer targets; and real macOS AMD64 AOT output.
- Real Desktop Commander interactive terminal: launched ./nxscript,
  entered out 42, entered an approximately 200-character string, and
  sent :exit. The runtime printed both values and returned exit status 0.
- Baremetal via public NEXS AOT CLI and real QEMU, AMD64 and ARM64:
  both architectures booted an independently generated ELF and printed
  LEXER_FINAL_BAREMETAL, 9223372036854775807,
  -9223372036854775808 and the complete long literal. All markers
  were programmatically checked in the guest UART log; no fault detected.
- make power12-phase0: PASS 14 structural/compiler AST tests after
  regenerating source SHA-256 evidence and strict issue assignments.
- make os1-check: PASS. OS1 x86_64/AArch64 cross-linked ELF ABI
  verification and the concurrent two-program OS1 AOT isolation test passed.
- make test: PASS 59 Python tests, 34/34 NEXS syntax sources, 17/17
  noninteractive executed examples (separate from 3 interactive examples
  that require their own operation-specific assertions).
- make check-examples: 17 PASS and 3 UNVERIFIED_INTERACTIVE, an
  independently recorded limit of the existing noninteractive harness.
  This is not a claim that the three interactive applications passed.

## Explicit exclusions and unresolved work

- Legacy allocator xmalloc/die still terminates on OOM. A fully safe
  per-instance arena and explicit fallible allocation API are still P1.
- Large registry *components* are explicitly rejected (no silent
  corruption); the planned dynamic registry and capability authority
  are P3. Existing independently supplied registry paths are not all
  covered by this lexer-level validation.
- The parser remains recursive and the ABI retains other fixed buffers.
- No NexsOS1 guest nxadb result is claimed: umbrella Makefile references
  missing user/NexsOS1-main/portability/doom/doom.mk, outside the
  permitted edit boundary.
- Power12 strict-all is NOT green: the current traceable baseline is
  280 Base-NEXS issues plus 72 unadapted Linux 9P gotos in NXOLMO.
  This work grants no exceptions and does not falsify per-function tests.
- NXOLMO, NXEC, NexsOS1 umbrella, CORE, SDK, USERKIT remain read-only.
  No commit or push was performed.
