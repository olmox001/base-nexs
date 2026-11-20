#!/bin/sh
# Legacy compatibility command for standalone documentation only.
# The former recursive NexsOS1/reference Git submodule has been removed.
# Use NX-SDK as an explicit external dependency, not a copy of the umbrella.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
case "${1:---check}" in
  --check)
    if git -C "$ROOT" ls-files --stage reference | grep -q '^160000 '; then
      echo "ERROR: recursive NexsOS1 reference still tracked" >&2
      exit 1
    fi
    if [ -e "$ROOT/reference/.git" ]; then
      echo "ERROR: legacy recursive reference checkout still present" >&2
      exit 1
    fi
    echo "No recursive NexsOS1 reference submodule; use external/os1/nx-sdk."
    ;;
  *)
    echo "Recursive reference setup has been retired; run resolve-sdk.sh." >&2
    exit 2
    ;;
esac
