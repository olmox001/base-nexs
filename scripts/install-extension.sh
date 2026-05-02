#!/bin/bash
# install-extension.sh — Installa l'estensione NeXs localmente in VS Code

# Directory sorgente: usa il primo argomento, o il default di sviluppo, o il percorso di release
SRC_DIR="${1:-$(pwd)/tools/vscode-nexs}"
if [ ! -d "$SRC_DIR" ] && [ -d "extensions/nexs-lang" ]; then
    SRC_DIR="$(pwd)/extensions/nexs-lang"
fi

EXT_DIR_ANTI="$HOME/.antigravity/extensions/nexs.nexs-lang-0.1.0"
EXT_DIR_VSCODE="$HOME/.vscode/extensions/nexs.nexs-lang-0.1.0"

if [ ! -d "$SRC_DIR" ]; then
    echo "Errore: Directory sorgente $SRC_DIR non trovata."
    exit 1
echo "Installazione estensione NeXs da $SRC_DIR..."

# 0. Pulizia preventiva di tutte le versioni precedenti
echo "[*] Rimozione versioni precedenti dell'estensione..."
rm -rf "$HOME/.antigravity/extensions/nexs.nexs-lang-"*
rm -rf "$HOME/.vscode/extensions/nexs.nexs-lang-"*

# 1. Antigravity
mkdir -p "$HOME/.antigravity/extensions"
rm -rf "$EXT_DIR_ANTI"
if [ -f "autoinstall.sh" ]; then
    cp -r "$SRC_DIR" "$EXT_DIR_ANTI"
    echo "[+] Copiata in Antigravity (Release mode)"
else
    ln -s "$SRC_DIR" "$EXT_DIR_ANTI"
    echo "[+] Linkata in Antigravity (Dev mode)"
fi

# 2. VS Code (opzionale)
if [ -d "$HOME/.vscode/extensions" ]; then
    rm -rf "$EXT_DIR_VSCODE"
    if [ -f "autoinstall.sh" ]; then
        cp -r "$SRC_DIR" "$EXT_DIR_VSCODE"
        echo "[+] Copiata in VS Code"
    else
        ln -s "$SRC_DIR" "$EXT_DIR_VSCODE"
        echo "[+] Linkata in VS Code"
    fi
fi

echo "Fatto! Riavvia l'editor per attivare l'estensione."
