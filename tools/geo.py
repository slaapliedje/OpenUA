#!/usr/bin/env python3
"""Read / write / build FRUA GEO area files (GEOnnn.DAT).

A GEO file is the on-disk form of one adventure area (a level map): its
dimensions, wall layout, per-cell event hooks, the event table, and the area
string table. The engine loads it in jt198 -> l7226 (src/engine/boot.c) into the
design-state buffer (A5 global -12300); this module mirrors that parser exactly,
so `Geo.parse(build(g)) == g` round-trips byte-for-byte.

See docs/geo-format.md for the field-by-field layout. The container is an
IFF-ish FORM/AMOD with four fixed-size chunks, and the whole file is always
exactly GEO_SIZE (12962) bytes:

    FORM <filesize-8>
      AMOD <filesize-16>
        HDR  0x122 (290)   -> design-state[0..289]   (header: dims, entries, zones)
        MAP  0xd80 (3456)  -> design-state[290..]     (up to 576 cells x 6 bytes)
        ENCR 0x7d0 (2000)  -> event table (-13038)     (100 events x 20 bytes)
        STRG 0x1c00 (7168) -> string table (-13034)

All multi-byte scalars in the container framing and the HDR are BIG-ENDIAN (the
file was authored on a 68k Mac). This module is host-side tooling; no engine or
copyrighted data is required to build a fresh area.
"""
import struct

GEO_SIZE   = 12962
HDR_SIZE   = 0x122     # 290
MAP_SIZE   = 0xd80     # 3456  (MAX_CELLS * CELL_SIZE)
ENCR_SIZE  = 0x7d0     # 2000  (MAX_EVENTS * EVENT_SIZE)
STRG_SIZE  = 0x1c00    # 7168
AMOD_SIZE  = HDR_SIZE + 8 + MAP_SIZE + 8 + ENCR_SIZE + 8 + STRG_SIZE + 8  # 12946
FORM_SIZE  = AMOD_SIZE + 8                                                 # 12954

CELL_SIZE  = 6
MAX_CELLS  = MAP_SIZE // CELL_SIZE    # 576  (the design-state cell capacity)
EVENT_SIZE = 20
MAX_EVENTS = ENCR_SIZE // EVENT_SIZE  # 100

VERSION_MIN = 100
VERSION_MAX = 106     # l7226 rejects a HDR whose version word is outside this


class GeoError(ValueError):
    pass


class Geo:
    """A parsed GEO area. `.hdr`/`.map`/`.encr`/`.strg` are mutable bytearrays;
    the typed accessors below decode/encode fields within them."""

    def __init__(self, hdr, map_, encr, strg):
        self.hdr  = bytearray(hdr)
        self.map  = bytearray(map_)
        self.encr = bytearray(encr)
        self.strg = bytearray(strg)
        if len(self.hdr)  != HDR_SIZE:  raise GeoError("HDR wrong size")
        if len(self.map)  != MAP_SIZE:  raise GeoError("MAP wrong size")
        if len(self.encr) != ENCR_SIZE: raise GeoError("ENCR wrong size")
        if len(self.strg) != STRG_SIZE: raise GeoError("STRG wrong size")

    # ---- HDR fields (design-state[0..289]) ----
    @property
    def version(self):
        return struct.unpack_from(">H", self.hdr, 0)[0]

    @version.setter
    def version(self, v):
        struct.pack_into(">H", self.hdr, 0, v)

    @property
    def width(self):   return self.hdr[2]      # ds[2] — columns
    @width.setter
    def width(self, v): self.hdr[2] = v

    @property
    def height(self):   return self.hdr[3]     # ds[3] — rows per column
    @height.setter
    def height(self, v): self.hdr[3] = v

    def entry_point(self, idx):
        """Party start for entry `idx` (the Game-Settings 'entry point' value):
        the 4-byte record based at ds[idx*4]; returns (x, y, facing).
        Mirrors l0bbc: st=ds+idx*4; X=st[15], Y=st[14], facing=st[16]&7."""
        base = idx * 4
        return (self.hdr[base + 15], self.hdr[base + 14], self.hdr[base + 16] & 7)

    def set_entry_point(self, idx, x, y, facing):
        base = idx * 4
        self.hdr[base + 14] = y & 0xff
        self.hdr[base + 15] = x & 0xff
        self.hdr[base + 16] = (self.hdr[base + 16] & ~7) | (facing & 7)

    def zone_rule(self, zone):
        """(event, no_rest) for a zone 0..7 — ds[48+zone*4] / ds[49+zone*4] bit7."""
        base = 48 + zone * 4
        return (self.hdr[base], bool(self.hdr[base + 1] & 0x80))

    # ---- MAP cells (design-state[290..]) ----
    def _cell_off(self, col, row):
        # jt201/jt212: idx = height*col + row  (row within a column, then column)
        if not (0 <= col < self.width and 0 <= row < self.height):
            raise IndexError("cell (%d,%d) out of %dx%d" % (col, row, self.width, self.height))
        return (self.height * col + row) * CELL_SIZE

    def cell(self, col, row):
        o = self._cell_off(col, row)
        return bytes(self.map[o:o + CELL_SIZE])

    def wall(self, col, row, direction):
        """Wall id (high nibble) for a direction 0..3 (edge/2). Low nibble is the
        wall attribute (door/secret); use raw cell() bytes for that."""
        o = self._cell_off(col, row)
        return (self.map[o + direction] >> 4) & 15

    def set_cell(self, col, row, walls=(0, 0, 0, 0), special=0, zone=0, flags=0):
        o = self._cell_off(col, row)
        for d in range(4):
            self.map[o + d] = walls[d] & 0xff
        self.map[o + 4] = special & 0xff
        self.map[o + 5] = ((zone & 7) << 2) | (flags & ~0x1c & 0xff)

    def cell_special(self, col, row):
        return self.map[self._cell_off(col, row) + 4]

    def cell_zone(self, col, row):
        return (self.map[self._cell_off(col, row) + 5] >> 2) & 7

    # ---- ENCR events ----
    def event(self, idx):
        return bytes(self.encr[idx * EVENT_SIZE:(idx + 1) * EVENT_SIZE])

    def event_type(self, idx):
        return self.encr[idx * EVENT_SIZE]

    def set_event(self, idx, data):
        data = bytes(data)
        if len(data) != EVENT_SIZE:
            raise GeoError("event must be %d bytes" % EVENT_SIZE)
        self.encr[idx * EVENT_SIZE:(idx + 1) * EVENT_SIZE] = data

    # ---- container round-trip ----
    @classmethod
    def parse(cls, data):
        """Parse a GEO file. Mirrors l7226's structural checks."""
        if len(data) not in (GEO_SIZE, GEO_SIZE + 1):  # engine pads odd sizes
            raise GeoError("GEO must be %d bytes, got %d" % (GEO_SIZE, len(data)))
        if data[0:4] not in (b"FORM", b"CAT ", b"LIST"):
            raise GeoError("not an IFF container (%r)" % data[0:4])
        cur = _Walker(data)
        cur.expect(b"FORM")  # FORM + size
        # AMOD is a SIZED sub-chunk here, not a bare IFF formType ("no formType
        # — FRUA quirk"): FORM<sz> AMOD<sz> { HDR MAP ENCR STRG }.
        cur.expect(b"AMOD")  # AMOD + size
        hdr  = cur.chunk(b"HDR ", HDR_SIZE)
        map_ = cur.chunk(b"MAP ", MAP_SIZE)
        encr = cur.chunk(b"ENCR", ENCR_SIZE)
        strg = cur.chunk(b"STRG", STRG_SIZE)
        g = cls(hdr, map_, encr, strg)
        if not (VERSION_MIN <= g.version <= VERSION_MAX):
            raise GeoError("HDR version %d outside %d..%d"
                           % (g.version, VERSION_MIN, VERSION_MAX))
        dims = g.width * g.height
        if not (0 < dims <= MAX_CELLS):
            raise GeoError("dims %d x %d = %d outside 1..%d"
                           % (g.width, g.height, dims, MAX_CELLS))
        return g

    def build(self):
        """Serialise back to the fixed 12962-byte container."""
        out = bytearray()
        out += b"FORM" + struct.pack(">I", FORM_SIZE)
        out += b"AMOD" + struct.pack(">I", AMOD_SIZE)  # sized sub-chunk (FRUA quirk)
        out += b"HDR " + struct.pack(">I", HDR_SIZE)  + self.hdr
        out += b"MAP " + struct.pack(">I", MAP_SIZE)  + self.map
        out += b"ENCR" + struct.pack(">I", ENCR_SIZE) + self.encr
        out += b"STRG" + struct.pack(">I", STRG_SIZE) + self.strg
        assert len(out) == GEO_SIZE, len(out)
        return bytes(out)

    @classmethod
    def blank(cls, width, height, version=VERSION_MAX):
        """A minimal empty area: given dimensions, entry 0 at (0,0) facing 0, all
        zones rest-allowed, no walls/events, zero string table (the engine
        re-seeds an all-NUL STRG via l4db4/l4e3a on load)."""
        if not (0 < width * height <= MAX_CELLS):
            raise GeoError("dims out of range")
        g = cls(bytes(HDR_SIZE), bytes(MAP_SIZE), bytes(ENCR_SIZE), bytes(STRG_SIZE))
        g.version = version
        g.width = width
        g.height = height
        g.set_entry_point(0, 0, 0, 0)
        return g


class _Walker:
    def __init__(self, data):
        self.d = data
        self.o = 0

    def read_tag(self):
        t = self.d[self.o:self.o + 4]
        self.o += 4
        return t

    def read_u32(self):
        v = struct.unpack_from(">I", self.d, self.o)[0]
        self.o += 4
        return v

    def expect(self, tag):
        t = self.read_tag()
        if t != tag:
            raise GeoError("expected %r, got %r" % (tag, t))
        self.read_u32()  # size word (framing only)

    def chunk(self, tag, size):
        t = self.read_tag()
        if t != tag:
            raise GeoError("expected chunk %r, got %r" % (tag, t))
        sz = self.read_u32()
        if sz != size:
            raise GeoError("%r size %d != %d" % (tag, sz, size))
        body = self.d[self.o:self.o + size]
        self.o += size + (size & 1)   # skip the odd-byte pad
        return body


def main(argv):
    import sys
    if not argv:
        print(__doc__)
        return 2
    g = Geo.parse(open(argv[0], "rb").read())
    print("GEO %s: version=%d  %dx%d = %d cells"
          % (argv[0], g.version, g.width, g.height, g.width * g.height))
    print("entry 0 (x,y,facing):", g.entry_point(0))
    specials = [(c, r, g.cell_special(c, r))
                for c in range(g.width) for r in range(g.height)
                if g.cell_special(c, r)]
    print("cells with an event hook:", len(specials), specials[:8])
    events = [(i, g.event_type(i)) for i in range(MAX_EVENTS) if any(g.event(i))]
    print("events:", len(events), events[:8])
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main(sys.argv[1:]))
