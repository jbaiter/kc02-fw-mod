/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020250a4 Name: win_create_widget_list
 */

/* Track2b second-opinion confirm (names_03.tsv): Invokes the first descriptor's procedure to obtain
   a handle, then walks 0x30-byte descriptors and invokes their procedures, notifying each result
   with winSendMsgToParentNoData(...,0x1a). The handle flag at +0x1a and winSetbgColor further
   corroborate the window/widget context.  [was FUN_ram_020250a4] */

int win_create_widget_list(undefined4 *param_1,undefined4 param_2,undefined4 param_3)

{
  undefined4 uVar1;
  int local_14;
  
  if ((param_1 != (undefined4 *)0x0) &&
     (local_14 = (*(code *)*param_1)(param_3,param_2), local_14 != 0)) {
    if ((*(ushort *)(local_14 + 0x1a) & 2) == 0) {
      while( true ) {
        param_1 = param_1 + 0xc;
        if ((code *)*param_1 == (code *)0x0) break;
        uVar1 = (*(code *)*param_1)(param_1,param_2);
        winSendMsgToParentNoData(uVar1,0x1a);
      }
      return local_14;
    }
    winSetbgColor(&local_14);
  }
  return 0;
}

