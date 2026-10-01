#!/usr/bin/env bash
# verify_hoist_ghidra.sh - disassembly round-trip proof for the KC02 per-frame
# USB/BOT hoist (TASK A).
#
# OFFLINE ONLY.  Imports two raw blobs into a THROWAWAY Ghidra project/cache:
#
#   1. the 36-byte RAM trampoline at 0x020D9200 (9 instructions)
#   2. the 4-byte code patch word at 0x02000404 (1 instruction: `l.j 0x020d9200`)
#
# disassembles both with the project's KC02:LE:32:nodelay language, and checks
# every instruction byte-for-byte against the hand-assembled bytes in
# tools/kc02_unlock.py (which are in turn derived from the harvested encodings
# in tools/kc02_shim.py).
#
# It never reads or writes the firmware image, never touches the main analysis
# cache (/home/jbaiter/pi-ghidra-cache-kc02-or1k), and never talks to a USB
# device.
#
# Usage:
#   tools/verify_hoist_ghidra.sh [cache-dir]
#
# Default cache dir: /home/jbaiter/pi-ghidra-cache-kc02-hoisttest
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS="$REPO/tools"
CACHE="${1:-${HOIST_CACHE_DIR:-/home/jbaiter/pi-ghidra-cache-kc02-hoisttest}}"
GHIDRA_HOME="${GHIDRA_HOME:-/home/jbaiter/.ghidra/ghidra_12.1.4_PUBLIC}"
LAUNCHER="$GHIDRA_HOME/support/analyzeHeadless"
LANG="KC02:LE:32:nodelay"
OUT="$TOOLS/hoist_roundtrip.txt"

# --- hard safety guard: never operate on the real analysis cache ------------
case "$CACHE" in
  */pi-ghidra-cache-kc02-or1k|*/pi-ghidra-cache|*/pi-ghidra-cache/)
    echo "refusing to use the main analysis cache: $CACHE" >&2; exit 2 ;;
esac
[[ "$CACHE" == *hoisttest* ]] || {
  echo "refusing: throwaway cache dir must contain 'hoisttest' (got: $CACHE)" >&2
  exit 2
}
[[ -x "$LAUNCHER" ]] || { echo "analyzeHeadless not found at $LAUNCHER" >&2; exit 3; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

eval "$(python3 - "$TOOLS" <<'PY'
import sys
sys.path.insert(0, sys.argv[1])
import kc02_unlock as u
print(f"HOOK_ADDR=0x{u.PER_FRAME_HOOK_ADDR:08x}")
print(f"STUB_ADDR=0x{u.HOIST_STUB_ADDR:08x}")
print(f"STUB_LEN={u.HOIST_STUB_LEN}")
PY
)"

python3 "$TOOLS/kc02_unlock.py" selftest
rm -rf "$CACHE"
python3 "$TOOLS/kc02_unlock.py" blob --kind hoist-stub --out "$WORK/stub.bin"
python3 "$TOOLS/kc02_unlock.py" blob --kind hoist-code --out "$WORK/code.bin"

stub_count=$(( $(stat -c%s "$WORK/stub.bin") / 4 ))
code_count=$(( $(stat -c%s "$WORK/code.bin") / 4 ))

dump_region() {  # dump_region <blob> <cpu_addr> <count> <out.tsv> <label>
  local blob="$1" addr="$2" count="$3" out="$4" label="$5"
  local sha art prog
  sha="$(sha256sum "$blob" | cut -d' ' -f1)"
  art="$CACHE/artifacts/$sha"
  prog="${sha:0:16}.bin"
  rm -rf "$art"
  mkdir -p "$art/input" "$art/project"
  cp "$blob" "$art/input/$prog"

  echo
  echo "== $label: load $addr, $count instruction(s), sha256 $sha"
  "$LAUNCHER" "$art/project" pi-ghidra \
    -import "$art/input/$prog" \
    -processor "$LANG" \
    -loader-baseAddr "$addr" \
    -noanalysis \
    -scriptPath "$TOOLS" \
    -postScript DumpShim.java "$addr" "$count" "$out" \
    -max-cpu 1 >"$WORK/ghidra_$label.log" 2>&1 || {
      echo "analyzeHeadless failed for $label; see log" >&2
      tail -20 "$WORK/ghidra_$label.log" >&2
      exit 4
    }
  grep -E "ERROR|Exception|DumpShim\.java>" "$WORK/ghidra_$label.log" || true

  cat > "$art/manifest.json" <<JSON
{
  "schema": 1,
  "hash": "$sha",
  "source": "$blob",
  "program": "$prog",
  "ghidraVersion": "12.1.4",
  "note": "THROWAWAY project holding only one KC02 $label blob; NOT the firmware."
}
JSON
}

dump_region "$WORK/stub.bin" "$STUB_ADDR" "$stub_count" "$WORK/stub.tsv" hoiststub
dump_region "$WORK/code.bin" "$HOOK_ADDR" "$code_count" "$WORK/code.tsv" hoistcode

{
  echo "# KC02 per-frame USB/BOT hoist round-trip dump (throwaway Ghidra project)"
  echo "# 1) 36-byte RAM trampoline at $STUB_ADDR"
  cat "$WORK/stub.tsv"
  echo "# 2) 4-byte code patch word at $HOOK_ADDR"
  cat "$WORK/code.tsv"
} > "$OUT"

echo
python3 "$TOOLS/kc02_unlock.py" verify-hoist-roundtrip "$OUT"
echo
echo "disassembly dump: $OUT"
