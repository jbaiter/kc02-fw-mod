/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02025154 Name: win_find_child_by_id
 */

/* Track2 evidence-based naming (likely, names_07.tsv): Posts message 0x19 to the window and then
   walks the child chain at param_1+0x2c comparing widgetGetId(child) with the requested id (only
   searched when id > 0x13), returning the matching child pointer.  [was FUN_ram_02025154] */

int win_find_child_by_id(uint param_1,uint param_2)

{
  uint uVar1;
  undefined4 local_20 [2];
  int local_18;
  uint local_14;
  
  param_2 = param_2 & 0xffff;
  if (((int)(param_1 | -param_1) < 0) &&
     (local_18 = -((int)(((param_2 ^ 0xffffffff) & 0xffff) - 1) >> 0x1f), local_18 == 0)) {
    local_20[0] = 0x19;
    local_14 = param_2;
    winSendMsg(local_20);
    if (local_18 != 0) {
      return local_18;
    }
    if (0x13 < param_2) {
      do {
        uVar1 = widgetGetId(*(undefined4 *)(param_1 + 0x2c));
        if (uVar1 == param_2) {
          return *(int *)(param_1 + 0x2c);
        }
        param_1 = *(uint *)(param_1 + 0x2c);
      } while (param_1 != 0);
    }
  }
  return 0;
}

