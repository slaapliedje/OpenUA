"""gbsave.py — a Dark Queen of Krynn saved game (SAVGAMx.QSV) read as the
game writes it (ADR-0026 step (f)).

This is the reference for src/engine/gbsave.c (tests/test_gbsave.py holds
the two together). The layout was found by saving in the game itself
(DOSBox, the Steam release): the party walked, turned, crossed a scripted
event, won a fight and sailed to a second map, and each save was compared
with the one before. Greybox (kblood/Greybox_CombinedGoldbox,
docs/engine/research/dqk-save-format.md) had the party records' start.

A save is always 7965 bytes:

    0 .. 1023      script memory: byte k is script cell k + 1, one byte a
                   cell (cells 0x001..0x400). Among them, from the scripts:
                     0x11  facing, 0=N 1=E 2=S 3=W
                     0x13  the map loaded (LOAD_FILES writes it; script 46
                           compares it with 47, a map number)
                     0x16  the script running: each script's initial entry
                           compares it with its own number and EXITS when
                           they match, so a reload does not replay arrival;
                           the game loads the map itself
                     0x34, 0x35  "INITIAL X,Y POSITION" (the debug menu)
                     0x1b9 + 17n  name strings, a character a cell
                     0x05..0x0b  THE CLOCK: rounds, minutes, tens of
                           minutes, hours, days, months, years. It is FRUA's
                           game clock (the game record's h[5..11], the same
                           fields and radices 10/10/6/24/30/12/100), and
                           the game keeps it nowhere else. It is carried
                           lazily: scripts add 15 days or set the minutes to
                           59, and the next tick carries one radix at a
                           time (save B has day 38 of month 0, save C two
                           minutes later day 8 of month 1). The engine
                           serves these cells from h[5..11] (boot.c
                           gbs_var).
    1024, 1025     the party's square, x east and y south
    1026           its facing, 0=N 2=E 4=S 6=W
    1027           (not identified)
    1028           the descriptor byte of the square it stands on
    1037           u16, the number of party records (an NPC counts)
    1039           the records, back to back

A record is a .QCH file as it would be written (tools/gbchar.py): the fixed
part, items (the count at +8), and affect nodes when the far pointer at +4 is
set, each node's own far pointer at +5 naming the next and zero on the last.
Only the live pointer words differ from the .QCH the game writes for the same
character. After the last record come 17 bytes of 0xff and a block that is
the same in every save (shifted along by a longer party); nothing here reads
it.

The vault, VAULTx.DAT beside the save, is FRUA's vault file with the game's
17-byte item records (an ITEM.DAT entry; FRUA's record is the same and one
byte longer, gbitems.c): 12 bytes of money (steel, gems, jewelry, three
little-endian longs), 0xffff, a little-endian count, then the items, padded
to 50 records from the item table. Always 866 bytes. read_vault() reads one;
the engine's vault screen (jt74) reads it, and a design that plays the game
from its own files writes its vaults this way too (jt75). The vault itself
is the Imperial Bank of Kristophan: a COMBAT with no monsters and area byte
0x31 set. The engine loads a save through boot.c's gbs_qsv_load.

    python3 tools/gbsave.py SAVGAMA.QSV              # summary
    python3 tools/gbsave.py SAVGAMA.QSV --qch DIR    # each member as a .QCH

The game data is the user's own; nothing here ships any.
"""
import os
import struct
import sys

SIZE = 7965
CELLS = 1024
X, Y, FACING, DESC = 1024, 1025, 1026, 1028
COUNT = 1037
PARTY = 1039
MEMBERS = 8         # six heroes and two companions

QCH_FIXED = 0x19e
ITEM = 17
NODE = 9
NODES = 64          # a chain longer than this is not one

CELL_CLOCK = 0x05
CLOCK = ("rounds", "minutes", "tens", "hours", "days", "months", "years")
RADIX = (10, 10, 6, 24, 30, 12, 100)
VAULT_SIZE = 866

# Krynn's three moons, Solinari, Lunitari and Nuitari (robe 1..3): the phase
# (0 low sanction, 1 waxing, 2 high sanction, 3 waning) in cells
# 0x1fa..0x1fc and a day count in 0x1fd..0x1ff. Each day (an hour carried
# into a day, DQK.EXE 0x6128) a moon whose count has reached its period
# moves on a phase and counts from 0 again, else counts on (0x5d6e); a new
# game starts all three at high sanction (0x37f0b). The start-up script
# stores "BALDRIC_NAME" at cell 509, over the counts, so all three move on
# the first day. boot.c's krynn_moons_day is held to this.
MOON_PHASE = 0x1fa
MOON_COUNT = 0x1fd
MOON_PERIOD = (8, 1, 6)


def moons_day(cells):
    """One day of the moons on `cells` (a mutable sequence, cell n at n-1)."""
    for m, period in enumerate(MOON_PERIOD):
        c = MOON_COUNT + m - 1
        if cells[c] >= period:
            p = MOON_PHASE + m - 1
            cells[p] = 0 if cells[p] >= 3 else cells[p] + 1
            cells[c] = 0
        else:
            cells[c] += 1


def moon_bonus(phase):
    """A mage's bonus spells for his moon's phase (DQK.EXE 0x4501c)."""
    return {1: 1, 2: 2}.get(phase, 0)
VAULT_ITEMS = 50
CELL_FACING = 0x11
CELL_MAP = 0x13
CELL_SCRIPT = 0x16
# Cells the game keeps, not the scripts (DQK.EXE; gbsave.h, boot.c gbs_var):
CELL_PLACE = 0x0d     # the selected member's place in the party, from 0
CELL_SEARCH = 0x19    # 1 while searching (Search on, or a Look)
CELL_PARTY = 0x20     # members in the party, companions counted


class GbSaveError(ValueError):
    pass


def record_length(data, at):
    """The length of the record at `at`, walked as the game links it."""
    if at + QCH_FIXED > len(data):
        raise GbSaveError("record at %d runs past the end" % at)
    q = at + QCH_FIXED + ITEM * data[at + 8]
    if struct.unpack_from("<I", data, at + 4)[0] != 0:
        for _ in range(NODES):
            if q + NODE > len(data):
                raise GbSaveError("affect chain at %d runs past the end" % at)
            nxt = struct.unpack_from("<I", data, q + 5)[0]
            q += NODE
            if nxt == 0:
                break
        else:
            raise GbSaveError("affect chain at %d does not end" % at)
    if q > len(data):
        raise GbSaveError("items at %d run past the end" % at)
    return q - at


class GbSave:
    def __init__(self, data):
        if len(data) < PARTY:
            raise GbSaveError("%d bytes, shorter than the header" % len(data))
        self.cells = bytes(data[:CELLS])
        self.x, self.y = data[X], data[Y]
        self.facing = data[FACING] >> 1 & 3
        self.desc = data[DESC]
        count = struct.unpack_from("<H", data, COUNT)[0]
        if count > MEMBERS:
            raise GbSaveError("%d party records" % count)
        self.members = []
        at = PARTY
        for _ in range(count):
            n = record_length(data, at)
            self.members.append(bytes(data[at:at + n]))
            at += n

    def cell(self, n):
        """Script cell n (0x001..0x400)."""
        return self.cells[n - 1]

    @property
    def map(self):
        return self.cell(CELL_MAP)

    @property
    def script(self):
        return self.cell(CELL_SCRIPT)

    @property
    def clock(self):
        """The seven clock fields as saved (not carried)."""
        return dict(zip(CLOCK, (self.cell(CELL_CLOCK + i) for i in range(7))))

    @property
    def time_of_day(self):
        """(hour, minute) as the game shows it."""
        c = self.clock
        return c["hours"], c["tens"] * 10 + c["minutes"]


def write_qsv(cells, x, y, facing, desc, members, base=None):
    """A save the game loads (ADR-0026 decision 4): the 1024 script cells
    (cell n at n-1), the square, facing 0..3, the square's descriptor, and
    the party records as .QCH bytes (tools/gbchar.py to_qch). What is not
    identified comes from `base`, a save the game wrote (the slot's own, or
    the one it ships): bytes 1027 and 1029..1036, and the block after the
    records, which is the same in every save and cut off at SIZE (a longer
    party keeps less of it; zeros past what `base` has)."""
    if len(cells) != CELLS:
        raise GbSaveError("%d cells, not %d" % (len(cells), CELLS))
    if len(members) > MEMBERS:
        raise GbSaveError("%d party records" % len(members))
    b = bytes(base) if base else bytes(PARTY) + b"\xff" * 17
    if len(b) < PARTY:
        raise GbSaveError("the base is shorter than the header")
    head = bytearray(b[:PARTY])
    head[:CELLS] = cells
    head[X], head[Y] = x, y
    head[FACING] = (facing & 3) << 1
    head[DESC] = desc
    struct.pack_into("<H", head, COUNT, len(members))
    tail = b[PARTY:]
    if base:
        tail = tail[sum(len(m) for m in GbSave(b).members):]
    body = bytes(head) + b"".join(bytes(m) for m in members)
    if len(body) > SIZE:
        raise GbSaveError("the party does not fit a save")
    tail = tail[:SIZE - len(body)]
    return body + tail + bytes(SIZE - len(body) - len(tail))


def read_vault(data):
    """VAULTx.DAT -> (steel, gems, jewelry), [17-byte item records]."""
    if len(data) != VAULT_SIZE:
        raise GbSaveError("a vault is %d bytes, not %d" % (VAULT_SIZE, len(data)))
    money = struct.unpack_from("<3I", data, 0)
    mark, count = struct.unpack_from("<HH", data, 12)
    if mark != 0xffff or count > VAULT_ITEMS:
        raise GbSaveError("not a vault header: %04x, %d items" % (mark, count))
    return money, [bytes(data[16 + ITEM * k:16 + ITEM * (k + 1)])
                   for k in range(count)]


def name_of(record):
    return record[0x68:0x78].split(b"\0")[0].decode("latin-1")


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    s = GbSave(open(argv[0], "rb").read())
    print("map %d, script %d, at %d,%d facing %s, descriptor %d"
          % (s.map, s.script, s.x, s.y, "NESW"[s.facing], s.desc))
    for r in s.members:
        print("  %-16s %d bytes, %d items" % (name_of(r), len(r), r[8]))
    if len(argv) == 3 and argv[1] == "--qch":
        for r in s.members:
            fn = "".join(c for c in name_of(r).upper() if c.isalnum())[:8] or "MEMBER"
            with open(os.path.join(argv[2], fn + ".QCH"), "wb") as f:
                f.write(r)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
