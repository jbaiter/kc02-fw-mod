/* AX3292 DE framebuffer address contract.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Hardware readbacks contain 24-bit SDRAM offsets, whereas CSI reports full
 * CPU addresses. SDK and canonical ax32xx_videoSetAddr store full pointers.
 * Evidence and geometry: docs/hw/2026-10-15.md, original preview fixture.
 */
static uint32_t ax3292_de_address_register(uint32_t value)
{
    return value & 0x00ffffffu;
}

static uint32_t ax3292_de_dma_address(uint32_t value)
{
    uint32_t offset = ax3292_de_address_register(value);
    /* Retain the model's null/unprogrammed-plane guard. */
    return offset ? 0x02000000u | offset : 0;
}
