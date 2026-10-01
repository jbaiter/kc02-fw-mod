#!/usr/bin/env bash
# kc02_disasm_region.sh - offline disassembly of arbitrary KC02 code regions.
#
# The main analysis cache (/home/jbaiter/pi-ghidra-cache-kc02-or1k) only holds
# the functions Ghidra's analysis happened to define.  Several UI/dispatch
# routines in this firmware are reached only through computed pointers, so the
# read-only `ghidra` MCP `disassemble` action (which resolves by function) can
# not read them.  This helper imports a *read-only copy* of the firmware into a
# THROWAWAY project with -noanalysis and linearly disassembles the requested
# windows with DumpShim.java.
#
# Safety:
#   * never writes the firmware image (it copies bytes only),
#   * refuses to touch the main analysis cache,
#   * the throwaway cache dir must contain "disastmp",
#   * offline only; no USB device is touched.
#
# Usage:
#   tools/kc02_disasm_region.sh <start> <count> [out.tsv] [<start> <count> <out.tsv> ...]
# Example:
#   tools/kc02_disasm_region.sh 0x020082f4 175 /tmp/disp.tsv 0x02007f00 254 /tmp/mode.tsv
#
# Env: KC02_DUMP (firmware path), KC02_REGION_CACHE (throwaway dir),
#      GHIDRA_HOME, KC02_LOAD_BASE (loader base; default 0x01ffda00).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS="$REPO/tools"

FW="${KC02_DUMP:-$REPO/firmware/original.bin}"
CACHE="${KC02_REGION_CACHE:-/home/jbaiter/pi-ghidra-cache-kc02-disastmp}"
GHIDRA_HOME="${GHIDRA_HOME:-/home/jbaiter/.ghidra/ghidra_12.1.4_PUBLIC}"
LAUNCHER="$GHIDRA_HOME/support/analyzeHeadless"
LANG="KC02:LE:32:nodelay"

# The KC02 application maps CPU 0x02000000 to file offset 0x2600, i.e. the raw
# image must be imported with loader base 0x02000000-0x2600 so that file offset
# 0x2600 lands on CPU 0x02000000.  (File offset 0x2600 holds app code.)
LOAD_BASE="${KC02_LOAD_BASE:-0x01ffda00}"

[[ $# -ge 2 ]] || { echo "usage: $0 <start> <count> [out.tsv] [<start> <count> <out.tsv> ...]" >&2; exit 64; }
[[ -f "$FW" ]] || { echo "firmware not found: $FW (set KC02_DUMP)" >&2; exit 3; }
[[ -x "$LAUNCHER" ]] || { echo "analyzeHeadless not found at $LAUNCHER" >&2; exit 3; }

# --- hard guard: never operate on the real analysis cache -------------------
case "$CACHE" in
  */pi-ghidra-cache-kc02-or1k|*/pi-ghidra-cache|*/pi-ghidra-cache/)
    echo "refusing to use the main analysis cache: $CACHE" >&2; exit 2 ;;
esac
[[ "$CACHE" == *disastmp* ]] || {
  echo "refusing: throwaway cache dir must contain 'disastmp' (got: $CACHE)" >&2
  exit 2
}

# --- parse "start count [out]" groups ---------------------------------------
SPECS=()
while [[ $# -gt 0 ]]; do
  start="$1"; count="$2"; shift 2
  out="/tmp/kc02_region_${start#0x}.tsv"
  if [[ $# -gt 0 && "${1:0:2}" != "0x" ]]; then out="$1"; shift; fi
  SPECS+=("$start" "$count" "$out")
done

# --- byte copy of the whole image (never modified) --------------------------
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp "$FW" "$WORK/fw_copy.bin"
sha="$(sha256sum "$WORK/fw_copy.bin" | cut -d' ' -f1)"
art="$CACHE/artifacts/$sha"
prog="${sha:0:16}_b${LOAD_BASE#0x}.bin"
# throwaway cache: drop any previous copy so the import name is free
rm -rf "$art"
mkdir -p "$art/input" "$art/project"
cp "$WORK/fw_copy.bin" "$art/input/$prog"

ARGS=(-import "$art/input/$prog" -processor "$LANG" -loader-baseAddr "$LOAD_BASE"
      -noanalysis -scriptPath "$TOOLS")
i=0
while [[ $i -lt ${#SPECS[@]} ]]; do
  ARGS+=(-postScript DumpShim.java "${SPECS[$i]}" "${SPECS[$((i+1))]}" "${SPECS[$((i+2))]}")
  i=$((i+3))
done
ARGS+=(-max-cpu 1)

"$LAUNCHER" "$art/project" pi-ghidra "${ARGS[@]}" 2>&1 \
  | grep -E "DumpShim\.java>|ERROR|Exception" || true

cat > "$art/manifest.json" <<JSON
{
  "schema": 1,
  "hash": "$sha",
  "source": "$FW (read-only copy)",
  "program": "$prog",
  "ghidraVersion": "12.1.4",
  "note": "THROWAWAY project: byte copy of the KC02 image for region disassembly."
}
JSON

i=0
while [[ $i -lt ${#SPECS[@]} ]]; do
  echo "wrote ${SPECS[$((i+2))]} (0x${SPECS[$i]#0x}, ${SPECS[$((i+1))]} insns)"
  i=$((i+3))
done
