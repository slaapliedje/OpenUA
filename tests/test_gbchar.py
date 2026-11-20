"""tools/gbchar.py — a Dark Queen .QCH as FRUA's .CCH stream.

Every record here is built in the test; no game data is read. The real-data
check (the six heroes the Steam release ships, added in the Training Hall
and shown on their sheets in Hatari) is recorded in the module docstring.
"""
import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import gbchar                                             # noqa: E402


def qch(name=b"WALKER KEYS", race=5, levels=(10, 0, 0, 0, 0, 0, 12),
        items=(), nodes=(), mask=6):
    q = bytearray(gbchar.QCH_FIXED)
    q[0:12] = b"\x11" * 12                     # live pointers in the file
    q[8] = len(items)
    q[0x44:0x48] = struct.pack("<L", 500000)   # XP
    q[0x4c:0x4e] = struct.pack("<H", 200)      # money: steel/platinum
    q[0x58] = race
    q[0x68:0x68 + len(name)] = name
    q[0xa5:0xac] = bytes(levels)
    q[0xbf] = mask
    q[0xc9] = len(items)
    q[0x157] = 0xb5                            # first spellbook byte
    for it in items:
        q += bytes(it)
    for k, d in enumerate(nodes):
        q += bytes(d) + (b"\0" * 4 if k == len(nodes) - 1 else b"\x1b\0\x71\x58")
    return bytes(q)


ITEM_A = bytes([0x43, 0, 0x70, 0x7e, 0x14, 0, 0xca, 8, 2, 0, 1, 0, 0, 0, 0, 0x83, 0x80])
ITEM_B = bytes([0x23, 0x70, 0x2f, 0x23, 0x5e, 1, 0xac, 0xd, 2, 0, 1, 0, 0, 0, 0, 0, 0])


def test_the_file_splits_into_fixed_part_items_and_nodes():
    q = qch(items=[ITEM_A, ITEM_B], nodes=[[0x5c, 0, 0, 0xff, 0], [0x48, 0, 0, 0xff, 0]])
    fixed, items, nodes = gbchar.parse_qch(q)
    assert len(fixed) == 0x19e and items == [ITEM_A, ITEM_B]
    assert nodes == [bytes([0x5c, 0, 0, 0xff, 0]), bytes([0x48, 0, 0, 0xff, 0])]


def test_a_length_that_does_not_split_is_refused():
    with pytest.raises(gbchar.GbCharError):
        gbchar.parse_qch(qch(items=[ITEM_A]) + b"\0")


def test_the_cch_stream_is_what_jt577_reads():
    q = qch(items=[ITEM_A, ITEM_B], nodes=[[0x5c, 0, 1, 0xff, 2], [0x48, 0, 0, 0xff, 0]])
    cch, notes = gbchar.convert(q)
    rec = cch[:398]
    assert len(cch) == 398 + 2 * 18 + 2 * 10
    assert rec[0:4] == bytes(4)                            # party link
    assert struct.unpack("<L", rec[4:8])[0] != 0           # effects follow
    assert struct.unpack("<L", rec[8:12])[0] != 0          # items follow
    assert rec[12:68] == bytes(56)                         # equip slots rebuilt
    assert rec[96:107] == b"WALKER KEYS"                   # name, eight down
    assert rec[88] == 4                                    # kender -> halfling
    assert rec[157:164] == bytes([10, 0, 0, 0, 0, 0, 12])  # class levels
    assert rec[183] == 6                                   # class bits as-is
    assert rec[193] == 2                                   # item count
    assert struct.unpack("<L", rec[68:72])[0] == 500000    # XP, little-endian
    assert struct.unpack("<H", rec[76:78])[0] == 200       # platinum
    assert rec[339] == 0xb5                                # spellbook bits
    assert cch[398:416] == ITEM_A + b"\0"                  # 17 bytes + a zero
    assert cch[416:434] == ITEM_B + b"\0"
    n1, n2 = cch[434:444], cch[444:454]
    assert n1[:6] == bytes([0x5c, 0, 0, 1, 0xff, 2])       # pad after the id
    assert struct.unpack("<L", n1[6:])[0] != 0             # more follow
    assert n2[:6] == bytes([0x48, 0, 0, 0, 0xff, 0])
    assert n2[6:] == bytes(4)                              # last one


def test_no_lists_means_zero_flags():
    cch, _ = gbchar.convert(qch())
    assert len(cch) == 398 and cch[4:12] == bytes(8) and cch[193] == 0


def test_a_container_is_left_off_and_counted_out():
    box = bytes([73]) + bytes(16)
    cch, notes = gbchar.convert(qch(items=[box, ITEM_B]))
    assert cch[193] == 1 and len(cch) == 398 + 18
    assert any("container" in n for n in notes)


def test_a_knight_stays_in_slot_one_unless_asked():
    q = qch(name=b"ROLAND OF DUST", race=6, levels=(0, 11, 0, 0, 0, 0, 0), mask=16)
    cch, notes = gbchar.convert(q)
    assert cch[157:164] == bytes([0, 11, 0, 0, 0, 0, 0]) and cch[183] == 16
    assert any("druid" in n for n in notes)
    cch, _ = gbchar.convert(q, knight="fighter")
    assert cch[157:164] == bytes([0, 0, 11, 0, 0, 0, 0]) and cch[183] == 8


def test_file_names_follow_frua():
    assert gbchar.file_stem("ROARK 'CROAKER'") == "ROARK'CR"
    assert gbchar.file_stem("BALTHIR NOMOON") == "BALTHIRN"
    assert gbchar.file_stem("A.B:C") == "ABC"
    assert gbchar.file_stem("  ") == "CHAR"


def test_under_the_games_own_rules_race_and_knight_are_kept():
    """A design holding DQK.EXE plays by the Dark Queen's rules (ADR-0026):
    a kender stays race 5, a human 6, and a knight's levels stay in slot 1
    with no druid note, whatever --knight says."""
    q = qch(name=b"ROLAND OF DUST", race=6, levels=(0, 11, 0, 0, 0, 0, 0), mask=16)
    cch, notes = gbchar.convert(q, knight="fighter", native=True)
    assert cch[88] == 6
    assert cch[157:164] == bytes([0, 11, 0, 0, 0, 0, 0]) and cch[183] == 16
    assert not any("druid" in n for n in notes)
    assert gbchar.convert(qch(race=5), native=True)[0][88] == 5
    assert gbchar.convert(qch(race=5))[0][88] == 4          # FRUA's halfling


def test_the_design_decides_native(tmp_path, monkeypatch, capsys):
    src = tmp_path / "WALKERKE.QCH"
    src.write_bytes(qch(race=5))
    dsn = tmp_path / "OUT.DSN"
    dsn.mkdir()
    gbchar.main([str(src), "--design", str(dsn)])
    assert (dsn / "SAVE" / "WALKERKE.CCH").read_bytes()[88] == 4
    (dsn / "DQK.EXE").write_bytes(b"MZ")
    gbchar.main([str(src), "--design", str(dsn)])
    assert (dsn / "SAVE" / "WALKERKE.CCH").read_bytes()[88] == 5
