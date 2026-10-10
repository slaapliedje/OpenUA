#!/usr/bin/env python3
"""midi_compare.py - compare a recording of the MIDI music with the XMIDI
score it was played from (the DOS Roland arrangement, RODQ<n>.XMI).

    python3 tools/midi_compare.py OUTDIR SCORE.XMI platform/midi_seq.c REC.wav

REC.wav: mono or stereo, any rate (ffmpeg -ac 1 -ar 22050 first is kindest).
Needs numpy + scipy (a venv is fine); fluidsynth + FluidR3_GM.sf2 for the
reference renders (NORENDER=1 skips them).

Reports: the tempo (a scale factor against the score, 1.0000 = exact),
how many of the score's onsets have a recorded onset within 60 ms, their
timing offset, where the unmatched ones sit, the alignment per 6 s window
(a window off by hundreds of ms = the port paying back a backlog), and
pitch classes that ring where the score has them silent (hanging notes).
Also renders the score through a GM soundfont with OpenUA's MT-32->GM map
(ref_gm) and with the MT-32 numbers raw (ref_raw, what the DOS game sends
to a GM module).

CONTROL: run it on ref_gm.wav itself (ffmpeg -ac 1 -ar 22050). The onset
detector reaches only ~74% matched on this music even with perfect timing
(soft notes inside sustained chords), so compare recordings against that
ceiling, not against 100%. First TT030 recordings (2026-10-10): 56% with
the once-a-tick pump, 69% once the queue was pumped on every poll when
behind; a perfect render 74%.
"""
import sys, re, struct, subprocess, numpy as np
from scipy.io import wavfile
from scipy.signal import stft, fftconvolve
from scipy.ndimage import gaussian_filter1d, maximum_filter1d, median_filter

S = sys.argv[1]; XMI = sys.argv[2]; SRC = sys.argv[3]; REC = sys.argv[4]

# ---- the score ---------------------------------------------------------------
def parse_xmi(data):
    i = data.find(b"EVNT"); ln = struct.unpack(">I", data[i+4:i+8])[0]
    ev = data[i+8:i+8+ln]; t = p = 0; events = []; tempos = [(0, 500000)]; end = None
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
            note, vel = ev[p+1], ev[p+2]; dur, q = vlq(p+3)
            events.append((t, 'on', ch, note, vel, dur)); p = q
        elif st in (0x80, 0xA0, 0xB0, 0xE0):
            events.append((t, st, ch, ev[p+1], ev[p+2], 0)); p += 3
        elif st in (0xC0, 0xD0):
            events.append((t, st, ch, ev[p+1], 0, 0)); p += 2
        else: p += 1
    if end is None: end = max(e[0] for e in events)
    return events, tempos, end

data = open(XMI, 'rb').read()
events, tempos, end_tick = parse_xmi(data)
# tick -> seconds, piecewise by tempo (one tick = tempo/60 us)
def tick_to_s(tick):
    s = 0.0; last_t, tempo = tempos[0]
    for tt, tp in tempos[1:]:
        if tick <= tt: break
        s += (tt - last_t) * tempo / 60 / 1e6; last_t, tempo = tt, tp
    return s + (tick - last_t) * tempo / 60 / 1e6
loop_s = tick_to_s(end_tick)
ons = [(tick_to_s(e[0]), e[2], e[3], e[4], tick_to_s(e[0] + max(e[5], 1))) for e in events if e[1] == 'on']
print("score: %d events, %d note-ons, tempos %s, end tick %d = %.2f s loop" %
      (len(events), len(ons), tempos, end_tick, loop_s))
chans = sorted(set(o[1] for o in ons)); print("channels with notes:", chans)

src = open(SRC).read()
blk = re.search(r'mt32_to_gm\[128\] = \{(.*?)\};', src, re.S).group(1)
blk = re.sub(r'/\*.*?\*/', '', blk, flags=re.S)
gm = [int(x) for x in re.findall(r'\d+', blk)]; assert len(gm) == 128, len(gm)

# ---- the recording -----------------------------------------------------------
sr, x = wavfile.read(REC); x = x.astype(np.float64)
if x.ndim > 1: x = x.mean(axis=1)
x /= (np.abs(x).max() or 1)
NFFT, HOP = 2048, 256
f, tf, Z = stft(x, fs=sr, nperseg=NFFT, noverlap=NFFT-HOP, boundary=None, padded=False)
mag = np.abs(Z); FR = sr / HOP                    # frames per second
L = np.log1p(10 * mag)
flux = np.maximum(L[:, 1:] - L[:, :-1], 0).sum(axis=0); flux = np.concatenate([[0], flux])
flux = flux - median_filter(flux, size=int(FR))   # remove slow level
flux = np.maximum(flux, 0); flux /= flux.max()
print("recording: %.1f s, %d frames at %.1f/s" % (len(x)/sr, len(flux), FR))

# score onset train, two loops long, weighted by velocity
nfr = int(2 * loop_s * FR) + 10
train = np.zeros(nfr)
for s0, ch, note, vel, s1 in ons:
    for k in (0, 1):
        i = int((s0 + k * loop_s) * FR)
        if i < nfr: train[i] += vel / 127.0
def smooth(a, sig=2.0): return gaussian_filter1d(a, sig)
fs_ = smooth(flux); 

# tempo scale search: stretch the train by `scale` (recording time = score time * scale)
best = None
for scale in np.arange(0.94, 1.06, 0.0025):
    idx = np.arange(nfr) * scale
    tr = np.zeros(int(nfr * scale) + 2)
    np.add.at(tr, np.round(idx).astype(int), train)
    tr = smooth(tr)
    c = fftconvolve(tr, fs_[::-1], mode='full')      # c[k] = sum tr[n] fs[n - lag]
    # lag = n_train - n_rec: index k = n_tr + (len(fs)-1 - n_rec) -> lag = k - (len(fs)-1)
    k = int(np.argmax(c)); lag = k - (len(fs_) - 1)
    score = c[k] / (np.linalg.norm(tr) * np.linalg.norm(fs_) + 1e-9)
    if best is None or score > best[0]: best = (score, scale, lag)
score_, scale, lag = best
print("alignment: tempo scale %.4f (recording/score; >1 = slower), corr %.3f, recording starts %.2f s into the score" % (scale, score_, (lag / FR) / scale % loop_s))

# onset peaks in the recording
peaks = (flux == maximum_filter1d(flux, size=7)) & (flux > 0.08)
pk = np.flatnonzero(peaks); pk_t = pk / FR
# expected onsets inside the recording window (grouped per frame: a chord = one onset)
exp = {}
for s0, ch, note, vel, s1 in ons:
    for k in (0, 1, 2):
        t_rec = (s0 + k * loop_s) * scale - lag / FR
        if 0.3 <= t_rec <= len(x)/sr - 0.3:
            exp.setdefault(round(t_rec, 2), []).append((ch, note, vel))
exp_t = np.array(sorted(exp)); 
tol = 0.06
d = np.abs(exp_t[:, None] - pk_t[None, :]) if len(pk_t) else np.full((len(exp_t), 1), 9)
nearest = d.min(axis=1); matched = nearest <= tol
print("expected onset groups in window: %d; matched within %d ms: %d (%.0f%%); recorded peaks: %d, unexplained (no score onset within %d ms): %d"
      % (len(exp_t), tol*1000, matched.sum(), 100*matched.mean(), len(pk_t), tol*1000,
         int((np.abs(pk_t[:, None] - exp_t[None, :]).min(axis=1) > tol).sum()) if len(pk_t) else 0))
off = (pk_t[d.argmin(axis=1)] - exp_t)[matched] * 1000
print("timing offset of matched onsets: mean %+.1f ms, sd %.1f ms, max |%.0f| ms" % (off.mean(), off.std(), np.abs(off).max()))
# where do the unmatched expected onsets sit: how far from the nearest peak, and when
um = ~matched
bands = [(0.06, 0.12), (0.12, 0.25), (0.25, 0.5), (0.5, 99)]
print("unmatched expected onsets by distance to the nearest recorded peak:",
      ", ".join("%d-%d ms: %d" % (a*1000, b*1000, int(((nearest > a) & (nearest <= b)).sum())) for a, b in bands))
late = (pk_t[d.argmin(axis=1)] - exp_t)
print("of those within 60-250 ms, late (peak after the onset): %d, early: %d" %
      (int(((nearest > 0.06) & (nearest <= 0.25) & (late > 0)).sum()), int(((nearest > 0.06) & (nearest <= 0.25) & (late < 0)).sum())))
print("matched per 6 s window: " + " ".join("%d%%" % (100 * matched[(exp_t >= w) & (exp_t < w + 6)].mean()) if ((exp_t >= w) & (exp_t < w + 6)).any() else "-"
      for w in range(0, int(len(x)/sr), 6)))
loud = [t for t, g in exp.items() if max(v for _, _, v in g) >= 80]
lm = np.array([exp_t[i] in loud for i in range(len(exp_t))])
if lm.any(): print("of the loud onsets (vel>=80): %d/%d matched" % (matched[lm].sum(), lm.sum()))
# local drift: lag per 6 s window
print("local alignment (lag of recording vs score per window; a jump = a pause):")
W = int(6 * FR)
for w0 in range(0, len(fs_) - W, W):
    seg = fs_[w0:w0+W]
    idx = np.arange(nfr) * scale
    tr = np.zeros(int(nfr * scale) + 2); np.add.at(tr, np.round(idx).astype(int), train); tr = smooth(tr)
    t0 = w0 + lag                                           # where this window sits in the train
    a, b = max(t0 - int(FR), 0), min(t0 + W + int(FR), len(tr))
    if b - a <= W: continue
    c = fftconvolve(tr[a:b], seg[::-1], mode='valid')
    j = int(np.argmax(c)); local_lag = (a + j) - w0
    print("  %5.1f-%5.1f s: lag %+6.0f ms (relative to global)  corr %.2f" %
          (w0/FR, (w0+W)/FR, (local_lag - lag) / FR * 1000, c[j] / (np.linalg.norm(tr[a+j:a+j+W]) * np.linalg.norm(seg) + 1e-9)))

# ---- hanging notes: pitch-class energy where the score has that class silent ------
freqs = f; pc_of_bin = np.full(len(freqs), -1)
for i, fr_ in enumerate(freqs):
    if 60 < fr_ < 2100:
        n = 69 + 12 * np.log2(fr_ / 440.0)
        if abs(n - round(n)) < 0.3: pc_of_bin[i] = int(round(n)) % 12
P = mag ** 2
chroma = np.zeros((12, P.shape[1]))
for c in range(12):
    sel = pc_of_bin == c
    if sel.any(): chroma[c] = P[sel].sum(axis=0)
tot = chroma.sum(axis=0) + 1e-12; chroma_n = chroma / tot
# the score's sounding pitch classes per recording frame (drums excluded)
sound = np.zeros((12, P.shape[1]), bool)
for s0, ch, note, vel, s1 in ons:
    if ch == 9: continue
    for k in (0, 1, 2):
        a = int(((s0 + k*loop_s) * scale - lag/FR) * FR); b = int(((s1 + k*loop_s) * scale - lag/FR) * FR) + int(0.25*FR)
        a, b = max(a, 0), min(b, P.shape[1])
        if a < b: sound[note % 12, a:b] = True
names = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
print("pitch classes ringing while the score has them silent (runs > 1.5 s, > 25%% of the energy):")
found = 0
for c in range(12):
    bad = (chroma_n[c] > 0.25) & ~sound[c]
    # runs
    i = 0
    while i < len(bad):
        if bad[i]:
            j = i
            while j < len(bad) and bad[j]: j += 1
            if (j - i) / FR > 1.5:
                print("  %-2s  %6.1f - %6.1f s (%.1f s)" % (names[c], i/FR, j/FR, (j-i)/FR)); found += 1
            i = j
        else: i += 1
if not found: print("  none")
# the tail: is there sound after the last expected onset?
rms = np.sqrt(np.convolve(x**2, np.ones(int(sr*0.1))/int(sr*0.1), mode='same'))
print("level: first 2 s rms %.3f, last 2 s rms %.3f (full-scale 1.0)" % (rms[:int(2*sr)].mean(), rms[-int(2*sr):].mean()))

# ---- reference renders -------------------------------------------------------
def write_smf(path, mapped):
    evs = []  # (tick, order, bytes)
    for t, kind, ch, a, b, dur in events:
        if kind == 'on':
            evs.append((t, 1, bytes([0x90|ch, a, b]))); evs.append((t + max(dur, 1), 0, bytes([0x80|ch, a, 0])))
        elif kind == 0xC0:
            prog = gm[a] if (mapped and ch != 9) else a
            evs.append((t, 0, bytes([0xC0|ch, prog])))
        elif kind == 0xB0:
            if 110 <= a <= 120: continue
            evs.append((t, 0, bytes([0xB0|ch, a, b])))
        elif kind in (0x80, 0xA0, 0xE0):
            evs.append((t, 0, bytes([kind|ch, a, b])))
        elif kind == 0xD0:
            evs.append((t, 0, bytes([0xD0|ch, a])))
    for tt, tp in tempos:
        evs.append((tt, -1, bytes([0xFF, 0x51, 0x03, tp >> 16, (tp >> 8) & 255, tp & 255])))
    evs.sort(key=lambda e: (e[0], e[1]))
    out = bytearray(); last = 0
    def vlq(v):
        r = [v & 0x7F]; v >>= 7
        while v: r.append((v & 0x7F) | 0x80); v >>= 7
        return bytes(reversed(r))
    for t, _, by in evs:
        out += vlq(t - last) + by; last = t
    out += vlq(0) + b'\xff\x2f\x00'
    with open(path, 'wb') as fh:
        fh.write(b'MThd' + struct.pack('>IHHH', 6, 0, 1, 60))
        fh.write(b'MTrk' + struct.pack('>I', len(out)) + out)
import os
if os.environ.get('NORENDER'): sys.exit(0)
write_smf(S + '/ref_gm.mid', True); write_smf(S + '/ref_raw.mid', False)
for name in ('ref_gm', 'ref_raw'):
    subprocess.run(['fluidsynth', '-ni', '-q', '-F', S + '/%s.wav' % name, '-r', '44100',
                    '/usr/share/sounds/sf2/FluidR3_GM.sf2', S + '/%s.mid' % name], check=True)
print("rendered ref_gm.wav (OpenUA's GM map) and ref_raw.wav (MT-32 patch numbers raw, as DOS sends them)")
