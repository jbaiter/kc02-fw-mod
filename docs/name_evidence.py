#!/usr/bin/env python3
"""Evidence extractor for the unnamed KC02 functions.

For every function still named FUN_*, harvest the machine-checkable evidence:
  - SPR/SFR addresses it touches (subsystem signature)
  - literal data addresses it loads, resolved to strings (ASCII + GBK), known
    tables, or RAM globals
  - the named functions it calls (subsystem + purpose)
  - the named functions that call it
  - its position relative to the nearest named anchors (module attribution)

Writes name_worklist.tsv and prints aggregate coverage.
"""
import bisect, json, re, sys
from pathlib import Path

RE = Path('/tmp/kc02-board-re')
IMG = Path('/home/jbaiter/.src/kc02-fw-mod/firmware/original.bin').read_bytes()
BASE = 0x01FFDA00
APP_LO, APP_HI = 0x2600, 0x0D3200

# ---------------------------------------------------------------- string map
sm = {}
i = APP_LO
while i < APP_HI:
    if 32 <= IMG[i] < 127:
        j = i
        while j < APP_HI and 32 <= IMG[j] < 127:
            j += 1
        if j - i >= 6:
            sm[i + BASE] = IMG[i:j].decode('ascii', 'replace')
        i = j
    elif 0x81 <= IMG[i] <= 0xFE and i + 1 < APP_HI and 0x40 <= IMG[i + 1] <= 0xFE:
        j = i
        while j + 1 < APP_HI and 0x81 <= IMG[j] <= 0xFE and 0x40 <= IMG[j + 1] <= 0xFE:
            j += 2
        if j - i >= 4:
            try:
                s = IMG[i:j].decode('gbk')
                if s.strip():
                    sm[i + BASE] = s
            except Exception:
                pass
        i = j
    else:
        i += 1
sm_addr = sorted(sm)
n_ascii = sum(1 for a, s in sm.items() if all(ord(c) < 128 for c in s))


def str_at(a):
    k = bisect.bisect_right(sm_addr, a) - 1
    if k >= 0:
        s = sm[sm_addr[k]]
        if sm_addr[k] <= a < sm_addr[k] + len(s.encode('gbk', 'replace')):
            return s
    return None


# ------------------------------------------------------- tables / ram globals
tables = []
for l in (RE / 'app_tables.tsv').read_text().splitlines():
    p = l.split('\t')
    if len(p) >= 4:
        tables.append((p[0], int(p[1], 0), int(p[2]) * max(int(p[3]), 1)))
G = json.load(open(RE / 'ram_globals.json'))['globals']
gaddr = set()
for it in G:
    if isinstance(it, dict):
        for k in ('addr', 'address', 'cpu', 'a', 'start'):
            if k in it:
                gaddr.add(int(str(it[k]), 0))
                break
    elif isinstance(it, (str, int)):
        try:
            gaddr.add(int(str(it), 0))
        except ValueError:
            pass


def table_at(a):
    for n, ad, size in tables:
        if ad <= a < ad + max(size, 4):
            return n
    return None


# --------------------------------------------------------------- functions
F = []
for l in (RE / 'funcs_now.tsv').read_text().splitlines():
    a, n, s = l.split('\t')
    F.append((int(a, 16), n, int(s)))
F.sort()
starts = [f[0] for f in F]
name_of = {a: n for a, n, _ in F}


def fn_at(a):
    k = bisect.bisect_right(starts, a) - 1
    if k < 0:
        return None
    s = starts[k]
    nxt = starts[k + 1] if k + 1 < len(starts) else 1 << 32
    return s if s <= a < nxt else None


unnamed = [f for f in F if f[1].startswith('FUN_')]
unset = {a for a, _, _ in unnamed}
sprname = {}
for l in (RE / 'sfr_map.tsv').read_text().splitlines():
    p = l.split('\t')
    if len(p) == 2:
        sprname[int(p[1], 16)] = p[0]

# ------------------------------------------------------- instruction harvest
ev = {a: {'n': 0, 'spr': {}, 'lits': set(), 'calls': set(), 'callers': set()} for a in unset}
code = {a: [] for a in unset}
ins = re.compile(r'^\s*([0-9a-f]+):\s+(.*)$')
with open(RE / 'full.dis') as fh:
    for line in fh:
        m = ins.match(line)
        if not m:
            continue
        a = int(m.group(1), 16)
        fs = fn_at(a)
        if fs is None:
            continue
        # rest = "<hex bytes> [<hex bytes> ...] <mnemonic> [operands]"
        toks = m.group(2).split()
        k = 0
        while k < len(toks) and re.fullmatch(r'[0-9a-f]{2,4}', toks[k]):
            k += 1
        mn = toks[k] if k < len(toks) else '?'
        ops = ' '.join(toks[k + 1:])
        if fs in unset:
            code[fs].append((a, mn, ops))
            ev[fs]['n'] += 1
        if mn == 'l.jal':
            t = re.search(r'(0x[0-9a-f]+)', ops)
            if t:
                tgt = int(t.group(1), 16)
                cname = name_of.get(fs) if fs is not None else None
                if tgt in unset:
                    if cname and not cname.startswith('FUN_'):
                        ev[tgt]['callers'].add(cname)
                elif fs in unset:
                    nm = name_of.get(fn_at(tgt) or 0)
                    if nm and not nm.startswith('FUN_'):
                        ev[fs]['calls'].add(nm)

for a, cl in code.items():
    for k, (ia, mn, ops) in enumerate(cl):
        if mn in ('l.mfspr', 'l.mtspr'):
            m2 = re.search(r',\s*(0x[0-9a-f]+|\d+)\s*$', ops)
            if m2:
                s = int(m2.group(1), 0)
                ev[a]['spr'][s] = ev[a]['spr'].get(s, 0) + 1
        if mn == 'l.movhi':
            m2 = re.match(r'(r\d+),\s*(0x[0-9a-f]+)', ops)
            if m2:
                reg, hi = m2.group(1), int(m2.group(2), 16)
                for _, mn2, ops2 in cl[k + 1:k + 4]:
                    m3 = re.match(re.escape(reg) + r',' + re.escape(reg) + r',\s*(0x[0-9a-f]+)', ops2)
                    if mn2 in ('l.ori', 'l.addi', 'l.addic') and m3:
                        ev[a]['lits'].add((hi << 16) + int(m3.group(1), 16))
                        break
                    if mn2 in ('l.ori', 'l.addi', 'l.addic'):
                        break

# ------------------------------------------------------------------- report
rows = []
nany = nstr = nspr = ncall = ncaller = ntab = 0
for a in sorted(unset):
    e = ev[a]
    lits = e['lits']
    strs = [(x, str_at(x)) for x in lits if str_at(x)]
    tabs = sorted({table_at(x) for x in lits if table_at(x)})
    if strs: nstr += 1
    if e['spr']: nspr += 1
    if e['calls']: ncall += 1
    if e['callers']: ncaller += 1
    if tabs: ntab += 1
    if strs or e['spr'] or e['calls'] or e['callers'] or tabs or lits: nany += 1
    top = sorted(e['spr'].items(), key=lambda kv: -kv[1])[:4]
    spr_txt = ",".join(f"{sprname.get(s, hex(s))}" for s, _ in top)
    rows.append((a, name_of[a], e['n'], spr_txt, " | ".join(f"{s!r}" for _, s in strs[:3]),
                 ",".join(sorted(e['calls'])[:6]), ",".join(sorted(e['callers'])[:4]), ",".join(tabs[:3])))
with open(RE / 'name_worklist.tsv', 'w') as fh:
    fh.write("addr\tfunc\tinstrs\tsprs\tstrings\tnamed_callees\tnamed_callers\ttables\n")
    for r in rows:
        fh.write("\t".join(str(x) for x in r) + "\n")

print(f"string map: {len(sm)} entries ({n_ascii} ASCII, {len(sm)-n_ascii} GBK/CJK)   ram globals: {len(gaddr)}   tables: {len(tables)}")
print(f"unnamed functions: {len(rows)}")
print(f"  with >=1 piece of evidence : {nany}")
print(f"  touches SPR/SFR            : {nspr}")
print(f"  calls a named function     : {ncall}")
print(f"  called by a named function : {ncaller}")
print(f"  references a string        : {nstr}")
print(f"  references a known table   : {ntab}")
print()
# biggest clusters, ranked by how much evidence they carry
clusters, cur = [], [unnamed[0]]
for p, q in zip(unnamed, unnamed[1:]):
    if q[0] - p[0] <= 0x400:
        cur.append(q)
    else:
        clusters.append(cur)
        cur = [q]
clusters.append(cur)
scored = []
for c in clusters:
    sc = sum(len(ev[a]['calls']) * 3 + len(ev[a]['spr']) * 2 + len(ev[a]['lits']) + len(ev[a]['callers'])
             for a, _, _ in c)
    scored.append((-sc, -len(c), c))
scored.sort()
for _, _, c in scored[:5]:
    a0, a1 = c[0][0], c[-1][0]
    lo = bisect.bisect_left(starts, a0)
    before = F[lo - 1][1] if lo > 0 else '-'
    after = F[bisect.bisect_right(starts, a1)][1] if bisect.bisect_right(starts, a1) < len(F) else '-'
    sprs, strs, calls = {}, [], set()
    for a, _, _ in c:
        for s, n in ev[a]['spr'].items():
            sprs[s] = sprs.get(s, 0) + n
        strs += [str_at(x) for x in ev[a]['lits'] if str_at(x)]
        calls |= ev[a]['calls']
    top = sorted(sprs.items(), key=lambda kv: -kv[1])[:6]
    print(f"0x{a0:08X}-0x{a1:08X}  {len(c):3d} fns   after '{before}'")
    print(f"   SFRs: " + ", ".join(f"{sprname.get(s, hex(s))}x{n}" for s, n in top))
    if strs:
        print(f"   strings: " + " | ".join(repr(s)[:34] for s in strs[:6]))
    if calls:
        print(f"   calls:   " + ", ".join(sorted(calls)[:10]))
