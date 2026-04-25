#!/bin/sh
# scripts/flash-sd.sh — Create ARM64 SD card image for NEXS
#
# Produces an SD card image with:
#   Partition 1 (FAT32, 64 MB): kernel8.img (raw ELF or flat binary)
#                                 config.txt, cmdline.txt (RPi style)
#   Partition 2 (future: regfs/ext2 root)
#
# For QEMU test: use -drive file=nexs-arm64.img,format=raw
# For real HW:   dd if=nexs-arm64.img of=/dev/sdX bs=1M status=progress
set -e

KERNEL="build/baremetal-arm64/nexs.elf"
OUT_IMG="build/nexs-arm64.img"
MOUNT_TMP="build/sd_mount"

mkdir -p build "$MOUNT_TMP"

if [ ! -f "$KERNEL" ]; then
    echo "Error: $KERNEL not found. Run 'make baremetal-arm64' first."
    exit 1
fi

echo "Creating SD image (128 MB)..."
dd if=/dev/zero of="$OUT_IMG" bs=1M count=128 2>/dev/null

# Partition table: one FAT32 partition from sector 2048
if command -v sfdisk >/dev/null 2>&1; then
    printf '2048,131072,0x0C\n' | sfdisk "$OUT_IMG" 2>/dev/null || true
fi

# Format first partition as FAT32 (use loopback on Linux, hdiutil on macOS)
if [ "$(uname)" = "Darwin" ]; then
    # macOS: attach image and format
    LOOP=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage \
           -nomount "$OUT_IMG" 2>/dev/null | awk '{print $1}' | head -1)
    if [ -n "$LOOP" ]; then
        # Format the whole image as FAT32 (simpler on macOS without partition support)
        newfs_msdos -F 32 -v NEXS "${LOOP}" 2>/dev/null || true
        # Mount and copy files
        mkdir -p "$MOUNT_TMP"
        mount -t msdos "${LOOP}" "$MOUNT_TMP" 2>/dev/null || true
        if mountpoint -q "$MOUNT_TMP" 2>/dev/null || mount | grep -q "$MOUNT_TMP"; then
            cp "$KERNEL" "$MOUNT_TMP/kernel8.img"
            printf 'enable_uart=1\narm_64bit=1\n' > "$MOUNT_TMP/config.txt"
            printf 'nexs_baremetal\n' > "$MOUNT_TMP/cmdline.txt"
            sync
            umount "$MOUNT_TMP" 2>/dev/null || diskutil unmount "$MOUNT_TMP" 2>/dev/null || true
        fi
        hdiutil detach "${LOOP}" 2>/dev/null || true
    fi
else
    # Linux: use loop device
    LOOP=$(losetup -f --show "$OUT_IMG" 2>/dev/null)
    if [ -n "$LOOP" ]; then
        mkfs.vfat -F 32 -n NEXS "${LOOP}" 2>/dev/null || true
        mount "${LOOP}" "$MOUNT_TMP" 2>/dev/null || true
        cp "$KERNEL" "$MOUNT_TMP/kernel8.img"
        printf 'enable_uart=1\narm_64bit=1\n' > "$MOUNT_TMP/config.txt"
        printf 'nexs_baremetal\n' > "$MOUNT_TMP/cmdline.txt"
        sync
        umount "$MOUNT_TMP" 2>/dev/null || true
        losetup -d "${LOOP}" 2>/dev/null || true
    fi
fi

echo "Created: $OUT_IMG"
echo ""
echo "QEMU test:"
echo "  qemu-system-aarch64 -M virt -cpu cortex-a57 -m 128M \\"
echo "    -kernel $KERNEL -serial stdio -display none"
echo ""
echo "Real hardware (DANGEROUS — verify /dev/sdX):"
echo "  sudo dd if=$OUT_IMG of=/dev/sdX bs=1M status=progress && sync"
