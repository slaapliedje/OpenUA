#!/usr/bin/env python3
"""xmi2slb — build a Mac-format MUSIC.SLB from the DOS release's XMI music.

The DOS release ships FRUA's soundtrack as XMIDI files in four DRIVER
arrangements (the file prefix is the sound driver, not the tune):

    ADDQ?.XMI  AdLib          (multi-channel + GM drums on ch 9)
    RODQ?.XMI  Roland MT-32   (note-identical to ADDQ)
    TYDQ?.XMI  Tandy 3-voice  (three monophonic channels 1..3)
    PCDQ?.XMI  PC speaker     (one channel)

Each driver carries the same THREE songs Q1/Q2/Q3. The Mac release's
MUSIC.SLB holds EIGHT songs for its 4-tone synth; melody-trigram
correlation against the decoded Mac patterns places the DOS songs at
Mac slots (data measured 2026-07-21, TYDQ vs MUSIC.SLB):

    Q1 -> slot 0   the title / Training-Hall theme (129/201 trigrams)
    Q2 -> slot 1   the short jingle                 (20/27)
    Q3 -> slot 7   the big theme                    (69/107)

Slots 2..6 are Mac-only compositions with no DOS source; they are
written as EMPTY songs (a lone 0xFF terminator) so jt985's range check
passes and a design event requesting them plays silence instead of
popping "Song out of range".

The Tandy arrangement is the conversion source of choice: SSI's own
musicians already reduced each tune to three monophonic square-wave
voices — exactly the shape the Mac sequencer (jt974) wants.

## The .slb bank format (from jt975/l11a2/jt974, verified against the
## Mac MUSIC.SLB)

    bank:   'SLBR' u32_file_size u8_song_count u8_pad
            (count+1) x u16 song offsets (relative to the end of the
            offset table; entry [count] = total data size)
            song data, back to back
    song:   u16 period  u8 level  u8 voice_count  5 x u16 voice offsets
            (relative to song+14), then the voice pattern streams
    voice:  (value, duration) byte PAIRS.
            value 128 = rest, < 128 = MIDI note number, 255 = end;
            value > 128 or duration bit 7 set = a command pair
            (129 re-strike, 132 set level, others skipped).
            duration bits 0-2: note value, whole..1/128 (26880 >> n)
                     bit  3:   dotted (x1.5)
                     bits 4-5: tuplet t in {2,4,6}: (d/(t+1))*t
                     bit  6:   tie into the next pair
            Every Mac voice opens with the preamble 87 00 83 00 81 LL
            82 1a (135/131/129/130 command pairs; only 129 acts on the
            4-tone synth — the others target richer drivers) and ends
            with FF FF. Preamble levels are ff/47/4e by voice index.

## Timing (verified against the matched Mac/DOS song pairs)

XMIDI runs 60 ticks per quarter note; the Mac pattern grid is 26880
ticks per whole note (6720/quarter), so ONE XMI TICK = 112 MAC TICKS
(TYDQ2's opening 72-tick note is the Mac's 8064 exactly). Tempo lives
in the song-header period: reload = period*105/7200 (l0fc4), and the
matched pairs pin

    period = 15360 * 500000 / tempo_us

(500000 us/quarter -> 15360 = the Mac's own value; TYDQ3's 560747 ->
13696 = the Mac's song-7 period to the digit).

Durations are quantized onto the l0ff2 grid by aiming each event at
its ABSOLUTE ideal tick (greedy largest-fit + ties), so rounding never
accumulates across a voice and the three voices stay in step.
"""
import os
import struct
import sys

XMI_TICK = 112                  # Mac sequencer ticks per XMI tick
PERIOD_REF = 15360              # song-header period at 500000 us/quarter
TEMPO_REF = 500000
PREAMBLE_LEVELS = (0xFF, 0x47, 0x4E, 0x47, 0x47)
MAX_VOICES = 3                  # the Mac songs are 3-voice; Tandy is 3-channel


def l0ff2(b):
    """The engine's duration decode (CODE 5 + 0x0ff2), bit-faithful."""
    d = 26880 >> (b & 7)
    if b & 8:
        d = (d >> 1) * 3
    t = (b & 48) >> 3
    if b & 48:
        d = (d // (t + 1)) * t
    return d & 0xffff


def duration_grid():
    """All distinct (ticks, code) the duration byte can express, longest
    first. Codes use bits 0..5 only (bit 6 = tie, bit 7 = command)."""
    seen = {}
    for code in range(0x40):
        d = l0ff2(code)
        if d > 0 and d not in seen:
            seen[d] = code
    return sorted(seen.items(), key=lambda kv: -kv[0])


GRID = duration_grid()
GRID_MIN = GRID[-1][0]


def quantize(target):
    """Decompose `target` Mac ticks into grid codes (greedy largest-fit).
    Returns (codes, emitted_ticks); emitted is within GRID_MIN/2 of the
    target (callers aim at absolute positions, so error never drifts)."""
    codes = []
    left = target
    while left >= GRID_MIN:
        for d, c in GRID:
            if d <= left:
                codes.append(c)
                left -= d
                break
    if left > GRID_MIN // 2:
        codes.append(GRID[-1][1])
        left -= GRID_MIN
    return codes, target - left


def parse_xmi(data):
    """Parse one XMIDI file -> (notes, tempo_us).
    notes = [(tick, channel, note, velocity, duration_ticks)].
    XMIDI deltas are bare 0x00..0x7F accumulator bytes (no VLQ) and
    Note On carries a VLQ DURATION after the velocity (no Note Offs)."""
    i = data.find(b"EVNT")
    if i < 0:
        raise ValueError("no EVNT chunk")
    ln = struct.unpack(">I", data[i + 4:i + 8])[0]
    ev = data[i + 8:i + 8 + ln]
    t = 0
    p = 0
    tempo = TEMPO_REF
    notes = []
    while p < len(ev):
        b = ev[p]
        if b < 0x80:                        # interval byte
            t += b
            p += 1
            continue
        if b == 0xFF:                       # meta
            meta = ev[p + 1]
            ln2, q = 0, p + 2
            while True:
                c = ev[q]
                ln2 = (ln2 << 7) | (c & 0x7F)
                q += 1
                if not (c & 0x80):
                    break
            if meta == 0x51:
                tempo = (ev[q] << 16) | (ev[q + 1] << 8) | ev[q + 2]
            if meta == 0x2F:
                break
            p = q + ln2
            continue
        st, ch = b & 0xF0, b & 0x0F
        if st == 0x90:                      # note on + VLQ duration
            note, vel = ev[p + 1], ev[p + 2]
            q = p + 3
            dur = 0
            while True:
                c = ev[q]
                dur = (dur << 7) | (c & 0x7F)
                q += 1
                if not (c & 0x80):
                    break
            notes.append((t, ch, note, vel, dur))
            p = q
        elif st in (0x80, 0xA0, 0xB0, 0xE0):
            p += 3
        elif st in (0xC0, 0xD0):
            p += 2
        else:
            p += 1
    return notes, tempo


def monophonize(events):
    """[(start, note, dur)] sorted -> truncate overlaps (last-note wins)."""
    out = []
    for start, note, dur in sorted(events):
        if out and out[-1][0] + out[-1][2] > start:
            prev = out[-1]
            out[-1] = (prev[0], prev[1], max(0, start - prev[0]))
        out.append((start, note, dur))
    return [e for e in out if e[2] > 0]


def voice_pattern(events, level):
    """One channel's [(xmi_start, note, xmi_dur)] -> a Mac pattern stream.
    Events are aimed at their absolute ideal Mac tick so quantization
    error never accumulates."""
    pat = bytearray((0x87, 0x00, 0x83, 0x00, 0x81, level, 0x82, 0x1A))
    pos = 0                                     # emitted Mac ticks
    for start, note, dur in events:
        ideal = start * XMI_TICK
        if ideal > pos:                         # rest-fill the gap
            codes, emitted = quantize(ideal - pos)
            for c in codes:
                pat += bytes((128, c))
            pos += emitted
        codes, emitted = quantize(dur * XMI_TICK)
        for k, c in enumerate(codes):
            tie = 0x40 if k + 1 < len(codes) else 0
            pat += bytes((note, c | tie))
        pos += emitted
    pat += b"\xff\xff"
    return bytes(pat)


def song_from_xmi(data):
    """One XMI file -> a complete .slb song record."""
    notes, tempo = parse_xmi(data)
    by_ch = {}
    for t, ch, note, vel, dur in notes:
        if ch == 9:                             # GM percussion — no pitch
            continue
        by_ch.setdefault(ch, []).append((t, note, dur))
    # busiest channels first, up to the Mac voice budget
    chans = sorted(by_ch, key=lambda c: -len(by_ch[c]))[:MAX_VOICES]
    chans.sort()                                # keep the source ordering
    voices = [voice_pattern(monophonize(by_ch[c]), PREAMBLE_LEVELS[i])
              for i, c in enumerate(chans)]
    period = int(round(PERIOD_REF * TEMPO_REF / tempo))
    hdr = struct.pack(">HBB", period, 127, len(voices))
    voffs = []
    off = 0
    for v in voices:
        voffs.append(off)
        off += len(v)
    voffs += [0] * (5 - len(voffs))
    return hdr + struct.pack(">5H", *voffs) + b"".join(voices)


def empty_song():
    """A valid song that ends on its first sequencer tick (silence)."""
    return struct.pack(">HBB5H", PERIOD_REF, 127, 1, 0, 0, 0, 0, 0) + b"\xff\xff"


# Mac slot -> DOS song number (None = Mac-only composition, left empty)
SLOT_MAP = (1, 2, None, None, None, None, None, 3)


def build_bank(xmi_by_song):
    """{1: bytes, 2: bytes, 3: bytes} (XMI file contents) -> .slb bytes."""
    songs = []
    for slot, q in enumerate(SLOT_MAP):
        songs.append(song_from_xmi(xmi_by_song[q]) if q is not None
                     else empty_song())
    offs = []
    off = 0
    for s in songs:
        offs.append(off)
        off += len(s)
    offs.append(off)
    body = struct.pack(">%dH" % len(offs), *offs) + b"".join(songs)
    total = 4 + 4 + 1 + 1 + len(body)
    return b"SLBR" + struct.pack(">IBB", total, len(songs), 0) + body


def find_xmi_set(dos_dir):
    """Locate one full Q1/Q2/Q3 set under `dos_dir` (searched recursively,
    case-insensitive). Tandy (TY) preferred — already 3 monophonic voices;
    then PC (1 voice), then RO/AD (reduced to the 3 busiest channels)."""
    found = {}
    for r, _d, files in os.walk(dos_dir):
        for f in files:
            u = f.upper()
            if u.endswith(".XMI") and len(u) == 9 and u[2:4] == "DQ":
                found[(u[:2], int(u[4]))] = os.path.join(r, f)
    for pref in ("TY", "PC", "RO", "AD"):
        if all((pref, q) in found for q in (1, 2, 3)):
            return {q: found[(pref, q)] for q in (1, 2, 3)}, pref
    return None, None


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        print("usage: xmi2slb.py <dos_dir> <out.slb>")
        return 2
    paths, pref = find_xmi_set(argv[0])
    if paths is None:
        print("xmi2slb: no complete ??DQ1..3.XMI set under %s" % argv[0])
        return 1
    bank = build_bank({q: open(p, "rb").read() for q, p in paths.items()})
    with open(argv[1], "wb") as f:
        f.write(bank)
    print("xmi2slb: %s (%d bytes) from the %s arrangement (%s)"
          % (argv[1], len(bank), pref,
             ", ".join(os.path.basename(paths[q]) for q in (1, 2, 3))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
