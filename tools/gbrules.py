"""gbrules.py — a Gold Box game's class and race rules, read from its EXE (ADR-0026).

FRUA plays by about twenty tables in the A5 world (src/engine/rules.h names
them). The Dark Queen of Krynn plays by the same tables, kept in DQK.EXE's
data segment in the same order as FRUA's DOS executable (CKIT.EXE) keeps
its own, but holding Krynn's rules: seven player races, a Knight where
FRUA's slot 1 is a druid, no evil characters.

This file is the reference description of where those tables are and how
each becomes the layout the engine indexes (the Mac's: big-endian, FRUA's
row strides). The engine's own loader, src/engine/rules_gb.c, reads the
EXE at run time and is held to this file by tests/test_rules_gb.py.

WHERE. Each table is at a fixed offset in the data segment (DS, at file
offset 0x63a20 in the Steam DQK.EXE) and carries the 16-bit sum of its
bytes, as src/engine/a4_map.c does for CKIT.EXE: a different build is
refused rather than read from the wrong place, and the sums carry no
values. Found by matching FRUA's tables against DQK.EXE by shape: every
per-slot table from THAC0 to the thief skills sits exactly where it sits
in CKIT.EXE relative to the others.

WHAT DIFFERS FROM FRUA'S SHAPES

    races            7 player races (FRUA 6); the level caps, ability
                     ranges, ages and thief adjustments have 7 rows
    race_kinds       DOS rows are {count, 14 words} (CKIT: 13); a
                     half-elf may be any of 14 class kinds, so the
                     engine's row is 15 bytes, not FRUA's 14
    progressions     XP, the XP step past the table and the spell row
                     are kept for 11 PROGRESSIONS, not the 7 slots:
                     clerics by their god's alignment, knights by order,
                     mages by robe (rows named below, checked against the
                     Adventurer's Journal's Level Advancement Tables)
    xp levels        19 thresholds a row (FRUA 12)
    spell slots      FRUA's table shape (29 levels x 9 spell levels a row),
                     8 rows: the spell row of each progression names one

Saving throws, THAC0, hit dice, name level, the item and training-hall
class bits and the thief skill table keep FRUA's shapes, by slot.

THE THIEF RACE ADJUSTMENTS are the Player's Handbook's rows in Pool of
Radiance's race order (dwarf, elf, gnome, half-elf, halfling, half-orc,
human), and DQK indexes them by its own race word (DQK 0x62819: race * 8 +
0x64b5): a Qualinesti thief gets the elf's row, but a kender gets the
half-orc's and a hill dwarf the halfling's. That is how the game plays, so
the rows are read as they stand.

The names are DQK's own pointer tables: the class kinds (the "Magic-User"
table, as FRUA's) and the races, eight with "Monster". The creation
screen's labels ("SILVANESTI ELF", "CLERIC/FIGHTER/M-U") are one block of
NUL-ended strings its code addresses directly: 7 races, 2 genders, 17
kinds, in that order.

CHAMPIONS AND DEATH KNIGHTS (read_dax) keep the same tables in START.EXE's
data segment (a Turbo Pascal program, EXEPACKed; DS from System's start-up,
`mov dx, DS`), and the code that reads each is in GAME.OVR, the overlays.
Each table's shape is the one its code indexes (the row width it multiplies
by, the index it starts from), not guessed from the bytes; the game file
offsets below are GAME.OVR's. What differs from the Dark Queen's:

    slots        8, Pool of Radiance's: cleric, druid, fighter, paladin,
                 ranger, mage, thief, and the Knight in the monk's slot 7.
                 The Dark Queen's import moves class 7 to 1 (DQK.EXE
                 0x33ec5, the word at 0x5a), and so does this: every slot
                 table is read by slot 0, 7, 2..6, every kind table and
                 kind number has kinds 1 and 7 swapped (Druid where the
                 Dark Queen has Monk: offered to no race)
    levels       THAC0 0..18 (Champions 0..12), saves 1..18 (1..12; the
                 code indexes row - 5, level 0 is the slot before's last),
                 thief skills 1..18 (1..12). The engine's rows are longer:
                 the top level is held, and saves' level 0 is level 1's
    progressions one 204-byte record each (Champions 99): XP to reach
                 levels 2..18 (2..12), longs, then the spells GAINED at
                 each of those levels, 8 spell levels (5); OVR 0x194cd,
                 0x361b9 (Champions 0x250d4, 0x39fcd). The Dark Queen's 11
                 in its order; Champions has 10, no paladin. A threshold
                 of -1 is a level never reached (the code refuses one
                 <= 0): the engine's own "never", 0x7fffffff. The spells
                 are summed into the Dark Queen's cumulative slots, with
                 the one first-level spell the code gives a cleric and a
                 mage at level 1 (OVR 0x36177, 0x36578)
    alignments   a byte of bits per kind, bit j alignment [0 1 3 4 6 7][j]
                 (DS 0x584, OVR 0x161f9): the Dark Queen's lists
    level caps   Death Knights: race x 8 slots (OVR 0x19488), the
                 numbers the game stops at (the Dark Queen writes 40 for
                 none). Champions has NO table: a class stops where its
                 XP does, for every race, but for a Kender fighter or
                 ranger (OVR 0x25061), 5, 6 or 7 by Strength (below 17,
                 17, 18); the cap here is 7, the Strength rule not kept
    names        Pascal strings in place, not pointers: kinds string[26],
                 races [15], gods [11] (0 "None"), robes [6], money
                 [10] (Silver Copper Bronze Platinum Steel Gems Jewelry:
                 the coin is Steel); the orders (" of the Crown") are
                 code literals in GAME.OVR. The creation labels are the
                 names in capitals, as the Dark Queen's labels are
    hit dice     the table's count, but the code (OVR 0x26ea5) gives a
                 cleric of a god above Mishakal one die, the table's 2
                 for the others: the table is what is read

There is no XP step past the table: the step given is one no character
reaches (STEP_NONE).

    python3 tools/gbrules.py DQK.EXE            # print the rules
    python3 tools/gbrules.py DQK.EXE --sums     # the C table of positions
    python3 tools/gbrules.py COK.DSN            # Champions' or Death Knights'
                                                # (the folder: START.EXE, GAME.OVR)

The game data is the user's own; nothing here ships any.
"""
import struct
import sys

DS = 0x63a20               # DQK.EXE (Steam): file offset of the data segment
EXE_SIZE = 449664
DS_SEG = 0x322e            # the segment word in DQK's far pointers

SLOTS = ("cleric", "knight", "fighter", "paladin", "ranger", "mage", "thief")
PROGRESSIONS = ("cleric of good", "cleric of neutrality", "fighter",
                "knight of the crown", "knight of the sword",
                "knight of the rose", "ranger", "white robe mage",
                "red robe mage", "thief", "paladin")

RACES = 7
KINDS = 17
MONEY = 3          # the money types: the coin (Steel), Gems, Jewelry
PROGS = 11
XP_LEVELS = 19
KIND_SLOTS = 14            # race_kinds: kinds a row can list
SPELL_ROWS = 8             # spell_slots: rows of 29 levels x 9 spell levels
GODS, ROBES, ORDERS = 8, 4, 4   # rec[91]'s values, 0 included (rules.h)

# name, DS offset, bytes, 16-bit sum of the bytes in the Steam DQK.EXE
TABLES = (
    ("xp_step",          0x028c, 4 * PROGS,              0x0af5),
    ("race_abilities",   0x02b8, 16 * RACES,             0x06bf),
    ("race_kinds",       0x0328, (1 + 2 * KIND_SLOTS) * RACES, 0x0218),
    ("race_start_age",   0x03f3, 28 * RACES,             0x1011),
    ("race_age_cat",     0x04b7, 10 * RACES,             0x12bb),
    ("kind_ability_min", 0x04fd, 6 * KINDS,              0x0306),
    ("kind_aligns",      0x0563, 21 * KINDS,             0x0222),
    ("spell_row",        0x0847, PROGS,                  0x0319),
    ("xp",               0x0852, 4 * XP_LEVELS * PROGS,  0xb462),
    ("slot_saves",       0x13cc, 110 * 7,                0x1b15),
    ("race_level_cap",   0x17dc, 7 * RACES,              0x037a),
    ("slot_thac0",       0x6359, 22 * 7,                 0x1c23),
    ("slot_item_bit",    0x63f3, 7,                      0x00df),
    ("slot_hall_bit",    0x63fa, 7,                      0x007f),
    ("slot_name_level",  0x640a, 7,                      0x004a),
    ("slot_hd_count",    0x6411, 7,                      0x0009),
    ("slot_hd_size",     0x6418, 7,                      0x0038),
    ("thief_skills",     0x6425, 8 * 18,                 0x237a),
    ("race_thief_adj",   0x64b5, 8 * RACES,              0x085f),
    ("kind_names",       0x3b3e, 4 * KINDS,              0x4bd6),
    ("race_names",       0x3b86, 4 * (RACES + 1),        0x1da4),
    ("money_names",      0x3bd2, 4 * MONEY,              0x066b),
    ("labels",           0x7330, 296,                    0x4b9f),
    ("spell_slots",      0x0b96, 261 * SPELL_ROWS,       0x1308),
    ("sheet_kind_names", 0x3af6, 4 * KINDS,              0x3ea4),
    ("god_names",        0x3c0a, 4 * GODS,               0x1612),
    ("robe_names",       0x3c2a, 4 * ROBES,              0x04f9),
    ("order_names",      0x8d8c, 41,                     0x0d13),
)
NAME_TABLES = ("kind_names", "race_names", "money_names",   # sums: of the
               "sheet_kind_names", "god_names", "robe_names")  # strings


class GbRulesError(ValueError):
    pass


def _table(name):
    for t in TABLES:
        if t[0] == name:
            return t
    raise KeyError(name)


def raw(exe, name):
    _, off, n, _ = _table(name)
    if DS + off + n > len(exe):
        raise GbRulesError("%s: past the end of the file" % name)
    return exe[DS + off:DS + off + n]


def _cstr(exe, off):
    p = DS + off
    e = exe.find(b"\0", p)
    if p >= len(exe) or e < 0 or e - p > 40:
        raise GbRulesError("no string at DS:%04x" % off)
    return exe[p:e].decode("latin-1")


def names(exe, name):
    """A far-pointer table's strings."""
    b = raw(exe, name)
    out = []
    for k in range(0, len(b), 4):
        off, seg = struct.unpack_from("<HH", b, k)
        if seg != DS_SEG:
            raise GbRulesError("%s[%d]: segment %04x, not DS" % (name, k // 4, seg))
        out.append(_cstr(exe, off))
    return out


def _sum(b):
    return sum(b) & 0xffff


def sums(exe):
    """Each table's 16-bit sum; a name table's is the sum of its strings'
    bytes, NULs included (the pointers move with the build, the text not)."""
    out = {}
    for name, _, _, _ in TABLES:
        if name in NAME_TABLES:
            out[name] = _sum(b"".join(s.encode("latin-1") + b"\0"
                                      for s in names(exe, name)))
        else:
            out[name] = _sum(raw(exe, name))
    return out


def verify(exe):
    """Raise unless this is the DQK.EXE the positions were taken from."""
    if len(exe) != EXE_SIZE:
        raise GbRulesError("%d bytes, expected %d: not the DQK.EXE this "
                           "reads" % (len(exe), EXE_SIZE))
    got = sums(exe)
    for name, _, _, want in TABLES:
        if got[name] != want:
            raise GbRulesError("%s: sum %04x, expected %04x" % (name, got[name], want))


def be16(v):
    return struct.pack(">H", v & 0xffff)


def be32(v):
    return struct.pack(">L", v & 0xffffffff)


def engine_tables(exe):
    """{field: bytes} in the layout the engine indexes (rules.h), plus the
    names. Byte tables pass through; DOS words and longs become big-endian;
    race_kinds and kind_aligns lose their DOS word padding."""
    t = {}
    for name in ("race_abilities", "kind_ability_min", "spell_row", "spell_slots",
                 "slot_saves", "race_level_cap", "slot_thac0",
                 "slot_item_bit", "slot_hall_bit", "slot_name_level",
                 "slot_hd_count", "slot_hd_size", "thief_skills",
                 "race_thief_adj"):
        t[name] = bytes(raw(exe, name))

    b = raw(exe, "xp_step")
    t["xp_step"] = b"".join(be32(v) for v in struct.unpack("<%dl" % PROGS, b))
    b = raw(exe, "xp")
    t["xp"] = b"".join(be32(v) for v in
                       struct.unpack("<%dl" % (PROGS * XP_LEVELS), b))
    b = raw(exe, "race_age_cat")
    t["race_age_cat"] = b"".join(be16(v) for v in
                                 struct.unpack("<%dH" % (5 * RACES), b))

    b = raw(exe, "race_start_age")              # {word, dice, sides}
    out = bytearray()
    for k in range(0, len(b), 4):
        out += be16(struct.unpack_from("<H", b, k)[0]) + b[k + 2:k + 4]
    t["race_start_age"] = bytes(out)

    b = raw(exe, "race_kinds")                  # {count, 14 words} -> 15
    out = bytearray()
    for r in range(RACES):
        row = b[r * 29:(r + 1) * 29]
        out += bytes([row[0]]) + bytes(row[1 + 2 * k] for k in range(KIND_SLOTS))
    t["race_kinds"] = bytes(out)

    b = raw(exe, "kind_aligns")                 # {count, 10 words} -> 12
    out = bytearray()
    for r in range(KINDS):
        row = b[r * 21:(r + 1) * 21]
        out += bytes([row[0]]) + bytes(row[1 + 2 * k] for k in range(10)) + b"\0"
    t["kind_aligns"] = bytes(out)

    t["kind_names"] = names(exe, "kind_names")
    t["race_names"] = names(exe, "race_names")
    t["money_names"] = names(exe, "money_names")   # coin, gems, jewelry
    lab = labels(exe)
    t["race_labels"] = lab[:RACES]
    t["kind_labels"] = lab[RACES + 2:RACES + 2 + KINDS]
    t.update(sheet_words(exe))
    return t


def sheet_words(exe):
    """The character sheet's words (DQK 0x5cc86): its own kind names
    ("Mage", not "Magic-User"), the gods a cleric's kind starts with, the
    robes put before "Mage", and the orders put after a knight's; the
    orders are one block of NUL-ended strings, order 0 none."""
    b = bytes(raw(exe, "order_names"))
    orders = b.split(b"\0")
    if len(orders) < ORDERS or b[-1] != 0:
        raise GbRulesError("order_names: not %d NUL-ended strings" % (ORDERS - 1))
    return {"sheet_kind_names": names(exe, "sheet_kind_names"),
            "god_names": names(exe, "god_names"),
            "robe_names": names(exe, "robe_names"),
            "order_names": [""] + [x.decode("latin-1")
                                   for x in orders[:ORDERS - 1]]}


def labels(exe):
    """The creation screen's 26 labels (races, genders, kinds)."""
    b = bytes(raw(exe, "labels"))
    out = b.split(b"\0")
    if len(out) < RACES + 2 + KINDS or b[-1] != 0:
        raise GbRulesError("labels: not %d NUL-ended strings" % (RACES + 2 + KINDS))
    return [x.decode("latin-1") for x in out[:RACES + 2 + KINDS]]


def read_dqk(exe, check=True):
    if check:
        verify(exe)
    return engine_tables(exe)


# ---- Champions of Krynn and Death Knights of Krynn ----------------------

STEP_NONE = 0x01ffffff     # an XP step no character reaches; 40 of them fit
NEVER = 0x7fffffff         # the engine's "never" threshold (jt26)
DAX_SLOTS = (0, 7, 2, 3, 4, 5, 6)   # the engine's slot s is the game's
GAME_SLOTS = 8
ALIGN_BITS = 6


def kind_of(k):
    """A game kind number as the Dark Queen's (and the engine's)."""
    return {1: 7, 7: 1}.get(k, k)


# Per game: the load image's length (the build check with the sums), DS,
# the top level its level tables hold, its progressions and their record,
# and each table: DS offset, bytes, 16-bit sum of the bytes in the Steam
# release. "orders" is GAME.OVR's.
DAX_GAMES = {
    "cok": {
        "title": "Champions of Krynn", "image": 65568, "ds": 0xbc60,
        "top": 12, "progs": 10, "prog_rec": 99, "spell_levels": 5,
        "tables": (
            ("slot_thac0",       0x3882, 8 * 13,  0x11a4),
            ("slot_item_bit",    0x38ea, 8,       0x00ff),
            ("slot_hall_bit",    0x38f2, 8,       0x0051),
            ("slot_name_level",  0x3903, 8,       0x0059),
            ("thief_skills",     0x3911, 8 * 12,  0x129f),
            ("race_thief_adj",   0x3971, 8 * 7,   0x085f),
            ("race_abilities",   0x39e0, 16 * 7,  0x06bc),
            ("race_kinds",       0x3a50, 15 * 7,  0x0219),
            ("race_start_age",   0x3ab9, 32 * 7,  0x1028),
            ("race_age_cat",     0x3b99, 10 * 7,  0x12bb),
            ("kind_ability_min", 0x3bdf, 6 * 17,  0x01d1),
            ("kind_aligns",      0x3c45, 17,      0x026a),
            ("progressions",     0x3c6d, 99 * 10, 0xd72d),
            ("slot_saves",       0x405b, 60 * 8,  0x15fc),
            ("slot_hd_count",    0x0b6e, 8,       0x000b),
            ("slot_hd_size",     0x0b76, 8,       0x0040),
            ("align_index",      0x0a9a, 6,       0x0015),
            ("kind_names",       0x0f5a, 27 * 17, 0x3faf),
            ("race_names",       0x1140, 16 * 8,  0x1e12),
            ("god_names",        0x1259, 12 * 8,  0x164a),
            ("robe_names",       0x12c0, 7 * 3,   0x0506),
            ("money_names",      0x12e3, 11 * 7,  0x112d),
        ),
        "orders": (0x26db3, 41, 0x0d39),
    },
    "dkk": {
        "title": "Death Knights of Krynn", "image": 82336, "ds": 0xbce0,
        "top": 18, "progs": 11, "prog_rec": 204, "spell_levels": 8,
        "tables": (
            ("slot_thac0",       0x71f2, 8 * 19,  0x1b1d),
            ("slot_item_bit",    0x728a, 8,       0x00ff),
            ("slot_hall_bit",    0x7293, 8,       0x0051),
            ("slot_name_level",  0x72a4, 8,       0x0059),
            ("thief_skills",     0x72b4, 8 * 18,  0x2385),
            ("race_thief_adj",   0x7344, 8 * 7,   0x085f),
            ("race_abilities",   0x73b3, 16 * 7,  0x06bf),
            ("race_kinds",       0x7423, 15 * 7,  0x0225),
            ("race_start_age",   0x748c, 32 * 7,  0x1028),
            ("race_age_cat",     0x756c, 10 * 7,  0x12bb),
            ("kind_ability_min", 0x75b2, 6 * 17,  0x0306),
            ("kind_aligns",      0x7618, 17,      0x030a),
            ("progressions",     0x7640, 204 * 11, 0x273f),
            ("slot_saves",       0x7f14, 90 * 8,  0x1ce4),
            ("race_level_cap",   0x82d4, 8 * 7,   0x01b2),
            ("slot_hd_count",    0x0656, 8,       0x000b),
            ("slot_hd_size",     0x065e, 8,       0x0040),
            ("align_index",      0x0584, 6,       0x0015),
            ("kind_names",       0x08ad, 27 * 17, 0x3faf),
            ("race_names",       0x0a93, 16 * 8,  0x1e12),
            ("god_names",        0x0bac, 12 * 8,  0x164a),
            ("robe_names",       0x0c13, 7 * 3,   0x0506),
            ("money_names",      0x0c36, 11 * 7,  0x112d),
        ),
        "orders": (0x23cb1, 41, 0x0d39),
    },
}
DAX_PASCAL = {"kind_names": (27, KINDS), "race_names": (16, RACES + 1),
              "god_names": (12, GODS), "robe_names": (7, ROBES - 1),
              "money_names": (11, 7)}


def dax_game(img):
    """'cok' or 'dkk' by the load image's length, or None."""
    for key, g in DAX_GAMES.items():
        if len(img) == g["image"]:
            return key
    return None


def dax_raw(img, game, name):
    g = DAX_GAMES[game]
    for n, off, size, _ in g["tables"]:
        if n == name:
            p = g["ds"] + off
            if p + size > len(img):
                raise GbRulesError("%s: past the end of the image" % name)
            return img[p:p + size]
    raise KeyError(name)


def dax_sums(img, ovr, game):
    g = DAX_GAMES[game]
    out = {n: _sum(dax_raw(img, game, n)) for n, _, _, _ in g["tables"]}
    off, size, _ = g["orders"]
    out["orders"] = _sum(ovr[off:off + size])
    return out


def dax_verify(img, ovr, game):
    g = DAX_GAMES[game]
    got = dax_sums(img, ovr, game)
    want = {n: s for n, _, _, s in g["tables"]}
    want["orders"] = g["orders"][2]
    for name, s in want.items():
        if got[name] != s:
            raise GbRulesError("%s: sum %04x, expected %04x" % (name, got[name], s))


def _pascal(b, stride, count):
    out = []
    for i in range(count):
        n = b[stride * i]
        if n >= stride:
            raise GbRulesError("a string longer than its place")
        out.append(bytes(b[stride * i + 1:stride * i + 1 + n]).decode("latin-1"))
    return out


def _by_slot(b, width):
    """A game table of 8 slot rows `width` wide as the engine's 7."""
    return b"".join(bytes(b[width * s:width * (s + 1)]) for s in DAX_SLOTS)


def _hold(rows, n):
    """`rows` (a list of rows) carried to `n`, the last held."""
    return rows + [rows[-1]] * (n - len(rows))


def read_dax(img, ovr, game=None, check=True):
    """Champions' or Death Knights' rules out of START.EXE's load image
    (unpacked) and GAME.OVR, as {field: bytes} in the layout the engine
    indexes: what engine_tables makes of DQK.EXE."""
    game = game or dax_game(img)
    if game not in DAX_GAMES:
        raise GbRulesError("not Champions' or Death Knights' START.EXE")
    if check:
        dax_verify(img, ovr, game)
    g = DAX_GAMES[game]
    top, nlev = g["top"], g["spell_levels"]
    raw = lambda name: bytes(dax_raw(img, game, name))
    t = {}

    for name in ("race_abilities", "race_thief_adj"):
        t[name] = raw(name)
    for name in ("slot_item_bit", "slot_hall_bit", "slot_name_level",
                 "slot_hd_count", "slot_hd_size"):
        t[name] = _by_slot(raw(name), 1)

    b = raw("slot_thac0")                       # levels 0..top
    rows = [list(b[(top + 1) * s:(top + 1) * (s + 1)]) for s in DAX_SLOTS]
    t["slot_thac0"] = b"".join(bytes(_hold(r, 22)) for r in rows)

    b = raw("slot_saves")                       # levels 1..top, 5 saves
    out = bytearray()
    for s in DAX_SLOTS:
        lv = [b[5 * (top * s + l):5 * (top * s + l) + 5] for l in range(top)]
        out += b"".join(_hold([lv[0]] + lv, 22))
    t["slot_saves"] = bytes(out)

    b = raw("thief_skills")                     # levels 1..top, 8 skills
    t["thief_skills"] = b"".join(_hold([b[8 * l:8 * l + 8] for l in range(top)], 18))

    b = raw("race_kinds")
    out = bytearray()
    for r in range(RACES):
        row = b[15 * r:15 * r + 15]
        out += bytes([row[0]]) + bytes(kind_of(k) if i < row[0] else k
                                       for i, k in enumerate(row[1:]))
    t["race_kinds"] = bytes(out)

    b = raw("race_start_age")                   # race x 8 slots {w, dice, sides}
    out = bytearray()
    for r in range(RACES):
        for s in DAX_SLOTS:
            e = b[32 * r + 4 * s:32 * r + 4 * s + 4]
            out += be16(struct.unpack_from("<H", e)[0]) + e[2:4]
    t["race_start_age"] = bytes(out)

    b = raw("race_age_cat")
    t["race_age_cat"] = b"".join(be16(v) for v in struct.unpack("<%dH" % (5 * RACES), b))

    b = raw("kind_ability_min")
    out = [b""] * KINDS
    for k in range(KINDS):
        out[kind_of(k)] = b[6 * k:6 * k + 6]
    t["kind_ability_min"] = b"".join(out)

    b, idx = raw("kind_aligns"), raw("align_index")
    out = [b""] * KINDS
    for k in range(KINDS):
        al = [idx[j] for j in range(ALIGN_BITS) if b[k] >> j & 1]
        out[kind_of(k)] = bytes([len(al)] + al + [0] * (11 - len(al)))
    t["kind_aligns"] = b"".join(out)

    # progressions: XP to levels 2..top, then the spells gained at each
    b, rec = raw("progressions"), g["prog_rec"]
    xp, gains = [], []
    for p in range(PROGS):
        if p < g["progs"]:
            r = b[rec * p:rec * (p + 1)]
            th = list(struct.unpack_from("<%dl" % (top - 1), r))
            gl = [list(r[4 * (top - 1) + nlev * i:4 * (top - 1) + nlev * (i + 1)])
                  for i in range(top - 1)]
        else:                                   # Champions' paladin: none
            th, gl = [-1] * (top - 1), [[0] * nlev] * (top - 1)
        th = [NEVER if v <= 0 else v for v in th]
        xp.append([0, 0] + th + [NEVER] * (XP_LEVELS - 1 - top))
        gains.append(gl)
    t["xp"] = b"".join(be32(v) for row in xp for v in row)
    t["xp_step"] = b"".join(be32(STEP_NONE) for _ in range(PROGS))

    spell_row = [0, 1, 255, 255, 2, 3, 4, 5, 6, 255, 7]   # the Dark Queen's
    t["spell_row"] = bytes(spell_row)
    slots = bytearray(261 * SPELL_ROWS)
    for p, row in enumerate(spell_row):
        if row == 255:
            continue
        have = [0] * 9
        if p in (0, 1, 7, 8):                   # a cleric's or mage's first
            have[0] = 1
        for lv in range(1, 30):
            if 2 <= lv <= top:
                for i, n in enumerate(gains[p][lv - 2]):
                    have[i] = (have[i] + n) & 0xff
            slots[261 * row + 9 * (lv - 1):261 * row + 9 * lv] = bytes(have)
    t["spell_slots"] = bytes(slots)

    caps = bytearray(7 * RACES)
    if game == "dkk":
        b = raw("race_level_cap")
        for r in range(RACES):
            caps[7 * r:7 * r + 7] = bytes(b[8 * r + s] for s in DAX_SLOTS)
    else:                                       # where a class's XP stops
        for r in range(RACES):
            row = t["race_kinds"][15 * r:15 * r + 15]
            for s in range(7):
                if not any(k < KINDS and s in KIND_SLOTS_OF[k] for k in row[1:1 + row[0]]):
                    continue
                progs = [q for q in range(PROGS) if SLOT_OF_PROG[q] == s]
                caps[7 * r + s] = max(top_level(xp[q]) for q in progs)
            if r == 5:                          # Kender (OVR 0x25061)
                for s in (2, 4):
                    if caps[7 * r + s]:
                        caps[7 * r + s] = 7
    t["race_level_cap"] = bytes(caps)

    kinds = _pascal(raw("kind_names"), *DAX_PASCAL["kind_names"])
    kinds = [kinds[kind_of(k)] for k in range(KINDS)]
    races = _pascal(raw("race_names"), *DAX_PASCAL["race_names"])
    money = _pascal(raw("money_names"), *DAX_PASCAL["money_names"])
    off, size, _ = g["orders"]
    t["kind_names"] = kinds
    t["race_names"] = races
    t["money_names"] = [money[4], money[5], money[6]]   # Steel Gems Jewelry
    t["race_labels"] = [r.upper() for r in races[:RACES]]
    t["kind_labels"] = [k.upper() for k in kinds]
    t["sheet_kind_names"] = list(kinds)
    t["god_names"] = _pascal(raw("god_names"), *DAX_PASCAL["god_names"])
    t["robe_names"] = [""] + _pascal(raw("robe_names"), *DAX_PASCAL["robe_names"])
    t["order_names"] = [""] + _pascal_run(ovr[off:off + size], ORDERS - 1)
    t["title"] = g["title"]
    return t


def _pascal_run(b, count):
    """`count` Pascal strings one after another."""
    out, p = [], 0
    for _ in range(count):
        if p >= len(b) or p + 1 + b[p] > len(b):
            raise GbRulesError("orders: not %d strings" % count)
        out.append(bytes(b[p + 1:p + 1 + b[p]]).decode("latin-1"))
        p += 1 + b[p]
    return out


# The slots each kind holds (the engine's numbering), and the slot each
# progression is of (gbrules.PROGRESSIONS).
KIND_SLOTS_OF = ((0,), (1,), (2,), (3,), (4,), (5,), (6,), (), (0, 2),
                 (0, 2, 5), (0, 4), (0, 5), (0, 6), (2, 5), (2, 6), (2, 5, 6),
                 (5, 6))
SLOT_OF_PROG = (0, 0, 2, 1, 1, 1, 4, 5, 5, 6, 3)


def top_level(row):
    """The last level an XP row reaches."""
    return max(lv for lv in range(1, XP_LEVELS) if lv < 2 or row[lv] != NEVER)


def read_dax_folder(folder, check=True):
    import gbdaxmon
    import os
    with open(os.path.join(folder, "START.EXE"), "rb") as fh:
        img = gbdaxmon.unexepack(fh.read())
    with open(os.path.join(folder, "GAME.OVR"), "rb") as fh:
        ovr = fh.read()
    return read_dax(img, ovr, check=check)


def _s8(v):
    return v - 256 if v > 127 else v


def describe(t):
    """The rules as text, one table after another."""
    L = []
    kn, rn = t["kind_names"], t["race_names"]
    L.append("races  %s  (monster: %s)" % (", ".join(rn[:RACES]), rn[RACES]))
    L.append("kinds  %s" % ", ".join(kn))
    L.append("")
    L.append("race classes and level caps (%s)" % " ".join(SLOTS))
    for r in range(RACES):
        row = t["race_kinds"][15 * r:15 * r + 15]
        cap = t["race_level_cap"][7 * r:7 * r + 7]
        L.append("  %-15s caps %-28s %s" % (
            rn[r], " ".join("%2d" % c for c in cap),
            ", ".join(kn[k] for k in row[1:1 + row[0]])))
    L.append("")
    L.append("race abilities (min/max pairs as FRUA's -30960)")
    for r in range(RACES):
        L.append("  %-15s %s" % (rn[r], " ".join("%3d" % v for v in
                                               t["race_abilities"][16 * r:16 * r + 16])))
    L.append("race ages (category thresholds; start age by slot: base+dice d sides)")
    for r in range(RACES):
        cat = struct.unpack(">5H", t["race_age_cat"][10 * r:10 * r + 10])
        st = t["race_start_age"][28 * r:28 * r + 28]
        L.append("  %-15s %-24s %s" % (rn[r], " ".join(str(c) for c in cat), " ".join(
            "%d+%dd%d" % (struct.unpack(">H", st[k:k + 2])[0], st[k + 2], st[k + 3])
            for k in range(0, 28, 4))))
    L.append("")
    L.append("kind ability minimums and alignments (0 LG 1 LN 2 LE 3 NG 4 N 5 NE 6 CG 7 CN 8 CE)")
    for k in range(KINDS):
        al = t["kind_aligns"][12 * k:12 * k + 12]
        L.append("  %-26s %-18s %s" % (kn[k], " ".join("%2d" % v for v in
                                                       t["kind_ability_min"][6 * k:6 * k + 6]),
                                      " ".join(str(a) for a in al[1:1 + al[0]])))
    L.append("")
    L.append("progressions: XP thresholds by level, step past the table, spell row")
    for p in range(PROGS):
        xp = struct.unpack(">%dl" % XP_LEVELS, t["xp"][4 * XP_LEVELS * p:4 * XP_LEVELS * (p + 1)])
        step = struct.unpack(">l", t["xp_step"][4 * p:4 * p + 4])[0]
        sr = t["spell_row"][p]
        L.append("  %-21s step %7d  spells %-4s %s" % (
            PROGRESSIONS[p], step, "-" if sr == 255 else sr,
            " ".join(str(v) for v in xp[2:])))
    L.append("")
    L.append("by slot      item hall name hd   thac0 (levels 0..21)")
    for s in range(7):
        L.append("  %-9s %4d %4d %4d %dd%-3d %s" % (
            SLOTS[s], t["slot_item_bit"][s], t["slot_hall_bit"][s],
            t["slot_name_level"][s], t["slot_hd_count"][s], t["slot_hd_size"][s],
            " ".join(str(v) for v in t["slot_thac0"][22 * s:22 * s + 22])))
    L.append("saving throws by slot (level: 5 saves), levels 1..18")
    for s in range(7):
        sv = t["slot_saves"][110 * s:110 * s + 110]
        L.append("  %-9s %s" % (SLOTS[s], " ".join(
            "".join("%x" % min(v, 15) for v in sv[5 * l:5 * l + 5]) for l in range(1, 19))))
    L.append("thief skills by level 1..18, then race adjustments")
    for lv in range(18):
        L.append("  %2d %s" % (lv + 1, " ".join("%3d" % v for v in
                                              t["thief_skills"][8 * lv:8 * lv + 8])))
    for r in range(RACES):
        L.append("  %-15s %s" % (rn[r], " ".join("%3d" % _s8(v) for v in
                                               t["race_thief_adj"][8 * r:8 * r + 8])))
    return "\n".join(L)


def c_table(exe):
    """The positions-and-sums table src/engine/rules_gb.c carries."""
    got = sums(exe)
    L = []
    for name, off, n, _ in TABLES:
        L.append("\t{ 0x%04x, %4d, 0x%04x },   /* %s */" % (off, n, got[name], name))
    return "\n".join(L)


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    import os
    if os.path.isdir(argv[0]):
        if "--sums" in argv:
            import gbdaxmon
            img = gbdaxmon.unexepack(open(os.path.join(argv[0], "START.EXE"), "rb").read())
            ovr = open(os.path.join(argv[0], "GAME.OVR"), "rb").read()
            game = dax_game(img)
            if game is None:
                raise GbRulesError("not Champions' or Death Knights' START.EXE")
            for name, s in sorted(dax_sums(img, ovr, game).items()):
                print("%-18s 0x%04x" % (name, s))
            return 0
        t = read_dax_folder(argv[0])
        print(t["title"])
        print(describe(t))
        return 0
    with open(argv[0], "rb") as fh:
        exe = fh.read()
    if "--sums" in argv:
        print(c_table(exe))
        return 0
    print(describe(read_dqk(exe)))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except GbRulesError as e:
        sys.exit("gbrules: %s" % e)
