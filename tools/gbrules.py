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

    python3 tools/gbrules.py DQK.EXE            # print the rules
    python3 tools/gbrules.py DQK.EXE --sums     # the C table of positions

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
