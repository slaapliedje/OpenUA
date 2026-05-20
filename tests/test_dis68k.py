"""Tests for the 68k disassembler's analysis logic (tools/dis68k.py).

Covers the parts that need no toolchain -- jump-table parsing and the
instruction annotator. The objdump-driven path is exercised by running the
tool over the real binary; see docs/decompilation.md.
"""
import dis68k

from fixtures import build_code0, build_crel


def test_parse_jump_table_adds_segment_header():
    # Routine offsets are code-relative; parse_jump_table adds the 4-byte
    # segment header to recover resource-relative entry addresses.
    code0 = build_code0(0x20, [(1, 0x000), (6, 0x586), (7, 0x1000)])
    jt_off, jt = dis68k.parse_jump_table(code0)
    assert jt_off == 0x20
    assert jt == [(1, 0x004), (6, 0x58A), (7, 0x1004)]


def test_annotate_resolves_jump_table_call():
    jt_off, jt = 0x20, [(7, 0x1004), (10, 0x2000)]
    # A JT call targets entry+2: disp = jt_off + 2 + 8*index.
    disp = jt_off + 2 + 8 * 1
    _ops, comment = dis68k.annotate(0, "4ead0000", "jsr", f"%a5@({disp})",
                                    0x4000, jt_off, jt, set())
    assert "CODE 10+0x2000" in comment
    assert "JT[1]" in comment


def test_annotate_names_a_line_trap():
    _ops, comment = dis68k.annotate(0, "a9f0", ".short", "0xa9f0",
                                    0x4000, 0x20, [], set())
    assert comment == "trap _LoadSeg"


def test_annotate_labels_intra_segment_branch():
    labels = set()
    ops, _comment = dis68k.annotate(0x10, "6004", "bras", "0x16",
                                    0x2000, 0x20, [], labels)
    assert 0x16 in labels
    assert "L0016" in ops


def test_annotate_flags_a5_global():
    _ops, comment = dis68k.annotate(0, "2d40ff00", "movel", "%d0,%a5@(-256)",
                                    0x4000, 0x20, [], set())
    assert "A5 global -256" in comment


def test_parse_crel_splits_offset_and_base():
    blob = build_crel([(0x10, True), (0x420, False), (0x100, True)])
    assert dis68k.parse_crel(blob) == {0x10: True, 0x420: False, 0x100: True}


def test_reloc_note_resolves_string_pool_reference():
    # a 32-bit immediate (value 0x0a) sits at offset 2 of a 6-byte instruction
    data = b"\x41\xf9" + (0x0A).to_bytes(4, "big")
    strs = b"." * 0x0A + b"HELLO\x00"
    note = dis68k.reloc_note({2: True}, strs, data, addr=0, insn_len=6)
    assert "STRS+0xa" in note
    assert "HELLO" in note


def test_reloc_note_a5_reference_has_no_string():
    data = b"\x41\xf9" + (0x1234).to_bytes(4, "big")
    note = dis68k.reloc_note({2: False}, None, data, addr=0, insn_len=6)
    assert note == "reloc A5+0x1234"
