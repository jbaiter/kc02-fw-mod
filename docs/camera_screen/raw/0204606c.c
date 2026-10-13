/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0204606c Name: mjpeg_set_frame_mode
 */

/* Track2 evidence-based naming (likely, names_08.tsv): Forces the JPEG frame-mode field _11646_4_
   to 2 when called with 0 and to 3 when called with non-zero (no-op when already at the target
   value); mode 3 is the only one in which the two span-copy blits run.  [was FUN_ram_0204606c] */

void mjpeg_set_frame_mode(int param_1)

{
  if (param_1 == 0) {
    if (JPEG_ram_020d7582._11646_4_ == 3) {
      JPEG_ram_020d7582._11646_4_ = 2;
    }
  }
  else if (JPEG_ram_020d7582._11646_4_ == 2) {
    JPEG_ram_020d7582._11646_4_ = 3;
  }
  return;
}

