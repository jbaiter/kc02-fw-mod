/* SPDX-License-Identifier: MIT */
#include "ax_usb.h"
#include "msc.h"
enum { IDLE, STATUS_IN, DATA_IN, DATA_IN_MORE, STATUS_OUT };
static u32 rd(AxUsb *a, unsigned r) { return a->io.read(a->io.context,r); }
static void wr(AxUsb *a, unsigned r, u32 v) { a->io.write(a->io.context,r,v); }
static void address(AxUsb *a, unsigned r, u8 *p)
{
    wr(a,r,a->io.dma_address(a->io.context,p));
}
static void endpoint_config(AxUsb *a, AxUsbEndpoint *e, unsigned number)
{
    wr(a,AX_USB_INDEX,number);
    wr(a,AX_USB_TXMAX_L,USB_DEVICE_PACKET); wr(a,AX_USB_TXMAX_H,0);
    wr(a,AX_USB_RXMAX_L,USB_DEVICE_PACKET); wr(a,AX_USB_RXMAX_H,0);
    wr(a,AX_USB_TXCSR_H,0x20); /* Peripheral bulk transmit mode. */
    wr(a,AX_USB_RXCSR_H,0);
    wr(a,AX_USB_TXCSR,0x48);   /* Flush TX FIFO, clear data toggle. */
    wr(a,AX_USB_RXCSR,0x90);   /* Flush RX FIFO, clear data toggle. */
    wr(a,AX_USB_EP_TX_REG(number),(u32)a->io.dma_address(a->io.context,e->packet_tx));
    wr(a,AX_USB_EP_RX_REG(number),(u32)a->io.dma_address(a->io.context,e->packet_rx));
    e->number=number;
    e->transmitting=0;
    e->halt_in=e->halt_out=0;
}
static void endpoints(AxUsb *a)
{
    endpoint_config(a,&a->msc,1);
    endpoint_config(a,&a->debug,2);
    endpoint_config(a,&a->update,3);
    wr(a,AX_USB_INDEX,0);
    address(a,AX_USB_EP0,a->ep0);
}
static void reset(AxUsb *a)
{
    usb_device_reset(a->usb);
    a->state=IDLE;
    wr(a,AX_USB_ADDRESS,0);
    endpoints(a);
}
void ax_usb_init(AxUsb *a, UsbDevice *u, const AxUsbIo *io)
{
    *a=(AxUsb){0}; a->usb=u; a->io=*io;
    wr(a,AX_USB_POWER,0); /* Remain detached during configuration. */
    wr(a,AX_USB_IE_TX,0); wr(a,AX_USB_IE_RX,0); wr(a,AX_USB_IE,0);
    reset(a);
    (void)rd(a,AX_USB_INTR); (void)rd(a,AX_USB_INTR_TX); (void)rd(a,AX_USB_INTR_RX);
    /* Full speed only. No HS enable, and no 64-byte HS bulk descriptor. */
    if (u->vid && u->pid) { wr(a,AX_USB_POWER,0x40); }
}
/* Sends the next chunk of a control reply. Only the last chunk carries
 * DATAEND, so a descriptor longer than one packet is a sequence rather than a
 * single truncated transfer. */
static void ep0_send_chunk(AxUsb *a)
{
    const u8 *reply=usb_device_response(a->usb,&a->response_size);
    unsigned remaining=a->response_size-a->response_sent;
    unsigned take=remaining>USB_DEVICE_PACKET ? USB_DEVICE_PACKET : remaining;
    unsigned i;
    for (i=0; i<take; i++) { a->ep0[i]=reply[a->response_sent+i]; }
    address(a,AX_USB_EP0,a->ep0);
    wr(a,AX_USB_CON1,(1u<<13)|take);
    a->response_sent+=take;
    a->state=take==remaining ? DATA_IN : DATA_IN_MORE;
    wr(a,AX_USB_CSR0,(unsigned)(0x02 | (take==remaining ? 0x08 : 0)));
}
static void control(AxUsb *a, unsigned tx_irq)
{
    unsigned csr, count, effects;
    u8 s[8];
    int n;
    wr(a,AX_USB_INDEX,0); csr=rd(a,AX_USB_CSR0);
    if (csr&0x10) { /* SETUPEND aborts the old pending address/configuration. */
        wr(a,AX_USB_CSR0,0x80); a->usb->pending=0; a->state=IDLE;
        csr=rd(a,AX_USB_CSR0);
    }
    if (csr&4) { wr(a,AX_USB_CSR0,0); a->usb->pending=0; a->state=IDLE; }
    if (a->state==STATUS_IN && tx_irq) {
        effects=usb_device_status_complete(a->usb);
        wr(a,AX_USB_ADDRESS,a->usb->address);
        if (effects&1) { endpoints(a); }  /* SET_CONFIGURATION resets toggles. */
        else if (effects&2) {             /* Revoked: stop both bulk pipes. */
            ax_usb_msc_stall(a,0);
            ax_usb_msc_stall(a,1);
        }
        if (effects&12) {
            wr(a,AX_USB_INDEX,2);
            if (effects&4) { wr(a,AX_USB_TXCSR,0x48); a->debug.transmitting=0; }
            if (effects&8) { wr(a,AX_USB_RXCSR,0x90); }
            wr(a,AX_USB_INDEX,0);
        }
        a->state=IDLE;
    }
    if (a->state==DATA_IN_MORE && tx_irq) { ep0_send_chunk(a); }
    else if (a->state==DATA_IN && tx_irq && !(csr&2)) { a->state=STATUS_OUT; }
    if (!(csr&1)) { return; } /* CSR0 RX ready is bit 0, TX ready is bit 1. */
    count=rd(a,AX_USB_COUNT_L);
    if (a->state==STATUS_OUT && !count) {
        wr(a,AX_USB_CSR0,0x40); a->state=IDLE; return;
    }
    if (count!=8 || a->state!=IDLE) {
        wr(a,AX_USB_CSR0,0x60); a->usb->pending=0; a->state=IDLE; return;
    }
    for (count=0; count<8; count++) { s[count]=a->ep0[count]; }
    n=usb_device_setup(a->usb,s);
    if (n<0) { wr(a,AX_USB_CSR0,0x60); return; } /* service RX + send stall */
    if (!(s[0]&0x80)) {
        /* No data stage: the zero-length IN is the status stage, and the
         * command takes effect when the host has acknowledged it. */
        a->state=STATUS_IN;
        wr(a,AX_USB_CSR0,0x48); /* Service RX + DATAEND: automatic IN status. */
        return;
    }
    wr(a,AX_USB_CSR0,0x40);
    if (n==0) { a->state=STATUS_IN; wr(a,AX_USB_CSR0,0x48); return; }
    a->response_sent=0;
    ep0_send_chunk(a);
}
/* Shared per-endpoint plumbing: stall transitions, packet collection and TX
 * completion.
 *
 * The endpoint is chosen by its interrupt bit, not by trying each one in turn.
 * A controller has one RXCSR visible per index, so guessing would let whichever
 * endpoint is serviced first take a packet that belongs to the other. Only the
 * debug endpoint is filled from here; the BOT engine stages its own bytes. */
static void endpoint_service(AxUsb *a, AxUsbEndpoint *e, unsigned in_halted,
                             unsigned out_halted, int collect,
                             unsigned rx_irq, unsigned tx_irq)
{
    unsigned csr, n;
    wr(a,AX_USB_INDEX,e->number);
    if (e->halt_in!=in_halted) {
        wr(a,AX_USB_TXCSR,in_halted ? 0x10 : 0x48);
        e->halt_in=in_halted;
        e->transmitting=0;
    }
    if (e->halt_out!=out_halted) {
        wr(a,AX_USB_RXCSR,out_halted ? 0x20 : 0x90);
        e->halt_out=out_halted;
    }
    if (tx_irq && e->transmitting) { e->transmitting=0; }
    if (!rx_irq) { return; }
    csr=rd(a,AX_USB_RXCSR);
    if ((csr&1) && !out_halted) {
        n=rd(a,AX_USB_COUNT_L)|(rd(a,AX_USB_COUNT_H)<<8);
        if (n>USB_DEVICE_PACKET) {
            /* The controller never delivers more than the endpoint's packet
             * size, so this is a fault in the model or the wiring, not
             * something to ignore. */
            a->usb->fault=1;
        } else if (collect) {
            usb_device_receive(a->usb,e->packet_rx,n);
        } else if (e->rx_used+n<=AX_EP_QUEUE) {
            unsigned i;
            for (i=0; i<n; i++) { e->rx[e->rx_used+i]=e->packet_rx[i]; }
            e->rx_used+=n;
        } else {
            /* The engine did not drain in time. Losing received bytes would
             * corrupt a write, so fail the pipe instead. */
            e->fault=1;
            e->halt_out=1;
            wr(a,AX_USB_RXCSR,0x20);
        }
        address(a,AX_USB_EP_RX_REG(e->number),e->packet_rx);
        wr(a,AX_USB_RXCSR,0);
    }
}
static void msc_service(AxUsb *a, unsigned tx_done)
{
    AxUsbEndpoint *e=&a->msc;
    unsigned n;
    /* Completion must be accounted for before deciding whether to send again,
     * otherwise the same packet goes out twice. */
    if (tx_done && e->transmitting) {
        e->tx_sent+=e->transmitting;
        e->transmitting=0;
    }
    if (e->tx_sent==e->tx_used) {
        e->tx_sent=e->tx_used=0;   /* Buffer drained: start from the front. */
    }
    n=e->tx_used-e->tx_sent;
    if (n>USB_DEVICE_PACKET) { n=USB_DEVICE_PACKET; }
    if (!e->transmitting && !e->fault && n && !e->halt_in) {
        wr(a,AX_USB_INDEX,e->number);
        address(a,AX_USB_EP_TX_REG(e->number),e->tx+e->tx_sent);
        wr(a,AX_USB_CON1,(1u<<(13+e->number))|n);
        e->transmitting=n;
        wr(a,AX_USB_TXCSR,1);
    }
}
void ax_usb_poll(AxUsb *a)
{
    unsigned intr=rd(a,AX_USB_INTR), tx=rd(a,AX_USB_INTR_TX);
    unsigned rx=rd(a,AX_USB_INTR_RX), n;
    if (intr&(4|0x20)) { reset(a); return; }
    if (intr&1) {
        /* Suspend is not a bus reset: keep address, configuration and toggles,
         * but revoke debug, because nobody is inspecting a suspended target. */
        if (a->usb->revoke) { a->usb->revoke(a->usb->context); }
        a->usb->queued=a->usb->consumed=0;
        return;
    }
    control(a,tx&1);
    if (!a->usb->configured) { return; }
    /* The storage endpoint's TX completion is handled inside msc_service, so
     * the byte counter advances in the same step as the flag. */
    endpoint_service(a,&a->msc,a->msc.stalled_in,a->msc.stalled_out,0,rx&(1u<<1),0);
    msc_service(a,tx&(1u<<1));
    endpoint_service(a,&a->update,0,0,0,rx&(1u<<3),0);
    /* The debug endpoint reads straight out of the device queue, and only
     * releases a packet once the hardware says it went out. */
    n=a->debug.transmitting ? 0 : usb_device_peek(a->usb,a->debug.packet_tx);
    endpoint_service(a,&a->debug,usb_device_halted(a->usb,USB_DEVICE_DEBUG_IN),
                     usb_device_halted(a->usb,USB_DEVICE_DEBUG_OUT),1,
                     rx&(1u<<2),tx&(1u<<2));
    if (n && !a->debug.halt_in && !a->debug.transmitting) {
        wr(a,AX_USB_INDEX,a->debug.number);
        address(a,AX_USB_EP_TX_REG(a->debug.number),a->debug.packet_tx);
        wr(a,AX_USB_CON1,(1u<<(13+a->debug.number))|n);
        a->debug.transmitting=n;
        wr(a,AX_USB_TXCSR,1);
        usb_device_sent(a->usb,n);
    }
    wr(a,AX_USB_INDEX,0);
    /* A faulted transport must not leave a debug session alive. */
    if (a->usb->fault && a->usb->revoke) { a->usb->revoke(a->usb->context); }
}
int ax_usb_msc_send(void *context, const u8 *data, unsigned size)
{
    AxUsb *a=context;
    AxUsbEndpoint *e=&a->msc;
    unsigned i;
    if (e->fault || e->stalled_in) { return -1; }
    if (size>AX_EP_QUEUE-e->tx_used) { return 0; }   /* busy, not an error */
    for (i=0; i<size; i++) { e->tx[e->tx_used+i]=data[i]; }
    e->tx_used+=size;
    return 1;
}
int ax_usb_msc_receive(void *context, u8 *out, unsigned size)
{
    AxUsb *a=context;
    AxUsbEndpoint *e=&a->msc;
    unsigned have, n, i;
    if (e->fault || e->stalled_out) { return -1; }
    have=e->rx_used-e->rx_read;
    n=have<size ? have : size;
    for (i=0; i<n; i++) { out[i]=e->rx[e->rx_read+i]; }
    e->rx_read+=n;
    if (e->rx_read==e->rx_used) { e->rx_read=e->rx_used=0; }
    return (int)n;
}
int ax_usb_msc_cbw_ready(AxUsb *a)
{
    return a->msc.rx_used-a->msc.rx_read>=MSC_CBW_SIZE && !a->msc.fault;
}
unsigned ax_usb_update_available(AxUsb *a)
{
    return a->update.rx_used-a->update.rx_read;
}
int ax_usb_update_receive(AxUsb *a, u8 *out, unsigned size)
{
    AxUsbEndpoint *e=&a->update;
    unsigned have=e->rx_used-e->rx_read, n, i;
    n=have<size ? have : size;
    for (i=0; i<n; i++) { out[i]=e->rx[e->rx_read+i]; }
    e->rx_read+=n;
    if (e->rx_read==e->rx_used) { e->rx_read=e->rx_used=0; }
    return (int)n;
}
void ax_usb_msc_stall(void *context, int in)
{
    AxUsb *a=context;
    if (in) { a->msc.stalled_in=1; } else { a->msc.stalled_out=1; }
}
void ax_usb_msc_reset(void *context)
{
    AxUsb *a=context;
    a->msc.stalled_in=a->msc.stalled_out=0;
    a->msc.tx_used=a->msc.tx_sent=0;
    a->msc.rx_used=a->msc.rx_read=0;
    a->msc.transmitting=0;
    a->msc.fault=0;
}
