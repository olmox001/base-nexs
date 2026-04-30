#!/bin/bash
# =============================================================================
# scripts/make-iso.sh — NEXS ISO Generator (Strict Build-Path Edition)
# =============================================================================
set -e

# Percorsi assoluti basati sulla tua struttura
KERNEL="build/baremetal-amd64/nexs.elf"
OUT_ISO="build/nexs-amd64.iso"
ISO_STAGE="build/iso_stage"

echo "--- Generazione ISO NEXS (Target: build/) ---"

# 1. Verifica Kernel
if [ ! -f "$KERNEL" ]; then
    echo "Errore: $KERNEL non trovato."
    exit 1
fi

# 2. Localizzazione Limine (Homebrew)
BREW_PREFIX=$(brew --prefix)
LIMINE_SHARE="$BREW_PREFIX/share/limine"
LIMINE_BIN="$BREW_PREFIX/bin/limine"

# 3. Preparazione staging esclusivamente dentro build/
rm -rf "$ISO_STAGE"
mkdir -p "$ISO_STAGE"

# 4. Copia dei file necessari per il boot
cp "$KERNEL" "$ISO_STAGE/nexs.elf"
cp "$LIMINE_SHARE/limine-bios.sys" "$ISO_STAGE/"
cp "$LIMINE_SHARE/limine-bios-cd.bin" "$ISO_STAGE/"
cp "$LIMINE_SHARE/limine-uefi-cd.bin" "$ISO_STAGE/"

# 5. Generazione automatica limine.conf (Root della ISO)
# Configurato per seriale 38400 baud per il tuo driver UART
cat > "$ISO_STAGE/limine.conf" << EOF
TIMEOUT=0
SERIAL=yes
INTERFACE_SETTING=38400,8,n,1

:NEXS OS
    PROTOCOL=multiboot2
    KERNEL_PATH=boot:///nexs.elf
EOF

echo "[*] Creazione ISO: $OUT_ISO"

# 6. xorriso: punta tutto al contenuto di iso_stage
xorriso -as mkisofs -b limine-bios-cd.bin \
        -no-emul-boot -boot-load-size 4 -boot-info-table \
        --efi-boot limine-uefi-cd.bin \
        -efi-boot-part --efi-boot-image --protective-msdos-label \
        "$ISO_STAGE" -o "$OUT_ISO" 2>/dev/null

# 7. Installazione MBR per SeaBIOS
"$LIMINE_BIN" bios-install "$OUT_ISO" 2>/dev/null

# Pulizia temporanea
rm -rf "$ISO_STAGE"

echo "----------------------------------------------------"
echo "ISO pronta in: $OUT_ISO"
echo "----------------------------------------------------"