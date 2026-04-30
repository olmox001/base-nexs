#!/bin/sh
# scripts/qemu-amd64.sh — Run NEXS on QEMU x86-64
set -e

ELF="build/baremetal-amd64/nexs.elf"
ISO="build/nexs-amd64.iso"

# Gestione flag per ISO e IMG
if [ "$1" = "--iso" ]; then
    shift
    if [ ! -f "$ISO" ]; then
        echo "Error: $ISO not found. Run './scripts/make-iso.sh' first."
        exit 1
    fi
    echo "Booting from ISO (Headless)..."
    exec qemu-system-x86_64 -cdrom "$ISO" -m 128M -nographic -no-reboot "$@"
    
elif [ "$1" = "--img" ]; then
    shift
    if [ ! -f "$ISO" ]; then
        echo "Error: $ISO not found. Run './scripts/make-iso.sh' first."
        exit 1
    fi
    echo "Booting from Disk Image (Headless)..."
    exec qemu-system-x86_64 -drive file="$ISO",format=raw -m 128M -nographic -no-reboot "$@"

else
    # Comportamento di default (identico a qemu-arm64.sh)
    if [ ! -f "$ELF" ]; then
        echo "Error: $ELF not found. Run 'make baremetal-amd64' first."
        exit 1
    fi
    echo "Booting from ELF (Default -kernel)..."
    exec qemu-system-x86_64 \
        -kernel "$ELF" \
        -m 128M \
        -serial stdio \
        -display none \
        -no-reboot \
        "$@"
fi