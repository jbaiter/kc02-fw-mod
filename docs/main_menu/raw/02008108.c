/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008108 Name: ui_mode_request
 */

/* ui_mode_request(mode, arg): sets nextTaskId=mode and taskArray[mode]->arg=arg (no-op when
   nextTaskId==1).
   Modes ARE tasks - see the kc02_sys_task descriptors and the task_array at 0x020CCD5C.
   taskService_loop 0x02008188 runs close->open->service.
   Modes: 1 Power Off, 2 Video Record, 3 Photo Encode (camera), 4 Play Back, 5 Audio Player, 6 Main
   menu, 7 Game menu, 8 Game, 9 Setting menu, 10 usb device.
   Boot requests mode 10 if USB-connected else mode 6 (CPU 0x020004AC - the CHILD_MODE patch site).
    */

void ui_mode_request(uint param_1,undefined4 param_2)

{
  uint uVar1;
  
  uVar1 = DAT_ram_020ccd50;
  if ((param_1 < 0xc) && (DAT_ram_020ccd50 != 1)) {
    if (task_array[param_1] == (undefined *)0x0) {
      uVar1 = param_1;
      if (param_1 != 0xb) {
        return;
      }
    }
    else {
      *(undefined4 *)(task_array[param_1] + 4) = param_2;
      DAT_ram_020ccd4c = DAT_ram_020ccd54;
      uVar1 = param_1;
    }
  }
  DAT_ram_020ccd50 = uVar1;
  return;
}

