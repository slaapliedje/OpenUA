"""tools/ecl.py — Gold Box ECL framing, on scripts assembled in the test.

No game data is read. What the real files established (zero undecodable bytes
and zero doubly-claimed bytes over all three Krynn games) is recorded in the
commit that added the module; these tests pin the rules that result rests on,
so a later edit to the operand table or the walk fails here first.
"""
import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import ecl                                                # noqa: E402
from ecl import GEN1, GEN1_DOK, GEN2, Block, EclError     # noqa: E402


# ---- a tiny assembler -----------------------------------------------------

def pack6(text):
    """The inverse of ecl.decode_string."""
    bits = "".join(format(ord(c) - 64 if "@" < c < "`" else ord(c), "06b")
                   for c in text) + "000000"
    bits += "0" * (-len(bits) % 8)
    return bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))


def imm(v):   return bytes([0x00, v])
def mem(a):   return bytes([0x01, a & 255, a >> 8])
def word(v):  return bytes([0x02, v & 255, v >> 8])


def text(s):
    p = pack6(s)
    return bytes([0x80, len(p)]) + p


def block(code, dialect, entries=None):
    """Header + code. `entries` are code-relative offsets for the five slots
    (default: all at the start of the code)."""
    entries = entries or [0] * 5
    start = dialect.vbase + dialect.header_len
    hdr = b"".join(struct.pack("<HH", 0x0101, start + e) for e in entries)
    if dialect.magic:
        hdr = struct.pack("<H", 0x1388) + hdr
    assert len(hdr) == dialect.header_len
    return hdr + code


def vaddr(dialect, code_offset):
    return dialect.vbase + dialect.header_len + code_offset


# ---- operands and strings -------------------------------------------------

def test_string_packing_round_trips():
    for s in ("HELLO", "X POSITION : 0-27", "A", ""):
        assert ecl.decode_string(pack6(s), 0, len(pack6(s))) == s


def test_operand_sizes():
    raw = block(bytes([0x03]) + imm(7) + mem(0x1234) + bytes([0x00]), GEN2)
    ins = ecl.decode_at(raw, GEN2.header_len, GEN2)
    assert (ins.mnemonic, ins.size) == ("COMPARE", 1 + 2 + 3)
    assert [(a.code, a.word) for a in ins.args] == [(0, 7), (1, 0x1234)]


def test_inline_string_is_part_of_the_instruction():
    code = bytes([0x12]) + text("WHERE DO YOU WISH TO GO?") + bytes([0x00])
    raw = block(code, GEN2)
    ins = ecl.decode_at(raw, GEN2.header_len, GEN2)
    assert ins.args[0].text == "WHERE DO YOU WISH TO GO?"
    assert raw[GEN2.header_len + ins.size] == 0x00        # lands on the EXIT


def test_a_truncated_instruction_does_not_decode():
    raw = block(bytes([0x03]) + imm(7), GEN2)             # COMPARE wants two
    assert ecl.decode_at(raw, GEN2.header_len, GEN2) is None
    assert ecl.decode_at(raw, len(raw), GEN2) is None


def test_an_undefined_opcode_does_not_decode():
    raw = block(bytes([0x7B]), GEN2)
    assert ecl.decode_at(raw, GEN2.header_len, GEN2) is None


# ---- count-driven opcodes -------------------------------------------------

def test_on_goto_reads_its_jump_table():
    # ON_GOTO [sel], #2, target0, target1 ; EXIT ; EXIT
    body = bytes([0x25]) + mem(0x10) + imm(2)
    t0 = len(body) + 3 + 3
    code = body + mem(vaddr(GEN2, t0)) + mem(vaddr(GEN2, t0 + 1)) + b"\x00\x00"
    b = Block(block(code, GEN2), GEN2)
    seen, halts = b.walk()
    assert not halts
    assert sorted(i.mnemonic for i in seen.values()) == ["EXIT", "EXIT", "ON_GOTO"]


def test_menu_items_are_counted_operands():
    code = (bytes([0x2b]) + mem(0x20) + imm(2) + text("YES") + text("NO")
            + bytes([0x00]))
    ins = ecl.decode_at(block(code, GEN2), GEN2.header_len, GEN2)
    assert [a.text for a in ins.args[2:]] == ["YES", "NO"]


# ---- the walk -------------------------------------------------------------

def test_if_guards_exactly_one_instruction():
    # COMPARE ; IF_EQ ; GOTO far ; PRINT ; EXIT ... far: EXIT
    head = bytes([0x03]) + imm(1) + imm(1) + bytes([0x16])
    tail = bytes([0x11]) + text("FALLTHROUGH") + bytes([0x00])
    far = len(head) + 4 + len(tail)
    code = head + bytes([0x01]) + mem(vaddr(GEN2, far)) + tail + bytes([0x00])
    seen, halts = Block(block(code, GEN2), GEN2).walk()
    assert not halts
    assert [seen[o].mnemonic for o in sorted(seen)] == [
        "COMPARE", "IF_EQ", "GOTO", "PRINT", "EXIT", "EXIT"]


def test_goto_does_not_fall_through_into_data():
    # GOTO over a run of bytes that are not instructions
    junk = bytes([0xFF, 0x7B, 0xFF])
    code = bytes([0x01]) + mem(vaddr(GEN2, 4 + len(junk))) + junk + bytes([0x00])
    seen, halts = Block(block(code, GEN2), GEN2).walk()
    assert not halts and len(seen) == 2


def test_gosub_returns_to_the_next_instruction():
    code = (bytes([0x02]) + mem(vaddr(GEN2, 5)) + bytes([0x00])   # GOSUB sub ; EXIT
            + bytes([0x13]))                                       # sub: RETURN
    seen, halts = Block(block(code, GEN2), GEN2).walk()
    assert [seen[o].mnemonic for o in sorted(seen)] == ["GOSUB", "EXIT", "RETURN"]


def test_a_target_in_another_block_is_not_followed():
    code = bytes([0x01]) + mem(0x0042) + bytes([0x00])
    seen, halts = Block(block(code, GEN2), GEN2).walk()
    assert not halts and [i.mnemonic for i in seen.values()] == ["GOTO"]


def test_each_entry_point_is_a_root():
    code = bytes([0x00, 0x3d, 0x00])          # EXIT | CLEAR_BOX ; EXIT
    b = Block(block(code, GEN2, entries=[1, 0, 0, 0, 0]), GEN2)
    seen, _ = b.walk()
    assert len(seen) == 3
    assert "vmRun1:" in b.listing()


# ---- dialects -------------------------------------------------------------

def test_gen1_header_has_the_magic_word_and_its_own_base():
    b = Block(block(bytes([0x00]), GEN1), GEN1)
    assert b.offset_of(b.entries[4]) == 22
    with pytest.raises(EclError):
        Block(block(bytes([0x00]), GEN2) + b"\x00\x00", GEN1)     # no magic


def test_parlay_is_six_operands_in_champions_and_one_after():
    # The dominant desync before the tables were split: PARLAY followed
    # directly by COMBAT. Read with the wrong count it swallows the COMBAT.
    code = bytes([0x2c]) + imm(35) + bytes([0x24, 0x00])
    for d in (GEN1_DOK, GEN2):
        seen, halts = Block(block(code, d), d).walk()
        assert [seen[o].mnemonic for o in sorted(seen)] == ["PARLAY", "COMBAT", "EXIT"]
    assert ecl.decode_at(block(code, GEN1), GEN1.header_len, GEN1) is None


def test_death_knights_differs_from_dark_queen_only_where_measured():
    assert GEN1_DOK.opcodes[0x21] == GEN1.opcodes[0x21]           # LOAD_FILES: 3
    assert GEN2.opcodes[0x21][1] == 2
    assert GEN1_DOK.opcodes[0x27] == GEN2.opcodes[0x27]           # TREASURE: 4+count


def test_misframing_is_zero_when_right_and_not_when_wrong():
    # Champions' six-operand PARLAY. Its second operand is a game variable
    # (code 0x7F), so a reader that stops after one operand takes 0x7F for the
    # next opcode — and there is no such opcode.
    code = (bytes([0x2c]) + imm(1) + bytes([0x7F, 5]) + imm(3) + imm(4) + imm(5)
            + imm(6) + bytes([0x24, 0x00]))
    assert ecl.misframing([Block(block(code, GEN1), GEN1)]) == 0
    assert ecl.misframing([Block(block(code, GEN1_DOK), GEN1_DOK)]) > 0


# ---- containers -----------------------------------------------------------

def dax(blocks):
    """A DAX archive, compressed as literal runs only."""
    toc, body = b"", b""
    for bid, raw in blocks.items():
        comp = b"".join(bytes([len(raw[i:i + 128]) - 1]) + raw[i:i + 128]
                        for i in range(0, len(raw), 128))
        toc += struct.pack("<BIHH", bid, len(body), len(raw), len(comp))
        body += comp
    return struct.pack("<H", len(toc)) + toc + body


def test_dax_archive_and_repeat_runs():
    raw = block(bytes([0x00]), GEN1)
    assert ecl.dax_blocks(dax({34: raw, 16: raw})) == {34: raw, 16: raw}
    # a hand-made repeat run: 0xFD = repeat the next byte 3 times
    arch = struct.pack("<H", 9) + struct.pack("<BIHH", 1, 0, 3, 2) + bytes([0xFD, 0x41])
    assert ecl.dax_blocks(arch) == {1: b"AAA"}
    with pytest.raises(EclError):
        ecl.dax_blocks(struct.pack("<H", 9) + struct.pack("<BIHH", 1, 0, 9, 2) + b"\xFD\x41")


def test_a_dax_archive_gets_the_dialect_that_frames_it():
    dok = bytes([0x2c]) + imm(35) + bytes([0x24, 0x00])
    cok = (bytes([0x2c]) + imm(1) + bytes([0x7F, 5]) + imm(3) + imm(4) + imm(5)
           + imm(6) + bytes([0x24, 0x00]))
    assert ecl.load_blocks(dax({1: block(dok, GEN1)}))[0].dialect is GEN1_DOK
    assert ecl.load_blocks(dax({1: block(cok, GEN1)}))[0].dialect is GEN1
    assert ecl.load_blocks(dax({1: block(cok, GEN1)}), GEN1_DOK)[0].dialect is GEN1_DOK


def test_ecl_library_is_keyed_by_area_id():
    from test_gbgeo import library
    a = block(bytes([0x00]), GEN2)
    b = block(bytes([0x3d, 0x00]), GEN2)
    table = struct.pack("<H", 2) + struct.pack("<HHHH", 34, 2, 4, 1)
    blocks = ecl.load_blocks(library(b"HLIB", [table, a, b]))
    assert [(x.block_id, len(x.raw)) for x in blocks] == [(34, 22), (4, 21)]
    assert all(x.dialect is GEN2 for x in blocks)
