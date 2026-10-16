/* KC02 normal CSI frame-completion callback, 2026-10-16 audit.
 * Reference only; not compiled into Luma.
 *
 * Source: firmware/original.bin, SHA256
 * 218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5.
 * Ghidra KC02:LE:32:nodelay. The indirect callback was absent from the function
 * inventory; defined/disassembled at 0x0203a2f0, the literal callback address in
 * lcd_panel_init (0x0203af1c): ax32xx_csiISRRegiser(1, 0x0203a2f0).
 * Name hal_lcdSwitchFrame2 comes from the corresponding SDK callback.
 *
 * Conclusions supported by the body, not by timing speculation:
 * - Every normal completed-frame path that continues capture writes both frame
 *   destination addresses, including the path that reuses the same buffer.
 * - Those paths end in ax32xx_csiLCDDmaKick (0x020349b4), which only ORs LCDEN
 *   when LDMAMANEN is set. No LCDEN clear or PIP counter reset on this path.
 * - CSI error recovery in ax32xx_csiIRQHandler is a different path.
 * - UV offset uses y/2. Sensor input type alone does not determine output layout.
 * - Enabling the stock ISR also enables these callbacks and stock buffer/display
 *   bookkeeping. It is not a safe drop-in replacement for Luma's polling loop.
 *
 * Below is the decompiler body. Types/names of unknown callees remain uncertain;
 * in particular do not infer that func_0x02022700 or FUN_ram_02022720 are safe to
 * call from Luma. Ghidra did not emit a warning for this body, but its inferred
 * types are not independently validated.
 */
void hal_lcdSwitchFrame2(void)
{
    uint uVar1;
    char cVar2;
    sbyte *psVar3;
    int iVar4;
    sbyte *psVar5;
    undefined *puVar6;
    undefined *p0;
    int iVar7;
    int iVar8;
    bool bVar9;
    ushort uStack_2c;
    ushort uStack_2a;

    puVar6 = DAT_ram_020ce97c;
    if (DAT_ram_020ce97c == (undefined *)0x0) return;
    hal_lcdUpdataVCH();
    psVar5 = DAT_ram_020ce96c;
    iVar4 = DAT_ram_020ce968;
    psVar3 = DAT_ram_020ce964;
    iVar7 = (uint)*(ushort *)(DAT_ram_020ce968 + 4) +
            (uint)(*(ushort *)(DAT_ram_020ce968 + 6) >> 1) *
            (uint)*(ushort *)(puVar6 + 0x28);
    iVar8 = (uint)*(ushort *)(DAT_ram_020ce968 + 4) +
            (uint)*(ushort *)(DAT_ram_020ce968 + 6) *
            (uint)*(ushort *)(puVar6 + 0x28);
    if ((DAT_ram_020ce96c[1] != 0) && (*DAT_ram_020ce96c == 1)) {
        ax32xx_pipConfig(DAT_ram_020ce96c[2], *(ushort *)(DAT_ram_020ce96c + 4),
                        *(ushort *)(DAT_ram_020ce96c + 6),
                        *(ushort *)(DAT_ram_020ce96c + 10),
                        *(ushort *)(DAT_ram_020ce96c + 8),
                        SUB41(-(uint)DAT_ram_020ce960 >> 0x1f, 0));
        ax32xx_videoSetScaleLine((*(short *)(psVar5 + 6) +
                                 *(short *)(psVar5 + 8)) - 1, 1);
    }
    bVar9 = *(sbyte *)(iVar4 + 1) != 0;
    if (bVar9) {
        hal_csiResolutionGet(&uStack_2c, &uStack_2a);
        ax32xx_csiLCDScaler(uStack_2c, uStack_2a,
                            *(ushort *)(iVar4 + 10), *(ushort *)(iVar4 + 8),
                            DAT_ram_020ce984, DAT_ram_020ce988,
                            DAT_ram_020ce986, DAT_ram_020ce98a,
                            *(ushort *)(puVar6 + 0x28),
                            (uint *)(DAT_ram_020ce980 + 0xd0));
    }
    cVar2 = puVar6[0x2c];
    uVar1 = 4;
    puVar6[0x2c] = (sbyte)(cVar2 | 4);
    if (*psVar3 == 1) uVar1 = 0xc;
    if (((int)((uVar1 ^ uVar1 & (cVar2 | 4)) - 1) < 0) && (!bVar9)) {
        if (app_context.b58 != 0) {
            if (app_context.b5c == 2) {
                app_context.b5c = 0;
                func_0x02022700(puVar6);
            } else {
                iVar7 = FUN_ram_02022720();
                if (iVar7 == 0) {
                    func_0x02022700(puVar6);
                    app_context.b5c = 0;
                }
            }
            DAT_ram_020ce97c = (undefined *)0x0;
            psVar3[1] = 0;
            *(undefined1 *)(iVar4 + 1) = 0;
            return;
        }
        p0 = hal_lcdGetIdleBuffer();
        if (p0 != (undefined *)0x0) {
            hal_lcdDealBuffer(puVar6);
            hal_dispframeVideoCfg(p0, *(ushort *)(DAT_ram_020ce980 + 0x38),
                                 *(ushort *)(DAT_ram_020ce980 + 0x3a),
                                 *(ushort *)(DAT_ram_020ce980 + 0x3c),
                                 *(ushort *)(DAT_ram_020ce980 + 0x3e));
            ax32xx_csiLCDFrameSet(*(int *)(p0 + 4) + iVar8,
                                  *(int *)(p0 + 8) + iVar7);
            DAT_ram_020ce97c = p0;
            goto LAB_ram_0203a50c;
        }
    }
    ax32xx_csiLCDFrameSet(*(int *)(puVar6 + 4) + iVar8,
                          *(int *)(puVar6 + 8) + iVar7);
    puVar6[0x2c] = 0;
LAB_ram_0203a50c:
    psVar3[1] = 0;
    *(undefined1 *)(iVar4 + 1) = 0;
    ax32xx_csiLCDDmaKick();
}
