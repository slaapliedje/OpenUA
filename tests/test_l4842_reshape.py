"""l4842 (editor map resize) — byte-exact validation of the re-lifted reshape.

The C in src/engine/boot.c (l4842, re-lifted 2026-07-24 from the CODE 2 asm)
re-spaces the 6-byte cell rows of the -12300 design record when the map dims
change.  Argument truth: rr/cc are the OLD dims; base[2]/base[3] already hold
the NEW dims when it runs.

This test mirrors the C's exact operation sequence — pointer inits, copy
order, per-call lengths — with jt406 as BlockMove (memmove semantics: a
single call is overlap-safe, but ORDER between calls is not) and jt399 as
memset.  It then asserts the whole buffer equals the faithful expected state:

  - surviving cell (i, j) of the old grid lands at new-stride position (i, j);
  - every non-cell byte inside the OLD extent is zero (vacated gaps, dropped
    rows, the compacted tail);
  - on a width GROW, rows spill past the old extent: the vacated tail gaps of
    rows 0..lim-2 are zeroed wherever they fall, but row lim-1's own tail gap
    is NOT zeroed (a faithful Mac quirk — the loop zeroes each gap one row
    behind the copy and stops after row 1), so bytes there keep their prior
    container contents;
  - nothing else outside the old extent is touched.

The pre-fix lift fails these properties (inverted dst/src per the jt406
banner: Mac JT[406](A, B, n) is src-first, the lifted jt406 is dst-first) —
the hazard widths below (oldW < 2*newW and newW < 2*oldW) are the overlapping
cases the 2026-07-14 audit flagged.

KEEP IN SYNC with l4842's reshape branches; this is a 1:1 transcription.
"""

HDR = 290           # map cells start at base+290
CELL = 6            # 6 bytes per cell


def _floor6(n):
    return (n // 6) * 6


def reshape_c(buf, old_h, old_w, new_h, new_w):
    """The re-lifted l4842 reshape, transcribed 1:1 (scan phase omitted —
    it only reads).  buf is a bytearray holding the whole record."""
    rr, cc = old_h, old_w           # params
    b2, b3 = new_h, new_w           # base[2] / base[3] (already stored)

    if b2 >= rr and b3 == cc:
        return

    lim = min(rr, b2)               # jt413
    delta = (cc - b3) * CELL
    endp = HDR + rr * cc * CELL

    def jt406(dst, src, n):         # BlockMove: memmove semantics
        buf[dst:dst + n] = bytes(buf[src:src + n])

    def jt399(dst, n):              # zero fill
        buf[dst:dst + n] = bytes(n)

    if delta > 0:
        # width SHRANK: compact front-to-back, keep first newW columns
        src = HDR + cc * CELL
        dst = HDR + b3 * CELL
        stride = b3 * CELL
        for _ in range(1, lim):
            jt406(dst, src, stride)
            src += cc * CELL
            dst += b3 * CELL
        jt399(dst, _floor6(endp - dst))
    elif delta < 0:
        # width GREW: zero dropped rows, then re-space back-to-front
        delta = -delta
        src = HDR + lim * cc * CELL
        jt399(src, _floor6(endp - src))
        src -= cc * CELL
        dst = HDR + (lim - 1) * b3 * CELL
        stride = cc * CELL
        for _ in range(lim - 1, 0, -1):
            jt406(dst, src, stride)
            jt399(dst - delta, delta)
            src -= cc * CELL
            dst -= b3 * CELL
    else:
        # width unchanged (height shrank): zero the freed tail
        dst = HDR + b2 * b3 * CELL
        if dst < endp:
            jt399(dst, _floor6(endp - dst))


def _cell_bytes(i, j):
    return bytes(1 + (i * 31 + j * 7 + k) % 251 for k in range(CELL))


def _make_grid(old_h, old_w, size):
    """A record whose every cell byte is unique and nonzero; header is an
    0xEE sentinel, everything past the old cell data is 0xDD."""
    buf = bytearray([0xDD]) * size
    buf[:HDR] = bytes([0xEE]) * HDR
    for i in range(old_h):
        for j in range(old_w):
            off = HDR + (i * old_w + j) * CELL
            buf[off:off + CELL] = _cell_bytes(i, j)
    return buf


def _expected(ref, old_h, old_w, new_h, new_w):
    """The faithful post-reshape state, constructed independently of the
    operation sequence (see the module docstring for the contract)."""
    lim = min(old_h, new_h)
    old_end = HDR + old_h * old_w * CELL
    exp = bytearray(ref)

    if new_h >= old_h and new_w == old_w:
        return exp                                  # early-out: untouched

    for off in range(HDR, old_end):                 # old extent vacated
        exp[off] = 0
    for i in range(lim):                            # surviving cells
        for j in range(min(old_w, new_w)):
            dst = HDR + (i * new_w + j) * CELL
            exp[dst:dst + CELL] = _cell_bytes(i, j)
    if new_w > old_w:
        # tail gaps of rows 0..lim-2 are zeroed wherever they fall; row
        # lim-1's gap is not (the Mac quirk), so past-old-extent bytes
        # there keep the container contents (0xDD in this harness).
        gap = (new_w - old_w) * CELL
        for i in range(lim - 1):
            g = HDR + (i * new_w + old_w) * CELL
            exp[g:g + gap] = bytes(gap)
    return exp


def _check(old_h, old_w, new_h, new_w):
    lim = min(old_h, new_h)
    size = HDR + CELL * max(old_h * old_w, lim * new_w + old_w) + 64
    buf = _make_grid(old_h, old_w, size)
    exp = _expected(bytes(buf), old_h, old_w, new_h, new_w)
    reshape_c(buf, old_h, old_w, new_h, new_w)
    if bytes(buf) != bytes(exp):
        diffs = [(k, buf[k], exp[k]) for k in range(size)
                 if buf[k] != exp[k]][:8]
        raise AssertionError(
            "%dx%d->%dx%d mismatch at (off, got, want): %s"
            % (old_h, old_w, new_h, new_w,
               [(k - HDR, hex(g), hex(w)) for k, g, w in diffs]))


def test_width_shrink_hazard():
    # the audit's hazard case: newW < oldW < 2*newW (overlapping strides)
    _check(19, 19, 19, 10)


def test_width_grow_hazard():
    # oldW < newW < 2*oldW — the widening overlap case
    _check(10, 10, 10, 19)


def test_width_shrink_extreme():
    _check(8, 16, 8, 3)


def test_width_grow_extreme():
    _check(8, 3, 8, 16)


def test_height_shrink_same_width():
    _check(19, 12, 7, 12)


def test_both_shrink():
    _check(19, 19, 7, 9)


def test_shrink_h_grow_w():
    _check(16, 8, 9, 14)


def test_grow_h_shrink_w():
    _check(9, 14, 16, 8)


def test_noop_pure_height_grow():
    size = HDR + CELL * 10 * 10 + 64
    buf = _make_grid(10, 10, size)
    ref = bytes(buf)
    reshape_c(buf, 10, 10, 15, 10)   # early-out: newH >= oldH, same width
    assert bytes(buf) == ref


def test_minimum_dims():
    _check(2, 2, 1, 1)
    _check(1, 2, 1, 1)


def test_matrix_sweep():
    # a broad matrix of small cases — any pointer slip shows up here
    for old_h in (1, 2, 3, 5):
        for old_w in (1, 2, 3, 5, 8):
            for new_h in (1, 2, 3, 5):
                for new_w in (1, 2, 3, 5, 8):
                    if new_h >= old_h and new_w == old_w:
                        continue    # early-out, covered above
                    _check(old_h, old_w, new_h, new_w)
