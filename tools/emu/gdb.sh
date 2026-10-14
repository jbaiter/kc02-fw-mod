#!/bin/sh
# Attach to a running AX3292 QEMU GDB stub (localhost only by default).
set -eu
PORT=${1:-1234}
if [ -n "${GDB:-}" ]; then
    DEBUGGER=$GDB
elif command -v gdb-multiarch >/dev/null 2>&1; then
    DEBUGGER=gdb-multiarch
elif [ -x "$HOME/.cache/kc02-gdb/root/usr/bin/gdb-multiarch" ]; then
    DEBUGGER="$HOME/.cache/kc02-gdb/root/usr/bin/gdb-multiarch"
else
    echo 'Install gdb-multiarch, or set GDB to an OpenRISC-capable debugger.' >&2
    exit 2
fi
printf '%s\n' 'Connecting pauses the guest; continue resumes it, detach releases it.' \
    'This stub sends big-endian registers, while guest memory is little-endian.' \
    'Use x/bx for raw memory; do not trust native word/disassembly endian handling.'
exec "$DEBUGGER" -nx -nh \
    -ex 'set architecture or1k' -ex 'set endian big' \
    -ex "target remote 127.0.0.1:$PORT"
