/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02076f98 Name: FUN_ram_02076f98
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* WARNING: Restarted to delay deadcode elimination for space: ram */

int FUN_ram_02076f98(int param_1,undefined4 param_2,uint param_3,uint param_4,undefined1 param_5,
                    sbyte param_6,undefined1 param_7,undefined2 param_8,undefined2 param_9,
                    undefined1 param_10,char param_11)

{
  undefined1 uVar1;
  uint uVar2;
  char *pcVar3;
  undefined4 uVar4;
  undefined *puVar5;
  uint uVar6;
  undefined1 *puVar7;
  uint uVar8;
  uint uVar9;
  int iVar10;
  uint uVar11;
  int unaff_r14;
  uint unaff_r18;
  int iVar12;
  undefined1 *puVar13;
  undefined1 *puVar14;
  uint p2;
  uint uStack_1f8;
  undefined *puStack_1e8;
  int iStack_1e4;
  undefined1 auStack_1d2 [2];
  undefined1 auStack_1d0 [12];
  undefined4 auStack_1c4 [4];
  undefined4 auStack_1b4 [4];
  undefined4 auStack_1a4 [8];
  undefined4 uStack_184;
  int iStack_180;
  int iStack_17c;
  undefined2 uStack_178;
  undefined4 uStack_174;
  ushort uStack_170;
  ushort uStack_16e;
  undefined1 auStack_164 [2];
  undefined1 auStack_162 [50];
  undefined1 auStack_130 [2];
  undefined1 auStack_12e [50];
  undefined1 auStack_fc [208];
  
  param_3 = param_3 & 0xffff;
  uVar2 = (uint)param_11;
  param_4 = param_4 & 0xffff;
  memcpy(auStack_1d0,0x20c205c,0xc,uVar2);
  memset(auStack_1c4,0,0x10);
  memset(auStack_1a4,0,0x20);
  p2 = param_3 >> 1;
  func_0x02040478(p2,param_4 >> 1);
  uRam020d40ec = 0x2800;
  uRam020d40f0 = 0;
  uRam020d40e8 = param_2;
  puRam020d40e4 = hal_sysMemMalloc(0x2800,0x40);
  if (puRam020d40e4 == (undefined *)0x0) {
    iVar12 = -9;
    uart_Printf(s_mallloc_w_sd_memory_error_ram_020c2034);
  }
  else {
    iVar12 = hal_mjpegMemInit();
    if (iVar12 < 0) {
      uart_Printf(s_image_encode__malloc_fail_ram_020c1ec8);
    }
    else {
      puVar14 = auStack_1d0;
      iVar12 = func_0x020793e8(param_1,0x2083acc);
      puVar7 = auStack_fc;
      iVar12 = (iVar12 + 2) - param_1;
      puVar13 = puVar7 + iVar12;
      while (puVar7 != &stack0xffffffd4) {
        iVar10 = strcpy(puVar7,param_1);
        uVar1 = puVar14[1];
        *puVar13 = *puVar14;
        puVar13[1] = uVar1;
        strcpy(iVar10 + 0x34,param_1);
        uVar1 = puVar14[4];
        puVar13[0x34] = puVar14[3];
        puVar13[0x35] = uVar1;
        puVar14 = puVar14 + 6;
        puVar13 = puVar13 + 0x68;
        puVar7 = (undefined1 *)(iVar10 + 0x68);
      }
      strcpy(auStack_164,param_1);
      auStack_162[iVar12 + -2] = 0x54;
      auStack_162[iVar12 + -1] = 0x48;
      strcpy(auStack_130,param_1);
      auStack_12e[iVar12 + -2] = 0x55;
      auStack_12e[iVar12 + -1] = 0x56;
      unaff_r18 = open(auStack_164,7);
      if ((int)unaff_r18 < 0) {
LAB_ram_02077c1c:
        pcVar3 = s_image_encode__open_thumbfile_fil_ram_020c1ee3;
LAB_ram_02077c54:
        uart_Printf(pcVar3);
        iVar12 = -3;
      }
      else {
        uart_Printf(s_SysCtrl_crop_level__d_mjpegEncCt_ram_020c1f0b,(uint)app_context.b8,
                    (uint)DAT_ram_020d41fa);
        func_0x02075614(param_5,param_7,param_6,0);
        iVar12 = XOSTimeGet();
        do {
          iVar10 = func_0x02042d70();
          if (iVar10 != 0) {
            sys_irq_clear();
            ax32xx_csiEnable(0);
            ui_state_apply(7);
            if (app_context.b6 != 0) {
              audio_play_sync(0x33);
            }
            uVar11 = (uint)DAT_ram_020d41fa;
            uart_Printf(s_SysCtrl_crop_level__d_mjpegEncCt_ram_020c1f0b + 0x16,uVar11);
            uVar9 = (uint)app_context.b8;
            uVar8 = uVar2;
            if ((uVar9 == 0) && (func_0x02042d90(0), uVar8 = uVar2, param_6 != 0)) {
              func_0x02077ef0(_DAT_ram_020d4210,param_8,param_9,param_10,uVar9,uVar2);
              uVar11 = uVar2;
            }
            ax32xx_mjpeg_manual_on();
            ax32xx_intEnable(0x10,1);
            uart_Printf(s_________wt______UPRIGHT_4________ram_020c1f40,uVar11);
            iVar12 = XOSTimeGet();
            uVar2 = iVar12 + 2000;
            goto LAB_ram_02077330;
          }
          uVar11 = XOSTimeGet();
        } while (uVar11 <= iVar12 + 0x5dcU);
        iVar12 = -2;
      }
    }
  }
  goto LAB_ram_02077098;
LAB_ram_020774e4:
  iVar12 = (uint)(char)sRam020cf62c * 0xc;
  if (*(int *)(iVar12 + 0x20d4110) == 0) goto LAB_ram_02077554;
  iVar12 = write(unaff_r18,(undefined *)(&uRam020d4108)[(uint)(char)sRam020cf62c * 3],
                 *(uint *)(iVar12 + 0x20d410c));
  if (iVar12 < 0) {
LAB_ram_0207752c:
    pcVar3 = s____thum_error____ram_020c1f64;
  }
  else {
    uart_Printf(s____image_OK_____ram_020c1f77);
    sRam020cf62c = sRam020cf62c + 1;
LAB_ram_02077574:
    iVar12 = configGet(0x18);
    if ((iVar12 == -0x7effff64) || (iVar12 = configGet(0x18), iVar12 == -0x7effff66)) {
      unaff_r14 = 6;
    }
    else {
      iVar12 = configGet(0x18);
      if (((iVar12 == -0x7effff65) || (iVar12 = configGet(0x18), iVar12 == -0x7effff67)) ||
         (iVar12 = configGet(0x18), iVar12 == -0x7effff68)) {
        unaff_r14 = 0xc;
      }
      else {
        iVar12 = configGet(0x18);
        if (((iVar12 == -0x7effff69) || (iVar12 = configGet(0x18), iVar12 == -0x7effff6a)) ||
           (iVar12 = configGet(0x18), iVar12 == -0x7effff6b)) {
          unaff_r14 = 0x18;
        }
        else {
          iVar12 = configGet(0x18);
          if (((iVar12 == -0x7effff6c) || (iVar12 = configGet(0x18), iVar12 == -0x7effff6d)) ||
             (iVar12 = configGet(0x18), iVar12 == -0x7effff6e)) {
            unaff_r14 = 0x24;
          }
          else {
            iVar12 = configGet(0x18);
            if ((iVar12 == -0x7effff6f) || (iVar12 = configGet(0x18), iVar12 == -0x7effff70)) {
              unaff_r14 = 0x50;
            }
            else {
              iVar12 = configGet(0x18);
              if (iVar12 == -0x7effff71) {
                unaff_r14 = 0xf0;
              }
            }
          }
        }
      }
    }
    iVar12 = configGet(0x18);
    if ((iVar12 == -0x7effff6f) || (iVar12 = configGet(0x18), iVar12 == -0x7effff70)) {
      uStack_1f8 = 1;
    }
    else {
      uVar2 = configGet(0x18);
      uStack_1f8 = ((-(uVar2 ^ 0x8100008f) | uVar2 ^ 0x8100008f) ^ 0xffffffff) >> 0x1f;
    }
    iStack_1e4 = open(auStack_130,7);
    iVar12 = XOSTimeGet();
    if (uStack_1f8 == 0) {
      func_0x02042cd4((&uRam020d4108)[((char)sRam020cf62c - 1) * 3]);
      puStack_1e8 = hal_dispframeMalloc(1);
      func_0x02076174(iStack_1e4,unaff_r14);
      iVar10 = param_4 * 2;
    }
    else {
      func_0x02042cd4((&uRam020d4108)[((char)sRam020cf62c - 1) * 3]);
      puStack_1e8 = hal_dispframeMalloc(1);
      func_0x0207645c(iStack_1e4,unaff_r14);
      p2 = param_3 >> 2;
      iVar10 = param_4 << 2;
    }
    uVar11 = iVar10 / unaff_r14 & 0xfffe;
    iVar10 = XOSTimeGet();
    uVar2 = 0;
    uart_Printf(s_________YUV__d______________ram_020c1fb9,iVar10 - iVar12);
    iVar12 = XOSTimeGet();
    if (uStack_1f8 == 0) {
      iVar12 = XOSTimeGet();
      uart_Printf(s_width1__d_height1__d_ram_020c1fd7,p2,uVar11,iVar12);
      uVar2 = 0;
      do {
        if (unaff_r14 <= (int)uVar2) goto LAB_ram_02077be0;
        uVar8 = uVar2 & 1;
        puVar5 = _DAT_ram_020d4238;
        uVar4 = _DAT_ram_020d423c;
        if (uVar8 == 0) {
LAB_ram_02077a98:
          func_0x02042ae4(puVar5,uVar4);
        }
        else if (uVar8 == 1) {
          puVar5 = *(undefined **)(puStack_1e8 + 4);
          uVar4 = uRam020c206c;
          goto LAB_ram_02077a98;
        }
        ax32xx_mjpegEncodeInit(true,true);
        ax32xx_mjpegEncodeSizeSet
                  ((uint)(_DAT_ram_020d4204 >> 1),(int)((uint)_DAT_ram_020d4206 * 2) / unaff_r14,p2,
                   uVar11);
        if ((param_6 == 1) && (uVar2 == unaff_r14 - 2U)) {
          video_watermark_update(p2,uVar11,0);
        }
        _DAT_ram_020d4248 = uRam020d41e8;
        _DAT_ram_020d4250 = iRam020d41ec;
        iVar10 = (int)((uint)_DAT_ram_020d4206 * 2) / unaff_r14;
        ax32xx_mjpeg_inlinebuf_init
                  ((byte *)(_DAT_ram_020d4210 + uVar2 * (_DAT_ram_020d4204 >> 1) * iVar10),
                   (byte *)(_DAT_ram_020d4214 + uVar2 * (_DAT_ram_020d4204 >> 2) * iVar10));
        ax32xx_mjpegEncodeBufferSet(_DAT_ram_020d4248,_DAT_ram_020d4248 + _DAT_ram_020d4250);
        ax32xx_mjpeg_manual_on();
        ax32xx_intEnable(0x10,1);
        iVar10 = XOSTimeGet();
        uVar9 = iVar10 + 2000;
        while( true ) {
          uVar6 = (uint)(char)sRam020cf62c;
          iVar10 = uVar6 * 0xc;
          if (*(int *)(iVar10 + 0x20d4110) != 0) break;
          uVar6 = XOSTimeGet();
          if (uVar9 < uVar6) goto LAB_ram_02077b88;
        }
        sRam020cf62c = sRam020cf62c + 1;
        uVar4 = *(undefined4 *)(iVar10 + 0x20d410c);
        auStack_1c4[uVar8] = (&uRam020d4108)[uVar6 * 3];
        auStack_1b4[uVar8] = uVar4;
        if (uVar8 != 0) {
          func_0x020768e8(param_2,auStack_1c4,auStack_1b4,(int)uVar2 >> 1,unaff_r14);
        }
        uVar2 = uVar2 + 1;
        sys_irq_clear();
      } while( true );
    }
    for (; (int)uVar2 < unaff_r14; uVar2 = uVar2 + 1) {
      uVar8 = uVar2 & 3;
      puVar5 = _DAT_ram_020d4238;
      uVar4 = _DAT_ram_020d423c;
      if (uVar8 == 0) {
LAB_ram_0207787c:
        func_0x02042ae4(puVar5,uVar4);
      }
      else if (uVar8 == 2) {
        puVar5 = *(undefined **)(puStack_1e8 + 4);
        uVar4 = uRam020c206c;
        goto LAB_ram_0207787c;
      }
      ax32xx_mjpegEncodeInit(true,true);
      ax32xx_mjpegEncodeSizeSet
                ((uint)(_DAT_ram_020d4204 >> 2),(int)((uint)_DAT_ram_020d4206 << 2) / unaff_r14,p2,
                 uVar11);
      if ((param_6 == 1) && (uVar2 == unaff_r14 - 4U)) {
        video_watermark_update(p2,uVar11,0);
      }
      _DAT_ram_020d4248 = uRam020d41e8;
      _DAT_ram_020d4250 = iRam020d41ec;
      iVar10 = (int)((uint)_DAT_ram_020d4206 << 2) / unaff_r14;
      ax32xx_mjpeg_inlinebuf_init
                ((byte *)(_DAT_ram_020d4210 + uVar2 * (_DAT_ram_020d4204 >> 2) * iVar10),
                 (byte *)(_DAT_ram_020d4214 + uVar2 * (_DAT_ram_020d4204 >> 3) * iVar10));
      ax32xx_mjpegEncodeBufferSet(_DAT_ram_020d4248,_DAT_ram_020d4248 + _DAT_ram_020d4250);
      ax32xx_mjpeg_manual_on();
      ax32xx_intEnable(0x10,1);
      iVar10 = XOSTimeGet();
      uVar9 = iVar10 + 2000;
      while( true ) {
        uVar6 = (uint)(char)sRam020cf62c;
        iVar10 = uVar6 * 0xc;
        if (*(int *)(iVar10 + 0x20d4110) != 0) break;
        uVar6 = XOSTimeGet();
        if (uVar9 < uVar6) goto LAB_ram_02077b88;
      }
      sRam020cf62c = sRam020cf62c + 1;
      uVar4 = *(undefined4 *)(iVar10 + 0x20d410c);
      auStack_1a4[uVar8] = (&uRam020d4108)[uVar6 * 3];
      (&uStack_184)[uVar8] = uVar4;
      if (uVar8 == 3) {
        func_0x02076c0c(param_2,auStack_1a4,&uStack_184,(int)uVar2 >> 2,unaff_r14);
      }
      sys_irq_clear();
    }
LAB_ram_02077be0:
    close(unaff_r18);
    iVar10 = XOSTimeGet();
    uart_Printf(s________________d_______________ram_020c1fef,iVar10 - iVar12);
    unaff_r18 = open(auStack_164,3);
    if ((int)unaff_r18 < 0) goto LAB_ram_02077c1c;
    uVar2 = fs_size(unaff_r18);
    iVar12 = read(unaff_r18,_DAT_ram_020d4238,uVar2);
    if (-1 < iVar12) {
      close(unaff_r18);
      iVar12 = 0;
      func_0x02074d1c(param_2,_DAT_ram_020d4238,uVar2);
      goto LAB_ram_02077098;
    }
    pcVar3 = s_image_encode__open_upper_file_fa_ram_020c2010;
  }
  goto LAB_ram_02077c54;
LAB_ram_02077554:
  uVar11 = XOSTimeGet();
  if (uVar2 < uVar11) goto LAB_ram_02077568;
  goto LAB_ram_020774e4;
LAB_ram_02077b88:
  pcVar3 = s_thumber_image_encode__timeout_ram_020c1f89 + 8;
  goto LAB_ram_02077b90;
  while (uVar11 = XOSTimeGet(), uVar11 <= uVar2) {
LAB_ram_02077330:
    if ((int)(-unaff_r18 | unaff_r18) < 0) {
      iVar12 = (uint)(char)sRam020cf62c * 0xc;
      uVar11 = *(uint *)(iVar12 + 0x20d4110);
      if ((int)(-uVar11 | uVar11) < 0) {
        iVar12 = write(unaff_r18,(undefined *)(&uRam020d4108)[(uint)(char)sRam020cf62c * 3],
                       *(uint *)(iVar12 + 0x20d410c));
        if (iVar12 < 0) goto LAB_ram_0207752c;
        uart_Printf(s____image_OK_____ram_020c1f77);
        sRam020cf62c = sRam020cf62c + 1;
        if (app_context.b8 != 0) {
          XOSTimeGet();
          iStack_180 = _DAT_ram_020d4210;
          iStack_17c = _DAT_ram_020d4214;
          uStack_178 = 2;
          uStack_170 = _DAT_ram_020d4204;
          uStack_16e = _DAT_ram_020d4206;
          uStack_184._1_1_ = 1;
          uStack_174 = uRam020d4108;
          iVar12 = jpeg_show_or_save(&uStack_184);
          if (iVar12 < 0) {
            uart_Printf(s_jpg_decode_fail_ram_020c1fa8);
          }
          else {
            XOSTimeGet();
            mjpeg_get_decode_resolution(auStack_1d2,auStack_1b4);
          }
          func_0x02042cd4((&uRam020d4108)[((char)sRam020cf62c - 1) * 3]);
          func_0x02042d90();
          if (param_6 != 0) {
            func_0x02077ef0(_DAT_ram_020d4210,param_8,param_9,param_10,0,uVar8);
          }
        }
        if (app_context.b8 == 0) goto LAB_ram_02077574;
        func_0x02075614(param_5,param_7,param_6,1);
        lseek(unaff_r18,0);
        iVar12 = XOSTimeGet();
        uVar2 = iVar12 + 2000;
        goto LAB_ram_020774e4;
      }
    }
  }
LAB_ram_02077568:
  pcVar3 = s_thumber_image_encode__timeout_ram_020c1f89;
LAB_ram_02077b90:
  uart_Printf(pcVar3);
  iVar12 = -1;
LAB_ram_02077098:
  sys_irq_clear();
  func_0x020425f8();
  ax32xx_csiEnable(1);
  ui_state_apply(app_context.b7);
  if (puRam020d40e4 != (undefined *)0x0) {
    func_0x02074b7c();
    hal_sysMemFree(puRam020d40e4);
    puRam020d40e4 = (undefined *)0x0;
  }
  hal_dispframeFree(puStack_1e8);
  hal_mjpegRawBufferfree();
  hal_mjpegMemUninit();
  hal_mjpegEncodeUninit();
  close(unaff_r18);
  close(iStack_1e4);
  f_unlink(auStack_164);
  f_unlink(auStack_130);
  return iVar12;
}

