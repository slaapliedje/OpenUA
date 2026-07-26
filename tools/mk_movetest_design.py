#!/usr/bin/env python3
"""Build MOVETEST.DSN — a module for the Mac 1.2 scripted-movement hunk (29).

Hunk 29 adds two instructions to the very end of `l2e42`, the type-12
"scripted movement" event handler (an animated passage that walks the party
across the map over `ev[6]` frames):

    g_a5_byte(-4942) = 1;        /* transition done */

`-4942` is `l709e`'s chain-control flag, and it changes TWO things in the event
loop's tail — in opposite directions. Both are authorable, and this tool builds
one module for each, because a single design cannot show both at once:

  chain (default)
      The auto-chain is `if (-18484 != 0 && -4942 == 0) idx = ev[3]`. With the
      fix, `-4942` is set, so a scripted move SUPPRESSES its own `ev[3]` link.
      This variant hangs a message event off `ev[3]`:
        1.2 -> the party walks, and nothing follows.
        1.0 -> the party walks, then the follow-on message fires, resolved
               against a cell the party has already left.

  --rescan
      The deferred re-trigger is `if (-4942 != 0 && -4943 != 0) idx = jt201(
      party row, party col)`. `l2e42` sets `-4943` from `ev[7] & 0x20`, so with
      BOTH bits live the loop re-scans the cell the party landed on and fires
      the event sitting there. This variant sets `ev[7] |= 0x20`, clears
      `ev[3]`, and puts a message on the destination cell:
        1.2 -> the party walks and the destination event fires on arrival.
        1.0 -> the party walks and nothing happens (that cell's event stays
               dormant until the player steps off and back on).

    python3 tools/mk_movetest_design.py data/work/gamedata --current
    python3 tools/mk_movetest_design.py data/work/gamedata --current --rescan

Then `driver.sh beginplay`. See docs/deterministic-ab.md.

The type-12 record, read off l2e42 (boot.c ~48784):

    ev[3]      auto-chain link (event index, 1-based)
    ev[4]      re-anchor column   } only when ev[7] & 0x10
    ev[5]      re-anchor row      }
    ev[6]      frame count; frames 0..ev[6]-1 each carry a move code
    ev[7]      bit4 re-anchor, bits 2-3 facing, bit5 -> -4943
    ev[8..13]  move codes, 2 bits per frame, 4 per byte, low bits first:
               0 = hold, 1 = turn left, 2 = turn right, 3 = step forward
    ev[14/15]  frame numbers at which text 1 / text 2 appear
    ev[16..19] the two text ids, little-endian pairs (0 = none)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dsn import Design, _walled_room, _hook          # noqa: E402
from geo import EVENT_SIZE                            # noqa: E402

STEP_FORWARD = 3


def _movement_event(chain=0, frames=2, rescan=False):
    """A 20-byte type-12 record that walks the party `frames` cells forward."""
    ev = bytearray(EVENT_SIZE)
    ev[0] = 12
    ev[1] = 0                       # condition "always", not once-only, and
                                    # bits 1/2 clear so l694e leaves -18484 = 1
    ev[3] = chain & 0xff
    ev[6] = frames
    ev[7] = 0x20 if rescan else 0x00
    for f in range(frames):         # every frame is a forward step
        ev[8 + (f >> 2)] |= STEP_FORWARD << ((f & 3) * 2)
    return bytes(ev)


def movetest_design(name="MOVETEST", rescan=False, frames=2):
    """Entry cell (col 3, row 3) facing 0; the walk lands on (col 3-frames, 3).

    FACING 0 DECREMENTS THE COLUMN, not the row. Measured: the party started at
    (col 3, row 3) and two forward frames put it at (col 1, row 3) — `jt201`
    indexes `height*col + row`, and the engine's -12288/-12287 pair is
    (row, col) in that order. `_walled_room` names edge 0 "N" and hangs it off
    `row == 0`, so the perimeter wall is NOT on the axis this walk travels;
    nothing stops the party before the map edge. Keep `frames` at 2 unless you
    have re-measured, because a walk that ends somewhere other than the hooked
    cell makes --rescan silently measure nothing.
    """
    dest_col = 3 - frames
    if dest_col < 0:
        raise SystemExit("frames=%d walks off the west edge of the room" % frames)

    a5 = _walled_room(entry=(3, 3), facing=0)
    a5.strg_write(["",
                   "THE CHAIN FIRED - event 2 ran after the move.",
                   "THE DESTINATION EVENT FIRED - you arrived here."])

    if rescan:
        a5.set_event(0, _movement_event(chain=0, frames=frames, rescan=True))
        a5.set_message(2, text_ids=[3])          # slot 2 -> event index 3
        _hook(a5, dest_col, 3, special=3)        # the cell the move lands on
    else:
        a5.set_event(0, _movement_event(chain=2, frames=frames))
        a5.set_message(1, text_ids=[2])          # slot 1 -> event index 2

    _hook(a5, 3, 3, special=1)                   # entry cell -> the move

    d = Design(name, title="Scripted-movement test")
    d.xp = 15000
    d.start_area = 5                             # >= 5 -> dungeon mode
    d.start_entry = 1
    d.add_area(5, a5)
    return d


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    base = argv[0]
    rescan = "--rescan" in argv
    frames = 2
    if "--frames" in argv:
        frames = int(argv[argv.index("--frames") + 1])
    name = "MOVERESC" if rescan else "MOVETEST"

    d = movetest_design(name, rescan=rescan, frames=frames)
    folder = d.write(base, make_current=("--current" in argv))
    ev = d.areas[5].event(0)
    print("wrote", folder)
    print("  variant: %s" % ("rescan (-4943 via ev[7] bit5)" if rescan
                             else "auto-chain (ev[3])"))
    print("  event 0: type=%d ev[3]=%d ev[6]=%d ev[7]=0x%02x ev[8]=0x%02x"
          % (ev[0], ev[3], ev[6], ev[7], ev[8]))
    print("  party walks (col 3, row 3) -> (col %d, row 3)" % (3 - frames))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
