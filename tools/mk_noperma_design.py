#!/usr/bin/env python3
"""Build NOPERMA.DSN — a test module for the Mac 1.2 no-permadeath family.

Hunks 1 (jt39), 15 (l102a) and 17 (jt860) all gate on the runtime flag
`hdr[29]`, which the combat entry seeds from the combat event's
`ev[12] & 0x40` (boot.c ~48861) and clears when the fight resolves. No design
shipped with the game sets that bit on the HEIRS path, so the family cannot be
exercised there — this authors the situation directly.

The room is deliberately hostile: one entry cell that fires a combat with the
maximum monster count the event format allows, so the seeded single-character
party is overwhelmed and actually reaches the dying/destroyed branches the three
fixes guard. Pair it with -DFRUA_CBTPLAY (auto party turn) and
-DFRUA_RNGSEED (reproducible dice); see docs/deterministic-ab.md.

    python3 tools/mk_noperma_design.py data/work/gamedata --current
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dsn import Design, _walled_room, _hook          # noqa: E402
from geo import EVENT_SIZE                            # noqa: E402


def noperma_design(name="NOPERMA", monster_id=1, flag=True, count=2):
    """One walled room; the party's entry cell fires a no-permadeath combat.

    `flag=False` builds the identical module with bit 6 CLEAR — the A/B control,
    so the two runs differ in exactly one bit of one event byte.

    KEEP `count` SMALL. The first version of this used six groups of 31 (the
    format's maximum) on the theory that more monsters = a faster party death.
    The opposite happened: every one of the 186 takes an animated turn on a
    16 MHz 030, so a single round outlasted any sane headless timeout and the
    run looked like a stall. Two monsters plus -DFRUA_PARTYHP=1 kills the party
    in round 1 and lets round 2 actually arrive, which is what the l102a
    bleed-out (hunk 15) needs.
    """
    a5 = _walled_room(entry=(3, 3), facing=0)
    a5.strg_write(["", "No-permadeath test chamber."])

    a5.set_combat(0, [(monster_id, count)])

    # ev[12] is group 2's count byte; its HIGH bits are config flags. Bit 5
    # (0x20) is hunk 24's "start adjacent"; bit 6 (0x40) is the one the combat
    # entry copies into hdr[29]. Set both: adjacent so the fight joins at melee
    # immediately (fewer rounds to a death), and bit 6 for the flag under test.
    ev = bytearray(a5.encr[0:EVENT_SIZE])
    ev[12] |= 0x20 | (0x40 if flag else 0x00)
    a5.encr[0:EVENT_SIZE] = bytes(ev)

    _hook(a5, 3, 3, special=1)                  # entry cell -> event 0

    d = Design(name, title="No-permadeath test")
    d.xp = 15000
    d.start_area = 5                            # >= 5 -> dungeon mode
    d.start_entry = 1
    d.add_area(5, a5)
    return d


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    base = argv[0]
    flag = "--noflag" not in argv
    name = "NOPERMA" if flag else "NOPERMB"
    d = noperma_design(name, flag=flag)
    folder = d.write(base, make_current=("--current" in argv))
    ev = d.areas[5].event(0)
    print("wrote", folder)
    print("  event 0: type=%d ev[12]=0x%02x  (bit5 adjacent=%d, bit6 noperma=%d)"
          % (d.areas[5].event_type(0), ev[12],
             (ev[12] >> 5) & 1, (ev[12] >> 6) & 1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
