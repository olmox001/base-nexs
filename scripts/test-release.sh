#!/bin/bash
# =============================================================================
# scripts/test-release.sh — Quick Tester for NEXS Release Artifacts
# =============================================================================
# Usage: ./scripts/test-release.sh [1-10 | --full]
#  1: MacOS Interpreter  2: MacOS MINIOS (AOT)
#  3: Baremetal ELF      4: Baremetal ISO
#  5: MiniOS ELF         6: MiniOS ISO
#  7: Linux Interpreter  8: Linux MINIOS (AOT)
#  9: seL4 aarch64 MiniOS (QEMU)
#  10: WASM / Node.js (pipe eval + version)
#  --full: Apre un terminale dedicato per ogni test compatibile col tuo OS
# =============================================================================

set -e

VERSION="v9.9.9"
REL_DIR="RELEASE/$VERSION"

# Salviamo i percorsi assoluti per poterli passare ai nuovi terminali
SCRIPT_PATH="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"
WORK_DIR="$PWD"

if [ ! -d "$REL_DIR" ]; then
    echo "Error: Release directory $REL_DIR not found."
    echo "Run './scripts/make-release.sh ' first."
    exit 1
fi

CHOICE="${1:-}"

# =============================================================================
# GESTIONE FLAG --full
# =============================================================================
if [ "$CHOICE" = "--full" ]; then
    echo "Avvio della modalità --full: apertura di un terminale per ogni test..."

    # Creazione di uno script temporaneo per gestire i prompt di [Invio] nei nuovi terminali
    WRAPPER_SCRIPT="/tmp/nexs_test_wrapper.sh"
    cat << 'EOF' > "$WRAPPER_SCRIPT"
#!/bin/bash
TEST_NUM="$1"
SCRIPT_PATH="$2"
WORK_DIR="$3"

# Spostiamoci nella directory di lavoro originale
cd "$WORK_DIR" || exit 1

echo "=========================================================="
echo " NEXS Test Artifact - Modalità Interattiva (Test #$TEST_NUM)"
echo "=========================================================="
read -p "Premi [INVIO] per eseguire il test $TEST_NUM..."

# Esegue il test originale
"$SCRIPT_PATH" "$TEST_NUM"
EXIT_CODE=$?

echo "=========================================================="
if [ $EXIT_CODE -eq 0 ]; then
    echo " Test completato con successo (Exit code: 0)."
else
    echo " Test terminato con un errore (Exit code: $EXIT_CODE)."
fi
echo "=========================================================="
read -p "Premi [INVIO] per chiudere questa finestra..."
EOF
    chmod +x "$WRAPPER_SCRIPT"

    for i in {1..10}; do
        # Ottimizzazione: salta i test incompatibili col sistema host attuale
        if [ "$(uname)" = "Darwin" ] && [[ "$i" == "7" || "$i" == "8" ]]; then
            echo "Salto il test $i (Linux) perché siamo su macOS."
            continue
        fi
        if [ "$(uname)" = "Linux" ] && [[ "$i" == "1" || "$i" == "2" ]]; then
            echo "Salto il test $i (macOS) perché siamo su Linux."
            continue
        fi
        if [ "$i" = "10" ] && ! command -v node >/dev/null 2>&1; then
            echo "Salto il test 10 (WASM) perché node non è disponibile."
            continue
        fi
        if [ "$i" = "10" ] && [ ! -f "$REL_DIR/wasm/nexs.js" ]; then
            echo "Salto il test 10 (WASM) perché $REL_DIR/wasm/nexs.js non esiste."
            continue
        fi

        echo "Apertura terminale per il Test $i..."
        
        if [ "$(uname)" = "Darwin" ]; then
            # macOS: Usa AppleScript per aprire una nuova finestra su Terminal.app
            osascript -e "tell app \"Terminal\" to do script \"'$WRAPPER_SCRIPT' '$i' '$SCRIPT_PATH' '$WORK_DIR'\""
        elif [ "$(uname)" = "Linux" ]; then
            # Linux: Cerca l'emulatore di terminale disponibile
            if command -v gnome-terminal &> /dev/null; then
                gnome-terminal -- "$WRAPPER_SCRIPT" "$i" "$SCRIPT_PATH" "$WORK_DIR"
            elif command -v konsole &> /dev/null; then
                konsole -e "$WRAPPER_SCRIPT" "$i" "$SCRIPT_PATH" "$WORK_DIR" &
            elif command -v xfce4-terminal &> /dev/null; then
                xfce4-terminal -x "$WRAPPER_SCRIPT" "$i" "$SCRIPT_PATH" "$WORK_DIR" &
            elif command -v xterm &> /dev/null; then
                xterm -e "$WRAPPER_SCRIPT" "$i" "$SCRIPT_PATH" "$WORK_DIR" &
            else
                echo "Errore: Nessun emulatore di terminale supportato trovato (gnome-terminal, konsole, xfce4-terminal, xterm)."
            fi
        fi
        
        # Un piccolo delay per evitare di accavallare l'apertura delle finestre
        sleep 0.5 
    done

    echo "Tutti i terminali sono stati aperti con successo!"
    exit 0
fi

# =============================================================================
# LOGICA DI ESECUZIONE SINGOLA (1-8)
# =============================================================================

case "$CHOICE" in
    1)
        echo "Testing Nexs-amd64-MacOS (Interpreter)..."
        "./$REL_DIR/Nexs-amd64-MacOS"
        ;;
    2)
        echo "Testing MINIOS-amd64-MacOS (AOT)..."
        "./$REL_DIR/MINIOS-amd64-MacOS"
        ;;
    3)
        echo "Testing Nexs-amd64-STANDALONE.elf (QEMU -kernel)..."
        qemu-system-x86_64 -kernel "$REL_DIR/Nexs-amd64-STANDALONE.elf" -m 512M -nographic -no-reboot
        ;;
    4)
        echo "Testing Nexs-amd64-STANDALONE.iso (QEMU -cdrom)..."
        qemu-system-x86_64 -cdrom "$REL_DIR/Nexs-amd64-STANDALONE.iso" -m 512M -nographic -no-reboot
        ;;
    5)
        echo "Testing MINIOS-amd64-STANDALONE.elf (QEMU -kernel)..."
        qemu-system-x86_64 -kernel "$REL_DIR/MINIOS-amd64-STANDALONE.elf" -m 512M -nographic -no-reboot
        ;;
    6)
        echo "Testing MINIOS-amd64-STANDALONE.iso (QEMU -cdrom)..."
        qemu-system-x86_64 -cdrom "$REL_DIR/MINIOS-amd64-STANDALONE.iso" -m 512M -nographic -no-reboot
        ;;
    7)
        echo "Testing Nexs-amd64-Linux (Interpreter)..."
        if [ "$(uname)" = "Linux" ]; then
            "./$REL_DIR/Nexs-amd64-Linux"
        else
            echo "Error: Cannot run Linux binary on $(uname). Use a Linux environment or Docker."
            exit 1
        fi
        ;;
    8)
        echo "Testing MINIOS-amd64-Linux (AOT)..."
        if [ "$(uname)" = "Linux" ]; then
            "./$REL_DIR/MINIOS-amd64-Linux"
        else
            echo "Error: Cannot run Linux binary on $(uname). Use a Linux environment or Docker."
            exit 1
        fi
        ;;
    9|sel4-aarch64)
        if [ -f "$REL_DIR/NEXS-seL4-aarch64-MiniOS.elf" ]; then
            echo "Launching seL4/NEXS aarch64 MiniOS in QEMU..."
            qemu-system-aarch64 \
                -machine virt,virtualization=on \
                -cpu cortex-a53 -nographic \
                -serial mon:stdio \
                -device loader,file="$REL_DIR/NEXS-seL4-aarch64-MiniOS.elf",addr=0x70000000,cpu-num=0 \
                -m size=1G
        else
            echo "seL4 aarch64 artifact not found. Run make-release.sh with MICROKIT_SDK set."
        fi
        ;;
    10|wasm)
        WASM_JS="$REL_DIR/wasm/nexs.js"
        if ! command -v node >/dev/null 2>&1; then
            echo "Error: node not found. Install Node.js to test the WASM build."
            exit 1
        fi
        if [ ! -f "$WASM_JS" ]; then
            echo "Error: $WASM_JS not found. Run make-release.sh with emcc available."
            exit 1
        fi
        echo "Testing NEXS-wasm (Node.js) — version..."
        node -e "
const M = require('$(cd "$(dirname "$WASM_JS")" && pwd)/$(basename "$WASM_JS")');
M().then(function(mod) {
    var ver = mod.ccall('nexs_wasm_version', 'string', [], []);
    console.log('NEXS WASM version: ' + ver);
    process.exit(0);
});
" 2>/dev/null || node "$WASM_JS" --version 2>/dev/null || true
        echo ""
        echo "Testing NEXS-wasm (Node.js) — eval 'out 1+2'..."
        echo 'out 1+2' | node "$WASM_JS" 2>/dev/null || true
        echo ""
        echo "Testing NEXS-wasm (Node.js) — interactive REPL (type :exit to quit)..."
        node "$WASM_JS"
        ;;
    *)
        echo "Usage: $0 [1-10 | --full]"
        echo "  1:  Nexs-amd64-MacOS (Host Interpreter)"
        echo "  2:  MINIOS-amd64-MacOS (Host AOT)"
        echo "  3:  Nexs-amd64-STANDALONE.elf (Baremetal ELF)"
        echo "  4:  Nexs-amd64-STANDALONE.iso (Baremetal ISO)"
        echo "  5:  MINIOS-amd64-STANDALONE.elf (MiniOS ELF)"
        echo "  6:  MINIOS-amd64-STANDALONE.iso (MiniOS ISO)"
        echo "  7:  Nexs-amd64-Linux (Linux Host Interpreter)"
        echo "  8:  MINIOS-amd64-Linux (Linux AOT)"
        echo "  9:  NEXS-seL4-aarch64-MiniOS (seL4 QEMU)"
        echo "  10: NEXS-wasm (Node.js REPL + pipe eval)"
        echo "  --full: Esegue tutti i test compatibili aprendo terminali separati"
        exit 1
        ;;
esac