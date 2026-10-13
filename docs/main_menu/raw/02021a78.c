/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02021a78 Name: photo_jpeg_decode_to_yuv
 */

/* Track2 evidence-based naming (likely, names_07.tsv): Fills a decode-request struct (buffer,
   width, height, mode 1) and calls jpeg_show_or_save; on success it post-processes and
   dcache-flushes width*height*3/2 bytes, i.e. a YUV420 (YUV420SP) photo frame.  [was
   FUN_ram_02021a78] */

bool photo_jpeg_decode_to_yuv
               (undefined4 param_1,uint param_2,undefined4 param_3,ushort param_4,ushort param_5)

{
  int iVar1;
  undefined1 auStack_30 [2];
  undefined1 auStack_2e [2];
  undefined1 uStack_2c;
  undefined1 local_2b;
  uint local_28;
  undefined4 local_24;
  undefined2 local_20;
  undefined4 local_1c;
  ushort local_18;
  ushort local_16;
  
  local_2b = 1;
  local_20 = 1;
  local_28 = param_2;
  local_24 = param_3;
  local_1c = param_1;
  local_18 = param_4;
  local_16 = param_5;
  iVar1 = jpeg_show_or_save(&uStack_2c);
  if (iVar1 >= 0) {
    mjpeg_get_decode_resolution(auStack_30,auStack_2e);
    ax32xx_sysDcacheFlush(param_2,(int)((uint)param_4 * (uint)param_5 * 3) >> 1);
  }
  return iVar1 < 0;
}

