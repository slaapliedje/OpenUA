"""gbchar.py — a Gold Box game's own characters as FRUA .CCH files (ADR-0023).

THE ENGINE NOW READS .QCH ITSELF (ADR-0026 step (d)): a .QCH in a design's
SAVE folder is listed and loaded by the training hall (src/engine/qch.c,
held to convert() below by tests/test_qch.py), and gbgeo.py --exe copies the
game's heroes in. This file is the reference for that reader; running it is
no longer needed to play.

The Dark Queen of Krynn ships six ready-made heroes as .QCH files (the Steam
release keeps them in "Default files/Saves"). FRUA's Training Hall lists every
<design>/SAVE/*.CCH, so a converted game can start with its own party: each
.QCH is rewritten as the .CCH stream FRUA itself writes (jt578) and reads
(jt577).

THE .QCH (Dark Queen). The in-memory character record written out as it
stood, live pointers and all; Greybox (kblood/Greybox_CombinedGoldbox,
docs/engine/research/dqk-save-format.md) solved the same record inside
SAVGAMA.QSV. A .QCH is

    0x000 .. 0x19d   the fixed part: MONCHA's layout without its two count
                     bytes. +0x08 is the number of items (mirrored at 0xc9).
    0x19e + 17*n     n items, each a 17-byte ITEM.DAT entry
    then             affect nodes to the end of the file, 9 bytes each:
                     five data bytes {id, word, 0xff, 0} and a far pointer
                     to the next one

which accounts for all six files to the byte.

THE .CCH (FRUA, little-endian as DOS writes it)

    398 bytes        the record. Its pointer fields only flag what follows:
                     rec[8] non-zero = items follow (rec[193] of them),
                     rec[4] non-zero = effect nodes follow.
    18 * rec[193]    the items, node bytes +40..+57 (a container, type 73,
                     would be followed by its contents; none is written)
    10 * nodes       {id, 0, word, byte, byte, more}; `more` non-zero on all
                     but the last

THE RECORD is gbmon.py's map (the two games' records are close relatives; see
its docstring), so the races map the same way and spell and affect numbers
carry over unchanged. A Dark Queen 17-byte item becomes FRUA's 18 with a zero
on the end (gbgeo.item_table). An affect node's five bytes are FRUA's with
the 68000's pad byte after the id: jt588 builds {id, 0, 0, 0xff, 0} where the
Dark Queen stores {id, 0, 0, 0xff, 0}.

CLASSES. The seven class-level slots are the same in both games (FRUA's own
class-name table still says Cleric, Knight, Fighter, Paladin, Ranger,
Magic-User, Thief), and so is the class-bit mask at rec[183] (fighter 8 in
both). FRUA's slot 1 is its druid, so a Dark Queen knight arrives as one:
--knight fighter moves those levels to the fighter slot instead.

UNDER THE GAME'S OWN RULES (ADR-0026). A design holding DQK.EXE plays by the
Dark Queen's rules, so its characters keep the game's race numbers (kender
5, human 6) and a knight stays a knight: when --design names such a folder,
nothing is translated and --knight is ignored.

CHECKED in Hatari (Falcon build) on the six heroes the Steam release ships:
all six appear in the hall, join, and show the game's own hit points,
ability scores, age, alignment, races and class levels (Roland of Dust's
sheet reads KNIGHT 11). Two things are the engine's, not this file's: FRUA
applies its druid's equipment rules to class bit 16, so the hall unreadies a
knight's sword and plate; and FRUA's item TYPE table still decides what an
item is, so Walker Keys' hoopak (type 0x43) cannot be readied.

    python3 tools/gbchar.py "Default files/Saves"/*.QCH            # list
    python3 tools/gbchar.py "Default files/Saves"/*.QCH --design OUT.DSN

The game data is the user's own; nothing here ships any.
"""
import os
import struct
import sys

import gbmon

QCH_FIXED = 0x19e
ITEM = 17
NODE = 9
REC = 398
CLASSES = ("cleric", "knight", "fighter", "paladin", "ranger", "mage", "thief")
KNIGHT_BIT, FIGHTER_BIT = 16, 8             # rec[183], read off the heroes


class GbCharError(ValueError):
    pass


def parse_qch(q):
    """(fixed part, [17-byte items], [5-byte affect data])."""
    if len(q) < QCH_FIXED:
        raise GbCharError("%d bytes: shorter than the fixed part" % len(q))
    n = q[8]
    end = QCH_FIXED + ITEM * n
    rest = len(q) - end
    if rest < 0 or rest % NODE:
        raise GbCharError("%d bytes do not split into %d items and 9-byte nodes"
                          % (len(q), n))
    items = [q[QCH_FIXED + ITEM * k:QCH_FIXED + ITEM * (k + 1)] for k in range(n)]
    nodes = [q[end + NODE * k:end + NODE * k + 5] for k in range(rest // NODE)]
    return q[:QCH_FIXED], items, nodes


STRIP = " =+<>\"[].*,?/\\:;|"            # FRUA's jt130 table (A5 -31268)


def file_stem(name):
    """The 8.3 stem FRUA's hall writes this character to (cch_file_name)."""
    stem = "".join(c for c in name if c not in STRIP)[:8].upper()
    return stem or "CHAR"


def name_of(q):
    return q[0x68:0x78].split(b"\0")[0].decode("latin-1")


def classes_of(q):
    return [(CLASSES[k], q[0xa5 + k]) for k in range(7) if q[0xa5 + k]]


def convert(q, knight=None, native=False):
    """One .QCH -> (.CCH bytes, notes). native: the design plays by the Dark
    Queen's own rules (it holds DQK.EXE), so the race stays the game's and a
    knight stays a knight."""
    fixed, items, nodes = parse_qch(q)
    notes = []
    # gbmon's map wants MONCHA's shape: the fixed part, then the two counts
    rec, more = gbmon.convert(bytes(fixed) + b"\0\0", native=native)
    rec = bytearray(rec[:REC])
    notes += [n for n in more if not n.startswith("dropped")]
    rec[0:4] = bytes(4)                    # party link
    rec[12:68] = bytes(56)                 # equip slots + sub-record: rebuilt
    if native:
        pass
    elif knight == "fighter" and rec[157 + 1]:
        if rec[157 + 2]:
            notes.append("knight and fighter both: knight levels left as druid")
        else:
            rec[157 + 2], rec[157 + 1] = rec[157 + 1], 0
            rec[164 + 2], rec[164 + 1] = rec[164 + 1], 0
            rec[183] = (rec[183] & ~KNIGHT_BIT | FIGHTER_BIT) & 0xff
    elif rec[157 + 1]:
        notes.append("knight %d arrives as FRUA's druid" % rec[157 + 1])

    stream = bytearray()
    kept = 0
    for it in items:
        if it[0] == 73:
            notes.append("container left off (its contents are not in the file)")
            continue
        stream += bytes(it) + b"\0"
        kept += 1
    rec[193] = kept
    rec[8:12] = struct.pack("<L", 1 if kept else 0)
    rec[4:8] = struct.pack("<L", 1 if nodes else 0)
    for k, d in enumerate(nodes):
        last = k == len(nodes) - 1
        stream += bytes([d[0], 0, d[1], d[2], d[3], d[4]])
        stream += struct.pack("<L", 0 if last else 1)
    return bytes(rec) + bytes(stream), notes


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    files = [a for a in argv if a.upper().endswith(".QCH")]
    design = argv[argv.index("--design") + 1] if "--design" in argv else None
    knight = argv[argv.index("--knight") + 1] if "--knight" in argv else None
    if knight not in (None, "druid", "fighter"):
        raise GbCharError("--knight is druid or fighter")
    native = bool(design) and os.path.exists(os.path.join(design, "DQK.EXE"))
    if native:
        print("%s plays by the Dark Queen's rules: races and knights kept" % design)
    if design:
        os.makedirs(os.path.join(design, "SAVE"), exist_ok=True)
    for path in files:
        with open(path, "rb") as fh:
            q = fh.read()
        cch, notes = convert(q, knight, native)
        _, items, nodes = parse_qch(q)
        print("%-16s %s  %d items, %d affects%s" % (
            name_of(q), "/".join("%s %d" % c for c in classes_of(q)),
            len(items), len(nodes), "".join("; " + n for n in notes)))
        if design:
            stem = file_stem(name_of(q))
            with open(os.path.join(design, "SAVE", stem + ".CCH"), "wb") as fh:
                fh.write(cch)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except GbCharError as e:
        sys.exit("gbchar: %s" % e)
