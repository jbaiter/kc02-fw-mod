/* SPDX-License-Identifier: MIT */
#include "debug.h"
static const char digits[]="0123456789abcdef";
static int digit(unsigned c)
{
    if (c>='0' && c<='9') { return c-'0'; }
    if (c>='a' && c<='f') { return c-'a'+10; }
    if (c>='A' && c<='F') { return c-'A'+10; }
    return -1;
}
static unsigned length(const char *s) { unsigned n=0; while (s[n]) { n++; } return n; }
static int starts(const u8 *s, const char *prefix)
{
    while (*prefix) { if (*s++ != (u8)*prefix++) { return 0; } }
    return 1;
}
static int number(const u8 **p, u32 *out)
{
    const u8 *s=*p;
    u32 value=0;
    int n;
    if (digit(*s)<0) { return 0; }
    while ((n=digit(*s))>=0) {
        if (value>0x0fffffffu) { return 0; }
        value=(value<<4)|(unsigned)n; s++;
    }
    *p=s; *out=value;
    return 1;
}
static void send(Rsp *r, const u8 *data, unsigned size)
{
    if (r->ops.send) { r->ops.send(r->ops.context,data,size); }
}
static void reply(Rsp *r, const u8 *payload, unsigned size)
{
    unsigned i, checksum=0;
    if (size>RSP_CAP) { return; }
    r->last[0]='$';
    for (i=0; i<size; i++) { r->last[i+1]=payload[i]; checksum+=payload[i]; }
    r->last[size+1]='#'; r->last[size+2]=digits[(checksum>>4)&15]; r->last[size+3]=digits[checksum&15];
    r->last_size=size+4;
    send(r,r->last,r->last_size);
}
static void text(Rsp *r, const char *s) { reply(r,(const u8 *)s,length(s)); }
static void put_hex(u8 *out, u32 v)
{
    unsigned i;
    for (i=0; i<4; i++) { out[i*2]=digits[(v>>(i*8+4))&15]; out[i*2+1]=digits[(v>>(i*8))&15]; }
}
#define RX(n) "<reg name=\"r" #n "\" bitsize=\"32\"/>"
static const char target_xml[]=
    "<?xml version=\"1.0\"?><!DOCTYPE target SYSTEM \"gdb-target.dtd\"><target>"
    "<architecture>or1k</architecture><feature name=\"org.gnu.gdb.or1k.group0\">"
    RX(0) RX(1) RX(2) RX(3) RX(4) RX(5) RX(6) RX(7) RX(8) RX(9)
    RX(10) RX(11) RX(12) RX(13) RX(14) RX(15) RX(16) RX(17) RX(18) RX(19)
    RX(20) RX(21) RX(22) RX(23) RX(24) RX(25) RX(26) RX(27) RX(28) RX(29)
    RX(30) RX(31)
    "<reg name=\"ppc\" bitsize=\"32\" type=\"code_ptr\"/>"
    "<reg name=\"npc\" bitsize=\"32\" type=\"code_ptr\"/>"
    "<reg name=\"sr\" bitsize=\"32\"/></feature></target>";
#undef RX
static void dispatch(Rsp *r)
{
    u8 *p=r->packet;
    u8 out[RSP_CAP];
    const u8 *arg;
    u32 address,n,value;
    unsigned i;
    if (starts(p,"qSupported")) { text(r,"PacketSize=400;qXfer:features:read+"); return; }
    if (!r->enabled) { text(r,"E22"); return; }
    if (starts(p,"qXfer:features:read:target.xml:")) {
        arg=p+length("qXfer:features:read:target.xml:");
        if (!number(&arg,&address) || *arg++!=',' || !number(&arg,&n) || *arg || !n ||
            address>sizeof(target_xml)-1) { text(r,"E01"); return; }
        if (n>RSP_CAP-1) { n=RSP_CAP-1; }
        if (n>sizeof(target_xml)-1-address) { n=sizeof(target_xml)-1-address; }
        out[0]=address+n<sizeof(target_xml)-1 ? 'm' : 'l';
        for (i=0; i<n; i++) { out[i+1]=target_xml[address+i]; }
        reply(r,out,n+1); return;
    }
    if (p[0]=='?' && !p[1]) { text(r,"S05"); return; }
    if (starts(p,"qAttached")) { text(r,"1"); return; }
    if (starts(p,"qfThreadInfo")) { text(r,"m1"); return; }
    if (starts(p,"qsThreadInfo")) { text(r,"l"); return; }
    if (starts(p,"qC") && !p[2]) { text(r,"QC1"); return; }
    if (starts(p,"Hg0") || starts(p,"Hc0") || starts(p,"Hg1") || starts(p,"Hc1") || starts(p,"Hc-1")) { text(r,"OK"); return; }
    if (p[0]=='g' && !p[1]) {
        if (!r->ops.reg_read) { text(r,"E01"); return; }
        for (i=0; i<RSP_REGS; i++) {
            if (!r->ops.reg_read(r->ops.context,i,&value)) { text(r,"E01"); return; }
            put_hex(out+i*8,value);
        }
        reply(r,out,RSP_REGS*8); return;
    }
    if (p[0]=='p' || p[0]=='P') {
        arg=p+1;
        if (!number(&arg,&n) || n>=RSP_REGS) { text(r,"E01"); return; }
        if (p[0]=='p') {
            if (*arg || !r->ops.reg_read || !r->ops.reg_read(r->ops.context,n,&value)) { text(r,"E01"); return; }
            put_hex(out,value); reply(r,out,8); return;
        }
        if (*arg++!='=' || length((const char *)arg)!=8) { text(r,"E01"); return; }
        value=0;
        for (i=0; i<4; i++) {
            int hi=digit(arg[i*2]), lo=digit(arg[i*2+1]);
            if (hi<0 || lo<0) { text(r,"E01"); return; }
            value|=(u32)((hi<<4)|lo)<<(i*8);
        }
        text(r,r->ops.reg_write && r->ops.reg_write(r->ops.context,n,value) ? "OK" : "E02"); return;
    }
    if (p[0]=='m' || p[0]=='M') {
        arg=p+1;
        if (!number(&arg,&address) || *arg++!=',' || !number(&arg,&n) || n>256 ||
            n>0xffffffffu-address) { text(r,"E01"); return; }
        if (p[0]=='m') {
            u8 bytes[256];
            if (*arg || !r->ops.mem_read || !r->ops.mem_read(r->ops.context,address,bytes,n)) { text(r,"E02"); return; }
            for (i=0; i<n; i++) { out[i*2]=digits[bytes[i]>>4]; out[i*2+1]=digits[bytes[i]&15]; }
            reply(r,out,n*2); return;
        }
        if (*arg++!=':' || length((const char *)arg)!=n*2) { text(r,"E01"); return; }
        for (i=0; i<n; i++) {
            int hi=digit(arg[i*2]),lo=digit(arg[i*2+1]);
            if (hi<0 || lo<0) { text(r,"E01"); return; }
            out[i]=(hi<<4)|lo;
        }
        text(r,r->ops.mem_write && r->ops.mem_write(r->ops.context,address,out,n) ? "OK" : "E02"); return;
    }
    if (p[0]=='c' || p[0]=='s') {
        arg=p+1;
        address=0;
        if (*arg && (!number(&arg,&address) || *arg)) { text(r,"E01"); return; }
        if (!r->ops.resume || !r->ops.resume(r->ops.context,p[0]=='s',p[1]!=0,address)) { text(r,"E02"); }
        /* Success has no immediate response; the next stop emits Sxx. */
        return;
    }
    if (p[0]=='D' && !p[1]) {
        /* Don't report a successful detach unless execution can resume. */
        if (!r->ops.resume || !r->ops.resume(r->ops.context,0,0,0)) { text(r,"E02"); }
        else { text(r,"OK"); r->enabled=0; }
        return;
    }
    text(r,""); /* Unsupported, never advertise an unimplemented capability. */
}
void rsp_init(Rsp *r, const DebugOps *ops)
{
    *r=(Rsp){.ops=*ops};
}
void rsp_stop(Rsp *r, unsigned signal)
{
    u8 message[3]={'S',digits[(signal>>4)&15],digits[signal&15]};
    if (r->enabled) { reply(r,message,3); }
}
void rsp_feed(Rsp *r, u8 byte)
{
    if (byte=='$' && !r->escaped) {
        r->state=1; r->length=0; r->checksum=0; r->overflow=0; return;
    }
    if (!r->state) {
        if (byte=='-' && r->last_size) { send(r,r->last,r->last_size); }
        if (byte==3 && r->enabled && r->ops.interrupt) { r->ops.interrupt(r->ops.context); }
        return;
    }
    if (r->state==1) {
        if (byte=='#' && !r->escaped) { r->state=2; return; }
        r->checksum=(r->checksum+byte)&255;
        if (byte=='}' && !r->escaped) { r->escaped=1; return; }
        if (r->escaped) { byte^=0x20; r->escaped=0; }
        if (r->length>=RSP_CAP-1 || !byte) { r->overflow=1; }
        else { r->packet[r->length++]=byte; }
        return;
    }
    if (r->state==2) {
        int n=digit(byte);
        if (n<0) { r->state=0; send(r,(const u8 *)"-",1); return; }
        r->high=(unsigned)n<<4; r->state=3; return;
    }
    {
        int n=digit(byte);
        r->state=0;
        if (n<0 || (r->high|(unsigned)n)!=r->checksum || r->overflow) { send(r,(const u8 *)"-",1); return; }
        send(r,(const u8 *)"+",1);
        r->packet[r->length]=0;
        dispatch(r);
    }
}
