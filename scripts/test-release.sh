#!/bin/bash
# =============================================================================
# scripts/test-release.sh — Quick Tester for NEXS Release Artifacts
# =============================================================================
# Usage: ./scripts/test-release.sh [1-6]
# 1: MacOS Interpreter  2: MacOS MINIOS (AOT)
# 3: Baremetal ELF      4: Baremetal ISO
# 5: MiniOS ELF         6: MiniOS ISO
# =============================================================================

set -e

VERSION="v9.9.9"
REL_DIR="RELEASE/$VERSION"

if [ ! -d "$REL_DIR" ]; then
    echo "Error: Release directory $REL_DIR not found."
    echo "Run './scripts/make-release.sh ' first."
    exit 1
fi

CHOICE="${1:-}"

case "$CHOICE" in
    1)
        echo "Testing Nexs-amd64-MacOS (Interpreter)..."
        "./$REL_DIR/Nexs-amd64-MacOS"
        ;;
    2)
        echo "Testing Nexs-amd64-MacOS-MINIOS (AOT)..."
        "./$REL_DIR/Nexs-amd64-MacOS-MINIOS"
        ;;
    3)
        echo "Testing Nexs-amd64-STANDALONE.elf (QEMU -kernel)..."
        qemu-system-x86_64 -kernel "$REL_DIR/Nexs-amd64-STANDALONE.elf" -m 128M -nographic -no-reboot
        ;;
    4)
        echo "Testing Nexs-amd64-STANDALONE.iso (QEMU -cdrom)..."
        qemu-system-x86_64 -cdrom "$REL_DIR/Nexs-amd64-STANDALONE.iso" -m 256M -nographic -no-reboot
        ;;
    5)
        echo "Testing Nexs-amd64-STANDALONE-MINIOS.elf (QEMU -kernel)..."
        qemu-system-x86_64 -kernel "$REL_DIR/Nexs-amd64-STANDALONE-MINIOS.elf" -m 128M -nographic -no-reboot
        ;;
    6)
        echo "Testing Nexs-amd64-STANDALONE-MINIOS.iso (QEMU -cdrom)..."
        qemu-system-x86_64 -cdrom "$REL_DIR/Nexs-amd64-STANDALONE-MINIOS.iso" -m 256M -nographic -no-reboot
        ;;
    7)
        echo "Testing Nexs-amd64-Linux (Interpreter)..."
        if [ "$(uname)" = "Linux" ]; then
            "./$REL_DIR/Nexs-amd64-Linux"
        else
            echo "Error: Cannot run Linux binary on $(uname). Use a Linux environment or Docker."
            exit 1
        fi
        ;;
    8)
        echo "Testing Nexs-amd64-Linux-MINIOS (AOT)..."
        if [ "$(uname)" = "Linux" ]; then
            "./$REL_DIR/Nexs-amd64-Linux-MINIOS"
        else
            echo "Error: Cannot run Linux binary on $(uname). Use a Linux environment or Docker."
            exit 1
        fi
        ;;
    *)
        echo "Usage: $0 [1-8]"
        echo "  1: Nexs-amd64-MacOS (Host)"
        echo "  2: Nexs-amd64-MacOS-MINIOS (Host AOT)"
        echo "  3: Nexs-amd64-STANDALONE.elf (Baremetal ELF)"
        echo "  4: Nexs-amd64-STANDALONE.iso (Baremetal ISO)"
        echo "  5: Nexs-amd64-STANDALONE-MINIOS.elf (MiniOS ELF)"
        echo "  6: Nexs-amd64-STANDALONE-MINIOS.iso (MiniOS ISO)"
        echo "  7: Nexs-amd64-Linux (Linux Host)"
        echo "  8: Nexs-amd64-Linux-MINIOS (Linux AOT)"
        exit 1
        ;;
esac
