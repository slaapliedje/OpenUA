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


# Event types, named from l709e's dispatch handlers (src/engine/boot.c).
# Byte 0 of a 20-byte ENCR record selects one of these.
EVENT_TYPES = {
    0:  "(empty / chain-only)",
    1:  "Combat",
    2:  "Message / Text",
    3:  "Give-Take treasure",
    4:  "Affect-party effect",
    5:  "Stairs / level change",
    6:  "Training Hall",
    7:  "Tavern",
    8:  "Shop / merchant",
    9:  "Give treasure / Temple",
    10: "Encounter",
    11: "Passage / level change",
    12: "Scripted movement",
    13: "HP percentage",
    14: "Message (conditional)",
    15: "Conditional event",
    16: "(handler l6020)",
    17: "Play sounds",
    18: "Question outcome / branch",
    19: "Question outcome / branch",
    20: "Question outcome / branch",
    21: "Encounter",
    22: "Menu meta-event",
    23: "(chain-only, via ev[8])",
    24: "Vault",
    25: "Give-Take treasure",
    26: "Award experience",
    27: "Pass time",
    29: "Inn",
    32: "Select member by class",
    33: "Combat (fixed)",
    34: "Passage / level change",
    35: "Conditional-variable branch",
    36: "Yes/No Question",
    37: "Set standard rumors",
    38: "Set quest-flag",
}

# Condition types = ev[1] >> 3 (l694e). Decides whether the event fires; the
# parameter is ev[2]. Types 9..16 exist in l694e but aren't mapped here yet.
EVENT_CONDITIONS = {
    0: "always",
    1: "design-flag set (rec[param+69] != 0)",
    2: "design-flag clear (rec[param+69] == 0)",
    3: "party NOT in class band 6..20",
    4: "party in class band 6..20",
    5: "percent chance (roll <= param)",
    6: "rec[25] != 0",
    7: "rec[25] == 0",
    8: "party facing (param = N/E/S/W bitmask)",
}


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

    def event_info(self, idx):
        """Decode an event's common header (shared by every type; see
        docs/geo-format.md). Returns None for an all-zero (empty) slot."""
        ev = self.event(idx)
        if not any(ev):
            return None
        cond_type = ev[1] >> 3
        return {
            "type":       ev[0],
            "name":       EVENT_TYPES.get(ev[0], "(unmapped %d)" % ev[0]),
            "once_only":  bool(ev[1] & 0x01),
            "cond_type":  cond_type,
            "cond_name":  EVENT_CONDITIONS.get(cond_type, "(cond %d)" % cond_type),
            "cond_param": ev[2],   # flag index / percent / facing mask, per cond
            "chain":      ev[3],   # auto next-event index (0 = none)
            "flags":      ev[7],   # branch-control flag bits
        }

    def set_event_header(self, idx, type, cond_type=0, cond_param=0,
                         chain=0, once_only=False):
        """Write the common header of event `idx` (leaves param bytes intact)."""
        o = idx * EVENT_SIZE
        self.encr[o + 0] = type & 0xff
        self.encr[o + 1] = ((cond_type & 0x1f) << 3) | (1 if once_only else 0)
        self.encr[o + 2] = cond_param & 0xff
        self.encr[o + 3] = chain & 0xff

    # ---- Combat events (type 1 / 33) — l159a ----
    def combat(self, idx):
        """Decode a Combat event (type 1 or 33). Returns a dict with the monster
        `groups` [(monster_id, count), ...], the descriptive `text_id` (into the
        area STRG table), and the `picture` id. Raises if the event isn't combat.
        See docs/geo-format.md for the byte layout."""
        ev = self.event(idx)
        if ev[0] not in (1, 33):
            raise GeoError("event %d is type %d, not Combat (1/33)" % (idx, ev[0]))
        groups = []
        for s in range(6):
            count = ev[8 + s * 2] & 0x1f          # low 5 bits = count
            mid   = ev[9 + s * 2]                  # monster id (MONST index)
            if count and mid:
                groups.append((mid, count))
        return {
            "type":    ev[0],
            "random":  ev[0] == 33,   # type 1 = all groups; 33 = one picked at random
            "groups":  groups,
            "text_id": (ev[4] << 8) | ev[5],       # big-endian STRG index (0=none)
            "picture": ev[6],                       # 0=none, <240 sprite PIC, >=240 bigpic
        }

    def set_combat(self, idx, groups, text_id=0, picture=0, random=False,
                   cond_type=0, cond_param=0, chain=0, once_only=False):
        """Build a Combat event from a list of (monster_id, count) groups (max 6;
        count 1..31, id 1..255). Writes the header + text/picture + monster slots
        with config-flag bits left zero (normal surprise, near range)."""
        if len(groups) > 6:
            raise GeoError("at most 6 monster groups")
        o = idx * EVENT_SIZE
        rec = bytearray(EVENT_SIZE)
        rec[0] = 33 if random else 1
        rec[1] = ((cond_type & 0x1f) << 3) | (1 if once_only else 0)
        rec[2] = cond_param & 0xff
        rec[3] = chain & 0xff
        rec[4] = (text_id >> 8) & 0xff
        rec[5] = text_id & 0xff
        rec[6] = picture & 0xff
        for s, (mid, count) in enumerate(groups):
            if not (1 <= count <= 31):
                raise GeoError("group %d count %d out of 1..31" % (s, count))
            if not (1 <= mid <= 255):
                raise GeoError("group %d monster id %d out of 1..255" % (s, mid))
            rec[8 + s * 2] = count & 0x1f          # high 3 bits (flags) left 0
            rec[9 + s * 2] = mid & 0xff
        self.encr[o:o + EVENT_SIZE] = rec

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
    events = [g.event_info(i) for i in range(MAX_EVENTS)]
    events = [e for e in events if e]
    print("events:", len(events))
    for i, e in enumerate(events[:12]):
        print("  [%2d] type %2d %-24s cond=%-8s param=%3d chain=%3d%s"
              % (i, e["type"], e["name"], e["cond_name"].split(" (")[0],
                 e["cond_param"], e["chain"], " once" if e["once_only"] else ""))
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main(sys.argv[1:]))
