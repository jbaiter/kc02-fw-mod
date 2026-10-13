/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02067240 Name: R_loadResource
 */

/* SDK prototype: extern void R_loadResource(void *res,unsigned long int length)
   [bwlib/Maxlib/res/R.h] */

void R_loadResource(uint *p0)

{
  if (p0 != (uint *)0x0) {
    if ((*p0 & 0xff000000) == 0x80000000) {
      R_iconLoadResource();
    }
    else if ((*p0 & 0xff000000) == 0x81000000) {
      R_stringLoadResource(&stack0x00000000);
    }
  }
  return;
}

