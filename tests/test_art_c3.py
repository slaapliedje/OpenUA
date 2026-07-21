"""The Mac-only method-3 (0xc3) composite codec — decoder validation.

SSI's Mac pipeline re-encoded 121 nested PIC*/SPRIT/TITLE animation frames
from DOS method 23 into the engine's jt1185 composite codec. c3_decode
(tools/art_convert.py) is the reference decoder, derived from the lifted
jt1185 blitter (CODE 4+0x94e); these tests hold it to the two properties
that PROVE the derivation on the whole base-game corpus:

  1. the stream model is exact — a single pass over the ctl stream for
     `height` rows consumes both streams to within the 1-byte even-pad;
  2. the content relation: the two codecs' grids anchor one column
     apart (c3 x == m23-planar x+1; the m23 sweep law's own +1 is pinned
     separately by FRAME 22-24 byte-identity), and under that shift
     every shared pixel matches on all 121 entries — SSI's re-encodes
     are content-identical, not retouched.

Skips when the copyrighted corpora are not staged under data/.
"""
import os
import struct
import sys

import pytest

sys.path.insert(0, "tools")
import art_convert as ac  # noqa: E402

MAC_ROOT = "data/frua-mac/joined"
DOS_ROOT = "data/dos-frua"

pytestmark = pytest.mark.skipif(
    not (os.path.isdir(MAC_ROOT) and os.path.isdir(DOS_ROOT)),
    reason="Mac/DOS base-game corpora not staged under data/",
)


def _find(root, suffix):
    out = {}
    for r, _d, files in os.walk(root):
        for f in files:
            if f.upper().endswith(suffix):
                out.setdefault(f.upper()[: -len(suffix)], os.path.join(r, f))
    return out


def _c3_pairs():
    macs = _find(MAC_ROOT, ".CTL")
    tlbs = _find(DOS_ROOT, ".TLB")
    for name in ("PICA", "PICB", "PICC", "PICD", "PICE", "SPRIT", "TITLE"):
        mc = ac.parse(open(macs[name], "rb").read())
        dc = ac.parse(open(tlbs[name], "rb").read())
        for i, (de, me) in enumerate(zip(dc["entries"], mc["entries"])):
            if me[:4] not in (b"GLIB", b"HLIB"):
                continue
            msub, dsub = ac.parse(me), ac.parse(de)
            for j, (ds, ms) in enumerate(zip(dsub["entries"], msub["entries"])):
                if len(ms) > 8 and ms[7] == 0xC3:
                    yield "%s[%d][%d]" % (name, i, j), ds, ms


def test_stream_model_is_exact():
    n = 0
    for label, _ds, ms in _c3_pairs():
        h = struct.unpack(">H", ms[0:2])[0]
        w = ms[6] * 8
        payload = ms[8:]
        ctl_len, stride = struct.unpack(">HH", payload[0:4])
        px, mask = ac.c3_decode(payload, w, h)
        # re-walk for consumption accounting
        ctl = payload[4 : 4 + ctl_len]
        pix = payload[4 + ctl_len :]
        ci = pi = 0
        for _y in range(h):
            while ci < len(ctl):
                c = ctl[ci]
                ci += 1
                if c == 0:
                    break
                nn = c & 0x3F
                if (c & 0xC0) == 0xC0:
                    pi += nn
                elif (c & 0xC0) == 0x80:
                    pi += 1
                elif (c & 0xC0) == 0x40:
                    ci += nn
                    pi += nn
        assert len(ctl) - ci <= 1, label      # even-pad only
        assert len(pix) - pi <= 1, label
        assert stride == len(pix), label      # word1 = per-plane pix stride
        n += 1
    assert n == 121


def test_content_identical_under_the_one_column_anchor():
    n = full_mask = 0
    for label, ds, ms in _c3_pairs():
        h = struct.unpack(">H", ms[0:2])[0]
        w = ms[6] * 8
        dh = struct.unpack("<H", ds[0:2])[0]
        dw = ds[6] * 4
        assert (dw, dh) == (w, h), label
        mpx, mmask = ac.c3_decode(ms[8:], w, h)
        dpx, dmask, _used = ac.m23_decode(ds[8:], w, h, planar=True)
        badpx = mask_mism = 0
        for y in range(h):
            for x in range(w - 1):
                k, s2 = y * w + x, y * w + x + 1
                if mmask[k] and dmask[s2] and mpx[k] != dpx[s2]:
                    badpx += 1
                if bool(mmask[k]) != bool(dmask[s2]):
                    mask_mism += 1
            if dmask[y * w]:
                mask_mism += 1
            if mmask[y * w + w - 1]:
                mask_mism += 1
        assert badpx == 0, (label, badpx)      # shared pixels identical
        assert mask_mism <= 20, (label, mask_mism)
        if mask_mism == 0:
            full_mask += 1
        n += 1
    assert n == 121
    assert full_mask >= 108
