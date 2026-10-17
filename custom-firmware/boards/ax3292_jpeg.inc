/* RAM-only 1280x720 hardware-JPEG trial. Included from ax3292.c after
 * hardware_init, so it runs inside LUMA_BENCH_RUN with a live preview.
 *
 * Every firmware prefix this calls is pinned and verified by show_luma.py
 * before the image is uploaded (LUMA_STOCK_SITES). Nothing here writes a file,
 * touches flash, calls the allocator, or installs an interrupt handler.
 */
static u32 jpeg_reg_read(unsigned n) { return REG(n); }
static void jpeg_reg_write(unsigned n,u32 v) { REG(n)=v; }
static const AxJpegOps jpeg_ops={
    jpeg_reg_read,jpeg_reg_write,
    (void (*)(int,int))0x02036964u,                       /* mjpegEncodeInit */
    (void (*)(u8 *,u8 *,u16,u16))0x02034308u,             /* csihv_tomjp_dma_cfg */
    (void (*)(u8 *,u8 *))0x02036948u,                     /* inlinebuf_init */
    (void (*)(u32,u32,u32,u32))0x020366fcu,               /* EncodeSizeSet */
    (void (*)(u8))0x020367a4u,                            /* EncodeQuilitySet */
    (void (*)(u8))0x020368e4u,                            /* EncodeInfoSet */
    (void (*)(u32,u32))0x020364d0u,                       /* EncodeBufferSet */
    (void (*)(void))0x02036b9cu,                          /* mjpeg_manual_on */
    (void (*)(u32,u32))0x0202b0d4u,                       /* sysDcacheFlush */
    (void (*)(u32,u32))0x0202b150u                        /* sysDcacheInvalid */
};

/* Return the shared CSI/ISP to the configuration that produced the working
 * preview, in the same order hardware_init established it. */
static void jpeg_restore_preview(void)
{
    unsigned i;
    typedef void (*scaler_fn)(u16,u16,u16,u16,u16,u16,u16,u16,u16,u32 *);
    u32 panel=*(volatile u32 *)0x020ce980u;

    REG(0xb018) = 0x00200000u;                 /* manual staging, all output off */
    REG(0xb000) = 0x0440062cu;                 /* CSI_ENABLE clear */
    REG(0xb038) = luma_jpeg_state.saved_i2w_h;
    REG(0xb03c) = luma_jpeg_state.saved_i2w_v;
    ax_csi_guards_init(&sensor_dma_y,&sensor_dma_uv);
    jpeg_ops.flush((u32)&sensor_dma_y,sizeof(sensor_dma_y));
    jpeg_ops.flush((u32)&sensor_dma_uv,sizeof(sensor_dma_uv));
    if (panel < 0x02000000u || panel > 0x027ffe00u) { luma_csi_fault|=0x100u; return; }
    ((scaler_fn)0x020349f8u)(1280,720,SENSOR_COLS,SENSOR_ROWS,0,0,1280,720,
                             SENSOR_COLS,(u32 *)(panel+0xd0u));
    if (luma_csi_size_override) { REG(0xb01c) = luma_csi_size_override; }
    REG(0xb060) = (u32)sensor_dma_y.data;
    REG(0xb064) = (u32)sensor_dma_uv.data;
    REG(0xb014) = 0x800ef87bu;
    REG(0xb000) |= 1u;
    for (i=0;i<0x400000u && (REG(0xb008)&0x80000u);i++) { }
    if (REG(0xb008)&0x80000u) { luma_csi_fault|=0x200u; return; }
    if ((REG(0xb004)&0x420000u)!=0x420000u ||
        REG(0xb180)!=((SENSOR_ROWS<<16)|SENSOR_COLS) ||
        REG(0xb068)!=SENSOR_COLS) { luma_csi_fault|=0x400u; return; }
    REG(0xb00c)=0x5au;
    REG(0xb014)|=0x8008005au;
    REG(0xb018)|=0x40000u;
}

static void jpeg_ram_check(u32 scratch)
{
    AxTime t={0,0,0};
    unsigned guard;
    u32 now=ax_time_ms(&t,REG(0x9908));
    if (luma_csi_fault) { return; }
    luma_jpeg_owned=1;
    if (ax_jpeg_start(&luma_jpeg_state,&jpeg_ops,&luma_jpeg_output,
                      sensor_dma_y.data,sizeof(sensor_dma_y),sensor_dma_y.tail,
                      sensor_dma_uv.data,sizeof(sensor_dma_uv),sensor_dma_uv.tail,now)) {
        for (guard=0;guard<0x8000000u;guard++) {
            lcd_service();
            ax_jpeg_poll(&luma_jpeg_state,ax_time_ms(&t,REG(0x9908)));
            if (luma_jpeg_state.state==AX_JPEG_DONE ||
                luma_jpeg_state.state==AX_JPEG_FAILED) break;
        }
        if (luma_jpeg_state.state==AX_JPEG_INPUT ||
            luma_jpeg_state.state==AX_JPEG_ENCODING)
            ax_jpeg_poll(&luma_jpeg_state,now+2001u);
        ax_jpeg_stop(&luma_jpeg_state);
        if (scratch) {
            volatile u32 *e=(volatile u32 *)(scratch+512u);
            volatile u32 *c=(volatile u32 *)(scratch+1024u);
            for (guard=0;guard<128;guard++) {
                e[guard]=REG(0xb800u+4u*guard);   /* encoder block */
                c[guard]=REG(0xb000u+4u*guard);   /* CSI/ISP block */
            }
        }
        jpeg_restore_preview();
    }
    luma_jpeg_owned=0;
    /* Give the restored preview a chance to deliver fresh frames again. */
    now=ax_time_ms(&t,REG(0x9908));
    for (guard=0;guard<0x8000000u;guard++) {
        lcd_service(); csi_service();
        if (ax_time_ms(&t,REG(0x9908))-now>=300u) break;
    }
}
