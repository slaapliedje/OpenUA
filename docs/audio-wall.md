# Audio / music / sound subsystem — findings + worklist

Goal: make the port audible — UI beep, combat/event SFX, then `.slb` music.

**SFX AND MUSIC ARE LIVE (2026-07-11).** Both verified by recording the
emulator's audio (`driver.sh sound`, see the run skill): four effects, then ten
seconds of a three-voice tune whose pitch walks a melody.

* **SFX:** `jt52 → jt965 → l7ee0 → SndDriverWrite → plat_sound_play_mono8`.
* **MUSIC:** `jt52 → l5876 → jt985 → l11a2` arms the voice table; the SEQUENCER
  `jt974` (run every vblank by the Mac VBL task `jt1091`) walks each voice's
  pattern and pushes notes through `jt1131 → jt1122`, which writes the four rate
  fields of a **live FTSoundRec**; the HAL renders that record continuously.

> **FRUA's music is NOT sampled.** It drives the Mac Sound Driver's **four-tone
> synthesizer** — 4 voices, each a Fixed 16.16 step through a 256-byte wave table
> that `jt1111` builds (a square at -522, a trapezoid at -778). The Mac ROM
> rendered that in software, so the PORT HAS TO TOO: the four-tone synth now
> lives in `platform/sound_falcon.c`. `jt1122`'s magic constant `0x233244F7` is
> the giveaway — rate = 0x233244F7/count reproduces the square-wave synth's
> `freq = 783360/count` to within 0.01%, which is what pinned the whole model.

Still muted: nothing. (`SysBeep` now emits a real tone through the swMode path.)

Three defects had to fall before anything was audible — the first was the real
blocker and the other two were only findable once we could listen:

1. **`l0eda` (the sound-subsystem init) was never called.** On the Mac it rides
   in `JT[1079]`'s toolbox bring-up, which the port's `jt12` boot replaces — so
   `jt1111` never ran, the sound-active flag `-806` stayed 0, and *every* sound
   path bailed at its `JT[1154]` gate while the bank loaded happily. **If sound
   ever goes silent again, check `-806` and `-17444` first** (the `FRUA_SNDTEST`
   harness logs both).
2. **Effects cut each other off.** `l7ee0` spins on the driver-busy word BEFORE
   its KillIO — the Mac waits for the previous effect to finish. `plat_sound_playing()`
   was a skeleton returning "never busy", so each new effect killed the one still
   playing. Now reads the DMA state back with `Buffoper(-1) & SB_PLA_ENA`.
3. **Effects played ~10% sharp.** The CODEC clocks only 8 fixed rates and FRUA's
   effects are 22254.5454/n (7417 / 11127 Hz) — *none* of them clockable. The HAL
   was playing the samples at the nearest rate, which transposes them. It now
   RESAMPLES to the nearest clockable rate (linear, 16.16 fixed point — the
   default build is `-msoft-float`), preserving the Mac's pitch and duration.

> KEY FINDING: **FRUA does NOT use the high-level Mac Sound Manager.**
> `_SoundDispatch` (0xa800) appears nowhere in any CODE segment. The `.slb` is a
> *sampled-sound bank* loaded as data, and playback is via the Mac **Device
> Manager** (`_Write`/`_Control` param blocks to a sound *driver*). So "port the
> Sound Manager" is the wrong framing — the faithful target is to reproduce the
> `.slb` mixer pump + the IO-param-block playback leaves, routing their buffers to
> the Atari DMA HAL instead of a Mac driver `_Write`.

## The sound dispatch flow  (HISTORICAL — this is where it USED to die)

Two trigger paths reach `jt52`, the dispatcher (boot.c:18587; cmd 255=stop,
0/1=mute, 2=drain, 3–15=beat/sfx, 32–39=song):

- **A. UI / combat SFX** — e.g. boot.c:25851 `jt52((hit)?5:4)`. cmd 3–15:
  - `-17444==0` (driver not loaded): spins ~2 ticks + pokes `jt1122(1,2,127)` —
    a **menu-slot poke, not a sample**. Silent.
  - `-17444!=0`: `jt965(...)` → boot.c:18568 → **PROBE stub. No sound.**
- **B. Dungeon event "PLAY SOUNDS"** — `l709e` case 17 → `l3ac6(ev)`
  (**LIFTED**); loops ev[4..13] into jt52. `l40b4()` (**LIFTED**) is the post-event
  VIEW REFRESH, NOT a sound hook — it is called before cases 4/11/26/27/34 because
  those redraw, not because they make noise.
- **Song path** — jt52 cmd 32–39 → `l5876(cmd-32)` → `jt985` (18562, **stub**) →
  faithfully `l11a2` (**missing**), the note-stream player over the `.slb` bank.

*(All of the above is now LIVE — jt965, jt985, jt974 are lifted and the driver
write reaches the HAL.)*

**DUNGEON EVENT SOUNDS ARE LIVE too, and needed no new lift.** `l3ac6` (the
type-17 "play sounds" arm) was ALREADY a faithful lift, and **`l40b4` is not a
sound function at all** — it is the post-event VIEW REFRESH (jt221 + jt938 +
jt102). An older revision of this file called it "an event-sound pre-hook"; that
was a guess, and it was wrong. Nothing was stubbed; the chain simply had no
working `jt52` under it until the sfx/music work landed.

Verified end-to-end (`FRUA_EVTSND`): HEIRS holds exactly **three** type-17 events
— `GEO008/009/010`, **l709e index 6** — each queueing ten ids
(`[8,8,8,10,8,8,8,8,10,6]` -> jt52 cmds -> sfx 5/7/3). Firing one through the real
dispatcher produces a single 2.7 s burst containing ten distinct attacks: L7ee0's
busy-spin makes each effect wait for the previous, so they play BACK-TO-BACK with
no gaps. That is the faithful behaviour, not a bug.

**Where the events live:** a GEO level is an IFF file (`FORM`/`AMOD`); the event
table is the **`ENCR` chunk** — 0x7d0 bytes = 100 records x 20, type in byte 0,
and for type 17 the ten sound ids in bytes 4..13. `tools/` has no dumper; the
one-liner is `d.find(b'ENCR')` + the big-endian size that follows.

## What the Mac side does (the faithful behaviour to reproduce)

Grounded in CODE_05.s:
- **`.slb` load (data, not driver):** `jt986`/`MLoad` (CODE5+0x10f0) appends
  `"slb"`, loads via `jt975`, allocs the sample pool `-4770`, installs `jt974`
  as the per-tick mixer pump at A5 `-4774`.
- **Playback via Device Manager:** jt965's output leaf `L7ee0` (CODE_05.s:11604)
  builds an IO param block in A5 `-3138..-3094` and calls `L5716` — a thin
  wrapper around the `_Write` trap (0xa003/0xa403). Siblings: `_Control`,
  `_Status`, `_GetVol`. So the engine drives a **sound device driver** with
  PBWrite/PBControl param blocks.
- jt965 gates on JT[1154] (sound-on), loads the resource via jt468, loops `L7ee0`
  `count*reps` times. jt985 gates on song count `-4758` then calls `L11a2` to
  stream notes.
- **Sample format:** 8-bit PCM; `l7eb8`/`jt964` sign-flip signed↔excess-128 —
  exactly the conversion the Falcon DMA path also needs.

## The Atari target (scoping only — `platform/` concern)

Per the layer rule, hardware lives in `platform/`; engine must not touch XBIOS.
The HAL now carries the Sound Driver's synthesizers (the Mac ROM's job):
- `platform/sound_falcon.c` — `plat_sound_play_mono8()` does the real path:
  `Locksnd`/`Setbuffer(SR_PLAY)`/`Setmode(MONO)`/`Devconnect(...)`/`Buffoper`,
  DMA buffer in ST-RAM via `Mxalloc(count,0)`, and **resamples** to the nearest
  clockable CODEC rate (see the pitch defect above). `plat_sound_playing()` reads
  the DMA state back via `Buffoper(-1)`. API in `platform/include/plat_sound.h`.
- `compat/sound.c` — `SndDriverWrite`/`SndDriverBusy`/`SndDriverStop` are the live
  shim boundary the sfx leaf goes through (ADR-0003: no HAL calls from engine
  code); they decode the FFSynthRec header and convert Mac-unsigned → Falcon-signed.
  The `snd `-resource path (`SndPlay`) remains **dead code** — FRUA's effects are
  GLIB items, not `snd ` resources.
- **TT030 caveat:** Falcon030 = 8-bit stereo DMA + DSP56001; TT030 = YM2149 PSG
  only (no DMA sound). The HAL must degrade gracefully; TT path unimplemented.
- **SysBeep** now emits a real tone: the HAL renders the Mac's swMode square wave,
  so the beep is `plat_sound_tone(710, 128, ticks)` (freq = 783360/count → ~1.1 kHz).
- **The four-tone synth** (music) is rendered by the HAL into a looping ST-RAM DMA
  ring, refilled from the HAL's own vblank. It is deliberately NOT tied to the
  display's VBL, which only exists when the VIDEL backend triple-buffers.

**The two things that were actually missing** (everything else was already
lifted, which is why this looked bigger than it was):

1. **`jt1044` swallowed the driver write.** A negative refnum is a driver, and
   the only one FRUA opens is `.Sound` (-4). The shim now decodes the synth MODE
   WORD every buffer starts with — ftMode(1) = music, swMode(-1) = tone,
   ffMode(0) = sampled effect — and routes it to the HAL.
2. **`jt1036` (`_VInstall`) was a stub, so the sequencer never ran.** FRUA
   installs exactly one VBL task: the sound task (`L741e`, routine `JT[1091]`).
   Without it the song loaded into the voice table and sat there in silence.

**Interrupt-context hazards (both bit, both fixed).** The sequencer runs from the
vblank, so anything it can reach must not TRAP:
* `plat_ticks()` read the 200 Hz counter through **`Supexec`** — a trap — and
  `jt1091 -> jt1149 -> TickCount` calls it every vblank. Bus error within
  seconds. A VBL handler is already supervisor, so it now reads `_hz_200`
  directly (`g_plat_in_super`).
* `dbg_log` is `Cconws`, also a trap. Nothing on a sequencer-reachable path may
  log — a looping song re-arms the synth from interrupt context.
The synth's DMA loop is therefore programmed ONCE, from normal context
(`plat_sound_init`), and the vblank only ever writes memory.

## Status table

| Fn | JT / addr | Status | boot.c | Role |
|----|-----------|--------|--------|------|
| `jt52` | JT[52], CODE6+0x5888 | **LIFTED** | 18587 | Sound/music command dispatcher |
| `jt984`/`jt979`/`jt980`/`l0faa`/`jt983` | CODE5 | **LIFTED** | 18547/18532/18526/18518/17170 | voice stop/count/service/dispatch/sound-on flag |
| `jt986` (`MLoad`) | JT[986], CODE5+0x10f0 | **LIFTED** | 18603 | open `<name>.slb`, load via jt975, install jt974 pump |
| `jt975` | JT[975], CODE5+0x1042 | **LIFTED** | 51568 | `.slb` bank-load callback (header + offsets + samples) |
| `jt964`/`l7eb8`/`l3736` | CODE5 | **LIFTED** | 51645/51633/51618 | sample sign-flip + GLIB count |
| `l59d6` | CODE6+0x59d6 | **LIFTED** | 51669 | audio bring-up: load `music.slb` + `sounds` group 18 |
| `jt1122`/`jt1131`/`jt1145`/`jt1151`/`jt1149` | CODE4 | **LIFTED** (menu-tone, not samples) | 18448/18660/17117/17144/18506 | semitone→period helpers → jt1122 menu-slot |
| `jt1147` | JT[1147], CODE4+0x77f6 | **LIFTED** | 48409 | Alert beep → `SysBeep(6)` — the ONLY Sound Manager call wired |
| `jt965` | JT[965], CODE5+0x7dee | **LIFTED** | 18568 | Play SFX (load resource, loop L7ee0 count×reps) |
| `l7ee0` | CODE5+0x7ee0 | **LIFTED** | 20926 | SFX output leaf — FFSynthRec in place, busy-spin, `SndDriverWrite` |
| `l0eda` | CODE5+0x0eda | **LIFTED** | 67610 | Sound-subsystem init (`jt1111` + voice table). **Was the blocker** |
| `jt985` | JT[985], CODE5+0x12b4 | **LIFTED** | 20895 | Play song n (range-checked) |
| `l11a2` | CODE5+0x11a2 | **LIFTED** | 20860 | Arm the 5-voice table from the song header |
| `jt974` | JT[974], CODE5+0x1304 | **LIFTED** | 84576 | **THE SEQUENCER** — walks each voice's pattern, emits notes |
| `l0ff2` | CODE5+0x0ff2 | **LIFTED** | — | Duration byte -> ticks (26880>>n, dotted, tuplets) |
| `jt1117` | JT[1117], CODE4+0x77ee | **LIFTED** | — | link/unlk/rts — a genuine Mac NO-OP |
| `jt1036` | JT[1036] | **LIFTED** | 17868 | `_VInstall` — installs the sound VBL task. Was a stub: **the sequencer never ran** |
| `jt1044`/`jt1050` | CODE5 | **LIFTED** | 17574/17685 | `_Write`/`_KillIO` to .Sound. The write was **SWALLOWED** — the true dead end |
| `l3ac6` | CODE20+0x3ac6 (case-17) | **LIFTED** | 43368 | Event "PLAY SOUNDS": loop ev[4..13] → jt52 |
| `l40b4` | CODE20+0x40b4 | **LIFTED** | 3757 | Post-event VIEW REFRESH (jt221/jt938/jt102) — **not audio** |
| `SndPlay`/`SndNewChannel`/`SndDoCommand` | shim | **SHIMMED but DEAD** | compat/sound.c | parse `snd ` → plat_sound_play_mono8; never called |
| `SysBeep` | shim | **LIVE** | compat/sound.c | real tone via the swMode square wave (count=710 ≈ 1.1 kHz) |
| `plat_sound_play_mono8` | HAL | **LIFTED** (Falcon DMA) | platform/sound_falcon.c:94 | real DMA path — unreachable |

> Corrections to prior docs: **`jt17` is NOT a sound function** (it's a class/HD
> calc, boot.c:25459). **`jt988` is NOT the `.slb` loader** (it's a char/path
> classifier; band5-wall.md:12 mislabels it — the real loader is `jt986`).

## What works vs muted

- **Works (load/decode/HAL):** bank loader (jt986/jt975/jt964/l3736/l7eb8),
  dispatcher (jt52) + voice bookkeeping, mute toggles, menu-tone helpers, the
  Falcon DMA backend, the `snd ` parser.
- **Stub/missing (every actual playback):** jt965 (sfx), jt985 + l11a2 (song),
  jt974 (mixer), l7ee0 (output leaf), l3ac6 (event sounds), l40b4 (event hook),
  SysBeep (tone).
- **Confirmed muted:** the engine calls `SndPlay`/`SndNewChannel` **zero** times
  (only SysBeep via jt1147, itself a logging stub). compat/sound.c +
  sound_falcon.c are compiled in but **dead, never reached.** Even when
  `-17444=1`, jt965/jt985 are PROBE stubs. **No sample ever reaches the DAC.**

## Blockers + open questions

1. **Bridge model mismatch:** Mac plays via Device Manager `_Write` to a driver;
   the HAL plays via Falcon DMA `Buffoper`. The lift must **re-route, not
   transliterate** — L7ee0/L11a2/jt974 need an adapter to `plat_sound_play_mono8`.
2. **GLIB FAR-pool not wired:** l59d6 (51676) notes jt459 reads 0, so the Mac
   low-mem check is bypassed (-17444 forced to 1). Confirm `music.slb` + `sounds`
   group 18 actually load in Hatari before any audio can work.
3. **Async/mixing:** jt974 is a per-tick mixer over a 5-voice table; the HAL today
   is single-buffer fire-and-forget (`plat_sound_playing()` always 0). Multi-voice
   needs software mixing into one DMA buffer, or the DSP.
4. **Sample rate/format of the `.slb`** — confirm against an actual dump (shim
   assumes 22 kHz / stdSH 8-bit).
5. **TT030 has no DMA sound** — needs a YM2149 fallback or accept silence on TT.

## Plan — multi-part subsystem, small increments

1. **Smallest first step (1 session) — `SysBeep`/UI beep:** give compat/sound.c:226
   a real tone via XBIOS `Dosound` (YM2149) or a tiny canned sample through
   `plat_sound_play_mono8`. Makes jt1147 alert beeps audible and **proves the
   engine→HAL path end-to-end.** Lowest risk, no `.slb` dependency.
2. **SFX (`jt965` + `l7ee0`):** lift jt965's body (gate on sound-on, jt468-load
   the resource, loop the output leaf) + write l7ee0 as an adapter that hands the
   8-bit PCM to `plat_sound_play_mono8` instead of `_Write`. Wire `l3ac6` (event
   PLAY SOUNDS) once jt965 works. Most player-visible win (combat hit/miss).
3. **Music (`jt974` pump + `jt985`/`l11a2`) — largest:** the `.slb` song engine
   with the per-tick 5-voice mixer. The real work; needs the multi-voice mixing
   question resolved (software-mix into one DMA buffer, possibly DSP). Defer until
   SFX is proven.

Validate each step against the load path (l59d6/jt986) actually pulling
`music.slb`/`sounds` group 18 in Hatari first — otherwise there's no sample data
to play regardless of the output leaves.

Related: [[band1-tail-triage]] (jt52 is the level-2 dispatcher lift),
[[toolchain-no-softfloat-020]] (Mxalloc ST-RAM for DMA buffers).
