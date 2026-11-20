"""tools/gbmon.py — Dark Queen monster records into FRUA's layout.

Synthetic records only: every byte of the source is given a value that says
where it came from, so a field landing in the wrong place shows as the wrong
number. The real libraries are compared when they are present (data/ and a
Steam install are both the user's own and never in the repository).
"""
import glob
import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import gbmon                                                  # noqa: E402


def record(effects=(), items=(), size_pad=0):
    q = bytearray(0x1a0)
    for o in range(0x1a0):
        q[o] = (o * 7 + 1) & 0x7f or 1
    for i in range(8):
        q[0x59 + 2 * i] = 0
    q[0x58] = 7
    q[0xc7] = 0x21
    q[0x19e], q[0x19f] = len(effects), len(items)
    for e in effects:
        q += bytes([e, 0, 0, 0xff, 0])
    for it in items:
        q += it
    return bytes(q) + bytes(size_pad)


def pack(rec):
    """PackBits, literals only: enough for unpack() to be exercised."""
    out = bytearray(struct.pack(">H", len(rec)))
    for i in range(0, len(rec), 100):
        chunk = rec[i:i + 100]
        out += bytes([len(chunk) - 1]) + chunk
    return bytes(out)


def library(members, magic=b"HLIB"):
    e = "<" if magic == b"HLIB" else ">"
    table = struct.pack(e + "H", len(members))
    blobs = []
    for index, (ident, rec) in enumerate(sorted(members.items()), 1):
        table += struct.pack(e + "HH", ident, index)
        blobs.append(pack(rec))
    body = [table] + blobs
    n = len(body)
    off = 16 + 4 * (n + 1)
    offs = []
    for b in body:
        offs.append(off)
        off += len(b)
    offs.append(off)
    head = magic + struct.pack(e + "L", off) + struct.pack(e + "HH", n, 1)
    head += b"DATA"
    return head + struct.pack(e + "%dL" % (n + 1), *offs) + b"".join(body)


def test_unpack_runs_and_literals():
    assert gbmon.unpack(bytes([0, 6, 0xfe, 9, 2, 1, 2, 3])) == bytes([9, 9, 9, 1, 2, 3])


@pytest.mark.parametrize("magic", [b"HLIB", b"GLIB"])
def test_library_round_trip_by_id(magic):
    a, b = record(), record(effects=(5,))
    got = gbmon.load_library(library({3: a, 77: b}, magic))
    assert got == {3: a, 77: b}


def test_every_field_lands_where_the_map_says():
    q = record()
    f, notes = gbmon.convert(q)
    assert len(f) == 450
    assert f[:0x58] == q[:0x58]
    assert f[0x58] == 6                                  # race: monster is 7 there
    assert [f[0x59 + i] for i in (0, 1, 3, 4, 5, 6)] == \
           [q[0x5a + 2 * i] for i in (0, 1, 3, 4, 5, 6)]
    assert f[0x5b] == 0                                  # the word FRUA has no use for
    assert f[0x60:0xbf] == q[0x68:0xc7]                  # name onward, eight bytes down
    assert f[0xbf] == 0x01                               # undead bit gone
    assert f[0xc0:0x14f] == q[0xc8:0x157]                # through the memorized spells
    assert f[0x14f:0x153] == bytes(4)                    # FRUA's four extra spell slots
    assert f[0x153:0x17e] == q[0x157:0x182]              # spellbook bits onward
    assert f[0x17e:0x18c] == q[0x18b:0x199]
    assert f[0x18c:] == bytes(450 - 0x18c)
    assert any("dropped" in n for n in notes)            # the bytes FRUA lacks were not empty


def test_giant_flag_is_set_from_the_two_size_bits():
    q = bytearray(record())
    q[0xc7] = 0x0c
    assert gbmon.convert(bytes(q))[0][0xbf] == 0x2c


def test_effects_fill_ten_slots_and_say_what_fell_off():
    f, notes = gbmon.convert(record(effects=range(1, 13)))
    assert list(f[0x1ae:0x1b8]) == list(range(1, 11))
    assert sum("effect" in n for n in notes) == 2


def test_items_become_item_table_numbers():
    table = bytearray(17 * 6)
    for k in range(6):
        table[k * 17] = 0x10 + k
        table[k * 17 + 3] = 0x10 + k
    index = gbmon.item_index(bytes(table))
    worn = bytearray(table[3 * 17:4 * 17])
    worn[10] = 1                                         # readied: not part of the match
    unknown = bytes([0x99] * 17)
    f, notes = gbmon.convert(record(items=(bytes(worn), unknown,
                                           bytes(table[5 * 17:6 * 17]))), index)
    assert list(f[0x18e:0x191]) == [3, 5, 0]
    assert notes.count("item type 153 left off") == 1


def test_design_files_are_named_by_the_games_own_number(tmp_path):
    n = gbmon.write_design(str(tmp_path), {4: record(), 300: record(), 91: record()})
    assert n == 2
    assert sorted(p.name for p in tmp_path.iterdir()) == ["MONST004.DAT", "MONST091.DAT"]
    assert (tmp_path / "MONST091.DAT").stat().st_size == 450


def _real():
    stock = os.path.join(os.path.dirname(__file__), "..", "data", "work",
                         "gamedata", "MONST.GLB")
    dq = glob.glob(os.path.expanduser(
        "~/.local/share/Steam/steamapps/common/*Krynn*/games/"
        "The Dark Queen of Krynn/GAME/DQK/DISK2/MONCHA.GLB"))
    return (stock, dq[0]) if os.path.exists(stock) and dq else None


@pytest.mark.skipif(_real() is None, reason="game data not present")
def test_real_shared_monsters_convert_to_fruas_own_record():
    stock_path, dq_path = _real()
    stock = gbmon.load_library(open(stock_path, "rb").read())
    dq = gbmon.load_library(open(dq_path, "rb").read())
    by_name = {gbmon.name_of(r, True).upper(): r for r in stock.values()}
    close = shared = 0
    for q in dq.values():
        ref = by_name.get(gbmon.name_of(q).upper())
        if ref is None:
            continue
        shared += 1
        got = gbmon.convert(q)[0]
        close += sum(got[o] != ref[o] for o in range(0x18e)) <= 3
    assert shared >= 25 and close >= 18


def test_native_keeps_the_games_race_numbers():
    """Under the Dark Queen's own rules a monster stays race 7 (ADR-0026)."""
    q = bytearray(record())
    assert gbmon.convert(bytes(q))[0][0x58] == 6
    assert gbmon.convert(bytes(q), native=True)[0][0x58] == 7
    q[0x58] = 6                                             # a human
    assert gbmon.convert(bytes(q))[0][0x58] == 5
    assert gbmon.convert(bytes(q), native=True)[0][0x58] == 6
