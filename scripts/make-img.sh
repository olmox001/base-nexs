#!/bin/bash
# =============================================================================
# scripts/make-img.sh — NEXS HDD Image Packager (Fixed Mpartition)
# =============================================================================
set -e

KERNEL="build/baremetal-amd64/nexs.elf"
OUT_IMG="build/nexs-amd64.img"
IMG_SIZE=64 # MB

# 1. Verifica Tools
if ! command -v mtools >/dev/null 2>&1; then
    echo "Errore: mtools non trovato."
    exit 1
fi

BREW_PREFIX=$(brew --prefix)
LIMINE_BIN="$BREW_PREFIX/bin/limine"
LIMINE_SHARE="$BREW_PREFIX/share/limine"

# 2. Crea file immagine vuoto
dd if=/dev/zero of="$OUT_IMG" bs=1M count=$IMG_SIZE

# 3. Inizializza MBR
mpartition -I "$OUT_IMG"

# 4. Crea la partizione (2048 sectors offset = 1MB)
mpartition -c -b 2048 -l 129024 -T 0x0c -v "$OUT_IMG"

# 5. Formatta la partizione come FAT32
mformat -i "$OUT_IMG@@1M" -F -v "NEXS_BOOT"

# 6. Copia Kernel e Limine Config
mmd -i "$OUT_IMG@@1M" ::/limine
mcopy -i "$OUT_IMG@@1M" "$KERNEL" ::/nexs.elf
mcopy -i "$OUT_IMG@@1M" "$LIMINE_SHARE/limine-bios.sys" ::/limine/

# Crea limine.conf
cat > limine.conf.tmp << EOF
TIMEOUT=0
SERIAL=yes
VERBOSE=yes

:NEXS OS
    PROTOCOL=limine
    KERNEL_PATH=boot:///nexs.elf
EOF
mcopy -i "$OUT_IMG@@1M" limine.conf.tmp ::/limine/limine.conf
rm limine.conf.tmp

# 7. Installa Limine MBR
"$LIMINE_BIN" bios-install "$OUT_IMG"

echo "[+] Disk Image creata: $OUT_IMG"
