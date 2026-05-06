#!/bin/bash
# =============================================================================
# scripts/make-release.sh — NEXS Release Packager
# =============================================================================
# Generates hosted and baremetal artifacts for a new release.
# =============================================================================

set -euo pipefail

VERSION="${1:-v9.9.9}"
RELEASE_DIR="RELEASE/$VERSION"
mkdir -p "$RELEASE_DIR"

echo "=== NEXS RELEASE PACKAGER ($VERSION) ==="
echo "Artifacts will be saved in: $RELEASE_DIR"
echo "-------------------------------------------"

# Force 64MB pool for all AOT builds
export NEXS_POOL_PROFILE=POOL_64MB

# 1. Build Native Interpreter (Host)
UNAME_S=$(uname -s)
if [ "$UNAME_S" = "Darwin" ]; then
    HOST_NAME="MacOS"
    CROSS_TARGET="linux-amd64"
    CROSS_NAME="Linux"
else
    HOST_NAME="Linux"
    CROSS_TARGET="macos-amd64"
    CROSS_NAME="MacOS"
fi

echo "[*] Building Nexs-amd64-$HOST_NAME (Native Interpreter)..."
make clean > /dev/null
make POOL_PROFILE=64MB > /dev/null
cp nexs "$RELEASE_DIR/Nexs-amd64-$HOST_NAME"

# 2. Build Native AOT (MINIOS)
echo "[*] Building MINIOS-amd64-$HOST_NAME (AOT)..."
./nexs --compile example/minios/boot.nx \
       -o "$RELEASE_DIR/MINIOS-amd64-$HOST_NAME"

# 3. Build Baremetal Interpreter (STANDALONE)
echo "[*] Building Nexs-amd64-STANDALONE (Interpreter)..."
make clean > /dev/null
make POOL_PROFILE=64MB > /dev/null  # Need native compiler
./nexs --compile /dev/null --target baremetal-amd64 -o build/baremetal-amd64/nexs.elf
cp build/baremetal-amd64/nexs.elf "$RELEASE_DIR/Nexs-amd64-STANDALONE.elf"

echo "[*] Generating Nexs-amd64-STANDALONE.iso..."
./scripts/make-iso.sh > /dev/null
cp build/nexs-amd64.iso "$RELEASE_DIR/Nexs-amd64-STANDALONE.iso"

# 4. Build Baremetal AOT (STANDALONE-MINIOS)
echo "[*] Building MINIOS-amd64-STANDALONE.elf (MiniOS AOT)..."
# We need the native compiler first
make clean > /dev/null
make > /dev/null

./nexs --compile example/minios/boot.nx \
       --target baremetal-amd64 \
       -o build/baremetal-amd64/nexs.elf

cp build/baremetal-amd64/nexs.elf "$RELEASE_DIR/MINIOS-amd64-STANDALONE.elf"

echo "[*] Generating MINIOS-amd64-STANDALONE.iso..."
./scripts/make-iso.sh > /dev/null
cp build/nexs-amd64.iso "$RELEASE_DIR/MINIOS-amd64-STANDALONE.iso"

# 5. Build Cross Interpreter
echo "[*] Building Nexs-amd64-$CROSS_NAME (Cross-compiled Interpreter)..."
make clean > /dev/null
make "$CROSS_TARGET" POOL_PROFILE=64MB > /dev/null
if [ -f "build/$CROSS_TARGET/nexs" ]; then
    cp "build/$CROSS_TARGET/nexs" "$RELEASE_DIR/Nexs-amd64-$CROSS_NAME"
else
    echo "Warning: $CROSS_TARGET build failed, skipping Nexs-amd64-$CROSS_NAME"
fi

# 6. Build Cross AOT
echo "[*] Building MINIOS-amd64-$CROSS_NAME (Cross-compiled AOT)..."
# We need native nexs to run the compiler
make clean > /dev/null
make > /dev/null
./nexs --compile example/minios/boot.nx \
       --target "$CROSS_TARGET" \
       -o "$RELEASE_DIR/MINIOS-amd64-$CROSS_NAME" || echo "Warning: Cross AOT build failed, skipping"

echo "-------------------------------------------"
echo "[+] RELEASE COMPLETE: $VERSION"
ls -lh "$RELEASE_DIR"
echo "-------------------------------------------"
echo "Test manuali consigliati:"
echo "1. ./$RELEASE_DIR/Nexs-amd64-MacOS"
echo "2. qemu-system-x86_64 -kernel $RELEASE_DIR/Nexs-amd64-STANDALONE.elf -nographic"
echo "3. qemu-system-x86_64 -cdrom $RELEASE_DIR/MINIOS-amd64-STANDALONE.iso -nographic"
