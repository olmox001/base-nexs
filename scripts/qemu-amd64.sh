#!/bin/sh
# scripts/qemu-amd64.sh — Run NEXS on QEMU x86-64 (multiboot2 kernel)
set -e

IMG="build/nexs-amd64.img"

if [ ! -f "$IMG" ]; then
    echo "Rebuilding image..."
    scripts/make-iso.sh
fi

exec qemu-system-x86_64 \
    -drive file="$IMG",format=raw \
    -m 128M \
    -serial stdio \
    -display none \
    -no-reboot \
    -d int,cpu_reset \
    -D /Users/olmo/Documents/git/base-nexs/qemu.log \
    "$@"
