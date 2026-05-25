"""Tests for tools/dataemit.py — the DATA + DREL → C-header tool."""
import io
import struct

from dataemit import (
    _frsc_lookup,
    emit_c_header,
    emit_c_source,
    summary,
)
from datapool import (
    parse_data,
    parse_drel,
    RelocEntry,
    RELOC_BASE_A4,
    RELOC_BASE_A5,
)


def _build_frsc(resources):
    """Minimal FRSC archive builder for the lookup helper.

    `resources` is a list of (rtype: 4-byte bytes, rid: short, data: bytes).
    """
    header_len = 16
    entry_len  = 16
    n = len(resources)
    tbl_off = header_len
    data_off = tbl_off + entry_len * n

    out = bytearray()
    out += b"FRSC"
    out += struct.pack(">HHII", 1, n, tbl_off, 0)        # version, count, tbl_off, reserved

    bodies = bytearray()
    for rtype, rid, body in resources:
        off = data_off + len(bodies)
        out += rtype + struct.pack(">hHII", rid, 0, off, len(body))
        bodies += body
    out += bodies
    return bytes(out)


def test_frsc_lookup_pulls_data_and_drel():
    blob = _build_frsc([
        (b"DATA", 0, b"\x01\x02\x03\x04"),
        (b"DREL", 0, struct.pack(">2H", 0xFFFC, 0xFFFD)),
    ])
    assert _frsc_lookup(blob, b"DATA", 0) == b"\x01\x02\x03\x04"
    assert _frsc_lookup(blob, b"DREL", 0) == \
        struct.pack(">2H", 0xFFFC, 0xFFFD)


def test_emit_c_header_contains_size_and_count_macros():
    data_bytes = b"\x00" * 12
    table = parse_drel(struct.pack(">2H", 0xFFFC, 0xFFFD))
    buf = io.StringIO()
    emit_c_header(data_bytes, table, buf)
    text = buf.getvalue()
    assert "#define G_A5_INIT_BYTES_LEN 12" in text
    assert "#define G_A5_RELOCS_COUNT   2" in text
    assert "extern const unsigned char g_a5_init_bytes" in text
    assert "extern const g_a5_reloc_t g_a5_relocs" in text


def test_emit_c_source_emits_bytes_and_reloc_table():
    data_bytes = b"\x12\x34\x56\x78"
    table = parse_drel(struct.pack(">2H", 0xFFFC, 0xFFFD))
    buf = io.StringIO()
    emit_c_source(data_bytes, table, buf)
    text = buf.getvalue()
    # All four DATA bytes appear in hex form.
    assert "0x12" in text and "0x34" in text and "0x56" in text and "0x78" in text
    # Both reloc bases show.
    assert "G_A5_RELOC_A5" in text
    assert "G_A5_RELOC_A4" in text
    # The reloc table has the right number of entries.
    assert text.count(",\n") >= 2          # 4 data bytes + 2 reloc entries


def test_summary_reports_a5_a4_split():
    table = parse_drel(struct.pack(">3H", 0xFFE0, 0xFFE1, 0x8001))
    buf = io.StringIO()
    summary(b"\x00" * 4, table, buf)
    text = buf.getvalue()
    assert "DATA: 4 bytes" in text
    assert "DREL: 3 entries" in text
    assert "1 A5" in text
    assert "2 A4" in text
