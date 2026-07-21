"""xmi2slb — the DOS-XMI -> Mac MUSIC.SLB music converter.

Holds the synthesized bank to the contracts the ENGINE enforces
(jt975's bank reader, l11a2's song loader, jt974's pattern walker,
l0ff2's duration grid) and to the measured facts that pinned the
conversion (slot mapping, tick scale, period/tempo law). Skips when
the copyrighted DOS corpus is not staged under data/.
"""
import os
import struct
import sys

import pytest

sys.path.insert(0, "tools")
import xmi2slb  # noqa: E402

DOS_ROOT = "data/dos-frua"
MAC_MUSIC = "data/frua-mac/joined/Disk1/MUSIC.SLB"

pytestmark = pytest.mark.skipif(
    not os.path.isdir(DOS_ROOT),
    reason="DOS corpus not staged under data/",
)


@pytest.fixture(scope="module")
def bank():
    paths, pref = xmi2slb.find_xmi_set(DOS_ROOT)
    assert paths is not None, "no complete XMI set in the DOS corpus"
    assert pref == "TY"                       # Tandy preferred (3 voices)
    return xmi2slb.build_bank(
        {q: open(p, "rb").read() for q, p in paths.items()})


def parse_bank(d):
    assert d[:4] == b"SLBR"
    total, count, pad = struct.unpack(">IBB", d[4:10])
    assert total == len(d)
    assert pad == 0
    offs = struct.unpack(">%dH" % (count + 1), d[10:10 + (count + 1) * 2])
    base = 10 + (count + 1) * 2
    assert base + offs[count] == len(d)       # last offset = data size
    return [d[base + offs[i]:base + offs[i + 1]] for i in range(count)]


def walk_voice(pat):
    """jt974's exact pair walk -> (events, terminated, mac_ticks)."""
    events = []
    ticks = 0
    i = 0
    while i + 1 < len(pat):
        v, dur = pat[i], pat[i + 1]
        if v == 255:
            return events, True, ticks
        if v > 128 or (dur & 0x80):
            i += 2
            continue
        d = xmi2slb.l0ff2(dur & 0x3F)
        events.append((v, d, bool(dur & 0x40)))
        ticks += d
        i += 2
    return events, False, ticks


def test_bank_shape_and_slot_mapping(bank):
    songs = parse_bank(bank)
    assert len(songs) == 8                    # jt985 range = the Mac's 8
    for slot, q in enumerate(xmi2slb.SLOT_MAP):
        period, lvl, nv = struct.unpack(">HBB", songs[slot][:4])
        assert lvl == 127
        if q is None:                         # Mac-only slot -> empty song
            assert nv == 1
            assert songs[slot][14:16] == b"\xff\xff"
        else:
            assert nv == 3                    # the Tandy 3-voice reduction


def test_period_matches_the_mac_tempo_law(bank):
    songs = parse_bank(bank)
    # measured: Q1/Q2 run at 500000us/quarter -> the Mac's own 15360;
    # Q3 at 560747 -> the Mac song-7 period 13696 to the digit.
    assert struct.unpack(">H", songs[0][:2])[0] == 15360
    assert struct.unpack(">H", songs[1][:2])[0] == 15360
    assert struct.unpack(">H", songs[7][:2])[0] == 13696


def test_patterns_walk_clean_and_stay_in_sync(bank):
    songs = parse_bank(bank)
    for slot, q in enumerate(xmi2slb.SLOT_MAP):
        if q is None:
            continue
        s = songs[slot]
        nv = s[3]
        voffs = struct.unpack(">5H", s[4:14])
        ends = []
        for v in range(nv):
            start = 14 + voffs[v]
            end = 14 + voffs[v + 1] if v + 1 < nv else len(s)
            pat = s[start:end]
            # the Mac preamble, byte-exact
            assert pat[:4] == bytes((0x87, 0x00, 0x83, 0x00))
            assert pat[4] == 0x81 and pat[6:8] == bytes((0x82, 0x1A))
            events, terminated, ticks = walk_voice(pat)
            assert terminated, (slot, v)
            assert events, (slot, v)
            for note, d, tie in events:
                assert note == 128 or 0 < note < 128
                assert d > 0
            ends.append(ticks)
        # absolute-position quantization: the three voices end within a
        # couple of sequencer beats of one another (no accumulated drift)
        assert max(ends) - min(ends) < 2000, (slot, ends)


def test_timeline_matches_the_xmi_ideal(bank):
    """Each voice's emitted length ~= its channel's XMI extent * 112."""
    paths, _ = xmi2slb.find_xmi_set(DOS_ROOT)
    songs = parse_bank(bank)
    for slot, q in zip((0, 1, 7), (1, 2, 3)):
        notes, tempo = xmi2slb.parse_xmi(open(paths[q], "rb").read())
        by_ch = {}
        for t, ch, note, vel, dur in notes:
            if ch == 9:
                continue
            by_ch.setdefault(ch, []).append((t, note, dur))
        chans = sorted(by_ch, key=lambda c: -len(by_ch[c]))[:3]
        chans.sort()
        s = songs[slot]
        voffs = struct.unpack(">5H", s[4:14])
        for v, ch in enumerate(chans):
            mono = xmi2slb.monophonize(by_ch[ch])
            ideal = (mono[-1][0] + mono[-1][2]) * xmi2slb.XMI_TICK
            start = 14 + voffs[v]
            end = 14 + voffs[v + 1] if v + 1 < s[3] else len(s)
            _e, _t, ticks = walk_voice(s[start:end])
            assert abs(ticks - ideal) <= xmi2slb.GRID_MIN, (slot, v)


@pytest.mark.skipif(not os.path.isfile(MAC_MUSIC),
                    reason="Mac MUSIC.SLB not staged")
def test_header_layout_matches_the_mac_bank(bank):
    mac = open(MAC_MUSIC, "rb").read()
    assert mac[:4] == bank[:4] == b"SLBR"
    assert struct.unpack(">I", mac[4:8])[0] == len(mac)   # the size field law
    assert mac[8] == bank[8] == 8                          # 8 songs
