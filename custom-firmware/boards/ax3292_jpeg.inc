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

/* A bounded run still has the stock interrupt handlers installed, and this trial
 * reconfigures the CSI underneath them: csihv_tomjp_dma_cfg even ORs more sources
 * into the CSI enable register. Taking a CSI or JPEG interrupt in the middle of
 * that is how a run dies without ever answering USB again, so those three vectors
 * are masked at PICMR for the duration and restored afterwards. Polling is
 * unaffected: the status bits are read directly. USB and the tick stay enabled. */
#define JPEG_IRQ_MASK ((1u<<14)|(1u<<16)|(1u<<22))   /* CSI, JPGA, JPGB */

static u32 jpeg_irq_saved, jpeg_irq_active;

static void jpeg_irq_hold(void)
{
    if (jpeg_irq_active) { return; }
    jpeg_irq_saved=REG(0x4800);
    REG(0x4800)=jpeg_irq_saved&~JPEG_IRQ_MASK;
    jpeg_irq_active=1;
}
static void jpeg_irq_release(void)
{
    if (!jpeg_irq_active) { return; }
    REG(0x4800)=jpeg_irq_saved;
    jpeg_irq_active=0;
}

static int jpeg_encode_once(u32 scratch)
{
    AxTime t={0,0,0};
    unsigned guard;
    u32 now=ax_time_ms(&t,REG(0x9908));
    if (luma_csi_fault) { return 0; }
    jpeg_irq_hold();
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
    jpeg_irq_release();
    return luma_jpeg_state.state==AX_JPEG_DONE;
}

/* ---------------------------------------------------------------------------
 * DECODE: 1280x720 still -> 320x180 view
 *
 * Used by the gallery when a photo is opened, and by the bounded trial to prove
 * the whole loop end to end. Stock surface is parse + arm + size + stop; the
 * enable is inlined because ax32xx_mjpegDecodeEnable would unmask IRQ 0x16 in
 * the stock PICMR (see include/ax_jpeg_decode.h).
 * --------------------------------------------------------------------------- */
static AxDecode luma_decode_state;
static u32 luma_decode_geom;

static int jpeg_alloc_ready(void)
{
    /* mjpeg_parse_image_header mallocs 1 KiB of scratch without checking the
     * result, so the only safe thing is to confirm the pool can serve it first. */
    void *p=(void *)0;
    p=(void *)((void *(*)(u32,u32))0x0203e5c4u)(0x400u,0x20u);
    if (!p) { return 0; }
    ((void (*)(void *))0x0203e694u)(p);
    return 1;
}
static const AxDecodeOps jpeg_decode_ops={
    jpeg_reg_read,jpeg_reg_write,jpeg_alloc_ready,
    (void (*)(const void *,unsigned))0x0202b0d4u,
    (int (*)(const u8 *))0x02041864u,                        /* parse header */
    (int (*)(const u8 *,u8 *,u8 *,u16,u16))0x02041d64u,      /* arm decoder  */
    (void (*)(u32,u32,u32,u32,u32))0x02036cd8u,              /* size+stride  */
    (void (*)(void))0x02037350u                              /* stop         */
};

/* Decode one still into the viewer's YUV planes. Services the panel while it
 * waits: the decoder is bounded but not instant. */
static int jpeg_view_to_yuv(const u8 *jpeg,unsigned length)
{
    AxTime t={0,0,0};
    unsigned guard;
    u32 now=ax_time_ms(&t,REG(0x9908));
    int ok;
    jpeg_irq_hold();
    unsigned w=0,h=0;
    if (!ax_jpeg_frame_info(jpeg,length,&length,&w,&h)) return 0;
    luma_decode_geom=(w<<16)|(h&0xffffu);
    if (!ax_decode_start(&luma_decode_state,&jpeg_decode_ops,jpeg,length,
                         stock_photo_view_y(),AX_DECODE_Y_BYTES,
                         stock_photo_view_y_guard(),
                         stock_photo_view_uv(),AX_DECODE_UV_BYTES,
                         stock_photo_view_uv_guard(),now)) return 0;
    for (guard=0;guard<0x8000000u;guard++) {
        lcd_service();
        ax_decode_poll(&luma_decode_state,ax_time_ms(&t,REG(0x9908)));
        if (luma_decode_state.state==AX_DECODE_DONE ||
            luma_decode_state.state==AX_DECODE_FAILED) break;
    }
    if (luma_decode_state.state==AX_DECODE_RUNNING)
        ax_decode_poll(&luma_decode_state,now+2001u);
    ok=luma_decode_state.state==AX_DECODE_DONE;
    jpeg_irq_release();
    return ok;
}
static const StockPhotoDecode jpeg_view={jpeg_view_to_yuv};

/* One still at the sensor's own resolution, offered to the photo store. Called
 * from the shutter path, so it must leave the preview exactly as it found it and
 * must not queue a second save while one is still in flight. */
static int luma_capture_photo(void)
{
    if (!luma_photos_enabled) { return 0; }
    if (!jpeg_encode_once(0)) { return 0; }
    return stock_photo_save_jpeg(luma_jpeg_output.bytes,luma_jpeg_state.length);
}

/* End-to-end photo self-test for the bounded trial: the frame that was just
 * encoded is saved as a create-only LUMnnnnn.JPG through the stock FatFs, then
 * read back and decoded again. Every step is the same code the shutter and the
 * gallery use, so one bounded run proves the whole loop without a panel verdict.
 *
 * Returns: 1 saved and read back, 2 save refused, 3 save failed, 4 readback failed.
 */
static int jpeg_photo_selftest(App *a,u32 *loaded_size,unsigned *file_count)
{
    AxTime t={0,0,0};
    unsigned guard,before=stock_photo_count();
    const StockFs *fs;
    int ret;
    if (loaded_size) { *loaded_size=0; }
    if (file_count) { *file_count=before; }
    jpeg_irq_hold();
    ret=stock_photo_save_jpeg(luma_jpeg_output.bytes,luma_jpeg_state.length);
    if (ret!=PHOTO_QUEUED) { jpeg_irq_release(); return 2; }
    {
        u32 deadline=ax_time_ms(&t,REG(0x9908))+30000u;
        for (guard=0;guard<200000u;guard++) {
            lcd_service();
            stock_photo_poll(a,ax_time_ms(&t,REG(0x9908)),1);
            fs=stock_photo_state();
            if (!fs->kind || fs->last_error) break;
            if (ax_time_ms(&t,REG(0x9908))>deadline) { jpeg_irq_release(); return 3; }
        }
    }
    fs=stock_photo_state();
    if (fs->last_error || fs->count==before) { return 3; }
    if (file_count) { *file_count=fs->count; }
    /* Read the file back and let the gallery decode it, exactly as a viewer would. */
    a->screen=GALLERY;
    a->photo=fs->count-1;
    a->photo_rows=0;
    if (!stock_photo_load(a->photo,(u16 *)frame)) { jpeg_irq_release(); return 4; }
    {
        u32 deadline=ax_time_ms(&t,REG(0x9908))+10000u;
        for (guard=0;guard<200000u;guard++) {
            lcd_service();
            stock_photo_poll(a,ax_time_ms(&t,REG(0x9908)),1);
            fs=stock_photo_state();
            if ((!fs->kind && a->photo_rows) || fs->last_error) break;
            if (ax_time_ms(&t,REG(0x9908))>deadline) { break; }
        }
    }
    jpeg_irq_release();
    fs=stock_photo_state();
    a->screen=CAMERA;
    if (fs->last_error || a->photo_rows!=AX_DECODE_H) { return 4; }
    if (loaded_size) { *loaded_size=fs->loaded_size; }
    return 1;
}
