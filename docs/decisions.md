# Architecture Decisions

Decisions are append-only. Supersede an old one with a new entry rather than
editing it in place.

---

## ADR-0001 — Port from the Macintosh 68k release

**Decision:** Base the port on the Macintosh release of FRUA, not the MS-DOS
release.

**Why:** The Mac version is already 68k machine code. The Atari Falcon030 and
TT030 are 68030 machines, so the CPU instruction stream carries over directly.
The porting effort collapses to retargeting the OS layer (Mac Toolbox → Atari
TOS/GEM/XBIOS) and the display/sound paths, instead of an x86 → 68k rewrite.

---

## ADR-0002 — Hybrid C + 68k assembly decompilation

**Decision:** Decompile game logic to recompilable C; keep original 68k
assembly for routines that do not lift cleanly.

**Why:** Pure C is the most maintainable long term but a full clean
decompilation is a large up-front cost. Keeping everything as raw asm boots
fastest but is hard to maintain and diff. The hybrid path gets a running build
sooner while letting the codebase converge toward C subsystem by subsystem.

---

## ADR-0003 — Mac Toolbox compatibility shim first, native later

**Decision:** Implement a Mac Toolbox compatibility layer (`compat/`) so the
decompiled code can call QuickDraw / Resource / Memory / Sound / File Manager
APIs unchanged. Migrate hot paths to native Atari APIs afterward.

**Why:** Keeping original call sites intact lets the decompiled output be
verified against the original behaviour, and gets the game running before any
rewriting. Native rewrites then happen incrementally where performance demands.

---

## ADR-0004 — m68k-atari-mint GCC cross toolchain

**Decision:** Build with the m68k-atari-mint GCC cross toolchain on Linux.

**Why:** Best-in-class debugging, CI integration, dependency tracking, and
mixed C + asm support. Cross-building is faster to iterate than native or
emulator-hosted builds.

---

## ADR-0005 — Hardware abstraction layer for display

**Decision:** Route all rendering through a thin display HAL (`platform/`)
with selectable backends: VIDEL (Falcon030), TT-shifter (TT030), and a later
VDI fallback.

**Why:** The Falcon and TT have different video hardware. A single
engine-facing surface API keeps the decompiled engine machine-agnostic and
isolates the per-machine code to one swappable backend.

---

## ADR-0006 — Editor UI reimplemented inside the Toolbox shim

**Decision:** The Mac Menu, Window, Dialog, Control, and TextEdit managers are
reimplemented within the `compat/` shim, drawing Mac-style widgets into the
HAL surface. The decompiled design-tools code calls them unchanged.

**Why:** Consistent with ADR-0003 (shim-first) — it keeps the decompiled
editor verifiable against the original and needs no call-site rewrites. The
design tools are not a performance hot path, so a software-rendered UI is
acceptable. Mac Dialog Manager semantics (DITL item lists, modal filter procs,
TextEdit) do not map cleanly onto GEM AES, and an AES mapping would tie the
editor to GEM and to an Atari look-and-feel. One implementation also behaves
identically on the Falcon and the TT.

---

## ADR-0007 — Resource fork delivered as a flat archive

**Decision:** A host-side tool (`tools/rsrcpack`) packs every resource from the
Mac resource fork, keyed by `(type, id)`, into a single indexed archive file.
The Resource Manager shim in `compat/` serves `GetResource()` and friends from
that archive at runtime.

**Why:** Atari filesystems have no resource fork. A single archive handles all
resource types (`DLOG`/`DITL`/`MENU`/`PICT`/`snd`/`STR#`/custom game-data
types) uniformly, keeps the Resource Manager API faithful (consistent with
ADR-0003), and sidesteps the TOS 8.3 filename limits a file-per-resource
scheme would hit — a 4-character type plus an ID does not encode cleanly.
Build-time native conversion can still happen later, per resource type, where
it pays off.

---

## ADR-0008 — Runtime before editor

**Decision:** Port the play-UA-modules runtime first; bring up the design
tools as a second phase. Scope is unchanged — the full *Unlimited Adventures*
package remains the goal.

**Why:** The runtime exercises far less of the Mac Toolbox than the
dialog-heavy editor and can reach a playable state before the Menu/Window/
Dialog/Control/TextEdit shim work of ADR-0006 is needed. Front-loading the
runtime de-risks the port: it proves out the decompilation, the Toolbox shim,
and the display/input/audio HAL on the smaller surface first.

---

## ADR-0009 — Scripted objdump-based disassembly

**Decision:** Disassembly is produced by `tools/dis68k.py` driving the
`m68k-atari-mint-objdump` from the project toolchain (ADR-0004). It emits
annotated listings under `data/work/disasm/`, which is git-ignored. The
committed work product is the hand-lifted C in `src/engine/`, not the
disassembly.

**Why:** Chosen over interactive Ghidra. A scripted pass is reproducible and
diffable, needs no toolchain beyond the binutils already required, and
resolves the classic-Mac jump-table and trap model in a way tailored to this
binary. Keeping the raw disassembly out of git — it is the original
copyrighted program in another notation — keeps the repository to the port's
own code and tooling, consistent with the `data/` policy.

---

## ADR-0010 — Engine bring-up PROBE-lift phase complete; remaining work
gated on HAL

**Decision:** Treat the engine bring-up phase (lifting PROBE-only stubs in
the boot path) as structurally complete. Remaining unlifted bodies are
gated on host-facing infrastructure that doesn't exist yet (Falcon display
HAL, input HAL, audio HAL, Palette Manager shim, font cache). Land that
infrastructure before resuming the per-function lifting.

**Why:** A 15-second probe boot in Hatari now generates ~2000 PROBE log
lines across 117 unique labels, finishes without bus or address errors, and
exercises every phase of the dialog event loop. Every event arm of L725c
(Mac event-pump dispatcher) routes through a lifted handler; the dialog
loop (L2d3e) iterates 30 times correctly; the base DLItem handler L1676
fires 222 times with all command arms covered; the text-bounds chain
(JT[1005] → L2856 → jt1135 → jt1200 → jt397 / jt413), the InvalRect
dispatcher (L4d88), the event-pump prelude (jt1134 + L731e + L66e8 +
L6538 + L62fa + jt1118 + L31ea + L3198), and the no-hit feedback chime
(jt1080) are all lifted. Probe counts:

| Function | Calls | Role |
|----------|-------|------|
| L1676    | 222   | DLItem base handler |
| jt382 etc| 60-92 | Shape handler hit-tests (cmd=2) |
| L4d88    | 60    | InvalRect dispatcher |
| L6804    | 60    | Front-window probe |
| jt397/413| 60    | min/max in text-bounds chain |
| jt468    | 34    | Resource-handle lookup |
| jt376/1200/1153 | 31 | Boot-time leaves |
| L725c / jt1134 / L2d3e / L31ea / L3198 / L66e8 / L6538 / L62fa / L731e / jt1118 / jt1005 / L2856 | 30 | Event-pump + dialog loop |
| jt452    | 14    | DLItem stream installer |
| jt444    | 12    | DLItem dispatcher |

What's *not* lifted falls into five buckets, each blocked on prerequisite
infrastructure:

1. **Display HAL** — `L4fae` / `L4e12` are 200+-line text-paint routines
   (SetPort + StringWidth + character-class table at g_a5_-3016 + EraseRect
   / PaintRect / MoveTo / DrawString). `L309c` is a 200+-line scaled
   bitmap blit. `L3e38`'s deep blit arm walks page descriptors at
   g_a5_-2570 and BlockMoves into the page's bits pointer. All wait on a
   Falcon-side pixel destination and font metrics. L3d8c, jt1084, jt1064,
   L448c, L4350, l24aa, L24aa (palette restore), l3e38 page-walk all sit
   downstream of this.

2. **Input HAL** — `jt1132` (mouse poll) stays PROBE-only with zeroed
   coords and "button released" return. Until Hatari mouse / keyboard
   events route through `platform/input.c`, the `L725c` mouseUp / mouseDown
   / keyDown arms never fire and `cmd=3` mouse-track / `cmd=4` action /
   `cmd=5` keyboard-select arms of L1676 stay dormant. `GlobalToLocal` is
   skipped from compat (single-window engine, coords already local).

3. **Audio HAL** — Currently no boot path drives audio. Trap calls in the
   asm (_TickCount aside) don't reach our code; jt1122 turned out to be a
   menu-bar slot setter, not the audio gate we initially guessed. Real
   audio plumbing (Falcon DMA / DSP, TT YM2149 fallback per ADR-0010's
   audio assumption) waits on the runtime phase consuming UA module SFX
   tags.

4. **Palette Manager shim** — `L24aa` is a 700+-line Palette Manager state
   restore via _PMForeColor. Without the Palette Manager in the compat
   shim or a direct VIDEL bridge, the function stays PROBE. Only L71ac /
   L7204 osEvt resume paths reach it.

5. **Resource manager + module loader** — The runtime per ADR-0008 needs
   the engine to read UA modules from disk and walk record graphs. Stubs
   like `jt361` (loads "GAME"), `jt81` (loads module 51), `l0444` (opens
   "start.dat"), `jt449` (rebuilds shape-handler table), `jt938` /
   `jt942` / `jt918` sit on this. The PROBE labels fire once each during
   boot's resource-touching prelude.

A handful of small surprises landed during bring-up that future readers
should know:

- **`jt1134`'s "dead arithmetic"** wasn't dead — d0 holds `(elapsed * 6) /
  5` after rts, which `jt1080` reads as the idle-adjusted tick counter.
  Signature corrected from void to long.
- **DATA-blit pre-loaded values** in g_a5_-1316 (0x05 idle flag), -820 /
  -810 / -809 / -814 (event cluster), -126 / -130 (idle ticks) must be
  zeroed in `boot_a5_seed_defaults`. The Mac runtime drained these via
  pre-main init we don't run. Without the explicit zero, l31ea reports
  "keep polling" every iteration and L2d3e's Phase 5 walks every DLItem
  cmd=5 method (L1676 spikes from 222 to 887 calls).
- **`l747a` signature** was originally `(void*, short, short)`; corrected
  to `(void*, long, long)` after asm analysis showed 12-byte caller
  cleanup.
- **`L7690` ≡ `JT[1122]`** — same function, two naming conventions.
  Consolidated.
- **Misnomers corrected:** L24aa = palette restore (not "menu repaint"),
  L309c = scaled bitmap (not "channel write"), L4fae / L4e12 = text paint
  (not "rect builders"), jt1080 = menu-slot blink feedback (not audio
  "chime"), jt1122 = menu-bar slot setter (not "audio gate").

**Consequences:**
- Future PROBE-lift sessions targeting the boot path should pause until
  Display / Input / Palette / Audio HAL or the resource manager lands.
  Lifting in isolation produces PROBE-stub helpers that don't help — the
  meaningful next steps go through HAL.
- The 117 distinct probe labels in the current trace are a usable
  regression fingerprint: changes that move major counts (L1676 = 222,
  L2d3e = 30, jt1134 = 30, L4d88 = 60, L6804 = 60) should be investigated.
  `tools/run_probe.sh` is the gate.
- When the HAL phases land, the dormant arms (L690e cases 3/5/6 + drag/
  zoom, L6dd0 Cmd-key + key-map, L71ac / L7204 suspend/resume, L7090
  updateEvt + L3e38 repaint, L70e0 activateEvt) come alive without
  further engine-side lifting.

**Display HAL follow-up (2026-05-28):** A short investigation into
exactly what's missing for engine output to reach screen turned up a
finding worth recording.

- **The Display HAL backend is done.** `platform/display_videl.c`
  initializes a 320x400 (8-bit paletted) Falcon mode, allocates the
  planar screen, owns a chunky 8-bit back buffer, and presents via a
  correctness-first c2p. `compat/quickdraw.c` is hooked through to the
  same buffer (`qd_attach_screen`) and palette (`qd_set_palette`). A
  screenshot of the current build (in `data/work/` per ADR-0007 policy,
  not committed) shows the QuickDraw demo — three stacked windows, the
  menu bar, fonts — render correctly.
- **Engine output doesn't reach screen because the play loop is
  gated.** `jt315` (CODE 5 + 0x1b56, the play-loop predicate) returns 0
  in normal builds — see the comment at boot.c around the definition.
  Without the play loop, `jt918 → ... → l0aae → jt453 → L2d3e →
  L2c60` never runs and no engine DLItem ever gets a cmd=1. Probe
  builds force `jt315` to fire once, which is why the boot trace shows
  L2d3e/L2c60/etc. firing 30 times each.
- **Even with the play loop running, the shape handlers don't paint.**
  jt376..jt382 currently handle only cmd=2 (hit-test) and delegate
  everything else to L1676 — whose cmd=1 just sets the dirty bit
  (rec[28] |= 0x80) and returns. The Mac asm confirms this: the actual
  paint is somewhere in the shape-handler arms we haven't lifted yet
  (L4fae / L4e12 text paint sit on the same g_a5_-936 gate that
  L4d88 reads, and that gate stays 0 in our boot path).
- **jt452 is a partial lift.** Our jt452 zeros the DLItem record and
  installs the method pointer but ignores the rest of the Mac's
  stream (positions, callback pointers, etc.). Item rects end up
  (0, 0, 0, 0); even a port-side FrameRect addition would draw
  nothing visible.

The unblocking sequence to make FRUA's own UI render is, in order:

1. **Lift jt452 to decode the full stream** — coordinates land in
   rec[16..23] (top / left / bottom / right shorts) and the action
   callback in rec[4..7].
2. **Lift `jt918` (the play loop body)** — currently a PROBE-only
   stub. Even without paint, this gets the engine into its dialog
   event loop in normal builds.
3. **Lift one shape handler's cmd=1 paint** — pick the simplest
   (jt376 / jt377) and have it draw an EraseRect + FrameRect + label
   via the QuickDraw shim. This is the proof-of-life that engine
   draws end up on screen.
4. **Lift L4fae / L4e12** — the 200+-line text-paint routines that
   render the actual UI labels via DrawString. Their gate
   (g_a5_-936) will need to be driven too — that's the
   "deferred-paint count" the engine increments when an item wants
   its text re-rendered.

These four are deeper engine-side lifts (each its own session), not
HAL work. The Display HAL itself is ready.

**Update (2026-05-28, same day):** Items 1 (jt452 stream lift) and
2 (jt315 enabling the play loop) landed and end-to-end engine
output now reaches screen — verified with a temp DrawString call
inside L2c60 that drew "MODIFY CHARACTER", "VIEW CHARACTER", and
"EXIT FROM PLAY" labels at the expected coords during ua_main.
The temp paint has been removed; the path from `jt315` →
`jt918` → `l0aae` → `jt452` → `L2c60` → shape-handler dispatch
is now wired such that future shape-handler cmd=1 lifts will
produce pixels without further plumbing.

jt452 changes:

  Previously installed only the method pointer. Now also stamps
  rec[12..15] with the label ptr (Mac shape-1's long arg),
  rec[16..17] with phr (shape-1's top), rec[18..19] with page
  (shape-1's left), rec[29] with the shortcut byte (shape-32's
  arg), and rec[28] |= (1<<4) | (1<<5) | 0x80 — the visibility
  / enabled / dirty bits the paint walker checks. Coords are
  still engine-scaled (jt1135 remaps at paint time);
  rec[20..23] (bottom / right) stay 0 (shape-handler cmd=2
  hit-test computes them dynamically).

jt315 change:

  Was returning 0 unconditionally in normal builds, so the play
  loop never ran. Now fires once (matching the probe-mode
  behavior) so a single pass through l07dc → jt918 → l0aae
  drives the dialog event loop and exercises L2d3e / L2c60 /
  jt453. The Mac's real `jt315` is a segment-cycling predicate
  we haven't lifted; one iteration is enough for the engine to
  reach its menu state.

Probe-trace shifts (which become the new fingerprint):

| Label | Before | After |
|-------|--------|-------|
| L1676 | 222 | 372 |
| jt918 | 0 | 1 |
| jt315 | 2 | 2 (same) |
| L2d3e / L2c60 / L725c / jt1134 | 30 each | 30 each (unchanged) |
| jt452 | 14 | 14 (same) |

The +150 on L1676 is dirty items flowing through L2c60 → shape
handler → L1676 cmd=1 each iteration (5 hot items × 30 iters).

Step 3 (shape-handler cmd=1 paint) is the next session — pick the
simplest (jt376 / jt377) and have it call EraseRect + FrameRect +
DrawString via the QuickDraw shim against the item's rec[16..23]
coords (remapped via jt1135).

**Step 3 first-pass attempt (2026-05-28, same day):** Lifted jt382
cmd=1 with the intended QuickDraw shim path (jt1135 → MoveTo →
DrawString). The lift compiled and ran; PROBE counted 32 jt382:cmd=1
calls per boot. But the screenshot showed no menu labels.

Per-call diagnostics revealed the issue: jt452 variadic args
stamped the right values into rec for items 0..11 (verified —
labels at addresses ~0x155CCC, page/phr coords matching the
k_jt918_menu_items table), and the shape-handler table mapped
items 0 and 7 to jt382 correctly. But the jt382:cmd=1 path
only ever fired for item idx=12 — the "extra" item from
`jt452(7, 20L, (long)0)` with invalid label=20.

What we know so far:
- jt452 stamping is correct (per-item dbg_log_num confirms
  label / page / phr land in the right rec slots).
- Method table mapping is correct (item 0 and 7 get matching
  method pointers).
- jt382:cmd=1 fires 32 times in PROBE, but the per-call diag
  saw only idx=12. Items 0 and 7's cmd=1 calls aren't reaching
  the diag — suggesting the dirty bit on those items gets
  cleared somewhere between jt452 stamp and L2c60 walk, OR the
  method pointer gets overwritten, OR the L2c60 walk skips them.

Reverted the jt382 cmd=1 lift to keep the probe trace stable.
Next session: instrument L2c60 to log the method pointer +
rec[28] value for every item-walk hit, to pinpoint why items 0
and 7's cmd=1 doesn't reach jt382's body. Once that's understood,
the shape-handler paint should just plug back in.

Probe fingerprint remains unchanged:

  L1676 = 372, jt918 = 1, L2d3e / L2c60 / L725c / jt1134 = 30,
  L4d88 = 60, jt452 = 14, jt315 = 2, jt382 = 244.

---

**Display HAL step-3 closed (2026-05-28, next day):** The full
FRUA design menu now renders on screen — all 12 items in their
two-column layout (Add / Remove / Modify / Train / Human Change
Class / View on the left; Create / Delete / Load / Save / Begin
Adventuring / Exit from Play on the right). Screenshot:
`data/work/screenshots/2026-05-28_full_design_menu.png`.

L2c60 walker diagnostics from the previous attempt revealed the
root cause: L1676's cmd=16..22 arm (which sets bit `cmd-16` of
rec[28]) faithfully matches the Mac asm in clearing bit 7 (the
dirty flag) whenever the target bit changed value. jt444 fires
cmd=16 (enable) for the items whose c79x flag is set, which for
items 0, 2-5, 7, 9-11 changes bit 0 → clears bit 7. Only items
1, 6, 8, 12, 13 stay dirty by the time L2c60 walks. The Mac
expects `jt158` (the menu-initial-paint function, which we
haven't lifted) to have already painted all items BEFORE jt444
runs — so by the time the dirty bit gets cleared, the paint has
already happened and bit 7 just means "needs re-paint."

Two port-side additions land this milestone:

1. **L2c60 force-paint on first iteration.** A `static int
   first_pass` makes the first walk treat every item as if
   `force_paint != 0`. This stands in for jt158's pre-jt444
   paint until jt158 itself is lifted.

2. **L1676 cmd=1 paint funnel.** All shape handlers (jt376..
   jt382) delegate cmd=1 to L1676; lifting L1676's cmd=1 to call
   `jt1135` (engine→pixel) + `MoveTo` + `DrawString` against
   `rec[12..15]` (label ptr) catches every item in one place
   while staying faithful to the Mac (bit 7 still gets set).
   Items without a valid label or with bit 1 of rec[28]
   ("disabled") set skip silently.

Bridges in place until the more faithful chain (jt158 + per-
shape-handler cmd=1) lands. Probe shifts:

  L1676 = 475 (+103 from cmd=1 paint),
  jt1135 = 309 (was 30 — 14 items × first-iter force + dirty),
  jt376  = 90 (was 31),
  jt382 = 270 (was 244),
  L2d3e / L2c60 / L725c / jt1134 / L4d88 = ~22-44 (slightly
    fewer iters per 15s probe budget since each iter now does
    real DrawString work).

Next step: lift `jt158` faithfully so we can drop the L2c60
force-paint shim; then lift each shape handler's cmd=1 body to
match the Mac (different visual style per shape) so we can drop
the L1676 cmd=1 paint shim. Both are smaller follow-ups now
that the path is proven end-to-end.

**Inverted dirty bit (2026-05-28, same evening):** Looking at the
Mac asm for L2c60 (L2c8e in CODE 3 at offset 0x2c8e) more
carefully:

```
  2c9e: btst #7, d0
  2ca2: bnes L2cb6          ; if bit 7 SET → SKIP dispatch
  2ca4: ...dispatch method(rec, 1)...
```

`bnes` branches when bit 7 is set; the dispatch happens when bit
7 is CLEAR. So the Mac convention is the inverse of what the port
initially assumed: **bit 7 = "already painted, skip"** (set by
cmd=1 itself), not "needs paint." L1676's cmd=16 clear-on-change
makes sense in this light — a state change invalidates the
existing paint, so clear bit 7 → paint dispatches again on next
walk.

With the convention inverted, the port-side hacks unwind:

- jt452 no longer stamps bit 7 on new items (was a port mistake;
  Mac items start with bit 7 clear so they get their first
  paint via L2c60). Bits 4 / 5 still get set (the Mac
  shape-20 / shape-21 stream codes).
- L2c60 walks items where bit 7 is CLEAR, calling cmd=1; the
  per-iteration `first_pass` force-paint shim is removed.
- L1676 cmd=1 still sets bit 7 ("painted") + does the actual
  paint via jt1135 + DrawString — same as before.

The end-to-end menu rendering is preserved (verified — same
screenshot result) and the flow is now Mac-faithful:

  - jt452 creates items, bit 7 clear → "needs paint"
  - First L2c60 walk paints all items, cmd=1 sets bit 7
  - jt444 enables items → cmd=16 changes bit 0 → clears bit 7
  - Next L2c60 walk repaints just those items
  - Steady state: bit 7 stays set, nothing repaints

Probe trace recovers (no more wasted re-paint work each iter):

  L1676 = 236 (was 475 with the shim), L2c60 = 30 (was 23),
  L725c = 30, L4d88 = 60, jt1135 = 43 (was 309), jt376 = 64
  (was 90), jt382 = 190 (was 270), jt452 = 14, jt315 = 2,
  jt918 = 1.

Next: the L1676 cmd=1 paint funnel is still a port-side bridge.
Lifting each shape handler's cmd=1 (jt382 / jt381 / ... / jt376)
to its Mac equivalent gets us per-shape visual rendering (text
vs. button vs. icon vs. ...) and lets us drop the L1676 funnel.

**Input HAL milestone (2026-05-29):** The engine now responds to
real keystrokes. Pressing F1 against the rendered main menu fires
a downstream action (multiple windows materialise — buggy
window-cascade, but the menu *acted on the key*), confirming the
chain is end-to-end live:

  IKBD vector → compat/events.c → WaitNextEvent → L725c case 3
  → L6dd0 (lifted) → g_a5_-818 = 272 (F1) + g_a5_-820 = 1
  → L2d3e Phase 5 idle gate (l31ea → jt1118 returns nonzero)
  → l31f0 → jt1133 returns 272
  → DLItem method(rec, 5, 272) → action dispatch

Made possible by this session's sequence of input-chain lifts:

- jt1132 + L6204: live mouse poll into compat → IKBD coords
- jt1131: semitone period table → jt1122 sound dispatch
- jt1133 + L31f0: keyboard read w/ macro replay + live poll
- jt1153: page-flip back-index (CGrafPort chase deferred —
  attempted full chase bus-errored at $fff1fd28 since the port
  hasn't built the PixMapHandle the slot expects)
- L62fa: mouse hit-test via real GetMouse / FindWindow
- jt391 + jt422: is_letter / toupper for Cmd-key path
- jt1138: cold-init reset of -809 / -810 / -820 / -814 / -2592
- jt1114 + jt1119 + jt1155 + jt1156: master init/shutdown leaves
- L448c: hard-code screen depth = 8bpp (Falcon VIDEL is locked
  at 256-colour, so the GetCWMgrPort + PixMapHandle chase is
  deterministic)

The cascading windows on F1 are the next bug — the action
dispatcher is over-firing window creation, probably because the
PROBE-only stubs in the Window Manager shim aren't enforcing the
"only one top-level dialog" invariant. That's a separate target;
the *input chain* is now proven.

Screenshot: `data/work/screenshots/2026-05-29-f1-pressed-windows-spawned.png`.

---

## Working assumptions (not yet ratified — confirm or amend)

- **Scope:** the full *Unlimited Adventures* package — the design/editor tools
  **and** the runtime that plays UA modules.
- **Lead target:** Falcon030 first, TT030 as a close follow-on.
- **Audio:** Falcon030 has DMA sound + DSP56001; the TT030 has only the YM2149
  PSG (no DMA sound). The audio HAL must degrade gracefully on the TT.
