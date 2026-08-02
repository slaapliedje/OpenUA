#!/usr/bin/env python3
"""Author a balanced roster party (CHAR*.CHR files) for the headless harness.

    python3 tools/mk_party.py data/work/gamedata [--start 1] [--level 5]

Six characters, one per single-class the engine knows, written into the
gamedata directory's saved-character pool.

**This writes FILE STRUCTURES — it does not drive character generation.** The
two are different tests and neither substitutes for the other:

  * this tool exercises the LOADER (`load_roster` -> `l_cch_read`, the faithful
    JT[577] .cch reader) and gives a deterministic party for play testing;
  * the real CODE 17 chargen — Training Hall -> CREATE -> roll/pick/finalize —
    is a separate path and is listed in `docs/milestone.md` §B as lifted but
    NOT live-verified. Driving it headlessly is the test that covers it, and
    this tool deliberately does not stand in for that.

The pool is enumerated by `load_roster()` from "CHAR*.CHR" (max 16). Loading a
character into the POOL is not the same as putting it in the PARTY — party
membership is the `-27928` list, assembled in the Training Hall (ADD) or by the
boot seed. Expect these to show up as available-to-add, not auto-partied.

Class ids are the FAITHFUL `rec[89]` values (`cg_class_to_port`, boot.c ~9701):
0 Cleric, 1 Knight, 2 Fighter, 3 Paladin, 4 Ranger, 5 Magic-User, 6 Thief,
7 Monk. `rec[157 + class]` is that class's level row.

Record construction is `mk_caster_chr.build()` — reused rather than
reimplemented, because it carries the hard-won field notes (write the BASE
AC/THAC0/move, not the derived ones; `rec[382]`/`rec[130]` gate every magic
command). Only the class byte and the level row are patched afterwards.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mk_caster_chr as C                                # noqa: E402

# name, faithful class id, hp — a classic six, hp generous enough that a stray
# fight does not end a headless session.
PARTY = [
    ("THORGRIM", 2, 64),   # Fighter
    ("SISTER ANN", 0, 48),  # Cleric
    ("MERLINA", 5, 34),     # Magic-User
    ("PIP", 6, 40),         # Thief
    ("SIR GARETH", 3, 58),  # Paladin
    ("HAWKE", 4, 55),       # Ranger
]


def build_member(name, klass, hp, level):
    """A .CHR record for `klass`. Spell-less: casters get an empty grimoire so
    nothing here depends on the spell tables (mk_caster_chr covers that case)."""
    rec = bytearray(C.build(name, C.DEFAULT_STATS, (), level, hp))
    # build() hardcodes the Magic-User class and its level row; retarget both.
    rec[C.CHAR_CLASS] = klass
    rec[C.CHAR_LEVEL + C.CLASS_MAGIC_USER] = 0
    rec[C.CHAR_LEVEL + klass] = level
    return bytes(rec)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("gamedata", help="directory to write CHAR*.CHR into")
    ap.add_argument("--start", type=int, default=1,
                    help="first pool slot (default 1; slot 0 is the stock "
                         "BARBARUS in the staged tree)")
    ap.add_argument("--level", type=int, default=5)
    args = ap.parse_args(argv)

    for i, (name, klass, hp) in enumerate(PARTY):
        slot = args.start + i
        path = os.path.join(args.gamedata, "CHAR%04d.CHR" % slot)
        with open(path, "wb") as fh:
            fh.write(build_member(name, klass, hp, args.level))
        print("wrote %s: %-11s class %d, L%d, %d hp"
              % (os.path.basename(path), name, klass, args.level, hp))
    print("%d characters in the pool (max 16 are loaded)" % len(PARTY))
    return 0


if __name__ == "__main__":
    sys.exit(main())
