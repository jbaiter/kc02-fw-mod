#include "common.h"

#if CASE == 6
static NOINLINE u32 leaf(u32 x) { return (x ^ 0x13579bdfUL) + 17; }
static NOINLINE u32 tail(u32 x) { return leaf(x); }
static NOINLINE u32 nested(u32 a, u32 b)
{
    u32 i, r = a;
    for (i = 0; i < 8; ++i) {
        if ((r ^ i) & 1) r = leaf(r + b); else r = leaf(r - b);
    }
    return tail(r);
}
#elif CASE == 7
extern void probe_preserved(const volatile u32 *, volatile u32 *);
extern void clobber_caller(void);
NOINLINE u32 pressure(u32 x)
{
    u32 a=x+1, b=x+2, c=x+3, d=x+4, e=x+5, f=x+6, g=x+7, h=x+8, i=x+9;
    __asm__ volatile ("" : "+r"(a), "+r"(b), "+r"(c), "+r"(d), "+r"(e),
                             "+r"(f), "+r"(g), "+r"(h), "+r"(i));
    clobber_caller();
    return a+b+c+d+e+f+g+h+i;
}
#elif CASE == 8
extern u32 assembly_vr(void);
#elif CASE == 18
/* Deterministic addressing forms for the SPR probe. The C front end may emit
   either form, so both are written explicitly here to compare them. */
#define SPR_IMM(name,addr) \
static ALWAYS_INLINE u32 name(void) \
{ u32 v; __asm__ volatile ("l.mfspr %0,r0," #addr : "=r"(v)); return v; }
SPR_IMM(spr_imm_0,0x0)
SPR_IMM(spr_imm_18,0x18)
SPR_IMM(spr_imm_2000,0x2000)
SPR_IMM(spr_imm_480c,0x480c)
static ALWAYS_INLINE u32 spr_reg(u32 address)
{
    u32 v;
    __asm__ volatile ("l.mfspr %0,%1,0" : "=r"(v) : "r"(address));
    return v;
}
#elif CASE == 10
extern u64 oracle_wide(u64, u32, u64, u32, u64);
extern u64 call_c_wide(const volatile u32 *);
NOINLINE u64 c_wide(u64 a, u32 b, u64 c, u32 d, u64 e)
{
    return ((u64)((u32)(a >> 32) ^ (u32)(c >> 32) ^ (u32)(e >> 32) ^ 0x12345678UL) << 32)
           | ((u32)a ^ (u32)c ^ (u32)e ^ b ^ d);
}
#elif CASE == 11
struct pair { u32 a, b; };
struct big { u32 a[8]; };
extern struct pair oracle_pair(u32, u32);
extern struct big oracle_big(u32);
extern void call_c_pair(volatile u32 *, u32, u32);
extern void call_c_big(volatile u32 *, u32);
NOINLINE struct pair c_pair(u32 a, u32 b)
{
    struct pair p = { a ^ 0x12345678UL, b + 5 };
    return p;
}
NOINLINE struct big c_big(u32 x)
{
    struct big b;
    u32 i;
    for (i=0; i<8; ++i) b.a[i]=x+7*i;
    return b;
}
#elif CASE == 12
#define P1 u32 a0
#define P2 P1, u32 a1
#define P3 P2, u32 a2
#define P4 P3, u32 a3
#define P5 P4, u32 a4
#define P6 P5, u32 a5
#define P7 P6, u32 a6
#define P8 P7, u32 a7
#define V1 in[2]
#define V2 V1, in[3]
#define V3 V2, in[4]
#define V4 V3, in[5]
#define V5 V4, in[6]
#define V6 V5, in[7]
#define V7 V6, in[8]
#define V8 V7, in[9]
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
extern u32 oracle_args(CAT(P,ARG_COUNT));
extern u32 call_c_args(const volatile u32 *);
NOINLINE u32 c_args(CAT(P,ARG_COUNT))
{
    u32 r=a0;
#if ARG_COUNT > 1
    r += a1 << 1;
#endif
#if ARG_COUNT > 2
    r += a2 << 2;
#endif
#if ARG_COUNT > 3
    r += a3 << 3;
#endif
#if ARG_COUNT > 4
    r += a4 << 4;
#endif
#if ARG_COUNT > 5
    r += a5 << 5;
#endif
#if ARG_COUNT > 6
    r += a6 << 6;
#endif
#if ARG_COUNT > 7
    r += a7 << 7;
#endif
    return r;
}
#elif CASE == 13
extern void *memcpy(void *, const void *, size_t);
extern int strcmp(const char *, const char *);
#elif CASE == 14
#include <setjmp.h>
#elif CASE == 15
extern void assembly_calls(volatile u32 *);
#endif

void __attribute__((section(".entry"))) test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer), *out = OUTPUT(buffer);
    u32 count = 0;
    (void)in;
#if CASE == 1
    out[0] = (u32)buffer; out[1] = length; out[2] = parameter;
    out[3] = in[0]; out[4] = TEST_ID; count=5;
#elif CASE == 2
    u32 a=in[2], b=in[3], s=in[4];
    out[0]=a+b; out[1]=a-b; out[2]=a*b;
    out[3]=a&b; out[4]=a|b; out[5]=a^b; out[6]=~a;
    out[7]=a<<s; out[8]=a>>s; out[9]=(u32)((s32)a>>s);
    out[10]=(s32)a<(s32)b; out[11]=(s32)a>=(s32)b;
    out[12]=(a<<s)|(a>>(32-s)); out[13]=a==b; out[14]=a<b; count=15;
#elif CASE == 3
    u32 a=in[2], b=in[3];
    out[0]=a/b; out[1]=a%b;
    out[2]=(u32)((s32)a/(s32)b); out[3]=(u32)((s32)a%(s32)b); count=4;
#elif CASE == 4
    const volatile unsigned char *p=(const volatile unsigned char *)(in+2);
    volatile unsigned char *q=(volatile unsigned char *)(out+8);
    u32 i;
    for (i=0; i<4; ++i) out[i]=p[i];
    out[4]=(u32)((const volatile signed char *)p)[1];
    out[5]=((const volatile uint16_t *)p)[0];
    out[6]=(u32)((const volatile int16_t *)p)[0]; out[7]=in[3];
    for (i=0; i<4; ++i) q[i]=(unsigned char)(0x11*(i+1));
    for (i=4; i<8; ++i) q[i]=0;
    ((volatile uint16_t *)(out+9))[0]=0xbeef; count=10;
#elif CASE == 5
    struct layout { unsigned char a; u32 b; uint16_t c; unsigned char d; } p;
    struct wide_layout { unsigned char a; u64 b; uint16_t c; };
    struct bits { unsigned a:3, b:5, c:8, d:16; } bits;
    unsigned char *bytes=(unsigned char *)&p;
    u32 i;
    for (i=0; i<sizeof(p); ++i) bytes[i]=0;
    p.a=(unsigned char)in[2]; p.b=in[3]; p.c=(uint16_t)in[4]; p.d=(unsigned char)in[5];
    out[0]=sizeof(p); out[1]=offsetof(struct layout,b); out[2]=offsetof(struct layout,c);
    out[3]=offsetof(struct layout,d); out[4]=sizeof(struct wide_layout);
    out[5]=offsetof(struct wide_layout,b); out[6]=__alignof__(u64);
    for (i=0; i<3; ++i) out[7+i]=(u32)bytes[4*i]|((u32)bytes[4*i+1]<<8)
        |((u32)bytes[4*i+2]<<16)|((u32)bytes[4*i+3]<<24);
    bits.a=in[6]; bits.b=in[7]; bits.c=in[8]; bits.d=in[9];
    bytes=(unsigned char *)&bits;
    out[10]=(u32)bytes[0]|((u32)bytes[1]<<8)|((u32)bytes[2]<<16)|((u32)bytes[3]<<24); count=11;
#elif CASE == 6
    u32 (*volatile callback)(u32)=leaf;
    out[0]=nested(in[2],in[3]); out[1]=callback(in[2]); out[2]=tail(in[3]); count=3;
#elif CASE == 7
    probe_preserved(in+2,out); count=18;
#elif CASE == 8
    u32 selector=in[2];
    out[0]=*(volatile __sfr u32 *)0;
    out[1]=assembly_vr();
    out[2]=*(volatile __sfr u32 *)selector; count=3;
#elif CASE == 18
    /* Read-only SPR addressing probe. Four reviewed system registers are read
       three ways: the C __sfr front end, an immediate/selector form (address in
       the instruction, base r0) and a register-base form (address in a GPR,
       K == 0). VR, SR and PICVR must agree exactly, which exercises the grouped
       SPR encoding (0x2000 group 4, 0x480c group 9). SR[F,CY,OV,OVE,DSX]
       (0x3e00) change on every compare, so SR comparisons mask those bits.
       ICCR changes while the payload runs, so only its documented enable bit 0
       is compared there. No SPR is written. */
    u32 keep=~((u32)0x3e00);
    u32 vr_c=*(volatile __sfr u32 *)0;
    u32 sr_c=*(volatile __sfr u32 *)0x18;
    u32 pv_c=*(volatile __sfr u32 *)0x480c;
    u32 ic_c=*(volatile __sfr u32 *)0x2000;
    u32 vr_i=spr_imm_0();
    u32 vr_r=spr_reg(0);
    u32 sr_i=spr_imm_18();
    u32 sr_r=spr_reg(0x18);
    u32 pv_i=spr_imm_480c();
    u32 pv_r=spr_reg(0x480c);
    u32 ic_i=spr_imm_2000();
    u32 ic_r=spr_reg(0x2000);
    out[0]=vr_c; out[1]=vr_i; out[2]=vr_r;
    out[3]=(u32)(vr_c==vr_i && vr_c==vr_r);
    out[4]=sr_c; out[5]=sr_i; out[6]=sr_r;
    out[7]=(u32)(((sr_c^sr_i)&keep)==0 && ((sr_c^sr_r)&keep)==0);
    out[8]=pv_c; out[9]=pv_i; out[10]=pv_r;
    out[11]=(u32)(pv_c==pv_i && pv_c==pv_r);
    out[12]=ic_c; out[13]=ic_i; out[14]=ic_r;
    out[15]=(u32)((ic_c&1)==1 && (ic_i&1)==1 && (ic_r&1)==1); count=16;
#elif CASE == 9
    u64 a=((u64)in[3]<<32)|in[2], b=((u64)in[5]<<32)|in[4], r;
#if OP == 0
    u32 s=in[6];
    r=a+b; out[0]=(u32)r; out[1]=(u32)(r>>32);
    r=a-b; out[2]=(u32)r; out[3]=(u32)(r>>32);
    r=a*b; out[4]=(u32)r; out[5]=(u32)(r>>32);
    r=a<<s; out[6]=(u32)r; out[7]=(u32)(r>>32);
    r=a>>s; out[8]=(u32)r; out[9]=(u32)(r>>32);
    r=(u64)((s64)a>>s); out[10]=(u32)r; out[11]=(u32)(r>>32); count=12;
#else
#if OP == 1
    r=a/b;
#elif OP == 2
    r=a%b;
#elif OP == 3
    r=(u64)((s64)a/(s64)b);
#else
    r=(u64)((s64)a%(s64)b);
#endif
    out[0]=(u32)r; out[1]=(u32)(r>>32); count=2;
#endif
#elif CASE == 10
    u64 a=((u64)in[3]<<32)|in[2], c=((u64)in[6]<<32)|in[5],
        e=((u64)in[9]<<32)|in[8], r;
    r=oracle_wide(a,in[4],c,in[7],e); out[0]=(u32)r; out[1]=(u32)(r>>32);
    r=call_c_wide(in+2); out[2]=(u32)r; out[3]=(u32)(r>>32); count=4;
#elif CASE == 11
    struct pair p=oracle_pair(in[2],in[3]);
    struct big b=oracle_big(in[2]);
    u32 i;
    out[0]=p.a; out[1]=p.b; call_c_pair(out+2,in[2],in[3]);
    for (i=0; i<8; ++i) out[4+i]=b.a[i];
    call_c_big(out+12,in[2]); count=20;
#elif CASE == 12
    out[0]=oracle_args(CAT(V,ARG_COUNT)); out[1]=call_c_args(in+2); count=2;
#elif CASE == 13
    void *r=memcpy((void *)out,(const void *)(in+2),16);
    int cmp=strcmp((const char *)out,"abcde");
    out[4]=(u32)r; out[5]=(u32)((cmp>0)-(cmp<0)); count=6;
#elif CASE == 14
    jmp_buf context;
    volatile u32 touched=0;
    int r=setjmp(context);
    if (r==0) { touched=77; longjmp(context,13); }
    out[0]=(u32)r; out[1]=touched; count=2;
#elif CASE == 15
    assembly_calls(out); count=6;
#elif CASE == 16
    union { u32 bits; float f; } a __attribute__((unused)),
        b __attribute__((unused)), r __attribute__((unused));
    a.bits=in[2]; b.bits=in[3];
#if OP == 0
    r.f=a.f+b.f; out[0]=r.bits;
#elif OP == 1
    r.f=a.f*b.f; out[0]=r.bits;
#elif OP == 2
    r.f=a.f/b.f; out[0]=r.bits;
#elif OP == 3
    out[0]=(u32)(s32)a.f;
#elif OP == 4
    r.f=(float)(s32)in[2]; out[0]=r.bits;
#else
    out[0]=(u32)a.f;
#endif
    count=1;
#elif CASE == 17
    union { u64 bits; double d; } a __attribute__((unused)),
        b __attribute__((unused)), r __attribute__((unused));
    a.bits=((u64)in[3]<<32)|in[2]; b.bits=((u64)in[5]<<32)|in[4];
#if OP == 0
    r.d=a.d+b.d;
#elif OP == 1
    r.d=a.d*b.d;
#elif OP == 2
    r.d=a.d/b.d;
#elif OP == 3
    out[0]=(u32)(s32)a.d; count=1;
#else
    r.d=(double)(s32)in[2];
#endif
#if OP != 3
    out[0]=(u32)r.bits; out[1]=(u32)(r.bits>>32); count=2;
#endif
#else
#error Unknown test case
#endif
    finish(buffer,length,parameter,count);
}
