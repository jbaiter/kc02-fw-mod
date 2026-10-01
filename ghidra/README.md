# Ghidra project archive (DO NOT open from this path!)

Ghidra rejects project paths containing a dot-leading element ("Path element
starting with '.' is not permitted"), so this copy under ~/.src/ is an
ARCHIVE ONLY — it cannot be opened in place.

To use it:
  cp -a ~/.src/kc02-fw-mod/ghidra ~/kc02-ghidra   # or any dot-free path
  ghidraRun  ->  open ~/kc02-ghidra/pi-ghidra.gpr -> program d837019dd7de37ec.bin

The live cache used by tools/ghidra_symbols.py is:
  /home/jbaiter/pi-ghidra-cache-kc02-or1k/artifacts/d837019dd7de37ecda89a86399f01060e1a265c48bbc5a4befdf0bc7bb03f19e/project/

Contains 153 annotation records (replayable from tools/ghidra_symbols.tsv):
  python3 tools/ghidra_symbols.py --apply --project-dir <dot-free-project-dir>
