#!/usr/bin/env python3
"""Author a spell-casting roster character (a .CHR file) for the headless harness.

The synthetic boot roster is all fighters with no memorized spells, so every
spell screen in the engine — the camp Memorize/Cast flows, and the jt595 spell
picker behind the character sheet's "Spells" verb — is unreachable headless.
This writes a Magic-User into the design folder's saved-character pool so those
screens can be driven.

The pool is enumerated by load_roster() from "CHAR*.CHR" in the gamedata dir,
deserialized by the faithful .cch reader (JT[577]). A 398-byte file is exactly
the record with empty inventory and spell-book chains, which is what we want:
no items means the character sheet's command bar has no "Items" verb, and that
is load-bearing for the hunk-16 A/B (see docs/deterministic-ab.md).

Record fields this touches:

    96..111   name, C string
    89        CHAR_CLASS — 5 = Magic-User
    112+2i    ability score i (STR INT WIS DEX CON CHA), base
    113+2i    the same score, current — l4e2c reads rec[115] (INT) > 8
    157+n     per-class level; jt40(rec, 5) is the mage level
    198..338  the 141 memorized-spell slots (bit 7 set = "to memorize")

Spell ids are indices into the runtime -16906 table (class at [0], level at
[1]), so they are game-data defined, not ours. Measured against the stock
tables: 1..8 are cleric level 1, 9..21 are MAGE level 1, 22.. are cleric
level 2. Only ids whose class the character can cast (l4e2c) reach the list.

Usage:
    python3 tools/mk_caster_chr.py data/work/gamedata            # defaults
    python3 tools/mk_caster_chr.py <dir> --slot 4 --name MERLIN
"""

import argparse
import os
import sys

RECORD_LEN = 398

CHAR_XP = 68            # long
CHAR_RACE = 88
CHAR_CLASS = 89
CHAR_ALIGN = 93
CHAR_NAME = 96
CHAR_MAXHP = 129
CHAR_LEVEL = 157        # per-class level slot 0; +n for class n
CHAR_MEMORIZED = 198    # 141 slots
CHAR_THAC0 = 384        # displayed THAC0 = 60 - this
CHAR_AC = 385           # displayed AC = |this - 60|
CHAR_HP = 395
CHAR_MOVE = 396

CLASS_MAGIC_USER = 5
RACE_HUMAN = 5

# STR INT WIS DEX CON CHA — INT 17 clears l4e2c's `rec[115] > 8` mage gate.
DEFAULT_STATS = (9, 17, 12, 12, 16, 14)

# The stock mage level-1 band, verified live via the FRUA_SPLDIAG jt597 probe.
MAGE_L1_SPELLS = tuple(range(9, 22))


def build(name, stats, spells, mage_level, hp):
    rec = bytearray(RECORD_LEN)

    encoded = name.encode("ascii")[:15]
    rec[CHAR_NAME:CHAR_NAME + len(encoded)] = encoded

    rec[CHAR_RACE] = RACE_HUMAN
    rec[CHAR_CLASS] = CLASS_MAGIC_USER
    rec[CHAR_ALIGN] = 0
    rec[CHAR_XP:CHAR_XP + 4] = (mage_level * 10000).to_bytes(4, "big")
    for i, score in enumerate(stats):
        rec[112 + i * 2] = score        # base
        rec[113 + i * 2] = score        # current
    rec[CHAR_LEVEL + CLASS_MAGIC_USER] = mage_level

    # Alive, and not so fragile that a stray fight ends the session.
    rec[CHAR_HP] = hp
    rec[CHAR_MAXHP] = hp
    rec[CHAR_AC] = 60 - 10              # displayed AC 10
    rec[CHAR_THAC0] = 60 - 16           # displayed THAC0 16
    rec[CHAR_MOVE] = 12

    for i, spell in enumerate(spells):
        rec[CHAR_MEMORIZED + i] = spell

    return bytes(rec)


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
    args = ap.parse_args(argv)

    if not os.path.isdir(args.gamedata):
        sys.exit("not a directory: %s" % args.gamedata)

    spells = [int(s) for s in args.spells.split(",") if s.strip()]
    if len(spells) > 141:
        sys.exit("at most 141 memorized spells")

    path = os.path.join(args.gamedata, "CHAR%04d.CHR" % args.slot)
    with open(path, "wb") as fh:
        fh.write(build(args.name, DEFAULT_STATS, spells, args.level, args.hp))

    print("wrote %s: %s, Magic-User L%d, %d memorized spells"
          % (path, args.name, args.level, len(spells)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
