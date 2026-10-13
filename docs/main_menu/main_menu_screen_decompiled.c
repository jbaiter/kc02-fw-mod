/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020130bc Name: mode_menu_tick
 */

/* pipeline map */

void mode_menu_tick(void)

{
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020130d0 Name: mode_menu_exit
 */

/* pipeline map */

void mode_menu_exit(void)

{
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020130e4 Name: mode_menu_enter
 */

/* mode 6 Main menu */

void mode_menu_enter(void)

{
  ax32xx_csiEnable(0);
  ui_state_apply(7,register0x00000401);
  win_open(&PTR_mode6_main_menu_keymap_ram_020c4c2c,0);
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02013120 Name: main_menu_msg0a_msg0a
 */

/* device-verified mode descriptor 'Main menu' (0x020C4C18) keymap entry: handler for message 0x0A
   (msg0a); map at 0x020C4C3C  [was FUN_ram_02013120] */

undefined4 main_menu_msg0a_msg0a(void)

{
  return 0;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02013138 Name: FUN_ram_02013138
 */

void FUN_ram_02013138(int param_1)

{
  undefined *p0;
  int iVar1;
  ushort uStack_14;
  ushort uStack_12;
  
  p0 = dispLayerGetFrame(0);
  if (p0 != (undefined *)0x0) {
    display_get_mode(&uStack_14,&uStack_12);
    hal_dispframeVideoCfg(p0,0,0,uStack_14,uStack_12);
    if (param_1 == 0) {
      memcpy(*(undefined4 *)(p0 + 4),uRam020ccecc,
             (int)((uint)uRam020cceca * (uint)uRam020ccec8 * 3) >> 1);
      iVar1 = (uint)app_context.b67 * 4;
      blit_b(*(undefined4 *)(p0 + 4),*(undefined2 *)(p0 + 0x20),*(undefined2 *)(p0 + 0x22),
             (int)*(short *)(&main_menu_icon_coords + iVar1),(int)*(short *)(iVar1 + 0x207bb62),
             uRam020cceac,*(undefined4 *)(iVar1 + 0x20cceb0),uRam020cceae,uRam020cceac,0x8a,0x81);
      ax32xx_sysDcacheFlush
                (*(uint *)(p0 + 4),
                 (int)((uint)*(ushort *)(p0 + 0x20) * (uint)*(ushort *)(p0 + 0x22) * 3) >> 1);
    }
    else {
      memcpy(*(undefined4 *)(p0 + 4),uRam020ccecc,
             (int)((uint)uRam020cceca * (uint)uRam020ccec8 * 3) >> 1);
      ax32xx_sysDcacheFlush
                (*(uint *)(p0 + 4),
                 (int)((uint)*(ushort *)(p0 + 0x20) * (uint)*(ushort *)(p0 + 0x22) * 3) >> 1);
    }
    dispLayerShow(p0,0,0,0xff);
  }
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020132e4 Name: FUN_ram_020132e4
 */

void FUN_ram_020132e4(undefined4 param_1,int param_2)

{
  undefined4 uVar1;
  
  if (param_2 == 0) {
    uVar1 = win_find_child_by_id(&stack0x00000000);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,1);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,2);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,3);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,4);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,5);
  }
  else {
    uVar1 = win_find_child_by_id(0,&stack0x00000000);
    winSetVisible(uVar1,1);
    uVar1 = win_find_child_by_id(param_1,1);
    winSetVisible(uVar1,1);
    uVar1 = win_find_child_by_id(param_1,2);
    winSetVisible(uVar1,1);
    uVar1 = win_find_child_by_id(param_1,3);
    winSetVisible(uVar1,1);
    uVar1 = win_find_child_by_id(param_1,4);
    winSetVisible(uVar1,1);
    uVar1 = win_find_child_by_id(param_1,5);
  }
  winSetVisible(uVar1,param_2 != 0);
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02013434 Name: main_menu_free_icons
 */

undefined4 main_menu_free_icons(void)

{
  int iVar1;
  undefined *p0;
  
  if (puRam020ccecc != (undefined *)0x0) {
    hal_sysMemFree(puRam020ccecc);
    puRam020ccecc = (undefined *)0x0;
  }
  iVar1 = 0;
  do {
    p0 = *(undefined **)(iVar1 + 0x20cceb0);
    if (p0 != (undefined *)0x0) {
      hal_sysMemFree(p0);
      *(undefined4 *)(iVar1 + 0x20cceb0) = 0;
    }
    iVar1 = iVar1 + 4;
  } while (iVar1 != 0x18);
  return 0;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020134b8 Name: main_menu_init_icons
 */

/* six icons from 0x0207BB78 */

undefined4 main_menu_init_icons(undefined4 param_1)

{
  undefined4 *puVar1;
  undefined *p0;
  undefined *puVar2;
  int iVar3;
  
  uRam020cced0 = 0;
  if (5 < app_context.b67) {
    app_context.b67 = 0;
  }
  display_get_mode(0x20cceca,0x20ccec8,&stack0x00000000);
  puRam020ccecc = hal_sysMemMalloc((int)((uint)uRam020cceca * (uint)uRam020ccec8 * 3) >> 1,0x20);
  if (puRam020ccecc != (undefined *)0x0) {
    photo_jpeg_decode_to_yuv
              (0x20,puRam020ccecc,puRam020ccecc + (uint)uRam020cceca * (uint)uRam020ccec8);
    uRam020cceae = 0x60;
    uRam020cceac = 0x60;
    p0 = hal_sysMemMalloc(0x6c00,0x20);
    if (p0 != (undefined *)0x0) {
      iVar3 = 0;
      do {
        puVar2 = hal_sysMemMalloc((int)((uint)uRam020cceae * (uint)uRam020cceac * 3) >> 1,0x20);
        *(undefined **)(iVar3 + 0x20cceb0) = puVar2;
        if (puVar2 == (undefined *)0x0) {
          return 0;
        }
        puVar1 = (undefined4 *)(&main_menu_icon_resources + iVar3);
        iVar3 = iVar3 + 4;
        icon_decode(*puVar1,p0,puVar2,puVar2 + (uint)uRam020cceae * (uint)uRam020cceac);
      } while (iVar3 != 0x18);
      hal_sysMemFree(p0);
      FUN_ram_02013138(uRam020cced0);
      uRam020cced4 = XOSTimeGet();
      FUN_ram_020132e4(param_1,1);
    }
  }
  return 0;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02013698 Name: ui_toggle_state_300ticks
 */

/* Track2b second-opinion revise (names_02.tsv): XOSTimeGet is compared against iRam020cced4+300,
   then sRam020cced0 is boolean-toggled and passed to func_0x02013138. The body establishes a
   300-tick state toggle, not milliseconds or an indicator.  [was FUN_ram_02013698] */

undefined4 ui_toggle_state_300ticks(void)

{
  uint uVar1;
  
  uVar1 = XOSTimeGet();
  if (iRam020cced4 + 300U <= uVar1) {
    sRam020cced0 = sRam020cced0 == 0;
    FUN_ram_02013138(sRam020cced0,register0x00000401);
    iRam020cced4 = XOSTimeGet();
  }
  return 0;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02013714 Name: main_menu_next_item
 */

/* menu item idx 0..5 wrap */

undefined4 main_menu_next_item(int param_1,int *param_2)

{
  if ((param_1 == 1) && (*param_2 == 0)) {
    if (app_context.b67 < 5) {
      app_context.b67 = app_context.b67 + 1;
    }
    else {
      app_context.b67 = 0;
    }
    uRam020cced0 = 0;
    FUN_ram_02013138(0,&stack0x00000000);
    uRam020cced4 = XOSTimeGet();
  }
  return 0;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0201379c Name: main_menu_prev_item
 */

undefined4 main_menu_prev_item(int param_1,int *param_2)

{
  if ((param_1 == 1) && (*param_2 == 0)) {
    if (app_context.b67 == 0) {
      app_context.b67 = 5;
    }
    else {
      app_context.b67 = app_context.b67 - 1;
    }
    uRam020cced0 = 0;
    FUN_ram_02013138(0,&stack0x00000000);
    uRam020cced4 = XOSTimeGet();
  }
  return 0;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02013828 Name: main_menu_page_up
 */

undefined4 main_menu_page_up(int param_1,int *param_2)

{
  sbyte sVar1;
  
  if ((param_1 == 1) && (*param_2 == 0)) {
    if (app_context.b67 < 3) {
      sVar1 = 3;
    }
    else {
      sVar1 = -3;
    }
    app_context.b67 = app_context.b67 + sVar1;
    uRam020cced0 = 0;
    FUN_ram_02013138(0,&stack0x00000000);
    uRam020cced4 = XOSTimeGet();
  }
  return 0;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020138b0 Name: main_menu_page_down
 */

undefined4 main_menu_page_down(int param_1,int *param_2)

{
  sbyte sVar1;
  
  if ((param_1 == 1) && (*param_2 == 0)) {
    if (app_context.b67 < 3) {
      sVar1 = 3;
    }
    else {
      sVar1 = -3;
    }
    app_context.b67 = app_context.b67 + sVar1;
    uRam020cced0 = 0;
    FUN_ram_02013138(0,&stack0x00000000);
    uRam020cced4 = XOSTimeGet();
  }
  return 0;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02013938 Name: main_menu_ok
 */

/* menu item -> mode: 0->3, 1->2, 2->5, 3->4, 4->7, 5->9 */

undefined4 main_menu_ok(int param_1,int *param_2)

{
  byte bVar1;
  undefined4 uVar2;
  
  if ((param_1 == 1) && (*param_2 == 0)) {
    FUN_ram_020132e4(0,&stack0x00000000);
    video_present(1);
    uVar2 = 3;
    if ((app_context.b67 != 0) && (uVar2 = 2, app_context.b67 != 1)) {
      if (app_context.b67 == 2) {
        ui_mode_request(5,0);
        bVar1 = app_context.b68;
        if (app_context.b68 != 0) {
          return 0;
        }
        app_context.b68 = 1;
        app_context.b69 = bVar1;
        return 0;
      }
      uVar2 = 4;
      if ((app_context.b67 != 3) && (uVar2 = 7, app_context.b67 != 4)) {
        if (app_context.b67 != 5) {
          return 0;
        }
        uVar2 = 9;
      }
    }
    ui_mode_request(uVar2,0);
  }
  return 0;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02013a14 Name: video_request_present_mode
 */

/* Track2b second-opinion confirm (names_02.tsv): For event 1 with *param_2==0, the body calls
   func_0x020132e4(0), video_present(1), and ui_mode_request(4,0). The named video-present and
   UI-mode callees directly support the proposal.  [was FUN_ram_02013a14] */

undefined4 video_request_present_mode(int param_1,int *param_2)

{
  if ((param_1 == 1) && (*param_2 == 0)) {
    FUN_ram_020132e4(0,&stack0x00000000);
    video_present(1);
    ui_mode_request(4,0);
  }
  return 0;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02000404 Name: app_main_loop
 */

/* per-frame: sys_irq_clear, system_tick(1), ui_key_dispatch, video_present */

void app_main_loop(void)

{
  sys_irq_clear(&stack0x00000000);
  system_tick(1);
  ui_key_dispatch();
  video_present(0);
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020079b4 Name: ui_key_dispatch
 */

/* UI key dispatcher (l.jalr r14 at 0x02007A98); r4=1, r5=&flags, ret 0x02007A9C */

void ui_key_dispatch(void)

{
  uint uVar1;
  bool bVar2;
  undefined *puVar3;
  undefined3 extraout_var;
  int iVar4;
  undefined4 uVar5;
  code *pcVar6;
  sbyte sStack_15;
  uint uStack_14;
  
  puVar3 = XMsgQPend((undefined *)app_context._116_4_,&sStack_15);
  if ((sStack_15 == 0) && (uVar1 = (uint)puVar3 & 0xffff, uVar1 < 0x2e)) {
    uStack_14 = (uint)puVar3 >> 0x10;
    if (((uVar1 - 0x1a < 0x14) && (uStack_14 == 0)) &&
       ((((bVar2 = menuIsOpen(), CONCAT31(extraout_var,bVar2) != 0 ||
          (iVar4 = ui_mode_get_current(), iVar4 != 4)) && (uVar1 != 0x26)) &&
        ((iVar4 = ui_mode_get_current(), iVar4 != 5 || (iVar4 = audio_core_state(), iVar4 != 1))))))
    {
      FUN_ram_0200ce14();
    }
    pcVar6 = (code *)(&DAT_ram_020ccc8c)[uVar1];
    if (pcVar6 != (code *)0x0) {
      uVar5 = ui_get_ctx();
      (*pcVar6)(uVar5,1,&uStack_14);
    }
    pcVar6 = *(code **)(&DAT_ram_020ccbd4 + uVar1 * 4);
    if (pcVar6 != (code *)0x0) {
      uVar5 = ui_get_ctx();
      (*pcVar6)(uVar5,1,&uStack_14);
    }
  }
  return;
}



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



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008188 Name: mode_manager_loop
 */

/* close->open->service of the selected task (curTask at 0x020CCD58) */

void mode_manager_loop(void)

{
  uint uVar1;
  int iVar2;
  int iVar3;
  
  iVar3 = 0;
  while (DAT_ram_020ccd50 != 0xb) {
    if (puRam020ccd58 != (undefined *)0x0) {
      if (*(code **)(puRam020ccd58 + 0xc) != (code *)0x0) {
        (**(code **)(puRam020ccd58 + 0xc))(*(undefined4 *)(puRam020ccd58 + 4),register0x00000401);
      }
      winDestroyDeskTopChildWin();
    }
    iVar2 = iVar3 + 1;
    uiHeapInfor(iVar3);
    uVar1 = DAT_ram_020ccd50;
    DAT_ram_020ccd54 = DAT_ram_020ccd50;
    DAT_ram_020ccd50 = 0xc;
    puRam020ccd58 = task_array[uVar1];
    if (puRam020ccd58 == (undefined *)0x0) {
      DAT_ram_020ccd54 = 1;
      puRam020ccd58 = task_array[1];
    }
    if (*(code **)(puRam020ccd58 + 8) != (code *)0x0) {
      (**(code **)(puRam020ccd58 + 8))(*(undefined4 *)(puRam020ccd58 + 4));
    }
    if (DAT_ram_020ccd54 != 1) {
      video_present(1);
    }
    while (iVar3 = iVar2, 0xb < DAT_ram_020ccd50) {
      app_main_loop();
      if (*(code **)(puRam020ccd58 + 0x10) != (code *)0x0) {
        (**(code **)(puRam020ccd58 + 0x10))(*(undefined4 *)(puRam020ccd58 + 4));
      }
    }
  }
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020047b4 Name: dispLayerShow
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: int dispLayerShow(INT8U layer,INT32U addr,INT16U width,INT16U height,INT8U rotate)
   [ax32_platform_demo/inc/display.h] */

int dispLayerShow(sbyte param_1,undefined *param_2)

{
  if (param_1 == 0) {
    hal_lcdSetFrame(0,0,0,0,param_2,0);
  }
  return 0;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020047f8 Name: dispLayerGetFrame
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void *dispLayerGetFrame(INT8U layer)   [ax32_platform_demo/inc/display.h] */

undefined * dispLayerGetFrame(uint param_1)

{
  undefined *puVar1;
  
  puVar1 = (undefined *)0x0;
  if ((param_1 & 0xff) < 3) {
    if ((param_1 & 0xff) - 1 < 2) {
      puVar1 = (undefined *)lcd_osd_get_show_addr(0,&stack0x00000000);
    }
    else {
      puVar1 = hal_lcdGetIdleBuffer();
    }
  }
  return puVar1;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02004944 Name: ui_state_apply
 */

/* state==7 special; else table 0x0207B908[state] */

undefined4 ui_state_apply(uint param_1)

{
  param_1 = param_1 & 0xff;
  if (param_1 == 7) {
    hal_lcdSetPIPEnable(0);
  }
  else {
    if (5 < param_1) {
      return 0xffffffff;
    }
    (*(code *)ui_state_dispatch[param_1])(&stack0x00000000);
  }
  return 0;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02012150 Name: FUN_ram_02012150
 */

void FUN_ram_02012150(uint *param_1)

{
  uint uVar1;
  undefined4 uVar2;
  uint uVar3;
  int iVar4;
  undefined4 uVar5;
  undefined4 *puVar6;
  undefined4 uStack_20;
  undefined4 uStack_1c;
  undefined4 uStack_18;
  
  uVar3 = *param_1;
  puVar6 = &uStack_20;
  uVar1 = param_1[1];
  if (uVar3 == 0x15) {
    uVar3 = param_1[3];
    uVar2 = 3;
    uStack_20 = *(undefined4 *)(uVar3 + 0x14);
    uStack_1c = *(undefined4 *)(uVar3 + 0x18);
    uStack_18 = *(undefined4 *)(uVar3 + 0x1c);
    uVar5 = 0x12;
LAB_ram_020122b4:
    ui_key_invoke(uVar5,uVar1,uVar2,puVar6);
  }
  else {
    if (uVar3 < 0x16) {
      if (uVar3 == 2) {
        if (*(undefined4 **)(uVar1 + 0x84) == (undefined4 *)0x0) {
          return;
        }
        msgFuncRegister((msgDealInfor *)**(undefined4 **)(uVar1 + 0x84));
        uVar2 = 0;
        uVar5 = 0x10;
        puVar6 = (undefined4 *)0x0;
        goto LAB_ram_020122b4;
      }
      if (uVar3 != 4) {
        return;
      }
      uVar5 = 0;
      uVar2 = **(undefined4 **)(uVar1 + 0x84);
      uVar3 = param_1[3];
      puVar6 = (undefined4 *)0x0;
    }
    else {
      if (uVar3 != 0x22) {
        if (uVar3 != 0x23) {
          return;
        }
        puVar6 = *(undefined4 **)(uVar1 + 0x84);
        if (puVar6 == (undefined4 *)0x0) {
          return;
        }
        msgDealByMsgInfor(*puVar6,0xf,uVar1,0);
        puVar6[2] = 0;
        return;
      }
      puVar6 = *(undefined4 **)(uVar1 + 0x84);
      if (puVar6 == (undefined4 *)0x0) {
        return;
      }
      iVar4 = *(int *)(uVar1 + 0x24);
      if (((iVar4 != 0) && ((*(ushort *)(iVar4 + 0x1a) & 0x10) != 0)) &&
         (*(undefined4 **)(iVar4 + 0x84) != (undefined4 *)0x0)) {
        msgDealByMsgInfor(**(undefined4 **)(iVar4 + 0x84),0x11,0);
      }
      uVar2 = *puVar6;
      uVar5 = *(undefined4 *)param_1[3];
      uVar3 = 0xe;
      puVar6 = (undefined4 *)param_1[3] + 1;
    }
    msgDealByMsgInfor(uVar2,uVar3,uVar1,uVar5,puVar6);
  }
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02012304 Name: win_open
 */

/* Track2 evidence-based naming (likely, names_03.tsv): window open helper: looks up/reuses a window
   handle, registers the msg function, sends message 0x22 and returns the handle; used with several
   window descriptors across the UI.  [was FUN_ram_02012304] */

undefined4 win_open(undefined4 *param_1,uint param_2)

{
  uint *puVar1;
  uint uVar2;
  uint *puVar3;
  int iVar4;
  undefined4 local_38 [3];
  uint *local_2c;
  uint local_28;
  uint local_24 [5];
  
  if (5 < param_2) {
    param_2 = 5;
  }
  puVar1 = &local_28;
  puVar3 = (uint *)register0x00000401;
  for (uVar2 = 0; puVar1 = puVar1 + 1, uVar2 != param_2; uVar2 = uVar2 + 1) {
    *puVar1 = *puVar3;
    puVar3 = puVar3 + 1;
  }
  local_28 = param_2;
  if (param_1[2] == 0) {
    if (param_1[1] != 0) {
      *(undefined4 **)(param_1[1] + 0x24) = param_1;
    }
    iVar4 = win_create_widget_list(FUN_ram_02012150,0);
    if (iVar4 != 0) {
      msgFuncRegister((msgDealInfor *)*param_1);
      param_1[2] = iVar4;
      goto LAB_ram_020123b8;
    }
  }
  else if (*(sbyte *)(param_1 + 3) != 0) {
LAB_ram_020123b8:
    local_38[0] = 0x22;
    local_2c = &local_28;
    winSendMsg(param_1[2],local_38);
    return param_1[2];
  }
  return 0;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02021894 Name: XOSTimeGet
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: INT32U XOSTimeGet(void)   [xos/xos.h] */

undefined4 XOSTimeGet(void)

{
  return DAT_ram_020cd700;
}



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



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02021d00 Name: icon_decode
 */

undefined4
icon_decode(int param_1,undefined *param_2,undefined1 *param_3,undefined1 *param_4,uint param_5,
           uint param_6)

{
  uint uVar1;
  undefined1 uVar2;
  uint uVar3;
  char *pcVar4;
  undefined1 *puVar5;
  undefined1 *puVar6;
  int iVar7;
  uint uVar8;
  undefined4 uVar9;
  int iVar10;
  char *pcVar11;
  uint uVar12;
  
  param_5 = param_5 & 0xffff;
  param_6 = param_6 & 0xffff;
  iVar7 = nv_open(param_1);
  uVar8 = nv_size(param_1);
  uVar9 = 1;
  if (0x35 < uVar8) {
    uVar1 = (param_5 + 1) * 3 & 0xfffffffc;
    uVar3 = uVar8 - 0x36;
    if (uVar1 * param_6 < uVar8 - 0x36) {
      uVar3 = uVar1 * param_6;
    }
    nv_read(iVar7 + 0x36,param_2,uVar3);
    pcVar4 = param_2 + uVar1 * (param_6 - 1);
    puVar5 = param_3;
    for (uVar8 = 0; puVar6 = puVar5, pcVar11 = pcVar4, uVar8 < param_6; uVar8 = uVar8 + 2) {
      while ((uint)((int)puVar6 - (int)puVar5) < param_5) {
        uVar12 = (uint)*(char *)(pcVar11[5] + 0x20cdd8c) +
                 (uint)*(char *)(pcVar11[4] + 0x20cde8c) + (uint)*(char *)(pcVar11[3] + 0x20cdf8c);
        iVar10 = (((uint)*(char *)(pcVar11[3] + 0x20cd98c) - (uint)*(char *)(pcVar11[4] + 0x20cd88c)
                  ) + 0x80) - (uint)*(char *)(pcVar11[5] + 0x20cd78c);
        iVar7 = (((uint)*(char *)(pcVar11[2] + 0x20cda8c) - (uint)*(char *)(*pcVar11 + 0x20cdc8c)) -
                (uint)*(char *)(pcVar11[1] + 0x20cdb8c)) + 0x80;
        uVar3 = (uint)*(char *)(pcVar11[2] + 0x20cdd8c) +
                (uint)*(char *)(pcVar11[1] + 0x20cde8c) + (uint)*(char *)(*pcVar11 + 0x20cdf8c);
        if (0xff < uVar3) {
          uVar3 = 0xff;
        }
        uVar2 = 0;
        *puVar6 = (sbyte)uVar3;
        if (-1 < iVar7) {
          if (0xff < iVar7) {
            iVar7 = 0xff;
          }
          uVar2 = (undefined1)iVar7;
        }
        *param_4 = uVar2;
        uVar2 = (undefined1)uVar12;
        if (0xff < uVar12) {
          uVar2 = 0xff;
        }
        puVar6[1] = uVar2;
        uVar2 = 0;
        if (-1 < iVar10) {
          if (0xff < iVar10) {
            iVar10 = 0xff;
          }
          uVar2 = (undefined1)iVar10;
        }
        param_4[1] = uVar2;
        puVar6 = puVar6 + 2;
        pcVar11 = pcVar11 + 6;
        param_4 = param_4 + 2;
      }
      puVar5 = puVar6;
      pcVar11 = pcVar4 + -uVar1;
      while ((uint)((int)puVar5 - (int)puVar6) < param_5) {
        uVar12 = (uint)*(char *)(pcVar11[5] + 0x20cdd8c) +
                 (uint)*(char *)(pcVar11[4] + 0x20cde8c) + (uint)*(char *)(pcVar11[3] + 0x20cdf8c);
        uVar3 = (uint)*(char *)(pcVar11[2] + 0x20cdd8c) +
                (uint)*(char *)(pcVar11[1] + 0x20cde8c) + (uint)*(char *)(*pcVar11 + 0x20cdf8c);
        uVar2 = (undefined1)uVar3;
        if (0xff < uVar3) {
          uVar2 = 0xff;
        }
        *puVar5 = uVar2;
        uVar2 = (undefined1)uVar12;
        if (0xff < uVar12) {
          uVar2 = 0xff;
        }
        puVar5[1] = uVar2;
        puVar5 = puVar5 + 2;
        pcVar11 = pcVar11 + 6;
      }
      pcVar4 = pcVar4 + uVar1 * -2;
    }
    ax32xx_sysDcacheFlush((uint)param_3,(int)(param_5 * param_6 * 3) >> 1);
    uVar9 = 0;
  }
  return uVar9;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02022060 Name: blit_b
 */

void blit_b(int param_1,uint param_2,uint param_3,ushort param_4,ushort param_5,uint param_6,
           ushort param_7,int param_8,ushort param_9,ushort param_10,sbyte param_11,sbyte param_12)

{
  sbyte sVar1;
  int iVar2;
  int iVar3;
  uint uVar4;
  uint uVar5;
  uint uVar6;
  uint uVar7;
  uint uVar8;
  int iVar9;
  
  iVar2 = (int)(short)(param_4 & 0xfffe);
  param_2 = param_2 & 0xffff;
  param_3 = param_3 & 0xffff;
  uVar7 = (uint)param_9;
  uVar5 = (uint)param_10;
  iVar3 = (int)(short)(param_5 & 0xfffe);
  if ((((0 < (int)(iVar2 + (param_6 & 0xfffe))) && (0 < (int)(iVar3 + (param_7 & 0xfffe)))) &&
      (iVar2 < (int)param_2)) && (iVar3 < (int)param_3)) {
    uVar4 = 0;
    if (iVar2 < 0) {
      uVar4 = -iVar2 & 0xffff;
      iVar2 = 0;
    }
    uVar6 = 0;
    if (iVar3 < 0) {
      uVar6 = -iVar3 & 0xffff;
      iVar3 = 0;
    }
    uVar8 = param_2 - iVar2;
    if ((int)(iVar2 + uVar7) <= (int)param_2) {
      uVar8 = uVar7 - uVar4;
    }
    uVar6 = uVar5 - uVar6;
    if ((int)param_3 < (int)(iVar3 + uVar5)) {
      uVar6 = param_3 - iVar3;
    }
    iVar9 = param_1 + param_2 * iVar3 + iVar2;
    param_1 = param_1 + iVar2 + param_2 * param_3 + ((int)(param_2 * iVar3) >> 1);
    iVar2 = param_8 + uVar4 + uVar7 * uVar5;
    param_8 = param_8 + uVar4;
    for (uVar5 = 0; uVar5 < (uVar6 & 0xffff); uVar5 = uVar5 + 2 & 0xffff) {
      for (uVar4 = 0; (uVar4 & 0xffff) < (uVar8 & 0xffff); uVar4 = uVar4 + 1) {
        if ((*(sbyte *)(param_8 + uVar4) != param_11) || (*(sbyte *)(iVar2 + uVar4) != param_12)) {
          *(sbyte *)(iVar9 + uVar4) = *(sbyte *)(param_8 + uVar4);
          *(sbyte *)(param_1 + uVar4) = *(sbyte *)(iVar2 + uVar4);
        }
      }
      for (uVar4 = 0; (uVar4 & 0xffff) < (uVar8 & 0xffff); uVar4 = uVar4 + 1) {
        sVar1 = *(sbyte *)(param_8 + uVar7 + uVar4);
        if ((sVar1 != param_11) || (*(sbyte *)(iVar2 + uVar4) != param_12)) {
          *(sbyte *)(iVar9 + uVar4 + param_2) = sVar1;
          *(sbyte *)(param_1 + uVar4) = *(sbyte *)(iVar2 + uVar4);
        }
      }
      iVar9 = iVar9 + param_2 * 2;
      param_1 = param_1 + param_2;
      iVar2 = iVar2 + uVar7;
      param_8 = param_8 + uVar7 * 2;
    }
  }
  return;
}



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



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020253c8 Name: frameWinCreateIndirect
 */

/* SDK prototype: winHandle frameWinCreateIndirect(widgetCreateInfor* infor,winHandle parent,winCB
   cb)   [sUI/frameWin.h] */

void frameWinCreateIndirect(int p0,undefined4 param_2,undefined4 param_3)

{
  int iVar1;
  
  iVar1 = frameWinCreate((int)*(short *)(p0 + 6),(int)*(short *)(p0 + 8),*(undefined2 *)(p0 + 10),
                         *(undefined2 *)(p0 + 0xc),param_2,*(undefined2 *)(p0 + 0xe),param_3,
                         &stack0x00000000);
  winSetfgColor(iVar1,*(undefined1 *)(p0 + 0x10));
  if (iVar1 != 0) {
    *(undefined4 *)(iVar1 + 0x84) = *(undefined4 *)(p0 + 0x24);
  }
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202566c Name: imageIconCreateIndirect
 */

/* SDK prototype: winHandle imageIconCreateIndirect(widgetCreateInfor* infor,winHandle parent,winCB
   cb)   [sUI/imageIcon.h] */

int imageIconCreateIndirect(int p0,undefined4 param_2)

{
  int iVar1;
  
  iVar1 = imageIconCreate((int)*(short *)(p0 + 6),(int)*(short *)(p0 + 8),*(undefined2 *)(p0 + 10),
                          *(undefined2 *)(p0 + 0xc),param_2,*(ushort *)(p0 + 0xe) | 0x40,
                          *(undefined2 *)(p0 + 4),&stack0x00000000);
  if (iVar1 != 0) {
    *(undefined4 *)(iVar1 + 0x38) = *(undefined4 *)(p0 + 0x14);
    *(undefined1 *)(iVar1 + 0x3c) = *(undefined1 *)(p0 + 0x18);
    winSetfgColor(iVar1,*(undefined1 *)(p0 + 0x10));
  }
  return iVar1;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202808c Name: rectCreateIndirect
 */

/* SDK prototype: winHandle rectCreateIndirect(widgetCreateInfor* infor,winHandle parent,winCB cb)
   [sUI/rect.h] */

int rectCreateIndirect(int p0,undefined4 param_2)

{
  int iVar1;
  
  iVar1 = rectCreate((int)*(short *)(p0 + 6),(int)*(short *)(p0 + 8),*(undefined2 *)(p0 + 10),
                     *(undefined2 *)(p0 + 0xc),param_2,*(ushort *)(p0 + 0xe) | 0x40,
                     *(undefined2 *)(p0 + 4),&stack0x00000000);
  if (iVar1 != 0) {
    *(undefined1 *)(iVar1 + 0x38) = *(undefined1 *)(p0 + 0x21);
    winSetfgColor(iVar1,*(undefined1 *)(p0 + 0x10));
  }
  return iVar1;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020283cc Name: FUN_ram_020283cc
 */

void FUN_ram_020283cc(int param_1,undefined4 param_2)

{
  int iVar1;
  
  iVar1 = func_0x02028310((int)*(short *)(param_1 + 6),(int)*(short *)(param_1 + 8),
                          *(undefined2 *)(param_1 + 10),*(undefined2 *)(param_1 + 0xc),param_2,
                          *(ushort *)(param_1 + 0xe) | 0x40,*(undefined2 *)(param_1 + 4),
                          &stack0x00000000);
  if (iVar1 != 0) {
    *(undefined4 *)(iVar1 + 0x38) = *(undefined4 *)(param_1 + 0x1c);
    *(undefined1 *)(iVar1 + 0x3d) = *(undefined1 *)(param_1 + 0x20);
    *(undefined1 *)(iVar1 + 0x3e) = *(undefined1 *)(param_1 + 0x21);
    *(undefined1 *)(iVar1 + 0x3c) = *(undefined1 *)(param_1 + 0x22);
    winSetfgColor(iVar1,*(undefined1 *)(param_1 + 0x10));
    *(undefined1 *)(iVar1 + 0x40) = *(undefined1 *)(param_1 + 0x29);
    *(undefined1 *)(iVar1 + 0x41) = *(undefined1 *)(param_1 + 0x28);
    *(undefined1 *)(iVar1 + 0x42) = *(undefined1 *)(param_1 + 0x2a);
    *(undefined1 *)(iVar1 + 0x43) = *(undefined1 *)(param_1 + 0x2b);
    *(undefined1 *)(iVar1 + 0x3f) = *(undefined1 *)(param_1 + 0x2d);
  }
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202984c Name: winSetVisible
 */

/* SDK prototype: void winSetVisible(winHandle hWin,bool visible)   [sUI/winManage.h] */

void winSetVisible(undefined4 param_1,sbyte param_2)

{
  bool bVar1;
  undefined4 local_20 [3];
  int local_14;
  
  local_20[0] = 0xc;
  winSendMsg(local_20);
  if (param_2 == 0) {
    bVar1 = local_14 == 0;
    local_14 = 0;
    if (bVar1) {
      return;
    }
  }
  else {
    if (local_14 != 0) {
      return;
    }
    local_14 = 1;
  }
  local_20[0] = 0xb;
  winSendMsg(param_1,local_20);
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202990c Name: winSetResid
 */

/* SDK prototype: void winSetResid(winHandle hWin,resID id)   [sUI/winManage.h] */

void winSetResid(undefined4 param_1)

{
  undefined4 local_18 [3];
  undefined4 local_c;
  
  local_18[0] = 7;
  local_c = param_1;
  winSendMsg(local_18);
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02028d78 Name: winDestroy
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void winDestroy(winHandle* hWin)   [sUI/winManage.h] */

void winDestroy(undefined *p0)

{
  uiRect *puVar1;
  uiRect *p1;
  uiRect *in_r4;
  bool bVar2;
  undefined3 extraout_var;
  undefined3 extraout_var_00;
  uiRect local_34;
  undefined4 local_28 [3];
  uiRect *local_1c;
  
  local_28[0] = 6;
  do {
    if ((-1 < (int)((uint)p0 | -(int)p0)) || (-1 < (int)(-DAT_ram_020ce0d0 | DAT_ram_020ce0d0))) {
      return;
    }
    if ((*(ushort *)(p0 + 0x1a) & 1) != 0) {
      DAT_ram_020ce0d0 = DAT_ram_020ce0d0 - 1;
      if ((*(ushort *)(p0 + 0x1a) & 4) != 0) {
        p1 = (uiRect *)(p0 + 0xc);
        if (in_r4 == (uiRect *)0x0) {
          do {
            local_1c = p1;
            winSendMsg(p0,local_28);
            p1 = *(uiRect **)(p1 + 1);
          } while (p1 != (uiRect *)0x0);
        }
        else {
          do {
            bVar2 = winContain(in_r4,p1);
            if (CONCAT31(extraout_var,bVar2) == 0) {
              puVar1 = p1;
              if (((*(ushort *)(p0 + 0x1a) & 2) == 0) &&
                 (bVar2 = winOverlapping(p1,in_r4), CONCAT31(extraout_var_00,bVar2) != 0)) {
                local_1c = &local_34;
                if (p1->x0 < in_r4->x0) {
                  local_34.x1 = in_r4->x0 + -1;
                  local_34.y0 = p1->y0;
                  local_34.y1 = p1->y1;
                  local_34.x0 = p1->x0;
                  local_1c = &local_34;
                  winSendMsg(p0,local_28);
                }
                if (p1->y0 < in_r4->y0) {
                  local_34.x0 = p1->x0;
                  if (p1->x0 < in_r4->x0) {
                    local_34.x0 = in_r4->x0;
                  }
                  local_34.x1 = p1->x1;
                  local_34.y1 = in_r4->y0 + -1;
                  local_34.y0 = p1->y0;
                  winSendMsg(p0,local_28);
                }
                if (in_r4->x1 < p1->x1) {
                  local_34.x0 = in_r4->x1 + 1;
                  local_34.y0 = p1->y0;
                  if (p1->y0 < in_r4->y0) {
                    local_34.y0 = in_r4->y0;
                  }
                  local_34.y1 = p1->y1;
                  local_34.x1 = p1->x1;
                  winSendMsg(p0,local_28);
                }
                if (p1->y1 <= in_r4->y1) goto LAB_ram_02028fc4;
                local_34.x0 = p1->x0;
                if (p1->x0 < in_r4->x0) {
                  local_34.x0 = in_r4->x0;
                }
                local_34.x1 = in_r4->x1;
                if (p1->x1 < in_r4->x1) {
                  local_34.x1 = p1->x1;
                }
                local_34.y0 = in_r4->y1 + 1;
                local_34.y1 = p1->y1;
                puVar1 = local_1c;
              }
              local_1c = puVar1;
              winSendMsg(p0,local_28);
            }
LAB_ram_02028fc4:
            p1 = *(uiRect **)(p1 + 1);
          } while (p1 != (uiRect *)0x0);
        }
      }
      winCreate(p0);
      *(ushort *)(p0 + 0x1a) = *(ushort *)(p0 + 0x1a) & 0xfffe;
    }
    p0 = *(undefined **)(p0 + 0x2c);
  } while( true );
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202a110 Name: winUpdate
 */

/* SDK prototype: void winUpdate(winHandle hWin)   [sUI/winManage.h] */

void winUpdate(undefined2 *param_1)

{
  if (param_1 != (undefined2 *)0x0) {
    if ((param_1[0xd] & 1) == 0) {
      DAT_ram_020ce0d0 = DAT_ram_020ce0d0 + 1;
    }
    param_1[6] = *param_1;
    param_1[7] = param_1[1];
    param_1[8] = param_1[2];
    param_1[9] = param_1[3];
    param_1[0xd] = param_1[0xd] | 1;
  }
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202a1d0 Name: ui_close_all_windows
 */

/* Track2 evidence-based naming (likely, names_08.tsv): Walks the window sibling chain (next pointer
   at +0x28) from the root window DAT_ram_020ce0d4 calling winDestroy on each, then flags a
   widget-pool leak when freeBlks != maxBlks or minFreeBlks == 0 (printing the memPool error text
   and dumping pool stats) and returns 1 if it ran.  [was FUN_ram_0202a1d0] */

undefined4 ui_close_all_windows(void)

{
  undefined4 uVar1;
  undefined *p0;
  
  uVar1 = 0;
  p0 = DAT_ram_020ce0d4;
  if (DAT_ram_020ce0d0 != 0) {
    while (DAT_ram_020ce0d0 != 0) {
      if (*(int *)(p0 + 0x28) == 0) {
        winDestroy(p0);
        break;
      }
      winDestroy(p0);
      p0 = *(undefined **)(p0 + 0x28);
    }
    if ((DAT_ram_020ce0c0 != DAT_ram_020ce0c4) || (DAT_ram_020ce0c8 == 0)) {
      DAT_ram_020ce0dd = 1;
    }
    if (DAT_ram_020ce0dd != 0) {
      FUN_ram_02028ab8(s_mempool_err_ram_0208069c,register0x00000401);
      uart_Printf(s_memPool_error_information__ram_020806a8);
      ui_widget_pool_dump(&DAT_ram_020ce0b8);
    }
    uVar1 = 1;
    DAT_ram_020ce0d0 = 0;
  }
  return uVar1;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02034ee4 Name: ax32xx_csiEnable
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void ax32xx_csiEnable(u8 en)   [ax32xx/inc/ax32xx_csi.h] */

void ax32xx_csiEnable(byte p0)

{
  uint uVar1;
  
  if (p0 == 0) {
    uVar1 = Spr0000b014;
    Spr0000b014 = uVar1 & 0x7fffffff;
    uVar1 = Spr0000b000;
    Spr0000b000 = uVar1 & 0xfffffffe;
  }
  else {
    uVar1 = Spr0000b014;
    Spr0000b014 = uVar1 | 0x80000000;
    uVar1 = Spr0000b000;
    Spr0000b000 = uVar1 | 1;
  }
  ax32xx_intEnable(0xe,p0 != 0);
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0203a5c8 Name: display_get_mode
 */

/* SDK align: src_hal_lcd.c.o/hal_lcdGetResolution score=1.0 segment */

undefined4 display_get_mode(undefined2 *param_1,undefined2 *param_2)

{
  int iVar1;
  undefined2 uVar2;
  
  iVar1 = DAT_ram_020ce980;
  if (DAT_ram_020ce980 == 0) {
    return 0xffffffff;
  }
  if (DAT_ram_020ce961 == 0) {
    if (param_1 != (undefined2 *)0x0) {
      *param_1 = *(undefined2 *)(DAT_ram_020ce980 + 0x34);
    }
    if (param_2 == (undefined2 *)0x0) {
      return 0;
    }
    uVar2 = *(undefined2 *)(iVar1 + 0x36);
  }
  else {
    if (param_1 != (undefined2 *)0x0) {
      *param_1 = *(undefined2 *)(DAT_ram_020ce980 + 0x36);
    }
    if (param_2 == (undefined2 *)0x0) {
      return 0;
    }
    uVar2 = *(undefined2 *)(iVar1 + 0x34);
  }
  *param_2 = uVar2;
  return 0;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0203e5c4 Name: hal_sysMemMalloc
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void *hal_sysMemMalloc(u32 size,u8 algin)   [hal/inc/hal_sys.h] */

undefined * hal_sysMemMalloc(uint p0,byte p1)

{
  int *piVar1;
  uint uVar2;
  undefined3 in_register_00001011;
  uint uVar3;
  undefined4 *puVar4;
  undefined *puVar5;
  
  puVar4 = (undefined4 *)hal_memNodeMalloc(CONCAT31(in_register_00001011,p1),&stack0x00000000);
  if (puVar4 != (undefined4 *)0x0) {
    uVar2 = p0 + 0x3f & 0xffffffc0;
    for (piVar1 = DAT_ram_020cec54; piVar1 != (int *)0x0; piVar1 = (int *)piVar1[3]) {
      if ((*piVar1 == 0) && (uVar3 = piVar1[2], uVar2 <= uVar3)) {
        puVar5 = (undefined *)piVar1[1];
        *puVar4 = 0;
        puVar4[2] = uVar3 - uVar2;
        puVar4[1] = puVar5 + uVar2;
        puVar4[3] = piVar1[3];
        piVar1[3] = (int)puVar4;
        piVar1[2] = uVar2;
        *piVar1 = 1;
        return puVar5;
      }
    }
    *puVar4 = 0;
    puVar4[3] = DAT_ram_020cec58;
    DAT_ram_020cec58 = puVar4;
    hal_sysMemPrint();
  }
  return (undefined *)0x0;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0203e694 Name: hal_sysMemFree
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void hal_sysMemFree(void *mem)   [hal/inc/hal_sys.h] */

void hal_sysMemFree(undefined *p0)

{
  int *piVar1;
  int *piVar2;
  int *piVar3;
  
  if (p0 != (undefined *)0x0) {
    piVar1 = DAT_ram_020cec54;
    piVar3 = (int *)0x0;
    while (piVar2 = piVar1, piVar2 != (int *)0x0) {
      if ((*piVar2 != 0) && ((undefined *)piVar2[1] == p0)) {
        piVar1 = (int *)piVar2[3];
        *piVar2 = 0;
        if ((piVar1 != (int *)0x0) && (*piVar1 == 0)) {
          piVar2[2] = piVar2[2] + piVar1[2];
          piVar2[3] = piVar1[3];
          *piVar1 = 0;
          piVar1[3] = (int)DAT_ram_020cec58;
          DAT_ram_020cec58 = piVar1;
        }
        if (piVar3 == (int *)0x0) {
          return;
        }
        if (*piVar3 != 0) {
          return;
        }
        piVar3[2] = piVar3[2] + piVar2[2];
        piVar3[3] = piVar2[3];
        *piVar2 = 0;
        piVar2[3] = (int)DAT_ram_020cec58;
        DAT_ram_020cec58 = piVar2;
        return;
      }
      piVar3 = piVar2;
      piVar1 = (int *)piVar2[3];
    }
  }
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02044ce0 Name: hal_dispframeVideoCfg
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void hal_dispframeVideoCfg(disp_frame_t* p_frame,u16 posX,u16 posY,u16 width,u16
   height)   [hal/inc/hal_lcdframe.h] */

void hal_dispframeVideoCfg(undefined *p0,ushort p1,ushort p2,ushort p3,ushort p4)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  uint uVar4;
  uint uVar5;
  uint uVar6;
  uint uVar7;
  uint uVar8;
  uint uVar9;
  ushort local_38;
  ushort local_36;
  int local_34;
  int local_30;
  
  uVar5 = (uint)p1;
  uVar2 = Spr00000018;
  uVar9 = Spr00000018;
  uVar8 = (uint)p2;
  Spr00000018 = uVar9 & 0xfffffffd;
  uVar9 = (uint)p3;
  uVar1 = (uint)p4;
  if (p0[0x2e] == 0) {
    if ((((*(ushort *)(p0 + 0x24) != uVar5) || (*(ushort *)(p0 + 0x26) != uVar8)) ||
        (*(ushort *)(p0 + 0x20) != uVar9)) || (*(ushort *)(p0 + 0x22) != uVar1)) {
      uVar5 = uVar9 + 0x1f & 0xffffffe0;
      *(ushort *)(p0 + 0x20) = p3;
      iVar3 = uVar5 * uVar1;
      *(ushort *)(p0 + 0x22) = p4;
      *(int *)(p0 + 8) = *(int *)(p0 + 4) + iVar3;
      *(ushort *)(p0 + 0x24) = p1;
      *(ushort *)(p0 + 0x26) = p2;
      *(short *)(p0 + 0x28) = (short)uVar5;
      *(uint *)(p0 + 0x18) = (uint)(iVar3 * 3) >> 1;
    }
    goto LAB_ram_02044f20;
  }
  if (p0[0x2e] != 1) goto LAB_ram_02044f20;
  local_38 = 0;
  local_36 = 0;
  uVar6 = hal_lcdGetShowingRotate();
  uVar6 = uVar6 & 0xff;
  display_get_mode(&local_38,&local_36);
  uVar4 = uVar8;
  uVar7 = uVar5;
  if (uVar6 != 0) {
    if (uVar6 == 4) {
      uVar4 = (local_38 - uVar5) - uVar9;
    }
    else {
      if (uVar6 == 5) {
        uVar7 = (local_36 - uVar8) - uVar1 & 0xffff;
        uVar4 = uVar5;
        goto LAB_ram_02044e48;
      }
      uVar4 = (local_36 - uVar8) - uVar1;
      uVar8 = (local_38 - uVar5) - uVar9 & 0xffff;
    }
    uVar4 = uVar4 & 0xffff;
    uVar7 = uVar8;
  }
LAB_ram_02044e48:
  uVar5 = uVar1;
  if (uVar6 - 4 < 2) {
    uVar5 = uVar9;
    uVar9 = uVar1;
    if (uVar6 == 4) {
      uVar6 = 0;
    }
    else {
      uVar6 = 3;
    }
  }
  if (((*(ushort *)(p0 + 0x24) != uVar7) || (*(ushort *)(p0 + 0x26) != uVar4)) ||
     ((*(ushort *)(p0 + 0x20) != uVar9 || (*(ushort *)(p0 + 0x22) != uVar5)))) {
    uVar8 = uVar9 + 0x1f & 0xffffffe0;
    hal_lcdAddrCalculate(uVar9,uVar5,uVar6,uVar8,&local_34,&local_30);
    iVar3 = *(int *)(p0 + 4) + uVar8 * uVar5;
    *(int *)(p0 + 8) = iVar3;
    *(int *)(p0 + 0xc) = *(int *)(p0 + 4) + local_34;
    *(int *)(p0 + 0x10) = iVar3 + local_30;
    *(short *)(p0 + 0x20) = (short)uVar9;
    *(short *)(p0 + 0x22) = (short)uVar5;
    *(short *)(p0 + 0x24) = (short)uVar7;
    *(short *)(p0 + 0x26) = (short)uVar4;
    *(uint *)(p0 + 0x18) = uVar8 * uVar5 * 3 >> 1;
    *(short *)(p0 + 0x28) = (short)uVar8;
    p0[0x2a] = (sbyte)uVar6;
  }
LAB_ram_02044f20:
  uVar5 = Spr00000018;
  Spr00000018 = uVar2 & 2 | uVar5;
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0205b8c4 Name: video_present
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* LCD present / buffer blit */

void video_present(sbyte param_1)

{
  uint p0;
  int iVar1;
  undefined *p0_00;
  int local_18;
  
  boardIoctrl((int)(sbyte)app_context.b13,0x20000013,&local_18);
  if (local_18 == 0) {
    p0 = FUN_ram_02004840(1);
    memset(p0,0xfa,(uint)_DAT_ram_020d2ef6 * (uint)_DAT_ram_020d2ef4);
    ax32xx_sysDcacheWback(p0,(uint)_DAT_ram_020d2ef6 * (uint)_DAT_ram_020d2ef4);
    FUN_ram_020048f8(1,p0);
    ax32xx_deUpdate();
  }
  else {
    iVar1 = ui_close_all_windows();
    if (iVar1 == 0) {
      if (((param_1 == 0) && (app_context.b66 == 0)) && (app_context.b68 == 0)) {
        return;
      }
      app_context.b66 = 0;
    }
    ui_invoke_key_13();
    if (DAT_ram_020d2ef8 != 0) {
      p0_00 = (undefined *)FUN_ram_02004840(1);
      ax32xx_sys_dma_memkick
                (p0_00,_DAT_ram_020d2efc,(uint)_DAT_ram_020d2ef6 * (uint)_DAT_ram_020d2ef4);
      if (app_context.b68 != 0) {
        app_context.b68 = 0;
        app_context.b69 = 0;
        app_context.b66 = 1;
      }
      FUN_ram_020048f8(1,p0_00);
    }
    ax32xx_deUpdate();
    if (app_context.b68 != 0) {
      XOSTimeDly(0x1e);
    }
  }
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020782b0 Name: memcpy
 */

/* SDK align: lib_a-memcpy.o/memcpy score=1.0 segment */

undefined4 * memcpy(undefined4 *param_1,undefined4 *param_2,uint param_3)

{
  undefined4 *puVar1;
  undefined4 *puVar2;
  undefined4 *puVar3;
  undefined4 *puVar4;
  int iVar5;
  uint uVar6;
  undefined4 *puVar7;
  undefined4 *puVar8;
  undefined4 *puVar9;
  undefined4 *puVar10;
  undefined4 *puVar11;
  uint uVar12;
  
  uVar12 = param_3;
  puVar2 = param_1;
  if (0xf < param_3) {
    if ((((uint)param_1 | (uint)param_2) & 3) != 0) goto LAB_ram_020783b8;
    uVar12 = param_3 - 0x10 >> 4;
    puVar11 = param_1 + 1;
    puVar10 = param_2 + 2;
    puVar9 = param_1 + 2;
    puVar8 = param_2 + 3;
    puVar7 = param_1 + 3;
    puVar2 = param_2 + 1;
    puVar3 = param_1;
    puVar4 = param_2;
    do {
      puVar1 = puVar2 + 4;
      *puVar3 = *puVar4;
      puVar4 = puVar4 + 4;
      *puVar11 = *puVar2;
      puVar3 = puVar3 + 4;
      puVar11 = puVar11 + 4;
      *puVar9 = *puVar10;
      puVar10 = puVar10 + 4;
      puVar9 = puVar9 + 4;
      *puVar7 = *puVar8;
      puVar8 = puVar8 + 4;
      puVar7 = puVar7 + 4;
      puVar2 = puVar1;
    } while (puVar1 != param_2 + uVar12 * 4 + 5);
    iVar5 = uVar12 + 1;
    uVar12 = param_3 & 0xf;
    puVar2 = param_1 + iVar5 * 4;
    param_2 = param_2 + iVar5 * 4;
    uVar6 = uVar12;
    puVar3 = param_2;
    puVar4 = puVar2;
    if (3 < uVar12) {
      do {
        uVar6 = uVar6 - 4;
        *puVar4 = *puVar3;
        puVar3 = puVar3 + 1;
        puVar4 = puVar4 + 1;
      } while (3 < uVar6);
      iVar5 = (uVar12 - 4 & 0xfffffffc) + 4;
      param_2 = (undefined4 *)((int)param_2 + iVar5);
      uVar12 = param_3 & 3;
      puVar2 = (undefined4 *)((int)puVar2 + iVar5);
    }
  }
  param_3 = uVar12;
  if (param_3 == 0) {
    return param_1;
  }
LAB_ram_020783b8:
  puVar3 = puVar2;
  do {
    puVar4 = (undefined4 *)((int)puVar3 + 1);
    *(undefined1 *)puVar3 = *(undefined1 *)param_2;
    param_2 = (undefined4 *)((int)param_2 + 1);
    puVar3 = puVar4;
  } while (puVar4 != (undefined4 *)((int)puVar2 + param_3));
  return param_1;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0205b408 Name: nv_open
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: int nv_open(int res_num)   [bwlib/nvfs/nvfs.h] */

int nv_open(int p0)

{
  int iVar1;
  
  iVar1 = -1;
  if (_DAT_ram_020d2edc != 0) {
    if (_DAT_ram_020d2ee8 == p0) {
      iVar1 = _DAT_ram_020d2edc + _DAT_ram_020d2eec;
    }
    else {
      iVar1 = -1;
      if ((uint)(p0 * 8) < _DAT_ram_020d2ee4) {
        hal_spiFlashRead(p0 * 8 + _DAT_ram_020d2edc,0x20d2eec,8);
        iVar1 = _DAT_ram_020d2edc + _DAT_ram_020d2eec;
        _DAT_ram_020d2ee8 = p0;
      }
    }
  }
  return iVar1;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0205b49c Name: nv_size
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: int nv_size(int res_num)   [bwlib/nvfs/nvfs.h] */

int nv_size(int p0)

{
  int iVar1;
  
  iVar1 = -1;
  if ((_DAT_ram_020d2edc != 0) && (iVar1 = _DAT_ram_020d2ef0, _DAT_ram_020d2ee8 != p0)) {
    iVar1 = -1;
    if ((uint)(p0 * 8) < _DAT_ram_020d2ee4) {
      hal_spiFlashRead(p0 * 8 + _DAT_ram_020d2edc,0x20d2eec,8);
      iVar1 = _DAT_ram_020d2ef0;
      _DAT_ram_020d2ee8 = p0;
    }
  }
  return iVar1;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02067240 Name: R_loadResource
 */

/* SDK prototype: extern void R_loadResource(void *res,unsigned long int length)
   [bwlib/Maxlib/res/R.h] */

void R_loadResource(uint *p0)

{
  if (p0 != (uint *)0x0) {
    if ((*p0 & 0xff000000) == 0x80000000) {
      R_iconLoadResource();
    }
    else if ((*p0 & 0xff000000) == 0x81000000) {
      R_stringLoadResource(&stack0x00000000);
    }
  }
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202b0d4 Name: ax32xx_sysDcacheFlush
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void ax32xx_sysDcacheFlush(u32 addr, u32 size)   [ax32xx/inc/ax32xx_sys.h] */

void ax32xx_sysDcacheFlush(uint p0,uint p1)

{
  uint uVar1;
  
  if (p0 - 0x44000 < 0x7ffbc000) {
    while (p1 != 0) {
      uVar1 = p1;
      if (0x1000 < p1) {
        uVar1 = 0x1000;
      }
      p1 = p1 - uVar1;
      Spr0000181c = ((uVar1 + p0) - (p0 & 0xfffffff0)) - 1 >> 4;
      Spr00001814 = p0;
      p0 = uVar1 + p0;
    }
  }
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200d2c0 Name: system_tick
 */

/* 10 ms gated tasks (incl. kc02_keypad_poll) + usb/charger tick */

void system_tick(int param_1)

{
  int iVar1;
  
  timerMsgSend(&stack0x00000000);
  hal_ispService();
  if (param_1 != 0) {
    kc02_keypad_poll();
  }
  iVar1 = XOSTimeGet();
  if (10 < (uint)(iVar1 - DAT_ram_020ccde8)) {
    DAT_ram_020ccde8 = XOSTimeGet();
    iVar1 = ui_mode_get_current();
    if (iVar1 != 1) {
      tick_sd_detect();
      tick_lcd_timeout(0);
      tick_auto_poweroff(0);
    }
    tick_usb_charger();
  }
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02007880 Name: msgFuncRegister
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void msgFuncRegister(msgDealInfor* infor)   [ax32_platform_demo/application.h] */

void msgFuncRegister(msgDealInfor *p0)

{
  int iVar1;
  undefined4 *puVar2;
  kc02_task_map_entry *pkVar3;
  
  iVar1 = 0;
  do {
    puVar2 = (undefined4 *)((int)&DAT_ram_020ccc8c + iVar1);
    iVar1 = iVar1 + 4;
    *puVar2 = 0;
  } while (iVar1 != 0xb8);
  pkVar3 = task_map_builtin;
  while (pkVar3 = pkVar3 + 1, pkVar3->msg < 0x2e) {
    if (pkVar3->handler == (undefined *)0x0) break;
    (&DAT_ram_020ccc8c)[pkVar3->msg] = pkVar3->handler;
  }
  if (p0 != (msgDealInfor *)0x0) {
    while (p0->msgId < 0x2e) {
      if (p0->func == (undefined *)0x0) {
        return;
      }
      (&DAT_ram_020ccc8c)[p0->msgId] = p0->func;
      p0 = p0 + 1;
    }
  }
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200c698 Name: timerMsgSend
 */

/* SDK align: taskSysScanDevice.c.o/timerMsgSend score=1.0 segment */

void timerMsgSend(void)

{
  undefined *puVar1;
  int iVar2;
  
  puVar1 = hal_rtcTimeGet();
  if (UNK_ram_020c3350 != puVar1[6]) {
    app_context._40_4_ = app_context._40_4_ + 1;
    UNK_ram_020c3350 = puVar1[6];
    XMsgQPost((undefined *)app_context._116_4_,(undefined *)0xa);
    taskVideoRecTimeIncreace(register0x00000401);
    resBuffConut();
  }
  iVar2 = XOSTimeGet();
  if (500 < (uint)(iVar2 - UNK_ram_020ccdec)) {
    UNK_ram_020ccdec = XOSTimeGet();
    XMsgQPost((undefined *)app_context._116_4_,(undefined *)0x9);
  }
  return;
}



/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202a1b0 Name: ui_get_ctx
 */

/* SDK align: sUI_winManage.c.o/winGetCurrent score=1.0 segment */

undefined4 ui_get_ctx(void)

{
  return DAT_ram_020ce0d8;
}

