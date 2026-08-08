#!/usr/bin/env python3
"""Build CHAINCBT.DSN — a MESSAGE event that CHAINS straight into a small
combat, on ONE cell. This reproduces the ogre-fight structure (HEIRS event 60
"THE ROADWARDEN..." chains to the ogre combat) but with 3 kobolds that resolve
in a couple of rounds, so the post-combat walk-return can be tested fast.

The ogre bug does NOT reproduce with a DIRECT cell combat (verified: KOBOLD.DSN
at (3,4) relayouts the walk bar correctly). It needs the combat to arrive via a
CHAIN, so the fight resolves AFTER the message's Returns — past the step that
armed g_event_modal_shown. Layout: entry (3,3) hooks the message; the message
chains to the combat; nothing else needed.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dsn import Design, _walled_room, _hook   # noqa: E402

KOBOLD = 1
ENTRY  = (3, 3)


def design(name="CHAINCBT"):
    g = _walled_room(entry=ENTRY, facing=0)
    g.strg_write([
        "",
        "A horn blares. Three kobolds burst from the dark, yapping for blood!",
        "The kobolds lie dead.",
    ])
    # event 0 = MESSAGE; event 1 = the combat. Chain the message straight into
    # the combat by setting event 0's chain byte to 2 (1-BASED: l709e indexes
    # the target as (chain-1)*20, so 2 -> event index 1). Poked directly so the
    # message TYPE set by set_message is preserved.
    g.set_message(0, text_ids=[2])
    g.set_combat(1, [(KOBOLD, 3)], text_id=3)
    g.encr[0 * 20 + 3] = 2          # event 0 chain -> event 1 (combat)
    # Hook ALL FOUR orthogonal neighbours of entry with the chained message, so
    # a single step in ANY direction lands on it (sidesteps the col/row axis
    # question — the party cannot miss it). A cell event fires on STEP-ONTO,
    # never on spawn, so the entry cell itself is deliberately not hooked.
    for dc, dr in ((0, 1), (0, -1), (1, 0), (-1, 0)):
        _hook(g, ENTRY[0] + dc, ENTRY[1] + dr, special=1)
    d = Design(name, title="Chain Combat Test")
    d.xp = 15000
    d.start_area = 5
    d.start_entry = 1
    d.add_area(5, g)
    return d


def main(argv):
    out = argv[0] if argv and not argv[0].startswith("--") else "."
    d = design()
    folder = d.write(out, make_current=("--current" in argv))
    print("wrote %s" % folder)
    print("  area 5: entry (3,3) MESSAGE -> chains to 3x KOBOLD combat")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
