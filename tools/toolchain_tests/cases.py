"""Independent host oracles and reproducible, defined-behavior input vectors."""
import random
import struct

# Measured RAM layout (see docs/RAM_MAP_MEASURED.md). Code and the execution packet
# are separate regions so that a large code image does not force a large USB
# transfer: every transfer stays at SIZE (2048) bytes, the proven size.
SHIM = 0x020D8000        # 128-byte dispatcher / SPI-adapter slot
CANARY = 0x020D8080      # 4-byte return-only canary
CODE_ADDR = 0x020D8100   # code image base, links here
CODE_LIMIT = 5632        # 0x020D8100..0x020D96FF, verified free and stable
BASE = 0x020D9700        # execution packet: passed in r3 and returned by the helper
SIZE = 2048              # packet size == maximum USB transfer size
INPUT = 1792
HEADER = 1856
OUTPUT = 1888
GUARD = 2032
REGION_END = BASE + SIZE  # 0x020D9EFF, leaving 84 B below a live global at 0x020D9F54
MAGIC = 0x5432304B
# l.j from the shim slot to CODE_ADDR, preserving arguments and the link register.
DISPATCH = struct.pack('<I', (CODE_ADDR - SHIM) // 4)
MASK = 0xffffffff
# Deterministic fill for the unused part of the packet, so an unexpected write
# anywhere in the returned range is still detected.
PAD = bytes((0xA5 ^ (i * 0x3D)) & 0xFF for i in range(SIZE))
DEFAULT_LAYOUT=dict(code_limit=CODE_LIMIT,input=INPUT,header=HEADER,output=OUTPUT,guard=GUARD)

# Per-case SPR authorization for statically encoded addresses.
# 0x0 is SPR_VR, 0x18 is SPR_SR, 0x2000 is SPR_ICCR and 0x480C is SPR_PICVR in
# the AX3292 map (ax329x.h: SPRGROUP_SYS + (0x6 << 2), SPRGROUP_IC + 0,
# SPRGROUP_PIC + (0x3 << 2); MAX_SPRS_PER_GRP_BITS == 11). Only read-only,
# side-effect-free registers are listed.
# Reads may also use a register base because the front end materialises constant
# __sfr addresses into registers at -O0; mfspr has no side effects. Writes must
# be statically encoded and are only allowed for the reviewed set, because a
# status/clock/cache write can change or halt the running firmware.
SPR_POLICY={
    8: dict(reads=(0,),writes=()),
    14: dict(reads=(0x18,),writes=(0x18,)),
    18: dict(reads=(0,0x18,0x2000,0x480c),writes=()),
}
def spr_policy(case):
    return SPR_POLICY.get(case.get('case'),dict(reads=(),writes=()))


def packet_layout(case):
    # Code now lives in its own region, so every profile gets the full code
    # budget and the packet keeps its original input/header/output/guard offsets.
    del case
    return dict(DEFAULT_LAYOUT)


def signed(x, bits=32):
    x &= (1 << bits) - 1
    return x - (1 << bits) if x >> (bits - 1) else x


def truncdiv(a, b):
    q = abs(a) // abs(b)
    return -q if (a < 0) != (b < 0) else q


def words(x):
    return [x & MASK, (x >> 32) & MASK]


def fbits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def fvalue(x):
    return struct.unpack('<f', struct.pack('<I', x))[0]


def dbits(x):
    return list(struct.unpack('<II', struct.pack('<d', x)))


def dvalue(a):
    return struct.unpack('<d', struct.pack('<II', *a))[0]


# CASE/OP are compile-time selections, never camera-supplied function pointers.
CASES = [
    dict(name='spi', case=0, kind='spi', description='C -> stock SPI_read, two 2 KiB chunks'),
    dict(name='echo', case=1, description='C callback arguments and fresh code identity'),
    dict(name='isa_calls', case=15, description='assembly direct/indirect PC+4 returns and branches'),
    dict(name='sfr_read', case=8, description='read-only SPR_VR: C immediate/computed vs assembly'),
    dict(name='alu32', case=2, description='integer arithmetic, shifts, rotates and comparisons'),
    dict(name='divide32', case=3, description='signed/unsigned hardware divide and remainder'),
    dict(name='memory', case=4, description='byte/halfword/word accesses, extension and LE layout'),
    dict(name='layout', case=5, description='struct, bitfield and 64-bit member layout'),
    dict(name='calls', case=6, description='nested bounded calls and function pointers'),
    dict(name='tail_calls', case=6, tail=True, description='same call fixture with sibling calls enabled'),
    dict(name='preserved', case=7, description='assembly callee-saved, reserved registers, SP and frame guard'),
] + [dict(name='args%d' % n, case=12, argc=n,
          description='%d arguments, independent C <-> assembly' % n) for n in range(1,9)] + [
    dict(name='wide_abi', case=10, description='64-bit args/returns, independent C <-> assembly'),
    dict(name='struct_abi', case=11, description='small/large struct returns, C <-> assembly'),
    dict(name='u64', case=9, op=0, description='64-bit arithmetic and shifts'),
    dict(name='u64_div', case=9, op=1, description='unsigned 64-bit division runtime'),
    dict(name='u64_mod', case=9, op=2, description='unsigned 64-bit remainder runtime'),
    dict(name='s64_div', case=9, op=3, description='signed 64-bit division runtime'),
    dict(name='s64_mod', case=9, op=4, description='signed 64-bit remainder runtime'),
    dict(name='libc_memory', case=13, libc=True, description='reconstructed newlib memcpy/strcmp'),
    dict(name='setjmp', case=14, libc=True, description='reconstructed newlib setjmp/longjmp'),
] + [dict(name='float_' + name, case=16, op=i,
          description='software float ' + name) for i,name in enumerate(
              ('add','mul','div','to_int','from_int','to_uint'))] + [
    dict(name='double_' + name, case=17, op=i,
         description='software double ' + name) for i,name in enumerate(
             ('add','mul','div','to_int','from_int'))] + [
    dict(name='spr_map', case=18,
         description='read-only SPR_VR/SPR_SR selector vs register-base addressing'),
]
BY_NAME = {c['name']: c for c in CASES}


def vectors(case, seed=3292, count=8):
    """Small fixed edge set plus reproducible random cases; no C undefined behavior."""
    rng = random.Random('%s:%s' % (seed, case['name']))
    c = case['case']
    if c in (0,1,8,14,15,18):
        return [[0]] if c == 8 else [[]]
    if c == 2:
        v = [[0,0,1],[MASK,1,31],[0x80000000,0x7fffffff,16],[0x12345678,0x12345678,7]]
        return v + [[rng.getrandbits(32),rng.getrandbits(32),rng.randrange(1,32)] for _ in range(count)]
    if c == 3:
        v = [[0,1],[MASK,2],[0x80000000,2],[0x7fffffff,MASK],[0xffffffef,3]]
        for _ in range(count):
            a,b = rng.getrandbits(32), rng.getrandbits(32) or 1
            if (a,b) == (0x80000000,MASK): b=2
            v.append([a,b])
        return v
    if c == 4:
        return [[0xa5fe807f,0x11223344],[0x7f0080ff,0xdeadbeef]]
    if c == 5:
        return [[0x12,0x3456789a,0xbcde,0x56,5,17,0xa7,0xbeef],
                [MASK,0,MASK,0,7,31,255,65535]]
    if c in (6,7,10,11,12):
        n = {6:2,7:1,10:8,11:2,12:case.get('argc',1)}[c]
        v = [[(0x10203040+i*0x110011)&MASK for i in range(n)], [MASK]*n, [0]*n]
        return v + [[rng.getrandbits(32) for _ in range(n)] for _ in range(count)]
    if c == 9:
        pairs = [(0,1,0),(0xffffffffffffffff,3,63),(1<<63,2,32),
                 (0x123456789abcdef0,0x102030405,1)]
        pairs += [(rng.getrandbits(64),rng.getrandbits(64) or 1,rng.randrange(64)) for _ in range(count)]
        return [words(a)+words(2 if (a,b)==(1<<63,(1<<64)-1) else b)+[s] for a,b,s in pairs]
    if c == 13:
        return [list(struct.unpack('<IIII', text.ljust(16,b'\0'))) for text in
                (b'abcde\0',b'abcdef\0',b'abcd\0',b'Abcde\0',b'\0')]
    if c in (16,17):
        op=case['op']
        if op == 4:
            return [[x,0,0,0] for x in (0,1,MASK,0x7fffffff,0x80000000,16777217)]
        pairs=[(1.5,2.25),(-7.75,0.5),(0.0,-2.0),(-0.0,2.0),(123.0,4.0)]
        if op == 5: pairs=[(0.0,1.0),(1.5,1.0),(123.75,1.0),(2147483520.0,1.0)]
        if c == 16: return [[fbits(a),fbits(b)] for a,b in pairs]
        return [dbits(a)+dbits(b) for a,b in pairs]
    raise ValueError('unknown case')


def expected(case, a, entry, nonce=0):
    c=case['case']; op=case.get('op',0)
    if c == 1: r=[BASE,SIZE,0,nonce,entry['id']]
    elif c == 2:
        x,y,s=a
        r=[x+y,x-y,x*y,x&y,x|y,x^y,~x,x<<s,x>>s,signed(x)>>s,
           int(signed(x)<signed(y)),int(signed(x)>=signed(y)),(x<<s)|(x>>(32-s)),int(x==y),int(x<y)]
    elif c == 3:
        x,y=a; q=truncdiv(signed(x),signed(y))
        r=[x//y,x%y,q,signed(x)-q*signed(y)]
    elif c == 4:
        p=struct.pack('<II',*a)
        r=list(p[:4])+[signed(p[1],8),int.from_bytes(p[:2],'little'),
                         signed(int.from_bytes(p[:2],'little'),16),a[1],0x44332211,0xbeef]
    elif c == 5:
        x,b,z,d,ba,bb,bc,bd=a
        r=[12,4,8,10,16,4,4,x&255,b,(z&65535)|((d&255)<<16),
           (ba&7)|((bb&31)<<3)|((bc&255)<<8)|((bd&65535)<<16)]
    elif c == 6:
        def leaf(x): return ((x&MASK)^0x13579bdf)+17 & MASK
        x,y=a; v=x
        for i in range(8): v=leaf(v+y if (v^i)&1 else v-y)
        r=[leaf(v),leaf(x),leaf(y)]
    elif c == 7:
        r=[None,None,9*a[0]+45]+[0x1000+i for i in (2,14,18,20,22,24,26,28,30)]
        r += [None,None,None,None,0x6b6b,None]
    elif c == 8: r=[None,None,None]
    elif c == 18: r=[None,None,None,1,None,None,None,1,None,None,None,1,None,None,None,1]
    elif c == 9:
        x=a[0]|(a[1]<<32); y=a[2]|(a[3]<<32); s=a[4]
        if op == 0:
            r=sum((words(v) for v in (x+y,x-y,x*y,x<<s,x>>s,signed(x,64)>>s)),[])
        elif op == 1: r=words(x//y)
        elif op == 2: r=words(x%y)
        else:
            q=truncdiv(signed(x,64),signed(y,64))
            r=words(q if op==3 else signed(x,64)-q*signed(y,64))
    elif c == 10:
        r=[a[0]^a[3]^a[2]^a[5]^a[6],a[1]^a[4]^a[7]^0x12345678]*2
    elif c == 11:
        r=[a[0]^0x12345678,a[1]+5]*2 + [a[0]+7*i for i in range(8)]*2
    elif c == 12: r=[sum(x<<i for i,x in enumerate(a))]*2
    elif c == 13:
        text=struct.pack('<IIII',*a).split(b'\0',1)[0]; other=b'abcde'
        r=a+[BASE+OUTPUT,(text>other)-(text<other)]
    elif c == 14: r=[13,77]
    elif c == 15: r=[entry['symbols']['asm_direct_return'],entry['symbols']['asm_indirect_return'],1,2,3,4]
    elif c == 16:
        if op==4: r=[fbits(signed(a[0]))]
        else:
            x,y=map(fvalue,a)
            r=[fbits(x+y)] if op==0 else [fbits(x*y)] if op==1 else [fbits(x/y)] if op==2 else [int(x)]
    elif c == 17:
        if op==4: r=dbits(signed(a[0]))
        else:
            x,y=dvalue(a[:2]),dvalue(a[2:])
            r=dbits(x+y) if op==0 else dbits(x*y) if op==1 else dbits(x/y) if op==2 else [int(x)]
    else: raise ValueError('unknown expected result')
    return [None if x is None else x&MASK for x in r]


def make_packet(a, nonce, index, layout=None):
    layout=layout or DEFAULT_LAYOUT
    inp,header,output,guard=(layout[k] for k in ('input','header','output','guard'))
    capacity=(header-inp)//4-2
    if len(a)>capacity or any(not 0<=x<=MASK for x in a):
        raise ValueError('invalid fixed test packet/input')
    data=bytearray(PAD)
    struct.pack_into('<%dI' % (2+len(a)),data,inp,nonce,index,*a)
    data[header:output]=b'\xce'*(output-header)
    data[output:guard]=b'\xcc'*(guard-output)
    return bytes(data)


def validate_result(entry, packet, received):
    """Check complete code/input/guard bytes, not merely a success marker."""
    if len(received)!=SIZE: raise ValueError('short result buffer')
    case=BY_NAME[entry['name']]
    layout=entry.get('layout',DEFAULT_LAYOUT)
    inp,header,output,guard=(layout[k] for k in ('input','header','output','guard'))
    nonce,index=struct.unpack_from('<II',packet,inp)
    a=entry['vectors'][index]
    result=expected(case,a,entry,nonce)
    if output+len(result)*4>guard: raise ValueError('result exceeds declared partition')
    actual=list(struct.unpack_from('<%dI'%len(result),received,output))
    if case['case']==7:
        if actual[0]!=actual[1] or actual[0]&3 or not actual[0] or actual[17]!=actual[0]+80:
            raise ValueError('stack pointer/frame restoration failed')
        if actual[12]!=actual[13] or actual[14]!=actual[15]:
            raise ValueError('reserved thread/PIC register changed')
    if case['case']==8 and not actual[0]==actual[1]==actual[2]:
        raise ValueError('__sfr version reads differ from assembly')
    if case['case']==18 and not (actual[3]==actual[7]==actual[11]==actual[15]==1):
        raise ValueError('selector-form SPR read disagrees with register-base read')
    wanted=bytearray(packet)
    struct.pack_into('<8I',wanted,header,MAGIC,entry['id'],nonce,SIZE,0,len(result),BASE,index)
    for i,value in enumerate(result):
        struct.pack_into('<I',wanted,output+4*i,actual[i] if value is None else value)
    if received!=bytes(wanted):
        offset=next(i for i,(x,y) in enumerate(zip(wanted,received)) if x!=y)
        region='code/constant/guard' if offset<inp else 'inputs/guards' if offset<header else 'result/header/guard'
        raise ValueError('%s mismatch at buffer +0x%03x: expected %02x, got %02x' %
                         (region,offset,wanted[offset],received[offset]))
    return actual
