#!/usr/bin/env python3
"""Audit EQEmu's Laurion ENCODE (server -> client) blocks for slot-record mistakes.

The decode side mistakes a client slot *record* for a slot *number* on the way in. The encode
side does the mirror image on the way out, and it has a second failure mode unique to Laurion:
the client validates the whole first dword of an InventorySlot_Struct (Type | Padding1 << 16),
so a hand-built record whose padding is uninitialised is rejected and the item never moves.

For every ENCODE block this prints:
  - how each *slot* named field on the Laurion side is filled, and whether a converter ran
  - any hand-built slot record (a structs::*Slot_Struct local, or direct .Slot/.Type writes)
    and whether it was value-initialised
  - the size gate it uses

Usage:  python utils/laurion_encode_audit.py
"""
import re, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAURION_CPP = os.path.join(ROOT, 'common', 'patches', 'laurion.cpp')
STRUCTS_H = os.path.join(ROOT, 'common', 'patches', 'laurion_structs.h')

CONVERTERS = (
    'ServerToLaurionSlot', 'ServerToLaurionCorpseSlot', 'ServerToLaurionTypelessSlot',
    'ServerGeneralSlotToLaurion', 'ServerToLaurionCastingSlot', 'ServerToLaurionBuffSlot',
    'LaurionInventorySlotToCastingInventorySlot', 'ServerToLaurionItemPacketType',
    'GeneralBagSlotId', 'CalcSlotId', 'ServerToLaurionCorpseMainSlot',
)
SLOT_RECORDS = ('InventorySlot_Struct', 'TypelessInventorySlot_Struct', 'CastSpellInventorySlot_Struct')

# Slot-named fields that are genuinely a plain number on the Laurion wire, with the reason.
# Anything slot-named that is not in this list is reported for a human to confirm.
KNOWN_PLAIN_OK = {
    ('WearChange_Struct', 'wear_slot_id'):
        'equipment slot; Laurion and RoF2 both run slotCharm(0)..slotAmmo(22)',
    ('Merchant_Sell_Response_Struct', 'itemslot'):
        'merchant stock index (ItemBase::MerchantSlot), not a player inventory slot; '
        'the sell ack Merchant_Purchase_Response_Struct does carry a slot record and uses the converter',
    ('KeyRingSlots_Struct', 'mount_keyring_slots'): 'keyring capacity, not a slot',
    ('KeyRingSlots_Struct', 'illusion_keyring_slots'): 'keyring capacity, not a slot',
    ('KeyRingSlots_Struct', 'familiar_keyring_slots'): 'keyring capacity, not a slot',
    ('KeyRingSlots_Struct', 'heroforge_keyring_slots'): 'keyring capacity, not a slot',
    ('KeyRingSlots_Struct', 'teleportation_keyring_slots'): 'keyring capacity, not a slot',
}

FIELD_RE = re.compile(r'/\*[0-9A-Fa-f]{1,4}\*/\s+([A-Za-z_][A-Za-z_0-9 ]*?)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(\[[^\]]*\])?\s*;')
STRUCT_RE = re.compile(r'^\s*struct\s+([A-Za-z_][A-Za-z_0-9]*)\b')


def parse_structs(path):
    lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
    out, i = {}, 0
    while i < len(lines):
        m = STRUCT_RE.match(lines[i])
        if not m:
            i += 1
            continue
        name = m.group(1)
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
        out.setdefault(name, []).append(
            [(fm.group(1).strip(), fm.group(2)) for bl in body if (fm := FIELD_RE.search(bl))])
        i = j + 1
    return out


def blocks(src, keyword):
    # Anchor to the start of a line. Without this the regex also matches the opcode names that appear
    # inside Log(...) strings and comments - "Laurion::ENCODE(OP_MoveItem)" - which yields phantom blocks.
    for m in re.finditer(r'(?m)^[ \t]*' + keyword + r'\((OP_\w+)\)', src):
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
        yield m.group(1), src[:m.start()].count('\n') + 1, ''.join(body)


def main():
    src = open(LAURION_CPP, encoding='utf-8', errors='replace').read()
    structs = parse_structs(STRUCTS_H)

    flagged, clean, total = [], [], 0
    for op, line, blk in blocks(src, r'ENCODE'):
        total += 1
        sm = re.search(r'setup_direct_encode\(\s*(\w+)\s*,\s*structs::([A-Za-z_0-9]+)', blk, re.I)
        emu_s, eq_s = (sm.group(1), sm.group(2)) if sm else (None, None)

        notes, risky = [], False

        # 2 first, so section 1 knows which slot records in this block were built by hand.
        pads_written = {w for w in ('Padding1', 'Padding2')
                        if re.search(r'\w+\.%s\s*=' % w, blk)}
        hand_built, record_notes = set(), []
        for hm in re.finditer(r'structs::(%s)\s+(\w+)\s*(\{\})?\s*;' % '|'.join(SLOT_RECORDS), blk):
            init = bool(hm.group(3)) or pads_written == {'Padding1', 'Padding2'}
            record_notes.append(
                f"{'ok  ' if init else '!!  '}local {hm.group(1)} {hm.group(2)}"
                + ('' if init else '  <- NOT fully initialised: Padding1/Padding2 leak'))
            risky = risky or not init
            if init:
                hand_built.add(hm.group(2))

        # 1. Laurion-side slot fields and how they are filled
        for ltype, lname in structs.get(eq_s or '', [[]])[0]:
            if 'slot' not in lname.lower():
                continue
            am = re.search(r'(eq->%s\s*=\s*[^;]+;)' % lname, blk)
            known = KNOWN_PLAIN_OK.get((eq_s or '', lname))
            if am:
                rhs = am.group(1)
                converted = any(c in rhs for c in CONVERTERS)
                from_record = bool(hand_built) and re.search(r'=\s*(%s)\s*;' % '|'.join(hand_built), rhs)
                ok = converted or bool(from_record)
                if known:
                    notes.append(f"ok  {lname}: {rhs.strip()}  [{known}]")
                else:
                    notes.append(f"{'ok  ' if ok else '!!  '}{lname}: {rhs.strip()}")
                    risky = risky or not ok
            elif re.search(r'\bOUT\(\s*%s\s*\)' % lname, blk):
                # OUT() is a same-name copy: a server slot number written straight into the client field
                plain = ltype not in SLOT_RECORDS
                if known:
                    notes.append(f"ok  {lname}: OUT() same-name copy ({ltype})  [{known}]")
                else:
                    notes.append(f"{'!!  ' if plain else 'ok  '}{lname}: OUT() same-name copy ({ltype})")
                    risky = risky or plain
            else:
                notes.append(f"??  {lname}: never assigned ({ltype}) - stays zero")

        notes.extend(record_notes)
        for wm in re.finditer(r'(\w+)\.(Type|Slot|SubIndex|AugIndex|Padding1|Padding2)\s*=', blk):
            if not re.search(r'structs::(%s)\s+%s' % ('|'.join(SLOT_RECORDS), wm.group(1)), blk):
                continue
            if wm.group(2) in ('Padding1', 'Padding2'):
                notes.append(f"ok   explicit {wm.group(1)}.{wm.group(2)} write")

        gate = re.search(r'(ENCODE_LENGTH_EXACT|ENCODE_LENGTH_ATLEAST|EAT_ENCODE)\(([^)]*)\)', blk)
        gate_s = f"{gate.group(1)}({gate.group(2)})" if gate else "no size gate"

        rec = (op, line, eq_s, gate_s, notes, risky)
        (flagged if risky else clean).append(rec)

    print(f"== {total} Laurion ENCODE blocks ==\n")
    print(f"FLAGGED ({len(flagged)})\n")
    for op, line, eq_s, gate_s, notes, _ in flagged:
        print(f"  L{line:5} {op:34} structs::{eq_s}   {gate_s}")
        for n in notes:
            print(f"        {n}")
        print()
    print(f"CLEAN ({len(clean)}) - every slot field goes through a converter\n")
    for op, line, eq_s, gate_s, notes, _ in clean:
        if not notes:
            continue
        print(f"  L{line:5} {op:34} structs::{eq_s}")
        for n in notes:
            print(f"        {n}")


if __name__ == '__main__':
    main()
