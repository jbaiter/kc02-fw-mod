#!/bin/bash
# Wait for the camera, then print the given patterns (pairs of "id:desc"), validating each result
# freshly (fresh report file + ink_px > 0 + status 0). Retries while the device flaps.
cd /home/jbaiter/.src/kc02-fw-mod || exit 1
REP=/tmp/kc02-board-re/hw/run_ramprint_None.json
pat_list="$@"
for attempt in $(seq 1 90); do
  if lsusb -d 0219:3280 >/dev/null 2>&1; then
    sleep 2
    all_ok=1
    for spec in $pat_list; do
      pat=${spec%%:*}; desc=${spec#*:}
      rm -f "$REP"
      timeout 240 ./.env/bin/python /tmp/kc02-board-re/hw/execute_payload.py --payload ramprint \
          --input-words 64,3,0,1,0,$pat > /tmp/kc02-board-re/hw/p$pat.log 2>&1
      if [ -f "$REP" ]; then
        read ink st < <(python3 -c "
import json;o=json.load(open('$REP')).get('reply',{}).get('output',[0,0,0,0,0,0,0,0]);print(o[3],o[7])" 2>/dev/null)
        echo "  pattern $pat ($desc): ink_px=$ink status=$st"
        [ "$st" = "0" ] || all_ok=0
      else
        echo "  pattern $pat: device dropped before the report -> retry"; all_ok=0; break
      fi
      sleep 2
    done
    [ "$all_ok" = "1" ] && { echo "  both patterns printed"; exit 0; }
  fi
  sleep 2
done
echo "  gave up; replug and rerun: docs/hw/print_patterns_when_ready.sh '2:card' '3:ruler'"
exit 1
