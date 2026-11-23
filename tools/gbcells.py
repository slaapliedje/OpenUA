#!/usr/bin/env python3
"""The Dark Queen's character cells: script addresses 0x401..0x800.

In The Dark Queen of Krynn a script variable at 0x401..0x800 is not script
memory: it is a field of the SELECTED character's record. DQK.EXE sorts every
variable address with 0x40637 (1..0x400 script memory, 0x401..0x800 the
character, 0x8000..0xaee2 the block) and hands this range to two routines:

    0x406cd  read:  a range (0x41f..0x4ab, the memorized spells) and a
             58-entry switch (keys at cs:0x45a, targets 0x74 bytes on);
             any other cell reads 0.
    0x41e1e  write: the same spell range and a 14-entry switch (keys at
             cs:0x1bf6, targets 0x1c on); any other cell is ignored.

(cs = 0x402c0 in the file.) A string stored to the range goes through
0x40d19, which copies it into the name only for 0x401; that one the engine
already serves from script memory (gbs_seed_who).

Every case reads or writes one field at a fixed offset of the DQK record,
which is the .QCH file's own layout. READ and WRITE below give those
offsets; `parse_exe()` recovers the same tables from DQK.EXE, and
tests/test_gbcells.py holds the two together, then holds
src/engine/gbcells.c to this module.

The engine's record is FRUA's (qch.c makes it out of the .QCH), so a DQK
offset is translated the way qch.c moved the field (see gbmon.py's
docstring for that map). The engine record is in memory, after FRUA's own
byte swap (L0ce0): the longs at 68/72 and the words at 76..87 are
big-endian there, little-endian in DQK's. A field FRUA has no room for
(0x182..0x18a, 0x199, 0x19c, the high byte of DQK's words at 0x58..0x67, and
the word at 0x5e) reads 0 and takes no write. God, robe and knightly order
are packed into rec[91] (rules.h REC_KRYNN).

Writing a memorized spell follows 0x41e1e exactly: 255 means 0, and a 0
written over a spell that is memorized and not yet marked sets its bit 7
instead (the game's "cast, not yet forgotten"). Writing the status word
(0x5ee) also sets DQK 0x18b (engine 382) to "status is 0".
"""
import struct

LO, HI = 0x401, 0x800
SPELLS = (0x41f, 0x4ab, 0xce)          # first cell, last cell, DQK offset

# cell -> (DQK offset, width in bytes)
READ = {}
for c in range(0x411, 0x41d):          # abilities, permanent/current pairs
    READ[c] = (0x78 + c - 0x411, 1)
READ[0x41d] = (0x84, 1)                # exceptional strength, two bytes
READ[0x41e] = (0x85, 1)
READ[0x4ae] = (0x58, 2)                # race
READ[0x4af] = (0x5a, 2)                # class
READ[0x4b1] = (0x52, 2)                # age (both cells)
READ[0x4b2] = (0x52, 2)
READ[0x4b3] = (0x89, 1)
READ[0x4c9] = (0x19a, 1)               # knightly order
READ[0x4ca] = (0x19b, 2)               # god (a word: 0x19c is FRUA-less)
READ[0x4cb] = (0x19d, 1)               # robe
READ[0x538] = (0x90, 1)
for c in range(0x53c, 0x544):
    READ[c] = (0x93 + c - 0x53c, 1)
for c in range(0x54c, 0x552):          # steel, gems, jewelry; two cells a word
    READ[c] = (0x4c + ((c - 0x54c) & ~1), 2)
for c in range(0x552, 0x559):          # class levels
    READ[c] = (0xa5 + c - 0x552, 1)
for c in range(0x559, 0x560):          # former class levels
    READ[c] = (0xac + c - 0x559, 1)
READ[0x567] = (0x60, 2)                # gender
READ[0x568] = (0x62, 2)                # alignment
READ[0x571] = (0xbb, 1)
READ[0x5ec] = (0xcc, 1)
READ[0x5ee] = (0x64, 2)                # status
READ[0x5f0] = (0x66, 2)
READ[0x5fd] = (0x198, 1)               # current hit points

WRITE = {0x4b1: (0x52, 2), 0x538: (0x90, 1), 0x568: (0x62, 2),
         0x571: (0xbb, 1), 0x5ec: (0xcc, 1), 0x5ee: (0x64, 2),
         0x5f0: (0x66, 2), 0x5fd: (0x198, 1)}
for c in range(0x54c, 0x552):
    WRITE[c] = (0x4c + ((c - 0x54c) & ~1), 2)

STATUS_CELL, STATUS_FLAG = 0x5ee, 0x18b
REC_KRYNN = 91


# ---- DQK record offset -> the engine's record ----------------------------

def _slot(o):
    """Where DQK byte `o` lives in the engine record: an int offset, a
    ('krynn', shift, mask) tuple, or None."""
    if 0x44 <= o < 0x4c:                       # two longs, swapped
        base = o & ~3
        return base + 3 - (o - base)
    if 0x4c <= o < 0x58:                       # six words, swapped
        return o ^ 1
    if o < 0x58:
        return o
    if o < 0x68:                               # eight words kept as bytes
        if o & 1 or o == 0x5e:
            return None
        return 0x58 + (o - 0x58) // 2
    if o < 0x157:
        return o - 8
    if o < 0x182:
        return o - 4
    if 0x18b <= o < 0x199:
        return o - 0xd
    return {0x19a: ("krynn", 5, 3), 0x19b: ("krynn", 0, 7),
            0x19d: ("krynn", 3, 3)}.get(o)


def get_byte(rec, o):
    s = _slot(o)
    if s is None:
        return 0
    if isinstance(s, tuple):
        return (rec[REC_KRYNN] >> s[1]) & s[2]
    return rec[s]


def set_byte(rec, o, v):
    s = _slot(o)
    if s is None:
        return
    if isinstance(s, tuple):
        _, sh, m = s
        rec[REC_KRYNN] = (rec[REC_KRYNN] & ~(m << sh) & 0xff) | ((v & m) << sh)
        return
    rec[s] = v & 0xff


def read(rec, cell):
    """The value a Dark Queen script reads at `cell` (0x401..0x800)."""
    if SPELLS[0] <= cell <= SPELLS[1]:
        return get_byte(rec, SPELLS[2] + cell - SPELLS[0])
    if cell not in READ:
        return 0
    o, w = READ[cell]
    v = get_byte(rec, o)
    if w == 2:
        v |= get_byte(rec, o + 1) << 8
    return v


def write(rec, cell, v):
    """A Dark Queen script's write of `v` to `cell`, applied to `rec`."""
    v &= 0xffff
    if SPELLS[0] <= cell <= SPELLS[1]:
        o = SPELLS[2] + cell - SPELLS[0]
        if v == 0xff:
            v = 0
        cur = get_byte(rec, o)
        if v == 0 and cur and not cur & 0x80:
            set_byte(rec, o, cur | 0x80)
        else:
            set_byte(rec, o, v)
        return
    if cell not in WRITE:
        return
    o, w = WRITE[cell]
    set_byte(rec, o, v)
    if w == 2:
        set_byte(rec, o + 1, v >> 8)
    if cell == STATUS_CELL:
        set_byte(rec, STATUS_FLAG, 1 if v == 0 else 0)


# ---- the same tables, read out of DQK.EXE --------------------------------

CS = 0x402c0
READ_FN, WRITE_FN = 0x406cd, 0x41e1e


def _table(d, keys_at, count, gap):
    keys = struct.unpack_from("<%dH" % count, d, CS + keys_at)
    tgts = struct.unpack_from("<%dH" % count, d, CS + keys_at + gap)
    return [(k + LO, CS + t) for k, t in zip(keys, tgts)]


def _case(d, at, key):
    """Interpret one switch arm: the DQK offset and width it touches for
    `key` (cell - 0x401). Handles exactly the shapes DQK.EXE uses."""
    idx, i, ax = None, at, None
    while True:
        b = d[i:i + 6]
        if b[:3] == b"\x8b\x46\x06":            # mov ax,[bp+6] (the key)
            ax, i = key, i + 3
        elif b[:3] in (b"\x8b\x46\x08", b"\x8a\x46\x08"):  # the value
            i += 3
        elif b[:1] == b"\x05":                  # add ax,imm16
            ax, i = (ax + struct.unpack_from("<H", b, 1)[0]) & 0xffff, i + 3
        elif b[:2] == b"\xd1\xe8":              # shr ax,1
            ax, i = ax >> 1, i + 2
        elif b[:2] == b"\xd1\xe0":              # shl ax,1
            ax, i = ax << 1, i + 2
        elif b[:2] == b"\x88\x46":              # mov [bp-1],al (a stash)
            i += 3
        elif b[:2] == b"\x8a\x56":              # mov dl,[bp-1]
            i += 3
        elif b[:2] == b"\xb6\x00":              # mov dh,0
            i += 2
        elif b[:2] == b"\xd1\xe2":              # shl dx,1
            ax, i = ax << 1, i + 2
        elif b[:4] == b"\xc4\x1e\x67\xad":      # les bx,[-0x5299] (the record)
            idx, i = 0, i + 4
        elif b[:2] in (b"\x03\xd8", b"\x03\xda"):    # add bx,ax / add bx,dx
            idx, i = idx + ax, i + 2
        elif b[:2] == b"\x25\x01":              # and ax,1
            ax, i = ax & 1, i + 3
        elif b[:1] == b"\x26":                  # es: the access itself
            op, mod = b[1], b[2]
            disp = b[3] if mod in (0x47,) else struct.unpack_from("<H", b, 3)[0]
            width = 2 if op in (0x8b, 0x89) else 1
            return idx + disp, width
        else:
            raise ValueError("unexpected bytes at %#x: %s" % (i, b.hex()))


def parse_exe(d):
    """(READ, WRITE, SPELLS) recovered from DQK.EXE's bytes."""
    if d[READ_FN:READ_FN + 3] != b"\x55\x8b\xec" or \
       d[WRITE_FN:WRITE_FN + 3] != b"\x55\x8b\xec":
        raise ValueError("not the DQK.EXE these offsets were read from")
    # the spell range: cmp ax,0x1e / cmp ax,0xaa ... es:0xce[bx]
    lo = struct.unpack_from("<H", d, READ_FN + 0x15)[0]
    hi = struct.unpack_from("<H", d, READ_FN + 0x1a)[0]
    off = struct.unpack_from("<H", d, READ_FN + 0x2a)[0]
    spells = (LO + lo, LO + hi, off)
    rd = {c: _case(d, t, c - LO) for c, t in _table(d, 0x45a, 58, 0x74)}
    wr = {c: _case(d, t, c - LO) for c, t in _table(d, 0x1bf6, 14, 0x1c)}
    return rd, wr, spells
