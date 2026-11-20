"""gbmon.py — Gold Box monsters into a design's own MONSTnnn.DAT (ADR-0023).

A script's LOAD_MONSTER names a monster by the game's own number. FRUA already
lets a design replace any monster: `<design>/MONSTnnn.DAT`, 450 bytes, is read
before the stock MONST.GLB (jt127). So the Dark Queen's bestiary does not have
to be approximated by FRUA's — each of its records is rewritten in FRUA's
layout under the same number.

The two layouts are close relatives (FRUA's stock library still carries CAPTAIN
DAENOR and GRUNSCHKA). The field map below was not guessed: 29 monsters exist
in both games under the same name, and this conversion reproduces FRUA's own
record for them byte for byte except where FRUA re-balanced a creature
(tests/test_gbmon.py holds a synthetic record to the map; run this file on the
real libraries to see the comparison).

    Dark Queen (MONCHA.GLB member)            FRUA (450 bytes)
    0x000..0x057                              0x000..0x057   unchanged
    0x058..0x067  eight little-endian words   0x058..0x05f   eight bytes
                  [0] race  [3] dropped (0x11 on most; FRUA has 0)
    0x068..0x156  name, scores, combat stats, 0x060..0x14e
                  memorized spells (0xce, 137 of them; FRUA's 0xc6 has 141)
                  flag byte 0xc7: bit 0x20 remapped (see FLAGS)
    -                                         0x14f..0x152   four more memorized slots
    0x157..0x181  spellbook bits and the rest 0x153..0x17d   (FRUA rec[339])
    0x182..0x18a  nine bytes FRUA lacks       -
    0x18b..0x198                              0x17e..0x18b
    0x199..0x19d  five bytes FRUA lacks       -
    0x19e         number of effects  E
    0x19f         number of items    I
    0x1a0         E x 5 bytes {id, .., .., .., ..}    0x1ae  ten effect ids
                  I x 17 bytes, an ITEM.DAT entry     0x18e  sixteen item numbers
                                                      0x19e  sixteen charge bytes (left 0)

Both libraries are the GLIB/HLIB 'DATA' container with member 0 an id table;
a member is a big-endian length word and PackBits.

RACE. The Dark Queen has eight (two elves, two dwarves, kender), FRUA seven;
"monster" is 7 there and 6 here. Read off the shared records: 7->6 on every
creature, 6->5 on Captain Daenor (human), 4->2 on Grunschka (dwarf). A
design that plays by the Dark Queen's own rules (it holds DQK.EXE; ADR-0026)
keeps the game's numbers instead: native=True.

FLAGS. Bit 0x20 of one flag byte (FRUA 0xbf) changed meaning. The Dark
Queen sets it on its undead; FRUA's copies of the same six undead have it
clear, and FRUA's ettin and fire giant have it set where the Dark Queen's
(flags 0x0c) do not. So it is cleared, and set again on a creature carrying
both 0x04 and 0x08. Two giants is thin evidence for the second half.

WHERE THE FOUR BYTES GO. The monster libraries fix it only to within a run of
zeros (Captain Daenor's spell block shows the second block sits four bytes
later in FRUA). The player characters fix it: the Dark Queen's memorized-spell
list is 137 bytes at 0xce and its spellbook bits start at 0x157 in every mage
(the .QCH files), where FRUA has 141 at rec[198] and its bits at rec[339]. So
FRUA's list is four longer and the seam is the end of it. (It was 0xeb, inside
the list; every monster converts to the same bytes either way.)

ITEMS. The Dark Queen keeps a monster's items inline, FRUA keeps numbers into
ITEM.DAT. The two games' ITEM.DAT have the same 254 entries in the same order,
and an inline item is one of them with its "readied" bytes set, so the number
is found by matching the first ten bytes. Without --items the items are left
off and reported.

    python3 tools/gbmon.py MONCHA.GLB                         # list
    python3 tools/gbmon.py MONCHA.GLB --items ITEM.DAT --design OUT.DSN
    python3 tools/gbmon.py MONCHA.GLB --compare MONST.GLB     # the 29-monster check

The game data is the user's own; nothing here ships any.
"""
import os
import struct
import sys

FRUA_SIZE = 450
RACE = {0: 0, 1: 0, 2: 1, 3: 2, 4: 2, 5: 4, 6: 5, 7: 6}
ITEM_KEY = 10                       # leading bytes that identify an ITEM.DAT entry


class GbMonError(ValueError):
    pass


def unpack(member):
    """A library member: big-endian length, then PackBits."""
    if len(member) < 2:
        raise GbMonError("member too short")
    size = (member[0] << 8) | member[1]
    out = bytearray()
    i = 2
    while i < len(member) and len(out) < size:
        c = member[i]
        i += 1
        if c >= 0x80:
            out += member[i:i + 1] * (257 - c)
            i += 1
        else:
            out += member[i:i + c + 1]
            i += c + 1
    return bytes(out[:size])


def load_library(data):
    """{id: unpacked record} for a GLIB (big-endian) or HLIB (little-endian)."""
    if data[:4] == b"HLIB":
        e = "<"
    elif data[:4] == b"GLIB":
        e = ">"
    else:
        raise GbMonError("not a GLIB/HLIB library")
    n = struct.unpack(e + "H", data[8:10])[0]
    offs = struct.unpack(e + "%dL" % (n + 1), data[16:16 + 4 * (n + 1)])
    table = data[offs[0]:offs[1]]
    k = struct.unpack(e + "H", table[:2])[0]
    pairs = struct.unpack(e + "%dH" % (2 * k), table[2:2 + 4 * k])
    out = {}
    for ident, index in zip(pairs[0::2], pairs[1::2]):
        if 0 < index < n:
            out[ident] = unpack(data[offs[index]:offs[index + 1]])
    return out


def name_of(rec, frua=False):
    off = 0x60 if frua else 0x68
    return rec[off:off + 16].split(b"\0")[0].decode("latin-1")


def item_index(item_table):
    """ITEM.DAT (17 bytes an entry in the Dark Queen) -> {first ten bytes: number}."""
    index = {}
    for k in range(len(item_table) // 17):
        index.setdefault(bytes(item_table[k * 17:k * 17 + ITEM_KEY]), k)
    return index


def convert(q, items=None, native=False):
    """One Dark Queen record -> (FRUA record, notes). native: keep the Dark
    Queen's race numbers, for a design that plays by its rules."""
    notes = []
    if len(q) < 0x1a0:
        raise GbMonError("record is %d bytes, too short" % len(q))
    f = bytearray(FRUA_SIZE)
    f[:0x58] = q[:0x58]
    for i in range(8):
        f[0x58 + i] = q[0x58 + 2 * i]
        if q[0x59 + 2 * i]:
            notes.append("word %d has a high byte" % i)
    f[0x58] = q[0x58] if native else RACE.get(q[0x58], 6)
    f[0x5b] = 0
    f[0x60:0x14f] = q[0x68:0x157]
    f[0x153:0x17e] = q[0x157:0x182]
    f[0xbf] &= 0xdf
    if f[0xbf] & 0x0c == 0x0c:
        f[0xbf] |= 0x20
    f[0x17e:0x18c] = q[0x18b:0x199]
    if any(q[0x182:0x18b]) or any(q[0x199:0x19e]):
        notes.append("dropped %s / %s" % (q[0x182:0x18b].hex(), q[0x199:0x19e].hex()))
    neff, nitem = q[0x19e], q[0x19f]
    p = 0x1a0
    if p + 5 * neff + 17 * nitem > len(q):
        notes.append("lists run past the record; left off")
        neff = nitem = 0
    for i in range(neff):
        if i < 10:
            f[0x1ae + i] = q[p + 5 * i]
        else:
            notes.append("effect %d dropped (FRUA holds ten)" % q[p + 5 * i])
    p += 5 * neff
    slot = 0
    for i in range(nitem):
        it = bytes(q[p + 17 * i:p + 17 * i + 17])
        num = items.get(it[:ITEM_KEY]) if items is not None else None
        if not num or num > 254 or slot >= 16:
            notes.append("item type %d left off" % it[0])
            continue
        f[0x18e + slot] = num
        slot += 1
    return bytes(f), notes


def write_design(folder, monsters, items=None, log=None, native=False):
    """MONSTnnn.DAT for every monster FRUA can number (1..255)."""
    done = 0
    for ident, q in sorted(monsters.items()):
        if not 0 < ident < 256:
            continue
        rec, notes = convert(q, items, native)
        with open(os.path.join(folder, "MONST%03d.DAT" % ident), "wb") as fh:
            fh.write(rec)
        done += 1
        if log and notes:
            log("%3d %-16s %s" % (ident, name_of(q), "; ".join(notes)))
    return done


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    with open(argv[0], "rb") as fh:
        mons = load_library(fh.read())
    args = argv[1:]
    items = None
    if "--items" in args:
        with open(args[args.index("--items") + 1], "rb") as fh:
            items = item_index(fh.read())
    if "--compare" in args:
        with open(args[args.index("--compare") + 1], "rb") as fh:
            stock = load_library(fh.read())
        by_name = {name_of(r, True).upper(): r for r in stock.values()}
        same = shared = 0
        for ident, q in sorted(mons.items()):
            ref = by_name.get(name_of(q).upper())
            if ref is None:
                continue
            shared += 1
            got, _ = convert(q, items)
            diff = [o for o in range(FRUA_SIZE) if got[o] != ref[o]]
            same += not diff
            print("%3d %-16s %s" % (ident, name_of(q),
                  "identical" if not diff else "%d bytes differ: %s" % (
                      len(diff), " ".join("%03x" % o for o in diff[:12]))))
        print("%d shared by name, %d identical" % (shared, same))
        return 0
    if "--design" in args:
        folder = args[args.index("--design") + 1]
        n = write_design(folder, mons, items, log=print)
        print("wrote %d monsters to %s" % (n, folder))
        return 0
    for ident, q in sorted(mons.items()):
        print("%3d %-16s %3d bytes, %d effects, %d items"
              % (ident, name_of(q), len(q), q[0x19e], q[0x19f]))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except GbMonError as e:
        sys.exit("gbmon: %s" % e)
