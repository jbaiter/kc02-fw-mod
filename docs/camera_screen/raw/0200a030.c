/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200a030 Name: FUN_ram_0200a030
 */

void FUN_ram_0200a030(undefined4 param_1)

{
  undefined4 uVar1;
  int iVar2;
  undefined4 uVar3;
  
  iVar2 = configGet(0x18,&stack0x00000000);
  if (iVar2 == -0x7effff64) {
    uVar3 = win_find_child_by_id(param_1,4);
    uVar1 = 0x207ba28;
  }
  else {
    iVar2 = configGet(0x18,register0x00000401);
    if (iVar2 == -0x7effff65) {
      uVar3 = win_find_child_by_id(param_1,4);
      uVar1 = 0x207ba36;
    }
    else {
      iVar2 = configGet(0x18,register0x00000401);
      if (iVar2 == -0x7effff66) {
        uVar3 = win_find_child_by_id(param_1,4);
        uVar1 = 0x207ba2b;
      }
      else {
        iVar2 = configGet(0x18,register0x00000401);
        if (iVar2 == -0x7effff67) {
          uVar3 = win_find_child_by_id(param_1,4);
          uVar1 = 0x207ba2e;
        }
        else {
          iVar2 = configGet(0x18,register0x00000401);
          if (iVar2 == -0x7effff68) {
            uVar3 = win_find_child_by_id(param_1,4);
            uVar1 = 0x207ba3e;
          }
          else {
            iVar2 = configGet(0x18);
            if (iVar2 == -0x7effff69) {
              uVar3 = win_find_child_by_id(param_1,4);
              uVar1 = 0x207ba31;
            }
            else {
              iVar2 = configGet(0x18);
              if (iVar2 == -0x7effff6a) {
                uVar3 = win_find_child_by_id(param_1,4);
                uVar1 = 0x207ba35;
              }
              else {
                iVar2 = configGet(0x18);
                if (iVar2 == -0x7effff6b) {
                  uVar3 = win_find_child_by_id(param_1,4);
                  uVar1 = 0x207ba39;
                }
                else {
                  iVar2 = configGet(0x18);
                  if (iVar2 == -0x7effff6c) {
                    uVar3 = win_find_child_by_id(param_1,4);
                    uVar1 = 0x207ba3d;
                  }
                  else {
                    iVar2 = configGet(0x18);
                    if (iVar2 == -0x7effff6d) {
                      uVar3 = win_find_child_by_id(param_1,4);
                      uVar1 = 0x207ba41;
                    }
                    else {
                      iVar2 = configGet(0x18);
                      if (iVar2 == -0x7effff6e) {
                        uVar3 = win_find_child_by_id(param_1,4);
                        uVar1 = 0x207ba45;
                      }
                      else {
                        iVar2 = configGet(0x18);
                        if (iVar2 == -0x7effff6f) {
                          uVar3 = win_find_child_by_id(param_1,4);
                          uVar1 = 0x207ba49;
                        }
                        else {
                          iVar2 = configGet(0x18);
                          if (iVar2 == -0x7effff70) {
                            uVar3 = win_find_child_by_id(param_1,4);
                            uVar1 = 0x207ba4d;
                          }
                          else {
                            uVar3 = win_find_child_by_id(param_1,4);
                            uVar1 = 0x207ba51;
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  winSetResid(uVar3,uVar1);
  return;
}

