---
name: NEXS project overview
description: Current state of the NEXS language project — modules, features, plan
type: project
---

NEXS is a Plan 9-inspired scripting language at /Users/olmo/Documents/git/base-nexs/base-nexs.

**Why:** Long-term goal is a bootable OS with 9P filesystem. PLAN.md has 20-step roadmap.

**How to apply:** When the user asks to continue, check PLAN.md for the next pending step.

## Module layout
core/ registry/ lang/ sys/ compiler/ hal/ runtime/
old/src — legacy monolithic source (not compiled)
include/nexs.h — single-include public embedding API

## Key features implemented
- fn_table owns all function AST bodies (fixes memory leaks)
- TYPE_PTR: registry pointers with deref chaining (up to 32 hops)
- IPC message queues per-registry-key (sendmessage/receivemessage/msgpending)
- Builtin signature display: `<builtin: open(path str, mode int) → fd int>`
- IPC keywords support both literal path form (`sendmessage /q v`) and fn-call form (`sendmessage("/q" v)`)
- HAL bytecode (HALB): UTF-8 encoded Plan 9-style device abstraction, executor in hal/bc/
- Compiler pipeline: .nx → C → GCC for 7 targets (linux-amd64/arm64, macos-amd64/arm64, plan9-amd64, baremetal-arm64/amd64)
- PLAN.md: Steps 01-20 not yet executed (next: hal/amd64 GDT+IDT+long mode)
