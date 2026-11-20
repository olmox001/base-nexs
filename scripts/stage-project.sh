#!/bin/sh
# Stage the reviewed Base-NEXS tree, preserving its intentionally uninitialized
# optional SDK gitlink. Generic 'git add -A' removes a missing submodule path
# from the index and would silently make .gitmodules inconsistent.
set -eu
BASE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
SDK_PATH=external/os1/nx-sdk
EXPECTED_MODE=160000

PIN=$(git -C "$BASE" ls-files --stage "$SDK_PATH")
case "$PIN" in
  "$EXPECTED_MODE "*)
    ;;
  *)
    echo "Missing optional SDK gitlink: refuse staging without explicit pin." >&2
    exit 1
    ;;
esac

git -C "$BASE" add -A -- . ':(exclude)external/os1/nx-sdk'
UPDATED=$(git -C "$BASE" ls-files --stage "$SDK_PATH")
if [ "$UPDATED" != "$PIN" ]; then
  echo "SDK gitlink changed unexpectedly; refusing to continue." >&2
  exit 1
fi
git -C "$BASE" diff --name-only
printf 'Optional NX-SDK gitlink preserved in index.\n'
