#!/bin/sh
# =============================================================================
# scripts/qemu-amd64.sh — Run NEXS on QEMU x86-64
# =============================================================================
set -e

ELF="build/baremetal-amd64/nexs.elf"
ISO="build/nexs-amd64.iso"

if [ "$1" = "--iso" ]; then
    shift
    if [ ! -f "$ISO" ]; then
        echo "Error: $ISO not found. Run './scripts/make-iso.sh' first."
        exit 1
    fi
    echo "Booting from ISO (CD-ROM)..."
    exec qemu-system-x86_64 \
        -machine pc,smm=off \
        -m 256M \
        -cdrom "$ISO" \
        -boot order=dc \
        -nographic \
        -no-reboot \
        -debugcon file:limine.log \
        "$@"

elif [ "$1" = "--img" ]; then
    shift
    IMG="build/nexs-amd64.img"
    if [ ! -f "$IMG" ]; then
        echo "Error: $IMG not found. Run './scripts/make-img.sh' first."
        exit 1
    fi
    echo "Booting from Disk Image (Headless)..."
    exec qemu-system-x86_64 \
        -drive file="$IMG",format=raw,file.locking=off \
        -m 256M \
        -nographic \
        -no-reboot \
        "$@"

else
    # Default: boot from ELF
    if [ ! -f "$ELF" ]; then
        echo "Error: $ELF not found. Run 'make baremetal-amd64' first."
        exit 1
    fi
    echo "Booting from ELF (Default -kernel)..."
    exec qemu-system-x86_64 \
        -kernel "$ELF" \
        -m 128M \
        -nographic \
        -no-reboot \
        "$@"
fi