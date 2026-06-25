# DQK (Gen2) audio — XMIDI music + AdLib timbre bank decode (C5a)

How The Dark Queen of Krynn stores its music and instruments, and the engine container
decoders that make them addressable. Synthesis (OPL2 playback) + the platform-bound wiring
are C5b; this slice reverses and decodes the **containers**.

## Asset inventory (`DISK1/`)

| File | Role |
|---|---|
| `ADDQ1.XMI` `ADDQ2.XMI` `ADDQ3.XMI` | AdLib/**OPL2** music (Miles XMIDI) — the three song banks |
| `PCDQ*.XMI` | PC-speaker variant of the same songs |
| `RODQ*.XMI` | Roland **MT-32** variant |
| `TYDQ*.XMI` | **Tandy** variant |
| `INSTR.AD` | Miles AdLib **timbre bank** — `(patch,bank) → OPL2 register patch` |
| `SFXDQ.VOC` | Creative **Voice File** — digitized sound effects |
| `SOUNDS.GLB` | HLIB `DIG4` digitized-sound library |
| `ADLIB.ADV` `MT32MPU.ADV` `SBDIG.ADV` `TANDY.ADV` `PCSPKR.ADV` | Miles AIL **driver** overlays |

DQK is a **Miles Sound System / AIL** title: one XMIDI score per device family, resolved to
hardware through a `.ADV` driver + (for AdLib) the `INSTR.AD` global timbre library.

## XMIDI (`.XMI`) — VERIFIED

An `.XMI` is an EA IFF-85 tree of **two sibling top-level groups**:

```
FORM XDIR
  INFO  u16(LE) sequence-count          ← songs in the CAT
CAT  XMID
  FORM XMID            (one per song)
    TIMB u16(LE) count, count×{u8 patch, u8 bank}   ← timbres to load
    RBRN ...           (optional branch table — ignored)
    EVNT <XMIDI event stream>
```

All IFF id/length fields are **big-endian**; the `INFO`/`TIMB` counts inside payloads are
**little-endian**. `ADDQ1`/`ADDQ3` hold one song; **`ADDQ2` is a 6-song catalogue** (six
`FORM XMID` children under the one `CAT `) — the multi-FORM case the decoder must walk.

**The XMIDI event stream** differs from Standard MIDI in two decisive ways:

1. **Delay** before an event = a run of bytes each `< 0x80`, **summed** (not a VLQ). A long
   rest is several `0x7F` bytes plus a remainder.
2. **Note-on carries an explicit duration** — a standard MIDI VLQ *after* the velocity. There
   are **no note-off events**; the note self-releases after that many ticks. (Two different
   integer encodings in the same stream: summed-byte delay vs. VLQ duration.)

XMIDI uses **no running status** — every event has an explicit status byte. Meta events
(`0xFF`): `0x51` tempo (3-byte µs/quarter), `0x58` time signature, `0x2F` end-of-track,
`0x54` SMPTE offset (DQK songs lead with one). Controllers 110/116–119 are XMIDI loop/branch
controls — surfaced as generic `controller` events here (loop expansion is a C5b concern).

**Decoded (golden, real files):**

| File | songs | timbres (song 0) | events | note-ons | tempo |
|---|---|---|---|---|---|
| `ADDQ1.XMI` | 1 | 10 | 1995 | 1853 | 120 BPM |
| `ADDQ2.XMI` | 6 | 9 | 183 | 157 | 120 BPM |
| `ADDQ3.XMI` | 1 | 10 | 1391 | 1052 | ~107 BPM |

Timbres reference GM melodic patches (bank 0) and a percussion bank (the XMI marks it `127`).
Note-ons on channel 9 are the drum track. Engine: `decodeXmi`/`sniffXmi` (`audio/xmi.ts`),
returning `XmiFile { declaredCount, songs: XmiSong[] }` where each song is `{ timbres, events,
durationTicks }` and events are absolute-tick-tagged discriminated unions.

## `INSTR.AD` — Miles AdLib bank — VERIFIED

```
table:  N × { u8 patch, u8 bank, u32 offset(LE) }     ← entries, in record order
        u16 0xFFFF                                      ← terminator
data:   N × 14-byte instrument records  (at the offsets above)
```

The real bank: **199 instruments**, table at 0–0x4A9, `0xFFFF` terminator at 0x4AA, records
from **0x4AC** in contiguous **14-byte** slots (offsets strictly `+14`). Keys cover GM melodic
**bank 0** (patches 0–127) and a percussion **bank 100** (0x64). Each record begins with a
`u16(LE)` length (`0x000E` = 14) then **12 bytes** of OPL2 operator data (modulator + carrier
register values + feedback/connection). Engine: `decodeInstrAd`/`sniffInstrAd`
(`audio/instrAd.ts`) → `AdLibBank { instruments, byKey }`, `byKey` indexed by
`adLibKey(patch,bank)` so an XMI `TIMB` entry resolves straight to its operator bytes.

## `SFXDQ.VOC` — Creative Voice File, 13 digitized effects — VERIFIED (C5b-1)

`SFXDQ.VOC` (DISK1) is the DQK / FRUA sound-effect bank: **13 effects** separated by VOC
**marker** blocks `0..12`. Per hackdocs `UASOUND.TXT` the order is fixed, so the marker doubles
as the effect id:

```
0 cast   1 flame   2 sorcery   3 die    4 sling    5 hit      6 lightning
7 swing  8 walk    9 fireball  10 bow   11 sploosh 12 crackle
```

Structure: 20-byte `"Creative Voice File\x1A"` magic, `u16` header size, `u16` version, `u16`
checksum, then a block chain — each `u8 type` + 24-bit-LE length + payload. Type 0 = terminator,
type 4 = marker (`u16` value), **type 1 = sound data**: `u8` freq-divisor, `u8` codec, then the
audio. Rate = `1_000_000 / (256 − divisor)` (measured 7407 / 11111 / 5556 Hz). The codec byte is
**1 = Creative 4-bit ADPCM** for every effect.

**The ADPCM decode matters.** The first attempt used the asymmetric `ADPCM_SBPRO_4` model
(ffmpeg's tag for VOC codec 1) — it **railed every effect to the negative limit** (min −1.0, max
≈ +0.1, mean far below 0): wrong. The correct decoder is the **hardware-accurate Sound-Blaster
Creative 4-bit ADPCM** (DOSBox's `scaleMap`/`adjustMap`): the first payload byte is the 8-bit
reference sample, each later byte is a high then low nibble, and `nibble + scale` indexes a
**signed** delta table (`scaleMap`) plus a step table (`adjustMap`, ±16). This is what the game's
DAC reconstructed; decoded output is now **DC-centred** (|mean| < 0.01) and symmetric (peaks
±0.2…0.33) across all 13 effects. Engine: `decodeVoc`/`sniffVoc`/`DQK_SFX_NAMES`
(`audio/voc.ts`), output as normalized `Float32` {@link AudioClip}s (codec 0 = plain 8-bit PCM
also handled).

## `INSTR.AD` operator-field map — PINNED BY SYNTHESIS (C5b-2a)

The 12 operator bytes of each record (the record minus its `u16` length) are a leading **pad byte**
(always `0x00`) then 11 register bytes stored as **interleaved `(modulator, carrier)` pairs** — NOT
the SBI register order:

```
[0]  pad (always 0)
[1]  mod 0x20   [2]  car 0x20     AM / VIB / EG-type / KSR / MULT
[3]  mod 0x60   [4]  car 0x60     attack-rate / decay-rate
[5]  mod 0xE0   [6]  car 0xE0     waveform select
[7]  mod 0x40   [8]  car 0x40     key-scale-level / total-level
[9]  mod 0x80   [10] car 0x80     sustain-level / release-rate
[11] 0xC0                         feedback (bits 1-3) / connection (bit 0)
```

**How it was pinned (not guessed).** Column analysis over the bank's documented timbres
(`UA_INSTR.TXT`: 8/9 organ, 23 e-piano, 38 flute, 51 clarinet, 114/117 drums) shows this is the only
order where the audibility-critical fields are coherent: **every carrier's `0x40` total-level is 0**
(carriers at full volume), **every carrier's `0x60` attack-rate is ≥ 1** (every note actually
attacks), the two `0xE0` columns are the only ones confined to `0..3` (the four valid OPL2
waveforms), and `0xC0` stays `0..3`. The semantics fall out correctly too: the drum (117) reads
**additive** (CNT=1) with a fast carrier decay (DR=12); the clarinet (51) uses a **half-sine
modulator** (the classic odd-harmonic trick). The SBI interleave is ruled out — it lands the carrier
attack on a column that is `0` for most patches, i.e. silent notes.

The validator is the synth itself: a pure-TS OPL2 2-operator voice (`audio/opl2.ts` — the four OPL2
waveforms, an exponential ADSR envelope, FM/additive routing, modulator feedback) fed a real timbre
renders **voiced PCM whose pitch tracks the note** (a synthetic sine-FM patch locks 440 Hz within a
semitone; all 18 documented patches render voiced + in-band). `parseAdLibPatch` / `renderOplNote` /
`pcmRms` / `dominantHz` are the engine surface; `?dqk=1` renders patch 51 live in the readout. The
envelope rate→time curve here is an approximation tuned for correctly-shaped voiced output — the
exact OPL2 dB rate/KSL tables, fnum/block pitch, and the 9-channel mixer + XMI driver are **C5b-2b**.

## Status

**Shipped (C5a):** the two music container decoders + goldens (`ADDQ1` 1853 note-ons,
`ADDQ2` 6-song catalogue, `INSTR.AD` 199 instruments).

**Shipped (C5b-2a):** the `INSTR.AD` 12-byte operator map **pinned** (interleaved mod/car pairs,
above) + a pure-TS OPL2 voice that renders real timbres to voiced PCM, proven offline. Web
`?dqk=1` previews a timbre; `__dqk.previewOplNote` seam. 515 engine / 34 e2e.

**Shipped (C5b-2b-1):** a **9-channel OPL2 mixer** (`audio/oplPlayer.ts` — `renderXmiSong`) drives
the pinned voice from a decoded XMI event stream to one PCM mixdown. A **tempo map** converts
ticks→seconds at **PPQN 60** (`ADDQ1`→63 s, `ADDQ3`→52 s at file tempo — only total length depends
on the timebase). A **9-voice allocator** assigns notes in start order and steals the soonest-ending
slot when all nine are busy (the stolen note's tail is cut), so polyphony never exceeds 9. Per-channel
**program** + **CC#7 volume** scale each note; **percussion** (MIDI channel 9) resolves its timbre by
*note number in bank 100* — the `INSTR.AD` side of the bank-127↔100 map (`ADDQ1`'s `TIMB` marks 38/36/46
= snare/bass/hihat as bank 127). Each voice runs the pinned 2-op synth with a real **attenuation-domain
EG** (9-bit-style dB envelope, KSR-scaled rates), `fnum`/`block`-quantized OPL pitch and KSL — replacing
C5b-2a's amplitude approximation. **Proven offline (no reference WAV):** `ADDQ1` → 63.0 s, 1853 notes
(490 percussion), peak polyphony 9/9 (146 stolen), voiced 77% across the whole timeline, length matches
ticks×tempo. Web `?dqk=1` renders the first song live; `__dqk.previewDqkSong` seam. 518 engine / 34 e2e.

**Shipped (C5b-1):** `SFXDQ.VOC` → the 13 named digitized effects (Creative 4-bit ADPCM),
**platform-bound**: `gameAudio.setGame('dqk')` registers them as the **DOS-VGA** platform's SFX
(`AudioManager.registerDigitizedSfx`) and maps the action cues (melee→hit, arrow→bow, cast→cast,
fireball→fireball, death→die, door→sploosh, miss→swing) onto them, so DQK fights play DQK's own
sounds. Music stays honestly unarmed ("OPL2 synthesis is C5b-2"). Web `?dqk=1` reports the SFX;
`window.__dqk` exposes `sfxNames`/`sfxCount`/`gameSfxNames`/`playSfx` (plus the C5a
`audio*` seams).

**Shipped (C5b-2b-2a):** the OPL2 music now **plays through WebAudio and is graphics-mode-bound.**
`AudioManager` gained a looping **PCM music track** (`setPcmMusic` → `pcmMusic`/`pcmMusicBuffer`,
played as a `loop=true` `AudioBufferSourceNode` that takes precedence over the SMUS square-wave path);
`gameAudio`'s `'dqk'` branch renders the first ADDQ song (`renderXmiSong` @22050) and arms it on the
**DOS-VGA** platform **alongside the 13 `SFXDQ.VOC` effects** — so switching the graphics mode to DOS
DQK plays its music + SFX together, exactly how CoK/DoK Amiga SMUS is bound. The engine
`expandXmiLoops` unrolls the XMIDI **for-loop controllers** (116 for-loop start / 117 next-break end;
nested via a stack; infinite count 0 bounded) into a flat absolute-tick timeline that drives the mixer,
and the whole mixdown is marked as the loop body (`repeatSamples = length`) so WebAudio loops it
seamlessly. The ADDQ songs use only CC#114, so a synthetic 116/117 song unit-tests the unrolling.
520 engine / 34 e2e.

**Shipped (C5b-2b-2b — `SOUNDS.GLB` `DIG4` digitized SFX, SOLVED):** the last DQK SFX container is fully
reversed. `SOUNDS.GLB` is an HLIB DATA library (tag `DIG4`) of **13 raw members**, one per effect, in the
same fixed order as `SFXDQ.VOC` (`DQK_SFX_NAMES`).

*Member record* (reversed by column analysis + cross-correlation, engine `audio/dig.ts`):
- bytes `[0:2]` = **big-endian `u16` sample rate (Hz)** — only three distinct values, mapping 1:1 onto the
  VOC rates: `0x1CFA`=**7418**≈VOC 7407 (the default effects), `0x2B77`=**11127**≈VOC 11111 (members 3 & 6 —
  *die* and *lightning*, the two high-rate VOC effects), `0x15BC`=**5564**≈VOC 5556 (member 9 — *fireball*).
  This rate grouping is the structural proof the member order matches the VOC order.
- bytes `[2:4]` = **`00 00`** (the rate's always-zero high word).
- bytes `[4:]` = **raw 8-bit *unsigned* PCM**, centre `0x80`, one byte/sample to end of member.

*Codec confirmed by cross-check, not assumed.* Decoding as 8-bit unsigned PCM gives centred output
(|mean| < 0.25), and resampling each member's waveform to a length-normalized, zero-mean, unit-norm shape
and dotting it against the already-verified `SFXDQ.VOC` effects matches each distinctive effect to **its own
id**: lightning #6 r≈0.97, flame #1 0.83, walk #8 0.82, die #3 0.81, crackle #12 0.79, bow #10 0.67, hit #5
0.57. (Noise-burst effects — cast, sorcery, sling — have low-distinctiveness envelopes and correlate weakly,
as expected; they are still the same set, just not separable by waveform shape.) The earlier **IMA-ADPCM
hypothesis was rejected**: it produced full-scale noise (RMS≈0.9) that correlated with nothing and was not
DC-centred.

*Wiring.* New engine decoder `audio/dig.ts` exports `decodeSoundsGlb` / `decodeDigMember` / `sniffDig`
(+ `DigFile`/`DigSound`), with 5 vitest cases (synthetic round-trip of the BE rate + 8-bit normalize; golden
13-effect rate-bucket + centring; golden same-id cross-correlation vs the VOC). `gameAudio`'s `'dqk'` branch
now **prefers the in-engine `SOUNDS.GLB` DIG4 bank** and falls back to `SFXDQ.VOC` (both decode to the same 13
named clips). A `previewDigSfx` seam + e2e assert the count, names, rate buckets and centring. 525 engine / 34 e2e.

**Open (optional refinements only — DQK audio is otherwise complete):**
- **Chunked/progressive** WebAudio rendering (the current music track renders the full mixdown once at load).
- The **cycle-exact dbopl** EG counter (the current EG is attenuation-domain-faithful but not cycle-exact).
