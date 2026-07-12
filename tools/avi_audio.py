#!/usr/bin/env python3
"""Pull the PCM audio track out of a Hatari AVI and report where the sound is.

Sound is the one subsystem a screenshot cannot check. Hatari's `--avirecord`
writes a PCM audio track alongside the video, so booting the port under
`-DFRUA_SNDTEST` and recording an AVI gives the harness ears:

    tools/avi_audio.py capture.avi out.wav

It prints the track format, writes a plain WAV, and lists each burst of sound
with its start, duration, peak level and rough dominant pitch (zero-crossing
rate) — enough to tell "the effects played, at the right length and pitch" from
"the DMA path is silent".

Two Hatari quirks this has to cope with, both learned the hard way:

  * The AVI is only finalized on a clean exit — the RIFF/LIST sizes are written
    at close. SIGKILLing Hatari (what `driver.sh stop` does) leaves size fields
    at 0 and the file unreadable by a strict parser. Quit it via the command
    fifo (`hatari-shortcut quit`) instead; `hatari_ui.sh quit` does that.
  * Hatari writes its chunks UNPADDED. The AVI spec word-aligns every chunk, but
    a 4135-byte video chunk here is followed immediately by the next chunk, not
    at the even boundary. A spec-conformant walker desyncs by one byte on the
    first odd chunk and finds no audio at all, so we resync per chunk.

No ffmpeg needed — an AVI is just RIFF chunks and the stdlib can do the rest.
"""
import argparse
import array
import struct
import sys
import wave


def plausible(cid):
    """A FOURCC is four printable bytes — used to resync after odd-sized chunks."""
    return len(cid) == 4 and all(32 <= b < 127 for b in cid)


def extract(path):
    """-> (channels, bits, rate, PCM bytes) for the AVI's audio stream."""
    data = open(path, 'rb').read()
    if data[:4] != b'RIFF' or data[8:12] != b'AVI ':
        sys.exit('%s: not an AVI' % path)
    if struct.unpack('<I', data[4:8])[0] == 0:
        sys.exit('%s: unfinalized AVI (RIFF size 0) — Hatari was killed rather '
                 'than quit; use `hatari_ui.sh quit` so it closes the file' % path)

    fmt = None
    audio = bytearray()
    stream_kinds = []

    def walk(base, end):
        nonlocal fmt
        pos = base
        while pos + 8 <= end:
            cid = data[pos:pos + 4]
            if not plausible(cid):
                return
            (sz,) = struct.unpack('<I', data[pos + 4:pos + 8])
            body = pos + 8
            if cid == b'LIST':
                walk(body + 4, min(body + sz, end))
            elif cid == b'strh':
                stream_kinds.append(data[body:body + 4])
            elif cid == b'strf' and stream_kinds and stream_kinds[-1] == b'auds':
                ch, rate = struct.unpack('<HI', data[body + 2:body + 8])
                (bits,) = struct.unpack('<H', data[body + 14:body + 16])
                fmt = (ch, bits, rate)
            elif cid[2:4] == b'wb':                  # '01wb' — audio samples
                audio.extend(data[body:body + sz])
            nxt = body + sz
            # Hatari does not pad odd chunks: prefer the unpadded offset, and
            # only step over a pad byte if that is what yields a valid FOURCC.
            if not plausible(data[nxt:nxt + 4]) and plausible(data[nxt + 1:nxt + 5]):
                nxt += 1
            pos = nxt

    walk(12, len(data))
    if fmt is None:
        sys.exit('%s: no audio stream in this AVI (was Hatari built/run with '
                 'sound enabled? pass --sound 44100)' % path)
    ch, bits, rate = fmt
    return ch, bits, rate, bytes(audio)


def bursts(left, rate, floor=300, gap_ms=80):
    """Group the samples into runs of sound, tolerating short quiet spans."""
    win = max(1, rate // 100)                        # 10 ms
    peaks = [max((abs(v) for v in left[i:i + win]), default=0)
             for i in range(0, len(left), win)]
    gap = max(1, gap_ms // 10)
    out, run, quiet = [], None, 0
    for i, pk in enumerate(peaks):
        if pk > floor:
            if run is None:
                run = i
            quiet = 0
        elif run is not None:
            quiet += 1
            if quiet >= gap:
                out.append((run, i - quiet))
                run = None
    if run is not None:
        out.append((run, len(peaks)))
    return [(s * win, e * win) for s, e in out]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('avi')
    ap.add_argument('wav', nargs='?', help='write the audio track here')
    ap.add_argument('--floor', type=int, default=300,
                    help='amplitude above which a window counts as sound')
    args = ap.parse_args()

    ch, bits, rate, pcm = extract(args.avi)
    if bits != 16:
        sys.exit('expected 16-bit PCM, got %d-bit' % bits)
    secs = len(pcm) / float(rate * ch * bits // 8)
    print('audio: %d ch, %d-bit, %d Hz, %.1f s' % (ch, bits, rate, secs))
    if not pcm:
        sys.exit('audio track is EMPTY')

    if args.wav:
        with wave.open(args.wav, 'wb') as w:
            w.setnchannels(ch)
            w.setsampwidth(bits // 8)
            w.setframerate(rate)
            w.writeframes(pcm)
        print('wrote %s' % args.wav)

    a = array.array('h')
    a.frombytes(pcm)
    if sys.byteorder == 'big':
        a.byteswap()
    left = a[0::ch]

    found = bursts(left, rate, args.floor)
    print('\n%d burst(s) of sound:' % len(found))
    for k, (lo, hi) in enumerate(found, 1):
        seg = left[lo:hi]
        pk = max(abs(v) for v in seg)
        zc = sum(1 for i in range(1, len(seg)) if (seg[i - 1] < 0) != (seg[i] < 0))
        hz = zc * rate / (2.0 * len(seg)) if len(seg) else 0
        print('  #%d  start %6.2fs  dur %5.2fs  peak %5d  ~%4.0f Hz dominant'
              % (k, lo / float(rate), (hi - lo) / float(rate), pk, hz))
    if not found:
        print('  NONE — the DMA path produced silence')
        sys.exit(1)


if __name__ == '__main__':
    main()
