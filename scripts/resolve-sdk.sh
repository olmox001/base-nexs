#!/bin/sh
# Resolve a single NX-SDK source without creating recursive project references.
# Embedded Base-NEXS reuses NexsOS1/system; standalone Base-NEXS uses its
# pinned external/os1/nx-sdk Git submodule (non-recursive checkout).
set -eu
BASE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
SUPER=$(git -C "$BASE" rev-parse --show-superproject-working-tree 2>/dev/null || :)
MODE=standalone
SDK="$BASE/external/os1/nx-sdk"

if [ -n "$SUPER" ] && [ -f "$SUPER/.gitmodules" ]; then
  PARENT_LINK=$(git -C "$SUPER" config -f "$SUPER/.gitmodules"     --get submodule.user/bin/base-nexs.path 2>/dev/null || :)
  SDK_LINK=$(git -C "$SUPER" config -f "$SUPER/.gitmodules"     --get submodule.system.path 2>/dev/null || :)
  if [ "$PARENT_LINK" = user/BINEXTERNAL/base-nexs ] &&
     [ "$SDK_LINK" = system ]; then
    SDK="$SUPER/system"
    MODE=umbrella
  fi
fi

case "${1:---path}" in
  --path|--mode|--check) ;;
  --init)
    if [ "$MODE" = standalone ]; then
      # The optional gitlink is inactive in .gitmodules so recursive umbrella
      # update never pulls a duplicate SDK. Explicit standalone setup overrides
      # that policy for exactly this submodule and no other repository.
      git -C "$BASE" \
        -c submodule.external/os1/nx-sdk.active=true \
        -c submodule.external/os1/nx-sdk.update=checkout \
        submodule update --init --checkout external/os1/nx-sdk
    else
      echo "Integrated SDK selected; optional nested SDK checkout skipped."
    fi
    ;;
  *)
    echo "Usage: scripts/resolve-sdk.sh [--path|--mode|--check|--init]" >&2
    exit 2 ;;
esac

if [ ! -f "$SDK/include/api/os1.h" ] ||
   [ ! -f "$SDK/include/contract/object.h" ] ||
   [ ! -f "$SDK/Makefile" ]; then
  echo "NX-SDK unavailable at $SDK ($MODE)." >&2
  echo "For standalone checkouts: scripts/resolve-sdk.sh --init" >&2
  exit 2
fi

case "${1:---path}" in
  --path) (CDPATH= cd "$SDK" && pwd -P) ;;
  --mode) printf '%s\n' "$MODE" ;;
  --init)
    printf 'NX-SDK ready: %s (%s)\n' "$SDK" "$MODE"
    ;;
  --check)
    BRANCH=$(git -C "$SDK" symbolic-ref --short HEAD 2>/dev/null || :)
    if [ "$MODE" = umbrella ] && [ "$BRANCH" != SPERIMENTAL-LINUX ]; then
      echo "Integrated NX-SDK branch is $BRANCH, expected SPERIMENTAL-LINUX." >&2
      exit 3
    fi
    printf 'NX-SDK: %s (%s) revision %s\n' "$SDK" "$MODE"       "$(git -C "$SDK" rev-parse --short HEAD)"
    ;;
  *)
    echo "Usage: scripts/resolve-sdk.sh [--path|--mode|--check|--init]" >&2
    exit 2
    ;;
esac
