#!/usr/bin/env bash
# Bounded preview bench run under one continuous USB claim.
# Waits for the KC02, then runs the measured preview session. No flash writes.
set -eu -o pipefail

REPO="${REPO:-$HOME/.src/kc02-fw-mod}"
PHASE="${PHASE:-$(<"$HOME/kc02-hw/current-preview")}"
OBSERVE="${OBSERVE:-30}"
WARMUP="${WARMUP:-8}"

cd "$REPO"

if [[ ! -x .env/bin/python ]]; then
    echo "missing .env/bin/python" >&2
    exit 1
fi

echo "waiting for KC02 (0219:3280 / 1908:3283) ..."
for _ in $(seq 1 600); do
    if lsusb -d 0219:3280 >/dev/null 2>&1 || lsusb -d 1908:3283 >/dev/null 2>&1; then
        break
    fi
    sleep 1
done
if ! lsusb -d 0219:3280 >/dev/null 2>&1 && ! lsusb -d 1908:3283 >/dev/null 2>&1; then
    echo "KC02 did not appear within 10 minutes" >&2
    exit 1
fi
lsusb -d 0219:3280 2>/dev/null || lsusb -d 1908:3283

if mount | grep -qE '/dev/sd[a-z]1'; then
    echo "refusing: camera SD volume is mounted" >&2
    exit 1
fi

OUT="$PHASE/sessions/preview-$(date +%H%M%S)"
mkdir -p "$(dirname "$OUT")"
echo "output: $OUT"

flock -n "$HOME/kc02-hw/usb.lock" \
    .env/bin/python docs/hw/preview_session.py \
        --payload-dir "$PHASE/single-session-probes" \
        --output "$OUT" \
        --observe-seconds "$OBSERVE" \
        --warmup-seconds "$WARMUP"
