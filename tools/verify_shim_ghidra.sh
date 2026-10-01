#!/usr/bin/env bash
# verify_shim_ghidra.sh - disassembly round-trip proof for the KC02 RAM shim.
#
# OFFLINE ONLY. Imports a raw binary containing ONLY the 48 shim bytes (and,
# separately, ONLY the 4-byte canary) into a THROWAWAY Ghidra project/cache,
# disassembles it with the project's KC02:LE:32:nodelay language, then checks
# every instruction byte-for-byte against the hand-assembled bytes in
# tools/kc02_shim.py.
#
# It never reads or writes the firmware image, the main analysis cache
# (/home/jbaiter/pi-ghidra-cache-kc02-or1k) or the user's RAM-staging notes,
# and it never talks to a USB device.
#
# Usage:
#   tools/verify_shim_ghidra.sh [cache-dir]
#
# Default cache dir: /home/jbaiter/pi-ghidra-cache-kc02-shimtest
# The script also seeds that throwaway cache with the manifest the interactive
# `ghidra` tool expects, so it can re-read the same project with
# action=disassemble / cacheDir=<cache-dir>.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS="$REPO/tools"
CACHE="${1:-${SHIM_CACHE_DIR:-/home/jbaiter/pi-ghidra-cache-kc02-shimtest}}"
GHIDRA_HOME="${GHIDRA_HOME:-/home/jbaiter/.ghidra/ghidra_12.1.4_PUBLIC}"
LAUNCHER="$GHIDRA_HOME/support/analyzeHeadless"
LANG="KC02:LE:32:nodelay"

# --- hard safety guard: never operate on the real analysis cache ------------
case "$CACHE" in
  */pi-ghidra-cache-kc02-or1k|*/pi-ghidra-cache|*/pi-ghidra-cache/)
    echo "refusing to use the main analysis cache: $CACHE" >&2
    exit 2
    ;;
esac
[[ "$CACHE" == *shimtest* ]] || {
  echo "refusing: throwaway cache dir must contain 'shimtest' (got: $CACHE)" >&2
  exit 2
}

[[ -x "$LAUNCHER" ]] || { echo "analyzeHeadless not found at $LAUNCHER" >&2; exit 3; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Load addresses straight out of the assembler module (single source of truth).
eval "$(python3 - "$TOOLS" <<'PY'
import sys
sys.path.insert(0, sys.argv[1])
import kc02_shim as s
print(f"SHIM_ADDR=0x{s.SHIM_LOAD_ADDR:08x}")
print(f"CANARY_ADDR=0x{s.CANARY_LOAD_ADDR:08x}")
PY
)"

roundtrip() {
  # roundtrip <kind> <blob> <load-addr> <tsv-out>
  local kind="$1" blob="$2" addr="$3" out="$4"
  local count sha art rc
  count=$(( $(stat -c%s "$blob") / 4 ))
  sha="$(sha256sum "$blob" | cut -d' ' -f1)"
  art="$CACHE/artifacts/$sha"

  echo
  echo "== $kind: load 0x${addr#0x}, ${count} instruction(s), sha256 $sha"
  mkdir -p "$art/input" "$art/project"
  cp "$blob" "$art/input/${sha:0:16}.bin"

  set +e
  "$LAUNCHER" "$art/project" pi-ghidra \
    -import "$art/input/${sha:0:16}.bin" \
    -processor "$LANG" \
    -loader-baseAddr "$addr" \
    -noanalysis \
    -scriptPath "$TOOLS" \
    -postScript DumpShim.java "$addr" "$count" "$out" \
    -max-cpu 1 >"$WORK/$kind.ghidra.log" 2>&1
  rc=$?
  set -e
  grep -E "ERROR|Exception|DumpShim\.java>|Successfully applied" "$WORK/$kind.ghidra.log" || true
  if [[ $rc -ne 0 ]]; then
    echo "analyzeHeadless failed for $kind (rc=$rc); see $WORK/$kind.ghidra.log" >&2
    tail -20 "$WORK/$kind.ghidra.log" >&2
    return 1
  fi

  # Manifest so the interactive ghidra tool treats this as a cached project.
  cat > "$art/manifest.json" <<JSON
{
  "schema": 1,
  "hash": "$sha",
  "source": "$blob",
  "program": "${sha:0:16}.bin",
  "ghidraVersion": "12.1.4",
  "createdAt": "$(date -Is)",
  "note": "THROWAWAY project holding only the KC02 $kind blob; NOT the firmware."
}
JSON
  echo "project : $art/project"
}

# 1. Offline self-test + blob emission.
python3 "$TOOLS/kc02_shim.py" selftest
rm -rf "$CACHE"
python3 "$TOOLS/kc02_shim.py" blob --kind shim   --out "$WORK/shim.bin"
python3 "$TOOLS/kc02_shim.py" blob --kind canary --out "$WORK/canary.bin"

# 2. Round-trip both blobs through Ghidra in the throwaway cache.
roundtrip shim   "$WORK/shim.bin"   "$SHIM_ADDR"   "$TOOLS/shim_roundtrip.txt"
roundtrip canary "$WORK/canary.bin" "$CANARY_ADDR" "$TOOLS/canary_roundtrip.txt"

# 3. Verify the dumps against the assembled bytes.
echo
python3 "$TOOLS/kc02_shim.py" verify-roundtrip "$TOOLS/shim_roundtrip.txt" --kind shim
echo
python3 "$TOOLS/kc02_shim.py" verify-roundtrip "$TOOLS/canary_roundtrip.txt" --kind canary

echo
echo "disassembly dumps: $TOOLS/shim_roundtrip.txt, $TOOLS/canary_roundtrip.txt"
echo "re-read with     : ghidra action=disassemble cacheDir=$CACHE binary=<cache>/artifacts/<sha>/input/<sha16>.bin address=$SHIM_ADDR"
