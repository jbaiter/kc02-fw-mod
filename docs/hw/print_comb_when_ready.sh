#!/bin/bash
# Wait for the camera, then print the counting comb and VALIDATE that the result is fresh
# (ink_px must match the comb's ~1096, otherwise the report file is stale and we retry).
cd /home/jbaiter/.src/kc02-fw-mod || exit 1
REP=/tmp/kc02-board-re/hw/run_ramprint_None.json
for attempt in $(seq 1 60); do
  if lsusb -d 0219:3280 >/dev/null 2>&1; then
    sleep 2
    rm -f "$REP"
    echo "  camera up (attempt $attempt) -> printing the comb"
    timeout 200 ./.env/bin/python /tmp/kc02-board-re/hw/execute_payload.py --payload ramprint \
        --input-words 96,3,0,1,0,1 > /tmp/kc02-board-re/hw/comb.log 2>&1
    if [ -f "$REP" ]; then
      ink=$(python3 -c "import json;print(json.load(open('$REP')).get('reply',{}).get('output',[0,0,0,0])[3])" 2>/dev/null)
      st=$(python3 -c "import json;print(json.load(open('$REP')).get('reply',{}).get('output',[0]*8)[7])" 2>/dev/null)
      if [ "$ink" = "1096" ]; then
        echo "  PRINTED: comb pattern, ink_px=1096, status=$st  (0 = ok, 8 = no paper, 9 = low supply)"
        exit 0
      fi
      echo "  ran but ink_px=$ink (expected 1096) status=$st -> retrying"
    else
      echo "  device dropped mid-run -> retrying"
    fi
  fi
  sleep 2
done
echo "  gave up - replug the camera and run: docs/hw/print_comb_when_ready.sh"
exit 1
