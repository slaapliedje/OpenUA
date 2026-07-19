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
            "text_id": ev[4] | (ev[5] << 8),       # little-endian STRG index (0=none)
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
        rec[4] = text_id & 0xff                 # little-endian
        rec[5] = (text_id >> 8) & 0xff
        rec[6] = picture & 0xff
        for s, (mid, count) in enumerate(groups):
            if not (1 <= count <= 31):
                raise GeoError("group %d count %d out of 1..31" % (s, count))
            if not (1 <= mid <= 255):
                raise GeoError("group %d monster id %d out of 1..255" % (s, mid))
            rec[8 + s * 2] = count & 0x1f          # high 3 bits (flags) left 0
            rec[9 + s * 2] = mid & 0xff
        self.encr[o:o + EVENT_SIZE] = rec

    # ---- Treasure events (type 3 / 25) — l28b0 ----
    def treasure(self, idx):
        """Decode a Give/Take-Treasure event. Returns {platinum, gems, jewelry,
        items: [item_id, ...]}. Type 3 gives + refreshes the view; type 25 gives
        only. Money -> the -25314/-25310/-25306 pool; items -> jt187."""
        ev = self.event(idx)
        if ev[0] not in (3, 25):
            raise GeoError("event %d is type %d, not Treasure (3/25)"
                           % (idx, ev[0]))
        b = bytes(ev)
        return {
            "type":     ev[0],
            "platinum": struct.unpack_from("<I", b, 4)[0] & 0x7fffffff,
            "gems":     struct.unpack_from("<H", b, 8)[0],
            "jewelry":  struct.unpack_from("<H", b, 10)[0],
            "items":    [ev[12 + k] for k in range(8) if ev[12 + k]],
        }

    def set_treasure(self, idx, platinum=0, gems=0, jewelry=0, items=(),
                     take=False, cond_type=0, cond_param=0, chain=0,
                     once_only=False):
        """Build a Treasure event: award `platinum`/`gems`/`jewelry` and up to 8
        item ids. take=True builds type 25 (give only, no view refresh) instead
        of type 3."""
        if not (0 <= platinum <= 0x7fffffff):
            raise GeoError("platinum must be 0..0x7fffffff")
        if not (0 <= gems <= 0xffff and 0 <= jewelry <= 0xffff):
            raise GeoError("gems/jewelry must be 0..65535")
        items = list(items)
        if len(items) > 8:
            raise GeoError("at most 8 items")
        o = idx * EVENT_SIZE
        rec = bytearray(EVENT_SIZE)
        rec[0] = 25 if take else 3
        rec[1] = ((cond_type & 0x1f) << 3) | (1 if once_only else 0)
        rec[2] = cond_param & 0xff
        rec[3] = chain & 0xff
        struct.pack_into("<I", rec, 4, platinum & 0x7fffffff)   # LE, bit31 clear
        struct.pack_into("<H", rec, 8, gems & 0xffff)
        struct.pack_into("<H", rec, 10, jewelry & 0xffff)
        for k, iid in enumerate(items):
            if not (1 <= iid <= 255):
                raise GeoError("item id %d out of 1..255" % iid)
            rec[12 + k] = iid & 0xff
        self.encr[o:o + EVENT_SIZE] = rec

    # ---- Passage / transfer events (type 5 / 11 / 34) — l5676 ----
    def passage(self, idx):
        """Decode a Passage event. For a type-11 level change: {dest_area (the
        target GEO number, ev[14]), and the landing — either marker=ev[13] (an
        entry-point index in the target area) when ev[12] bit0 is set, or a
        direct (x, y, facing)}. Types 5/34 move within the area (dest_area 0)."""
        ev = self.event(idx)
        if ev[0] not in (5, 11, 34):
            raise GeoError("event %d is type %d, not Passage (5/11/34)"
                           % (idx, ev[0]))
        info = {"type": ev[0], "dest_area": ev[14],
                "confirm": bool(ev[7] & 0x20)}
        if ev[12] & 0x01:
            info["marker"] = ev[13]                 # entry index in target area
        else:
            info["x"] = ev[9]                        # landing row (0..height-1)
            info["y"] = ev[8]                        # landing col (0..width-1)
            info["facing"] = (ev[7] & 0x0c) >> 1     # 0=N 2=E 4=S 6=W
        return info

    def set_passage(self, idx, dest_area, x, y, facing=0,
                    cond_type=0, cond_param=0, chain=0, once_only=False):
        """Build a type-11 level-change Passage: stepping its cell transfers the
        party to `dest_area` (GEO<dest_area>.DAT) landing at (x, y) facing
        `facing` (0=N 2=E 4=S 6=W). Direct landing (no target-area marker)."""
        if not (1 <= dest_area <= 255):
            raise GeoError("dest_area must be 1..255")
        if facing not in (0, 2, 4, 6):
            raise GeoError("facing must be 0(N) 2(E) 4(S) 6(W)")
        o = idx * EVENT_SIZE
        rec = bytearray(EVENT_SIZE)
        rec[0] = 11
        rec[1] = ((cond_type & 0x1f) << 3) | (1 if once_only else 0)
        rec[2] = cond_param & 0xff
        rec[3] = chain & 0xff
        rec[7] = (facing << 1) & 0x0c        # bit5 clear => no yes/no prompt
        rec[8] = y & 0xff                    # landing col
        rec[9] = x & 0xff                    # landing row
        rec[12] = 0                          # bit0 clear => direct landing
        rec[14] = dest_area & 0xff           # target area number
        self.encr[o:o + EVENT_SIZE] = rec

    # ---- Message / Text events (type 2 / 14) — l4d26 ----
    def message(self, idx):
        """Decode a Message event. Returns {lines: [text_id,...] (up to 5, into
        the STRG table), picture, sound, confirm (per-line pause mask ev[4])}."""
        ev = self.event(idx)
        if ev[0] not in (2, 14):
            raise GeoError("event %d is type %d, not Message (2/14)" % (idx, ev[0]))
        lines = []
        for s in range(5):
            tid = ev[8 + s * 2] | (ev[9 + s * 2] << 8)   # little-endian STRG index
            if tid:
                lines.append(tid)
        return {"type": ev[0], "lines": lines, "picture": ev[6],
                "sound": ev[18], "confirm_mask": ev[4]}

    def set_message(self, idx, text_ids, picture=0, sound=0, confirm_mask=0x1f,
                    cond_type=0, cond_param=0, chain=0, once_only=False):
        """Build a Message event from up to 5 STRG text ids. `confirm_mask` bit i
        pauses for confirm after line i (default: pause after every line)."""
        if len(text_ids) > 5:
            raise GeoError("at most 5 text lines")
        o = idx * EVENT_SIZE
        rec = bytearray(EVENT_SIZE)
        rec[0] = 2
        rec[1] = ((cond_type & 0x1f) << 3) | (1 if once_only else 0)
        rec[2] = cond_param & 0xff
        rec[3] = chain & 0xff
        rec[4] = confirm_mask & 0xff
        rec[6] = picture & 0xff
        rec[18] = sound & 0xff
        for s, tid in enumerate(text_ids):
            rec[8 + s * 2] = tid & 0xff              # little-endian
            rec[9 + s * 2] = (tid >> 8) & 0xff
        self.encr[o:o + EVENT_SIZE] = rec

    # ---- STRG string table ----
    def strg_read(self):
        """Decode the area string table -> a list of up to 400 strings (empty
        strings for unused slots). Uppercase, per the 6-bit alphabet."""
        lt = self.strg[STRG_INDEX_OFF:STRG_INDEX_OFF + STRG_MAX_STR]
        body = self.strg[STRG_BODY_OFF:]
        strings, off = [], 0
        for i in range(STRG_MAX_STR):
            n = lt[i]
            if n == 255:                 # unused-slot marker
                strings.append("")
                continue
            if n == 0:
                strings.append("")
                continue
            nchars = (n << 2) // 3
            codes = _unpack6(body[off:off + n + 1], nchars)
            s = "".join(chr(_code6_to_char(c)) for c in codes).split("\x00")[0]
            strings.append(s)
            off += n
        return strings

    def strg_write(self, strings):
        """Build the string table from a list of strings (<=400; text folds to
        uppercase). Event text ids are 1-based (jt232 reads index num-1)."""
        if len(strings) > STRG_MAX_STR:
            raise GeoError("at most %d strings" % STRG_MAX_STR)
        index = bytearray(STRG_MAX_STR)
        body = bytearray()
        for i, s in enumerate(strings):
            b = s.encode("mac-roman", "replace") if isinstance(s, str) else bytes(s)
            if not b:
                index[i] = 0
                continue
            packed = _pack6([_char_to_code6(c) for c in b])
            if len(packed) > 254:      # index byte is 1 byte; 255 = unused marker
                raise GeoError("string %d too long (%d packed bytes > 254)"
                               % (i, len(packed)))
            if len(body) + len(packed) > STRG_BODY_CAP:
                raise GeoError("string body exceeds %d bytes" % STRG_BODY_CAP)
            index[i] = len(packed)
            body += packed
        header = struct.pack("<HHH", STRG_BODY_CAP, 0xffff, 0)  # LE on disk
        strg = header + bytes(index) + bytes(body)
        strg += bytes(STRG_SIZE - len(strg))                    # zero-pad body
        self.strg = bytearray(strg)

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


# ---- STRG string table: a 6-bit packed uppercase char pool ----
# Header (6 bytes, little-endian on disk; the engine byte-swaps it in l4e3a):
#   [0] body capacity (6762)   [2] 0xffff   [4] 0
# then a 400-byte length index (lt[i] = packed byte count of string i), then the
# body at +406. String i lives at body[sum(lt[0..i-1])]; its char count is
# lt[i]*4//3 (four 6-bit chars per three bytes; l4fbe/l4c88).
STRG_INDEX_OFF = 6
STRG_BODY_OFF  = 406
STRG_MAX_STR   = 400
STRG_BODY_CAP  = STRG_SIZE - STRG_BODY_OFF   # 6762


def _char_to_code6(c):
    """ASCII byte -> 6-bit code (l4c88 inverse). Lowercase folds to upper; the
    packed alphabet is uppercase A-Z, digits, space and punctuation only."""
    if 97 <= c <= 122:      # a-z -> A-Z
        c -= 32
    if 65 <= c <= 95:       # A-Z [\]^_  -> codes 1..31
        return c - 64
    if 32 <= c <= 63:       # space ! " ... 0-9 : ; < = > ?  -> codes 32..63
        return c
    return 32               # unsupported char -> space


def _code6_to_char(v):
    if 1 <= v <= 31:
        return v + 64
    return v                # 0 (pad) or 32..63 literal


def _pack6(codes):
    """Pack 6-bit codes into bytes, 4 codes per 3 bytes (minimal length)."""
    out = bytearray()
    for i in range(0, len(codes), 4):
        g = codes[i:i + 4] + [0] * (4 - len(codes[i:i + 4]))
        b0 = (g[0] << 2) | (g[1] >> 4)
        b1 = ((g[1] & 0xf) << 4) | (g[2] >> 2)
        b2 = ((g[2] & 0x3) << 6) | g[3]
        n = len(codes) - i               # codes left in this group
        for k, b in enumerate((b0, b1, b2)):
            if k < min(n, 3):            # only the bytes this group actually needs
                out.append(b)
    return bytes(out)


def _unpack6(data, nchars):
    """Unpack `nchars` 6-bit codes from `data` (l4c88)."""
    codes = []
    phase, si, cur, prev = 1, -1, 0, 0
    for _ in range(nchars):
        if phase < 4:
            si += 1
            prev, cur = cur, (data[si] if si < len(data) else 0)
        if   phase == 1: v = (cur >> 2) & 0x3f
        elif phase == 2: v = ((prev << 4) + (cur >> 4)) & 0x3f
        elif phase == 3: v = ((prev << 2) + (cur >> 6)) & 0x3f
        else:            v = cur & 0x3f
        phase = phase + 1 if phase < 4 else 1
        codes.append(v)
    return codes


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
    strings = g.strg_read()
    nstr = sum(1 for s in strings if s)
    print("strings:", nstr, "non-empty")
    for i in range(MAX_EVENTS):
        e = g.event_info(i)
        if not e:
            continue
        extra = ""
        if e["type"] in (1, 33):
            extra = "  " + repr(g.combat(i)["groups"])
        elif e["type"] in (2, 14):
            lines = g.message(i)["lines"]
            extra = "  " + repr([strings[t - 1][:40] for t in lines if t])
        elif e["type"] in (5, 11, 34):
            p = g.passage(i)
            extra = "  -> area %d" % p["dest_area"]
        elif e["type"] in (3, 25):
            t = g.treasure(i)
            extra = "  %dpp %dg %dj items=%r" % (t["platinum"], t["gems"],
                                                 t["jewelry"], t["items"])
        print("  [%2d] %-24s cond=%-14s chain=%3d%s%s"
              % (i, e["name"], e["cond_name"].split(" (")[0], e["chain"],
                 " once" if e["once_only"] else "", extra))
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main(sys.argv[1:]))
