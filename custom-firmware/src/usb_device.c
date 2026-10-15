/* SPDX-License-Identifier: MIT */
#include "usb_device.h"
enum { NONE, ADDRESS, CONFIG, HALT, CLEAR, REVOKE, BOT_RESET };
static unsigned word(const u8 *p) { return p[0]|((unsigned)p[1]<<8); }
/* Index into UsbDevice.halt, or -1 for an endpoint this device does not have. */
static int endpoint_index(unsigned endpoint)
{
    switch (endpoint) {
    case USB_DEVICE_MSC_IN: return 0;
    case USB_DEVICE_MSC_OUT: return 1;
    case USB_DEVICE_DEBUG_IN: return 2;
    case USB_DEVICE_DEBUG_OUT: return 3;
    default: return -1;
    }
}
static int descriptor(UsbDevice *u, unsigned value, unsigned language, u8 out[64])
{
    static const u8 device[]={18,1,0x10,1,0,0,0,64,0,0,0,0,0,1,0,1,0,1};
    /* Two interfaces, each with its own class: mass storage then vendor debug.
     * The debug function is deliberately not the storage function, because a
     * browser may claim vendor interfaces but never class 08. */
    static const u8 config[]={
        9,2,USB_DEVICE_CONFIG_BYTES,0,2,1,0,0x80,50,
        9,4,USB_DEVICE_MSC_INTERFACE,0,2,0x08,0x06,0x50,0,
        7,5,USB_DEVICE_MSC_IN,2,USB_DEVICE_PACKET,0,0,
        7,5,USB_DEVICE_MSC_OUT,2,USB_DEVICE_PACKET,0,0,
        9,4,USB_DEVICE_DEBUG_INTERFACE,0,2,0xff,0x4c,1,0,
        7,5,USB_DEVICE_DEBUG_IN,2,USB_DEVICE_PACKET,0,0,
        7,5,USB_DEVICE_DEBUG_OUT,2,USB_DEVICE_PACKET,0,0};
    static const char product[]="Luma KC02 bring-up";
    unsigned i, n=0, type=value>>8, index=value&255;
    const u8 *p=0;
    if (sizeof(config)!=USB_DEVICE_CONFIG_BYTES) { return -1; }
    if (type==1 && !index && !language) { p=device; n=sizeof(device); }
    if (type==2 && !index && !language) { p=config; n=sizeof(config); }
    if (p) {
        for (i=0; i<n; i++) { out[i]=p[i]; }
        if (type==1) { out[8]=u->vid; out[9]=u->vid>>8; out[10]=u->pid; out[11]=u->pid>>8; }
        return (int)n;
    }
    if (type==3 && index==0 && !language) { out[0]=4; out[1]=3; out[2]=9; out[3]=4; return 4; }
    if (type==3 && index==1 && language==0x409) {
        n=2+2*(sizeof(product)-1); out[0]=(u8)n; out[1]=3;
        for (i=0; i<sizeof(product)-1; i++) { out[2+i*2]=product[i]; out[3+i*2]=0; }
        return (int)n;
    }
    return -1;
}
void usb_device_reset(UsbDevice *u)
{
    unsigned i;
    u->address=u->configured=0;
    for (i=0; i<USB_DEVICE_ENDPOINTS; i++) { u->halt[i]=0; }
    u->pending=u->value=u->queued=u->consumed=u->fault=0;
    if (u->revoke) { u->revoke(u->context); }
    if (u->bus_reset) { u->bus_reset(u->context); }
}
void usb_device_init(UsbDevice *u, u16 vid, u16 pid, void *context,
                     void (*receive)(void *, const u8 *, unsigned), void (*revoke)(void *))
{
    *u=(UsbDevice){0}; u->vid=vid; u->pid=pid; u->context=context;
    u->receive=receive; u->revoke=revoke;
    usb_device_reset(u);
}
unsigned usb_device_halted(const UsbDevice *u, unsigned endpoint)
{
    int index=endpoint_index(endpoint);
    return index<0 ? 1 : u->halt[index];
}
int usb_device_setup(UsbDevice *u, const u8 s[8], u8 out[64])
{
    unsigned type=s[0], request=s[1], value=word(s+2), index=word(s+4), n=word(s+6);
    int slot, size=-1;
    u->pending=NONE; /* A new SETUP aborts the preceding control transfer. */
    if (!u->vid || !u->pid) { return -1; }
    /* Vendor message from the debug interface only. It revokes access and never
     * grants it, and it cannot reach the storage function. */
    if (type==0x40 && request==USB_DEVICE_REVOKE && !value && !n && u->configured &&
        index==USB_DEVICE_DEBUG_INTERFACE) {
        u->pending=REVOKE; return 0;
    }
    /* Class requests a mass-storage host sends, addressable only through the
     * storage interface. */
    if (type==0xa1 && request==USB_DEVICE_GET_MAX_LUN && !value && n==1 &&
        index==USB_DEVICE_MSC_INTERFACE && u->configured) {
        out[0]=0;               /* One LUN. */
        return 1;
    }
    if (type==0x21 && request==USB_DEVICE_BOT_RESET && !value && !index && !n &&
        u->configured) {
        u->pending=BOT_RESET; return 0;
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
        out[0]=(u8)u->configured; return 1;
    }
    if (request==0 && !value && n==2) {
        /* GET_STATUS: device, interface, or endpoint. */
        if (type==0x80 && !index) { size=0; }                       /* self-powered, no remote wakeup */
        if (type==0x81 && index<2 && u->configured) { size=0; }      /* both interfaces */
        if (type==0x82) {
            slot=endpoint_index(index);
            if (slot>=0 && u->configured) { size=u->halt[slot]; }
        }
        if (size>=0) { out[0]=(u8)size; out[1]=0; return 2; }
    }
    if (type==0x81 && request==10 && !value && index<2 && n==1 && u->configured) {
        out[0]=0;               /* Both interfaces have only alt setting 0. */
        return 1;
    }
    if (type==1 && request==11 && !value && index<2 && !n && u->configured) { return 0; }
    if (type==2 && (request==1 || request==3) && !value && !n && u->configured) {
        slot=endpoint_index(index);
        if (slot>=0) {
            u->pending=HALT;
            u->halt_slot=(unsigned)slot;
            u->halt_set=request==3;
            return 0;
        }
    }
    return -1; /* No vendor command can grant debug, format media, or touch NOR. */
}
int usb_device_status_complete(UsbDevice *u)
{
    int effects=u->pending==CONFIG ? 1 : u->pending==REVOKE ? 2 : 0;
    unsigned i;
    switch (u->pending) {
    case ADDRESS: u->address=u->value; break;
    case CONFIG:
        u->configured=u->value;
        for (i=0; i<USB_DEVICE_ENDPOINTS; i++) { u->halt[i]=0; }
        u->queued=u->consumed=u->fault=0;
        if (u->revoke) { u->revoke(u->context); }
        if (u->on_configured) { u->on_configured(u->context); }
        break;
    case REVOKE:
        u->queued=u->consumed=u->fault=0;
        if (u->revoke) { u->revoke(u->context); }
        break;
    case BOT_RESET:
        /* The BOT engine aborts its command and clears its own stalls. */
        if (u->bot_reset) { u->bot_reset(u->context); }
        break;
    case HALT:
        /* Clearing a halt also resets the data toggle, which is what the
         * endpoint layer watches for. It does so even when the endpoint was
         * not halted, which is why this is an effect and not a state change. */
        u->halt[u->halt_slot]=u->halt_set;
        if (!u->halt_set) { effects|=u->halt_slot==2 ? 4 : u->halt_slot==3 ? 8 : 0; }
        break;
    default: break;
    }
    u->pending=NONE;
    return effects;
}
int usb_device_send(UsbDevice *u, const u8 *p, unsigned n)
{
    unsigned i, available=USB_DEVICE_QUEUE-(u->queued-u->consumed);
    if (!u->configured || u->fault || n>available) { u->fault=1; return 0; }
    for (i=0; i<n; i++) { u->tx[(u->queued+i)%USB_DEVICE_QUEUE]=p[i]; }
    u->queued+=n;
    return 1;
}
unsigned usb_device_peek(UsbDevice *u, u8 out[USB_DEVICE_PACKET])
{
    unsigned i, n=u->queued-u->consumed;
    if (!u->configured || u->fault || u->halt[2]) { return 0; }
    if (n>USB_DEVICE_PACKET) { n=USB_DEVICE_PACKET; }
    for (i=0; i<n; i++) { out[i]=u->tx[(u->consumed+i)%USB_DEVICE_QUEUE]; }
    return n;
}
void usb_device_sent(UsbDevice *u, unsigned n)
{
    if (n>u->queued-u->consumed) { u->fault=1; return; }
    u->consumed+=n;
    /* Keep counters bounded without changing the ring's modulo position. */
    if (u->consumed>=USB_DEVICE_QUEUE) {
        u->consumed-=USB_DEVICE_QUEUE;
        u->queued-=USB_DEVICE_QUEUE;
    }
}
int usb_device_receive(UsbDevice *u, const u8 *p, unsigned n)
{
    if (!u->configured || u->fault || u->halt[3] || n>USB_DEVICE_PACKET) { return 0; }
    if (u->receive) { u->receive(u->context,p,n); }
    return !u->fault;
}
