/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200a4bc Name: ui_task_print_image
 */

/* ui_task_print_image - UI task (menu-mode msgs 0x1A/0x23): encodes the frame via 0x020087E8
   (watermark-aware JPEG encoder) then kc02_print_image(0x0205B0FC). */

undefined4 ui_task_print_image(undefined4 param_1,int param_2,int *param_3)

{
  uint uVar1;
  undefined4 uVar2;
  sbyte sVar3;
  int iVar4;
  int iVar5;
  char *pcVar6;
  short sVar7;
  undefined *puStack_34;
  int iStack_30;
  
  if (param_2 != 1) {
    return 0;
  }
  if (*param_3 != 0) {
    return 0;
  }
  sVar7 = 1;
  if (app_context.b59 != 0) goto LAB_ram_0200a600;
  uVar1 = (uint)app_context.b5b;
  if ((uVar1 - 7 < 2) || ((int)(uVar1 - 1) < 0)) {
    sVar7 = 1;
    goto LAB_ram_0200a600;
  }
  if (uVar1 == 1) {
    sVar7 = 3;
    goto LAB_ram_0200a600;
  }
  if (uVar1 == 2) {
    sVar7 = 5;
    goto LAB_ram_0200a600;
  }
  if (uVar1 != 3) {
    if (uVar1 == 4) {
      uVar1 = 5;
    }
    else {
      if (uVar1 != 5) {
        if (uVar1 != 6) goto LAB_ram_0200a600;
        uVar1 = 5;
        sVar7 = 3;
        goto LAB_ram_0200a588;
      }
      uVar1 = 10;
    }
  }
  sVar7 = 1;
LAB_ram_0200a588:
  iVar4 = XOSTimeGet();
  XOSTimeDly(100);
  sys_irq_clear();
  FUN_ram_02009358(param_1,1,uVar1);
  do {
    sys_irq_clear();
    iVar5 = XOSTimeGet();
    if (999 < (uint)(iVar5 - iVar4)) {
      uVar1 = uVar1 - 1 & 0xffff;
      FUN_ram_02009358(param_1,1,uVar1);
      if (uVar1 == 0) break;
      iVar4 = XOSTimeGet();
    }
    mode_photo_tick(0);
  } while( true );
  FUN_ram_02009358(param_1,0,0);
LAB_ram_0200a600:
  do {
    while( true ) {
      sVar7 = sVar7 + -1;
      if (sVar7 == -1) {
        FUN_ram_0200917c(param_1);
        return 0;
      }
      mode_photo_tick(0);
      if (1 < app_context.b4 - 3) break;
      boardIoctrl((int)(sbyte)app_context.b11,0x2000000a,0);
      if (iRam020ccd98 == 1) {
        boardIoctrl((int)(sbyte)app_context.b10,0x20000011);
      }
      if (((app_context.b59 == 0) && (app_context.b5b - 7 < 2)) &&
         (iVar4 = FUN_ram_02022720(), iVar4 != 0)) {
        photo_effect_edge_detect
                  (*(undefined4 *)(iVar4 + 4),*(undefined4 *)(iVar4 + 8),
                   *(undefined2 *)(iVar4 + 0x20),*(undefined2 *)(iVar4 + 0x22),
                   -(app_context.b5b ^ 7) >> 0x1f);
        hal_lcdDealBuffer(iVar4);
      }
      ax32xx_csiEnable(0);
      dispLayerUninit(0);
      iVar4 = FUN_ram_02008630();
      audio_playback_nv(0x2c,1,0);
      while (iVar5 = audioPlaybackGetStatus(), iVar5 == 1) {
        XOSTimeDly(10);
      }
      ax32xx_csiEnable(1);
      dispLayerInit(0);
      boardIoctrl((int)(sbyte)app_context.b11,0x2000000a,1);
      if (iRam020ccd98 == 1) {
        boardIoctrl((int)(sbyte)app_context.b10,0x20000011,0);
      }
      if (iVar4 == 0) {
        sys_irq_clear();
        ax32xx_csiEnable(0);
        ui_state_apply(7);
        if (app_context.b5f == 0) {
          XOSTimeDly(1000);
        }
        else {
          iVar4 = work_service(app_context._60_4_);
          uVar2 = app_context._60_4_;
          if (0 < iVar4) {
            iVar4 = work_service(app_context._60_4_);
            pcVar6 = manangerGetFileFullPathName(uVar2,iVar4,&iStack_30);
            if ((pcVar6 != (char *)0x0) && (iVar4 = open(pcVar6,1), -1 < iVar4)) {
              close(iVar4);
              FUN_ram_02009c1c(param_1,0x8100007f,0x81000056);
              video_present(1);
              iVar4 = open(pcVar6,1);
              if (-1 < iVar4) {
                sVar3 = kc02_print_file(iVar4,app_context.b61,app_context.b60,app_context.b0);
                if (sVar3 == 8) {
                  uVar2 = 0x81000057;
LAB_ram_0200a85c:
                  FUN_ram_02009c1c(param_1,0x8100007f,uVar2);
                  video_present(1);
                  XOSTimeDly(2000);
                }
                else {
                  if (sVar3 == 9) {
                    uVar2 = 0x81000058;
                    goto LAB_ram_0200a85c;
                  }
                  if (sVar3 == 0xe) {
                    uVar2 = 0x810000bb;
                    goto LAB_ram_0200a85c;
                  }
                }
                close(iVar4);
                FUN_ram_02009ccc(param_1,0,0);
                video_present(1);
              }
            }
          }
        }
        sys_irq_clear();
        ax32xx_csiEnable(1);
        ui_state_apply(app_context.b7);
      }
    }
    if (app_context.b5f != 0) {
      if (iRam020ccd98 == 1) {
        boardIoctrl((int)(sbyte)app_context.b10,0x20000011);
      }
      boardIoctrl((int)(sbyte)app_context.b11,0x2000000a,0);
      iVar4 = photo_encode_frame(&puStack_34,&iStack_30);
      boardIoctrl((int)(sbyte)app_context.b11,0x2000000a,1);
      if (iRam020ccd98 == 1) {
        boardIoctrl((int)(sbyte)app_context.b10,0x20000011,0);
      }
      if (iVar4 == 0) {
        FUN_ram_02009c1c(param_1,0x8100007f,0x81000056);
        video_present(1);
        XOSTimeDly(1000);
        sVar3 = kc02_print_image(puStack_34,app_context.b61,app_context.b60,app_context.b0);
        if (sVar3 == 8) {
          uVar2 = 0x81000057;
LAB_ram_0200a9bc:
          FUN_ram_02009c1c(param_1,0x8100007f,uVar2);
          video_present(1);
          XOSTimeDly(2000);
        }
        else {
          if (sVar3 == 9) {
            uVar2 = 0x81000058;
            goto LAB_ram_0200a9bc;
          }
          if (sVar3 == 0xe) {
            uVar2 = 0x810000bb;
            goto LAB_ram_0200a9bc;
          }
        }
        hal_sysMemFree(puStack_34);
        FUN_ram_02009ccc(param_1,0,0);
        video_present(1);
      }
      goto LAB_ram_0200a600;
    }
    XMsgQPost((undefined *)app_context._116_4_,(undefined *)0x1);
  } while( true );
}

