/* AX3292 JPGA encode and JPGB decode / scale, semiplanar YUV420.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Register contract: SDK ax32xx_mjpegDecode{Input,Output,Size,Enable}.
 * The guest parses the JPEG header and supplies its entropy-stream address;
 * find that header in the same input buffer, then let libjpeg do IDCT.
 *
 * TODO(approximation): chroma is resampled with a simple 4:2:0 box filter, not
 * the ISP's; the encoder honours the guest's quantization/Huffman tables but not
 * its exact rounding, and JPGB completion is deferred by a fixed 1 ms.
 */
#include <jpeglib.h>
#include <setjmp.h>

typedef struct AxJpegError {
    struct jpeg_error_mgr base;
    jmp_buf jump;
} AxJpegError;

static void ax_jpeg_error(j_common_ptr jpeg)
{
    AxJpegError *err = (AxJpegError *)jpeg->err;
    if (ax3292_dgetenv("AX3292_TRACE_JPEG")) {
        char message[JMSG_LENGTH_MAX];
        err->base.format_message(jpeg, message);
        fprintf(stderr, "jpeg: %s\n", message);
    }
    longjmp(err->jump, 1);
}

static bool ax3292_jpeg_decode(Ax3292State *s)
{
    struct jpeg_decompress_struct jpeg = { 0 };
    AxJpegError err;
    uint32_t input = s->sfr[0xc020 >> 2];
    uint32_t size = s->sfr[0xc078 >> 2];
    unsigned w = size >> 16, h = size & 0xffff;
    /* The upper half is DMA1's source stride, not the decoded YUV stride.
     * SDK ax32xx_mjpegDecodeSetSize: bit 12 enables manual output stride;
     * otherwise rows are packed at the scaled output width. */
    uint32_t layout = s->sfr[0xc0c0 >> 2];
    unsigned stride = layout & 0x1000 ? layout & 0xfff : w;
    uint32_t ybase = s->sfr[0xc010 >> 2];
    uint32_t uvbase = s->sfr[0xc014 >> 2];
    uint32_t start = input, end;
    uint8_t *pixels;
    uint8_t *packet = NULL;
    bool nested = false;

    if (!stride) { stride = w; }
    if (!w || !h || w > 2048 || h > 2048 || stride < w ||
        input >= AX3292_RAM_SIZE || input < AX3292_SDRAM_BASE ||
        !ybase || !uvbase ||
        (uint64_t)ybase + stride * h > AX3292_RAM_SIZE ||
        (uint64_t)uvbase + stride * ((h + 1) / 2) > AX3292_RAM_SIZE) {
        return false;
    }
    /* JPEG header is usually <1 KiB before entropy; bound the search. */
    while (start > AX3292_SDRAM_BASE && input - start < 65536) {
        if (s->ram_ptr[start] == 0xff && s->ram_ptr[start + 1] == 0xd8) {
            break;
        }
        start--;
    }
    if (s->ram_ptr[start] != 0xff || s->ram_ptr[start + 1] != 0xd8) {
        return false;
    }
    end = start + 2;
    while (end + 1 < AX3292_RAM_SIZE && end - start < 4 * MiB) {
        if (end >= input && s->ram_ptr[end] == 0xff && s->ram_ptr[end + 1] == 0xd8) {
            nested = true;
            break; /* next image in JRX, not part of this entropy scan */
        }
        if (s->ram_ptr[end] == 0xff && s->ram_ptr[end + 1] == 0xd9) {
            end += 2;
            break;
        }
        end++;
    }
    if (nested) {
        /* The hardware stops at image geometry. Give libjpeg an isolated
         * host-side packet with EOI; never change guest or SD bytes. */
        packet = g_try_malloc(end - start + 2);
        if (!packet) { return false; }
        memcpy(packet, s->ram_ptr + start, end - start);
        packet[end - start] = 0xff;
        packet[end - start + 1] = 0xd9;
    }
    jpeg.err = jpeg_std_error(&err.base);
    err.base.error_exit = ax_jpeg_error;
    if (setjmp(err.jump)) {
        jpeg_destroy_decompress(&jpeg);
        g_free(packet);
        return false;
    }
    jpeg_create_decompress(&jpeg);
    jpeg_mem_src(&jpeg, packet ? packet : s->ram_ptr + start,
                 end - start + (nested ? 2 : 0));
    jpeg_read_header(&jpeg, TRUE);
    if (jpeg.image_width > 4096 || jpeg.image_height > 4096) {
        jpeg_destroy_decompress(&jpeg);
        g_free(packet);
        return false;
    }
    jpeg.out_color_space = JCS_YCbCr;
    jpeg_start_decompress(&jpeg);
    pixels = (*jpeg.mem->alloc_large)((j_common_ptr)&jpeg, JPOOL_IMAGE,
                                     (size_t)jpeg.output_width * jpeg.output_height * 3);
    while (jpeg.output_scanline < jpeg.output_height) {
        JSAMPROW row = pixels + jpeg.output_scanline * jpeg.output_width * 3;
        jpeg_read_scanlines(&jpeg, &row, 1);
    }
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            unsigned sx = (uint64_t)x * jpeg.output_width / w;
            unsigned sy = (uint64_t)y * jpeg.output_height / h;
            uint8_t *p = pixels + (sy * jpeg.output_width + sx) * 3;
            s->ram_ptr[ybase + y * stride + x] = p[0];
            if (!(x & 1) && !(y & 1) && x + 1 < w) {
                s->ram_ptr[uvbase + (y / 2) * stride + x] = p[1];
                s->ram_ptr[uvbase + (y / 2) * stride + x + 1] = p[2];
            }
        }
    }
    /* JPGB consumes entropy for the programmed image, not a whole JPEG
     * container. KC02 can concatenate its stitched preview and original scan;
     * asking libjpeg to parse the following container trailer rejects that
     * otherwise complete first scan. Stop after all output rows, like DMA. */
    jpeg_abort_decompress(&jpeg);
    jpeg_destroy_decompress(&jpeg);
    g_free(packet);
    return true;
}

static void ax3292_jpeg_b_irq(Ax3292State *s)
{
    uint32_t mask = s->sfr[0xc03c >> 2];
    if ((mask & 0x80000000u) && (mask & s->sfr[0xc040 >> 2] & 0x7fffffff)) {
        s->cpu->env.picsr |= 1u << 22;
    } else {
        s->cpu->env.picsr &= ~(1u << 22);
    }
    ax3292_irq_update(s);
}

static void ax3292_jpeg_b_tick(Ax3292State *s)
{
    if (!s->jpeg_b_pending || qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) < s->jpeg_b_ready_ns) {
        return;
    }
    s->jpeg_b_pending = false;
    bool ok = s->jpeg_b_ok;
    s->sfr[0xc040 >> 2] |= ok ? 0x44 : 0x100;
    if (s->jpeg_trace) {
        fprintf(stderr, "jpgb: %s input=%08x size=%08x mask=%08x\n",
                ok ? "done" : "error", s->sfr[0xc020 >> 2],
                s->sfr[0xc078 >> 2], s->sfr[0xc03c >> 2]);
    }
    ax3292_jpeg_b_irq(s);
}

/* Keep all libjpeg-mutated state on the heap so error longjmp cannot leave
 * indeterminate automatic variables. The destination is bounded by DMA end,
 * including the sector padding the firmware uses when publishing a frame. */
typedef struct AxJpegEncode {
    struct jpeg_compress_struct jpeg;
    AxJpegError error;
    struct jpeg_destination_mgr dest;
    uint8_t *buffer;
    size_t capacity, length;
} AxJpegEncode;

static void ax_jpeg_dest_init(j_compress_ptr jpeg)
{
    AxJpegEncode *ctx = jpeg->client_data;
    ctx->dest.next_output_byte = ctx->buffer;
    ctx->dest.free_in_buffer = ctx->capacity;
}

static boolean ax_jpeg_dest_full(j_compress_ptr jpeg)
{
    ax_jpeg_error((j_common_ptr)jpeg);
    return FALSE;
}

static void ax_jpeg_dest_finish(j_compress_ptr jpeg)
{
    AxJpegEncode *ctx = jpeg->client_data;
    ctx->length = ctx->capacity - ctx->dest.free_in_buffer;
}

static bool ax3292_jpeg_encode(Ax3292State *s)
{
    static const uint8_t zigzag[64] = {
        0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,
        12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
        35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
        58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63
    };
    uint32_t ctrl = s->sfr[0xb800 >> 2];
    uint32_t size = s->sfr[0xb80c >> 2], source = s->sfr[0xb878 >> 2];
    unsigned w = size >> 16, h = size & 0xffff;
    unsigned sw = source >> 16, sh = source & 0xffff;
    /* In automatic CSI mode the encoder consumes the I2J DMA, not the
     * manual RAM-source registers. The SDK leaves B820/B824 zero in this mode. */
    unsigned input = ctrl & 0x400 ? 0xb820 : 0xb050;
    uint32_t ybase = s->sfr[input >> 2], uvbase = s->sfr[(input + 4) >> 2];
    uint32_t out = s->sfr[0xb810 >> 2];
    uint32_t end = s->sfr[0xb814 >> 2] & 0x0fffffff;
    unsigned mode = (ctrl >> 19) & 3;
    unsigned info = mode == 1 ? 8 : 0;
    unsigned q = (ctrl >> 23) & 15;
    size_t capacity;
    AxJpegEncode *ctx;
    bool ok = false;

    if (!w || !h || !sw || !sh || ((sw | sh) & 1) ||
        w > 4096 || h > 4096 || sw > 4096 || sh > 4096 ||
        mode > 1 || (ctrl & (1u << 18)) ||
        ybase < AX3292_SDRAM_BASE || uvbase < AX3292_SDRAM_BASE ||
        (uint64_t)ybase + (uint64_t)sw * sh > AX3292_RAM_SIZE ||
        (uint64_t)uvbase + (uint64_t)sw * sh / 2 > AX3292_RAM_SIZE ||
        out < AX3292_SDRAM_BASE || end > AX3292_RAM_SIZE || end <= out ||
        end - out < 512) {
        return false;
    }
    capacity = ((end - out) & ~511u) - info;
    ctx = g_new0(AxJpegEncode, 1);
    ctx->capacity = capacity;
    ctx->buffer = g_try_malloc(capacity);
    if (!ctx->buffer) { g_free(ctx); return false; }
    ctx->jpeg.err = jpeg_std_error(&ctx->error.base);
    ctx->error.base.error_exit = ax_jpeg_error;
    if (setjmp(ctx->error.jump)) { goto done; }
    jpeg_create_compress(&ctx->jpeg);
    ctx->jpeg.client_data = ctx;
    ctx->dest.init_destination = ax_jpeg_dest_init;
    ctx->dest.empty_output_buffer = ax_jpeg_dest_full;
    ctx->dest.term_destination = ax_jpeg_dest_finish;
    ctx->jpeg.dest = &ctx->dest;
    ctx->jpeg.image_width = w;
    ctx->jpeg.image_height = h;
    ctx->jpeg.input_components = 3;
    ctx->jpeg.in_color_space = JCS_YCbCr;
    jpeg_set_defaults(&ctx->jpeg); /* baseline 4:2:0 */
    /* DRISKIP (bit 16) suppresses row restart markers. KC02's StitchUpper
     * merges half-width JPEGs by alternating these independently coded rows;
     * emitting a plain uninterrupted JPEG breaks the saved stitched image. */
    ctx->jpeg.restart_in_rows = !(ctrl & (1u << 16));

    /* SDK jpegA_table_init stores the two quantizers in zigzag order at
     * 0x64000; quality nibble is a multiplier in eighths (zero means eight).
     * Use the guest's Huffman count/value tables, not a host quality preset. */
    if (!q) { q = 8; }
    for (unsigned table = 0; table < 2; table++) {
        JQUANT_TBL *qt = ctx->jpeg.quant_tbl_ptrs[table];
        JHUFF_TBL *dc = ctx->jpeg.dc_huff_tbl_ptrs[table];
        JHUFF_TBL *ac = ctx->jpeg.ac_huff_tbl_ptrs[table];
        for (unsigned i = 0; i < 64; i++) {
            unsigned quant = s->ram_ptr[0x64000 + table * 64 + i];
            if (!quant) { goto done; }
            qt->quantval[zigzag[i]] = MIN(255, MAX(1, (quant * q + 4) / 8));
        }
        memcpy(dc->bits + 1, s->ram_ptr + 0x64080 + table * 16, 16);
        memcpy(ac->bits + 1, s->ram_ptr + 0x640a0 + table * 16, 16);
        unsigned ndc = 0, nac = 0;
        for (unsigned i = 1; i <= 16; i++) {
            ndc += dc->bits[i]; nac += ac->bits[i];
        }
        if (!ndc || ndc > 12 || !nac || nac > 256) { goto done; }
        memcpy(dc->huffval, s->ram_ptr + 0x640c0 + table * 28, ndc);
        memcpy(ac->huffval, s->ram_ptr + (table ? 0x641aa : 0x640f8), nac);
        qt->sent_table = dc->sent_table = ac->sent_table = FALSE;
    }
    jpeg_start_compress(&ctx->jpeg, TRUE);
    JSAMPROW row = (*ctx->jpeg.mem->alloc_large)((j_common_ptr)&ctx->jpeg,
                                               JPOOL_IMAGE, (size_t)w * 3);
    while (ctx->jpeg.next_scanline < h) {
        unsigned sy = (uint64_t)ctx->jpeg.next_scanline * sh / h;
        for (unsigned x = 0; x < w; x++) {
            unsigned sx = (uint64_t)x * sw / w;
            unsigned uv = (sy / 2) * sw + (sx & ~1u);
            row[x * 3] = s->ram_ptr[ybase + sy * sw + sx];
            row[x * 3 + 1] = s->ram_ptr[uvbase + uv];
            row[x * 3 + 2] = s->ram_ptr[uvbase + uv + 1];
        }
        jpeg_write_scanlines(&ctx->jpeg, &row, 1);
    }
    jpeg_finish_compress(&ctx->jpeg);
    if (info) {
        memcpy(s->ram_ptr + out, "00dc", 4);
        stl_le_p(s->ram_ptr + out + 4, ctx->length);
    }
    memcpy(s->ram_ptr + out + info, ctx->buffer, ctx->length);
    size_t length = info + ctx->length;
    size_t padded = (length + 511) & ~(size_t)511;
    memset(s->ram_ptr + out + length, 0, padded - length);
    s->sfr[0xb818 >> 2] = s->sfr[0xb81c >> 2] = out + length;
    if (s->jpeg_trace) {
        fprintf(stderr, "jpga: %ux%u -> %ux%u q=%u out=%08x bytes=%zu info=%u restart_rows=%d\n",
                sw, sh, w, h, q, out, length, info,
                ctx->jpeg.restart_in_rows);
    }
    ok = true;
done:
    jpeg_destroy_compress(&ctx->jpeg);
    g_free(ctx->buffer);
    g_free(ctx);
    return ok;
}

static void ax3292_jpeg_a_irq(Ax3292State *s)
{
    uint32_t mask = s->sfr[0xb83c >> 2];
    if ((mask & 0x80000000u) && (mask & s->sfr[0xb840 >> 2] & 0x7fffffff)) {
        s->cpu->env.picsr |= 1u << 16;
    } else {
        s->cpu->env.picsr &= ~(1u << 16);
    }
    ax3292_irq_update(s);
}

static void ax3292_jpeg_a_tick(Ax3292State *s)
{
    uint32_t ctrl = s->sfr[0xb800 >> 2];
    if (!s->jpeg_a_pending) { return; }
    if ((ctrl & 0x400) && s->sfr[0xb8a4 >> 2] < (s->sfr[0xb878 >> 2] & 0xffff)) {
        return; /* manual encoder waits for the producer's posted lines */
    }
    s->jpeg_a_pending = false;
    bool ok = ax3292_jpeg_encode(s);
    s->sfr[0xb800 >> 2] &= ~0x100u;
    s->sfr[0xb840 >> 2] |= ok ? 0x40 : 0x48; /* frame end / output error */
    if (!ok && s->jpeg_trace) {
        fprintf(stderr, "jpga: encode failed ctrl=%08x size=%08x source=%08x\n",
                ctrl, s->sfr[0xb80c >> 2], s->sfr[0xb878 >> 2]);
    }
    ax3292_jpeg_a_irq(s);
}
