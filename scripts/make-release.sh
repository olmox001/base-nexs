#!/bin/bash
# =============================================================================
# scripts/make-release.sh — NEXS Release Packager
# =============================================================================
# Generates hosted and baremetal artifacts for a new release.
# =============================================================================

set -euo pipefail

VERSION="${1:-v0.2.0}"
RELEASE_DIR="RELEASE/$VERSION"
mkdir -p "$RELEASE_DIR"

echo "=== NEXS RELEASE PACKAGER ($VERSION) ==="
echo "Artifacts will be saved in: $RELEASE_DIR"
echo "-------------------------------------------"

# 1. Build Hosted Interpreter (MacOS)
echo "[*] Building Nexs-amd64-MacOS (Interpreter)..."
make clean > /dev/null
make POOL_PROFILE=16MB > /dev/null
cp nexs "$RELEASE_DIR/Nexs-amd64-MacOS"

# 2. Build Hosted AOT (MacOS-MINIOS)
echo "[*] Building Nexs-amd64-MacOS-MINIOS (AOT)..."
./nexs --compile example/minios_dev_readreadme/boot.nx \
       --target macos-amd64 \
       -o "$RELEASE_DIR/Nexs-amd64-MacOS-MINIOS"

# 3. Build Baremetal Interpreter (STANDALONE)
echo "[*] Building Nexs-amd64-STANDALONE (Interpreter)..."
make clean > /dev/null
make baremetal-amd64 POOL_PROFILE=16MB > /dev/null
cp build/baremetal-amd64/nexs.elf "$RELEASE_DIR/Nexs-amd64-STANDALONE.elf"

echo "[*] Generating Nexs-amd64-STANDALONE.iso..."
./scripts/make-iso.sh > /dev/null
cp build/nexs-amd64.iso "$RELEASE_DIR/Nexs-amd64-STANDALONE.iso"

# 4. Build Baremetal AOT (STANDALONE-MINIOS)
echo "[*] Building Nexs-amd64-STANDALONE-MINIOS (AOT)..."
# We need the hosted compiler first
make clean > /dev/null
make > /dev/null

./nexs --compile example/minios_dev_readreadme/boot.nx \
       --target baremetal-amd64 \
       -o build/baremetal-amd64/nexs.elf

cp build/baremetal-amd64/nexs.elf "$RELEASE_DIR/Nexs-amd64-STANDALONE-MINIOS.elf"

echo "[*] Generating Nexs-amd64-STANDALONE-MINIOS.iso..."
./scripts/make-iso.sh > /dev/null
cp build/nexs-amd64.iso "$RELEASE_DIR/Nexs-amd64-STANDALONE-MINIOS.iso"

echo "-------------------------------------------"
echo "[+] RELEASE COMPLETE: $VERSION"
ls -lh "$RELEASE_DIR"
echo "-------------------------------------------"
echo "Test manuali consigliati:"
echo "1. ./$RELEASE_DIR/Nexs-amd64-MacOS"
echo "2. qemu-system-x86_64 -kernel $RELEASE_DIR/Nexs-amd64-STANDALONE.elf -nographic"
echo "3. qemu-system-x86_64 -cdrom $RELEASE_DIR/Nexs-amd64-STANDALONE-MINIOS.iso -nographic"
