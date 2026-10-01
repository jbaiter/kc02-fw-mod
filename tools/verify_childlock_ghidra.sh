#!/usr/bin/env bash
# verify_childlock_ghidra.sh - disassembly round-trip proof for the KC02
# child-friendly lock (TASK B: boot-to-camera + adult-only Main menu).
#
# OFFLINE ONLY. Imports five tiny raw blobs into a THROWAWAY Ghidra
# project/cache, disassembles them with the project's KC02:LE:32:nodelay
# language and checks every instruction byte-for-byte against the
# hand-assembled bytes in tools/kc02_unlock.py (which are built from the
# encoders in tools/kc02_shim.py, themselves verified against instructions
# harvested from the stock image):
#
#   1. the 52-byte adult-gate RAM trampoline at 0x020D9400
#   2. the 24-byte UP-tap RAM trampoline     at 0x020D9440
#   3. the patched boot-mode word            at 0x020004AC
#   4. the patched menu-gate handler word    at 0x02009F08
#   5. the patched UP-handler word           at 0x02009890
#
# It never reads or writes the firmware image, never touches the main analysis
# cache (/home/jbaiter/pi-ghidra-cache-kc02-or1k), and never talks to a USB
# device.
#
# Usage:
#   tools/verify_childlock_ghidra.sh [cache-dir]
#
# Default cache dir: /home/jbaiter/pi-ghidra-cache-kc02-childlocktest
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS="$REPO/tools"
CACHE="${1:-${CHILDLOCK_CACHE_DIR:-/home/jbaiter/pi-ghidra-cache-kc02-childlocktest}}"
GHIDRA_HOME="${GHIDRA_HOME:-/home/jbaiter/.ghidra/ghidra_12.1.4_PUBLIC}"
LAUNCHER="$GHIDRA_HOME/support/analyzeHeadless"
LANG="KC02:LE:32:nodelay"
OUT="$TOOLS/childlock_roundtrip.txt"

# --- hard safety guard: never operate on the real analysis cache ------------
case "$CACHE" in
  */pi-ghidra-cache-kc02-or1k|*/pi-ghidra-cache|*/pi-ghidra-cache/)
    echo "refusing to use the main analysis cache: $CACHE" >&2; exit 2 ;;
esac
[[ "$CACHE" == *childlocktest* ]] || {
  echo "refusing: throwaway cache dir must contain 'childlocktest' (got: $CACHE)" >&2
  exit 2
}
[[ -x "$LAUNCHER" ]] || { echo "analyzeHeadless not found at $LAUNCHER" >&2; exit 3; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

eval "$(python3 - "$TOOLS" <<'PY'
import sys
sys.path.insert(0, sys.argv[1])
import kc02_unlock as u
print(f"GATE_ADDR=0x{u.GATE_STUB_ADDR:08x}")
print(f"GATE_LEN={u.GATE_STUB_LEN}")
print(f"SEQ_ADDR=0x{u.SEQ_STUB_ADDR:08x}")
print(f"SEQ_LEN={u.SEQ_STUB_LEN}")
print(f"BOOT_ADDR=0x{u.BOOT_MODE_WORD_ADDR:08x}")
print(f"GATE_CODE_ADDR=0x{u.MENU_GATE_HANDLER_ADDR:08x}")
print(f"UP_CODE_ADDR=0x{u.UP_HANDLER_ADDR:08x}")
PY
)"

python3 "$TOOLS/kc02_unlock.py" selftest
rm -rf "$CACHE"
python3 "$TOOLS/kc02_unlock.py" blob --kind gate-stub --out "$WORK/gate.bin" >/dev/null
python3 "$TOOLS/kc02_unlock.py" blob --kind seq-stub --out "$WORK/seq.bin" >/dev/null
python3 "$TOOLS/kc02_unlock.py" blob --kind boot-code --out "$WORK/boot.bin" >/dev/null
python3 "$TOOLS/kc02_unlock.py" blob --kind gate-code --out "$WORK/gatecode.bin" >/dev/null
python3 "$TOOLS/kc02_unlock.py" blob --kind up-code --out "$WORK/upcode.bin" >/dev/null

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

dump_region "$WORK/gate.bin"     "$GATE_ADDR"      "$(( GATE_LEN / 4 ))" "$WORK/gate.tsv"     childlockgate
dump_region "$WORK/seq.bin"      "$SEQ_ADDR"       "$(( SEQ_LEN / 4 ))"  "$WORK/seq.tsv"      childlockseq
dump_region "$WORK/boot.bin"     "$BOOT_ADDR"      1                     "$WORK/boot.tsv"     childlockboot
dump_region "$WORK/gatecode.bin" "$GATE_CODE_ADDR" 1                     "$WORK/gatecode.tsv" childlockgatecode
dump_region "$WORK/upcode.bin"   "$UP_CODE_ADDR"   1                     "$WORK/upcode.tsv"   childlockupcode

{
  echo "# KC02 child-lock round-trip dump (throwaway Ghidra project)"
  echo "# 1) ${GATE_LEN}-byte adult-gate RAM trampoline at $GATE_ADDR"
  cat "$WORK/gate.tsv"
  echo "# 2) ${SEQ_LEN}-byte UP-tap RAM trampoline at $SEQ_ADDR"
  cat "$WORK/seq.tsv"
  echo "# 3) 4-byte patched boot-mode word at $BOOT_ADDR"
  cat "$WORK/boot.tsv"
  echo "# 4) 4-byte patched menu-gate handler word at $GATE_CODE_ADDR"
  cat "$WORK/gatecode.tsv"
  echo "# 5) 4-byte patched UP-handler word at $UP_CODE_ADDR"
  cat "$WORK/upcode.tsv"
} > "$OUT"

echo
python3 "$TOOLS/kc02_unlock.py" verify-childlock-roundtrip "$OUT"

# Optional: if an original image copy is available, re-verify every embedded
# stock word (read-only) and print the exact file offsets for the recipe.
FW="${KC02_DUMP:-$REPO/firmware/original.bin}"
if [[ -f "$FW" ]]; then
  echo
  python3 "$TOOLS/kc02_unlock.py" stock-check --image "$FW"
fi

echo
echo "disassembly dump: $OUT"
