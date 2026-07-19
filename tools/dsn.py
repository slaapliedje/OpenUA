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


def demo_design(name="GENAREA"):
    """A tiny self-contained playable DUNGEON design: one 8x8 walled room (a
    dungeon area, so the first-person view renders the generated walls), with a
    welcome message on the entry cell and a goblin ambush a step ahead."""
    W = H = 8
    g = Geo.blank(W, H)
    # Wall every border edge so the room reads as an enclosed chamber. Wall id 1
    # (high nibble) on the outward edge of each perimeter cell; edge 0=N 1=E 2=S 3=W.
    for c in range(W):
        for r in range(H):
            n = 0x10 if r == 0     else 0
            e = 0x10 if c == W - 1 else 0
            s = 0x10 if r == H - 1 else 0
            w = 0x10 if c == 0     else 0
            g.set_cell(c, r, walls=(n, e, s, w))
    g.set_entry_point(0, x=3, y=3, facing=0)             # centre, facing north
    g.strg_write(["", "You stand in a cramped stone chamber.",
                  "A goblin lunges from the shadows!"])
    g.set_message(0, text_ids=[2])                       # event 1 -> welcome text
    g.set_combat(1, [(1, 3)])                            # event 2 -> 3x monster 1
    # cell special = event index + 1
    c = g.cell(3, 3); g.set_cell(3, 3, walls=tuple(c[:4]), special=1)
    c = g.cell(4, 3); g.set_cell(4, 3, walls=tuple(c[:4]), special=2)

    d = Design(name, title="Generated Test Area")
    d.xp = 15000
    d.start_area = 5           # level >= 5 -> dungeon (first-person) mode
    d.start_entry = 1
    d.add_area(5, g)
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
    print("  GEO001.DAT :", d.areas[1].width, "x", d.areas[1].height, "area")
    return 0


if __name__ == "__main__":
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    sys.exit(main(sys.argv[1:]))
