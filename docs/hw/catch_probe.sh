#!/bin/bash
# Fire the sensor probe the moment the camera is enumerable, retrying while it flaps.
cd /home/jbaiter/.src/kc02-fw-mod || exit 1
for attempt in $(seq 1 30); do
  if lsusb -d 0219:3280 >/dev/null 2>&1; then
    label=$(lsusb -d 0219:3280 | awk '{print $4}')
    echo "  [attempt $attempt] device ${label} visible at $(date +%H:%M:%S) -> probing"
    out=$(timeout 90 ./.env/bin/python /tmp/kc02-board-re/hw/execute_payload.py --payload sensorprobe --input-words 1 2>&1)
    echo "$out" | grep -vE '^🔧' | tail -8 | sed 's/^/    /'
    if echo "$out" | grep -q 'payload executed'; then echo "  PROBE COMPLETED"; exit 0; fi
    echo "    (device dropped mid-run; retrying)"
  fi
  sleep 1
done
echo "  gave up after 30 attempts"
exit 1
