# SDK function-prototype extraction report

- SDK tree: `/home/jbaiter/.src/ax3292-sdk/firmware0.96v2_totest`
- Header files scanned: 177
- Prototypes found in SDK headers (total): 1442
- Unique prototype names in SDK: 1427
- Prototypes matching a `funcs_now.tsv` name: 990
- Matching prototypes taking >=1 explicit pointer (`*`) parameter: 297
- Matching prototypes taking >=1 pointer parameter counting pointer typedefs (winHandle, FHANDLE, ...): 343
- Matched names declared in more than one header: 5
- Matched names with differing signatures across headers (conflicts): 1

## Limitation: parameter-count comparison vs Ghidra
`funcs_now.tsv` only holds `0xADDR\tname\tsize`; it does not carry Ghidra's parameter count, so a per-function comparison between the prototype parameter count and Ghidra's current parameter count could not be produced here. This is left as a follow-up to check later (the data would have to be read from `/tmp/kc02-board-re/ghidra_scripts/` or a fresh Ghidra dump).

## 20 most interesting struct-pointer prototypes (verbatim)

1. `hal_lcd_gamma_cfg` — hal/inc/hal_lcd.h
   `void hal_lcd_gamma_cfg(_Lcd_Header_ * p_devLcdHeader,lcddev_t *p_lcddev,lcd_gamma_t *p_lcd_gamma)`
2. `winIntersection` — sUI/winManage.h
   `void winIntersection(uiRect* out,uiRect* win1,uiRect* win2)`
3. `hal_streamInit` — hal/inc/hal_stream.h
   `int hal_streamInit(Stream_Head_T *head,Stream_Node_T *pool,u32 num,u32 mem,u32 mem_len)`
4. `drawImage` — sUI/winManage.h
   `void drawImage(uiRect* winRect,uiRect* drawRect,resID id,uint8 alignment,uiColor bgColor)`
5. `f_findfirst` — bwlib/fs/inc/ff.h
   `FRESULT f_findfirst (DIR* dp, FILINFO* fno, const TCHAR* path, const TCHAR* pattern)`
6. `hal_rotateAdd` — hal/inc/hal_rotate.h
   `void hal_rotateAdd(u8 ch,disp_frame_t * src,disp_frame_t * dst)`
7. `aviDecodeIdx1QInit` — multimedia/avi/inc/avi.h
   `void aviDecodeIdx1QInit(DECODE_Q_T *idxq,AVIINDEXENTRY *table,int len)`
8. `f_readdir` — bwlib/fs/inc/ff.h
   `FRESULT f_readdir (DIR* dp, FILINFO* fno)`
9. `f_findnext` — bwlib/fs/inc/ff.h
   `FRESULT f_findnext (DIR* dp, FILINFO* fno)`
10. `hal_csiRegister` — hal/inc/hal_csi.h
   `void hal_csiRegister(_Sensor_Adpt_ *sensor_op,_Sensor_Ident_ *sensor_ident)`
11. `usb_lcd_cfg` — hal/inc/hal_lcd.h
   `void usb_lcd_cfg(lcddev_t *new_lcddev,lcddev_t *p_devLcd)`
12. `hal_osdcmpStart` — hal/inc/hal_osdcmp.h
   `void hal_osdcmpStart(disp_frame_t * src,disp_frame_t * dst)`
13. `aviDecodeIdx1Cpy` — multimedia/avi/inc/avi.h
   `void aviDecodeIdx1Cpy(AVIINDEXENTRY *dst,AVIINDEXENTRY*src)`
14. `aviDecodeIdx1QIn` — multimedia/avi/inc/avi.h
   `void aviDecodeIdx1QIn(DECODE_Q_T *idxq,AVIINDEXENTRY *idx1)`
15. `aviDecodeIdx1QOut` — multimedia/avi/inc/avi.h
   `void aviDecodeIdx1QOut(DECODE_Q_T *idxq,AVIINDEXENTRY *idx1)`
16. `winContain` — sUI/winManage.h
   `bool winContain(uiRect* parent,uiRect* child)`
17. `winOverlapping` — sUI/winManage.h
   `bool winOverlapping(uiRect* rect1,uiRect* rect2)`
18. `msgDealByMsgInfor` — ax32_platform_demo/application.h
   `void msgDealByMsgInfor(msgDealInfor* infor,uint32 msgType,void* handle,uint32 parameNum,uint32* parame)`
19. `hal_dispframeVideoCfg` — hal/inc/hal_lcdframe.h
   `void hal_dispframeVideoCfg(disp_frame_t* p_frame,u16 posX,u16 posY,u16 width,u16 height)`
20. `hal_streamIn` — hal/inc/hal_stream.h
   `int hal_streamIn(Stream_Head_T *head, u32 p, u32 len,s32 t_sync,s32 t_sync_next)`

## Conflicts / ambiguities

### `get_fattime`
- bwlib/fs/inc/diskio.h: `U32 get_fattime ()`
- bwlib/fs/inc/ff.h: `DWORD get_fattime (void)`


## Matched names declared in more than one header (same signature)

- `CopyToMember`: device/cmos_sensor/cmos_sensor.h, hal/isp/hal_isp.h
- `api_backrec_status`: hal/dusb/hal_usb_api.h, hal/husb/hal_usb_host_uvc.h
- `api_mjp_avi_stop`: hal/inc/hal_mjpeg.h, multimedia/video/video_record.h
- `get_fattime`: bwlib/fs/inc/diskio.h, bwlib/fs/inc/ff.h
- `uart_Printf`: ax32xx/inc/ax32xx.h, bwlib/Maxlib/max_user.h, hal/inc/hal_uart.h, sUI/debug.h
