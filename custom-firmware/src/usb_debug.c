/* SPDX-License-Identifier: MIT */
#include "usb_debug.h"
enum { NONE, ADDRESS, CONFIG, HALT_IN, HALT_OUT, CLEAR_IN, CLEAR_OUT, REVOKE };
static unsigned word(const u8 *p) { return p[0]|((unsigned)p[1]<<8); }
static int descriptor(UsbDebug *u, unsigned value, unsigned language, u8 out[64])
{
    static const u8 device[]={18,1,0x10,1,0,0,0,64,0,0,0,0,0,1,0,1,0,1};
    static const u8 config[]={9,2,32,0,1,1,0,0x80,50,
                             9,4,0,0,2,0xff,0x4c,1,0,
                             7,5,0x81,2,64,0,0,7,5,1,2,64,0,0};
    static const char product[]="Luma KC02 bring-up";
    unsigned i, n=0, type=value>>8, index=value&255;
    const u8 *p=0;
    if (type==1 && !index && !language) { p=device; n=sizeof(device); }
    if (type==2 && !index && !language) { p=config; n=sizeof(config); }
    if (p) {
        for (i=0; i<n; i++) { out[i]=p[i]; }
        if (type==1) { out[8]=u->vid; out[9]=u->vid>>8; out[10]=u->pid; out[11]=u->pid>>8; }
        return (int)n;
    }
    if (type==3 && index==0 && !language) { out[0]=4; out[1]=3; out[2]=9; out[3]=4; return 4; }
    if (type==3 && index==1 && language==0x409) {
        n=2+2*(sizeof(product)-1); out[0]=n; out[1]=3;
        for (i=0; i<sizeof(product)-1; i++) { out[2+i*2]=product[i]; out[3+i*2]=0; }
        return (int)n;
    }
    return -1;
}
void usb_debug_reset(UsbDebug *u)
{
    u->address=u->configured=u->halt_in=u->halt_out=0;
    u->pending=u->value=u->queued=u->consumed=u->fault=0;
    if (u->revoke) { u->revoke(u->context); }
}
void usb_debug_init(UsbDebug *u, u16 vid, u16 pid, void *context,
                    void (*receive)(void *, const u8 *, unsigned), void (*revoke)(void *))
{
    *u=(UsbDebug){0}; u->vid=vid; u->pid=pid; u->context=context;
    u->receive=receive; u->revoke=revoke;
    usb_debug_reset(u);
}
int usb_debug_setup(UsbDebug *u, const u8 s[8], u8 out[64])
{
    unsigned type=s[0], request=s[1], value=word(s+2), index=word(s+4), n=word(s+6);
    int size=-1;
    u->pending=NONE; /* A new SETUP aborts the preceding control transfer. */
    if (!u->vid || !u->pid) { return -1; }
    if (type==0x40 && request==USB_DEBUG_REVOKE && !value && !index && !n && u->configured) {
        u->pending=REVOKE; return 0;
    }
    if (type==0x80 && request==6) {
        size=descriptor(u,value,index,out);
        return size<0 ? -1 : (unsigned)size>n ? (int)n : size;
    }
    if (type==0 && request==5 && value<128 && !index && !n && !u->configured) {
        u->pending=ADDRESS; u->value=value; return 0;
    }
    if (type==0 && request==9 && value<=1 && !index && !n && u->address) {
        u->pending=CONFIG; u->value=value; return 0;
    }
    if (type==0x80 && request==8 && !value && !index && n==1 && u->address) {
        out[0]=u->configured; return 1;
    }
    if (request==0 && !value && n==2) {
        if (type==0x80 && !index) { size=0; }
        if (type==0x81 && !index && u->configured) { size=0; }
        if (type==0x82 && (!index || index==0x80)) { size=0; }
        if (type==0x82 && u->configured && index==USB_DEBUG_IN) { size=u->halt_in; }
        if (type==0x82 && u->configured && index==USB_DEBUG_OUT) { size=u->halt_out; }
        if (size>=0) { out[0]=(u8)size; out[1]=0; return 2; }
    }
    if (type==0x81 && request==10 && !value && !index && n==1 && u->configured) { out[0]=0; return 1; }
    if (type==1 && request==11 && !value && !index && !n && u->configured) { return 0; }
    if (type==2 && (request==1 || request==3) && !value && !n && u->configured) {
        if (index==USB_DEBUG_IN) { u->pending=request==1 ? CLEAR_IN : HALT_IN; return 0; }
        if (index==USB_DEBUG_OUT) { u->pending=request==1 ? CLEAR_OUT : HALT_OUT; return 0; }
    }
    return -1; /* No vendor command can grant debug or program NOR. */
}
int usb_debug_status_complete(UsbDebug *u)
{
    int effects=u->pending==CONFIG ? 1 : u->pending==REVOKE ? 2 :
                u->pending==CLEAR_IN ? 4 : u->pending==CLEAR_OUT ? 8 : 0;
    switch (u->pending) {
    case ADDRESS: u->address=u->value; break;
    case CONFIG:
        u->configured=u->value; u->halt_in=u->halt_out=0;
        u->queued=u->consumed=u->fault=0;
        if (u->revoke) { u->revoke(u->context); }
        break;
    case REVOKE:
        u->queued=u->consumed=u->fault=0;
        if (u->revoke) { u->revoke(u->context); }
        break;
    case HALT_IN: u->halt_in=1; break;
    case HALT_OUT: u->halt_out=1; break;
    case CLEAR_IN: u->halt_in=0; break;
    case CLEAR_OUT: u->halt_out=0; break;
    default: break;
    }
    u->pending=NONE;
    return effects;
}
int usb_debug_send(UsbDebug *u, const u8 *p, unsigned n)
{
    unsigned i, available=USB_DEBUG_QUEUE-(u->queued-u->consumed);
    if (!u->configured || u->fault || n>available) { u->fault=1; return 0; }
    for (i=0; i<n; i++) { u->tx[(u->queued+i)%USB_DEBUG_QUEUE]=p[i]; }
    u->queued+=n;
    return 1;
}
unsigned usb_debug_peek(UsbDebug *u, u8 out[64])
{
    unsigned i, n=u->queued-u->consumed;
    if (!u->configured || u->halt_in || u->fault) { return 0; }
    if (n>64) { n=64; }
    for (i=0; i<n; i++) { out[i]=u->tx[(u->consumed+i)%USB_DEBUG_QUEUE]; }
    return n;
}
void usb_debug_sent(UsbDebug *u, unsigned n)
{
    if (n>u->queued-u->consumed) { u->fault=1; return; }
    u->consumed+=n;
    /* Keep counters bounded without changing the ring's modulo position. */
    if (u->consumed>=USB_DEBUG_QUEUE) { u->consumed-=USB_DEBUG_QUEUE; u->queued-=USB_DEBUG_QUEUE; }
}
int usb_debug_receive(UsbDebug *u, const u8 *p, unsigned n)
{
    if (!u->configured || u->halt_out || u->fault || n>64) { return 0; }
    if (u->receive) { u->receive(u->context,p,n); }
    return !u->fault;
}
