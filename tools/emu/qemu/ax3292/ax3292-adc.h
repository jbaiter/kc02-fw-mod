/* AX3292 ADC completion contract.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Hardware evidence: docs/hw/2026-10-15.md, adc-conversion/report.json.
 * Stock hal_adcGetChannel(3) returns 1023 and ADCCON reads 0x13 afterwards;
 * neither hal_adcGetChannel nor adcRead clears GO in software.
 * TODO(approximation): conversion is immediate; non-key channels are fixed
 * battery/bandgap samples rather than an analogue supply model.
 */
static uint32_t ax3292_adc_completed_control(uint32_t control)
{
    return control & ~0xc0u; /* GO and BUSY self-clear, EN/channel survive */
}

static uint32_t ax3292_adc_sample(unsigned channel, unsigned key_channel,
                                 unsigned key_value)
{
    if (channel == key_channel) {
        return key_value;
    }
    return (channel == 9 || channel == 1) ? 700 : (channel == 11 ? 280 : 512);
}
