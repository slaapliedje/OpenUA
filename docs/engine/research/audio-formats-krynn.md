# Audio Formats — Krynn Trilogy (CoK / DoK / DQK)

**Research date:** 2026-06-21  
**Scope:** Asset inventory, format details, in-browser playback strategy for the Greybox
graphics-mode switch feature (DOS-EGA / Amiga / C64).  
**Primary target:** Champions of Krynn (CoK); also Death Knights of Krynn (DoK) and
The Dark Queen of Krynn (DQK).

---

## 1. Asset Inventory by Platform

### 1.1 DOS — Champions of Krynn (CoK)

| File | Size | Role | Format confirmed |
|------|------|------|-----------------|
| `MIDI.DAX` | 7,369 B | Music (5 tracks) | DAX container; custom SSI music driver format inside |
| `SOUND.DAX` | 14,206 B | Sound effects (1 block, 9 logical SFX) | DAX container; custom PC-speaker SFX data |

**Music format detail (`MIDI.DAX`):**  
DAX container; 5 blocks (IDs 1–5). After RLE-decompression, each block starts with 6 `0xE9 XX YY` triplets followed by a 26-byte chromatic pitch-offset table, a 26-byte octave table, zeroed padding, and then the song note-sequence data (pairs of `period_lo period_hi` values at ~7.7 Hz apparent tempo). The value range in the triplets (e.g. `0x02CB`=715, `0x02E3`=739) is consistent with 8253 PIT timer divisors for PC-speaker pitch (1,193,180 / 715 ≈ 1,669 Hz). This is **not** raw OPL2 register data (values exceed valid OPL2 register addresses). Exact opcode semantics are UNKNOWN — this is a custom SSI music driver format requiring reverse-engineering.

**Sound effects detail (`SOUND.DAX`):**  
DAX container; 1 block (9 logical SFX packed inside). Block starts `7D 00 00 01 ...` (not GLBR). Content is custom PC-speaker beep/tone SFX data (the same raw SFX block as COAB and DoK, byte-for-byte identical 14,064-byte decompressed payloads).

**Sound hardware supported (CoK DOS):**  
Per VGMPF ([Champions of Krynn DOS](https://www.vgmpf.com/Wiki/index.php?title=Champions_of_Krynn_%28DOS%29)): Roland LAPC-1/MT-32 and AdLib/Sound Blaster, plus PC Speaker. Music composed on Atari ST (Master Tracks), conversion by Rob Hubbard. The MIDI.DAX is the **only** music container — Roland and AdLib versions may live as different block IDs within it, or may share the same data. (The block IDs 1–5 do not obviously separate by hardware variant. **UNKNOWN** whether one DAX block is Roland-only and another is AdLib-only.)

---

### 1.2 DOS — Death Knights of Krynn (DoK)

| File | Size | Role | Format confirmed |
|------|------|------|-----------------|
| `MUSIC.DAX` | 28,017 B | Music driver + 7 songs | DAX container, 8 blocks |
| `SOUND.DAX` | 14,206 B | Sound effects | Byte-identical to CoK SOUND.DAX |

**Music format detail (`MUSIC.DAX`):**  
DAX container; 8 blocks.  
- Block 0 (id=0, raw=12,528 B): x86 machine code — the actual AdLib/OPL2 **sound driver** packaged inside the DAX. Decompressed starts `55 8B EC 1E 06 8C C8...` (PUSH BP, MOV BP SP = standard 8086 function prologue). This is the in-game music player TSR/overlay.  
- Blocks 1–7 (raw sizes: 5,098 / 1,268 / 1,086 / 712 / 982 / 858 / 5,116 B): all decompress to `47 4C 42 52` = **`GLBR`** magic. These are the 7 song files in the `GLBR` (Gold Box Library Record) format.

`GLBR` appears to be SSI's proprietary AdLib/OPL2 music container. The header `47 4C 42 52 00 00 01 BC FF FF 00 20 00 0C 00 0C...` is followed by what look like note-on/off tables (values `0x0C00`, `0x1200`, `0x0800` repeat for what may be volume/timing registers). Format details beyond the magic bytes are **UNKNOWN** — no public documentation found for `GLBR`.

**Sound hardware:** AdLib OPL2 and Sound Blaster for music (per VGMPF). SFX is PC-speaker only (same data as CoK). Composed by David Govett in Dr. T MIDI sequencer, converted for game. Sound Blaster support debuted with DoK.

---

### 1.3 DOS — The Dark Queen of Krynn (DQK)

DQK uses the Miles Sound System (MSS/AIL) — confirmed by the copyright string `Copyright (C) 1991 John Miles` in all `.ADV` driver files.

#### Music files (XMI format, DISK1):

| File(s) | Size | Hardware target |
|---------|------|----------------|
| `ADDQ1-3.XMI` | 8,516 / 6,390 / 5,830 B | AdLib / Sound Blaster |
| `RODQ1-3.XMI` | 8,520 / 6,532 / 5,830 B | Roland MT-32 (MPU-401) |
| `PCDQ1-3.XMI` | 1,124 / 1,606 / 960 B | PC Speaker |
| `TYDQ1-3.XMI` | 2,782 / 3,656 / 2,546 B | Tandy 1000 (unused per `UAFILES.TXT`) |

**XMI format:** IFF-based (`FORM/XDIR` + `CAT/XMID` + `FORM/XMID` per song). Each `XMID` contains a `TIMB` chunk (10 AdLib patch references, 2 bytes each) and an `EVNT` chunk (compressed MIDI event stream). XMI plays at a fixed 120 Hz clock (tempo 500,000 µs + PPQN 60). Notes carry duration inline (no separate Note Off needed). See [XMI Format — ModdingWiki](https://moddingwiki.shikadi.net/wiki/XMI_Format).

Music structure (per `TUNES.TXT` and `UA_INSTR.TXT`):
- `ADDQ1.XMI`: Overture
- `ADDQ2.XMI`: Treasure / Foes / Battle / Mystery / Uh-Oh / Evil March (6 embedded mini-tunes)
- `ADDQ3.XMI`: Victory

#### Instrument bank:
`INSTR.AD` (3,982 B): 199 OPL2 instrument patches in Miles Sound System AIL format. Header is a table of 199 × 6-byte entries `(patch_number:u16, abs_offset:u32)`, pointing into 199 × 14-byte OPL2 operator records. Offsets are sequential starting at byte 1,196, stride 14.

#### Sound drivers:
| File | Role |
|------|------|
| `ADLIB.ADV` | AdLib OPL2 driver (MSS/AIL) |
| `MT32MPU.ADV` | Roland MT-32 driver (MSS/AIL) |
| `PCSPKR.ADV` | PC Speaker driver (MSS/AIL) |
| `SBDIG.ADV` | Sound Blaster digitized (MSS/AIL) |
| `TANDY.ADV` | Tandy driver (MSS/AIL, per UAFILES.TXT: unused) |

#### Sound effects:
| File | Size | Format | Notes |
|------|------|--------|-------|
| `SFXDQ.VOC` | 30,910 B | Creative Voice File 1.0, codec=1 (4-bit ADPCM) | 13 SFX at 5,555–11,111 Hz, marker-delimited |
| `SOUNDS.GLB` | 24,904 B | HLIB/DIG4, 4-bit ADPCM | PC-speaker SFX variant; 13 samples; matches UASOUND.TXT offsets exactly. Sample headers are **big-endian** (sample_rate=7,418 Hz at offset 0x48 reads `BE` not `LE`, despite UASOUND.TXT's `little-endian` note) |

**SFX names** (from `UASOUND.TXT`): Cast, Flame, Sorcery, Die, Sling, Hit, Lightning, Swing, Walk, Fireball, Bow, Sploosh, Crackle.  
**SFX for browser:** Use `SFXDQ.VOC` (4-bit ADPCM, standard Creative ADPCM decoding) — not `SOUNDS.GLB` (PC-speaker beep version). `SFXDQ.VOC` is the Sound Blaster digitized version used in-game when SB is selected.

---

### 1.4 Amiga — Champions of Krynn (CoK)

Amiga files in `amiga_extracted/ChampionsOfKrynn/disk1/Sound/`:

| File | Format | Role |
|------|--------|------|
| `krynn` | IFF `FORM/SMUS` (332 B SHDR+TRAK) | The **only** music track |
| `arrow`, `cast2`, `cast3Samp`, `dead`, `fireb`, `gate`, `hit`, `lightSamp`, `pad`, `pad2`, `swish`, `tuningfork`, `whislSamp2` | IFF `FORM/8SVX` | Sound effects (signed 8-bit mono PCM, big-endian) |
| `cast3`, `cast4` | IFF `FORM/INST` | Instrument samples |
| `periodTable` | Raw binary (big-endian period values) | Amiga hardware note period lookup table |
| `soundData` | IFF `NAME` wrapper around 8SVX (square wave) | Shared instrument/SFX data |

**SMUS format (`krynn`):** IFF `FORM/SMUS`. `SHDR` chunk: tempo field (big-endian `u16`), track count, type. `NAME` chunk: track name. `INS1` chunks: instrument references. `TRAK` chunks: event streams of 16-bit `SEvent` records (pitch 0–127 + duration encoding). Instruments for this file reference `in3` (probably `krynn` itself is used as a self-referencing sample). See [SMUS IFF — AmigaOS Wiki](https://wiki.amigaos.net/wiki/SMUS_IFF_Simple_Musical_Score).

**Note:** CoK Amiga has only **1 music track** (`krynn` = the title/main theme). All other audio is SFX (8SVX). This is dramatically less music than DoK Amiga.

---

### 1.5 Amiga — Death Knights of Krynn (DoK)

Files in `amiga_extracted/DeathKnightsOfKrynn/disk1/sound/` (same structure on disk2):

| File | Format | Role |
|------|--------|------|
| `krynn.smu` | IFF `FORM/SMUS` (4,658 B, 4 TRAK chunks) | Main theme |
| `action.smu` | IFF `FORM/SMUS` (1,084 B, 4 TRAK) | Battle/action music |
| `horror.smu` | IFF `FORM/SMUS` (284 B) | Horror sting |
| `lordsoth.smu` | IFF `FORM/SMUS` (550 B) | Lord Soth encounter |
| `sorrow.smu` | IFF `FORM/SMUS` (410 B) | Sorrow/death |
| `undead.smu` | IFF `FORM/SMUS` (632 B) | Undead encounter |
| `victory.smu` | IFF `FORM/SMUS` (1,810 B, 4 TRAK) | Victory fanfare |
| `arrow`, `cast2`, `cast3Samp`, `dead`, `fireb`, `gate`, `hit`, `lightSamp`, `pad`, `pad2`, `swish`, `tuningfork`, `whislSamp2` | IFF `FORM/8SVX` | Sound effects (same files as CoK) |
| `cast3`, `cast4` | IFF `FORM/INST` | Instrument samples |
| `soundData` | IFF `NAME`/8SVX | Shared data |

**SMUS detail:** `krynn.smu` has `SHDR` tempo=18944 (big-endian), 4 tracks. `INS1` references `in3` (internal instrument). Each `TRAK` is a stream of 16-bit `SEvent` records. The instruments used are internal/bundled, **not** referencing external 8SVX files by name (the `INS1` chunk just says `in3`). This means SMUS playback needs the Amiga's hardware sound channels to render the `TRAK` events using whatever sample is loaded into `in3`.

---

### 1.6 Amiga — The Dark Queen of Krynn (DQK)

Files in `amiga_extracted/DarkQueenOfKrynn/disk1/Disk1/`:

| File | Size | Format | Role |
|------|------|--------|------|
| `DQK.MX` | 68,580 B | **`MXTX`** (custom SSI) | Music sequences — **UNKNOWN format** |
| `MUSIC.SLB` | 6,548 B | **`SLBR`** (custom SSI) | Sound Library — 8 embedded tracks |
| `SOUNDS.GLB` | 61,602 B | **`GLIB/DIG8`** | SFX — 13 samples, 8-bit signed PCM at 7,418 Hz |

**`MXTX` (`DQK.MX`):** Magic `4D 58 54 58`. Word at offset 4 = 107 (possibly song count). No `FORM` or `SMUS` chunks inside. Contains what appears to be raw note/tempo sequence data starting early in the file. Format is completely undocumented. The data at file offset ~30 repeats patterns like `00 0F 26 99 00 30` which may encode (command, pitch, duration). **UNKNOWN — needs dedicated RE pass.**

**`SLBR` (`MUSIC.SLB`):** Magic `53 4C 42 52`. Byte at offset 8 = 8 (track count). 2-byte offsets at bytes 10+ point to 8 embedded track data regions (offsets: 0, 1974, 2222, 2702, 3380, 3762, 4018, 4520). Track data resembles SMUS `TRAK` event bytes but without IFF wrappers. **UNKNOWN — likely a compressed SMUS variant.**

**`GLIB/DIG8` (`SOUNDS.GLB`):** Magic `GLIB` (not `HLIB`). `DIG8` sub-type (not `DIG4`). Big-endian. 13 SFX samples, pointer table at offset 16 (big-endian `u32`). First sample at offset 72: sample_rate=7,418 Hz (big-endian `u16`), data_len=8,736 B (8-bit signed PCM). The much larger file size vs. DOS (61 KB vs. 25 KB) confirms `DIG8` = uncompressed 8-bit PCM while `DIG4` = 4-bit ADPCM.

---

### 1.7 C64 — All Games

No C64 assets are in the repository. C64 music would be SID format (6581/8580 chip). **Out of scope until C64 assets are supplied by the user.** Flag as "C64 mode — SID, not yet supported."

---

## 2. Format Details Summary Table

| Platform | Game | Music container | Music codec | SFX container | SFX codec |
|----------|------|----------------|-------------|---------------|-----------|
| DOS | CoK | `MIDI.DAX` (custom DAX/SSI) | Custom 0xE9-prefix format, PC-speaker PIT divisors | `SOUND.DAX` | Custom PC-speaker beep data |
| DOS | DoK | `MUSIC.DAX` (custom DAX/SSI) | GLBR sub-blocks (OPL2 AdLib, driver in block 0) | `SOUND.DAX` | Custom PC-speaker beep data |
| DOS | DQK | `ADDQ*.XMI` / `RODQ*.XMI` / `PCDQ*.XMI` | XMI (Miles Sound System AIL/XMIDI) | `SFXDQ.VOC` | Creative Voice File, 4-bit ADPCM (Creative codec 1) |
| Amiga | CoK | `Sound/krynn` | IFF SMUS (1 track, Amiga Paula hardware) | `Sound/arrow` etc. | IFF 8SVX (8-bit signed PCM, big-endian) |
| Amiga | DoK | `sound/*.smu` (7 tracks) | IFF SMUS (multi-track, Amiga Paula hardware) | `sound/arrow` etc. | IFF 8SVX (8-bit signed PCM, big-endian) |
| Amiga | DQK | `DQK.MX` + `MUSIC.SLB` | MXTX/SLBR — **UNKNOWN** custom SSI | `SOUNDS.GLB` (GLIB) | GLIB/DIG8 (8-bit signed PCM, big-endian) |
| C64 | All | — | SID | — | SID |

---

## 3. In-Browser Playback Strategy (Web Audio API)

### 3.1 DOS XMI (DQK) — Recommended first slice

**Path:** XMI → standard MIDI event stream → OPL2 FM synthesis via JS/WASM OPL emulator.

**Step 1 — XMI decode:**  
Write a TypeScript XMI parser (small, pure TS, no external dep). XMI is IFF-based with these twists vs. standard MIDI: (a) no separate Note Off — duration is embedded in Note On; (b) timing uses summed 7-bit delays at 120 Hz fixed clock (tempo=500,000 µs, PPQN=60); (c) multi-song CAT chunk. See [XMI Format — ModdingWiki](https://moddingwiki.shikadi.net/wiki/XMI_Format). Convert to a standard MIDI-like event timeline internally.

**Step 2 — OPL2 synthesis:**  
The `ADDQ*.XMI` AdLib versions play via OPL2 FM synthesis using `INSTR.AD` patches.

Library options:
- **`opl3` (npm: `opl3`, doomjs/opl3, MIT license):** Ported from Robson Cozendey's YMF262 emulator. Browser-capable, MIT. The GENMIDI instrument set is bundled; can swap in custom patches from `INSTR.AD`. Recommended for initial implementation. ([GitHub](https://github.com/doomjs/opl3), [npm](https://www.npmjs.com/package/opl3))
- **`@malvineous/opl` (MIT):** DOSBox OPL3 core compiled to WASM via Emscripten. More accurate but heavier. ([GitHub](https://github.com/Malvineous/opljs))
- **Nuked-OPL3 (nukeykt/Nuked-OPL3, LGPL-2.1):** Most accurate. **LGPL is usable in a MIT project** (LGPL is a lesser copyleft; it permits use in non-GPL software as long as the LGPL portion is dynamically linkable or object code is provided). In WASM via Emscripten the requirement is met by distributing the wasm binary separately from engine TS. However, this adds compliance burden. Prefer MIT libraries first.

**Step 3 — Connect to Web Audio API:**  
OPL emulator generates PCM samples → fill `AudioBuffer` → `AudioBufferSourceNode` or streaming via `ScriptProcessorNode` / `AudioWorklet`.

**Alternative path (simpler, for a quick win):**  
Convert `ADDQ*.XMI` → standard MIDI offline (Python script) → play with `spessasynth_core` (Apache-2.0, compatible with MIT) + a free GM soundfont (e.g. GeneralUser GS). This produces GM-quality audio rather than authentic AdLib timbre but requires zero WASM and works entirely in pure TypeScript. The `INSTR.AD` 199 OPL2 patches can later be converted to a micro-soundfont for authentic sound.

---

### 3.2 DOS FM (AdLib/OPL2) — CoK / DoK

CoK `MIDI.DAX` and DoK `MUSIC.DAX` / `GLBR` are **custom SSI formats with no known decoder**. Neither DRO nor raw OPL2 register writes. Reverse-engineering required before browser playback is possible.

**Intermediate option:** Record the game audio in DOSBox with the OPL capture feature (DRO format) and load DRO files in the browser via a DRO player. This is a preservation path, not an engine-native path. [The OPL Archive](https://opl.wafflenet.com/) has some Gold Box recordings.

---

### 3.3 Amiga SMUS / 8SVX (CoK / DoK)

**SMUS music:**  
No MIT-licensed JS SMUS player exists (as of 2026-06). SMUS is documented ([AmigaOS Wiki](https://wiki.amigaos.net/wiki/SMUS_IFF_Simple_Musical_Score)) and implementable in TypeScript:
- Parse SMUS IFF chunks: `SHDR` (tempo), `NAME`, `INS1` (instrument references), `TRAK` (event arrays of 16-bit `SEvent`).
- Resolve instruments: for the Gold Box games the `INS1` chunk references `in3` — likely a synthetic square-wave or the bundled `soundData` file (which is a plain 8SVX square wave). Load that as an `AudioBuffer`.
- Sequence events via Web Audio API `AudioContext.currentTime` scheduling.

This is a small, pure-TS implementation — no heavyweight dependency needed. Estimated 200–400 lines.

**8SVX SFX:**  
IFF 8SVX is straightforward: parse `VHDR` chunk (sample rate, compression, one-shot length), read `BODY` (signed 8-bit PCM, big-endian). Decode directly to `AudioBuffer` (convert signed 8-bit → float32). No external library needed. Pure TypeScript in ~80 lines.

---

### 3.4 Amiga DQK (MXTX / SLBR / DIG8)

`DQK.MX` (MXTX) and `MUSIC.SLB` (SLBR) are **undocumented custom SSI formats**. Require a dedicated RE pass before any playback path can be designed.

`SOUNDS.GLB` (GLIB/DIG8): 8-bit signed PCM, big-endian. Decode is trivial once the pointer table and per-sample headers are parsed — identical logic to 8SVX BODY.

---

### 3.5 DIG4 SFX (DQK DOS `SFXDQ.VOC`)

**Creative Voice File (VOC) with 4-bit ADPCM:**  
Parse the VOC header, iterate block-type markers (type 1 = audio, type 4 = marker). Codec = Creative ADPCM (type 1). Decode 4-bit ADPCM to signed 16-bit PCM, load into `AudioBuffer`. No external library needed — standard ADPCM decode is ~100 lines TypeScript. The 13 named SFX (Cast, Flame, Sorcery, Die, Sling, Hit, Lightning, Swing, Walk, Fireball, Bow, Sploosh, Crackle) are separated by VOC Marker blocks (0–12).

---

### 3.6 Library Licensing Summary

| Library | License | OPL2? | MIDI/XMI? | MOD/SMUS? |
|---------|---------|-------|-----------|-----------|
| `opl3` (doomjs) | **MIT** | Yes (OPL3 incl. OPL2 mode) | No | No |
| `@malvineous/opl` | **MIT** (WASM) | Yes | No | No |
| Nuked-OPL3 | **LGPL-2.1** | Yes (most accurate) | No | No |
| `spessasynth_core` | **Apache-2.0** | No | Yes (MIDI, XMF) | No |
| chiptune2.js | MIT (wrapper) + BSD (libopenmpt) | No | No | MOD/S3M/XM/IT |
| Wild Web MIDI | MIT (wrapper) + LGPL (wildmidi) | No | MIDI+XMI | No |

**XMI to MIDI conversion:** No pure-JS library with XMI support exists on npm as of 2026-06. WildMIDI (C/C++) supports XMI and has a browser WASM port ([wild-web-midi](https://github.com/zz85/wild-web-midi), MIT wrapper + LGPL core). Alternatively, write a pure-TS XMI→MIDI converter (~200 lines) and pipe to `spessasynth_core`.

---

## 4. Recommended First Implementable Slice

**Goal:** Play CoK title/intro music in the browser for at least one platform, with MIT-compatible stack.

**Recommended slice: DQK AdLib XMI via TS XMI parser + opl3 (MIT)**

1. Write `packages/engine/src/audio/xmi.ts`: pure-TS XMI IFF parser → normalized event timeline (note, patch, tempo, loop events). No deps.
2. Write `packages/engine/src/audio/opl2Adapter.ts`: wrap `opl3` (MIT npm package) to load `INSTR.AD` patches and accept note-on/off/patch-change events.
3. Write `packages/engine/src/audio/audioPlayer.ts`: schedule OPL2 output into a `ScriptProcessorNode` or `AudioWorklet`.
4. Hook into the graphics mode switch (`apps/web/src/ui/gswitch.ts`): when `setPlatform('DOS-EGA')` is called, start playing `ADDQ1.XMI` via AdLib; when `setPlatform('Amiga')` is called, pause and switch to the SMUS path (once implemented).

**Fallback simpler slice: DQK XMI → GM MIDI → spessasynth_core (Apache-2.0)**

1. Write `xmi.ts` XMI parser (same as above).
2. Convert XMI events to `Uint8Array` standard MIDI format (add Note Off events from embedded duration, rewrite timing headers).
3. Feed to `spessasynth_core` with a bundled GM soundfont (e.g. `GeneralUser GS Lite.sf2`). Sound will be GM-quality, not OPL2 timbre.

This path has **zero WASM** complexity and can produce a working audio demo in < 2 days of engine work.

**Second slice: CoK/DoK Amiga SMUS + 8SVX**

1. Write `packages/engine/src/audio/smus.ts`: IFF SMUS parser (SHDR, TRAK events, INS1 lookup). Pure TS, ~300 lines.
2. Write `packages/engine/src/audio/svx8.ts`: IFF 8SVX decoder to `AudioBuffer`. ~80 lines.
3. Load `Sound/krynn` (CoK Amiga) or `sound/krynn.smu` (DoK Amiga) and play via Web Audio API.
4. Load individual 8SVX files for SFX on Amiga platform mode.

---

## 5. Integration with Graphics Mode Switch

The existing switch lives in `apps/web/src/ui/gswitch.ts`. The `setPlatform(id: GraphicSetId)` function calls `manager.setActive(id)` and redraws. An audio layer should hook here.

**Proposed integration pattern (no engine code changes yet — design only):**

```typescript
// gswitch.ts addition (design stub only)
function setPlatform(id: GraphicSetId): void {
  if (!state) return;
  state.manager.setActive(id);
  redraw();
  // AUDIO HOOK (to be implemented):
  audioManager.switchPlatform(id); // stops current, starts platform's music
}
```

`AudioManager` would maintain a map from `GraphicSetId` → `AudioSet`:
- `'DOS-EGA'` → XMI player (ADDQ*.XMI) + VOC SFX (SFXDQ.VOC)
- `'Amiga'` → SMUS player (krynn.smu etc.) + 8SVX SFX
- `'C64'` → (not implemented; SID requires jsidplay2 or similar; defer)

The engine should expose a `src/audio/` subdirectory in `packages/engine/` parallel to `src/graphics/`.

---

## 6. Open Questions / Unknowns

1. **CoK/DoK `MIDI.DAX` format:** The 0xE9-prefix triplet structure is not matched by any known format (DRO, raw OPL2, VGM, HERAD). Whether CoK has AdLib music or only PC-speaker music is unclear — VGMPF says AdLib is supported but the COAB disassembly of a related game shows only PC/Tandy. **The VGMPF claim is primary; assume AdLib + Roland for CoK.**

2. **DoK `GLBR` format:** Documented nowhere publicly. The 8-block MUSIC.DAX structure (driver in block 0, GLBR songs in blocks 1–7) is confirmed from bytes. Format details beyond the magic header are unknown.

3. **Amiga DQK `MXTX` (`DQK.MX`) and `SLBR` (`MUSIC.SLB`):** Completely undocumented. No IFF FORM chunks inside. Require a dedicated disassembly / emulator trace pass.

4. **SMUS instrument resolution:** The Amiga CoK/DoK SMUS files reference instrument `in3`. Whether this resolves to a bundled 8SVX in the same archive, the Amiga hardware's built-in waveforms, or an external library file is unclear. Must test in an Amiga emulator (WinUAE).

5. **`SOUNDS.GLB` (DOS DQK) endianness inconsistency:** UASOUND.TXT states "little-endian" for sample rate and length, but the actual bytes at offset 0x48 read correctly only as big-endian (BE gives 7,418 Hz, LE gives 64,028 Hz). The file is big-endian despite UASOUND.TXT's note. No issue for decoding once this is known.

6. **C64 SID:** No C64 assets in repo. Technically feasible in-browser (jsidplay2 — GPL; `jsSID` — MIT-like). Out of scope until assets supplied.

7. **Loop points:** XMI supports looping via controller events. SMUS doesn't define looping natively. How the Gold Box engine looped Amiga music (restart entire SMUS or player-side loop) is UNKNOWN.

8. **`INSTR.AD` patch count discrepancy:** The header encodes 199 instruments (entries 0–198), but only 128 are usable MIDI patches (per UA_INSTR.TXT). The extra 71 entries are likely percussion/special voices. Need to validate mapping patch 0–127 to GM equivalent for the GM-MIDI fallback path.

---

## 7. Implications for Our Engine

1. **Audio is a third pillar of platform switching.** The existing graphics `GraphicSetId` type (`'DOS-EGA'` / `'Amiga'` / `'C64'`) maps cleanly to audio platforms. An `AudioSet` type and `AudioSetManager` parallel to `GraphicSet/GraphicSetManager` should be designed.

2. **DQK XMI is the easiest first audio.** It is the only game with a documented, convertible format. Start there. The `packages/engine/src/audio/` subtree should be built around the XMI/OPL2 pipeline first.

3. **CoK/DoK DOS audio requires RE.** Neither `MIDI.DAX` (CoK) nor `MUSIC.DAX / GLBR` (DoK) can be decoded without further reverse engineering. Consider DOSBox DRO capture as a practical interim: captures OPL2 register writes in real-time and DRO can be replayed in-browser. This is not "pure engine-native" but unblocks the DOS sound experience.

4. **Amiga SMUS + 8SVX is fully implementable** in pure TypeScript with no external deps and no GPL/LGPL baggage. Prioritize this for the Amiga platform mode alongside the graphics switch.

5. **LGPL compliance for Nuked-OPL3:** If the highest-accuracy OPL2 emulation is needed, Nuked-OPL3 (LGPL-2.1) can be used in the Greybox MIT project but requires distributing the WASM binary separately and providing its source. The MIT `opl3` package avoids this complexity at a small accuracy cost.

6. **Amiga DQK audio is blocked** until `MXTX` and `SLBR` are reverse-engineered. This is lower priority given the DOS DQK art is the chosen best for DQK.

7. **SFX switching:** CoK/DoK DOS SFX (`SOUND.DAX`) is PC-speaker only. Amiga SFX (`Sound/*.8SVX`) are high-quality sampled audio. When switching to Amiga mode, SFX quality improves dramatically — a strong user-perceptible win worth implementing alongside SMUS music. DQK DOS SFX (`SFXDQ.VOC`, 4-bit ADPCM) is easy to decode. Amiga DQK SFX (`GLIB/DIG8`, 8-bit PCM) is even better quality.

---

## Sources

- VGMPF: [Champions of Krynn (DOS)](https://www.vgmpf.com/Wiki/index.php?title=Champions_of_Krynn_%28DOS%29)
- VGMPF: [Death Knights of Krynn (DOS)](https://www.vgmpf.com/Wiki/index.php/Death_Knights_of_Krynn_(DOS))
- VGMPF: [The Dark Queen of Krynn (DOS)](https://vgmpf.com/Wiki/index.php/The_Dark_Queen_of_Krynn_(DOS))
- ModdingWiki: [XMI Format](https://moddingwiki.shikadi.net/wiki/XMI_Format)
- AmigaOS Wiki: [SMUS IFF Simple Musical Score](https://wiki.amigaos.net/wiki/SMUS_IFF_Simple_Musical_Score)
- AmigaOS Wiki: [8SVX IFF 8-Bit Sampled Voice](https://wiki.amigaos.net/wiki/8SVX_IFF_8-Bit_Sampled_Voice)
- Wikipedia: [8SVX](https://en.wikipedia.org/wiki/8SVX)
- WGMPF: [XMI](https://www.vgmpf.com/Wiki/index.php?title=XMI) (via Mindwerks/wildmidi wiki)
- GitHub: [doomjs/opl3](https://github.com/doomjs/opl3) — MIT OPL3 emulator
- GitHub: [Malvineous/opljs](https://github.com/Malvineous/opljs) — MIT DOSBox OPL3 WASM
- GitHub: [nukeykt/Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) — LGPL-2.1 high-accuracy
- GitHub: [spessasus/spessasynth_core](https://github.com/spessasus/spessasynth_core) — Apache-2.0 MIDI/SF2
- Hackdocs: `hackdocs_extracted/TUNES.TXT`, `PCSPKR.TXT`, `UASOUND.TXT`, `UA_INSTR.TXT`, `UAFILES.TXT`
- Repo primary sources: `Champions of Krynn/MIDI.DAX`, `SOUND.DAX`; `Death Knights of Krynn/MUSIC.DAX`, `SOUND.DAX`; `The Dark Queen of Krynn/DISK1/ADDQ*.XMI`, `INSTR.AD`, `SFXDQ.VOC`, `SOUNDS.GLB`, `*.ADV`; `amiga_extracted/ChampionsOfKrynn/disk1/Sound/*`; `amiga_extracted/DeathKnightsOfKrynn/disk1/sound/*`; `amiga_extracted/DarkQueenOfKrynn/disk1/Disk1/*`
