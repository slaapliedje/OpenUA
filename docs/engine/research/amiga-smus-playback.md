# Amiga SMUS Playback — Correct Duration, Tempo, and Timbre Model

**Research date:** 2026-06-23
**Scope:** IFF SMUS format (CoK/DoK Amiga music), correct duration formula, tempo encoding,
instrument/waveform mapping, and a concrete fix list for `smus.ts` + `audioManager.ts`.
**Primary source files inspected:**
- `amiga_extracted/ChampionsOfKrynn/disk1/Sound/krynn` (CoK sole music track)
- `amiga_extracted/DeathKnightsOfKrynn/disk1/sound/*.smu` (DoK: 7 tracks)
- `amiga_extracted/ChampionsOfKrynn/disk1/Sound/soundData` (instrument waveform library)
- `amiga_extracted/ChampionsOfKrynn/disk1/Sound/periodTable` (pitch period lookup)
- `packages/engine/src/audio/smus.ts`, `apps/web/src/audio/audioManager.ts`
**Authoritative spec:** EA IFF SMUS specification via AmigaOS Wiki
(<https://wiki.amigaos.net/wiki/SMUS_IFF_Simple_Musical_Score>).

---

## 1. Root Causes of the Playback Problem

Three independent bugs cause the reported "tempo and tones are off." They compound each other.

| Bug | Location | Effect |
|-----|----------|--------|
| Tempo divisor wrong (256 vs 128) | `smus.ts` line 62 | All songs play at **half speed** |
| Duration byte misinterpreted as raw ticks | `smus.ts` lines 45-49, `audioManager.ts` | Note lengths are **individually wrong** (not a constant factor) |
| All voices play as square waves | `audioManager.ts` `scheduleNote` | Three of the four CoK tracks use **different waveforms** (tri, odd, saw) |

---

## 2. SHDR Tempo — Correct Encoding

### Spec (authoritative)

The SMUS SHDR `tempo` field is defined as **"128ths of a quarter note per minute"** (a 9-bit integer part + 7-bit fractional part fixed-point value). To get quarter-notes per minute:

```
BPM_quarter = tempoRaw / 128
```

The current `smus.ts` code divides by 256:

```typescript
const tempoBPM = tempoRaw / 256;  // WRONG
```

This produces exactly **half the correct BPM** for every score.

### Confirmed values from actual files

| Track | tempoRaw (hex) | Correct BPM (÷128) | Current wrong BPM (÷256) |
|-------|---------------|---------------------|--------------------------|
| CoK `krynn` | 0x3C00 = 15360 | **120.0** | 60.0 |
| DoK `krynn.smu` | 0x4A00 = 18944 | **148.0** | 74.0 |
| DoK `action.smu` | 0x4100 = 16640 | **130.0** | 65.0 |
| DoK `horror.smu` | 0x3000 = 12288 | **96.0** | 48.0 |
| DoK `victory.smu` | 0x3580 = 13696 | **107.0** | 53.5 |
| DoK `lordsoth.smu` | 0x2F80 = 12160 | **95.0** | 47.5 |
| DoK `sorrow.smu` | 0x2680 = 9856 | **77.0** | 38.5 |
| DoK `undead.smu` | 0x2D00 = 11520 | **90.0** | 45.0 |

**Cross-check:** DoK `krynn.smu` contains an inline `SID_Tempo` (sID=136) event with `data=148` in every TRAK. Per the SMUS spec, `SID_Tempo data` is the integer BPM directly. This confirms 148 BPM — exactly `18944 / 128`. The header and inline events agree; the current code is wrong.

---

## 3. Note Duration — Correct Encoding

### Current code (wrong)

`smus.ts` treats the `data` byte of a NOTE SEvent as:

```typescript
// NOTE/REST `data` byte: low 6 bits = duration ticks; top two bits are flags.
const DUR_MASK = 0x3f;
const CHORD_BIT = 0x80;
const TIE_BIT   = 0x40;
```

This interprets the low 6 bits as raw linear tick count. It is not.

### Authoritative spec (EA IFF SMUS)

The `data` byte of a NOTE SEvent is a **packed note-value code**, not a tick count:

```c
// SNote data byte layout (from EA IFF SMUS specification):
typedef struct {
  UBYTE tone;           // NOTE: pitch (MIDI note 0-127)
  unsigned chord   :1; // bit 7 — chorded with next note
  unsigned tieOut  :1; // bit 6 — tied to following note of same pitch
  unsigned nTuplet :2; // bits 5:4 — 0=normal, 1=triplet, 2=quintuplet, 3=septuplet
  unsigned dot     :1; // bit 3 — dotted (multiply duration by 3/2)
  unsigned division:3; // bits 2:0 — note-value code
} SNote;
```

**`division` codes** (bits 2:0):

| division | Note value | Beats (quarter = 1 beat) |
|----------|-----------|--------------------------|
| 0 | whole note | 4.0 |
| 1 | half note | 2.0 |
| 2 | quarter note | 1.0 |
| 3 | eighth note | 0.5 |
| 4 | sixteenth note | 0.25 |
| 5 | 32nd note | 0.125 |
| 6 | 64th note | 0.0625 |
| 7 | 128th note | 0.03125 |

**Modifier fields:**

- `dot` (bit 3): if set, multiply beats by 1.5
- `nTuplet` (bits 5:4): `[1.0, 2/3, 4/5, 6/7]` for `[normal, triplet, quintuplet, septuplet]`

### Correct formula

```typescript
function smusDurationBeats(data: number): number {
  const division = data & 0x07;
  const dot      = (data >> 3) & 0x01;
  const nTuplet  = (data >> 4) & 0x03;

  let beats = 4.0 / Math.pow(2, division);          // 4=whole, 2=half, 1=quarter…
  if (dot) beats *= 1.5;
  if (nTuplet === 1) beats *= 2.0 / 3.0;            // triplet
  else if (nTuplet === 2) beats *= 4.0 / 5.0;       // quintuplet
  else if (nTuplet === 3) beats *= 6.0 / 7.0;       // septuplet

  return beats;
}

// Convert to seconds:
const secPerQuarter = 60.0 / (tempoRaw / 128);      // = 60 * 128 / tempoRaw
const durationSecs  = smusDurationBeats(data) * secPerQuarter;
```

### Worked example — CoK `krynn` (tempoRaw=15360)

```
BPM_correct = 15360 / 128 = 120 quarter-notes/min
secPerQuarter = 60 / 120 = 0.500 s

Opening 4 events of TRAK 0:
  [C4]  data=0x0B (bin: 0000 1011): division=3(eighth) dot=1 ntup=0
        beats = 4/8 * 1.5 = 0.75   → 0.375 s  (dotted eighth)

  [C4]  data=0x04 (bin: 0000 0100): division=4(16th) dot=0 ntup=0
        beats = 4/16 = 0.25         → 0.125 s  (sixteenth)

  [C4]  data=0x03 (bin: 0000 0011): division=3(eighth) dot=0 ntup=0
        beats = 4/8 = 0.5           → 0.250 s  (eighth)

  [B3]  data=0x03: eighth           → 0.250 s
```

This produces a classic dotted-eighth + sixteenth upbeat followed by running eighth notes — a musically sensible march opening pattern. The old code would compute `dur=11 * secPerTick` where `secPerTick = 60/60/8 = 0.125`, giving 1.375 s for the first note — 3.67× too long.

**DoK `krynn.smu` also uses nTuplet=1 (triplet, `*2/3`)** for its characteristic swing feel. Duration values 17=0x11 (half triplet), 19=0x13 (eighth triplet), and 26=0x1A (dotted-quarter triplet=1 beat) appear extensively. The old 6-bit mask `data & 0x3F` read these as raw values 17, 19, 26 — large nonsensical tick counts that would badly mangle the triplet passages.

---

## 4. Chord and Tie Flags

The current code has these flag positions **correct** (bit 7 = chord, bit 6 = tie), but the mask `data & 0x3F` for duration strips both bits 5:4 (the `nTuplet` field) as well as bits 3 (dot) and 2:0 (division). The mask must be removed; `durationBeats()` above extracts all fields individually from the raw `data` byte.

---

## 5. Instrument / Timbre

### What the Krynn SMUS files reference

Every TRAK in CoK/DoK begins with a `SID_Instrument` event (sID=129) that selects an instrument register. The SMUS `INS1` chunks map register indices to named instruments. All names (`in1`–`in7`) resolve into the `soundData` file on disk, which is a flat sequence of `NAME` + `FORM INST` definitions backed by 7-octave 8SVX waveforms.

### CoK `krynn` — instrument register to waveform

From parsing `amiga_extracted/ChampionsOfKrynn/disk1/Sound/soundData`:

| Register | INS1 name | Waveform sample | Web Audio equivalent |
|----------|-----------|-----------------|----------------------|
| 0 (`in1`) | `square` | 50% duty square | `OscillatorNode` type `'square'` ✓ |
| 1 (`in2`) | `tri` | triangle-like multi-level | `OscillatorNode` type `'triangle'` (close) or `PeriodicWave` |
| 2 (`in3`) | `odd` | complex odd-harmonic shape | `OscillatorNode` type `'sawtooth'` (approximation) or `PeriodicWave` |
| 3 (`in4`) | `saw` | sawtooth | `OscillatorNode` type `'sawtooth'` |
| 4 (`in5`) | `odd2` | variant odd shape | `OscillatorNode` type `'sawtooth'` (approximation) |
| 5 (`in6`) | `odd` | (same as in3) | same |
| 6 (`in7`) | `odd2` | (same as in5) | same |

**CoK `krynn` track-to-instrument assignment:**
- TRAK 0 → INSTR 0 (`in1` = `square`) — main melody voice
- TRAK 1 → INSTR 1 (`in2` = `tri`) — harmony/bass voice
- TRAK 2 → INSTR 2 (`in3` = `odd`) — accompaniment pattern
- TRAK 3 → INSTR 3 (`in4` = `saw`) — bass/counter-melody

The current `audioManager.ts` plays **all four TRAKs as square wave** (`osc.type = 'square'`). Three of the four voices are wrong.

### What the `soundData` waveforms actually look like

All five waveforms in `soundData` are **7-octave multi-sample 8SVX** bodies (VHDR: ctOctave=7, 254-byte BODY). The octaves are stored highest-pitch first:

```
bytes  0–1:   octave 7 (2 samples, highest pitch — shortest period)
bytes  2–5:   octave 6 (4 samples)
bytes  6–13:  octave 5 (8 samples)
bytes 14–29:  octave 4 (16 samples)
bytes 30–61:  octave 3 (32 samples)
bytes 62–125: octave 2 (64 samples)
bytes 126–253: octave 1 (128 samples, lowest pitch — longest period, best quality)
```

For Web Audio wavetable use, the **lowest octave** (last 128 samples) is the useful single-cycle waveform. The `square` lowest octave is a perfect 128-sample 50% duty square — identical to Web Audio's own `'square'` oscillator, confirming that oscillator type is correct for `in1`.

The `tri` and `odd` lowest octaves contain multi-level approximations to triangle and odd-harmonic tones respectively. The `saw` is a linear ramp (sawtooth).

### Instrument envelopes

Each `INST` definition in `soundData` includes:
- `VLUM` — reference to a named volume envelope (`holdEnv` = sustain hold; `basicEnv` = standard ADSR)
- `PTCH` — reference to a pitch-modulation envelope (`slowtrem` for `in2`, `fasttrem` for `in3`)

The `in2` (`tri`) and `in3` (`odd`) instruments have **pitch tremolo** applied (`slowtrem` / `fasttrem`). The envelopes are IFF `FORM ENVL` chunks in `soundData`. Decoding those is optional for a first-pass fix but would improve authenticity for the `tri` and `odd` voices.

### periodTable file

`amiga_extracted/ChampionsOfKrynn/disk1/Sound/periodTable` is a 640-byte binary table of 160 × `uint32` big-endian values indexed by MIDI note number (entry[0] = MIDI 0, entry[69] = A4). Values are NTSC Paula period register values (verified: entry[69] = 8135 ≈ 3,579,545 / 440). This table is **only used by the original 68000 code to program Paula hardware**. Our Web Audio path uses `midiToHz(note) = 440 * 2^((note-69)/12)` which is already correct and does not need this table.

---

## 6. 8SVX Multi-Octave Bug in `svx8.ts`

`svx8.ts` has a comment and logic error for multi-octave waveforms:

```typescript
// svx8.ts line 62-64 (current):
// "For a multi-octave sample the BODY holds octave 0 first (the lowest, longest-period copy)."
const firstOctave = oneShotSamples + repeatSamples;  // = 0 + 2 = 2 for soundData waveforms
const count = ctOctave > 1 && firstOctave > 0 && firstOctave <= body.length
              ? firstOctave : body.length;
```

For the `soundData` instruments: `oneShotSamples=0`, `repeatSamples=2` (the VHDR counts for the **highest** octave), so `firstOctave=2`. The code takes only 2 bytes — the highest octave (a 2-sample trivial approximation). The comment is factually wrong: **highest octave is stored first** per the 8SVX spec.

This bug does not affect playback of real SFX files (like `arrow`, `hit`, etc.) which have ctOctave=1 and large body lengths. It only affects `soundData` instrument waveforms loaded for SMUS playback.

---

## 7. Fix List

### 7.1 `packages/engine/src/audio/smus.ts`

**Fix 1 — Tempo divisor (one line):**

```typescript
// Line 62, current:
const tempoBPM = tempoRaw / 256;  // WRONG — produces half speed
// Fix:
const tempoBPM = tempoRaw / 128;  // correct: spec says 128ths of a quarter-note/min
```

**Fix 2 — Duration byte interpretation:**

Remove the current mask constants and replace the note/rest decoding with symbolic note-value computation. In `parseTrack`, when building a note event, replace:

```typescript
// Current (wrong):
durationTicks: data & DUR_MASK,
chord: (data & CHORD_BIT) !== 0,
tie: (data & TIE_BIT) !== 0,
```

with fields that carry the real information:

```typescript
// Fixed: extract all subfields
const division = data & 0x07;
const dot      = (data >> 3) & 0x01;
const nTuplet  = (data >> 4) & 0x03;
const chord    = (data & 0x80) !== 0;
const tie      = (data & 0x40) !== 0;
// duration in quarter-note beats:
let beats = 4.0 / (1 << division);
if (dot)           beats *= 1.5;
if (nTuplet === 1) beats *= 2.0 / 3.0;
else if (nTuplet === 2) beats *= 4.0 / 5.0;
else if (nTuplet === 3) beats *= 6.0 / 7.0;
```

The exported `SmusEvent` type should change `durationTicks: number` to `durationBeats: number` (or keep the field name and change its semantic, updating all callers).

**Fix 3 — SID_Tempo inline event (sID=136) handling:**

Currently decoded as `'other'`. It should update a running tempo that the caller can use:

```typescript
// sID 136 = SID_Tempo: data byte = integer BPM directly
} else if (sID === 136) {
  events.push({ kind: 'tempo', bpm: data });
```

The `audioManager.ts` `scheduleTrack` loop should respond to `'tempo'` events by recomputing `secPerBeat`.

### 7.2 `apps/web/src/audio/audioManager.ts`

**Fix 4 — Remove `ticksPerQuarter` entirely:**

`ticksPerQuarter` and `secPerTick = 60 / tempoBPM / ticksPerQuarter` are wrong abstractions. With the fixed `smus.ts` emitting `durationBeats`, the player simply:

```typescript
// In scheduleTrack:
const secPerBeat = 60.0 / score.tempoBPM;  // tempoBPM already correct from smus.ts fix
// Per note:
const durSecs = ev.durationBeats * secPerBeat;
```

**Fix 5 — Per-voice oscillator type:**

The `scheduleNote` function currently hardcodes `osc.type = 'square'`. The voice's oscillator type should be passed in or resolved from the instrument event:

```typescript
// In scheduleTrack, track the current instrument:
let oscType: OscillatorType = 'square'; // default / fallback
// On SmusEvent 'instrument':
oscType = resolveOscType(ev.instrument, score.instruments);

// resolveOscType(register, instrumentNames):
// instrument names are 'in1'..'in7'; waveforms from soundData:
// in1 (reg 0) = square  -> 'square'
// in2 (reg 1) = tri     -> 'triangle'
// in3 (reg 2) = odd     -> 'sawtooth' (approximation; 'square' + 2nd harm dominant)
// in4 (reg 3) = saw     -> 'sawtooth'
// in5 (reg 4) = odd2    -> 'sawtooth'
// in6 (reg 5) = odd     -> 'sawtooth'
// in7 (reg 6) = odd2    -> 'sawtooth'
```

Simple mapping for an immediate fix (no wavetable loading required):

```typescript
function resolveOscType(register: number, instNames: string[]): OscillatorType {
  const waveMap: Record<string, OscillatorType> = {
    'in1': 'square',
    'in2': 'triangle',
    'in3': 'sawtooth',
    'in4': 'sawtooth',
    'in5': 'sawtooth',
    'in6': 'sawtooth',
    'in7': 'sawtooth',
  };
  const name = instNames[register] ?? '';
  return waveMap[name] ?? 'square';
}
```

This is an approximation. Authentic reproduction requires loading the actual 8SVX waveforms from `soundData` as Web Audio `PeriodicWave` nodes (see §8).

---

## 8. Authentic Timbre Path (optional enhancement)

For each instrument, load the 254-byte waveform from `soundData`, extract the **lowest octave** (last 128 bytes), and create a `PeriodicWave`:

```typescript
// Extract lowest octave from 254-byte multi-octave 8SVX body:
// Highest octave stored first; the lowest 128-sample octave starts at byte 126.
function extractLowestOctave(body: Uint8Array): Float32Array {
  // For ctOctave=7, repeatHiSamples=2: lowest octave = 2^(7-1)*2 = 128 samples
  const lowestOctaveLen = 128;
  const lowestOctaveOffset = body.length - lowestOctaveLen;  // = 126 for 254-byte body
  const cycle = new Float32Array(lowestOctaveLen);
  for (let i = 0; i < lowestOctaveLen; i++) {
    const s = body[lowestOctaveOffset + i];
    cycle[i] = (s < 128 ? s : s - 256) / 127.0;  // signed byte -> [-1, 1]
  }
  return cycle;
}

// Build PeriodicWave from the single-cycle waveform:
function buildPeriodicWave(ctx: AudioContext, cycle: Float32Array): PeriodicWave {
  const fft = computeRealFFT(cycle);  // standard DFT; size/2+1 real+imag pairs
  return ctx.createPeriodicWave(fft.real, fft.imag, { disableNormalization: true });
}
```

Use a looped `AudioBufferSourceNode` as a simpler alternative:

```typescript
function makeWavetableSource(ctx: AudioContext, cycle: Float32Array, freqHz: number): AudioBufferSourceNode {
  const buf = ctx.createBuffer(1, cycle.length, ctx.sampleRate);
  buf.copyToChannel(cycle, 0);
  const src = ctx.createBufferSource();
  src.buffer = buf;
  src.loop = true;
  // playbackRate = targetFreq / (sampleRate / cycleLength)
  src.playbackRate.value = (freqHz * cycle.length) / ctx.sampleRate;
  return src;
}
```

The `svx8.ts` `decode8svx` function needs the separate bug fix (§6) to correctly decode multi-octave waveforms for instrument use: take the full 254-byte body and identify the lowest-octave slice rather than stopping at `firstOctave=2`.

---

## 9. Summary of Key Facts

| Property | Current (wrong) | Correct |
|----------|-----------------|---------|
| Tempo formula | `tempoRaw / 256` | `tempoRaw / 128` |
| Duration model | linear ticks (low 6 bits of data byte) | note-value code (3-bit division + dot + nTuplet) |
| CoK `krynn` BPM | 60 | **120** |
| DoK `krynn.smu` BPM | 74 | **148** |
| TRAK 0 waveform | square | square (correct already) |
| TRAK 1 waveform | square | triangle (`tri` waveform) |
| TRAK 2 waveform | square | sawtooth (approx. `odd` waveform) |
| TRAK 3 waveform | square | sawtooth (`saw` waveform) |
| `ticksPerQuarter` | 8 (guessed) | **not needed** (remove; use beats directly) |

The two biggest audible problems in order of severity:
1. **Tempo at half speed** — single-line fix in `smus.ts`.
2. **Duration bytes misread** — moderate rewrite of the SNote decoder in `smus.ts` and callers in `audioManager.ts`.
3. **Wrong waveforms** — three of four CoK voices are wrong; a simple oscillator-type map fixes the worst of it.

---

## 10. Open Questions / Unknowns

1. **Instrument envelopes from `soundData`:** The `ENVL` / `INST` / `PTCH` chunks define volume ADSR and pitch LFO envelopes. `in2` (`tri`) has `slowtrem` pitch modulation; `in3` (`odd`) has `fasttrem`. These are IFF `FORM ENVL` structures in `soundData`. Decoding them would add tremolo/vibrato to the triangle/odd voices. Format is not publicly documented but is parseable from the file. **UNKNOWN** format detail; deferred.

2. **DoK `krynn.smu` uses triplet durations extensively** (nTuplet=1, values 17/18/19/20/26). These are entirely broken in the current code. After the duration fix they will work correctly.

3. **`soundData` instrument waveforms for DoK:** DoK `sound/` directory has the same `soundData` file (it is byte-identical to the CoK version — **inferred** from inventory; not byte-checked here). If DoK uses different instruments for some SMUS files, those may not be covered by the waveform map in §7.2. DoK `INS1` references should be verified against DoK's own `soundData`.

4. **SMUS looping:** SMUS has no native loop command. The Gold Box game engine restarts the entire score on completion (all TRAK events exhausted). The current `audioManager.ts` does not implement looping of SMUS scores — after the score plays through once it stops. A caller-side restart or a score-wrap mechanism is needed for continuous in-game music. **UNKNOWN** whether the original engine had any partial-section loops or simply restarted from the top.

5. **DoK instrument register misalignment:** DoK `krynn.smu` has 7-register `INS1` references (same set as CoK). Some DoK TRAKs may start with `INSTR idx` values higher than 3. The waveform map should cover all 7 registers.

---

## 11. Implications for Our Engine

1. **The tempo bug is the #1 fix.** One character change (`256` → `128` in `smus.ts:62`) halves the playback time of every score immediately. This alone will bring CoK `krynn` from ~2:30 (halved) to ~1:15 (correct), noticeably right-sounding.

2. **The duration bug requires a `SmusEvent` type change.** The exported `durationTicks` field name is a misnomer. Rename to `durationBeats` (a floating-point beat count) and update `audioManager.ts` to multiply by `secPerBeat`. Remove the `ticksPerQuarter` field entirely from `AudioManager` — it has no meaning in the correct model.

3. **`ticksPerQuarter` in `audioManager.ts` is no longer needed** after the duration fix. Its public setter `setTicksPerQuarter()` should be deprecated and removed.

4. **Oscillator types for voices 1–3** are a meaningful audible improvement. The `tri` melody harmony voice (TRAK 1) sounds distinctly different from square, and the `saw` bass (TRAK 3) is characteristic of Amiga music. Switching to `'triangle'` and `'sawtooth'` oscillators requires only a lookup table change.

5. **Authentic waveform loading** (`PeriodicWave` from `soundData`) is optional enhancement. The `tri`→`'triangle'` and `odd/saw`→`'sawtooth'` substitutions are close enough for a first-pass faithful playback; the `PeriodicWave` path is a follow-up slice.

6. **`svx8.ts` multi-octave bug** affects only `soundData` instrument waveform decoding, not real SFX files. Fix it if/when loading `soundData` as `PeriodicWave` instrument sources. For now, the simple oscillator-type map in §7.2 is the preferred path.

---

## Sources

- AmigaOS Wiki: [SMUS IFF Simple Musical Score](https://wiki.amigaos.net/wiki/SMUS_IFF_Simple_Musical_Score) — authoritative spec, SHDR layout, SNote encoding, SEvent sID table
- AmigaOS Wiki: [8SVX IFF 8-Bit Sampled Voice](https://wiki.amigaos.net/wiki/8SVX_IFF_8-Bit_Sampled_Voice) — VHDR layout, ctOctave, multi-octave storage
- Existing project research: `docs/engine/research/audio-formats-krynn.md` (superseded here for SMUS details)
- Inspected files:
  - `amiga_extracted/ChampionsOfKrynn/disk1/Sound/krynn` — SHDR tempoRaw=15360, INS1 in1-in7, 4 TRAKs
  - `amiga_extracted/ChampionsOfKrynn/disk1/Sound/soundData` — 5 waveforms (square/saw/tri/odd/odd2) + INST definitions for in1-in7
  - `amiga_extracted/ChampionsOfKrynn/disk1/Sound/periodTable` — 160 × u32 NTSC period table, MIDI-indexed
  - `amiga_extracted/DeathKnightsOfKrynn/disk1/sound/krynn.smu` — SHDR tempoRaw=18944, SID_Tempo=148 (confirms ÷128)
  - `amiga_extracted/DeathKnightsOfKrynn/disk1/sound/action.smu`, `horror.smu`, `victory.smu`, `lordsoth.smu`, `sorrow.smu`, `undead.smu`
  - `packages/engine/src/audio/smus.ts` — bugs at lines 62 (tempo) and 45-49 (duration)
  - `apps/web/src/audio/audioManager.ts` — bugs at `scheduleNote` (osc type) and `playMusic` (secPerTick)
