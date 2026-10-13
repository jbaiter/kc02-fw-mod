/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009de0 Name: FUN_ram_02009de0
 */

undefined4 FUN_ram_02009de0(undefined4 param_1,int param_2,int *param_3)

{
  if ((param_2 == 1) && (*param_3 == 0)) {
    if (app_context.b5f == 0) {
      app_context.b5f = 1;
      if ((app_context.b59 == 0) && (app_context.b5b != 0)) {
        app_context.b5b = 7;
      }
    }
    else {
      app_context.b5f = 0;
    }
    FUN_ram_02009d5c(param_1,&stack0x00000000);
    FUN_ram_02008d98(param_1);
  }
  return 0;
}

