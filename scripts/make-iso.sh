#!/bin/bash
# =============================================================================
# scripts/make-iso.sh — NEXS ISO Packager (Limine Protocol)
# =============================================================================
set -e

KERNEL="build/baremetal-amd64/nexs.elf"
OUT_ISO="build/nexs-amd64.iso"
ISO_ROOT="build/iso_stage"

if [ ! -f "$KERNEL" ]; then
    echo "Errore: Kernel non trovato in $KERNEL"
    exit 1
fi

BREW_PREFIX=$(brew --prefix)
LIMINE_SHARE="$BREW_PREFIX/share/limine"
LIMINE_BIN="$BREW_PREFIX/bin/limine"

# Pulizia
rm -rf "$ISO_ROOT"
mkdir -p "$ISO_ROOT/limine"

# Copia file
cp "$KERNEL" "$ISO_ROOT/nexs.elf"
cp "$LIMINE_SHARE/limine-bios.sys"    "$ISO_ROOT/"          # root
cp "$LIMINE_SHARE/limine-bios.sys"    "$ISO_ROOT/limine/"
cp "$LIMINE_SHARE/limine-bios-cd.bin" "$ISO_ROOT/limine/"

# limine.conf
cat > "$ISO_ROOT/limine/limine.conf" << EOF
timeout: 0
serial: yes

/NEXS OS
    protocol: multiboot2
    path: boot():/nexs.elf
EOF

echo "[*] Creazione ISO con xorriso..."

xorriso -as mkisofs \
    -R -J -V "NEXS_BOOT" \
    -b limine/limine-bios-cd.bin \
    -no-emul-boot \
    -boot-load-size 4 \
    -boot-info-table \
    -o "$OUT_ISO" \
    "$ISO_ROOT"

echo "[*] Installazione Limine BIOS..."
"$LIMINE_BIN" bios-install "$OUT_ISO"

echo "[*] Contenuto ISO:"
xorriso -indev "$OUT_ISO" -ls / 2>/dev/null || true
echo "[+] ISO creata con successo: $OUT_ISO ($(du -sh "$OUT_ISO" | cut -f1))"