"""gbdaxmon.py — Champions' and Death Knights' monsters and items, read as the
games keep them, in the forms the engine already takes from the Dark Queen
(ADR-0026: the games' own files, read as they are kept).

The two older Krynn games keep a monster as a CHARACTER: one block of
MON<n>CHA.DAX, 409 bytes in Champions, 216 in Death Knights, with its items
(MON<n>ITM.DAX, 63-byte item records), its effects (MON<n>SPC.DAX, 9-byte
nodes) and, in Death Knights, a caster's spells (MON1WIZ.DAX, 220 bytes)
under the same block number. Nothing here guesses at those layouts: each
game carried its predecessor's characters forward, and the code that does
it is read here as the field map.

  Champions -> Death Knights   GAME.OVR (Death Knights) file 0x12cb4: ten
      Move()s of the 409-byte record into the 216-byte one (COK_TO_DKK),
      pointers cleared; a character whose class is not 2, 6 or 14 also
      gets the 220-byte caster block, whose first 0x3a bytes are the
      memorized spells (Champions 0x1e..0x57).
  Death Knights -> Dark Queen  DQK.EXE file 0x33ec5: 62 movmem()s of the
      216-byte record into the Dark Queen's 414-byte one (DKK_TO_DQK),
      with three adjustments kept in order (dkk_to_dqk); 0x34858 the
      effects, their numbers through 0x34e27 (AFFECT_MAP); 0x3497b the
      items, words through 0x34d51 (WORD_MAP), spells through 0x35003
      (SPELL_MAP), and the "is a scroll" test 1d99:0e69 = overlay 10,
      file 0x59ab7: types 39, 40 and 73 (SCROLL_TYPES).

The Dark Queen's form is what tools/gbmon.py already turns into FRUA's
450-byte record, so a monster goes Champions -> Death Knights -> Dark Queen
-> FRUA, or Death Knights -> Dark Queen -> FRUA. One step is this port's:
the Dark Queen's import leaves the caster block behind (a character
memorizes again), and a monster cannot, so its memorized spells go to the
Dark Queen's list at 0xce through SPELL_MAP as well. The final clamp at
DQK 0x34787 (four fields held to a limit 1d99:13a5 computes for a player
character) is not applied.

ITEMS. An item record is 63 bytes: a Pascal name (42), a list pointer (4)
and 17 bytes in the order type, three words, plus, plus to saves, readied,
hidden, cursed, weight (2), count, value (2), three effects. Death Knights'
item table (ITEM0.DAX, one block) holds just those 17 bytes an entry. The
Dark Queen's 17-byte entry (ITEM.DAT, what the engine's item table takes)
has the same fields reordered (item17). Champions has no table: its
treasure names a block of ITEM<n>.DAX, whole records, so one is built from
every record the game has (item_table), in a fixed order both the monsters
and the treasure count in.

WORDS. Item names are three word numbers. Both games' words (START.EXE,
packed with Microsoft EXEPACK; a table of string[20] from "Battle Axe",
word 1) are FRUA's but for twenty, which the Dark Queen moves to its own
numbers past FRUA's 126 (WORD_MAP); design_words() gives those numbers the
game's own words.

CHARACTERS. A player character is the same record in a file of its own:
Death Knights' NAME.GAM (216 bytes) with .WIZ (its caster block, 220),
.JNK (its items, 63-byte records) and .EFF (its effects, 9-byte nodes);
Champions' NAME.SAV (409) with .STF (items) and .SFX (effects). A saved
game's party is the same files named CHRDAT<save letter><place>, which
the Gold Box halls list too ("from save game A", COAB ovr017). They go
to the Dark Queen's .QCH (dqk_character, character()), which the engine
already reads as a hero (qch.c). The caster block, read off the
Champions import that builds one (GAME.OVR 0x12e81, COK_WIZ):

    0x00..0x49   memorized spells; Champions' 58 at the front, Death
                 Knights' kept sorted with the zeros first
    0x4a..0xb4   a byte a spell, set when the spell is known (id - 1)
    0xbc + 8 g   spell slots by spell level, four groups: 0 the cleric's,
                 3 the mage's (as the heroes' levels show); 1 and 2 are
                 empty in every save here and are not carried

The two games number spells alike (START.EXE's string[40] tables differ
only where Death Knights reused the evil spells' numbers), so Death
Knights' import translates none.

    python3 tools/gbdaxmon.py GAMEFOLDER              # the bestiary, converted
    python3 tools/gbdaxmon.py GAMEFOLDER --items      # the item table

The game data is the user's own; nothing here ships any.
"""
import glob
import os
import struct
import sys

import ecl
import gbmon

COK_SIZE, DKK_SIZE, DQK_SIZE = 409, 216, 0x19e
WIZ_SIZE, ITEM_REC, NODE = 220, 63, 9
ITEM_TAIL = 0x2e                    # where the 17 item bytes start in a record

# Death Knights' GAME.OVR, the Champions import: (from, to, count)
COK_TO_DKK = ((0x000, 0x00, 0x1e), (0x058, 0x1e, 0x0b), (0x0cf, 0x29, 0x08),
              (0x0da, 0x31, 0x0c), (0x0f3, 0x3f, 0x27), (0x11a, 0x6b, 0x02),
              (0x130, 0x6d, 0x03), (0x135, 0x70, 0x0c), (0x142, 0x7c, 0x46),
              (0x188, 0xc7, 0x11))
COK_DKK_CLEAR = (0x7d, 0x3a, 0xbd, 0xb9, 0xc2)      # four-byte pointers
COK_NO_WIZ = (2, 6, 14)                             # classes without one
COK_MEMORIZED = (0x1e, 0x3a)                        # -> caster block 0
WIZ_MEMORIZED = 0x3a        # what a monster's block holds (none reaches 0x3a)
# the caster block whole, as the Champions import builds it: (from, to, count)
COK_WIZ = ((0x1e, 0x00, 0x3a), (0x63, 0x4a, 0x6b)) + tuple(
    (0x11c + 5 * g, 0xbc + 8 * g, 5) for g in range(4))
WIZ_CHAR_MEMORIZED = 0x4a   # a character's list, right up to the known bytes
WIZ_KNOWN, WIZ_KNOWN_N = 0x4a, 0x6b                 # a byte a spell, id - 1
WIZ_SLOT = 0xbc                                     # four groups of 8 levels
WIZ_SLOTS = ((0, 0x167), (3, 0x179))                # cleric, mage -> DQK rows
DQK_KNOWN, DQK_KNOWN_N = 0x157, 128

# DQK.EXE, the Death Knights import: (from, to, count), in the game's order
DKK_TO_DQK = (
    (0x01, 0x068, 15), (0x10, 0x078, 12), (0x1c, 0x084, 1), (0x1d, 0x085, 1),
    (0x1e, 0x086, 1), (0x1f, 0x087, 1), (0x20, 0x058, 1), (0x21, 0x05a, 1),
    (0x22, 0x19a, 1), (0x23, 0x19b, 1), (0x24, 0x19d, 1), (0x25, 0x088, 1),
    (0x26, 0x052, 2), (0x28, 0x089, 1), (0x29, 0x08a, 1), (0x2a, 0x08b, 5),
    (0x2f, 0x090, 1), (0x30, 0x091, 1), (0x31, 0x05c, 1), (0x32, 0x093, 8),
    (0x3e, 0x09b, 1), (0x3f, 0x04c, 6), (0x4d, 0x09e, 8), (0x45, 0x0a5, 8),
    (0x55, 0x060, 1), (0x56, 0x062, 1), (0x57, 0x0b3, 2), (0x59, 0x0b5, 2),
    (0x5b, 0x0b7, 2), (0x5d, 0x0b9, 2), (0x5f, 0x0bb, 1), (0x60, 0x0bc, 1),
    (0x61, 0x0bd, 1), (0x62, 0x044, 4), (0x66, 0x048, 4), (0x6a, 0x0be, 1),
    (0x6b, 0x0bf, 1), (0x6c, 0x0c0, 1), (0x6d, 0x054, 2), (0x70, 0x0c3, 1),
    (0x71, 0x0c4, 1), (0x72, 0x0c5, 1), (0x73, 0x0c6, 1), (0x7a, 0x0c7, 1),
    (0x7b, 0x0c8, 1), (0x7c, 0x0c9, 1), (0xb5, 0x0ca, 1), (0xb6, 0x0cb, 1),
    (0xb7, 0x056, 2), (0xc1, 0x0cc, 1), (0xc6, 0x0cd, 1), (0xc7, 0x064, 1),
    (0xc8, 0x18b, 1), (0xc9, 0x066, 1), (0xca, 0x18c, 1), (0xcb, 0x18d, 1),
    (0xcc, 0x18e, 2), (0xce, 0x190, 2), (0xd0, 0x192, 2), (0xd2, 0x194, 2),
    (0xd4, 0x196, 2), (0xd6, 0x198, 1))
DQK_MEMORIZED = 0xce

# DQK.EXE's translations, Death Knights' numbers to its own; any other
# number is kept
WORD_MAP = {12: 138, 38: 139, 40: 140, 52: 141, 59: 142, 67: 126, 68: 143,
            69: 127, 74: 144, 75: 145, 76: 146, 77: 129, 84: 147, 85: 148,
            86: 149, 96: 150, 105: 135, 119: 151, 120: 152, 124: 153}
SPELL_MAP = {2: 90, 4: 96, 7: 105, 17: 102, 40: 56, 53: 36, 56: 110, 57: 127,
             59: 53, 60: 111, 61: 130, 62: 131, 63: 112, 64: 133, 65: 134,
             66: 113, 72: 114, 90: 119, 95: 135, 96: 132, 97: 136, 98: 118,
             99: 137, 101: 57, 102: 59, 103: 60, 104: 61, 105: 62, 106: 63,
             107: 64, 108: 115, 109: 116, 110: 117, 111: 120, 112: 122,
             113: 121, 114: 123}
AFFECT_MAP = {2: 0x81, 6: 0x82, 7: 0x83, 9: 0x4a, 13: 0x47, 32: 0x45,
              34: 0x20, 40: 0, 48: 13, 57: 0x76, 58: 0x7b, 59: 0x39,
              60: 0x7a, 61: 0x44, 65: 6, 66: 0x3c, 67: 0x65, 68: 0x66,
              69: 0x84, 70: 0x85, 74: 0x79, 75: 0x86, 76: 0x87, 77: 0x88,
              78: 0, 79: 0x89, 80: 0x8a, 81: 0x7c, 85: 0, 87: 0x4f, 90: 0,
              91: 0x8c, 93: 0x42, 96: 0x8d, 97: 0x8e, 99: 0x8f, 100: 0,
              104: 0, 106: 0, 108: 0, 114: 0xc8, 115: 0x91, 116: 0x92,
              117: 0x93, 118: 0x94, 120: 0, 121: 0}
SCROLL_TYPES = (39, 40, 73)

FIRST_WORD = 126                     # FRUA's own words are 0..125
WORDS = 125                          # each game's table, from word 1
ITEMS_MAX = 254


class GbDaxMonError(ValueError):
    pass


# ---- the games' words ---------------------------------------------------

def unexepack(exe):
    """A Microsoft EXEPACK program's load image, unpacked."""
    if exe[:2] != b"MZ":
        raise GbDaxMonError("not an MZ program")
    cblp, cp, _, hdr = struct.unpack_from("<4H", exe, 2)
    cs = struct.unpack_from("<H", exe, 0x16)[0]
    size = cp * 512 - (512 - cblp if cblp else 0)
    img = exe[hdr * 16:size]
    if len(img) < cs * 16 + 16:
        raise GbDaxMonError("no EXEPACK header")
    dest = struct.unpack_from("<H", img, cs * 16 + 12)[0] * 16
    out = bytearray(max(dest, cs * 16))
    out[:cs * 16] = img[:cs * 16]
    src, dst = cs * 16, dest
    while src > 0 and img[src - 1] == 0xff:
        src -= 1
    while True:
        if src < 3:
            raise GbDaxMonError("packed data runs out")
        cmd = img[src - 1]
        n = struct.unpack_from("<H", img, src - 3)[0]
        src -= 3
        if cmd & 0xfe == 0xb0:
            out[dst - n:dst] = img[src - 1:src] * n
            src -= 1
        elif cmd & 0xfe == 0xb2:
            out[dst - n:dst] = img[src - n:src]
            src -= n
        else:
            raise GbDaxMonError("not an EXEPACK record (%02x)" % cmd)
        dst -= n
        if cmd & 1:
            return bytes(out)


def game_words(start_exe):
    """[None, word 1, .. word 125] out of the game's START.EXE."""
    img = unexepack(start_exe)
    at = img.find(b"\x0aBattle Axe")
    if at < 0:
        raise GbDaxMonError("no word table")
    out = [None]
    for k in range(WORDS):
        p = at + 21 * k
        if img[p] > 20:
            raise GbDaxMonError("word %d is not a string[20]" % (k + 1))
        out.append(img[p + 1:p + 1 + img[p]].decode("latin-1"))
    return out


def design_words(words):
    """{word number past FRUA's: the game's word} for the twenty WORD_MAP
    moves there."""
    return {to: words[k] for k, to in WORD_MAP.items()}


# ---- items ----------------------------------------------------------------

def item17(t):
    """The 17 item bytes as Death Knights keeps them -> the Dark Queen's
    entry, as DQK.EXE 0x3497b turns them (the readied byte kept)."""
    if len(t) < 17:
        raise GbDaxMonError("an item is 17 bytes")
    q = bytearray(17)
    q[0] = t[0]
    q[1:4] = bytes(WORD_MAP.get(w, w) for w in t[1:4])
    q[4:6] = t[9:11]                 # weight
    q[6:8] = t[12:14]                # value
    q[8:13] = t[4:9]                 # plus, saves, readied, hidden, cursed
    q[13] = t[11]                    # count
    q[14:17] = t[14:17]
    lo = lambda b: (SPELL_MAP.get(b & 0x7f, b & 0x7f) | (b & 0x80)) & 0xff
    if q[0] in SCROLL_TYPES:
        q[14:17] = bytes(lo(b) for b in q[14:17])
    elif q[16] == 0x80:
        q[15] = AFFECT_MAP.get(q[15], q[15])
    else:
        q[15] = lo(q[15])
    if q[0] == 0x34:
        q[0], q[16] = 0x33, 0
    return bytes(q)


def records(block):
    """The 63-byte item records of a block (a trailing part left off)."""
    return [block[i:i + ITEM_REC] for i in range(0, len(block) - ITEM_REC + 1, ITEM_REC)]


def table_key(q):
    """An entry as the item table keeps it: not readied."""
    return q[:10] + b"\0" + q[11:]


def archives(folder, pattern):
    """[(archive number, path)] in number order."""
    out = []
    for p in glob.glob(os.path.join(folder, pattern)):
        digits = "".join(c for c in os.path.basename(p) if c.isdigit())
        out.append((int(digits or 0), p))
    return sorted(out)


def blocks_in_order(path):
    """[(id, block)] as the archive lists them, an id seen before left off."""
    d = open(path, "rb").read()
    toc = struct.unpack_from("<H", d, 0)[0]
    blocks = ecl.dax_blocks(d)
    out, seen = [], set()
    for k in range(toc // 9):
        bid = d[2 + 9 * k]
        if bid not in seen:
            seen.add(bid)
            out.append((bid, blocks[bid]))
    return out


def item_table(folder):
    """The game's item table as Dark Queen entries, 17 bytes each, entry k
    being item k + 1: Death Knights' ITEM0.DAX as it is (its scripts' item
    numbers), then every distinct item of ITEM1..9.DAX and MON1..9ITM.DAX
    not already there, in archive and block order. Champions has only the
    second part; a monster's copy of an item (Death Knights' carry value 0
    and their own weights) is an entry of its own."""
    out = []
    t0 = archives(folder, "ITEM0.DAX")
    if t0:
        blk = blocks_in_order(t0[0][1])[0][1]
        out = [table_key(item17(blk[i:i + 17])) for i in range(0, len(blk) - 16, 17)]
    seen = set(out)
    for pattern in ("ITEM[1-9].DAX", "MON[1-9]ITM.DAX"):
        for _, path in archives(folder, pattern):
            for _, blk in blocks_in_order(path):
                for r in records(blk):
                    q = table_key(item17(r[ITEM_TAIL:ITEM_TAIL + 17]))
                    if q not in seen and len(out) < ITEMS_MAX:
                        seen.add(q)
                        out.append(q)
    return out


def treasure_items(folder, file_no, block_id):
    """Champions' TREASURE item block: the item numbers of ITEM<n>.DAX's
    block, found in item_table by the whole entry."""
    table = {q: k + 1 for k, q in enumerate(item_table(folder))}
    path = os.path.join(folder, "ITEM%d.DAX" % file_no)
    blk = dict(blocks_in_order(path)).get(block_id)
    if blk is None:
        return []
    return [table.get(table_key(item17(r[ITEM_TAIL:ITEM_TAIL + 17])), 0)
            for r in records(blk)]


# ---- monsters -------------------------------------------------------------

def cok_to_dkk(c):
    """A Champions character (409 bytes) -> (Death Knights' 216, its
    memorized spells or None), as Death Knights imports one."""
    if len(c) < COK_SIZE:
        raise GbDaxMonError("a Champions character is %d bytes" % COK_SIZE)
    k = bytearray(DKK_SIZE)
    for src, dst, n in COK_TO_DKK:
        k[dst:dst + n] = c[src:src + n]
    for p in COK_DKK_CLEAR:
        k[p:p + 4] = bytes(4)
    mem = None
    if k[0x21] not in COK_NO_WIZ:
        mem = bytes(c[COK_MEMORIZED[0]:COK_MEMORIZED[0] + COK_MEMORIZED[1]])
    return bytes(k), mem


def dkk_to_dqk(k):
    """A Death Knights character (216 bytes) -> the Dark Queen's fixed
    record (414 bytes), as the Dark Queen imports one."""
    if len(k) < DKK_SIZE:
        raise GbDaxMonError("a Death Knights character is %d bytes" % DKK_SIZE)
    q = bytearray(DQK_SIZE)
    for src, dst, n in DKK_TO_DQK:
        if src == 0x45:                         # 0x34222..0x3427b
            q[0x9f] = q[0xa5]
        q[dst:dst + n] = k[src:src + n]
        if src == 0x01:                         # the name, NUL-ended
            q[0x68 + min(k[0], 15)] = 0
        elif src == 0x21 and q[0x5a] == 7 and q[0x5b] == 0:
            q[0x5a] = 1
        elif src == 0x45:
            q[0xa6] = q[0xac]
            q[0xac:0xb4] = bytes(8)
    return q


def effects(spc):
    """MON<n>SPC.DAX's 9-byte nodes -> the Dark Queen's 5-byte effects."""
    out = []
    for i in range(0, len(spc) - NODE + 1, NODE):
        e = spc[i:i + 5]
        out.append(bytes([AFFECT_MAP.get(e[0], e[0])]) + bytes(e[1:5]))
    return out


def dqk_member(q, mem, effs, items):
    """The Dark Queen's MONCHA.GLB member gbmon.convert reads."""
    q = bytearray(q)
    if mem:
        for i, s in enumerate(mem[:WIZ_MEMORIZED]):
            q[DQK_MEMORIZED + i] = SPELL_MAP.get(s, s) if s else 0
    return (bytes(q) + bytes([len(effs), len(items)])
            + b"".join(effs) + b"".join(items))


def monster(folder, file_no, block_id):
    """Monster `block_id` of MON<file_no>CHA.DAX as the Dark Queen's
    member, or None."""
    def block(stem):
        path = os.path.join(folder, "MON%d%s.DAX" % (file_no, stem))
        return dict(blocks_in_order(path)).get(block_id) if os.path.exists(path) else None

    cha = block("CHA")
    if cha is None:
        return None
    if len(cha) >= COK_SIZE:
        k, mem = cok_to_dkk(cha)
    else:
        k = cha
        wiz = block("WIZ")
        mem = wiz[:WIZ_MEMORIZED] if wiz else None
    spc, itm = block("SPC") or b"", block("ITM") or b""
    items = [item17(r[ITEM_TAIL:ITEM_TAIL + 17]) for r in records(itm)]
    return dqk_member(dkk_to_dqk(k), mem, effects(spc), items)


def frua_monster(folder, file_no, block_id, native=False):
    """(FRUA's 450-byte record, notes) or None."""
    m = monster(folder, file_no, block_id)
    if m is None:
        return None
    table = item_table(folder)
    index = gbmon.item_index(b"".join(table))
    return gbmon.convert(m, index, native)


# ---- characters -----------------------------------------------------------

def cok_wiz(c):
    """A Champions character's caster block as Death Knights builds it on
    import (GAME.OVR 0x12e81..0x12f45: allocated, zeroed, then COK_WIZ),
    or None for a class without one."""
    k, _ = cok_to_dkk(c)
    if k[0x21] in COK_NO_WIZ:
        return None
    w = bytearray(WIZ_SIZE)
    for src, dst, n in COK_WIZ:
        w[dst:dst + n] = c[src:src + n]
    return bytes(w)


def dqk_character(k, wiz, eff, jnk):
    """A Death Knights character (the 216-byte .GAM, its 220-byte .WIZ or
    None, its .EFF nodes and .JNK item records) -> a Dark Queen .QCH: the
    fixed record, the items, the effect nodes (tools/gbchar.py's layout).

    The record goes across as the Dark Queen imports one (dkk_to_dqk).
    The caster block, which that import leaves behind, is this port's:
    the memorized spells (sorted, zeros first, in Death Knights) go to the
    list at 0xce, packed; each known spell (a byte a spell, WIZ_KNOWN + id
    - 1) sets its bit at 0x157 (bit (n - 1) & 7 of byte (n - 1) >> 3, the
    Dark Queen's heroes and FRUA's rec[339] alike); both through
    SPELL_MAP, and a spell past the 128 bits is not known. Of the slot
    groups the cleric's and the mage's go to the Dark Queen's slot rows
    (WIZ_SLOTS); the engine rebuilds them on its next recompute anyway."""
    q = bytearray(dkk_to_dqk(k))
    if wiz and len(wiz) >= WIZ_SIZE:
        mem = [s for s in wiz[:WIZ_CHAR_MEMORIZED] if s]
        for i, s in enumerate(mem):
            q[DQK_MEMORIZED + i] = SPELL_MAP.get(s, s)
        for i in range(WIZ_KNOWN_N):
            if wiz[WIZ_KNOWN + i]:
                n = SPELL_MAP.get(i + 1, i + 1)
                if 1 <= n <= DQK_KNOWN_N:
                    q[DQK_KNOWN + ((n - 1) >> 3)] |= 1 << ((n - 1) & 7)
        for group, at in WIZ_SLOTS:
            q[at:at + 8] = wiz[WIZ_SLOT + 8 * group:WIZ_SLOT + 8 * group + 8]
    items = [item17(r[ITEM_TAIL:ITEM_TAIL + 17]) for r in records(jnk)][:255]
    nodes = [eff[i:i + NODE] for i in range(0, len(eff) - NODE + 1, NODE)][:64]
    q[8] = len(items)
    q[0xc9] = len(items)
    return (bytes(q) + b"".join(items)
            + b"".join(bytes([AFFECT_MAP.get(e[0], e[0])]) + e[1:] for e in nodes))


CHAR_FILES = {".GAM": (".WIZ", ".EFF", ".JNK"),      # Death Knights
              ".SAV": (None, ".SFX", ".STF")}        # Champions


def character(path):
    """A game's own character file (Death Knights' NAME.GAM or a save's
    CHRDATxn.GAM, Champions' .SAV) and the files beside it of the same
    stem -> the Dark Queen's .QCH, or None for a file that is not one."""
    stem, ext = os.path.splitext(path)
    if ext.upper() not in CHAR_FILES:
        return None

    def side(e):
        for p in (stem + e, stem + e.lower()):
            if os.path.exists(p):
                return open(p, "rb").read()
        return b""

    rec = open(path, "rb").read()
    w, e, j = CHAR_FILES[ext.upper()]
    if ext.upper() == ".SAV":
        if len(rec) != COK_SIZE:
            return None
        k, wiz = cok_to_dkk(rec)[0], cok_wiz(rec)
    else:
        if len(rec) != DKK_SIZE:
            return None
        k, wiz = rec, side(w) or None
    return dqk_character(k, wiz, side(e), side(j))


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    folder = argv[0]
    if "--items" in argv:
        for k, q in enumerate(item_table(folder)):
            print("%3d  %s" % (k + 1, q.hex(" ")))
        return 0
    for file_no, path in archives(folder, "MON[0-9]CHA.DAX"):
        for bid, _ in blocks_in_order(path):
            rec, notes = frua_monster(folder, file_no, bid)
            print("MON%d #%-3d %-16s %s" % (file_no, bid, gbmon.name_of(rec, True),
                                            "; ".join(notes)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
