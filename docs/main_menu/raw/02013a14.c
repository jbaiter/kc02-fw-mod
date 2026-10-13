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

