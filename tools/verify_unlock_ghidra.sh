#!/usr/bin/env bash
# verify_unlock_ghidra.sh - disassembly round-trip proof for the KC02 USB-unlock
# RAM frame stub.
#
# OFFLINE ONLY. Imports a raw binary containing ONLY the 28 stub bytes into a
# THROWAWAY Ghidra project/cache, disassembles it with the project's
# KC02:LE:32:nodelay language, then checks every instruction byte-for-byte
# against the hand-assembled bytes in tools/kc02_unlock.py.
#
# It never reads or writes the firmware image, the main analysis cache
# (/home/jbaiter/pi-ghidra-cache-kc02-or1k), and never talks to a USB device.
#
# Usage:
#   tools/verify_unlock_ghidra.sh [cache-dir]
#
# Default cache dir: /home/jbaiter/pi-ghidra-cache-kc02-unlocktest
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS="$REPO/tools"
CACHE="${1:-${UNLOCK_CACHE_DIR:-/home/jbaiter/pi-ghidra-cache-kc02-unlocktest}}"
GHIDRA_HOME="${GHIDRA_HOME:-/home/jbaiter/.ghidra/ghidra_12.1.4_PUBLIC}"
LAUNCHER="$GHIDRA_HOME/support/analyzeHeadless"
LANG="KC02:LE:32:nodelay"

# --- hard safety guard: never operate on the real analysis cache ------------
case "$CACHE" in
  */pi-ghidra-cache-kc02-or1k|*/pi-ghidra-cache|*/pi-ghidra-cache/)
    echo "refusing to use the main analysis cache: $CACHE" >&2; exit 2 ;;
esac
[[ "$CACHE" == *unlocktest* ]] || {
  echo "refusing: throwaway cache dir must contain 'unlocktest' (got: $CACHE)" >&2
  exit 2
}
[[ -x "$LAUNCHER" ]] || { echo "analyzeHeadless not found at $LAUNCHER" >&2; exit 3; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

eval "$(python3 - "$TOOLS" <<'PY'
import sys
sys.path.insert(0, sys.argv[1])
import kc02_unlock as u
print(f"STUB_ADDR=0x{u.FRAME_STUB_ADDR:08x}")
print(f"STUB_LEN={u.FRAME_STUB_LEN}")
PY
)"

python3 "$TOOLS/kc02_unlock.py" selftest
rm -rf "$CACHE"
python3 "$TOOLS/kc02_unlock.py" blob --kind stub --out "$WORK/stub.bin"

count=$(( $(stat -c%s "$WORK/stub.bin") / 4 ))
sha="$(sha256sum "$WORK/stub.bin" | cut -d' ' -f1)"
art="$CACHE/artifacts/$sha"
mkdir -p "$art/input" "$art/project"
cp "$WORK/stub.bin" "$art/input/${sha:0:16}.bin"

echo
echo "== unlock frame stub: load 0x${STUB_ADDR#0x}, ${count} instruction(s), sha256 $sha"
set +e
"$LAUNCHER" "$art/project" pi-ghidra \
  -import "$art/input/${sha:0:16}.bin" \
  -processor "$LANG" \
  -loader-baseAddr "$STUB_ADDR" \
  -noanalysis \
  -scriptPath "$TOOLS" \
  -postScript DumpShim.java "$STUB_ADDR" "$count" "$TOOLS/unlock_stub_roundtrip.txt" \
  -max-cpu 1 >"$WORK/ghidra.log" 2>&1
rc=$?
set -e
grep -E "ERROR|Exception|DumpShim\.java>" "$WORK/ghidra.log" || true
if [[ $rc -ne 0 ]]; then
  echo "analyzeHeadless failed (rc=$rc); see log" >&2
  tail -20 "$WORK/ghidra.log" >&2
  exit 4
fi

cat > "$art/manifest.json" <<JSON
{
  "schema": 1,
  "hash": "$sha",
  "source": "$WORK/stub.bin",
  "program": "${sha:0:16}.bin",
  "ghidraVersion": "12.1.4",
  "note": "THROWAWAY project holding only the KC02 unlock frame stub; NOT the firmware."
}
JSON

echo
python3 "$TOOLS/kc02_unlock.py" verify-roundtrip "$TOOLS/unlock_stub_roundtrip.txt"
echo
echo "disassembly dump: $TOOLS/unlock_stub_roundtrip.txt"
