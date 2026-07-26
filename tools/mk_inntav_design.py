#!/usr/bin/env python3
"""Build INNTAV.DSN — a chamber that puts the INN, the TAVERN and the town
MENU META-EVENT one step from the party, so all three can be driven headlessly.

WHY THIS EXISTS (task #80). l398a (inn, event type 29) and l4f9a (tavern, type
7) are real lifted bodies that no headless run had ever entered. They ARE
present in the shipped modules, but not somewhere a harness can reach:

  * HEIRS' only placed inn/tavern route is event 26 of GEO001 — a type-22 town
    on the OVERLAND at (col 1, row 9). The overland gates movement on TERRAIN,
    not walls, and the party lands in a mountain range that answers "there is no
    way to go in that direction". There is also no col,row readout on the
    overland HUD, so a mis-step is invisible. Reaching it is a navigation
    puzzle, not a test.
  * HEIRS' four direct type-7 taverns: three (GEO005 ev41, GEO006 ev41,
    GEO007 ev2) have NO cell pointing at them at all, and the fourth
    (GEO017 ev21 at col 6, row 9) needs area 17 entered first.
  * The designs with plenty of placed inns — GIANTS.DSN, Game39/Game40 — are
    not the current design, and switching costs a picker round-trip.

So this authors the situation instead, the way mk_temple_design.py does for the
temple endian probe. Cheap, deterministic, and it needs no copyrighted module.

    python3 tools/mk_inntav_design.py data/work/gamedata --current

LAYOUT — 9x9 walled room, party at (col 4, row 4):

              (4,3)  TAVERN        type  7   -> l4f9a
    (3,4) . . (4,4)  party . . . . (5,4)  MENU META  type 22 -> l5bde
              (4,5)  INN           type 29   -> l398a

★ THE EVENT ALSO GOES ON THE ENTRY CELL, chosen by --on-entry (default tavern).
  Do not rely on stepping onto the neighbours: the entry FACING here is 0 and
  which compass direction that is was not worth pinning down, so a scripted
  "Up" may walk away from the cell you meant. Worse, it fails SILENTLY — the
  party just walks, the clock ticks a minute a step, and nothing opens, which
  reads exactly like "the handler is broken" and is not. That cost a full boot
  on the first attempt at this design. mk_temple_design.py puts its event on
  the entry cell for the same reason; do the same and the handler runs before
  any key is pressed.

  So: one boot per event.
      python3 tools/mk_inntav_design.py <dir> --current --on-entry tavern
      python3 tools/mk_inntav_design.py <dir> --current --on-entry inn
      python3 tools/mk_inntav_design.py <dir> --current --on-entry menu

  The three neighbour cells are kept anyway, so a human can walk between all
  three in one boot once they know which way the party is looking.

Read the HUD's clock to confirm a step actually happened. Do NOT trust the key
count — a modal eats keys silently, which is how two walk soaks were lost (see
mk_walktest_design.py). NOTE that the first key after entering play is often
DRAINED (same as the design picker's list build), so send a throwaway key first.

FIELD NOTES, from the handlers rather than from a shipped module:

  TAVERN, l4f9a (boot.c ~49689)
    ev[6]      picture; 0 takes the jt935 refresh path (no BIGPIC id needed)
    ev[7] b5   0x20 offer "drink"      b4 0x10 offer "fight"
          b3   0x08 pick rumors RANDOMLY -- LEFT CLEAR HERE ON PURPOSE, so the
               "listen" option round-robins and successive runs are comparable
          b2   0x04 set the -4946 redraw flag on the way out
    ev[8..15]  four design rumor string ids, LITTLE-ENDIAN pairs. The engine
               reads `(ev[9+2k] << 8) | ev[8+2k]`, i.e. low byte first.
    ev[16..17] a defined drink (l4eea + its strength accrual); 0 here, which
               takes the generic "orders a local drink." path
    ev[18]     chained event for "fight"; 0 -> "Everyone runs away..."
    ev[19]     chained event once the drink total reaches 60

  INN, l398a (boot.c ~97346)
    ev[6]      picture, as above
    ev[7] b2   0x04 redraw flag
    An empty event text (ev[4..5] == 0) makes it print "The party enters a
    local Inn." and go straight into l473e, the rest/inn services -- which is
    the observable we want, so the text is deliberately left at 0.

  MENU META, l5bde (boot.c ~47585)
    ev[4..5]   event text id, LITTLE-ENDIAN (the engine byte-swaps the short it
               reads off a 68k, so the file holds low byte first)
    ev[6]      picture
    ev[7] b0   treasure  b1 training  b2 shop  b3 INN  b4 TAVERN  b5 vault
               (Exit is always appended). Only inn+tavern are enabled here:
               shop and treasure are already covered by #36/#37, and enabling
               them only adds sub-screens to navigate back out of.
    ev[10..11] the rumor id handed to the tavern sub-event as its subrec[8..9]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dsn import Design, _walled_room, _hook              # noqa: E402
from geo import EVENT_SIZE                               # noqa: E402

ROOM = 9
ENTRY_ROW, ENTRY_COL = 4, 4

TAVERN_CELL = (4, 3)        # (col, row) — north of the party
INN_CELL = (4, 5)           # south
MENU_CELL = (5, 4)          # east

EV_TAVERN, EV_INN, EV_MENU = 0, 1, 2

TYPE_TAVERN, TYPE_INN, TYPE_MENU = 7, 29, 22

# ★ STRG IDS ARE 1-BASED. strg_write() stores the list 0-based, but jt232 reads
# index (id - 1) — so the id of STRINGS[i] is i + 1, and id 0 means "no string"
# everywhere in the event params. Getting this wrong is SILENT: the id resolves
# to an empty slot, jt232 leaves its buffer empty, l0b20 prints nothing, and the
# tavern's "TALK" option appears to do nothing at all. That is exactly what
# happened on the first run of this design (ids were written as the 0-based
# index), and it looks identical to a broken handler. Hence _sid() below —
# never hand-write an id.
STRINGS = [
    "A low common room, thick with smoke.",                          # id 1
    "RUMOR ONE: the bridge on the north road is out.",               # id 2
    "RUMOR TWO: the miller hoards silver in his cellar.",             # id 3
    "RUMOR THREE: nobody who camps by the standing stones wakes up.",  # id 4
    "RUMOR FOUR: the well behind the smithy is poisoned.",           # id 5
    "A painted board offers beds, board and drink.",                 # id 6
]


def _sid(text):
    """The 1-based STRG id of `text`. Raises rather than silently return 0."""
    return STRINGS.index(text) + 1


STR_RUMORS = tuple(_sid(s) for s in STRINGS if s.startswith("RUMOR "))
STR_MENU = _sid("A painted board offers beds, board and drink.")


def _le16(v):
    return v & 0xff, (v >> 8) & 0xff


def _tavern_event():
    ev = bytearray(EVENT_SIZE)
    ev[0] = TYPE_TAVERN
    ev[6] = 0                                   # jt935 refresh, no BIGPIC
    ev[7] = 0x20 | 0x10                         # drink + fight; bit3 CLEAR so
                                                # "listen" round-robins
    for k, sid in enumerate(STR_RUMORS):        # ev[8..15], little-endian pairs
        ev[8 + 2 * k], ev[9 + 2 * k] = _le16(sid)
    return bytes(ev)                            # [16..19] = 0: generic drink,
                                                # "everyone runs away" on fight


def _inn_event():
    ev = bytearray(EVENT_SIZE)
    ev[0] = TYPE_INN
    ev[6] = 0
    ev[7] = 0
    # ev[4..5] left 0 on purpose -> "The party enters a local Inn." + l473e.
    return bytes(ev)


def _menu_event():
    ev = bytearray(EVENT_SIZE)
    ev[0] = TYPE_MENU
    ev[4], ev[5] = _le16(STR_MENU)              # little-endian text id
    ev[6] = 0
    ev[7] = 0x08 | 0x10                         # inn + tavern only
    ev[10], ev[11] = _le16(STR_RUMORS[0])       # -> the tavern's subrec[8..9]
    return bytes(ev)


ON_ENTRY = {"tavern": EV_TAVERN, "inn": EV_INN, "menu": EV_MENU}


def inntav_design(name="INNTAV", on_entry="tavern"):
    a5 = _walled_room(w=ROOM, h=ROOM, entry=(ENTRY_ROW, ENTRY_COL), facing=0)
    a5.strg_write(STRINGS)

    a5.set_event(EV_TAVERN, _tavern_event())
    a5.set_event(EV_INN, _inn_event())
    a5.set_event(EV_MENU, _menu_event())

    _hook(a5, TAVERN_CELL[0], TAVERN_CELL[1], special=EV_TAVERN + 1)
    _hook(a5, INN_CELL[0], INN_CELL[1], special=EV_INN + 1)
    _hook(a5, MENU_CELL[0], MENU_CELL[1], special=EV_MENU + 1)

    # ...and on the cell the party lands on, so it runs with no keys at all.
    if on_entry is not None:
        _hook(a5, ENTRY_COL, ENTRY_ROW, special=ON_ENTRY[on_entry] + 1)

    d = Design(name, title="Inn / tavern / town-menu probe")
    d.xp = 15000
    d.start_area = 5                            # >= 5 -> dungeon mode
    d.start_entry = 1
    d.add_area(5, a5)
    return d


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    on_entry = "tavern"
    for i, a in enumerate(argv):
        if a == "--on-entry" and i + 1 < len(argv):
            on_entry = argv[i + 1]
    if on_entry not in ON_ENTRY:
        print("--on-entry must be one of: %s" % ", ".join(sorted(ON_ENTRY)))
        return 2
    d = inntav_design(on_entry=on_entry)
    folder = d.write(argv[0], make_current=("--current" in argv))
    g = d.areas[5]
    print("wrote", folder)
    print("  %dx%d room, party at (col %d, row %d)"
          % (ROOM, ROOM, ENTRY_COL, ENTRY_ROW))
    print("  ON THE ENTRY CELL: %s -- fires with no keys pressed" % on_entry.upper())
    for idx, cell, what in ((EV_TAVERN, TAVERN_CELL, "TAVERN  (l4f9a)"),
                            (EV_INN, INN_CELL, "INN     (l398a)"),
                            (EV_MENU, MENU_CELL, "MENU    (l5bde)")):
        info = g.event_info(idx)
        print("  ev %d  type %2d %-16s at (col %d, row %d)  ev[7]=0x%02x"
              % (idx, info["type"], what, cell[0], cell[1], info["flags"]))
    print("  drive: Up = tavern | Left Left Up Up = inn "
          "| Left Left Up Right Up = menu")
    print("  confirm every step by the HUD's col,row -- never by the key count")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
