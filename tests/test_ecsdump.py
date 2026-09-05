"""ecsdump.py — the reader for the ECS backend's own state dump (#19).

The tool exists to say WHICH pixels the 32-colour cut got wrong and which
(band, index) mapping did it, in game coordinates. So what has to be right is
the lookup chain the backend itself performs —

    band = y * nbands // h;  slot = remap[band][idx];  rgb = band_pal[band][slot]

— and the run grouping that turns wrong pixels back into the horizontal runs
the hardware report describes. These build synthetic dumps where the answer is
known by construction, including a deliberately corrupted mapping, so a reader
that silently returns "nothing wrong" fails instead of looking clean.

No Pillow needed: only --png renders, and nothing here calls it.
"""
import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import ecsdump  # noqa: E402

W, H, NBANDS, NCOL = 320, 200, 25, 32
DEPTH, PITCH = 5, 40


def _dump(clut=None, band_pal=None, remap=None, chunky=None, seq=0):
    """Build a well-formed v1 dump; every section defaults to zeros."""
    clut = bytearray(clut if clut is not None else bytes(256 * 3))
    band_pal = bytearray(band_pal if band_pal is not None else bytes(NBANDS * NCOL * 3))
    remap = bytearray(remap if remap is not None else bytes(NBANDS * 256))
    chunky = bytearray(chunky if chunky is not None else bytes(W * H))
    hdr = b"ECSD" + struct.pack(">6H", 1, seq, W, H, NBANDS, NCOL) + bytes(16)
    return ecsdump.Dump(bytes(hdr + clut + band_pal + remap + chunky))


def _planes_from(chunky):
    """Encode SLOT values into 5 bitplanes — the c2p the backend runs.

    Five planes hold 0..31, i.e. SLOTS, never the 0..255 chunky index. Feeding
    chunky indices in here (and comparing against them) is exactly the mistake
    that reports ~99.9% of every frame as broken.
    """
    pl = bytearray(PITCH * H * DEPTH)
    for y in range(H):
        for x in range(W):
            v = chunky[y * W + x]
            for p in range(DEPTH):
                if v & (1 << p):
                    pl[p * PITCH * H + y * PITCH + (x >> 3)] |= 1 << (7 - (x & 7))
    return pl


def _dump2(chunky=None, planes=None, seq=0, front=0, remap=None):
    """A v2 dump: same as v1 plus the displayed bitplanes."""
    chunky = bytearray(chunky if chunky is not None else bytes(W * H))
    remap = bytearray(remap if remap is not None else bytes(NBANDS * 256))
    if planes is None:
        planes = _planes_from(bytes(W * H))
    hdr = (b"ECSD" + struct.pack(">6H", 2, seq, W, H, NBANDS, NCOL)
           + bytes([DEPTH, front]) + struct.pack(">H", PITCH) + bytes(12))
    body = (bytes(256 * 3) + bytes(NBANDS * NCOL * 3) + bytes(remap)
            + bytes(chunky) + bytes(planes))
    return ecsdump.Dump(bytes(hdr + body))


def _set_clut(clut, idx, rgb):
    clut[idx * 3:idx * 3 + 3] = bytes(rgb)


def _set_slot(pal, band, slot, rgb):
    o = (band * NCOL + slot) * 3
    pal[o:o + 3] = bytes(rgb)


def test_header_geometry_parses():
    d = _dump(seq=7)
    assert (d.seq, d.w, d.h, d.nbands, d.ncol) == (7, W, H, NBANDS, NCOL)


def test_bad_magic_is_rejected():
    with pytest.raises(ValueError):
        ecsdump.Dump(b"NOPE" + bytes(1000))


def test_truncated_dump_is_rejected():
    # A short read must raise, not silently slice garbage — a dump cut off by
    # a full disk would otherwise "analyse" fine and report nonsense.
    hdr = b"ECSD" + struct.pack(">6H", 1, 0, W, H, NBANDS, NCOL) + bytes(16)
    with pytest.raises(ValueError):
        ecsdump.Dump(hdr + bytes(100))


def test_band_of_is_eight_rows_per_band():
    d = _dump()
    assert d.band_of(0) == 0
    assert d.band_of(7) == 0
    assert d.band_of(8) == 1          # the boundary, which is where #15 lived
    assert d.band_of(H - 1) == NBANDS - 1


def test_lookup_chain_resolves_through_remap_into_band_pal():
    clut, pal, rem, chk = (bytearray(256 * 3), bytearray(NBANDS * NCOL * 3),
                           bytearray(NBANDS * 256), bytearray(W * H))
    _set_clut(clut, 5, (10, 20, 30))          # what index 5 SHOULD be
    rem[1 * 256 + 5] = 9                       # band 1 sends index 5 to slot 9
    _set_slot(pal, 1, 9, (200, 100, 50))       # and slot 9 holds this
    chk[8 * W + 3] = 5                         # y=8 is band 1
    d = _dump(clut, pal, rem, chk)
    assert d.want_rgb(5) == (10, 20, 30)
    assert d.shown_rgb(3, 8) == (200, 100, 50)
    # the same index one row UP is band 0, a different mapping entirely
    assert d.band_of(7) == 0


def test_clean_frame_reports_no_bad_mappings():
    """Sensitivity control: if a clean frame produced findings, every later
    'we found N bad mappings' would be noise."""
    clut, pal, rem, chk = (bytearray(256 * 3), bytearray(NBANDS * NCOL * 3),
                           bytearray(NBANDS * 256), bytearray(W * H))
    _set_clut(clut, 4, (120, 30, 30))
    for b in range(NBANDS):
        rem[b * 256 + 4] = 1
        _set_slot(pal, b, 1, (120, 30, 30))    # exact, every band
    for i in range(W * H):
        chk[i] = 4
    d = _dump(clut, pal, rem, chk)
    assert [r for r in ecsdump.pair_errors(d) if r["err"] > 0] == []
    assert ecsdump.runs(d, thresh=150) == []


def test_one_corrupt_mapping_is_found_and_named():
    # A red gradient index that band 3 alone resolves to beige — the exact
    # shape #19 describes.
    clut, pal, rem, chk = (bytearray(256 * 3), bytearray(NBANDS * NCOL * 3),
                           bytearray(NBANDS * 256), bytearray(W * H))
    _set_clut(clut, 4, (120, 30, 30))
    for b in range(NBANDS):
        rem[b * 256 + 4] = 1
        _set_slot(pal, b, 1, (120, 30, 30))
    _set_slot(pal, 3, 1, (240, 220, 190))      # band 3's slot 1 is beige
    for i in range(W * H):
        chk[i] = 4
    d = _dump(clut, pal, rem, chk)

    bad = [r for r in ecsdump.pair_errors(d) if r["err"] >= 150]
    assert len(bad) == 1
    assert bad[0]["band"] == 3 and bad[0]["idx"] == 4 and bad[0]["slot"] == 1
    assert bad[0]["shown"] == (240, 220, 190)
    assert bad[0]["pixels"] == W * 8           # band 3 is 8 full rows


def test_runs_are_horizontal_and_split_on_a_good_pixel():
    """The run grouping is the bit that has to match the report ('3-60 px
    horizontal runs'), so pin that a good pixel BREAKS a run rather than
    being swallowed into it."""
    clut, pal, rem, chk = (bytearray(256 * 3), bytearray(NBANDS * NCOL * 3),
                           bytearray(NBANDS * 256), bytearray(W * H))
    _set_clut(clut, 4, (120, 30, 30))          # bad index
    _set_clut(clut, 6, (10, 10, 10))           # good index
    for b in range(NBANDS):
        rem[b * 256 + 4] = 1
        rem[b * 256 + 6] = 2
        _set_slot(pal, b, 1, (240, 220, 190))  # index 4 -> beige everywhere
        _set_slot(pal, b, 2, (10, 10, 10))     # index 6 -> exact
    for i in range(W * H):
        chk[i] = 6
    for x in range(10, 15):
        chk[16 * W + x] = 4                    # a 5px run on row 16
    for x in range(20, 23):
        chk[16 * W + x] = 4                    # and a 3px run after a gap
    d = _dump(clut, pal, rem, chk)

    rr = ecsdump.runs(d, thresh=150)
    assert len(rr) == 2
    assert (rr[0]["y"], rr[0]["x0"], rr[0]["x1"], rr[0]["len"]) == (16, 10, 14, 5)
    assert (rr[1]["y"], rr[1]["x0"], rr[1]["x1"], rr[1]["len"]) == (16, 20, 22, 3)
    assert rr[0]["band"] == 2                  # y=16 -> band 2


def test_slot_usage_reports_free_slots():
    # Free slots are what ecs_ink_adopt_scan hands out, so "how many are
    # free" has to be answerable from a dump.
    d = _dump()
    used = ecsdump.slot_usage(d, band=0)
    assert list(used.keys()) == [0]            # all 256 indices map to slot 0
    assert len(used[0]) == 256


# -- v2: the bitplanes, which is where the cut and the SCREEN can disagree ----

def test_v2_header_carries_plane_geometry():
    d = _dump2()
    assert (d.version, d.depth, d.pitch, d.front) == (2, DEPTH, PITCH, 0)
    assert d.planes is not None


def test_v1_dump_still_loads_and_has_no_planes():
    d = _dump()
    assert d.version == 1 and d.planes is None
    with pytest.raises(ValueError):
        d.plane_idx_at(0, 0)


def test_plane_decode_is_the_inverse_of_the_c2p():
    """Round-trip: encode slots into planes, decode them back. A sign or
    bit-order error here would invent mismatches everywhere and read as a
    spectacular bug — so pin it before trusting any mismatch count."""
    slots = bytearray(W * H)
    for i, v in enumerate((1, 31, 16, 8, 4, 2, 7, 30)):
        slots[i] = v
    slots[5 * W + 319] = 21             # last pixel of a row, worst bit position
    d = _dump2(bytes(W * H), _planes_from(slots))
    for i, v in enumerate((1, 31, 16, 8, 4, 2, 7, 30)):
        assert d.plane_idx_at(i, 0) == v
    assert d.plane_idx_at(319, 5) == 21


def test_planes_are_compared_against_SLOTS_not_chunky_indices():
    """The regression guard for the mistake above: a frame whose chunky index
    is 200 everywhere and whose remap sends 200 -> slot 5, with planes holding
    5, is CORRECT. Comparing planes with the chunky index would call all 64000
    pixels wrong."""
    chunky = bytearray([200]) * (W * H)
    remap = bytearray(NBANDS * 256)
    for b in range(NBANDS):
        remap[b * 256 + 200] = 5
    slots = bytearray([5]) * (W * H)
    d = _dump2(chunky, _planes_from(slots), remap=remap)
    assert ecsdump.plane_mismatches(d) == []


def test_a_stale_plane_pixel_is_caught_and_grouped():
    # planes that disagree with the chosen slot over a 6px span on one row —
    # exactly the shape #19 shows on screen while the cut reads clean.
    chunky = bytearray([4]) * (W * H)
    remap = bytearray(NBANDS * 256)
    for b in range(NBANDS):
        remap[b * 256 + 4] = 3
    slots = bytearray([3]) * (W * H)
    for x in range(10, 16):
        slots[70 * W + x] = 29         # six stale pixels the cut never chose
    d = _dump2(chunky, _planes_from(slots), remap=remap)
    mm = ecsdump.plane_mismatches(d)
    assert len(mm) == 6
    assert all(m["slot"] == 3 and m["plane"] == 29 for m in mm)
    rr = ecsdump.plane_runs(mm, d)
    assert len(rr) == 1
    assert (rr[0]["y"], rr[0]["x0"], rr[0]["x1"], rr[0]["len"]) == (70, 10, 15, 6)
    assert rr[0]["band"] == 8           # y=70 -> band 8
