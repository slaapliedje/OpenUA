#!/usr/bin/env python3
"""gbgeo.py — read Gold Box (ECL-era) dungeon maps and re-express them as FRUA areas.

The first building block of ADR-0023: the script-driven Gold Box games keep their
maps in a four-plane cell layout that FRUA's own GEO files do not use, so before
the engine can walk one of those maps something has to translate the cells. This
module is that translation, host-side, and it can emit an ordinary FRUA design so
the UNMODIFIED engine walks the result.

The source layout ("Gen1", Pool of Radiance through Death Knights of Krynn; The
Dark Queen of Krynn keeps the same planes and only lifts the 16x16 limit):

    plane 0   (North << 4) | East      4-bit wall SLOT per edge, 0 = open
    plane 1   (South << 4) | West
    plane 2   backdrop / zone byte
    plane 3   door flags, 2 bits per edge: N = bits 0-1, E = 2-3, S = 4-5, W = 6-7
              0 = plain wall, non-zero = a door/gateway the party can step through

Cells are row-major, row 0 northernmost. Source: docs/engine/research/
geo-map-format-cok.md (the RESOLVED block) and the Greybox loader it describes.

FRUA's edge byte is `passability << 4 | texture` (docs/geo-format.md) — the direct
descendant of the same scheme, with the door state folded into the high nibble
instead of a separate plane. So a Gen1 edge maps onto one FRUA byte:

    slot == 0                    -> 0x00           open, nothing drawn
    slot != 0, door flag == 0    -> 0xE0 | slot    solid wall, textured
    slot != 0, door flag != 0    -> 0x00 | slot    passable, textured (a door)

NOT YET CARRIED OVER, deliberately, and each is a known loss rather than an
oversight: locked / wizard-locked door states (the 2-bit flag's values 2 and 3
are not pinned down — Greybox calls 2 "gateway/special" and does not name 3),
the backdrop/zone plane, and everything a map's ECL script does. Wall TEXTURES
come out as FRUA's stock wall groups, not the source game's own art.

Containers handled:

    GLIB/HLIB 'DATA' library   .GLB as shipped on the Atari ST / Amiga / Mac
                               (big-endian GLIB) and DOS Gen2 (little-endian
                               HLIB). Member 0 is an id table, the rest are maps.
    raw Gen1 block             1024 bytes of planes, or 1026 with the DOS DAX
                               block's little-endian length word in front.

    python3 tools/gbgeo.py GEO.GLB                    # list the maps
    python3 tools/gbgeo.py GEO.GLB --ascii 1          # draw one, north up
    python3 tools/gbgeo.py GEO.GLB --design data/work/gamedata --current
    python3 tools/gbgeo.py GEO.GLB --design OUT --ecl ECL.GLB --start 4
                                                      # ...with each area's script

No game data is needed to use or test the module (tests/test_gbgeo.py builds
its own maps); pointing it at a real file is the user's own copy.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

GEN1_SIDE  = 16
GEN1_CELLS = GEN1_SIDE * GEN1_SIDE          # 256
GEN1_BYTES = 4 * GEN1_CELLS                 # 1024

# FRUA edge-byte high nibbles (docs/geo-format.md "Wall bytes").
FRUA_SOLID = 14

N, E, S, W = 0, 1, 2, 3
DIR_NAMES = ("N", "E", "S", "W")
# (dx, dy) with y growing SOUTH, as the source rows do.
DIR_STEP = ((0, -1), (1, 0), (0, 1), (-1, 0))
# FRUA's 8-point facing code for each of the four directions.
FRUA_FACING = (0, 2, 4, 6)


class GbGeoError(ValueError):
    pass


class GbMap:
    """One dungeon map as per-cell edge slots and door flags.

    `slots[y][x]` and `doors[y][x]` are 4-tuples in N, E, S, W order; `backdrop`
    is the raw plane-2 byte. (x, y) has x growing east and y growing south."""

    def __init__(self, width, height, slots, doors, backdrop, area_id=None):
        self.width = width
        self.height = height
        self.slots = slots
        self.doors = doors
        self.backdrop = backdrop
        self.area_id = area_id

    # ---- decoding ---------------------------------------------------------
    @classmethod
    def from_planes(cls, planes, width, height, area_id=None):
        """`planes` is 4 * width * height bytes: the four planes back to back."""
        n = width * height
        if len(planes) != 4 * n:
            raise GbGeoError("planes are %d bytes, want %d for %dx%d"
                             % (len(planes), 4 * n, width, height))
        p0, p1, p2, p3 = (planes[i * n:(i + 1) * n] for i in range(4))
        slots, doors, back = [], [], []
        for y in range(height):
            srow, drow, brow = [], [], []
            for x in range(width):
                i = y * width + x
                srow.append((p0[i] >> 4, p0[i] & 15, p1[i] >> 4, p1[i] & 15))
                d = p3[i]
                drow.append((d & 3, (d >> 2) & 3, (d >> 4) & 3, (d >> 6) & 3))
                brow.append(p2[i])
            slots.append(srow)
            doors.append(drow)
            back.append(brow)
        return cls(width, height, slots, doors, back, area_id)

    @classmethod
    def from_gen1_block(cls, raw, area_id=None):
        """A Gen1 16x16 block: 1024 plane bytes, or 1026 with DOS's length word."""
        if len(raw) == GEN1_BYTES + 2:
            if struct.unpack_from("<H", raw, 0)[0] != GEN1_BYTES:
                raise GbGeoError("1026-byte block without the 0x0400 length word")
            raw = raw[2:]
        if len(raw) != GEN1_BYTES:
            raise GbGeoError("Gen1 block is %d bytes, want 1024 or 1026" % len(raw))
        return cls.from_planes(raw, GEN1_SIDE, GEN1_SIDE, area_id)

    # ---- queries ----------------------------------------------------------
    def passable(self, x, y, d):
        """Can the party leave (x, y) through edge `d`?"""
        return self.slots[y][x][d] == 0 or self.doors[y][x][d] != 0

    def edge_symmetry(self):
        """(agreeing, total) over shared interior edges: a wall between two cells
        is stored on both, so E of a cell should equal W of its east neighbour
        and S equal N of the one below. The discriminator that settled the plane
        layout (62-91% on real maps against 0-13% for any wrong reading) — a
        decoder pointed at the wrong bytes scores near zero here."""
        agree = total = 0
        for y in range(self.height):
            for x in range(self.width):
                if x + 1 < self.width:
                    total += 1
                    agree += self.slots[y][x][E] == self.slots[y][x + 1][W]
                if y + 1 < self.height:
                    total += 1
                    agree += self.slots[y][x][S] == self.slots[y + 1][x][N]
        return agree, total

    def find_entry(self):
        """A place to stand: the first cell (reading order) with an exit, and
        the direction of that exit. None if the map is sealed solid."""
        for y in range(self.height):
            for x in range(self.width):
                for d in (N, E, S, W):
                    nx, ny = x + DIR_STEP[d][0], y + DIR_STEP[d][1]
                    if (0 <= nx < self.width and 0 <= ny < self.height
                            and self.passable(x, y, d)):
                        return x, y, d
        return None

    def ascii(self, mark=None):
        """Draw the map north-up. Walls are '-' and '|', doors '=' and ':'.
        Each cell's own N and W edges are drawn, plus the outer S and E."""
        out = []
        for y in range(self.height):
            top, mid = "", ""
            for x in range(self.width):
                top += "+" + self._edge_glyph(x, y, N, "--", "==")
                here = "@ " if mark == (x, y) else "  "
                mid += self._edge_glyph(x, y, W, "|", ":") + here
            out.append(top + "+")
            out.append(mid + self._edge_glyph(self.width - 1, y, E, "|", ":"))
        out.append("".join("+" + self._edge_glyph(x, self.height - 1, S, "--", "==")
                           for x in range(self.width)) + "+")
        return "\n".join(out)

    def _edge_glyph(self, x, y, d, wall, door):
        if self.slots[y][x][d] == 0:
            return " " * len(wall)
        return door if self.doors[y][x][d] else wall

    # ---- FRUA -------------------------------------------------------------
    def frua_edge(self, x, y, d):
        """The FRUA edge byte for one edge — see the module docstring."""
        slot = self.slots[y][x][d]
        if slot == 0:
            return 0x00
        if self.doors[y][x][d]:
            return slot
        return (FRUA_SOLID << 4) | slot

    def to_frua(self, entry=None):
        """Build a tools/geo.Geo area holding this map, with no events."""
        from geo import Geo, WIDE_MAX_CELLS, GeoError
        from dsn import ART_BLOCK

        if self.width * self.height > WIDE_MAX_CELLS:
            # Over 576 cells the result is a WIDE area (tools/geo.py), which
            # only OpenUA reads — 13 of Dark Queen's 20 maps need it. 784 is
            # that game's largest map and the engine's GEO_MAX_CELLS.
            raise GbGeoError("%dx%d = %d cells; the engine's map holds %d"
                             % (self.width, self.height,
                                self.width * self.height, WIDE_MAX_CELLS))
        # ★ THE AXES AND THE EDGE ORDER, measured in the engine rather than
        # reasoned from the docs — which disagree with each other (dsn.py says
        # the four edge bytes are N,E,S,W; docs/geo-format.md says W,S,E,N).
        # Two wrong cuts came first, and each LOOKED fine in a screenshot:
        #   1. x on geo.py's `col`: the party walked, but the map was transposed.
        #   2. col=y/row=x with W,S,E,N bytes: the party walked east straight
        #      through a wall the source marks solid (reached x=6; the wall is
        #      after x=4).
        # What holds, checked by walking Curse of the Azure Bonds' first map on
        # the Falcon build (east from 0,0 stops at 4,0 exactly as the source
        # says, and south from there runs on as it should):
        #   * geo.py's `row` is the east-west axis and `col` the north-south
        #     one (its #104 note: "col here is the engine's row"), so a compass
        #     cell (x east, y south) is set_cell(col=y, row=x) and geo.py's
        #     `width` bounds y;
        #   * the engine reads edge byte `facing >> 1` when the party moves, and
        #     facing is 0=N 2=E 4=S 6=W, so the bytes are N, E, S, W.
        # A square room cannot tell these apart — any permutation still encloses
        # it — which is how the disagreement survived. Use an ASYMMETRIC map.
        try:
            g = Geo.blank(self.height, self.width)
        except GeoError as e:                    # pragma: no cover - guarded above
            raise GbGeoError(str(e))
        for y in range(self.height):
            for x in range(self.width):
                g.set_cell(col=y, row=x,
                           walls=tuple(self.frua_edge(x, y, d)
                                       for d in (N, E, S, W)))
        g.hdr[4:14] = ART_BLOCK                  # stock wall art; without it nothing draws
        if entry is None:
            entry = self.find_entry() or (0, 0, N)
        ex, ey, ed = entry
        g.set_entry_point(0, row=ex, col=ey, facing=FRUA_FACING[ed])
        return g


# ---- containers -----------------------------------------------------------

def read_library(data):
    """Split a GLIB/HLIB 'DATA' library into (endian, [member bytes...])."""
    if len(data) < 20 or data[:4] not in (b"GLIB", b"HLIB"):
        raise GbGeoError("not a GLIB/HLIB library")
    end = ">" if data[:4] == b"GLIB" else "<"
    size, count = struct.unpack_from(end + "IH", data, 4)
    if data[12:16] != b"DATA":
        raise GbGeoError("library content is %r, not DATA" % data[12:16])
    if size != len(data):
        raise GbGeoError("library says %d bytes, file is %d" % (size, len(data)))
    offs = struct.unpack_from(end + "%dI" % (count + 1), data, 16)
    if any(offs[i] > offs[i + 1] for i in range(count)) or offs[count] > len(data):
        raise GbGeoError("library offset table is not monotonic")
    return end, [data[offs[i]:offs[i + 1]] for i in range(count)]


def read_id_table(member0, end):
    """Member 0: u16 count, then count x {u16 area id, u16 member index}."""
    (count,) = struct.unpack_from(end + "H", member0, 0)
    if len(member0) < 2 + 4 * count:
        raise GbGeoError("id table truncated")
    return [struct.unpack_from(end + "HH", member0, 2 + 4 * i) for i in range(count)]


def load_maps(data):
    """Every map in a file, as GbMap objects in file order.

    Accepts a GLIB/HLIB GEO library or a single raw Gen1 block. Library members
    are taken as headerless 16x16 Gen1 planes when they are exactly 1024 bytes
    (the Atari ST / Amiga form); a member with Dark Queen's 8-byte
    height/width header is decoded at its own size."""
    if data[:4] in (b"GLIB", b"HLIB"):
        end, members = read_library(data)
        maps = []
        for area_id, idx in read_id_table(members[0], end):
            if not 0 < idx < len(members):
                raise GbGeoError("id table points at member %d of %d"
                                 % (idx, len(members)))
            m = members[idx]
            if len(m) == GEN1_BYTES:
                maps.append(GbMap.from_gen1_block(m, area_id))
            elif len(m) >= 8 and len(m) == 8 + 4 * m[0] * m[1]:
                maps.append(GbMap.from_planes(m[8:], width=m[1], height=m[0],
                                              area_id=area_id))
            else:
                raise GbGeoError("member %d (%d bytes) is not a map this reads"
                                 % (idx, len(m)))
        return maps
    return [GbMap.from_gen1_block(data)]


# Dialect numbers as src/engine/ecl.h has them (ECL_GEN1, ECL_GEN1_DOK, ECL_GEN2).
SCRIPT_DIALECT_ID = {"gen1": 0, "gen1-dok": 1, "gen2": 2}


def map_descriptor(m):
    """GBDnnn.DAT — what makes area slot nnn a SCRIPT AREA (src/engine/boot.c,
    "GOLD BOX SCRIPT AREAS"): the map's cell-descriptor plane, which the
    scripts dispatch on and the engine's own cells have no room for, with the
    map's size and the game's own number for it."""
    if not (m.width < 256 and m.height < 256 and 0 < (m.area_id or 0) < 256):
        raise GbGeoError("map does not fit a descriptor header")
    desc = bytes(m.backdrop[y][x] for y in range(m.height) for x in range(m.width))
    return b"GBD1" + bytes([m.width, m.height, m.area_id, 0]) + desc


def script_file(block):
    """ECLnnn.DAT, nnn = the game's own script number: the dialect and the
    block exactly as the game ships it. `block` is a tools/ecl.Block."""
    return b"GBS2" + bytes([SCRIPT_DIALECT_ID[block.dialect.name], 0, 0, 0]) + block.raw


def map_index(maps, first_area=5):
    """GBMAP.DAT — the game's map numbers against the design's area slots, so a
    script's "load map N" can find it. Scripts and maps are numbered separately
    in these games (Dark Queen has 47 scripts over 20 maps)."""
    return b"GBM1" + bytes([len(maps)]) + b"".join(
        bytes([m.area_id, first_area + i]) for i, m in enumerate(maps))


def write_scripts(folder, maps, blocks, first_area=5):
    """Write the descriptor of every map, every script, and the index.
    Returns (maps written, scripts written)."""
    for i, m in enumerate(maps):
        with open(os.path.join(folder, "GBD%03d.DAT" % (first_area + i)), "wb") as f:
            f.write(map_descriptor(m))
    for b in blocks:
        data = script_file(b)
        if len(data) > 16384:                      # GBS_MAX in the engine
            raise GbGeoError("script %s is %d bytes; the engine reads 16384"
                             % (b.block_id, len(data)))
        if not 0 < b.block_id < 256:
            raise GbGeoError("script number %s does not fit a file name" % b.block_id)
        with open(os.path.join(folder, "ECL%03d.DAT" % b.block_id), "wb") as f:
            f.write(data)
    with open(os.path.join(folder, "GBMAP.DAT"), "wb") as f:
        f.write(map_index(maps, first_area))
    return len(maps), len(blocks)


def build_design(maps, name="GOLDBOX", title="Imported Gold Box maps", start=0):
    """A FRUA design with one dungeon area per map (areas 5, 6, ...), no events."""
    from dsn import Design

    if not maps:
        raise GbGeoError("no maps")
    if len(maps) > 36:
        raise GbGeoError("%d maps; a FRUA design has 36 dungeon areas" % len(maps))
    d = Design(name, title=title)
    d.xp = 15000
    d.start_area = 5 + start                 # >= 5 -> dungeon mode
    d.start_entry = 1
    for i, m in enumerate(maps):
        d.add_area(5 + i, m.to_frua())
    return d


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    path, args = argv[0], argv[1:]
    with open(path, "rb") as f:
        maps = load_maps(f.read())

    def pick(flag):
        want = int(args[args.index(flag) + 1], 0)
        for i, m in enumerate(maps):
            if m.area_id == want:
                return i
        if 0 <= want < len(maps):
            return want
        raise GbGeoError("no map with id or index %d" % want)

    if "--ascii" in args:
        m = maps[pick("--ascii")]
        entry = m.find_entry()
        print(m.ascii(mark=entry[:2] if entry else None))
        if entry:
            print("entry: x=%d y=%d facing %s" % (entry[0], entry[1], DIR_NAMES[entry[2]]))
        return 0

    if "--design" in args:
        out = args[args.index("--design") + 1]
        start = pick("--start") if "--start" in args else 0
        d = build_design(maps, start=start)
        folder = d.write(out, make_current=("--current" in args))
        print("wrote", folder)
        if "--ecl" in args:
            import ecl
            blocks = []
            for path in args[args.index("--ecl") + 1].split(","):
                with open(path, "rb") as f:
                    blocks += ecl.load_blocks(f.read())
            nm, nb = write_scripts(folder, maps, blocks)
            print("  %d maps, %d scripts (%s)" % (nm, nb, blocks[0].dialect.name))
        m = maps[start]
        e = m.find_entry()
        print("  %d areas; starting in area %d (source id %s), party at x=%d y=%d facing %s"
              % (len(maps), 5 + start, m.area_id, e[0], e[1], DIR_NAMES[e[2]]))
        return 0

    for i, m in enumerate(maps):
        agree, total = m.edge_symmetry()
        print("%2d  id %-4s %2dx%-2d  shared edges agree %3d/%d (%d%%)"
              % (i, m.area_id, m.width, m.height, agree, total,
                 100 * agree // total if total else 0))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except GbGeoError as e:
        sys.exit("gbgeo: %s" % e)
