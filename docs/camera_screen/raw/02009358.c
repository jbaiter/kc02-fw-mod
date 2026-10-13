/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009358 Name: FUN_ram_02009358
 */

void FUN_ram_02009358(undefined4 param_1,int param_2,sbyte param_3)

{
  undefined4 unaff_r2;
  undefined4 uVar1;
  
  if (param_2 == 0) {
    uVar1 = win_find_child_by_id(0x10,&stack0x00000000);
    winSetVisible(uVar1,0);
    video_present(1);
  }
  else {
    switch(param_3 + -1) {
    case 0:
      unaff_r2 = 0x8000004e;
      break;
    case 1:
      unaff_r2 = 0x80000050;
      break;
    case 2:
      unaff_r2 = 0x80000051;
      break;
    case 3:
      unaff_r2 = 0x80000052;
      break;
    case 4:
      unaff_r2 = 0x80000053;
      break;
    case 5:
      unaff_r2 = 0x80000054;
      break;
    case 6:
      unaff_r2 = 0x80000055;
      break;
    case 7:
      unaff_r2 = 0x80000056;
      break;
    case 8:
      unaff_r2 = 0x80000057;
      break;
    case 9:
      unaff_r2 = 0x8000004f;
    }
    uVar1 = win_find_child_by_id(param_1,0x10,&stack0x00000000);
    winSetResid(uVar1,unaff_r2);
    uVar1 = win_find_child_by_id(param_1,0x10);
    winSetVisible(uVar1,1);
    video_present(1);
    audio_playback_nv(0x29,1);
  }
  return;
}

