#!/usr/bin/env python3
"""Audit EQEmu's Laurion packet structs.

Two checks:

1. Size - for every Laurion struct whose declaration carries a "N byte payload" note, compare
   that note with the size implied by the field list. This is what catches a struct that is two
   bytes short of the wire packet (the OP_Consume bug: comment said 20, fields added to 18).

2. Slot mapping - for every DECODE block in common/patches/laurion.cpp that touches a field named
   *slot*, show how it is mapped. A raw IN() copy or a field that is never assigned is where a
   12-byte InventorySlot_Struct gets mistaken for a plain slot number.

Offsets in these headers are decimal in some structs and hex in others, so sizes are computed from
the field types rather than from the offset comments.

Usage:  python utils/laurion_struct_audit.py
"""
import re, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STRUCTS_H = os.path.join(ROOT, 'common', 'patches', 'laurion_structs.h')
LAURION_CPP = os.path.join(ROOT, 'common', 'patches', 'laurion.cpp')

TYPES = {
    'uint8': 1, 'int8': 1, 'char': 1, 'bool': 1, 'undefined1': 1,
    'uint16': 2, 'int16': 2, 'short': 2, 'undefined2': 2,
    'uint32': 4, 'int32': 4, 'int': 4, 'float': 4, 'undefined4': 4,
    'uint64': 8, 'int64': 8, 'long long': 8, 'double': 8, 'undefined8': 8,
}
FIELD_RE = re.compile(r'/\*[0-9A-Fa-f]{1,4}\*/\s+([A-Za-z_][A-Za-z_0-9 ]*?)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(\[[^\]]*\])?\s*;')
STRUCT_RE = re.compile(r'^\s*struct\s+([A-Za-z_][A-Za-z_0-9]*)\b(.*)$')


def parse_structs(path):
    lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
    out, i = {}, 0
    while i < len(lines):
        m = STRUCT_RE.match(lines[i])
        if not m:
            i += 1
            continue
        name, tail = m.group(1), m.group(2)
        j, depth, started, body = i, 0, False, []
        while j < len(lines) and j < i + 500:
            for ch in lines[j]:
                if ch == '{':
                    depth += 1
                    started = True
                elif ch == '}':
                    depth -= 1
            if started:
                body.append(lines[j])
                if depth == 0:
                    break
            j += 1
        fields = []
        for bl in body:
            fm = FIELD_RE.search(bl)
            if fm:
                fields.append((fm.group(1).strip(), fm.group(2), fm.group(3)))
        # Only trust a byte count that sits on the struct's own declaration line, or on the line
        # immediately above it that also names the opcode. Anything further up is prose about some other
        # struct and produces false positives ("EQEmu's canonical X_Struct is 129 bytes ...").
        note = tail.strip()
        if not re.search(r'\d+\s*byte', note):
            for k in range(max(0, i - 2), i):
                if re.search(r'0x[0-9a-fA-F]{4}', lines[k]) and re.search(r'\d+\s*byte', lines[k]):
                    note = lines[k].strip()
                    break
        out.setdefault(name, []).append(dict(line=i + 1, fields=fields, note=note))
        i = j + 1
    return out


def packed_sizes(structs):
    sizes = {}
    for _ in range(8):                            # nested structs resolve over a few passes
        for n, s in structs.items():
            total, ok = 0, True
            for t, f, arr in s[0]['fields']:
                k = 1
                if arr:
                    am = re.search(r'\d+', arr)
                    if am:
                        k = int(am.group())
                    else:
                        ok = False
                        break
                sz = TYPES.get(t, sizes.get(t))
                if sz is None:
                    ok = False
                    break
                total += sz * k
            if ok:
                sizes[n] = total
    return sizes


def main():
    structs = parse_structs(STRUCTS_H)
    sizes = packed_sizes(structs)

    print(f"== {len(structs)} Laurion structs, {len(sizes)} with a computable layout ==\n")

    print("1) declared payload size != field list")
    bad = 0
    for n, s in sorted(structs.items(), key=lambda kv: kv[1][0]['line']):
        dm = re.search(r'(\d+)\s*byte\s+payload', s[0]['note'])
        if not dm or n not in sizes:
            continue
        declared, computed = int(dm.group(1)), sizes[n]
        if declared != computed:
            bad += 1
            print(f"   L{s[0]['line']:5} {n:38} declared={declared:4} fields={computed:4}   {s[0]['note'][:60]}")
    print(f"   -> {bad} mismatch(es)\n")

    src = open(LAURION_CPP, encoding='utf-8', errors='replace').read()
    print("2) how each Laurion DECODE maps a *slot* field")
    for m in re.finditer(r'DECODE\((OP_\w+)\)\s*(?://\s*(0x[0-9a-fA-F]{4}))?', src):
        op, hx = m.group(1), m.group(2)
        k, depth, started, body = m.end(), 0, False, []
        while k < len(src):
            for ch in src[k]:
                if ch == '{':
                    depth += 1
                    started = True
                elif ch == '}':
                    depth -= 1
            if started:
                body.append(src[k])
                if depth == 0:
                    break
            k += 1
        blk = ''.join(body)
        sm = re.search(r'setup_direct_decode\(\s*\w+\s*,\s*structs::([A-Za-z_0-9]+)', blk, re.I)
        if not sm:
            continue
        struct = sm.group(1)
        slot_fields = [f for t, f, _ in structs.get(struct, [{}])[0].get('fields', []) if 'slot' in f.lower()]
        if not slot_fields:
            continue
        lines_out = []
        for f in slot_fields:
            am = re.search(r'(emu->\w+\s*=\s*[^;]*%s[^;]*;)' % f, blk)
            if am:
                lines_out.append(am.group(1).strip())
            elif re.search(r'\bIN\(\s*%s\s*\)' % f, blk):
                lines_out.append(f"IN({f})  <- raw copy, verify the client really sends a number here")
            else:
                lines_out.append(f"{f}  <- never assigned")
        risky = any('raw copy' in x or 'never assigned' in x for x in lines_out)
        print(f"   {'!!' if risky else '  '} {hx or '????'} {op:32} structs::{struct}")
        for x in lines_out:
            print(f"          {x}")


if __name__ == '__main__':
    main()
