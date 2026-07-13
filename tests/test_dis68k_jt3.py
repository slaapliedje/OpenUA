"""JT[3] table-detection tests for tools/dis68k.py.

The detector is the runtime sibling of tools/jt3_extract.py — it spots
`jsr JT[3]` in the disassembled stream and decodes the inline table.
These tests exercise the table-decode primitive (decode_jt3_table), the
site predicate (is_jt3_jsr), and the resyncing stream walker
(resync_stream) over a synthetic blob; the listing-writer integration
is covered indirectly by the real disassembly being re-runnable
against the FRUA fork.
"""
import struct

from dis68k import decode_jt3_table, is_jt3_jsr, resync_stream


def _l12a0_blob():
    """Build a blob whose layout matches CODE 12's L12a0 dispatcher:
    a `jsr JT[3]` at 0x12e6 followed by the inline table at 0x12ea
    (min=0, max=2, default -> 0x1306, cases -> 0x12f6 / 0x12fe / 0x15de).
    """
    blob = bytearray(0xff for _ in range(0x1700))
    # JSR to JT[3] at 0x12e6. The mnem doesn't matter to the decoder,
    # only the (signed) shorts of the table at 0x12ea do.
    struct.pack_into(">3h", blob, 0x12ea, 0, 2, 0x18)             # min, max, default
    struct.pack_into(">3h", blob, 0x12f0, 0x06, 0x0c, 0x02ea)     # case 0/1/2
    return bytes(blob)


def test_decode_returns_l12a0_arms():
    blob = _l12a0_blob()
    decoded = decode_jt3_table(blob, 0x12ea)
    assert decoded is not None
    table_end, min_c, max_c, default_target, case_targets = decoded
    assert (min_c, max_c) == (0, 2)
    assert default_target == 0x1306
    assert case_targets == [0x12f6, 0x12fe, 0x15de]
    assert table_end == 0x12f6              # 6 prefix + 3*2 case bytes


def test_decode_rejects_implausible_bounds():
    """A wrong table address typically lands on garbage that decodes
    as huge min..max; the helper bails so the row stream isn't
    contaminated with bogus skip ranges."""
    blob = struct.pack(">3h", 0, 5000, 0) + b"\x00" * 32
    assert decode_jt3_table(blob, 0) is None


def test_decode_rejects_max_below_min():
    blob = struct.pack(">3h", 7, 3, 0) + b"\x00" * 16
    assert decode_jt3_table(blob, 0) is None


def test_decode_rejects_table_past_end():
    blob = struct.pack(">3h", 0, 10, 0)     # 11 cases needed, only 6 bytes left
    assert decode_jt3_table(blob, 0) is None


def test_decode_handles_negative_offsets():
    """Case offsets are signed shorts — arms before the table address
    resolve to lower addresses without underflow."""
    blob = bytearray(b"\x00" * 0x400)
    struct.pack_into(">3h", blob, 0x200, 0, 0, -0x80)   # default at 0x184
    struct.pack_into(">h",  blob, 0x206, -0x10)         # case 0 at 0x1f6
    decoded = decode_jt3_table(bytes(blob), 0x200)
    assert decoded is not None
    _, _, _, default_target, case_targets = decoded
    assert default_target == 0x184
    assert case_targets == [0x1f6]


def test_is_jt3_jsr_matches_only_slot_3():
    """A JSR through a non-JT[3] jump-table slot looks similar but
    doesn't carry the inline-switch table — the predicate must skip it.
    With jt_off=32, JT[3]'s call displacement is 32 + 2 + 8*3 = 58."""
    assert is_jt3_jsr("jsr", "%a5@(58)", 32)
    assert not is_jt3_jsr("jsr", "%a5@(64)", 32)     # JT[3.75] isn't a slot
    assert not is_jt3_jsr("jsr", "%a5@(66)", 32)     # JT[4]
    assert not is_jt3_jsr("pea", "%a5@(58)", 32)     # not a call
    assert not is_jt3_jsr("jsr", "%a1@(58)", 32)     # not through A5


def test_resync_walks_stream_and_decodes_tables():
    """The walker spots `jsr JT[3]` sites and decodes the table at the
    instruction's end address."""
    blob = _l12a0_blob()

    def dis(start):
        if start == 4:                       # NEAR_HEADER
            return [
                (0x12e6, "4ead003a", "jsr", "%a5@(58)"),
                # the table bytes 0x12ea..0x12f6 decode as garbage; the
                # walker restarts at table_end regardless, so these rows
                # must vanish from the stream:
                (0x12ea, "00000002", "orib",  "#2,%d0"),
                (0x12ee, "00180006", "orib",  "#6,%a0@+"),
                (0x12f2, "000c02ea", "orib",  "#42,%a2@"),
            ]
        assert start == 0x12f6               # the restart point
        return [(0x12f6, "4e75", "rts", "")]

    rows, tables = resync_stream(blob, 32, dis)
    assert 0x12ea in tables
    table_end, min_c, max_c, default_target, case_targets = tables[0x12ea]
    assert (min_c, max_c) == (0, 2)
    assert default_target == 0x1306
    assert case_targets == [0x12f6, 0x12fe, 0x15de]
    # the garbage table rows are gone; the stream resumes at table_end
    assert [r[0] for r in rows] == [0x12e6, 0x12f6]


def test_resync_restarts_after_straddling_table():
    """THE jt433 bug: a garbage 'instruction' straddles the table's end
    and eats the first real code bytes after it. One linear pass never
    resyncs — CODE 3's `4eba fe8e` (`jsr L4854`, the form-feed page
    close) listed as a stray `.short 0xfe8e`. resync_stream must restart
    the decode at table_end and return the REAL instruction."""
    blob = _l12a0_blob()

    def dis(start):
        if start == 4:
            return [
                (0x12e6, "4ead003a", "jsr", "%a5@(58)"),
                # garbage decode of the table bytes: the last one starts
                # INSIDE the table (0x12f2) but is 6 bytes long — it
                # swallows 0x12f6..0x12f8, the real jsr's opcode word.
                (0x12ea, "00000002",     "orib", "#2,%d0"),
                (0x12ee, "00180006",     "orib", "#6,%a0@+"),
                (0x12f2, "000c02ea4eba", "cmpib", "#-70,%a2@(19130)"),
                (0x12f8, "fe8e",         ".short", "0xfe8e"),
                (0x12fa, "4e75",         "rts", ""),
            ]
        assert start == 0x12f6               # the resync point
        return [
            (0x12f6, "4ebafe8e", "jsr", "%pc@(0x1186)"),
            (0x12fa, "4e75",     "rts", ""),
        ]

    rows, tables = resync_stream(blob, 32, dis)
    assert 0x12ea in tables
    # the straddling garbage and the stray .short are gone; the real
    # jsr at table_end made it into the stream
    assert [(r[0], r[2]) for r in rows] == \
        [(0x12e6, "jsr"), (0x12f6, "jsr"), (0x12fa, "rts")]
