#!/usr/bin/env python3
"""ecl.py — disassemble Gold Box ECL scripts (the Krynn dialects).

ADR-0023's second half: every Gold Box game but FRUA drives an area from ECL, a
bytecode script, and the engine is to interpret it natively. Before a line of
that interpreter is written in C, this module pins the instruction FRAMING
against the games' own files — a decoder that mis-sizes one operand walks off
into data and every instruction after it is fiction, so the framing has to be
proven first and on real scripts.

Nothing here is original research. The opcode table, the operand encoding and
the per-game overrides are the Greybox project's (kblood/Greybox_CombinedGoldbox,
0BSD): `packages/engine/src/ecl/{opcodes,disassemble,reachability}.ts` and
`docs/engine/research/ecl-opcode-spec-for-asm.md`. Greybox read the Dark Queen
operand counts out of DQK.EXE's own handlers; this is a Python restatement of
that result, kept small so the C interpreter can be written against it.

THE FORMAT

A block is a header of five far pointers {u16 segment, u16 vaddr} — vmRun1 (every
step), searchLocation (per-cell events), preCampCheck, campInterrupted, and the
initial entry — followed by bytecode. Addresses in the bytecode are VIRTUAL:

    Gen1 (Champions / Death Knights of Krynn)   0x1388 magic, 22-byte header,
                                                 vaddr = offset + 0x7FFE
    Gen2 (The Dark Queen of Krynn)              no magic, 20-byte header,
                                                 vaddr = offset + 0x8000

An instruction is an opcode byte and a fixed number of operands; four opcodes
then carry `count` more (jump tables and menus). An operand is two bytes
{code, value}, or three when code is 1, 2, 3 or 0x81 (a 16-bit little-endian
word); code 0x80 is an inline string, `value` bytes of 6-bit packed text.

    python3 tools/ecl.py ECL.GLB                 # census: every block, walked
    python3 tools/ecl.py ECL.GLB --block 4       # disassemble one area's script
    python3 tools/ecl.py ECL1.DAX --block 34
    python3 tools/ecl.py ECL1.DAX --dialect gen1-dok   # else the best fit is used

No game data is needed to use or test the module; the files are the user's own.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ENTRY_NAMES = ("vmRun1", "searchLocation", "preCampCheck",
               "campInterrupted", "initialEntry")

# opcode -> (mnemonic, fixed operand count, index of the count operand or None)
GEN1_OPCODES = {
    0x00: ("EXIT", 0, None),            0x01: ("GOTO", 1, None),
    0x02: ("GOSUB", 1, None),           0x03: ("COMPARE", 2, None),
    0x04: ("ADD", 3, None),             0x05: ("SUBTRACT", 3, None),
    0x06: ("DIVIDE", 3, None),          0x07: ("MULTIPLY", 3, None),
    0x08: ("RANDOM", 2, None),          0x09: ("SAVE", 2, None),
    0x0a: ("LOAD_CHARACTER", 1, None),  0x0b: ("LOAD_MONSTER", 3, None),
    0x0c: ("SETUP_MONSTER", 3, None),   0x0d: ("APPROACH", 0, None),
    0x0e: ("PICTURE", 1, None),         0x0f: ("INPUT_NUMBER", 2, None),
    0x10: ("INPUT_STRING", 2, None),    0x11: ("PRINT", 1, None),
    0x12: ("PRINTCLEAR", 1, None),      0x13: ("RETURN", 0, None),
    0x14: ("COMPARE_AND", 4, None),     0x15: ("VERTICAL_MENU", 3, 2),
    0x16: ("IF_EQ", 0, None),           0x17: ("IF_NE", 0, None),
    0x18: ("IF_LT", 0, None),           0x19: ("IF_GT", 0, None),
    0x1a: ("IF_LE", 0, None),           0x1b: ("IF_GE", 0, None),
    0x1c: ("CLEARMONSTERS", 0, None),   0x1d: ("PARTYSTRENGTH", 1, None),
    0x1e: ("CHECKPARTY", 6, None),      0x1f: ("UNKNOWN_1F", 2, None),
    0x20: ("NEWECL", 1, None),          0x21: ("LOAD_FILES", 3, None),
    0x22: ("PARTY_SURPRISE", 2, None),  0x23: ("SURPRISE", 4, None),
    0x24: ("COMBAT", 0, None),          0x25: ("ON_GOTO", 2, 1),
    0x26: ("ON_GOSUB", 2, 1),           0x27: ("TREASURE", 8, None),
    0x28: ("ROB", 3, None),             0x29: ("ENCOUNTER_MENU", 14, None),
    0x2a: ("GETTABLE", 3, None),        0x2b: ("HORIZONTAL_MENU", 2, 1),
    0x2c: ("PARLAY", 6, None),          0x2d: ("CALL", 1, None),
    0x2e: ("DAMAGE", 5, None),          0x2f: ("AND", 3, None),
    0x30: ("OR", 3, None),              0x31: ("SPRITE_OFF", 0, None),
    0x32: ("FIND_ITEM", 1, None),       0x33: ("PRINT_RETURN", 0, None),
    0x34: ("ECL_CLOCK", 2, None),       0x35: ("SAVE_TABLE", 3, None),
    0x36: ("ADD_NPC", 2, None),         0x37: ("LOAD_PIECES", 3, None),
    0x38: ("PROGRAM", 1, None),         0x39: ("WHO", 1, None),
    0x3a: ("DELAY", 0, None),           0x3b: ("SPELL", 3, None),
    0x3c: ("PROTECTION", 1, None),      0x3d: ("CLEAR_BOX", 0, None),
    0x3e: ("DUMP", 0, None),            0x3f: ("FIND_SPECIAL", 1, None),
    0x40: ("DESTROY_ITEMS", 1, None),   0x41: ("GEN1_AWARD_XP", 2, None),
}

# The Dark Queen of Krynn: operand counts as its own handlers declare them
# (Greybox, "the whole Gen2 operand table was read out of DQK.EXE").
#
# Three of the zero-operand ones kept their Champions NAMES in Greybox's table
# but not their meaning. Read out of DQK.EXE's handlers (file offsets 0x330a3,
# 0x330e4, 0x33122, 0x33169) and borne out by every script that uses them:
#   0x1d  CONTINUE    "press Return", then the text box is cleared
#   0x22  YES_NO      asks; yes sets the == flag, no the != flag, and the
#                     ordering flags are cleared
#   0x23  CLEAR_EXIT  clears the text box and ends the script
#   0x29  NOP_29      only steps over itself
# Death Knights uses 0x1d and 0x22 the same way in its scripts; its 0x23 and
# 0x29 are taken to match and have not been read out of that game's code.
GEN2_OVERRIDES = {
    0x0c: ("SETUP_MONSTER", 4, None),   0x1d: ("CONTINUE", 0, None),
    0x1f: ("UNKNOWN_1F", 1, None),      0x21: ("LOAD_FILES", 2, None),
    0x22: ("YES_NO", 0, None),          0x23: ("CLEAR_EXIT", 0, None),
    0x27: ("TREASURE", 4, 3),           0x29: ("NOP_29", 0, None),
    0x2c: ("PARLAY", 1, None),          0x31: ("SHORT_MENU", 2, 1),
    0x37: ("LOAD_PIECES", 2, None),     0x41: ("GEN2_EXT_41", 2, None),
}

# Death Knights of Krynn: a Gen1 container with a "short" operand table of its
# own, read out of DoK's GAME.OVR handlers (Greybox, ECL_GEN1_SHORT_DOK). It is
# the Dark Queen table less the three entries that game changed again.
DOK_OVERRIDES = {k: v for k, v in GEN2_OVERRIDES.items()
                 if k not in (0x21, 0x37, 0x41)}

OP_EXIT, OP_GOTO, OP_GOSUB, OP_RETURN, OP_NEWECL = 0x00, 0x01, 0x02, 0x13, 0x20
OP_ON_GOTO, OP_ON_GOSUB = 0x25, 0x26
IF_MIN, IF_MAX = 0x16, 0x1b
WORD_CODES = (0x01, 0x02, 0x03, 0x81)      # three-byte operands
CODE_STRING = 0x80


class EclError(ValueError):
    pass


class Dialect:
    def __init__(self, name, header_len, vbase, magic, overrides=None):
        self.name = name
        self.header_len = header_len
        self.vbase = vbase
        self.magic = magic
        self.opcodes = dict(GEN1_OPCODES)
        self.opcodes.update(overrides or {})


GEN1 = Dialect("gen1", 22, 0x7FFE, True)                      # Champions of Krynn
GEN1_DOK = Dialect("gen1-dok", 22, 0x7FFE, True, DOK_OVERRIDES) # Death Knights
GEN2 = Dialect("gen2", 20, 0x8000, False, GEN2_OVERRIDES)      # The Dark Queen
DIALECTS = {d.name: d for d in (GEN1, GEN1_DOK, GEN2)}


def decode_string(raw, offset, packed_len):
    """6-bit packed text, MSB first: 0 ends it, 1..31 are '@'+n (so 1 = 'A'),
    32..63 are themselves (space, digits, punctuation)."""
    acc = bits = 0
    out = []
    for b in raw[offset:offset + packed_len]:
        acc = ((acc << 8) | b) & 0xFFFFFF
        bits += 8
        while bits >= 6:
            bits -= 6
            v = (acc >> bits) & 0x3F
            if v == 0:
                return "".join(out)
            out.append(chr(v + 64 if v <= 31 else v))
    return "".join(out)


class Operand:
    __slots__ = ("code", "word", "text")

    def __init__(self, code, word, text=None):
        self.code = code
        self.word = word
        self.text = text

    def __repr__(self):
        if self.code == CODE_STRING:
            return '"%s"' % self.text
        if self.code == 0x00:
            return "#%d" % self.word
        if self.code == 0x02:
            return "#0x%04x" % self.word
        if self.code in (0x01, 0x03):
            return "[0x%04x]" % self.word
        return "c%02x:%d" % (self.code, self.word)


class Instruction:
    __slots__ = ("offset", "opcode", "mnemonic", "args", "size")

    def __init__(self, offset, opcode, mnemonic, args, size):
        self.offset = offset
        self.opcode = opcode
        self.mnemonic = mnemonic
        self.args = args
        self.size = size


def decode_at(raw, offset, dialect):
    """Decode the instruction at `offset`. Returns an Instruction, or None when
    the byte is not an opcode or the block ends inside the instruction."""
    if not 0 <= offset < len(raw):
        return None
    opcode = raw[offset]
    d = dialect.opcodes.get(opcode)
    if d is None:
        return None
    mnemonic, fixed, count_at = d
    pos = offset + 1
    args = []

    def operand():
        nonlocal pos
        if pos + 2 > len(raw):
            return False
        code, value = raw[pos], raw[pos + 1]
        pos += 2
        text = None
        if code in WORD_CODES:
            if pos >= len(raw):
                return False
            value |= raw[pos] << 8
            pos += 1
        elif code == CODE_STRING:
            if pos + value > len(raw):
                return False
            text = decode_string(raw, pos, value)
            pos += value
        args.append(Operand(code, value, text))
        return True

    for _ in range(fixed):
        if not operand():
            return None
    if count_at is not None:
        for _ in range(args[count_at].word):
            if not operand():
                return None
    return Instruction(offset, opcode, mnemonic, args, pos - offset)


class Block:
    """One area's script: the raw bytes, its dialect and its five entry points."""

    def __init__(self, raw, dialect, block_id=None):
        if len(raw) < dialect.header_len:
            raise EclError("block is %d bytes, shorter than its header" % len(raw))
        self.raw = raw
        self.dialect = dialect
        self.block_id = block_id
        base = 2 if dialect.magic else 0
        if dialect.magic and struct.unpack_from("<H", raw, 0)[0] != 0x1388:
            raise EclError("no 0x1388 magic word")
        self.entries = [struct.unpack_from("<HH", raw, base + 4 * i)[1]
                        for i in range(5)]

    def offset_of(self, vaddr):
        return vaddr - self.dialect.vbase

    def in_code(self, offset):
        return self.dialect.header_len <= offset < len(self.raw)

    def walk(self):
        """Follow control flow from the five entry points.

        Returns (instructions by offset, halts) where a halt is an offset the
        walk reached that does not decode. The rules are Greybox's
        (reachability.ts): an IF_* guards exactly the next instruction, so both
        it and the one after are reachable; GOTO goes only to its target; GOSUB
        and ON_GOSUB also return; EXIT, RETURN and NEWECL end the path. A
        target outside this block's code is another block's and is not followed."""
        seen, halts = {}, []
        work = [self.offset_of(v) for v in self.entries]
        while work:
            off = work.pop()
            if off in seen or not self.in_code(off):
                continue
            ins = decode_at(self.raw, off, self.dialect)
            if ins is None:
                if off not in halts:
                    halts.append(off)
                continue
            seen[off] = ins
            nxt = off + ins.size
            op = ins.opcode
            if IF_MIN <= op <= IF_MAX:
                work.append(nxt)
                guarded = decode_at(self.raw, nxt, self.dialect)
                if guarded is not None:
                    work.append(nxt + guarded.size)
            elif op in (OP_EXIT, OP_RETURN, OP_NEWECL):
                pass
            elif op == OP_GOTO:
                work.append(self.offset_of(ins.args[0].word))
            elif op == OP_GOSUB:
                work.append(self.offset_of(ins.args[0].word))
                work.append(nxt)
            elif op in (OP_ON_GOTO, OP_ON_GOSUB):
                for a in ins.args[2:]:
                    work.append(self.offset_of(a.word))
                if op == OP_ON_GOSUB:
                    work.append(nxt)
            else:
                work.append(nxt)
        return seen, halts

    def listing(self):
        seen, halts = self.walk()
        names = {}
        for name, v in zip(ENTRY_NAMES, self.entries):
            names.setdefault(self.offset_of(v), []).append(name)
        out = []
        for off in sorted(seen):
            for n in names.get(off, ()):
                out.append("%s:" % n)
            ins = seen[off]
            out.append("  %04x  %-16s %s" % (off + self.dialect.vbase, ins.mnemonic,
                                             ", ".join(repr(a) for a in ins.args)))
        for off in halts:
            out.append("  %04x  <does not decode: byte 0x%02x>"
                       % (off + self.dialect.vbase, self.raw[off]))
        return "\n".join(out)


# ---- containers -----------------------------------------------------------

def dax_blocks(data):
    """A DOS DAX archive -> {block id: decompressed bytes} (docs/dax-format.md)."""
    if len(data) < 2:
        raise EclError("not a DAX archive")
    (toc,) = struct.unpack_from("<H", data, 0)
    if toc % 9 or 2 + toc > len(data):
        raise EclError("not a DAX archive")
    base = 2 + toc
    out = {}
    for i in range(toc // 9):
        bid, off, raw_size, comp = struct.unpack_from("<BIHH", data, 2 + 9 * i)
        src = data[base + off:base + off + comp]
        if len(src) != comp:
            raise EclError("DAX block %d runs past the file" % bid)
        buf = bytearray()
        p = 0
        while p < len(src):
            c = src[p]
            p += 1
            if c < 0x80:                       # literal run of c + 1
                buf += src[p:p + c + 1]
                p += c + 1
            else:                              # one byte repeated 256 - c times
                buf += src[p:p + 1] * (256 - c)
                p += 1
        if len(buf) != raw_size:
            raise EclError("DAX block %d decompressed to %d bytes, header says %d"
                           % (bid, len(buf), raw_size))
        out[bid] = bytes(buf)
    return out


def misframing(blocks):
    """How badly a dialect fits: halts, plus bytes claimed by two instructions.

    Both are zero when the operand table is right and neither can be when it is
    wrong for long — a mis-sized operand lands the walk mid-instruction, where
    it either meets a byte that is no opcode or decodes a second instruction
    over the first. Champions and Dark Queen score 0 under their own tables;
    Death Knights read as Champions scores 244 halts and 3,460 doubled bytes."""
    bad = 0
    for b in blocks:
        seen, halts = b.walk()
        bad += len(halts)
        used = bytearray(len(b.raw))
        for off, ins in seen.items():
            for k in range(off, off + ins.size):
                bad += used[k]
                used[k] = 1
    return bad


def load_blocks(data, dialect=None):
    """Every script in a file, as [Block]: a Dark Queen ECL.GLB library (Gen2)
    or a DOS ECL*.DAX archive (Gen1). A DAX archive does not say which Gen1
    game it is from, so unless told, the Gen1 dialect that frames it best wins
    (see misframing)."""
    if data[:4] in (b"GLIB", b"HLIB"):
        from gbgeo import read_library, read_id_table, GbGeoError
        try:
            end, members = read_library(data)
            table = read_id_table(members[0], end)
        except GbGeoError as e:
            raise EclError(str(e))
        blocks = []
        for area_id, idx in table:
            if not 0 < idx < len(members):
                raise EclError("id table points at member %d of %d"
                               % (idx, len(members)))
            blocks.append(Block(members[idx], dialect or GEN2, area_id))
        return blocks
    raws = sorted(dax_blocks(data).items())
    if dialect is not None:
        return [Block(raw, dialect, bid) for bid, raw in raws]
    best = None
    for d in (GEN1, GEN1_DOK):
        blocks = [Block(raw, d, bid) for bid, raw in raws]
        score = misframing(blocks)
        if best is None or score < best[0]:
            best = (score, blocks)
    return best[1]


def census(blocks):
    """(instructions reached, halts, opcode histogram, inline strings)."""
    total = halts = 0
    hist, strings = {}, []
    for b in blocks:
        seen, bad = b.walk()
        total += len(seen)
        halts += len(bad)
        for ins in seen.values():
            hist[ins.mnemonic] = hist.get(ins.mnemonic, 0) + 1
            strings += [a.text for a in ins.args if a.code == CODE_STRING]
    return total, halts, hist, strings


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    dialect = None
    if "--dialect" in argv:
        name = argv[argv.index("--dialect") + 1]
        if name not in DIALECTS:
            raise EclError("dialect is one of: " + ", ".join(sorted(DIALECTS)))
        dialect = DIALECTS[name]
    with open(argv[0], "rb") as f:
        blocks = load_blocks(f.read(), dialect)
    print("dialect: %s" % blocks[0].dialect.name)
    if "--block" in argv:
        want = int(argv[argv.index("--block") + 1], 0)
        for b in blocks:
            if b.block_id == want:
                print(b.listing())
                return 0
        raise EclError("no block %d" % want)
    for b in blocks:
        seen, bad = b.walk()
        print("block %-3s %5d bytes  %4d instructions reached  %d halt(s)"
              % (b.block_id, len(b.raw), len(seen), len(bad)))
    total, halts, hist, strings = census(blocks)
    print("\n%d blocks, %d instructions reached, %d halt(s), %d inline strings"
          % (len(blocks), total, halts, len(strings)))
    print("most used: " + ", ".join("%s %d" % kv for kv in
                                    sorted(hist.items(), key=lambda kv: -kv[1])[:12]))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except EclError as e:
        sys.exit("ecl: %s" % e)
