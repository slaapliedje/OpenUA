"""rules.py — a ruleset file: the classes and races a design plays by
(ADR-0026 step (g)).

FRUA's rules are built into the engine and the Dark Queen's are read from
DQK.EXE. A new game has no original to read, so it brings its rules in a
file, RULES.DAT in its design folder, which this tool compiles from a text
file a designer can read and edit. To start, decompile FRUA's or Krynn's:

    python3 tools/rules.py frua UnlimitedAdventures.rfork -o FRUA.TXT
    python3 tools/rules.py dqk DQK.EXE -o KRYNN.TXT
    python3 tools/rules.py compile KRYNN.TXT MYGAME.DSN/RULES.DAT
    python3 tools/rules.py decompile MYGAME.DSN/RULES.DAT

This is the reference for src/engine/rules_file.c (tests/test_rules_file.py
holds the two together). The tables are kept in the layout the engine
indexes (src/engine/rules.h: big-endian, the engine's row strides), so the
file is what the engine reads and every value a table holds survives a
round trip through the text.

THE FILE (RULES.DAT)

    "UARULES1"
    chunks to the end: tag (4 ASCII), u16 length, u16 0, the data, padded
    with zeros to a multiple of 4 (a word or long table then sits on a long
    boundary, which a 68000 needs)

    HEAD  8 bytes: races, progressions, xp levels, the race_kinds row
          length, spell rows, default race, flags (1 prime_limits,
          2 knights), 0
    NAME  the ruleset's name                  (strings: each NUL-ended)
    COIN  the coin's name; absent, FRUA's platinum
    KNAM  17 class-kind names   RNAM  races + 1 race names (the last the
    KLAB  17 creation labels          monster's)
    RLAB  races creation labels
    SLOT  7 slot names          PROG  progressions names (text only)
    the tables, by the tag in TABLES below; and optionally
    SHET  the character sheet's words: 17 kind names as the sheet shows
          them, then 8 gods, 4 robes, 4 knightly orders (rec[91]'s values;
          a cleric's kind starts with his god, the robe goes before "Mage",
          the order after a knight's kind; DQK 0x5cc86); absent, the kind
          names and none of these
    PSEL  the progression a character's slot plays by, picked by its god,
          knightly order or robe (rec[91], rules.h): three groups of a
          slot (255 unused) and 8 / 4 / 4 progressions by that value

The race numbers past the player races are monsters: race `races` plays as
FRUA's monster race, as Krynn's 7 does.

THE TEXT. One statement a line, `#` to the end of a line a comment, strings
in double quotes. The scalars (name, coin, races, ...), then `race`, `kind`,
`slot` and `progression` lines naming each, then every table a row a line:
`<table> <row> <values>`, rows in order from 0. The decompiler names each
row in a comment. A start age is written base+NdS.

The game data is the user's own; nothing here ships any.
"""
import os
import shlex
import struct
import sys

MAGIC = b"UARULES1"
KINDS = 17
SLOTS = 7
MAX_RACES = 7          # the creation screen's room (Krynn's seven)
MAX_PROGS = 16
MAX_XP_LEVELS = 29
MAX_SPELL_ROWS = 16
SPELL_ROW = 261        # 29 levels x 9 spell levels
FLAG_PRIME_LIMITS, FLAG_KNIGHTS = 1, 2

ALIGNS = ("LG", "LN", "LE", "NG", "N", "NE", "CG", "CN", "CE")
SELECTORS = (("god", 8), ("order", 4), ("robe", 4))

# field (rules.h), tag, element, rows(shape), columns(shape)
# element: u8, s8, u16 / s32 (big-endian), age ({u16 base, dice, sides})
R = lambda s: s["races"]
P = lambda s: s["progressions"]
TABLES = (
    ("race_base",        "RBAS", "u8",  lambda s: 1, R),
    ("kind_list",        "KLST", "u8",  lambda s: 1, lambda s: KINDS),
    ("race_kinds",       "RKND", "u8",  R, lambda s: s["kind_row"]),
    ("kind_aligns",      "KALN", "u8",  lambda s: KINDS, lambda s: 12),
    ("kind_ability_min", "KMIN", "u8",  lambda s: KINDS, lambda s: 6),
    ("race_abilities",   "RABL", "u8",  R, lambda s: 16),
    ("race_age_cat",     "RAGC", "u16", R, lambda s: 5),
    ("race_start_age",   "RSTA", "age", R, lambda s: SLOTS),
    ("race_level_cap",   "RCAP", "u8",  R, lambda s: SLOTS),
    ("slot_xp",          "XPTB", "s32", P, lambda s: s["xp_levels"]),
    ("slot_xp_step",     "XPST", "s32", lambda s: 1, P),
    ("slot_spell_row",   "SROW", "u8",  lambda s: 1, P),
    ("spell_slots",      "SSLT", "u8",  lambda s: s["spell_rows"] * 29, lambda s: 9),
    ("slot_prog",        "SPRG", "u8",  lambda s: 1, lambda s: SLOTS),
    ("slot_thac0",       "THAC", "u8",  lambda s: SLOTS, lambda s: 22),
    ("slot_saves",       "SAVE", "u8",  lambda s: SLOTS * 22, lambda s: 5),
    ("slot_item_bit",    "IBIT", "u8",  lambda s: 1, lambda s: SLOTS),
    ("slot_hall_bit",    "HBIT", "u8",  lambda s: 1, lambda s: SLOTS),
    ("slot_name_level",  "NLVL", "u8",  lambda s: 1, lambda s: SLOTS),
    ("slot_hd_count",    "HDCT", "u8",  lambda s: 1, lambda s: SLOTS),
    ("slot_hd_size",     "HDSZ", "u8",  lambda s: 1, lambda s: SLOTS),
    ("thief_skills",     "THSK", "u8",  lambda s: 18, lambda s: 8),
    ("race_thief_adj",   "RTHF", "s8",  R, lambda s: 8),
)
WIDTH = {"u8": 1, "s8": 1, "u16": 2, "s32": 4, "age": 4}
SCALARS = ("races", "progressions", "xp_levels", "kind_row", "spell_rows",
           "default_race", "prime_limits", "knights")
# The sheet's words (SHET): text keyword, ruleset field, count
SHEET = (("sheet_kind", "sheet_kind_names", KINDS), ("god", "god_names", 8),
         ("robe", "robe_names", 4), ("order", "order_names", 4))
STRINGS = (("kind_names", "KNAM", lambda s: KINDS),
           ("race_names", "RNAM", lambda s: s["races"] + 1),
           ("kind_labels", "KLAB", lambda s: KINDS),
           ("race_labels", "RLAB", R),
           ("slot_names", "SLOT", lambda s: SLOTS),
           ("prog_names", "PROG", P))
FRUA_RACES = ("elf", "half-elf", "dwarf", "gnome", "halfling", "human",
              "monster")


class RulesError(ValueError):
    pass


def table(field):
    for t in TABLES:
        if t[0] == field:
            return t
    raise KeyError(field)


def shape(rs, field):
    _, _, el, rows, cols = table(field)
    return rows(rs), cols(rs), WIDTH[el]


def check(rs):
    """Raise unless the ruleset's shapes and tables agree, and fit the
    engine."""
    lim = {"races": (1, MAX_RACES), "progressions": (1, MAX_PROGS),
           "xp_levels": (2, MAX_XP_LEVELS), "kind_row": (2, KINDS + 1),
           "spell_rows": (1, MAX_SPELL_ROWS)}
    for k in SCALARS:
        if k not in rs:
            raise RulesError("%s is not given" % k)
    for k, (lo, hi) in lim.items():
        if not lo <= rs[k] <= hi:
            raise RulesError("%s %d: not %d..%d" % (k, rs[k], lo, hi))
    if rs["default_race"] >= rs["races"]:
        raise RulesError("default_race %d: there are %d races"
                         % (rs["default_race"], rs["races"]))
    if not rs.get("name"):
        raise RulesError("the ruleset has no name")
    for field, _, n in STRINGS:
        got = rs.get(field)
        if got is None or len(got) != n(rs):
            raise RulesError("%s: %s names, expected %d"
                             % (field, "no" if got is None else len(got), n(rs)))
        for x in got:
            if "\0" in x or len(x.encode("latin-1")) > 40:
                raise RulesError("%s: %r is not a name" % (field, x))
    have = [rs.get(f) is not None for _, f, _ in SHEET]
    if any(have) and not all(have):
        raise RulesError("the sheet's words come together: %s"
                         % ", ".join(k for k, _, _ in SHEET))
    for key, field, n in SHEET if all(have) else ():
        if len(rs[field]) != n or any("\0" in x or len(x.encode("latin-1")) > 31
                                      for x in rs[field]):
            raise RulesError("%s: %d names of up to 31 bytes" % (key, n))
    for t in TABLES:
        rows, cols, w = shape(rs, t[0])
        b = rs.get(t[0])
        if b is None or len(b) != rows * cols * w:
            raise RulesError("%s: %s bytes, expected %d" % (
                t[0], "no" if b is None else len(b), rows * cols * w))
    for v in rs["race_base"]:
        if v > 6:
            raise RulesError("race_base: %d is not one of FRUA's races" % v)
    for v in rs["slot_spell_row"]:
        if v != 255 and v >= rs["spell_rows"]:
            raise RulesError("slot_spell_row: no spell row %d" % v)
    for v in rs["slot_prog"]:
        if v >= rs["progressions"]:
            raise RulesError("slot_prog: no progression %d" % v)
    for name, slot, row in rs.get("prog_by", ()):
        if slot >= SLOTS or any(v >= rs["progressions"] for v in row):
            raise RulesError("prog_by %s: slot %d or progression out of range"
                             % (name, slot))
    for r in range(rs["races"]):
        row = rs["race_kinds"][r * rs["kind_row"]:(r + 1) * rs["kind_row"]]
        if row[0] > rs["kind_row"] - 1 or any(k >= KINDS for k in row[1:1 + row[0]]):
            raise RulesError("race_kinds %d: not a list of kinds" % r)


# --- the binary file -------------------------------------------------------

def _chunk(tag, data):
    data = bytes(data)
    if len(data) > 0xffff:
        raise RulesError("%s: %d bytes" % (tag, len(data)))
    return (tag.encode("ascii") + struct.pack(">HH", len(data), 0) + data
            + b"\0" * (-len(data) % 4))


def _strs(xs):
    return b"".join(x.encode("latin-1") + b"\0" for x in xs)


def to_bytes(rs):
    check(rs)
    flags = (FLAG_PRIME_LIMITS if rs["prime_limits"] else 0) | \
            (FLAG_KNIGHTS if rs["knights"] else 0)
    out = [MAGIC, _chunk("HEAD", bytes([
        rs["races"], rs["progressions"], rs["xp_levels"], rs["kind_row"],
        rs["spell_rows"], rs["default_race"], flags, 0]))]
    out.append(_chunk("NAME", _strs([rs["name"]])))
    if rs.get("coin") is not None:
        out.append(_chunk("COIN", _strs([rs["coin"]])))
    for field, tag, _ in STRINGS:
        out.append(_chunk(tag, _strs(rs[field])))
    for t in TABLES:
        out.append(_chunk(t[1], rs[t[0]]))
    if rs.get("god_names") is not None:
        out.append(_chunk("SHET", _strs(sum((rs[f] for _, f, _ in SHEET), []))))
    if rs.get("prog_by"):
        sel = {name: (slot, row) for name, slot, row in rs["prog_by"]}
        b = bytearray()
        for name, n in SELECTORS:
            slot, row = sel.get(name, (255, [0] * n))
            b += bytes([slot]) + bytes(row)
        out.append(_chunk("PSEL", b))
    return b"".join(out)


def _split(data, n, tag):
    xs = data.split(b"\0")
    if len(xs) != n + 1 or xs[n] != b"":
        raise RulesError("%s: not %d NUL-ended strings" % (tag, n))
    return [x.decode("latin-1") for x in xs[:n]]


def from_bytes(b):
    if b[:8] != MAGIC:
        raise RulesError("not a ruleset file")
    chunks, p = {}, 8
    while p < len(b):
        if p + 8 > len(b):
            raise RulesError("a chunk header runs past the end")
        tag = b[p:p + 4].decode("latin-1")
        n = struct.unpack_from(">H", b, p + 4)[0]
        if p + 8 + n > len(b):
            raise RulesError("%s runs past the end" % tag)
        chunks[tag] = bytes(b[p + 8:p + 8 + n])
        p += 8 + n + (-n % 4)
    if "HEAD" not in chunks or len(chunks["HEAD"]) != 8:
        raise RulesError("no HEAD")
    h = chunks["HEAD"]
    rs = dict(zip(("races", "progressions", "xp_levels", "kind_row",
                   "spell_rows", "default_race"), h[:6]))
    rs["prime_limits"] = 1 if h[6] & FLAG_PRIME_LIMITS else 0
    rs["knights"] = 1 if h[6] & FLAG_KNIGHTS else 0
    for k, (lo, hi) in {"races": (1, MAX_RACES), "progressions": (1, MAX_PROGS),
                        "kind_row": (2, KINDS + 1)}.items():
        if not lo <= rs[k] <= hi:
            raise RulesError("%s %d: not %d..%d" % (k, rs[k], lo, hi))
    if "NAME" not in chunks:
        raise RulesError("no NAME")
    rs["name"] = _split(chunks["NAME"], 1, "NAME")[0]
    rs["coin"] = _split(chunks["COIN"], 1, "COIN")[0] if "COIN" in chunks else None
    for field, tag, n in STRINGS:
        if tag not in chunks:
            raise RulesError("no %s" % tag)
        rs[field] = _split(chunks[tag], n(rs), tag)
    for t in TABLES:
        if t[1] not in chunks:
            raise RulesError("no %s (%s)" % (t[1], t[0]))
        rs[t[0]] = chunks[t[1]]
    if "SHET" in chunks:
        xs = _split(chunks["SHET"], sum(n for _, _, n in SHEET), "SHET")
        for _, field, n in SHEET:
            rs[field], xs = xs[:n], xs[n:]
    if "PSEL" in chunks:
        b = chunks["PSEL"]
        if len(b) != sum(1 + n for _, n in SELECTORS):
            raise RulesError("PSEL: %d bytes" % len(b))
        rs["prog_by"], p = [], 0
        for name, n in SELECTORS:
            if b[p] != 255:
                rs["prog_by"].append((name, b[p], list(b[p + 1:p + 1 + n])))
            p += 1 + n
    check(rs)
    return rs


# --- the text --------------------------------------------------------------

def _values(rs, field):
    rows, cols, w = shape(rs, field)
    el = table(field)[2]
    b = rs[field]
    out = []
    for r in range(rows):
        row = []
        for c in range(cols):
            q = (r * cols + c) * w
            if el == "u8":
                row.append(str(b[q]))
            elif el == "s8":
                row.append(str(b[q] - 256 if b[q] > 127 else b[q]))
            elif el == "u16":
                row.append(str(struct.unpack_from(">H", b, q)[0]))
            elif el == "s32":
                row.append(str(struct.unpack_from(">l", b, q)[0]))
            else:
                row.append("%d+%dd%d" % (struct.unpack_from(">H", b, q)[0],
                                         b[q + 2], b[q + 3]))
        out.append(row)
    return out


def _row_note(rs, field, r):
    rn, kn, sn, pn = (rs["race_names"], rs["kind_names"], rs["slot_names"],
                      rs["prog_names"])
    rows = shape(rs, field)[0]
    if field in ("race_base", "slot_xp_step", "slot_spell_row", "slot_prog",
                 "slot_item_bit", "slot_hall_bit", "slot_name_level",
                 "slot_hd_count", "slot_hd_size"):
        return None
    if field == "kind_list":
        return None
    if rows == rs["races"] and field.startswith("race_"):
        return rn[r]
    if field.startswith("kind_"):
        return kn[r]
    if field == "slot_xp":
        return pn[r]
    if field == "slot_thac0":
        return sn[r]
    if field == "slot_saves":
        return "%s level %d" % (sn[r // 22], r % 22)
    if field == "spell_slots":
        return "spell row %d, level %d" % (r // 29, r % 29)
    if field == "thief_skills":
        return "level %d" % (r + 1)
    return None


NOTES = {
    "race_base": "the FRUA race each race plays as, for the rules written "
                 "into the engine (0 elf 1 half-elf 2 dwarf 3 gnome "
                 "4 halfling 5 human)",
    "kind_list": "the creation screen's class row for each kind; +128: no "
                 "race may take it (shown flagged)",
    "race_kinds": "the kinds a race may be: a count, then kind numbers",
    "kind_aligns": "the alignments a kind may have: a count, then "
                   + " ".join("%d %s" % a for a in enumerate(ALIGNS)),
    "kind_ability_min": "minimum STR INT WIS DEX CON CHA",
    "race_abilities": "ability ranges, min/max pairs (FRUA's A5-30960 layout)",
    "race_age_cat": "age category thresholds (years)",
    "race_start_age": "starting age by slot: base + dice d sides",
    "race_level_cap": "highest level by slot",
    "slot_xp": "XP thresholds by level, by progression",
    "slot_xp_step": "XP a level past the table, by progression",
    "slot_spell_row": "the spell_slots row each progression casts by, "
                      "255 none",
    "spell_slots": "spells a day: 29 levels a spell row, 9 spell levels",
    "slot_prog": "the progression each slot plays by",
    "slot_thac0": "THAC0 by level 0..21",
    "slot_saves": "saving throws by level, 5 a level",
    "slot_item_bit": "the class bit items test (rec[183])",
    "slot_hall_bit": "the class bit the training hall tests",
    "slot_name_level": "the last level with a hit die",
    "slot_hd_count": "hit dice at level 1",
    "slot_hd_size": "hit die size",
    "thief_skills": "thief skills by level",
    "race_thief_adj": "thief skill adjustments by race",
}


def _q(s):
    if '"' in s or "\n" in s:
        raise RulesError("%r: a name cannot hold a quote" % s)
    return '"%s"' % s


def to_text(rs):
    check(rs)
    L = ["# OpenUA ruleset (ADR-0026). Compile with",
         "#   python3 tools/rules.py compile THIS.TXT <design>.DSN/RULES.DAT",
         "# and the design plays by it.",
         "",
         "name %s" % _q(rs["name"])]
    if rs.get("coin") is not None:
        L.append("coin %s" % _q(rs["coin"]))
    else:
        L.append("# coin \"Steel\"     (none: FRUA's platinum)")
    for k in SCALARS:
        L.append("%s %d" % (k, rs[k]))
    L.append("")
    L.append("# race <n> <name> <creation label>; the monster is race %d"
             % rs["races"])
    for r in range(rs["races"]):
        L.append("race %d %s %s" % (r, _q(rs["race_names"][r]),
                                    _q(rs["race_labels"][r])))
    L.append("monster %s" % _q(rs["race_names"][rs["races"]]))
    L.append("")
    L.append("# kind <n> <name> <creation label>")
    for k in range(KINDS):
        L.append("kind %d %s %s" % (k, _q(rs["kind_names"][k]),
                                    _q(rs["kind_labels"][k])))
    L.append("")
    for s in range(SLOTS):
        L.append("slot %d %s" % (s, _q(rs["slot_names"][s])))
    for p in range(rs["progressions"]):
        L.append("progression %d %s" % (p, _q(rs["prog_names"][p])))
    if rs.get("god_names") is not None:
        L.append("")
        L.append("# the character sheet: a kind's name there; the god a cleric's")
        L.append("# starts with, the robe before \"Mage\", the order after a knight's")
        for key, field, n in SHEET:
            for i in range(n):
                L.append("%s %d %s" % (key, i, _q(rs[field][i])))
    if rs.get("prog_by"):
        L.append("")
        L.append("# prog_by god|order|robe <slot> <progression by value 0..>")
        for name, slot, row in rs["prog_by"]:
            L.append("prog_by %s %d  %s" % (name, slot, " ".join(map(str, row))))
    for t in TABLES:
        L.append("")
        L.append("# %s: %s" % (t[0], NOTES[t[0]]))
        for r, row in enumerate(_values(rs, t[0])):
            note = _row_note(rs, t[0], r)
            line = "%s %d  %s" % (t[0], r, " ".join(row))
            L.append(line + ("    # " + note if note else ""))
    return "\n".join(L) + "\n"


def _int(tok, where):
    try:
        return int(tok, 0)
    except ValueError:
        raise RulesError("%s: %r is not a number" % (where, tok))


def _pack(el, tok, where):
    if el == "age":
        try:
            base, rest = tok.split("+")
            dice, sides = rest.lower().split("d")
            v = (int(base), int(dice), int(sides))
        except ValueError:
            raise RulesError("%s: %r is not base+NdS" % (where, tok))
        if not (0 <= v[0] <= 0xffff and 0 <= v[1] <= 255 and 0 <= v[2] <= 255):
            raise RulesError("%s: %r out of range" % (where, tok))
        return struct.pack(">HBB", *v)
    v = _int(tok, where)
    rng = {"u8": (0, 255), "s8": (-128, 127), "u16": (0, 0xffff),
           "s32": (-(1 << 31), (1 << 31) - 1)}[el]
    if not rng[0] <= v <= rng[1]:
        raise RulesError("%s: %d out of range" % (where, v))
    return {"u8": lambda: bytes([v]), "s8": lambda: bytes([v & 0xff]),
            "u16": lambda: struct.pack(">H", v),
            "s32": lambda: struct.pack(">l", v)}[el]()


def from_text(text):
    rs = {"coin": None}
    named = {"race": {}, "kind": {}, "slot": {}, "progression": {},
             "sheet_kind": {}, "god": {}, "robe": {}, "order": {}}
    labels = {"race": {}, "kind": {}}
    rows = {}
    for n, line in enumerate(text.splitlines(), 1):
        where = "line %d" % n
        try:
            tok = shlex.split(line, comments=True)
        except ValueError as e:
            raise RulesError("%s: %s" % (where, e))
        if not tok:
            continue
        key = tok[0]
        if key in ("name", "coin", "monster"):
            if len(tok) != 2:
                raise RulesError("%s: %s takes one string" % (where, key))
            rs[key] = tok[1]
        elif key in SCALARS:
            if len(tok) != 2:
                raise RulesError("%s: %s takes one number" % (where, key))
            rs[key] = _int(tok[1], where)
        elif key in named:
            want = 4 if key in labels else 3
            if len(tok) != want:
                raise RulesError("%s: %s takes a number and %d string%s"
                                 % (where, key, want - 2, "s" if want == 4 else ""))
            i = _int(tok[1], where)
            named[key][i] = tok[2]
            if key in labels:
                labels[key][i] = tok[3]
        elif key == "prog_by":
            sel = dict(SELECTORS)
            if len(tok) < 3 or tok[1] not in sel or len(tok) != 3 + sel[tok[1]]:
                raise RulesError("%s: prog_by god|order|robe <slot> and 8/4/4 "
                                 "progressions" % where)
            rs.setdefault("prog_by", []).append(
                (tok[1], _int(tok[2], where), [_int(t, where) for t in tok[3:]]))
        else:
            try:
                el = table(key)[2]
            except KeyError:
                raise RulesError("%s: %r is not a statement" % (where, key))
            if len(tok) < 2:
                raise RulesError("%s: %s needs a row number" % (where, key))
            r = _int(tok[1], where)
            got = rows.setdefault(key, [])
            if r != len(got):
                raise RulesError("%s: %s row %d, expected row %d"
                                 % (where, key, r, len(got)))
            got.append((where, el, tok[2:]))
    for k in SCALARS:
        if k not in rs:
            raise RulesError("%s is not given" % k)
    if "monster" not in rs:
        raise RulesError("the monster race is not named")

    def listed(kind, n, src):
        if sorted(src) != list(range(n)):
            raise RulesError("%s: expected %s 0..%d" % (kind, kind, n - 1))
        return [src[i] for i in range(n)]

    rs["race_names"] = listed("race", rs["races"], named["race"]) + [rs.pop("monster")]
    rs["race_labels"] = listed("race", rs["races"], labels["race"])
    rs["kind_names"] = listed("kind", KINDS, named["kind"])
    rs["kind_labels"] = listed("kind", KINDS, labels["kind"])
    rs["slot_names"] = listed("slot", SLOTS, named["slot"])
    rs["prog_names"] = listed("progression", rs["progressions"],
                              named["progression"])
    for key, field, n in SHEET:
        if any(named[k] for k, _, _ in SHEET):
            rs[field] = listed(key, n, named[key])
    for t in TABLES:
        nrows, ncols, _ = shape(rs, t[0])
        got = rows.get(t[0], [])
        if len(got) != nrows:
            raise RulesError("%s: %d rows, expected %d" % (t[0], len(got), nrows))
        b = bytearray()
        for where, el, vals in got:
            if len(vals) != ncols:
                raise RulesError("%s: %s has %d values, expected %d"
                                 % (where, t[0], len(vals), ncols))
            for v in vals:
                b += _pack(el, v, where)
        rs[t[0]] = bytes(b)
    check(rs)
    return rs


# --- starting points -------------------------------------------------------

def kind_list(race_kinds, races, stride):
    """The class list of a game that brings only its race-class rows
    (rules.c kind_list): every kind some race may take gets the next row,
    the others are flagged (+128) and share the next offered kind's row."""
    offered = [False] * KINDS
    for r in range(races):
        e = race_kinds[r * stride:(r + 1) * stride]
        for k in e[1:1 + min(e[0], stride - 1)]:
            if k < KINDS:
                offered[k] = True
    out, row = [0] * KINDS, 0
    for k in range(KINDS):
        if offered[k]:
            out[k] = row
            row += 1
    row = row - 1 if row else 0
    for k in range(KINDS - 1, -1, -1):
        if offered[k]:
            row = out[k]
        else:
            out[k] = row | 0x80
    return bytes(out)


# FRUA's tables, by A5 offset (rules.c g_rules_frua) and length
FRUA_A5 = {
    "race_kinds": (-30864, 6 * 14), "kind_aligns": (-30450, KINDS * 12),
    "kind_ability_min": (-30552, KINDS * 6), "race_abilities": (-30960, 6 * 16),
    "race_age_cat": (-30612, 6 * 10), "race_start_age": (-30780, 6 * 28),
    "race_level_cap": (-28048, 6 * 7), "slot_xp": (-30212, 7 * 12 * 4),
    "slot_xp_step": (-31228, 7 * 4), "slot_thac0": (-23184, 7 * 22),
    "slot_saves": (-28818, 7 * 110), "slot_item_bit": (-23030, 7),
    "slot_hall_bit": (-23023, 7), "slot_name_level": (-23007, 7),
    "slot_hd_count": (-23000, 7), "slot_hd_size": (-22993, 7),
    "slot_spell_row": (-30220, 7), "spell_slots": (-29876, 4 * SPELL_ROW),
    "thief_skills": (-22980, 144), "race_thief_adj": (-22836, 6 * 8),
}
FRUA_RACE_STRS = (0x48c6, 0x48ca, 0x48d4, 0x48da, 0x48e0, 0x48ea)
FRUA_KIND_STRS = (0x48fe, 0x4906, 0x490c, 0x4914, 0x491c, 0x4924, 0x4930,
                  0x4936, 0x493c, 0x494c, 0x4960, 0x496e, 0x4980, 0x498e,
                  0x49a2, 0x49b0, 0x49c2)
FRUA_KIND_LIST = bytes((0, 1 | 0x80, 1, 2, 3, 4, 5, 6 | 0x80, 6, 7, 8, 9,
                        9 | 0x80, 10, 11, 12, 13))
FRUA_SLOTS = ("cleric", "druid", "fighter", "paladin", "ranger",
              "magic-user", "thief")


def from_frua(rfork):
    """FRUA's rules, as the engine has them built in, out of the Mac
    release's resource fork (its DATA image and string pool)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import a4map
    img, drel, strs = a4map.load(rfork)
    a4 = dict(a4map.parse_a4_map(img, drel))

    def at(off, n):
        return bytes(img[len(img) + off:len(img) + off + n])

    def s(off):
        return strs[off:strs.index(b"\0", off)].decode("mac-roman")

    def names(slot, n):
        return [s(a4[slot + 4 * i]) for i in range(n)]

    rs = {"name": "FRUA", "coin": None, "races": 6, "progressions": 7,
          "xp_levels": 12, "kind_row": 14, "spell_rows": 4,
          "default_race": 5, "prime_limits": 1, "knights": 0}
    for field, (off, n) in FRUA_A5.items():
        rs[field] = at(off, n)
    rs["race_base"] = bytes(range(6))
    rs["kind_list"] = FRUA_KIND_LIST
    rs["slot_prog"] = bytes(range(7))
    rs["kind_names"] = names(-14636, KINDS)
    rs["race_names"] = names(-14564, 7)
    rs["race_labels"] = [s(o) for o in FRUA_RACE_STRS]
    rs["kind_labels"] = [s(o) for o in FRUA_KIND_STRS]
    rs["slot_names"] = list(FRUA_SLOTS)
    rs["prog_names"] = list(FRUA_SLOTS)
    check(rs)
    return rs


def from_dqk(exe):
    """The Dark Queen of Krynn's rules, as rules.c plays them, out of
    DQK.EXE (tools/gbrules.py reads the tables)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import gbrules
    t = gbrules.read_dqk(exe)
    rs = {"name": "Dark Queen of Krynn", "coin": t["money_names"][0],
          "races": gbrules.RACES, "progressions": gbrules.PROGS,
          "xp_levels": gbrules.XP_LEVELS, "kind_row": gbrules.KIND_SLOTS + 1,
          "spell_rows": gbrules.SPELL_ROWS, "default_race": 6,
          "prime_limits": 0, "knights": 1}
    for field, src in (("race_kinds", "race_kinds"), ("kind_aligns", "kind_aligns"),
                       ("kind_ability_min", "kind_ability_min"),
                       ("race_abilities", "race_abilities"),
                       ("race_age_cat", "race_age_cat"),
                       ("race_start_age", "race_start_age"),
                       ("race_level_cap", "race_level_cap"),
                       ("slot_xp", "xp"), ("slot_xp_step", "xp_step"),
                       ("slot_thac0", "slot_thac0"), ("slot_saves", "slot_saves"),
                       ("slot_item_bit", "slot_item_bit"),
                       ("slot_hall_bit", "slot_hall_bit"),
                       ("slot_name_level", "slot_name_level"),
                       ("slot_hd_count", "slot_hd_count"),
                       ("slot_hd_size", "slot_hd_size"),
                       ("slot_spell_row", "spell_row"),
                       ("spell_slots", "spell_slots"),
                       ("thief_skills", "thief_skills"),
                       ("race_thief_adj", "race_thief_adj")):
        rs[field] = bytes(t[src])
    # rules.c: both elves elves, the dwarves dwarves, kender halflings
    rs["race_base"] = bytes((0, 0, 1, 2, 2, 4, 5))
    rs["kind_list"] = kind_list(rs["race_kinds"], rs["races"], rs["kind_row"])
    rs["slot_prog"] = bytes((0, 3, 2, 10, 6, 7, 9))
    # rules.c dqk_prog (DQK.EXE 0xbb19): a cleric's god above Mishakal is of
    # neutrality; Crown/Sword/Rose (none: the Crown); a white robe, else red
    rs["prog_by"] = [("god", 0, [0, 0, 0, 0, 0, 1, 1, 1]),
                     ("order", 1, [3, 3, 4, 5]),
                     ("robe", 5, [8, 7, 8, 8])]
    rs["kind_names"] = list(t["kind_names"])
    rs["race_names"] = list(t["race_names"])
    rs["race_labels"] = list(t["race_labels"])
    rs["kind_labels"] = list(t["kind_labels"])
    rs["slot_names"] = list(gbrules.SLOTS)
    rs["prog_names"] = list(gbrules.PROGRESSIONS)
    for _, field, _ in SHEET:
        rs[field] = list(t[field])
    check(rs)
    return rs


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(prog="rules.py", description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, helptext in (("frua", "FRUA's rules from the Mac resource fork"),
                           ("dqk", "the Dark Queen's rules from DQK.EXE")):
        p = sub.add_parser(name, help=helptext)
        p.add_argument("src")
        p.add_argument("-o", "--out", help="the text (default: stdout)")
        p.add_argument("--dat", help="also write the compiled file here")
    p = sub.add_parser("compile", help="text to RULES.DAT")
    p.add_argument("src")
    p.add_argument("out")
    p = sub.add_parser("decompile", help="RULES.DAT to text")
    p.add_argument("src")
    p.add_argument("-o", "--out")
    a = ap.parse_args(argv)

    if a.cmd == "frua":
        rs = from_frua(a.src)
    elif a.cmd == "dqk":
        rs = from_dqk(open(a.src, "rb").read())
    elif a.cmd == "compile":
        rs = from_text(open(a.src, encoding="latin-1").read())
        with open(a.out, "wb") as fh:
            fh.write(to_bytes(rs))
        return 0
    else:
        rs = from_bytes(open(a.src, "rb").read())
    if getattr(a, "dat", None):
        with open(a.dat, "wb") as fh:
            fh.write(to_bytes(rs))
    text = to_text(rs)
    if a.out:
        with open(a.out, "w", encoding="latin-1") as fh:
            fh.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except RulesError as e:
        sys.exit("rules: %s" % e)
