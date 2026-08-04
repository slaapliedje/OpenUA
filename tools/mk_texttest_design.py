#!/usr/bin/env python3
"""Build TEXTTEST.DSN — the UNPROMPTED square-text case, one step from entry.

This is the fixture for #161. The bug it exists to catch is a *transition*, not
a settled frame, so a single screenshot cannot see it: the text types out, the
box and the command bar are wiped by l4d26's jt23 view refresh, the sticky
replay redraws the whole message at once, and the walk loop's relayout puts the
bar back — reported from real hardware as "it gets to about 'the wandering
trav' then it redraws and finishes".

What makes this design the right instrument is `confirm_mask=0`: with no
[RETURN] page confirm the text is STICKY (DOS keeps it in the box until the
party leaves the square), which is the arm that runs the replay. A default
message (confirm after every line) takes the l1806 arm instead and never gets
there — so the obvious `mk_kobold_design.py` message does NOT reproduce it.

    python3 tools/mk_texttest_design.py [outdir] [--current] [--short]

`--short` uses a one-line message: the whole event then fits inside a single
screenshot burst, which is what you want when you are looking at the tail
rather than at the typing.

Driving it headlessly (Falcon; same shape on the other targets):

    driver.sh start
    PLAY_NUDGE=0 driver.sh beginplay        # seats the party, enters area 5
    driver.sh key Down                      # about-face
    driver.sh key Up                        # step onto the text square

★ ALL FOUR NEIGHBOURS of the entry cell carry the event, deliberately. Which
  way a key steps depends on the facing the harness happens to leave, and a
  drive that lands on an unhooked cell looks exactly like an event that
  declined to fire — it cost a full boot to learn that once.

★ TEXT IDS ARE 1-BASED (`strs[id - 1]`), so the message below asks for id 2 to
  show list index 1. Asking for id 1 shows the reserved empty slot: a blank box
  that reads as a rendering bug. Same trap `mk_kobold_design.py` documents.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dsn import Design, _walled_room, _hook          # noqa: E402

ENTRY = (3, 3)

LONG = ("THE WEARY WANDERER.  A faded sign creaks over the door of a low "
        "stone house, and the smell of stew drifts out into the passage.")
SHORT = "THE WEARY WANDERER."


def texttest_design(name="TEXTTEST", short=False):
    g = _walled_room(entry=ENTRY, facing=0)
    g.strg_write(["", SHORT if short else LONG])
    g.set_message(0, text_ids=[2], confirm_mask=0)      # 0 = unprompted/sticky
    for col, row in ((ENTRY[0], ENTRY[1] + 1), (ENTRY[0] + 1, ENTRY[1]),
                     (ENTRY[0], ENTRY[1] - 1), (ENTRY[0] - 1, ENTRY[1])):
        _hook(g, col, row, special=1)

    d = Design(name, title="Unprompted Text Test")
    d.xp = 15000
    d.start_area = 5            # >= 5 selects the first-person dungeon mode
    d.start_entry = 1
    d.add_area(5, g)
    return d


def main(argv):
    out = argv[0] if argv and not argv[0].startswith("--") else "."
    d = texttest_design(short=("--short" in argv))
    folder = d.write(out, make_current=("--current" in argv))
    print("wrote %s" % folder)
    print("  area 5: unprompted text on all four cells around (3,3)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
