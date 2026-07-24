#!/usr/bin/env python3
"""Assemble a playable FRUA design folder (`<NAME>.DSN`) from generated content.

A design is a folder of files the engine loads by name from the current-design
directory (`-31336`); the minimum for a *playable* one is:

  GAME001.DAT   the 388-byte settings record — title, starting XP/money, and
                (byte 48) the START AREA + (byte 49) START ENTRY that the engine
                reads at Play (boot.c ~19218: `jt127("GAME",1,...)`, `gh[48]`
                the level, `gh[49]` the 1-based entry).
  GEO0NN.DAT    one area per file; the start area's number NNN must equal
                GAME001.DAT byte 48 and the file must exist (else jt198 -> jt69
                fatal). Built by tools/geo.py.

Wiring rule: `design.start_area = N` and `design.add_area(N, geo)` write
`GEO<NNN>.DAT`; at Play the engine loads that GEO and places the party from its
entry-point table (see docs/geo-format.md). GAME001.DAT XP/money seed character
creation; they don't affect area loading.

Nothing here needs the engine or any copyrighted data — a fresh design is your
own content. `data/` is git-ignored; never commit a built design.
"""
import os
import struct

from geo import Geo, GeoError

GAME_SIZE = 388

# GAME001.DAT field offsets (little-endian), verified across TUTORIAL/HEIRS/GIANTS.
_OFF_TITLE   = 0     # C string, up to 31 bytes
_OFF_XP      = 32    # u32 starting experience
_OFF_PLAT    = 36    # u32 platinum
_OFF_GEMS    = 40    # u32 gems
_OFF_JEWEL   = 44    # u32 jewelry
_OFF_AREA    = 48    # u8  start area number  (-> GEO<NNN>.DAT)
_OFF_ENTRY   = 49    # u8  start entry index  (1-based)
_OFF_EQUIP   = 50    # u8  starting-equipment level


class Design:
    """A FRUA design under construction. Add areas, set the start area, write()."""

    def __init__(self, name, title=None):
        if not name.upper().endswith(".DSN"):
            name = name + ".DSN"
        self.name        = name
        self.title       = title if title is not None else name[:-4]
        self.xp          = 15000
        self.platinum    = 100
        self.gems        = 0
        self.jewelry     = 0
        self.equipment   = 2         # 0..? (Meager..Average); cosmetic for loading
        self.start_area  = 1
        self.start_entry = 1         # 1-based
        self.areas       = {}        # area number -> Geo

    def add_area(self, number, geo):
        if not isinstance(geo, Geo):
            raise TypeError("area must be a Geo")
        if not (1 <= number <= 999):
            raise GeoError("area number must be 1..999")
        self.areas[number] = geo
        return geo

    def game001(self):
        """Serialise GAME001.DAT (the 388-byte settings record)."""
        if self.start_area not in self.areas:
            raise GeoError("start_area %d has no GEO (call add_area first)"
                           % self.start_area)
        rec = bytearray(GAME_SIZE)
        t = self.title.encode("mac-roman", "replace")[:31]
        rec[_OFF_TITLE:_OFF_TITLE + len(t)] = t
        struct.pack_into("<I", rec, _OFF_XP,    self.xp    & 0xffffffff)
        struct.pack_into("<I", rec, _OFF_PLAT,  self.platinum & 0xffffffff)
        struct.pack_into("<I", rec, _OFF_GEMS,  self.gems  & 0xffffffff)
        struct.pack_into("<I", rec, _OFF_JEWEL, self.jewelry & 0xffffffff)
        rec[_OFF_AREA]  = self.start_area & 0xff
        rec[_OFF_ENTRY] = self.start_entry & 0xff
        rec[_OFF_EQUIP] = self.equipment & 0xff
        return bytes(rec)

    def write(self, base_dir, make_current=False):
        """Write `<base_dir>/<NAME>.DSN/` with GAME001.DAT + each GEO<NNN>.DAT.
        With make_current, also write `<base_dir>/start.dat` pointing at it."""
        if not self.areas:
            raise GeoError("design has no areas")
        folder = os.path.join(base_dir, self.name)
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, "GAME001.DAT"), "wb") as f:
            f.write(self.game001())
        for n, geo in sorted(self.areas.items()):
            with open(os.path.join(folder, "GEO%03d.DAT" % n), "wb") as f:
                f.write(geo.build())
        if make_current:
            self.set_current(base_dir)
        return folder

    def set_current(self, base_dir):
        """Point the game at this design (start.dat: 34-byte name + 1 flag)."""
        name = self.name.encode("mac-roman", "replace")
        data = name + b"\x00" * (34 - len(name)) + b"\x00"
        with open(os.path.join(base_dir, "start.dat"), "wb") as f:
            f.write(data)


def _walled_room(w=8, h=8, entry=(3, 3), facing=0):
    """An enclosed dungeon chamber: wall id 1 on every perimeter cell's outward
    edge (edge 0=N 1=E 2=S 3=W), party entering at `entry` facing `facing`."""
    g = Geo.blank(w, h)
    for c in range(w):
        for r in range(h):
            g.set_cell(c, r, walls=(0x10 if r == 0 else 0,
                                    0x10 if c == w - 1 else 0,
                                    0x10 if r == h - 1 else 0,
                                    0x10 if c == 0 else 0))
    g.set_entry_point(0, x=entry[0], y=entry[1], facing=facing)
    return g


def _hook(g, col, row, special):
    """Point cell (col,row) at an event (special = event index + 1), keeping its
    walls."""
    walls = tuple(g.cell(col, row)[:4])
    g.set_cell(col, row, walls=walls, special=special)


def demo_design(name="GENAREA"):
    """A self-contained playable TWO-AREA dungeon module, so the first-person
    view renders generated walls AND a Passage links area 5 -> area 6:

      area 5 (start): welcome message on entry, a goblin combat, and a passage
                      one cell east that transfers to area 6.
      area 6:         a distinct 'second chamber' message on its entry cell.
    """
    a5 = _walled_room(entry=(3, 3), facing=0)
    a5.strg_write(["", "You stand in a cramped stone chamber.",
                   "A goblin lunges from the shadows!"])
    a5.set_message(0, text_ids=[2])                      # event 0 -> welcome
    a5.set_combat(1, [(1, 3)])                           # event 1 -> 3x monster 1
    a5.set_passage(2, dest_area=6, x=3, y=3, facing=0)   # event 2 -> to area 6
    _hook(a5, 3, 3, special=1)                           # entry cell -> message
    _hook(a5, 3, 4, special=2)                           # south cell -> combat
    _hook(a5, 4, 3, special=3)                           # east cell  -> passage

    a6 = _walled_room(entry=(3, 3), facing=0)
    a6.strg_write(["", "You have entered the second chamber."])
    a6.set_message(0, text_ids=[2])                      # event 0 -> welcome
    _hook(a6, 3, 3, special=1)                           # entry cell -> message

    d = Design(name, title="Generated Test Area")
    d.xp = 15000
    d.start_area = 5           # level >= 5 -> dungeon (first-person) mode
    d.start_entry = 1
    d.add_area(5, a5)
    d.add_area(6, a6)
    return d


def main(argv):
    if not argv:
        print(__doc__)
        print("usage: dsn.py <base_dir> [design_name]   # writes a demo design")
        return 2
    base = argv[0]
    name = argv[1] if len(argv) > 1 else "GENAREA"
    d = demo_design(name)
    folder = d.write(base, make_current=("--current" in argv))
    print("wrote", folder)
    print("  GAME001.DAT: title=%r start_area=%d entry=%d xp=%d"
          % (d.title, d.start_area, d.start_entry, d.xp))
    for n in sorted(d.areas):
        print("  GEO%03d.DAT :" % n, d.areas[n].width, "x", d.areas[n].height, "area")
    return 0


if __name__ == "__main__":
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    sys.exit(main(sys.argv[1:]))
