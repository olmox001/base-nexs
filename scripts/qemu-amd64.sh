#!/bin/sh
# scripts/qemu-amd64.sh — Run NEXS on QEMU x86-64 (multiboot2 kernel)
set -e

ELF="build/baremetal-amd64/nexs.elf"

if [ ! -f "$ELF" ]; then
    echo "Error: $ELF not found. Run 'make baremetal-amd64' first."
    exit 1
fi

exec qemu-system-x86_64 \
    -kernel "$ELF" \
    -m 128M \
    -serial stdio \
    -display none \
    -no-reboot \
    -d int,cpu_reset \
    -D /Users/olmo/Documents/git/base-nexs/qemu.log \
    "$@"
