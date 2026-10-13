#!/bin/bash
# One-shot card fixup: remove my non-sequential/grayscale files, install the pattern under
# sequential photo names. Run while the camera is in USB/SD mode and the host has the card.
set -e
cd /home/jbaiter/.src/kc02-fw-mod
findmnt -no TARGET /dev/sda1 >/dev/null 2>&1 || udisksctl mount -b /dev/sda1 >/dev/null
MP=$(findmnt -no TARGET /dev/sda1)
echo "  card: $MP"
echo "  --- removing my earlier files (non-sequential names / grayscale content):"
for f in TEST384.JPG TEST384A.JPG TEST672.JPG TEST384C.JPG TEST384AC.JPG TEST672C.JPG PHO09999.JPG PHO09997.JPG; do
  if [ -f "$MP/PHOTO/$f" ]; then rm -f "$MP/PHOTO/$f"; echo "      removed $f"; fi
done
echo "  --- installing (3-component colour, sequential names):"
for n in 00045 00046 00047 00048; do
  cp "/tmp/kc02-board-re/hw/stage_PHO$n.JPG" "$MP/PHOTO/PHO$n.JPG"
  printf "      PHO%s.JPG  %s bytes  %s\n" "$n" "$(stat -c%s "$MP/PHOTO/PHO$n.JPG")" "$(md5sum "$MP/PHOTO/PHO$n.JPG" | cut -c1-12)"
done
sync
echo "  --- PHOTO/ now (last 10):"
ls "$MP/PHOTO" | sort | tail -10 | sed 's/^/      /'
udisksctl unmount -b /dev/sda1 >/dev/null
echo "  unmounted; the camera should be power-cycled once so it re-mounts the card"
