#!/bin/bash
# install-extension.sh — Installa l'estensione NeXs localmente in VS Code

EXT_DIR="$HOME/.antigravity/extensions/olmo.nexs-lang-0.1.0"
SRC_DIR="$(pwd)/tools/vscode-nexs"

if [ ! -d "$SRC_DIR" ]; then
    echo "Errore: Directory sorgente $SRC_DIR non trovata."
    exit 1
fi

echo "Installazione estensione NeXs in Antigravity ($EXT_DIR)..."

# Rimuovi link esistente se presente
rm -rf "$EXT_DIR"

# Assicurati che la cartella extensions esista
mkdir -p "$HOME/.antigravity/extensions"

# Crea link simbolico
ln -s "$SRC_DIR" "$EXT_DIR"

echo "Fatto! Riavvia Antigravity per attivare l'estensione."
