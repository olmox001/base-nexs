#!/bin/bash
# ================================================
# Test completo NexS - Versione senza percorsi hardcoded
# ================================================

set -euo pipefail

# Directory di lavoro
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# Nome del file di log
LOGFILE="test-cycle-$(date +%Y%m%d-%H%M%S).log"

# Redirige output su console e file
exec > >(tee -a "$LOGFILE")
exec 2>&1

echo "=== INIZIO TEST COMPLETO NEXS ==="
echo "Data: $(date)"
echo "Cartella di lavoro: $(pwd)"
echo "Log: $LOGFILE"
echo "==========================================="

# Funzione per eseguire comandi con logging chiaro
run_cmd() {
    echo ""
    echo ">>> Eseguendo: $*"
    echo "-------------------------------------------"
    "$@"
    local status=$?
    echo "-------------------------------------------"
    echo "Comando terminato con codice: $status"
    echo ""
    return $status
}

echo "=== Fase 1: Pulizia e build nativa ==="
run_cmd make clean
run_cmd make
run_cmd ./nexs
run_cmd ./nexs example/minios/boot.nx

echo "=== Fase 2: Build baremetal-amd64 + QEMU ==="
run_cmd make clean
run_cmd make baremetal-amd64
run_cmd ./scripts/qemu-amd64.sh

echo "=== Fase 3: Build ISO + QEMU ISO ==="
run_cmd make clean
run_cmd make baremetal-amd64
run_cmd ./scripts/make-iso.sh
run_cmd ./scripts/qemu-amd64.sh --iso

echo "=== Fase 4: Compilazione diretta baremetal ==="
run_cmd ./nexs --compile example/minios/boot.nx \
               --target baremetal-amd64 \
               -o build/baremetal-amd64/nexs.elf

run_cmd ./scripts/qemu-amd64.sh
run_cmd ./scripts/make-iso.sh
run_cmd ./scripts/qemu-amd64.sh --iso

echo "=== Fase 5: Compilazione e test target macOS ==="
run_cmd make clean
run_cmd ./nexs --compile example/minios/boot.nx \
               --target macos-amd64 \
               -o test.out

run_cmd ./test.out

echo ""
echo "==========================================="
echo "TEST COMPLETO TERMINATO con successo!"
echo "Log salvato in: $LOGFILE"
echo "==========================================="