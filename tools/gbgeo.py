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
    python3 tools/gbgeo.py GEO.GLB --design OUT --ecl ECL.GLB --start 2 --boot-script 1
                                                      # Dark Queen from its own start-up script
    python3 tools/gbgeo.py GEO.GLB --design OUT --ecl ECL.GLB --pictures DISK2
                                                      # ...and the pictures its scripts show
    python3 tools/gbgeo.py GEO.GLB --design OUT --ecl ECL.GLB --monsters MONCHA.GLB,ITEM.DAT
                                                      # ...and its own monsters (tools/gbmon.py)
    python3 tools/gbgeo.py GEO.GLB --design OUT --ecl ECL.GLB --exe DQK.EXE
                                                      # ...its rules, items, shops, item words and
                                                      # monsters (DQK.EXE, ITEM.DAT, ITEMS.DAT and
                                                      # MONCHA.GLB copied in and read by the engine
                                                      # as they are)

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


def map_descriptor(m, boot_script=0):
    """GBDnnn.DAT — what makes area slot nnn a SCRIPT AREA (src/engine/boot.c,
    "GOLD BOX SCRIPT AREAS"): the map's cell-descriptor plane, which the
    scripts dispatch on and the engine's own cells have no room for, with the
    map's size and the game's own number for it. `boot_script` names the
    script to run when the ENGINE opens this area (a new game); 0 means the
    script numbered like the map."""
    if not (m.width < 256 and m.height < 256 and 0 < (m.area_id or 0) < 256
            and 0 <= boot_script < 256):
        raise GbGeoError("map does not fit a descriptor header")
    desc = bytes(m.backdrop[y][x] for y in range(m.height) for x in range(m.width))
    return b"GBD1" + bytes([m.width, m.height, m.area_id, boot_script]) + desc


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


def write_scripts(folder, maps, blocks, first_area=5, boot=None):
    """Write the descriptor of every map, every script, and the index.
    `boot` is (map index, script number): that map's descriptor names the
    script to start with. Returns (maps written, scripts written)."""
    for i, m in enumerate(maps):
        with open(os.path.join(folder, "GBD%03d.DAT" % (first_area + i)), "wb") as f:
            f.write(map_descriptor(m, boot[1] if boot and boot[0] == i else 0))
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


# The picture libraries a script's PICTURE opcode draws from. The Dark Queen
# ships them under the names FRUA uses and in the DOS container FRUA's own
# converter reads, so they only have to be converted and put in the design
# folder. The frame, wall and combat libraries are left alone on purpose:
# FRUA's screens are built from FRUA's own pieces of those.
PICTURE_LIBS = ("PICA", "PICB", "PICC", "BIGPIC", "SPRIT", "CPIC")


def write_pictures(folder, dirs, convert=None, libs=PICTURE_LIBS):
    """Convert the game's picture libraries found in `dirs` into `folder`.
    Returns the names written. A library the game does not have is skipped."""
    if convert is None:
        import art_convert
        convert = art_convert.convert
    done = []
    for lib in libs:
        for d in dirs:
            hit = [n for n in sorted(os.listdir(d)) if n.upper() == lib + ".TLB"]
            if not hit:
                continue
            with open(os.path.join(d, hit[0]), "rb") as f:
                out = convert(f.read())
            with open(os.path.join(folder, lib + ".CTL"), "wb") as f:
                f.write(out)
            done.append(lib + ".CTL")
            break
    return done


def item_table(table):
    """The Dark Queen's ITEM.DAT (17 bytes an entry) as FRUA's (18): the same
    fields in the same places with one byte more on the end, which the 254
    entries the two games share show to be zero. Always 254 entries."""
    out = bytearray()
    for k in range(254):
        out += table[k * 17:k * 17 + 17].ljust(17, b"\xff") + b"\0"
    return bytes(out)


def find_shops(exe):
    """The shop stock lists out of the game's executable: [[item number, ...], ...].

    The Dark Queen's shop opcode takes a shop type 1..9 and reads three tables
    that sit together in its data: nine first-indexes, nine last-indexes, then
    the list of ITEM.DAT numbers those index (all 16-bit, the list 1-based).
    Nothing fixes their address, so they are found by shape: the first table
    starts at 1 and each shop starts one past where the one before it ended."""
    for at in range(0, len(exe) - 38, 2):
        first = struct.unpack_from("<9H", exe, at)
        if first[0] != 1:
            continue
        last = struct.unpack_from("<9H", exe, at + 18)
        if any(not f <= l < 1024 for f, l in zip(first, last)):
            continue
        if any(last[i] + 1 != first[i + 1] for i in range(8)):
            continue
        n = last[8]
        if at + 36 + 2 * n > len(exe):
            continue
        items = struct.unpack_from("<%dH" % n, exe, at + 36)
        if any(v == 0 or v > 255 for v in items):
            continue
        return [list(items[f - 1:l]) for f, l in zip(first, last)]
    return None


def write_shops(folder, shops):
    """GBSHOP.DAT: "GBP1", u8 shop count, then per shop u8 n and n item numbers."""
    out = bytearray(b"GBP1") + bytes([len(shops)])
    for stock in shops:
        stock = stock[:255]
        out += bytes([len(stock)]) + bytes(stock)
    with open(os.path.join(folder, "GBSHOP.DAT"), "wb") as f:
        f.write(out)


# FRUA's item-name word table (A5 -13628, read by jt28) has 126 words; its
# next entries are the old-age death messages, and the Dark Queen's table
# carries the same 126 words (two spelled differently), 29 more of its own
# (Hoopak, Dragonlance, Solamnic Plate, ...) and then the same messages. An
# item names its words by number, so those 29 must come from the game.
FRUA_WORDS = 126
WORDS_END = b"who is looking very old"


def find_words(exe):
    """The game's item-name words past FRUA's 126, out of its executable, or
    None. The table is far pointers into the data segment; it is found by
    shape: the words 1..4 are Battle Axe, Hand Axe, Club, Dagger, stored one
    after another, which fixes both the table and the segment's base."""
    first = (b"Battle Axe", b"Hand Axe", b"Club", b"Dagger")
    at = exe.find(b"\0".join(first) + b"\0")
    if at < 0:
        return None
    gaps = [len(w) + 1 for w in first]

    def word(base, off):
        a = base + off
        e = exe.find(b"\0", a)
        return exe[a:e] if 0 <= a < e else None

    for p in range(0, len(exe) - 4 * 256):
        o = [struct.unpack_from("<H", exe, p + 4 * k)[0] for k in range(1, 5)]
        if any(o[k + 1] - o[k] != gaps[k] for k in range(3)):
            continue
        base = at - o[0]
        seg = struct.unpack_from("<H", exe, p + 6)[0]
        # other tables point into the same strings at the same spacing; the
        # item table is the one whose next words are these too
        if [word(base, struct.unpack_from("<H", exe, p + 4 * k)[0])
                for k in (5, 6, 7)] != [b"Dart", b"Hammer", b"Javelin"]:
            continue
        out = []
        for k in range(FRUA_WORDS, 256):
            off, sg = struct.unpack_from("<HH", exe, p + 4 * k)
            w = word(base, off) if sg == seg else None
            if w is None or w.startswith(WORDS_END):
                break
            out.append(w.decode("latin-1"))
        if out:
            return out
    return None


def write_words(folder, words):
    """GBWORDS.DAT: "GBW1", u8 first word number, u8 count, the words NUL-ended."""
    out = bytearray(b"GBW1") + bytes([FRUA_WORDS, len(words)])
    for w in words:
        out += w.encode("latin-1")[:40] + b"\0"
    with open(os.path.join(folder, "GBWORDS.DAT"), "wb") as f:
        f.write(out)


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
            boot = None
            if "--boot-script" in args:
                boot = (start, int(args[args.index("--boot-script") + 1], 0))
            nm, nb = write_scripts(folder, maps, blocks, boot=boot)
            print("  %d maps, %d scripts (%s)" % (nm, nb, blocks[0].dialect.name))
        if "--pictures" in args:
            got = write_pictures(folder, args[args.index("--pictures") + 1].split(","))
            print("  pictures: %s" % (", ".join(got) or "none found"))
        exe = native = None
        if "--exe" in args:
            import gbrules
            with open(args[args.index("--exe") + 1], "rb") as f:
                exe = f.read()
            try:
                gbrules.verify(exe)
                native = True       # the design plays by the game's own rules
            except gbrules.GbRulesError:
                native = False
        if "--monsters" in args:
            import gbmon
            parts = args[args.index("--monsters") + 1].split(",")
            with open(parts[0], "rb") as f:
                mons = gbmon.load_library(f.read())
            items = None
            if len(parts) > 1:
                with open(parts[1], "rb") as f:
                    table = f.read()
                items = gbmon.item_index(table)
                # a design playing by the game's rules has the engine read
                # ITEM.DAT as it is (src/engine/gbitems.c); any other gets
                # it in FRUA's layout
                with open(os.path.join(folder, "ITEM.DAT" if native else "GBITEM.DAT"),
                          "wb") as f:
                    f.write(table if native else item_table(table))
            if native:              # read by the engine as it is (gbmon.c)
                with open(os.path.join(folder, "MONCHA.GLB"), "wb") as f, \
                        open(parts[0], "rb") as g:
                    f.write(g.read())
                print("  monsters: %d, the game's own MONCHA.GLB copied in" % len(mons))
            else:
                print("  monsters: %d" % gbmon.write_design(folder, mons, items))
        if exe is not None and not native:
            shops = find_shops(exe)
            if shops is None:
                print("  shops: no stock tables found in that executable")
            else:
                write_shops(folder, shops)
                print("  shops: %d (%s items)" % (len(shops),
                      ", ".join(str(len(x)) for x in shops)))
            words = find_words(exe)
            if words is None:
                print("  item words: none past FRUA's %d found" % FRUA_WORDS)
            else:
                write_words(folder, words)
                print("  item words: %d past FRUA's (%s ...)" % (len(words), words[0]))
            print("  rules: not the Dark Queen's DQK.EXE; FRUA's rules")
        elif exe is not None:       # the engine reads them from the EXE itself
            print("  shops, item words: read from DQK.EXE by the engine")
            with open(os.path.join(folder, "DQK.EXE"), "wb") as f:
                f.write(exe)
            print("  rules: the Dark Queen's (DQK.EXE copied in)")
            # its item table and item TYPE table (which classes use
            # which item) and its monster library, read by the engine
            # as they are; the game keeps them in DISK1 and DISK2
            exe_dir = os.path.dirname(os.path.abspath(args[args.index("--exe") + 1]))
            for name, fits, what in (
                    ("ITEMS.DAT", lambda n: n == 2048, "item types"),
                    ("ITEM.DAT", lambda n: n and n % 17 == 0, "items"),
                    ("MONCHA.GLB", lambda n: n > 16, "monsters")):
                for d in [exe_dir] + sorted(os.path.join(exe_dir, x) for x in os.listdir(exe_dir)):
                    src = os.path.join(d, name)
                    if os.path.isfile(src) and fits(os.path.getsize(src)):
                        with open(src, "rb") as f, open(os.path.join(folder, name), "wb") as g:
                            g.write(f.read())
                        print("  %s: the game's own %s copied in" % (what, name))
                        break
            # its heroes (SAVE\*.QCH beside the EXE): the hall reads
            # them as they are (ADR-0026 step (d)); one the hall has
            # saved to a .CCH is read from that instead
            heroes = os.path.join(exe_dir, "SAVE")
            qch = sorted(x for x in os.listdir(heroes)
                         if x.upper().endswith(".QCH")) if os.path.isdir(heroes) else []
            if qch:
                os.makedirs(os.path.join(folder, "SAVE"), exist_ok=True)
                for x in qch:
                    with open(os.path.join(heroes, x), "rb") as f, \
                            open(os.path.join(folder, "SAVE", x.upper()), "wb") as g:
                        g.write(f.read())
                print("  heroes: %d .QCH copied into SAVE" % len(qch))
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
