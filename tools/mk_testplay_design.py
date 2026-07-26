#!/usr/bin/env python3
"""Build TPTEST.DSN — the module for the Mac 1.2 test-play pair, hunks 30+31.

Both hunks live on ONE code path: `l5676`'s early return at CODE 20 `0x57ac`,
taken when a **type-11 transfer** fires while a design is being TEST-PLAYED
(`-18485 != 0`, set only by the map editor's Utilities -> Test module):

    g_a5_byte(-4943) = ev[12] & 4;          /* at l5676's entry */
    ...
    if (-18485 != 0 && type == 11) {
            jt101("Transfer module ends testing!", 11, 0);   /* <- hunk 30 */
            g_a5_byte(-27982) = 1;
            return;
    }

  hunk 30  1.2 says so before tearing the test session down; 1.0 just vanishes,
           which reads as a crash to the designer.
  hunk 31  raising `-27982` makes `l709e` skip its whole convergence block —
           including 1.0's ONLY clear of `-4943`. So the flag escapes the call
           and the NEXT `l709e` sees a deferred re-trigger request left over
           from a previous test session. 1.2 clears it at the top of every
           iteration instead. This is the only leak route there is: within a
           call, 1.0's tail clear has already run before any later event could
           inherit the flag.

Three cells, everything else deliberately event-free:

  (3,3)  no event  — the map editor's cursor lands here, and it is also the
                     design's entry point, so a NORMAL play session can walk
                     in and Encamp -> Save. That save is not optional:
                     `-18485 != 0` sends `l07dc` down `jt582()` instead of the
                     Training Hall, and it bails with "No saved games!".
  (4,3)  event 1   — the type-11 transfer, `ev[12]` bit 2 set. One step
                     FORWARD of the cursor cell.
  all the
  rest   event 3   — a Yes/No question (once-only, so it fires once).

Test-play does NOT place the party at the design's entry point (`l0bbc` skips
that when `-18485 != 0`) and a LOADED save fires no landing event, so events
here only ever fire on a step. The two-session script that follows from that:

  session 1   land on (3,3), step forward onto (4,3).
              -> hunk 30's message, `-4943 = 4`, `-27982 = 1`, tail skipped.
  session 2   Escape -> Done. `jt948`'s res==4 arm breaks to the outer
              level-reload when `-27982` is set, and that reload clears it —
              measured, and it is what makes the second half reachable at all
              without leaving the adventure (there is no route from the camp
              menu back to the main menu). Then turn and step onto any
              question cell.
              -> the question's yes-branch sets `-4946`, the tail turns that
                 into `-4942`, and `if (-4942 && -4943)` reads the leak.

Nothing else may fire in between: any OTHER event's l709e iteration runs a
full tail, and 1.0's tail clear would take the leak with it. A step onto a
cell with no special is safe — `l709e(0)` breaks before its body — but there is
no safe THIRD event. That is also why the leak cannot be measured inside one
session: the transfer's own `-27982` gates the test off for the rest of it, and
a combat (the natural way to clear `-27982`) would run a full tail.

A type-36 question is the only event shape that fits the measuring end: its
yes-branch sets `-4946` and it never writes `-4943`. A type-12 scripted move
does NOT work — `l2e42` assigns `-4943 = ev[7] & 0x20` on entry and overwrites
the leak. The question is once-only so the re-scan it provokes terminates.

    python3 tools/mk_testplay_design.py data/work/gamedata --current

Full driving script in docs/deterministic-ab.md.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dsn import Design, _walled_room, _hook          # noqa: E402
from geo import EVENT_SIZE                            # noqa: E402

ENTRY = (3, 3)          # (col, row) — left event-free so normal play can save
TCELL = (4, 3)          # (col, row) — the transfer, one step FORWARD of ENTRY
                        # every OTHER cell carries the question (see below)


def testplay_design(name="TPTEST", size=8):
    a5 = _walled_room(w=size, h=size, entry=ENTRY, facing=0)
    a5.strg_write(["",
                   "Do you wish to leak the flag?",
                   "The leaked flag re-scanned this cell."])

    # event 1 (slot 0) — the transfer. Destination is deliberately area 6,
    # which does not exist: test-play returns before ever loading it, and a
    # NORMAL play session must not reach this cell anyway.
    a5.set_passage(0, dest_area=6, x=3, y=3, facing=0)
    ev = bytearray(a5.encr[0:EVENT_SIZE])
    ev[12] |= 0x04                    # -> -4943 = 4 at l5676's entry (hunk 31)
    a5.encr[0:EVENT_SIZE] = bytes(ev)

    # event 3 (slot 2) — the session-2 question. flags: bit2 CLEAR and bit5 SET
    # so the yes-branch takes `else if (ev[7] & 0x20) -4946 = 1`, and both
    # chain bytes are 0 so -4945 stays clear and the tail reaches the
    # `-4942 && -4943` test at all.
    a5.set_question(2, question=2, yes_chain=0, no_chain=0,
                    flags=0x20, once_only=True)

    # The question goes on EVERY remaining cell, not one chosen square. After
    # the camp round-trip the party's facing is not predictable headlessly (the
    # reload repaints the compass and two runs disagreed about it), and a step
    # that lands on an empty cell measures nothing — l709e(0) breaks before its
    # body. Blanketing the map makes any step in any direction except +col land
    # on the question. It is once-only, so it still fires exactly once.
    for c in range(size):
        for r in range(size):
            if (c, r) in (ENTRY, TCELL):
                continue
            _hook(a5, c, r, special=3)
    _hook(a5, TCELL[0], TCELL[1], special=1)     # -> the transfer

    d = Design(name, title="Test-play transfer test")
    d.xp = 15000
    d.start_area = 5                  # >= 5 -> dungeon mode
    d.start_entry = 1
    d.add_area(5, a5)
    return d


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    base = argv[0]
    d = testplay_design()
    folder = d.write(base, make_current=("--current" in argv))
    a5 = d.areas[5]
    print("wrote", folder)
    print("  event 1: type=%d ev[12]=0x%02x (bit2 -> -4943)"
          % (a5.event_type(0), a5.event(0)[12]))
    print("  event 3: type=%d ev[7]=0x%02x once-only"
          % (a5.event_type(2), a5.event(2)[7]))
    print("  entry (col %d, row %d) | transfer (col %d, row %d)" % (ENTRY + TCELL))
    print("  every other cell -> the question")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
