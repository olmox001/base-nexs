#!/bin/sh
# scripts/qemu-arm64.sh — Run NEXS on QEMU AArch64 (virt machine)
set -e

ELF="build/baremetal-arm64/nexs.elf"

if [ ! -f "$ELF" ]; then
    echo "Error: $ELF not found. Run 'make baremetal-arm64' first."
    exit 1
fi

exec qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a57 \
    -m 128M \
    -kernel "$ELF" \
    -serial stdio \
    -display none \
    -no-reboot \
    "$@"
