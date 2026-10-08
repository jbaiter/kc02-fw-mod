"""Bounded, partial OpenRISC integer/no-delay model. NOT device validation.

Independent raw-word decode catches broken fixture/oracle/stack code before USB.
Only the instructions actually exercised by this suite are supported. Unknown
instructions, writes outside the packet result area/owned stack and SPRs other
than the reviewed read-only version register and the setjmp status-register
save/restore pair are fatal, never silently emulated as no-ops.
"""
import struct

from .cases import BASE,SHIM,CODE_ADDR,SIZE,DEFAULT_LAYOUT,MASK,signed,truncdiv,make_packet,validate_result

STOP=0xfffffff0
TOP=0x100000
SAVED=(2,10,14,16,18,20,22,24,26,28,30)


def execute(entry, packet, flash_offset=0, max_steps=50000):
    spi=entry['kind']=='spi'
    start=SHIM if spi else CODE_ADDR
    layout=entry.get('layout',DEFAULT_LAYOUT)
    inp,header,guard=(layout[k] for k in ('input','header','guard'))
    regs=[(i*0x01010101)&MASK for i in range(32)]
    regs[0]=0; regs[1]=TOP; regs[9]=STOP
    regs[3]=BASE; regs[4]=SIZE; regs[5]=flash_offset
    before=regs[:]
    memory={BASE+i:b for i,b in enumerate(packet)}
    memory.update({start+i:b for i,b in enumerate(entry['blob'])})
    memory.update({i:0 for i in range(TOP-8192,TOP)})
    flag=False; carry=0; pc=start; spi_calls=[]; cache_calls=0
    # Modelled system registers. 0x0 is SPR_VR; 0x18 is SPR_SR in the AX3292 map
    # (SPRGROUP_SYS + (0x6 << 2)). Values are arbitrary constants: this model
    # checks our instruction sequences, not silicon behaviour.
    spr={0:0x12345678,0x18:0x0000c003,0x2000:0x00000201,0x480c:0x00090003}
    def load(address,n,sign=False):
        if address%n: raise ValueError('model: unaligned access')
        try: value=int.from_bytes(bytes(memory[address+i] for i in range(n)),'little')
        except KeyError as exc: raise ValueError('model: uninitialized/out-of-range read '+hex(address)) from exc
        return signed(value,n*8) if sign else value
    def store(address,n,value):
        if address%n: raise ValueError('model: unaligned store')
        stack=TOP-8192<=address and address+n<=TOP
        result=BASE+header<=address and address+n<=BASE+guard
        if not stack and not result: raise ValueError('model: store outside results/stack '+hex(address))
        for i in range(n): memory[address+i]=(value>>(8*i))&255
    for step in range(max_steps):
        if pc==STOP:
            if regs[1]!=TOP or any(regs[i]!=before[i] for i in SAVED):
                raise ValueError('model: callback stack/callee-saved corruption')
            if not spi and cache_calls!=1: raise ValueError('model: missing/repeated result cache publication')
            return bytes(memory[BASE+i] for i in range(SIZE)),spi_calls
        if not spi and pc==0x0202B058:
            if tuple(regs[3:6])!=(BASE+inp,SIZE-inp,0):
                raise ValueError('model: wrong result cache-clean arguments')
            cache_calls+=1; target=regs[9]
            for i in (3,4,5,6,7,8,11,12,13,15,17,19,21,23,25,27,29,31): regs[i]=0x51515151
            pc=target; continue
        if spi and pc==0x0203D38C:
            if (regs[3],regs[4],regs[5])!=(flash_offset,BASE,SIZE):
                raise ValueError('model: wrong SPI arguments')
            spi_calls.append(tuple(regs[3:6]))
            for i in range(SIZE): memory[BASE+i]=(flash_offset//SIZE+i*17)&255
            target=regs[9]
            for i in (3,4,5,6,7,8,11,12,13,15,17,19,21,23,25,27,29,31): regs[i]=0x51515151
            pc=target; continue
        limit=len(entry['blob']) if spi else layout['code_limit']
        if not start<=pc<start+limit or pc&3: raise ValueError('model: invalid instruction PC '+hex(pc))
        w=load(pc,4); op=w>>26; d=(w>>21)&31; a=(w>>16)&31; b=(w>>11)&31
        imm=w&65535; simm=signed(imm,16); following=pc+4
        if op in (0,1,3,4):
            take=op in (0,1) or (op==3 and not flag) or (op==4 and flag)
            if op==1: regs[9]=pc+4
            if take: following=pc+4*signed(w&0x3ffffff,26)
        elif op==5: pass  # l.nop
        elif op==6: regs[d]=imm<<16
        elif op in (0x11,0x12):
            following=regs[b]
            if op==0x12: regs[9]=pc+4
        elif op in (0x21,0x22,0x23,0x24,0x25,0x26):
            n={0x21:4,0x22:4,0x23:1,0x24:1,0x25:2,0x26:2}[op]
            regs[d]=load((regs[a]+simm)&MASK,n,op in (0x22,0x24,0x26))
        elif op in (0x27,0x28):
            value=regs[a]+(simm&MASK)+(carry if op==0x28 else 0)
            regs[d]=value; carry=int(value>MASK)
        elif op==0x29: regs[d]=regs[a]&imm
        elif op==0x2a: regs[d]=regs[a]|imm
        elif op==0x2b: regs[d]=regs[a]^(simm&MASK)
        elif op==0x2c: regs[d]=regs[a]*simm
        elif op==0x2d:
            address=regs[a]|imm
            if address not in spr: raise ValueError('model: unapproved SPR selector '+hex(address))
            regs[d]=spr[address]
        elif op==0x30:
            # l.mtspr rA, rB, K writes rB to SPR (rA | K); the immediate is split
            # across bits 25:21 and 10:0 because bits 15:11 hold rB.
            if regs[a]: raise ValueError('model: dynamic SPR address unsupported')
            address=((w>>21)&31)<<11 | (w&0x7ff)
            if address not in spr: raise ValueError('model: unapproved SPR write '+hex(address))
            spr[address]=regs[b]
        elif op==0x2e:
            kind=(w>>6)&3; shift=w&31
            if kind==0: regs[d]=regs[a]<<shift
            elif kind==1: regs[d]=regs[a]>>shift
            elif kind==2: regs[d]=signed(regs[a])>>shift
            else: raise ValueError('model: rotate instruction unsupported')
        elif op in (0x2f,0x39):
            x=regs[a]; y=(simm&MASK) if op==0x2f else regs[b]
            comparisons={0:x==y,1:x!=y,2:x>y,3:x>=y,4:x<y,5:x<=y,
                         10:signed(x)>signed(y),11:signed(x)>=signed(y),
                         12:signed(x)<signed(y),13:signed(x)<=signed(y)}
            if d not in comparisons: raise ValueError('model: unknown comparison')
            flag=comparisons[d]
        elif op in (0x35,0x36,0x37):
            offset=signed(((w>>10)&0xf800)|(w&0x7ff),16)
            store((regs[a]+offset)&MASK,{0x35:4,0x36:1,0x37:2}[op],regs[b])
        elif op==0x38:
            sub=w&0x7ff; x=regs[a]; y=regs[b]
            if sub in (0,1):
                value=x+y+(carry if sub==1 else 0); carry=int(value>MASK); regs[d]=value
            elif sub==2: regs[d]=x-y; carry=int(x<y)
            elif sub==3: regs[d]=x&y
            elif sub==4: regs[d]=x|y
            elif sub==5: regs[d]=x^y
            elif sub==8: regs[d]=x<<(y&31)
            elif sub==0x48: regs[d]=x>>(y&31)
            elif sub==0x88: regs[d]=signed(x)>>(y&31)
            elif sub==0xe: regs[d]=x if flag else y
            elif sub==0x306: regs[d]=x*y
            elif sub in (0x309,0x30a):
                if not y: raise ValueError('model: division by zero')
                regs[d]=truncdiv(signed(x),signed(y)) if sub==0x309 else x//y
            elif sub==0xf: regs[d]=(x&-x).bit_length() if x else 0  # ff1
            elif sub==0x10f: regs[d]=x.bit_length()  # fl1
            else: raise ValueError('model: unknown ALU '+hex(w))
        else: raise ValueError('model: unknown opcode '+hex(w))
        regs=[x&MASK for x in regs]; regs[0]=0; pc=following
    raise ValueError('model: instruction budget exceeded')


def check_model(suite):
    count=0
    for entry in suite['tests']:
        if entry['status']!='ready': continue
        try:
            if entry['kind']=='spi':
                for offset in (0,2048):
                    packet=bytes([0xa5])*SIZE
                    received,calls=execute(entry,packet,offset)
                    expected=bytes((offset//SIZE+i*17)&255 for i in range(SIZE))
                    if received!=expected or len(calls)!=1: raise ValueError('SPI stub mismatch')
                    count+=1
            else:
                for index,a in enumerate(entry['vectors']):
                    packet=make_packet(a,0x12340000+index,index,entry['layout'])
                    received,_=execute(entry,packet)
                    validate_result(entry,packet,received); count+=1
        except Exception as exc:
            raise ValueError(entry['key']+': '+str(exc)) from exc
    return count
