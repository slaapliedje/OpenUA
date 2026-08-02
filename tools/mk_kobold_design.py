#!/usr/bin/env python3
"""Build KOBOLD.DSN — a small hand-authored dungeon: message, fight, treasure.

The acceptance test for the authoring tools: everything a real module needs, in
one short walk, so a single headless drive exercises the whole play loop.

    (3,3) entry   MESSAGE  — flavour text, one confirm
    (3,4) south   COMBAT   — 3x KOBOLD, the shortest real fight available
    (4,3) east    TREASURE — 250 pp, so the reward screen renders

Layout is deliberately a plus-shape of interesting cells around the entry, all
one step away: the drive is `Return` (clear the message), `Down` (fight),
`Up`, `Right` (treasure). No navigation to get wrong.

★ KOBOLD IS MONSTER ID 1. Combat monster ids index the BASE `MONST.GLB` (a
design ships no monster table of its own — HEIRS.DSN is GAME001 + GEO*.DAT and
nothing else), and KOBOLD is the first record in it, at offset 1061 ahead of
GOBLIN at 1157. `dsn.py`'s own demo calls `set_combat(1, [(1, 3)])` and its
comment says "3x monster 1" without naming it; it is three kobolds.

★ THE ART BLOCK IS NOT OPTIONAL. `_walled_room` stamps `hdr[4:14]`; without it
the area renders no art at all and every geometry gets the same fixed viewport
(see the `generated-areas-need-art-block` note). Anything authored here that
skips `_walled_room` has to stamp it too.

★ `special` IS THE EVENT INDEX PLUS ONE. `_hook(g, col, row, special=N)` points
the cell at event `N-1`; 0 means "no event". Off-by-one here is silent — the
cell simply fires the wrong event, or none.

Areas are numbered >= 5 so the engine runs the DUNGEON (first-person) mode
rather than the overland top-down one.

    python3 tools/mk_kobold_design.py [outdir] [--current]

`--current` also points GAME.DAT at this design, so PLAY THE GAME picks it up
without going through Select A Design.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dsn import Design, _walled_room, _hook          # noqa: E402

KOBOLD = 1          # first record in MONST.GLB
ENTRY  = (3, 3)     # (col, row)


def kobold_design(name="KOBOLD"):
    g = _walled_room(entry=ENTRY, facing=0)

    # STRG slot 0 is reserved/empty — real areas keep it that way, and the
    # event text ids below are 1-based into this list.
    g.strg_write([
        "",
        "The passage opens into a low, smoke-stained chamber. "
        "Something small is chittering in the dark to the south.",
        "Three kobolds burst from a crack in the wall, yapping!",
        "Behind a loose stone you find a purse of platinum.",
    ])

    # ★ TEXT IDS ARE 1-BASED: jt232 reads strs[id - 1], so id N shows the
    # string at list index N-1. Passing the list index directly shows the
    # PREVIOUS string — and for index 1 that is the reserved empty slot 0, i.e.
    # an empty box with a live "PRESS RETURN" prompt, which reads as an engine
    # bug. (geo.py's strg_write docstring says so; SSI data does not settle it
    # by inspection, because neighbouring ids are both plausible sentences.)
    g.set_message(0, text_ids=[2])              # event 0 — strs[1], the flavour
    g.set_combat(1, [(KOBOLD, 3)], text_id=3)   # event 1 — strs[2], the fight
    g.set_treasure(2, platinum=250)             # event 2 — the reward

    _hook(g, ENTRY[0],     ENTRY[1],     special=1)   # entry -> message
    _hook(g, ENTRY[0],     ENTRY[1] + 1, special=2)   # south -> combat
    _hook(g, ENTRY[0] + 1, ENTRY[1],     special=3)   # east  -> treasure

    d = Design(name, title="Kobold Test Dungeon")
    d.xp = 15000
    d.start_area = 5            # >= 5 selects the first-person dungeon mode
    d.start_entry = 1
    d.add_area(5, g)
    return d


def main(argv):
    out = argv[0] if argv and not argv[0].startswith("--") else "."
    d = kobold_design()
    folder = d.write(out, make_current=("--current" in argv))
    print("wrote %s" % folder)
    print("  area 5: message @ (3,3), combat 3x KOBOLD @ (3,4), "
          "treasure 250pp @ (4,3)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
