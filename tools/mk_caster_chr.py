#!/usr/bin/env python3
"""Author a spell-casting roster character (a .CHR file) for the headless harness.

The synthetic boot roster is all fighters with no memorized spells, so every
spell screen in the engine — the camp Cast/Memorize/Scribe flows, and the jt595
spell picker behind the character sheet's "Spells" verb — is unreachable
headless. This writes a Magic-User into the design folder's saved-character pool
so those screens can be driven.

The pool is enumerated by load_roster() from "CHAR*.CHR" in the gamedata dir and
deserialized by the faithful .cch reader (JT[577], boot.c ~38243).

WHAT EACH SCREEN NEEDS — the three are gated differently, which is why they came
online one at a time:

    CAST      rec[382] != 0 and rec[94] == 0 (l05c4's gate), plus memorized
              spell bytes in rec[198..338]. Verified 2026-07-26.
    MEMORIZE  a GRIMOIRE: bits in rec[339..354]. jt597(1) lists a spell only if
              its bit is set AND rec[355 + class*9 + level-1] (the capacity
              grid) is non-zero for its class/level.
    SCRIBE    a SCROLL in the inventory chain: jt597(6)/l5900 walk rec[8]'s item
              list, keep the ones jt638 accepts (item type 39, 40 or 73) and
              read up to four spell ids from item bytes [54..57].

★ THE CAPACITY GRID IS NOT OURS TO WRITE. rec[355..381] (3 classes x 9 levels)
  is rebuilt by jt908 from the class-level progression every time jt910 runs —
  which the party-Add path does (boot.c ~36687), and which zeroes the 27 bytes
  first (jt399). All four stock characters ship it ALL-ZERO for exactly this
  reason. Writing it here would be overwritten and would teach the reader a
  falsehood. jt908 does NOT touch rec[339..354], so the grimoire bits below do
  survive.

★ WHY A FRESH MAGE HAS NO GRIMOIRE IN THE FIRST PLACE. jt908's cleric/paladin/
  ranger arms set grimoire bits for every spell of a class they can cast — the
  divine casters know their whole list. The MAGE arm (case 5) grants capacity
  only and sets no bits: an AD&D mage learns spells by scribing them. So an
  empty grimoire on a newly authored mage is CORRECT ENGINE BEHAVIOUR, not a
  gap — the harness has to author the spells the way play would have acquired
  them.

Record fields this touches:

    4         spell-list chain head — left 0 (unrelated to the 198.. slots)
    8         inventory chain head. NON-ZERO on disk means "this file has an
              inventory"; jt577 reads the value only as a flag (it saves it,
              zeroes the slot, then loops while it is non-zero). The stock
              files carry a stale Mac pointer here.
    68        XP, long, LITTLE-ENDIAN on disk (see the endianness note below)
    76        money, word, little-endian
    88/89     race / class (5 = Magic-User; 89 also indexes the 157+n level row)
    94        conscious flag — l05c4 refuses when this is 1
    96..111   name, C string
    112+2i    ability score i (STR INT WIS DEX CON CHA), base
    113+2i    the same score, current — l4e2c reads rec[115] (INT) > 8
    157+n     per-class level; jt40(rec, 5) is the mage level
    193       inventory item count; the file is 398 + 18*count bytes
    198..338  the 141 memorized-spell slots (bit 7 set = "to memorize")
    339..354  the 126 GRIMOIRE bits — spell i at byte 339 + (i-1)/8,
              mask 1 << ((i-1) & 7)
    382       "alive" — l05c4's gate; every magic command dies without it

★ ENDIANNESS. The .cch file is LITTLE-endian for the fields l0ce0_c15 (boot.c
  ~38123) swaps on the way in: the long at 68, the long at 72, the words at
  76/78/80, 82, 84 and 86. Everything else is bytes. An earlier version of this
  tool wrote XP big-endian, so 50000 was read back as 0x50C30000 — a level-5
  mage with 1.35 billion experience. Verified against the stock characters,
  which all carry little-endian 50000 at +68.

★ THE GRIMOIRE BIT ORDER is mask[bit] = 1 << bit, LSB-first — read out of the
  real -18893 table in the DATA pool, and cross-checked against the 0x00ff
  run checksum committed in src/engine/a4_map.c (8 single-bit masks sum to 255).

Spell ids are indices into the runtime -16906 table (class at [0], level at
[1]), so they are game-data defined, not ours. The bands below were dumped from
that table: class 0 = cleric, 1 = druid, 2 = MAGE.

Usage:
    python3 tools/mk_caster_chr.py data/work/gamedata            # defaults
    python3 tools/mk_caster_chr.py <dir> --slot 4 --name MERLIN
    python3 tools/mk_caster_chr.py <dir> --scroll-spells 20,21   # + SCRIBE
"""

import argparse
import os
import sys

RECORD_LEN = 398

CHAR_SPELL_HEAD = 4     # long
CHAR_INV_HEAD = 8       # long — non-zero = "has an inventory" (a flag, not a ptr)
CHAR_XP = 68            # long, little-endian
CHAR_MONEY = 76         # word, little-endian
CHAR_RACE = 88
CHAR_CLASS = 89
CHAR_CONSCIOUS = 94     # l05c4 refuses when this is 1
CHAR_ALIGN = 93
CHAR_NAME = 96
CHAR_MAXHP = 129
CHAR_LEVEL = 157        # per-class level slot 0; +n for class n
CHAR_ITEM_COUNT = 193
CHAR_MEMORIZED = 198    # 141 slots
CHAR_GRIMOIRE = 339     # 16 bytes = 126 spell bits
CHAR_CAPACITY = 355     # 3 classes x 9 levels — jt908 OWNS THIS, do not write
# BASE combat fields — these are the authoritative ones. l4842 (boot.c ~38943)
# resets the derived bytes from them on every load: 384<-127, 385<-179, 396<-136.
CHAR_BASE_THAC0 = 127
CHAR_BASE_MOVE = 136
CHAR_BASE_AC = 179
# l05c4 refuses EVERY magic command when this is 0 (or rec[94] == 1), returning
# before jt597 builds any spell list. char-gen seeds it to 1; so must we.
CHAR_ALIVE = 382
# DERIVED bytes, rewritten from the bases above. Displayed AC = 60 - rec[385];
# displayed THAC0 = 60 - rec[384]. All four stock characters carry base AC 50
# (= displayed 10, unarmoured) and base move 12.
CHAR_THAC0 = 384
CHAR_AC = 385
CHAR_HP = 395
CHAR_MOVE = 396

CLASS_MAGIC_USER = 5    # rec[89]; also the 157+n row jt40(rec, 5) reads
RACE_HUMAN = 5
SPELLDEF_CLASS_MAGE = 2  # the -16906 table's class code for mage spells

# STR INT WIS DEX CON CHA — INT 17 clears l4e2c's `rec[115] > 8` mage gate.
DEFAULT_STATS = (9, 17, 12, 12, 16, 14)

# The mage (spell-def class 2) bands, dumped from the -16906 table. Level 1
# matches the thirteen spells the FRUA_SPLDIAG jt597 probe listed live.
MAGE_SPELLS_BY_LEVEL = {
    1: (9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
    2: (29, 30, 31, 32, 33, 34, 35),
    3: (45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55),
    4: (81, 82, 83, 84, 85, 86, 87, 88, 89, 100),
    5: (91, 92, 93, 94, 118, 119),
    6: (110, 111, 112, 113, 114),
    7: (115, 116, 117),
    8: (120, 121, 122, 123),
    9: (124, 125, 126),
}
MAGE_L1_SPELLS = MAGE_SPELLS_BY_LEVEL[1]        # kept: callers import this

# A mage of class level N can memorize spells up to level (N+1)//2 — L1 gets
# 1st, L3 gets 2nd, L5 gets 3rd. Past that jt597(1) filters the row out anyway
# (its rec[355+...] capacity cell is 0), so this only keeps the grimoire honest.
def max_spell_level(mage_level):
    return max(0, min(9, (mage_level + 1) // 2))


# --- scrolls ---------------------------------------------------------------
# An 18-byte item template, the same shape as an ITEM.DAT row (tools/items.py):
# [0] type, [1..3] name-word indices ([3] == the type for base items), [4..5]
# weight LE, [6..7] value LE, [10] usability, [11] known-name mask, [13] stack
# count, [14..17] the up-to-four scroll spell ids l5726 reads at node[54..57].
SCROLL_TYPE_MAGE = 39   # jt638 accepts 39, 40 (cleric scroll) and 73 (bundle)
SCROLL_MAX_SPELLS = 4


def build_scroll(spells):
    """One mage-scroll item, ready to append after the 398-byte record.

    ★ [11] IS DELIBERATELY 0. Real scrolls (ITEM.DAT ids 252..254) carry 0x04
    there, and l5726 SKIPS any scroll with a low-3 bit of node[51] set unless
    the reader has record-flag 16 (jt41) or a cleric level — the engine's
    Read Magic gate. Authoring the scroll pre-read is what makes SCRIBE
    reachable in one step instead of needing a Read Magic cast first; it is a
    deliberate harness deviation from a shipped item, not a guess at the format.
    """
    if len(spells) > SCROLL_MAX_SPELLS:
        raise ValueError("a scroll holds at most %d spells" % SCROLL_MAX_SPELLS)
    it = bytearray(18)
    it[0] = SCROLL_TYPE_MAGE
    it[3] = SCROLL_TYPE_MAGE            # primary name-word == type for base items
    it[4:6] = (25).to_bytes(2, "little")        # 2.5 lb, as the real scrolls
    it[6:8] = (0).to_bytes(2, "little")        # value: irrelevant to scribing
    it[10] = 1                                  # usability, as every stock item
    it[11] = 0                                  # ★ pre-read; see above
    for i, s in enumerate(spells):
        it[14 + i] = s
    return bytes(it)


def build(name, stats, spells, mage_level, hp, grimoire=(), scroll_spells=()):
    rec = bytearray(RECORD_LEN)

    encoded = name.encode("ascii")[:15]
    rec[CHAR_NAME:CHAR_NAME + len(encoded)] = encoded

    rec[CHAR_RACE] = RACE_HUMAN
    rec[CHAR_CLASS] = CLASS_MAGIC_USER
    rec[CHAR_ALIGN] = 0
    # ★ LITTLE-endian: l0ce0_c15 swaps the long at 68 on the way in.
    rec[CHAR_XP:CHAR_XP + 4] = (mage_level * 10000).to_bytes(4, "little")
    rec[CHAR_MONEY:CHAR_MONEY + 2] = (100).to_bytes(2, "little")
    for i, score in enumerate(stats):
        rec[112 + i * 2] = score        # base
        rec[113 + i * 2] = score        # current
    rec[CHAR_LEVEL + CLASS_MAGIC_USER] = mage_level

    # Alive, and not so fragile that a stray fight ends the session.
    rec[CHAR_HP] = hp
    rec[CHAR_MAXHP] = hp

    # ★ Write the BASE fields, not the derived ones. l4842's "reset combat/move/
    # save bases" pass does
    #       m[384] = m[127]     THAC0
    #       m[385] = m[179]     AC
    #       m[396] = m[136]     movement
    # (boot.c ~38943), so anything written straight to 384/385/396 is overwritten
    # the moment the character is loaded into a party and then saved back. The
    # earlier version of this function set only the derived bytes, which is why
    # MERLIN displayed **AC 60 and Move 1**: the engine copied his zeroed bases
    # over them. The roster renders AC as 60 - rec[385], so base 50 is the
    # unarmoured AC 10 that all four stock characters carry.
    rec[CHAR_BASE_AC] = 50              # -> derived 385, displayed AC 10
    rec[CHAR_BASE_THAC0] = 40           # -> derived 384; L5 mage THAC0 20
    rec[CHAR_BASE_MOVE] = 12            # -> derived 396
    # Seed the derived bytes too, so the record reads correctly even BEFORE the
    # first load/save round-trip re-derives them.
    rec[CHAR_AC] = 50
    rec[CHAR_THAC0] = 40
    rec[CHAR_MOVE] = 12

    # ★ THE FIELDS THAT GATE EVERY MAGIC COMMAND. l05c4 (boot.c ~100112) is the
    # precondition on camp CAST / MEMORIZE / SCRIBE, and it refuses with
    # "<name> is in no condition to cast any spells" when
    #       rec[94] == 1  ||  rec[382] == 0
    # returning BEFORE jt597 ever builds a spell list. A record missing rec[382]
    # therefore produces a CAST screen that opens, flashes a message and comes
    # straight back with no list — which reads exactly like a broken spell
    # picker and is not one. All four stock characters carry 382=1 / 130=1.
    #
    # These values mirror cg_build_record's own seed line (boot.c ~29158), which
    # is the engine's authoring path and so the right thing to copy:
    #     cg_rec[179]=50  cg_rec[127]=40  cg_rec[94]=0
    #     cg_rec[382]=1   cg_rec[130]=1   cg_rec[189]=8
    rec[CHAR_ALIVE] = 1                 # 382 — l05c4's gate
    rec[130] = 1
    rec[189] = 8
    rec[CHAR_CONSCIOUS] = 0             # 94 — l05c4 also refuses when this is 1

    for i, spell in enumerate(spells):
        rec[CHAR_MEMORIZED + i] = spell

    # The grimoire: bit (i-1)&7 of byte 339 + (i-1)/8, LSB-first.
    for spell in grimoire:
        rec[CHAR_GRIMOIRE + ((spell - 1) >> 3)] |= 1 << ((spell - 1) & 7)

    # Deliberately NOT written: rec[355..381]. jt908 rebuilds it on Add.
    assert not any(rec[CHAR_CAPACITY:CHAR_ALIVE]), "jt908 owns rec[355..381]"

    out = bytearray(rec)
    if scroll_spells:
        # rec[8] non-zero is what makes jt577 enter its inventory loop at all;
        # the stock files carry a stale Mac pointer, any non-zero will do.
        out[CHAR_INV_HEAD:CHAR_INV_HEAD + 4] = (1).to_bytes(4, "big")
        out[CHAR_ITEM_COUNT] = 1
        out += build_scroll(scroll_spells)
    return bytes(out)


def default_grimoire(mage_level, exclude=()):
    """Every mage spell the character could memorize, minus `exclude`.

    Spells held back via `exclude` are what makes SCRIBE reachable: l0df2
    rejects a pick the grimoire already contains ("already knows that spell"),
    so a scroll is only scribable while its spells are still missing.
    """
    top = max_spell_level(mage_level)
    out = []
    for level in range(1, top + 1):
        out += [s for s in MAGE_SPELLS_BY_LEVEL[level] if s not in exclude]
    return tuple(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("gamedata", help="the design/gamedata directory to write into")
    ap.add_argument("--slot", type=int, default=4,
                    help="pool slot -> CHAR%%04d.CHR (default 4, past the "
                         "usual seeded 0..3)")
    ap.add_argument("--name", default="MERLIN", help="character name (default MERLIN)")
    ap.add_argument("--level", type=int, default=5, help="mage class level (default 5)")
    ap.add_argument("--hp", type=int, default=49, help="hit points (default 49)")
    ap.add_argument("--spells", default=",".join(str(s) for s in MAGE_L1_SPELLS),
                    help="comma-separated memorized spell ids")
    ap.add_argument("--grimoire", default=None,
                    help="comma-separated grimoire spell ids (default: every "
                         "mage spell up to the level's cap, minus "
                         "--scroll-spells). Empty string = no grimoire, which "
                         "leaves MEMORIZE with nothing to offer.")
    ap.add_argument("--scroll-spells", default="",
                    help="up to 4 spell ids on a scroll added to the "
                         "inventory, which is what makes SCRIBE reachable. "
                         "OFF by default: an empty inventory is load-bearing "
                         "for the hunk-16 A/B (docs/deterministic-ab.md) "
                         "because it keeps the 'Items' verb off the character "
                         "sheet's command bar.")
    args = ap.parse_args(argv)

    if not os.path.isdir(args.gamedata):
        sys.exit("not a directory: %s" % args.gamedata)

    def ids(text):
        return [int(s) for s in text.split(",") if s.strip()]

    spells = ids(args.spells)
    if len(spells) > 141:
        sys.exit("at most 141 memorized spells")
    scroll = ids(args.scroll_spells)
    if len(scroll) > SCROLL_MAX_SPELLS:
        sys.exit("a scroll holds at most %d spells" % SCROLL_MAX_SPELLS)

    if args.grimoire is None:
        grimoire = default_grimoire(args.level, exclude=scroll)
    else:
        grimoire = ids(args.grimoire)
    for s in tuple(grimoire) + tuple(scroll) + tuple(spells):
        if not 1 <= s <= 126:
            sys.exit("spell id out of range 1..126: %d" % s)

    path = os.path.join(args.gamedata, "CHAR%04d.CHR" % args.slot)
    with open(path, "wb") as fh:
        fh.write(build(args.name, DEFAULT_STATS, spells, args.level, args.hp,
                       grimoire=grimoire, scroll_spells=scroll))

    print("wrote %s: %s, Magic-User L%d" % (path, args.name, args.level))
    print("  %d memorized, %d in grimoire (spell levels 1..%d)"
          % (len(spells), len(grimoire), max_spell_level(args.level)))
    if scroll:
        print("  1 scroll carrying %s -> SCRIBE reachable"
              % ",".join(str(s) for s in scroll))
    else:
        print("  no inventory -> SCRIBE has no copyable scrolls "
              "(pass --scroll-spells to change that)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
