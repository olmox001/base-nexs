#!/bin/sh
# scripts/make-iso.sh — Create bootable x86-64 ISO (stage1 MBR + stage2 + kernel)
#
# Requires: nasm, x86_64-elf-gcc (or gcc), dd, mkisofs/genisoimage
#
# Disk layout (raw image):
#   Sector 0       : stage1.S (MBR, 512 bytes)
#   Sectors 1-62   : stage2.S (31 KB)
#   Sectors 63+    : nexs.elf (kernel)
set -e

STAGE1="hal/amd64/boot/stage1.S"
STAGE2="hal/amd64/boot/stage2.S"
KERNEL="build/baremetal-amd64/nexs.elf"
OUT_IMG="build/nexs-amd64.img"
OUT_ISO="build/nexs-amd64.iso"

mkdir -p build

if [ ! -f "$KERNEL" ]; then
    echo "Error: $KERNEL not found. Run 'make baremetal-amd64' first."
    exit 1
fi

echo "Extracting flat binary from kernel ELF..."
/usr/local/opt/x86_64-elf-binutils/bin/x86_64-elf-objcopy -O binary "$KERNEL" build/nexs.bin

echo "Assembling stage1..."
nasm -f bin -o build/stage1.bin "$STAGE1"

echo "Assembling stage2..."
nasm -f bin -o build/stage2.bin "$STAGE2"

# Verify stage1 is exactly 512 bytes
S1_SZ=$(wc -c < build/stage1.bin)
if [ "$S1_SZ" -ne 512 ]; then
    echo "Warning: stage1.bin is $S1_SZ bytes (expected 512)"
fi

echo "Building disk image..."
# Create 64 MB zeroed image
dd if=/dev/zero of="$OUT_IMG" bs=512 count=131072 2>/dev/null

# Write stage1 at sector 0
dd if=build/stage1.bin of="$OUT_IMG" bs=512 seek=0 conv=notrunc 2>/dev/null

# Write stage2 at sector 1
dd if=build/stage2.bin of="$OUT_IMG" bs=512 seek=1 conv=notrunc 2>/dev/null

# Write kernel at sector 63
dd if=build/nexs.bin of="$OUT_IMG" bs=512 seek=63 conv=notrunc 2>/dev/null

echo "Created raw image: $OUT_IMG"

# Wrap in ISO using El Torito (if mkisofs available)
if command -v mkisofs >/dev/null 2>&1; then
    mkdir -p build/iso_root
    cp "$OUT_IMG" build/iso_root/nexs.img
    mkisofs -o "$OUT_ISO" \
        -b nexs.img \
        -no-emul-boot \
        -boot-load-size 4 \
        -boot-info-table \
        build/iso_root 2>/dev/null || true
    echo "Created ISO: $OUT_ISO"
elif command -v genisoimage >/dev/null 2>&1; then
    mkdir -p build/iso_root
    cp "$OUT_IMG" build/iso_root/nexs.img
    genisoimage -o "$OUT_ISO" \
        -b nexs.img \
        -no-emul-boot \
        -boot-load-size 4 \
        -boot-info-table \
        build/iso_root 2>/dev/null || true
    echo "Created ISO: $OUT_ISO"
else
    echo "mkisofs/genisoimage not found — skipping ISO creation"
fi

echo "Done."
echo ""
echo "Test with:"
echo "  qemu-system-x86_64 -drive file=$OUT_IMG,format=raw -m 128M -serial stdio -display none"
