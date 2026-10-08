"""Reviewed stock instruction-cache maintenance for the researched KC02 image.

Ghidra KC02:LE:32:nodelay at 0x0202b014 and the AX3292 SDK's
ax32xx_sysIcacheInit agree: save r2/SP; disable ICCR bit 0 (SPR 0x2000),
write zero to ICAIR (SPR 0x2008), enable ICCR bit 0, restore r2/SP and return.
No clock, flash, interrupt-status or data-cache configuration writes occur.
The stock boot routine enables I-cache; this helper leaves that normal state on.
Pin its FULL body, not just a prologue, before using it as a USB callback.
"""
ICACHE_INIT_FN=0x0202B014
ICACHE_SIGNATURE=bytes.fromhex(
    'fc17e1d7f80fe1d70020609cfeff409c000083b4f8ff219c031084e0'
    '0000409c002003c0081080c0000083b4010084a8002003c00800219c'
    'f8ff2184fcff418400480044'
)
ICACHE_SITE=(ICACHE_INIT_FN,ICACHE_SIGNATURE,'stock instruction-cache reset')
