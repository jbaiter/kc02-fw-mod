#!/usr/bin/env python3
"""Dependency-free DWARF-info parser for GCC `objdump --dwarf=info` text.

Reads /tmp/kc02-board-re/sdktypes/dwarf-final.txt (the .debug_info dump of a
synthetic TU that #includes the SDK headers with -g) and writes a clean,
de-mangled SDK type dictionary to /tmp/kc02-board-re/sdk_types_clean.tsv.

Output format (tab separated, no header):

    S <name> <size_bytes>
    M <struct_name> <offset> <type> <member_size> <field_name>
    U <name> <size_bytes>
    N <union_name>  <offset> <type> <member_size> <field_name>

Type column is a Ghidra-friendly string: `byte ushort short uint int char
char* float double void*` for basics; `PTR:<TypeName>` for pointers;
`<TypeName>` for nested struct/union/enum references; `byte[12]` style for
arrays; `BITFIELD:<bits>` for bitfields.

Run with no arguments:  python3 dwarf_parse.py
"""

import os
import re
import sys

DWARF_PATH = "/tmp/kc02-board-re/sdktypes/dwarf-final.txt"
OUT_PATH = "/tmp/kc02-board-re/sdk_types_clean.tsv"

# ---------------------------------------------------------------------------
# DWARF parsing
# ---------------------------------------------------------------------------

DIE_RE = re.compile(
    r"^\s*<(\d+)><([0-9a-fA-F]+)>:\s+Abbrev Number:\s*(\d+)"
    r"(?:\s+\((DW_TAG_\w+)\))?\s*$"
)
ATTR_RE = re.compile(r"^\s*<([0-9a-fA-F]+)>\s+(DW_AT_\w+)\s*:\s*(.*)$")
INDIRECT_RE = re.compile(
    r"^\(indirect string, offset: 0x[0-9a-fA-F]+\):\s*(.*)$"
)
REF_RE = re.compile(r"<0x([0-9a-fA-F]+)>")


def clean_name(value):
    """DW_AT_name values may be wrapped as '(indirect string, ...): NAME'."""
    m = INDIRECT_RE.match(value)
    return (m.group(1) if m else value).strip()


def ref_offset(value):
    """Extract the DIE offset from a '<0x123>' reference value."""
    m = REF_RE.match(value or "")
    return int(m.group(1), 16) if m else None


def parse_dies(lines):
    """Return a list of DIE dicts in file order.

    Each DIE: {depth, offset, tag, attrs: {name: rawvalue}, raw: [lines]}
    """
    dies = []
    cur = None
    for line in lines:
        m = DIE_RE.match(line)
        if m:
            cur = {
                "depth": int(m.group(1)),
                "offset": int(m.group(2), 16),
                "tag": m.group(4),
                "attrs": {},
                "raw": [line],
            }
            dies.append(cur)
            continue
        am = ATTR_RE.match(line)
        if am and cur is not None:
            cur["attrs"][am.group(2)] = am.group(3).strip()
            cur["raw"].append(line)
    return dies


# ---------------------------------------------------------------------------
# Base-type normalisation
# ---------------------------------------------------------------------------

# (byte_size, DWARF encoding) -> Ghidra-friendly basic name.
BASIC_BY_SIZE_ENCODING = {
    (1, 8): "byte",        # unsigned char
    (1, 6): "char",        # char / signed char
    (1, 2): "byte",        # _Bool
    (2, 7): "ushort",      # unsigned short
    (2, 5): "short",       # signed short
    (4, 7): "uint",        # unsigned int / unsigned long
    (4, 5): "int",         # int / signed long
    (8, 7): "uint64",      # unsigned 64-bit (not in the canonical basic set)
    (8, 5): "int64",       # signed 64-bit
    (4, 4): "float",
    (8, 4): "double",
    (16, 4): "long double",
}

def to_int(value, default=0):
    """Parse a DWARF integer that may be decimal or 0x-prefixed hex."""
    if value is None:
        return default
    value = value.strip()
    try:
        return int(value, 0)
    except ValueError:
        head = value.split()[0] if value.split() else ""
        try:
            return int(head, 0)
        except ValueError:
            return default


def is_internal(name):
    """True for compiler/libc internals that should not enter the SDK dict.

    The task names `Elf32_*`, `__*` and `std::*`.  This TU has no Elf32/std
    types, but newlib exposes internals such as __tm / __sbuf / __FILE.  The
    SDK also declares its own globals with a dunder name (`__RAM_USE__`,
    `__FIFO_USB__`, `__CSI_STAT__`); those are SDK types and must be kept.
    Newlib's internals are `__lowercase`; the SDK's are `__UPPERCASE__`.
    """
    if name.startswith("Elf32_") or name.startswith("std::"):
        return True
    if name.startswith("__"):
        return not name.endswith("__")
    return False


# ---------------------------------------------------------------------------
# Type graph
# ---------------------------------------------------------------------------

STRUCT_TAGS = {"DW_TAG_structure_type", "DW_TAG_union_type"}
COMPOSITE_TAGS = STRUCT_TAGS | {"DW_TAG_enumeration_type"}


class TypeModel(object):
    def __init__(self, dies):
        self.dies = dies
        self.by_offset = {d["offset"]: d for d in dies}
        # parent index for every DIE (depth stack)
        self.parent = {}
        stack = []
        for i, d in enumerate(dies):
            while stack and dies[stack[-1]]["depth"] >= d["depth"]:
                stack.pop()
            self.parent[i] = stack[-1] if stack else None
            stack.append(i)
        self.index_of = {id(d): i for i, d in enumerate(dies)}
        # typedef aliases keyed by the offset they point at
        self.aliases = {}
        for d in dies:
            if d["tag"] == "DW_TAG_typedef" and "DW_AT_type" in d["attrs"]:
                tgt = ref_offset(d["attrs"]["DW_AT_type"])
                nm = d["attrs"].get("DW_AT_name")
                if tgt is not None and nm is not None:
                    self.aliases.setdefault(tgt, []).append(clean_name(nm))
        self._name_cache = {}
        self._type_cache = {}
        self.unresolved = []          # (member_die, reason)

    # -- naming ------------------------------------------------------------
    def preferred_name(self, die):
        off = die["offset"]
        if off in self._name_cache:
            return self._name_cache[off]
        own = die["attrs"].get("DW_AT_name")
        own = clean_name(own) if own else None
        name = None
        if own and not own.startswith("_"):
            name = own
        elif off in self.aliases:
            # anonymous struct/union, or a GCC 'private' tag like _uiWin:
            # take the public typedef name (uiWin, widgetCreateInfor, ...).
            name = self.aliases[off][0]
        elif own:
            name = own
        else:
            kind = "union" if die["tag"] == "DW_TAG_union_type" else "struct"
            name = "anon_%s_%x" % (kind, off)
        self._name_cache[off] = name
        return name

    # -- size / type resolution -------------------------------------------
    def resolve(self, offset, seen=None):
        """Return (type_string, size_bytes) for a DIE offset."""
        if offset in self._type_cache:
            return self._type_cache[offset]
        if seen is None:
            seen = set()
        if offset in seen:
            return ("undefined", 0)
        seen = seen | {offset}
        die = self.by_offset.get(offset)
        if die is None:
            return ("undefined", 0)
        tag = die["tag"]
        attrs = die["attrs"]
        result = None

        if tag == "DW_TAG_base_type":
            size = to_int(attrs.get("DW_AT_byte_size"))
            enc = attrs.get("DW_AT_encoding") or ""
            enc_tok = enc.split()[0] if enc.split() else ""
            enc = int(enc_tok) if enc_tok.isdigit() else None
            result = (BASIC_BY_SIZE_ENCODING.get((size, enc), "byte"), size)

        elif tag == "DW_TAG_typedef":
            result = self.resolve(ref_offset(attrs.get("DW_AT_type")))

        elif tag in COMPOSITE_TAGS:
            size = to_int(attrs.get("DW_AT_byte_size"))
            if size == 0 and die["tag"] == "DW_TAG_enumeration_type":
                size = 4
            result = (self.preferred_name(die), size)

        elif tag == "DW_TAG_pointer_type":
            size = to_int(attrs.get("DW_AT_byte_size"), 4)
            pointee = ref_offset(attrs.get("DW_AT_type"))
            if pointee is None:
                result = ("void*", size)
            else:
                inner, _ = self.resolve(pointee, seen)
                if inner == "char":
                    result = ("char*", size)
                elif inner in ("void", "undefined"):
                    result = ("void*", size)
                elif inner == "func":
                    result = ("void*", size)
                else:
                    result = ("PTR:%s" % inner, size)

        elif tag in ("DW_TAG_const_type", "DW_TAG_volatile_type"):
            result = self.resolve(ref_offset(attrs.get("DW_AT_type")), seen)

        elif tag == "DW_TAG_array_type":
            elem, esize = self.resolve(ref_offset(attrs.get("DW_AT_type")), seen)
            counts = self._subrange_counts(die)
            total = esize
            for c in counts:
                total *= c
            dims = "".join("[%d]" % c for c in counts)
            result = ("%s%s" % (elem, dims), total)

        elif tag == "DW_TAG_subroutine_type":
            result = ("func", 0)

        if result is None:
            result = ("undefined", 0)
        result = (result[0], int(result[1] or 0))
        self._type_cache[offset] = result
        return result

    def _subrange_counts(self, array_die):
        """Collect element counts from the subrange children of an array DIE."""
        counts = []
        start = self.index_of[id(array_die)]
        base_depth = array_die["depth"]
        for j in range(start + 1, len(self.dies)):
            child = self.dies[j]
            if child["depth"] <= base_depth:
                break
            if child["tag"] == "DW_TAG_subrange_type":
                ub = child["attrs"].get("DW_AT_upper_bound")
                cnt = child["attrs"].get("DW_AT_count")
                if ub is not None:
                    counts.append(to_int(ub) + 1)
                elif cnt is not None:
                    counts.append(to_int(cnt))
                else:
                    counts.append(0)
        return counts or [0]

    # -- members -----------------------------------------------------------
    def members(self, struct_die):
        """Yield the direct DW_TAG_member children of a struct/union DIE."""
        start = self.index_of[id(struct_die)]
        base_depth = struct_die["depth"]
        for j in range(start + 1, len(self.dies)):
            child = self.dies[j]
            if child["depth"] <= base_depth:
                break
            if child["tag"] == "DW_TAG_member":
                yield child

    def member_rows(self, struct_die):
        """Compute output rows for a struct/union DIE."""
        name = self.preferred_name(struct_die)
        rows = []
        n_unresolved = 0
        for mem in self.members(struct_die):
            attrs = mem["attrs"]
            field = clean_name(attrs["DW_AT_name"]) if "DW_AT_name" in attrs else ""
            lot = attrs.get("DW_AT_data_member_location")
            offset = to_int(lot) if lot else 0
            bit_size = attrs.get("DW_AT_bit_size")
            toff = ref_offset(attrs.get("DW_AT_type"))
            if toff is None:
                tstr, msize = "undefined", 0
                n_unresolved += 1
                self.unresolved.append((mem, "member has no DW_AT_type"))
            else:
                tstr, msize = self.resolve(toff)
                if tstr == "undefined":
                    n_unresolved += 1
                    tgt = self.by_offset.get(toff)
                    reason = "dangling DW_AT_type <0x%x>" % toff
                    if tgt is None:
                        reason = "DW_AT_type <0x%x> not found" % toff
                    elif tgt["tag"] is None:
                        reason = "DW_AT_type <0x%x> is a null DIE" % toff
                    self.unresolved.append((mem, reason))
            if bit_size:
                tstr = "BITFIELD:%s" % bit_size
                msize = to_int(bit_size)
            rows.append((offset, tstr, msize, field))
        return name, rows, n_unresolved


# ---------------------------------------------------------------------------
# Emit
# ---------------------------------------------------------------------------

def build(dies):
    model = TypeModel(dies)
    composites = [d for d in dies if d["tag"] in STRUCT_TAGS]

    # Deduplicate by output name; prefer the DIE with the most members.
    chosen = {}
    conflicts = []
    for d in composites:
        name = model.preferred_name(d)
        n_members = sum(1 for _ in model.members(d))
        prev = chosen.get(name)
        if prev is None:
            chosen[name] = d
            continue
        prev_members = sum(1 for _ in model.members(prev))
        keep = d if n_members > prev_members else prev
        drop = prev if keep is d else d
        chosen[name] = keep
        if keep["attrs"].get("DW_AT_byte_size") != drop["attrs"].get("DW_AT_byte_size") \
                or n_members != prev_members:
            conflicts.append((name, prev, d))

    out_lines = []
    emitted_structs = 0
    emitted_unions = 0
    emitted_members = 0
    unresolved_total = 0
    sizes = []  # (name, size, is_union)

    # Preserve DWARF order of the surviving DIEs.
    for d in composites:
        name = model.preferred_name(d)
        if chosen.get(name) is not d:
            continue
        if is_internal(name):
            continue
        is_union = d["tag"] == "DW_TAG_union_type"
        size = to_int(d["attrs"].get("DW_AT_byte_size"))
        out_lines.append("%s\t%s\t%d" % ("U" if is_union else "S", name, size))
        sizes.append((name, size, is_union))
        if is_union:
            emitted_unions += 1
        else:
            emitted_structs += 1
        mtag = "N" if is_union else "M"
        _, rows, n_unres = model.member_rows(d)
        unresolved_total += n_unres
        for offset, tstr, msize, field in rows:
            out_lines.append(
                "%s\t%s\t%d\t%s\t%d\t%s" % (mtag, name, offset, tstr, msize, field)
            )
            emitted_members += 1

    with open(OUT_PATH, "w") as fh:
        fh.write("\n".join(out_lines) + "\n")

    return {
        "model": model,
        "structs": emitted_structs,
        "unions": emitted_unions,
        "members": emitted_members,
        "unresolved": unresolved_total,
        "sizes": sizes,
        "conflicts": conflicts,
    }


def report(stats):
    model = stats["model"]
    print("=== DWARF parse summary ===")
    print("input                 : %s" % DWARF_PATH)
    print("output                : %s" % OUT_PATH)
    print("structs emitted       : %d" % stats["structs"])
    print("unions emitted        : %d" % stats["unions"])
    print("members emitted       : %d" % stats["members"])
    print("unresolved members    : %d" % stats["unresolved"])
    if stats["conflicts"]:
        print("name conflicts        : %d" % len(stats["conflicts"]))
        for name, a, b in stats["conflicts"]:
            print("   %s: off=0x%x size=%s members=%d  vs  off=0x%x size=%s members=%d"
                  % (name, a["offset"], a["attrs"].get("DW_AT_byte_size"),
                     sum(1 for _ in model.members(a)),
                     b["offset"], b["attrs"].get("DW_AT_byte_size"),
                     sum(1 for _ in model.members(b))))
    else:
        print("name conflicts        : 0")

    if model.unresolved:
        print("\n-- unresolved member examples (raw DIE lines) --")
        for mem, why in model.unresolved[:8]:
            print("   why: %s" % why)
            for ln in mem["raw"]:
                print("     " + ln.rstrip())
    else:
        print("\n-- no unresolved members --")

    print("\n-- 25 largest structs/unions --")
    for name, size, is_union in sorted(stats["sizes"], key=lambda x: -x[1])[:25]:
        print("   %-40s %8d  %s" % (name, size, "union" if is_union else "struct"))


def main():
    if not os.path.exists(DWARF_PATH):
        sys.stderr.write("missing input: %s\n" % DWARF_PATH)
        return 1
    with open(DWARF_PATH, "r", errors="replace") as fh:
        lines = fh.read().split("\n")
    dies = parse_dies(lines)
    stats = build(dies)
    report(stats)
    return 0


if __name__ == "__main__":
    sys.exit(main())
