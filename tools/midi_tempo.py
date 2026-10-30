#!/usr/bin/env python3
"""midi_tempo.py SCORE.XMI REC.mid - check a MIDI recording of OpenUA's music
against the XMIDI score it played (the DOS RODQ<n>.XMI).

REC.mid is a Standard MIDI File of what the port sent, e.g. from amiberry's
MIDI out into `arecordmidi` (see .claude/skills/run-amiga-port/SKILL.md) or
from any host MIDI capture. The recording must start before the song does:
the sequencer sends the score's events in order, so the i-th recorded
note-on is the i-th of the score. Reports the (channel, note) multiset of
one loop against the score's (IDENTICAL or the difference), how many
simultaneous-note groups match as sets (notes inside one chord may be sent
in any order), the least-squares tempo factor (1.0000 = the score's tempo),
the timing residual and, when the recording is long enough, where the loop
restarted against the score's loop length.

Amiga AGA in amiberry, 2026-10-30: multiset IDENTICAL, 500 of 506 groups,
tempo 1.0015, residual rms 11 ms, loop restart 62.58 s = the score's."""
import sys, struct
def parse_xmi(data):
    i = data.find(b"EVNT"); ln = struct.unpack(">I", data[i+4:i+8])[0]
    ev = data[i+8:i+8+ln]; t = p = 0; ons = []; tempos = [(0, 500000)]; end = None
    def vlq(q):
        v = 0
        while True:
            c = ev[q]; v = (v << 7) | (c & 0x7F); q += 1
            if not c & 0x80: return v, q
    while p < len(ev):
        b = ev[p]
        if b < 0x80: t += b; p += 1; continue
        if b == 0xFF:
            meta = ev[p+1]; ln2, q = vlq(p+2)
            if meta == 0x51: tempos.append((t, (ev[q]<<16)|(ev[q+1]<<8)|ev[q+2]))
            if meta == 0x2F: end = t; break
            p = q + ln2; continue
        if b in (0xF0, 0xF7): ln2, q = vlq(p+1); p = q + ln2; continue
        st, ch = b & 0xF0, b & 0x0F
        if st == 0x90:
            dur, q = vlq(p+3); ons.append((t, ch, ev[p+1])); p = q
        elif st in (0x80, 0xA0, 0xB0, 0xE0): p += 3
        elif st in (0xC0, 0xD0): p += 2
        else: p += 1
    def tick_to_s(tick):
        s = 0.0; last_t, tempo = tempos[0]
        for tt, tp in tempos[1:]:
            if tick <= tt: break
            s += (tt - last_t) * tempo / 60 / 1e6; last_t, tempo = tt, tp
        return s + (tick - last_t) * tempo / 60 / 1e6
    return [(tick_to_s(t), ch, n) for t, ch, n in ons], tick_to_s(end)

def parse_smf(data):
    assert data[:4] == b"MThd"; div = struct.unpack(">H", data[12:14])[0]
    p = 14; tracks = []
    while p < len(data):
        assert data[p:p+4] == b"MTrk"; ln = struct.unpack(">I", data[p+4:p+8])[0]
        tracks.append(data[p+8:p+8+ln]); p += 8 + ln
    out = []
    for tr in tracks:
        t = 0; p = 0; run = None; tempo = 500000; secs = 0.0; last_t = 0
        def vlq():
            nonlocal p; v = 0
            while True:
                c = tr[p]; v = (v << 7) | (c & 0x7F); p += 1
                if not c & 0x80: return v
        while p < len(tr):
            dt = vlq(); t += dt; secs += dt * tempo / div / 1e6
            b = tr[p]
            if b == 0xFF:
                meta = tr[p+1]; p += 2; ln = vlq()
                if meta == 0x51: tempo = (tr[p]<<16)|(tr[p+1]<<8)|tr[p+2]
                p += ln; continue
            if b in (0xF0, 0xF7): p += 1; ln = vlq(); p += ln; continue
            if b & 0x80: run = b; p += 1
            st, ch = run & 0xF0, run & 0x0F
            if st in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                d1, d2 = tr[p], tr[p+1]; p += 2
                if st == 0x90 and d2 > 0: out.append((secs, ch, d1))
            else: p += 1
    return sorted(out)


import collections, itertools
score, loop_s = parse_xmi(open(sys.argv[1], 'rb').read())
rec = parse_smf(open(sys.argv[2], 'rb').read())
print("score: %d note-ons, loop %.2f s; recording: %d note-ons over %.1f s" %
      (len(score), loop_s, len(rec), rec[-1][0] - rec[0][0] if rec else 0))
n = min(len(score), len(rec))
if n < 16:
    sys.exit("too little recorded")
a = collections.Counter((c, k) for _, c, k in score[:n]); b = collections.Counter((c, k) for _, c, k in rec[:n])
print("(channel, note) multiset over %d note-ons: %s" % (n, "IDENTICAL" if a == b else "differs: %s" % ((a - b) + (b - a))))
ok = bad = 0
for _, grp in itertools.groupby(range(n), key=lambda k: round(score[k][0], 3)):
    g = list(grp)
    if sorted((score[k][1], score[k][2]) for k in g) == sorted((rec[k][1], rec[k][2]) for k in g): ok += 1
    else: bad += 1
print("simultaneous-note groups matching as sets: %d, not: %d" % (ok, bad))
xs = [score[k][0] for k in range(n)]; ys = [rec[k][0] - rec[0][0] for k in range(n)]
mx = sum(xs) / n; my = sum(ys) / n
slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs)
res = [y - (my + slope * (x - mx)) for x, y in zip(xs, ys)]
print("tempo factor %.4f (1 = the score's); residual max %.0f ms, rms %.0f ms" %
      (slope, max(abs(r) for r in res) * 1000, (sum(r * r for r in res) / n) ** 0.5 * 1000))
if len(rec) > len(score):
    print("loop restart at recorded %.2f s vs the score's loop %.2f s" % (rec[len(score)][0] - rec[0][0], loop_s))
