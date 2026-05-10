#!/bin/bash
# scripts/compile-and-test-logos.sh
# Compila i loghi e avvia minios per testarli

set -e

BASE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
COMPILE_SCRIPT="$BASE_DIR/../compile_to_nxtimg.py"
MINIOS_DIR="$BASE_DIR/example/minios"
LOGO_SRC_DIR="$MINIOS_DIR/logo"
LOGO_OUT_DIR="$MINIOS_DIR/logos"

echo "═══════════════════════════════════════════════════════════════"
echo "Logo Compilation & Test Suite"
echo "═══════════════════════════════════════════════════════════════"
echo

# Step 1: Check Python
if ! command -v python3 &>/dev/null; then
    echo "[ERR] python3 non trovato. Installa python3 per compilare i loghi."
    exit 1
fi

# Step 2: Create output directory
mkdir -p "$LOGO_OUT_DIR"
echo "[1/4] Directory creata: $LOGO_OUT_DIR"

# Step 3: Compile PNG → NXTIMG
echo "[2/4] Compilazione loghi..."
count=0
if [ -d "$LOGO_SRC_DIR" ]; then
    for png_file in "$LOGO_SRC_DIR"/*.png; do
        if [ -f "$png_file" ]; then
            name=$(basename "$png_file" .png)
            nxtimg_file="$LOGO_OUT_DIR/$name.nxtimg"
            echo "  → $name.png"
            python3 "$COMPILE_SCRIPT" "$png_file" "$nxtimg_file" --width 80 --height 24
            count=$((count + 1))
        fi
    done
fi

if [ $count -eq 0 ]; then
    echo "  [WARN] Nessun file PNG trovato in $LOGO_SRC_DIR"
else
    echo "  ✓ Compilati $count loghi"
fi

# Step 4: Verify NXTIMG files
echo "[3/4] Verifica file .nxtimg..."
nxtimg_count=$(find "$LOGO_OUT_DIR" -name "*.nxtimg" -type f | wc -l)
if [ $nxtimg_count -gt 0 ]; then
    echo "  ✓ Trovati $nxtimg_count file .nxtimg"
    ls -lh "$LOGO_OUT_DIR"/*.nxtimg
else
    echo "  [WARN] Nessun file .nxtimg trovato"
fi

# Step 5: Ready for boot
echo "[4/4] Sistema pronto!"
echo
echo "═══════════════════════════════════════════════════════════════"
echo "Prossimi step:"
echo "  1. Compila NEXS:  make"
echo "  2. Avvia minios:  ./nexs example/minios/boot.nx"
echo "  3. Dalla shell:   termimg sd02/logos/logo.nxtimg"
echo "═══════════════════════════════════════════════════════════════"
