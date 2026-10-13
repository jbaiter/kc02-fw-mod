#!/bin/bash
# Wait for the camera to enumerate, then immediately run the sensor probe.
cd /home/jbaiter/.src/kc02-fw-mod || exit 1
for i in $(seq 1 150); do
  if lsusb -d 0219:3280 >/dev/null 2>&1; then
    echo "  camera present after ${i}s -> running sensor probe now"
    sleep 2   # let enumeration settle
    timeout 120 ./.env/bin/python /tmp/kc02-board-re/hw/execute_payload.py --payload sensorprobe --input-words 1 2>&1 | tail -7
    exit $?
  fi
  sleep 1
done
echo "  timed out waiting for the camera (150s)"
exit 1
