#!/bin/sh
# scripts/qemu-arm64.sh — Run NEXS on QEMU AArch64 (virt machine)
set -e

# --compile ... -o out/kernel.elf produces the selected runtime image.
# Never silently run a stale Make output.
ELF="${NEXS_QEMU_ELF:-out/kernel.elf}"
if [ "$#" -gt 0 ]; then
    case "$1" in
        *.elf) ELF="$1"; shift ;;
    esac
fi

if [ ! -f "$ELF" ]; then
    echo "Error: runtime ELF not found: $ELF" >&2
    echo "Compile with: ./nxscript --compile example/minios/boot.nx --target baremetal-arm64 -o out/kernel.elf" >&2
    exit 1
fi
echo "[QEMU] Runtime image: $ELF" >&2

exec qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a57 \
    -m "${NEXS_QEMU_RAM_MIB:-512}M" \
    -kernel "$ELF" \
    -serial stdio \
    -display none \
    -no-reboot \
    "$@"
