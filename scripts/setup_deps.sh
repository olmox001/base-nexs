#!/bin/bash
# setup_deps.sh — Scarica le dipendenze esterne per NEXS OS

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BASE_DIR="$( cd "$SCRIPT_DIR/.." && pwd )"
EXT_DIR="$BASE_DIR/external"

echo "[*] Preparazione cartella external in $EXT_DIR..."
mkdir -p "$EXT_DIR"

if [ ! -d "$EXT_DIR/termimg" ]; then
    echo "[*] Clonazione termimg..."
    git clone https://github.com/olmox001/termimg.git "$EXT_DIR/termimg"
else
    echo "[OK] termimg già presente."
fi

echo "[OK] Setup dipendenze completato."
