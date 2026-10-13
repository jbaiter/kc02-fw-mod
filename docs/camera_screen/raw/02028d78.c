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

