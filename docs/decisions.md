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

---

## ADR-0011 — Art libraries resolve design-first, root-fallback; hacks never
overwrite the base game

**Decision:** A module's replacement art is **additive**, not destructive. The
engine resolves an art library by trying the **current design's folder first**
(`<design>:8X8DB.CTL`) and **falling back to the install root** when the design
does not ship one. A design's art is therefore in force only while that design is
current, and the base-game files are **never mutated**.

Installing a module becomes: unpack → convert art (`tools/art_convert.py`) →
done. There is no enable/disable step, no backup, no global state.

**Why:**

*The engine already does exactly this for every other design file.* `jt127` (the
design-data loader) composes `<design>:<PREFIX><nnn>.dat`, tries that, and keeps
the bare name as a fallback. Design data is already an override-with-fallback.
**Art is the anomaly:** `cw_wallfile_load` opens a hardcoded, bare
`"8X8DB.CTL"` / `"8X8DC.CTL"` with no design qualifier at all. So this ADR does
not invent a policy — it extends the engine's own existing one to the one file
class that was left out.

*The alternative is permanent destructive global state.* On DOS, a "hack" is
physically installed by overwriting the base game's files — which is precisely
why UA Shell exists and calls itself a **hack manager**: its job is swapping
those files in and out. Inherit that model and we inherit its problems forever:
two modules with different wall art cannot coexist; enabling one silently
degrades every other design; and an interrupted install corrupts the base
installation. That is not theoretical — while testing Pool of Radiance's wall
hack, the base `8X8DB.CTL`/`8X8DC.CTL` were overwritten twice and had to be
restored from a backup taken by hand.

**Is this a faithfulness violation?** It is a deviation, which is why it is being
ratified rather than just done. It is acceptable because it is a **resource-
location policy, not engine behaviour**:

- For an unmodified design the observable behaviour is **identical** — the design
  folder holds no art, the lookup falls through to the root, and the same bytes
  load. Nothing about a faithful playthrough changes.
- The no-shortcuts rule (ADR-0002, and the standing "lift the real Mac asm"
  instruction) governs *engine logic* — we are not gate-flipping a behaviour or
  substituting a stand-in for lifted code. Precedent: **ADR-0007** already
  restructures the data layout (resource fork → a flat `(type, id)` archive)
  without changing what the engine does with it.

**Consequences / work required:**

- Route **every** bare art open through the design-first resolver — not just the
  walls. Audit at minimum `8X8DB`/`8X8DC`, `ALWAYS`, `FRAME`, `BACK`, `BIGPIC`,
  `DUNGCOM`, and the font/`PIC*` libraries.
- **Watch the FC/GLIB pool.** A design's library may be *larger* than the base's
  (Pool of Radiance's converted `8x8db` is 296,922 B vs the base 296,414 B), and
  the pool is already tight enough that two wall files cannot coexist in the
  Mac-sized 450 K budget. Bigger third-party libraries could push it.
- `tools/modtool.py` loses its enable/disable/backup commands entirely.
- **Open, unverified:** whether *per-id* design art (`CPIC1001.CTL` and friends,
  which fan modules ship inside the `.DSN`) already resolves design-first. It
  plausibly does — that is how a Mac module's custom pictures are meant to work —
  but it has not been traced. Confirm before assuming the walls are the only gap.

**Rejected:** porting **UA Shell**. Its core function is launching external DOS
editor executables, which this port does not need — the editors (GEO, event, art
gallery, monster) are lifted and live *inside* the engine. The only part worth
having is its hack management, and this ADR removes the need for it.


## ADR-0012 — Second target: Amiga AGA, via a Bebbo-GCC toolchain and a direct-chipset display backend

**Decision:** Add the **Amiga AGA** (A1200/A4000, 68020+, 8-bitplane 256-colour)
as the second shipping target, alongside the Falcon030/TT030. The port is
selected at build time by `MACHINE` (`falcon` default, `amiga`), and is confined
to three seams the architecture already isolates:

1. **Toolchain** — a new `toolchain/m68k-amigaos.mk` using **Bebbo's
   `m68k-amigaos-gcc`** (github.com/bebbo/amiga-gcc): modern GCC, AmigaOS hunk
   executables, bundled NDK (`exec`/`dos`/`graphics`/`intuition`). Chosen over
   VBCC because the whole codebase is already GCC `-std=gnu99 -m68020` C, so it
   compiles with minimal change; VBCC's dialect friction would add churn for no
   gain. AGA's baseline CPU is the 68020 (A1200), so the flag is `-m68020`
   (soft-float; no FPU assumed, same as the Falcon030 build).

2. **Display HAL backend** — a new `platform/amiga/` backend implementing the
   existing `dsp_backend_t` (`display.h`), using the **direct AGA chipset**: set
   up 8 bitplanes + a copper list, convert the engine's 8bpp *chunky* back buffer
   to bitplanes, and flip on the vertical blank. NOTE: `platform/c2p.S` is the
   *Falcon* converter — its output is word-interleaved 8-plane, which the Amiga's
   per-plane fetch does not accept, so the Amiga needs its own chunky->planar to
   8 separate (or row-interleaved) planes. The bit-transpose core is shared; only
   the final scatter differs. This is the faithful analog of the VIDEL backend
   (ADR-0005), which
   likewise owns the Falcon's video hardware. Rejected: routing through
   `graphics.library` / `intuition` — multitasking-friendly but slower and more
   OS-version-dependent, and a full-screen Gold Box game has no reason to behave
   like a windowed app. The engine still renders into one 8-bit paletted surface
   and knows nothing about bitplanes; only `platform/amiga/` does.

3. **File I/O** — `compat/files.c` is the *only* file outside `platform/` that
   still calls the machine directly (GEMDOS). Its I/O is factored behind the
   `MACHINE` switch: the Atari path keeps GEMDOS; the Amiga path uses AmigaOS
   `dos.library` (`Open`/`Read`/`Write`/`Close`/`Seek`), translated to the same
   Mac `OSErr` the engine already expects. Everything else in `compat/` is
   already portable (`macmemory` is `malloc`, QuickDraw draws into the chunky
   surface, etc.), so no other shim file changes.

**Why AGA and not ST/STe first:** the build emits **2153** 68020-only ops
(`bfextu`/`bfins`/`muls.l`). The Amiga AGA shares the CPU (68020+) and the colour
depth (256), and the bit-transpose core of the chunky→planar step is already
written (the Amiga still needs its own final scatter — see the display backend
note above) — so AGA adds **one** new axis (the AmigaOS shim). ST/STe would add
**three** at once (68000 codegen, a
256→16 colour quantization the engine has never needed, and a 512K–1MB memory
squeeze), each a research project. AGA is the lower-risk second target and proves
the HAL boundary before the harder quantized target.

**Layer rule unchanged (ADR-0005):** only `platform/` (and the one gated block in
`compat/files.c`) may know which machine it is. Engine code compiles identically
for both targets.

**Status / prerequisites:** scaffolded 2026-07-14 with the Falcon build kept
green. The Bebbo toolchain is a hard external prerequisite (built from source on
a networked host; see `docs/toolchain-amiga.md`) and is not yet installed here,
so the Amiga target is **not yet compiled or run** — the scaffold is structural.
The run harness is **amiberry** (installed as the `com.blitterstudio.amiberry`
flatpak), analogous to Hatari for the Atari side.

## ADR-0013 — Per-id art overrides probe the DOS 8.3 spelling as a fallback

**Status:** ratified 2026-07-17.

**Context.** The engine derives a module's per-id art override name from the
library base — `"%s%d%03d.ctl"` (`.tlb` in mono): `bigpic0245.ctl`,
`sprit0052.ctl`, `8x8db1008.ctl`. But the fan-module community names those
files in DOS 8.3 on BOTH platforms: PC modules ship `BIGP0245.TLB`
(necessarily — DOS made the name), and even Mac-authored modules ship
`BIGP0244.CTL` / `SPRI0052.CTL` (Yezukriis). Three of the six per-id
families (walls, big pictures, sprites) derive names longer than 8+3, which
also cannot exist on a real GEMDOS/FAT volume — so on Atari hardware the
Mac-convention spelling is unshippable even when a converter produces it.

**Decision.** `l33ac`'s override probe, after the derived name misses, retries
with the uniform 8.3 clip — `base[:4] + digit + id:03` + the same extension —
which reproduces the community's actual spelling for every family (`8x8d`,
`bigp`, `spri`; the ≤8-char families collapse to the identical name and skip
the retry). Both spellings missing still falls through to the base library,
unchanged (ADR-0011). `-DFRUA_ARTTRACE` logs which spelling hit.

**Why an ADR:** this is a deliberate PORT EXTENSION — the disassembly shows a
single-name probe. It is strictly additive (a file the Mac engine would find is
still found first), it makes both converted PC modules and real Mac fan modules
resolve their art, and it is the only spelling that can exist on the Atari
target's own volumes. `tools/art_convert.py --dos83` emits matching names.

## ADR-0014 — Fan-module art converts IN-ENGINE for colour, INSTALL-TIME for mono

**Status:** ratified 2026-07-18 (user decision + measurement).

**Context.** With every DOS art format solved (ADR-0013 era work), the
converter could live in three places: the offline Python tool (shipped in
v0.3.0-beta), a native installer utility, or inside the engine itself —
detect `HLIB` at art-open and convert on the fly, so PC modules "just work"
when dropped into a `.DSN` folder. The user chose the composed approach
(in-engine conversion + a native ZIP-installer utility), gated on a
performance test of the on-the-fly half.

**Measurement.** `tools/art_convert.py` was ported to a dependency-free C
core (`src/convert/artconv.c`), proven byte-identical to the Python over
the POR corpus (191/191 colour, 191/191 reverse, 189/189 mono), and timed
in Hatari via the 200 Hz tick (`src/convert/bench.c`, POR's full 191-file
art set):

|                              | ST 68000 8 MHz | Falcon030 |
|------------------------------|---------------:|----------:|
| colour conv — median file    |          48 ms |      6 ms |
| colour conv — typical picture|         ~1.5 s |    ~0.3 s |
| colour conv — worst (TITLE)  |         12.2 s |     2.4 s |
| mono synth — median          |         1.35 s |    0.25 s |
| mono synth — wall master     |      41–53 s   |   6.5–8 s |
| whole module up front        |        8.4 min |   1.5 min |

**Decision.**
1. **Colour conversion runs in-engine**: when the art probe finds only an
   `HLIB` file, convert on first touch and WRITE the converted `.ctl` back
   into the `.DSN` folder, then open it normally — the read path is
   untouched and the cost is paid once per file (median 48 ms, worst a
   one-time pause at design pick).
2. **Mono synthesis never runs on the fly.** A 41–53 s freeze at area
   entry on the exact machine the mono build serves is detrimental; the
   ST-mono build detects an `HLIB` `.tlb` and falls back to base mono art
   (instead of reading garbage). Mono `.tlb`s come from the installer
   utility or the offline converter.
3. **The native installer utility** (ZIP → pick destination → extract →
   full convert including mono) remains the recommended install path; the
   up-front cost (1.5–8.4 min for a large module) is fine as an install
   step, and it is the ONLY path that yields mono art on the ST.

The Python tool stays the tested reference; `tests/test_artconv_c.py`
holds the C core to byte-identical output.

### ADR-0012 status update (2026-07-18)

The "not yet compiled or run" caveat is half-cleared: the Bebbo
toolchain IS installed on the dev machine (`~/opt/amiga`, GCC 6.5.0b +
NDK) and `make MACHINE=amiga` now builds a hunk executable cleanly (the
one link error — `g_dsp_mono_active` defined in an Atari-only backend —
is fixed by moving the definition to shared compat). The engine
binary has still never been RUN on an emulated Amiga; the amiberry
flatpak plus real Kickstart images (`~/Amiberry/ROMs/kicka1200.rom`)
are on hand for that first boot. `make installer-amiga` builds the
module installer's Amiga CLI (`uainst_amiga`, bundled into the Amiga
release zips); its asl.library FileRequester frontend and the first
engine boot remain open.

## ADR-0015 — The on-load DOS-art converter is Falcon/ST only; the Amiga uses the installer

**Status:** ratified 2026-07-18.

**Context.** ADR-0014 added in-engine conversion of DOS `HLIB` art on first
touch (jt398 + ua_open_art + the l17e2 design-first base-library probe). On
the Falcon and ST this works and is play-tested. On the Amiga it
**intermittently hangs the art loader**: the walk enters, the frame chrome
draws, then the engine freezes partway through loading the wall/backdrop
libraries — at a *different* group each run (the signature of memory/stack
corruption, not a clean limit). Bisected live under amiberry: with the hooks
compiled out, the same AGA walk loads every library and renders the 3D view;
with them in, it stalls. The hooks add per-load design-folder opens and a
second 210-byte FileSpec to the already-deep l17e2→jt987→jt104→jt460
recursion; raising the CLI stack to 256 KB (which the engine needs anyway,
see the __stack add) moved the stall but did not remove it, so the root cause
is not stack size alone and remains open.

**Decision.** Compile the on-load converter **out on the Amiga**
(`#if !defined(FRUA_AMIGA)` around the jt398 conversion; the l17e2
design-first probe likewise). The Amiga's path to a DOS module is
**`uainst`** (ADR-0014's installer), which converts the whole module up
front — colour *and* mono — so nothing needs converting at load time. Falcon
and ST keep the on-load path unchanged. `FRUA_ARTCONV_OFF` is also honoured
as a build-time escape hatch for bisecting.

**Why an ADR:** it deliberately makes one feature platform-specific, and it
records a known-open engine bug (the Amiga art-load corruption) so the next
session starts from the bisection, not from scratch. Related follow-up: the
Amiga in-game **HUD text** (party roster, command bar) does not render even
with the hooks off — a separate, pre-existing Amiga text/blit issue, tracked
apart from this.

---

## ADR-0016 — Native planar rendering on the bitplane machines (ST/STe/Amiga), diverging from the Mac chunky model

**Status:** ratified 2026-07-19. Work on the `planar-native` branch. Amends
ADR-0005 (the display HAL) for the bitplane backends.

**Context.** The engine is a Macintosh decompilation: every draw goes through
the QuickDraw shim into a single **8-bit chunky (palette-indexed) surface**, and
each present converts that surface to the machine's native format. The Falcon
(VIDEL 8bpp) and TT are chunky-native, so this is free-ish for them and they have
CPU headroom above the Mac's 68020 minimum. The **ST/STe (8 MHz 68000)** and
**Amiga (7 MHz 68000, ECS/OCS)** are **bitplane** machines with **no chunky
mode**, so every present pays a per-frame **chunky→planar (c2p)** tax on the
slowest CPUs we target. Measured on the ECS build: the menu renders correctly in
32 colours, but the in-game feel is slow — the cost is the pure-CPU c2p
(`platform/amiga/c2p_amiga.c`, no blitter) plus a per-force-full median-cut
re-band (`quant_banded`). A blitter-assisted c2p would help, but c2p is the wrong
tax to optimise: **nothing in this engine needs a chunky framebuffer.** It is a
*grid* dungeon — 2D tiles, sprites, perspective-scaled wall slabs at a small set
of **discrete depths**, and 2D UI/text. No per-pixel shading or texture-mapping.

**Decision.** On the **bitplane machines, render natively in planar bitplanes**;
do **not** round-trip through a chunky surface. Concretely:

1. **Pre-convert + pre-scale art to planar once, per wall-set load** (not per
   frame). A grid dungeon shows each wall piece at only a handful of on-screen
   sizes, so each piece is quantised to the target palette (32-colour ECS,
   16-colour ST) and pre-scaled to its discrete depth slots, stored as planar
   bitplane data with a transparency mask.
2. **The dungeon render becomes plane blits** at precomputed slot rectangles —
   **blitter-driven on the Amiga and STe**, CPU planar blit on the plain ST.
   No per-frame c2p in the dungeon.
3. **Static chrome/UI (menu, HUD frame) converts to planar once** and is blitted;
   only the dynamic viewport and text redraw. Palette-cycle animation (torch/fire)
   stays free — it is just colour-register writes in planar.

The **Mac/Falcon/TT keep the chunky path unchanged** (they can afford it and the
VIDEL wants it). This is a deliberate, target-specific **fork of the render path**
for the bitplane HAL backends — the divergence ADR-0005 anticipated but did not
yet require.

**Why an ADR:** it splits the render architecture by target class (chunky vs
planar) — a structural departure from "one chunky surface, HAL converts at
present." It is a large, multi-phase effort (see `docs/planar-plan.md`), done on
a branch so `main` stays shippable while it lands phase by phase. The **Falcon/TT
chunky path is explicitly retained**, not deprecated; those two get faster c2p
later if ever needed, but they are not blocked on this work.

### ADR-0016 status update (2026-07-19) — ST/StE perf (#41) closed at Phase-0

> **⚠ SUPERSEDED by the 2026-07-26 update at the end of this ADR.** This section
> concludes that the native-writer end-state was abandoned. It was not: the third
> wall below was routed around three days later and the draw-time plane path now
> **ships** on both bitplane targets. The three walls were real and the reasoning
> here is worth keeping — it is *why* the eventual solution had the shape it did —
> but do not read this section as current state.

The ST/StE performance goal that motivated this ADR (#41) is **closed with its
wins banked at "Phase-0"**; the full native-writer conversion is **not** pursued
further. What landed on `planar-native` and is verified on the STE:

- **Fixed per-scene palette + banded quantiser** (B1) — one global reduce; also
  fixed the #40 banding.
- **Viewport composited as a self-contained planar region** (B2.1) — the walk step
  is a free plane blit, no re-c2p of the HUD scanlines.
- **Flat-fill c2p fast path** — a flat span converts to constant plane words,
  −36% on menu presents.
- **Stable-slot alignment + smart-skip** — a re-band permutes slots to keep
  persistent colours put, so the static granite chrome/HUD is not re-transposed
  across scene changes.
- **Scene-stable remap / register-only re-band (Phase-0)** — a *within-scene*
  palette change (fade/settle) reloads only the hardware palette registers via a
  captured per-slot representative, skipping the ~2.2s force-full c2p. Unconditional
  ST-build improvement.

**Why the native-writer end-state (drop the chunky surface + c2p, present = flip)
was NOT reached.** Converting the remaining writers to draw plane bits directly
hit three *measured* walls, each rooted in the same fact — the batch c2p works
precisely because `s_chunky` is the single, draw-order-correct composite:
1. **Chrome (a base layer):** its pixels can't be excluded from the c2p while
   overlays (roster/text) share its scanlines — base+overlay z-order.
2. **Text (an overlay):** a separate overlay buffer can't reconstruct per-pixel
   draw order; whole-clear ghosts/blanks, region-clear breaks modals that re-fill
   without redrawing static text. (Mechanism proved on the menu; parked on
   `b-text-overlay-wip`.)
3. **Draw-time model:** draw-time writers need the palette remap *before* drawing,
   but a quality remap needs the drawn-frame histogram — remap-from-CLUT was tried
   and **measured a clear quality regression**. So draw-time can't cleanly drop the
   scene-transition c2p (it's where the remap is bootstrapped).

Only a **fully self-contained rectangle** (the B2.1 viewport) fits the incremental
composite model. The remaining payoff (dropping the transition c2p) is small
against a large, risky big-bang, so the pursuit is closed here. **The ADR-0016
decision stands** — the bitplane fork is real and shipped (viewport + fixed
palette + the c2p optimisations above); it simply lands at a pragmatic Phase-0
rather than the full chunky-surface removal. If ST/StE perf is reopened, the lever
is **c2p throughput** (a tighter `st_c2p_span`/`c2p4st` inner loop, or the Atari
BLiTTER for plane fills on transitions), not writer conversion. Full findings:
`docs/planar-plan.md`. **Amiga/ECS** shares the c2p tax and never received B2.1 —
a parallel track if that target's perf is prioritised later.

**Update (2026-07-19): the B4 double-buffered page-flip was MERGED to `planar-native`
(merge 5583964) and is now the ST/StE default** — for the anti-tear on progressive draws,
on its own merits (the native-writer end-state that would have made it a pure win is not
pursued). Cost: 2 c2p's per re-band (~+60% c2p on scene TRANSITIONS; normal presents 1x, the
dungeon walk untouched) — an accepted trade for tear-free rendering. Reconciled with Phase-0:
a within-scene palette change takes the register-only `st_repalette` (both pages' planes stay
valid, no both-pages c2p); only a genuine re-band pays it. Verified on the STE (menu + dungeon
render correctly, gold roster intact).

### ADR-0016 status update (2026-07-26) — the end-state LANDED and is now the 68000 DEFAULT

**Supersedes the 2026-07-19 update above.** That section closed the native-writer
pursuit at Phase-0 against three measured walls. Wall 3 — the remap bootstrap —
turned out to be soluble, and the end-state it was blocking is now shipping.

**What broke the deadlock.** The wall was circular: draw-time writers need the
palette remap *before* they draw, but a good remap needs the drawn frame's
histogram. Remap-from-CLUT was tried and measured a clear quality regression, so
the conclusion was "draw-time can't bootstrap". The resolution was to stop trying
to make the first frame perfect and instead **detect when it wasn't**: the
**new-ink re-quant trigger** counts converted pixels carrying an index the last
quantise never saw (post-re-band ink riding the nearest-luma fallback — the
invisible-gold-roster bug) and schedules a re-quant when four or more appear in a
present. The chunky surface stays as the bridge for anything a writer does not
own, so draw order is never reconstructed — the thing walls 1 and 2 said was
impossible is simply not attempted. Walls 1 and 2 stand as stated; the end-state
did not require beating them.

**What ships.** The draw-time plane path (`FRUA_PLANAR`):

- **Writers stamp planes as they draw** — `DrawChar`, pattern and solid fills,
  `CopyBits`, the software cursor, and the GLIB chrome blitter via an
  immediate-c2p bridge. Audited at **64000/64000 pixels owned, 0 mismatches**.
- **Presents skip writer-owned rows** and span-bridge the rest, then page-flip.
  The batch full-frame c2p is out of the steady-state present path.
- **Self-healing ownership** (#41): a bridged row *is* `remap[chunky]` by
  construction, so it claims itself. Coverage holes heal after one conversion.
  Measured A/B over identical autoplay windows: the hot message-text band went
  from ~400 conversions to zero, and steady diffed presents went from
  conv 7 / skip 0 to conv 0 / skip 31–193 — the row-skip had never fired in play
  before that fix.
- **Promoted to the release recipes**: `release-ste` at 0.5.0-beta,
  `release-amiga-ecs` at 0.5.1-beta.

**Now the default for `CPU68K=68000`** (this change). Until today the flag lived
only in the release recipes, so a plain `make CPU68K=68000` — every dev build,
every Hatari/amiberry soak, CI — compiled the *chunky* path. The binary under
test was not the binary that shipped, which is precisely the arrangement that
lets a regression reach a zip unseen. `PLANAR=0` restores the chunky path for
A/B work, and `PLANAR` is in the build stamp so switching it purges objects.
Verified: `make CPU68K=68000` with the release flags is **byte-identical** to the
same build with `-DFRUA_PLANAR` spelled out, so the default reproduces the
shipped ST binary exactly. The release recipes keep the explicit `-D` as a pin.

**What is still chunky, and why.** Falcon/TT (VIDEL 8bpp) and Amiga RTG are
chunky-native — the ADR always exempted them. **Every bitplane target now has
the draw-time path**: AGA was the last gap and was ported and promoted the same
day (see the addendum below).

### ADR-0016 addendum (2026-07-26) — AGA ported and promoted; every bitplane target now native

`display_aga.c` had no draw-time code; it is now the third backend with it, and
`release-amiga` ships `-DFRUA_PLANAR`. **AGA turned out to be the simple case,
and the reason is instructive:** ST (16 colours) and ECS (32) must *quantise* a
256-entry chunky index to a slot and rebuild that mapping whenever the scene
palette changes — which is the origin of the re-band, the per-band remap, the
stable-slot alignment, the epoch reset and the new-ink re-quant trigger, i.e.
every part of this design that ever produced a bug. AGA has 8 planes = 256
colours and `set_palette` writes `COLOR[i]` for index `i`, so **the remap is the
identity and never changes**: no re-band, therefore no epoch reset (a stamp
cannot be invalidated by a slot renumbering when slots never renumber); no remap
buffer, so a bridge is a `c2p` straight from the chunky surface; and no new-ink
trigger, which exists solely to catch ink quantised through a stale nearest-luma
fallback. The port is ~90 lines against ECS's several hundred.

Two things deliberately *not* done, both recorded in the code:

- **No row-diff shadow.** AGA has never had one — every present rewrites the
  whole back page — so leaving it that way keeps the two pages trivially
  coherent and the flip semantics untouched. The win banked is dropping the
  *transpose*, which B3.0b measured as ~100% of present cost.
- **`aga_present_rect` stays plain c2p.** Ownership is decided by `a_dt`'s `idx`
  snapshot, so a row the rect path touched either still reads `idx == chunky`
  (a shim writer stamped it; identical values under an identity remap) or reads
  as changed and re-bridges. Making the rect path stamp too would buy nothing.

**RTG is unaffected** even though it shares the zip: a graphics card is chunky,
`display_rtg.c` never registers a draw-time target, so `dsp_planar_draw_target()`
returns 0 and its writers take the unchanged path.

**Soak, and its one honest gap.** Three 110-grab amiberry runs plus a manual
drive: menu, Training Hall, roster list, the dungeon 3D view, the caravan BIGPIC
message chain, the treasure screen, the full-screen XP award panel. 91 of 109
consecutive frame pairs at zero chrome delta in each scan, every outlier a scene
transition or the typewriter, no engine errors. **A walk STEP was never reached**
— HEIRS' entry cell opens a long modal chain (messages, then treasure, then the
award) and movement keys sent into a modal are eaten. That gap does not matter
*on AGA specifically*, because AGA has no viewport-composite path (that is
ST-only, `planar_viewport_register`): the 3D view renders into the shared chunky
surface, so a walk step goes through the same `aga_present` as every screen the
soak did cover. Note also that the skip RATE was not instrumented on AGA — the
performance claim is inherited from the ST's b4skip/b30b measurements, not
measured here.

The draw-time target is **registered by the active backend**, so a 68000 binary
that boots on a Falcon, TT, or AGA machine finds no registration, sees `dt = 0`,
and runs the chunky path unchanged. That is how the shipped ST zip already
behaves on non-ST Atari hardware.

**Remaining, optional:** drop the chunky surface entirely so a present is a pure
flip. The payoff is now marginal — transitions and re-bands are the cost floor,
and the steady state already skips. Open bug in this path: **#61**, an occasional
STe clipping/redraw glitch suspected of being page-flip-related.

---

## ADR-0017 — A player needs the DOS **or** the Mac release, never specifically the Mac: port-authored A5 world + user-supplied string pool

**Status:** ratified 2026-07-20. Supersedes nothing; sets the target end-state
for the Mac-dependency work (#67, #68). Amends ADR-0007's practical
consequences (the flat resource archive stays; what must come *from* it
shrinks).

**Context.** The port's one hard dependency on the Macintosh release is
`frua.rsc` (`docs/redistributable-binary.md`, `GAMEDATA.md:96`). The Mac
release is long out of print; the DOS release is still sold (*Forgotten
Realms: The Archives – Collection Two*). Design data is **already**
byte-identical between releases and art **already** converts
(`tools/art_convert.py`, ADR-0011/0013/0014/0015), so the gap is narrow but
absolute. Three things were measured on 2026-07-20 rather than assumed:

1. **The A5 world is load-bearing.** With `data_pool_replay()` skipped
   (`-DFRUA_NO_REPLAY`, `src/main.c`), the Falcon build still renders the main
   menu and Training Hall with full labels — but those are port-authored C
   literals, so a rendering menu proves nothing. At party depth it breaks
   against a baseline control: no selection highlight, no `* NAME` marker, **no
   command bar at all**, Return inert, dungeon unreachable. Boot also turns
   non-deterministic (baseline 10 s to `menu: modal up`; no-replay ~4 min on one
   run, hung indefinitely on the next). The replay cannot simply be dropped.
2. **The string pool CAN come from DOS.** 2068/2145 Mac `STRS` entries are
   recoverable byte-exact as whole NUL-delimited strings from the GOG
   `CKIT.EXE` (96.4%); the 37 absent are all Mac-platform-specific and **no
   creative game text is missing** (`docs/dos-strings-probe.md`,
   `tools/strs_dos_probe.py`).
3. **A Mac 1.2 retarget does not help.** Mac 1.0 and Mac 1.2 score *identically*
   against DOS 1.2 (96.4%, same 40 substring-only, same 37 absent) because the
   gaps are platform differences, not version differences. "1.2" also is not the
   same version on both platforms: DOS 1.2 (June 1993) *predates* Mac 1.2
   (February 1994) by eight months.

**Decision.**

1. **Target end-state:** a player supplies the **DOS *or* the Mac** data files
   and needs nothing else. The Mac release becomes a *build-time* input used
   once, on the developer side, to derive tables — never a player requirement.
   Mac-copy scarcity therefore stops mattering to anyone downstream.
2. **Split the two halves by mechanism.** The **A5 world** becomes
   port-authored C, compiled into the binary (our own code, redistributable).
   The **string pool** stays *user-supplied*, extracted at install time. #67
   does **not** replace the string pipeline — roughly 1,016 of the A5 globals
   are pointers *into* the pool, and the pool is copyrighted text that cannot be
   compiled in.
3. **Keep the A4-relocation machinery** in `src/engine/data_pool_replay.c`. The
   end-state has the same shape the engine already has — reconstruct globals,
   then point them at a string pool — so only the *source* of the globals
   changes. This is a materially smaller change than "replace the replay".
4. **Extraction is an offset table**, `pool_offset -> (source_offset, length)`,
   carrying **positions only, never text**. Beyond being distributable, this is
   what makes a *native* installer viable: `CKIT.EXE` is 587 KB and unloadable
   on a 1 MB ST, but the extractor only ever seeks and reads (~2,100 times), with
   no EXE parsing and a few KB of buffer.
5. **Offset tables are build-specific — verify, then refuse loudly.** A table is
   derived against one `CKIT.EXE`; a repack or a different floppy/CD build
   shifts every offset and would silently yield garbage. Install-time
   verification of a handful of anchor strings is **designed in, not bolted
   on**, consistent with `GAMEDATA.md`'s "refused loudly rather than mangled".
6. **The ~35 Mac-only strings are port-authored**, as part of #67 rather than a
   separate task. They are diagnostics and Mac platform concepts (`.ctl`
   templates, `:DISK4:` paths, MacPaint/PICT import, the Mac memory-partition
   warning, Toolbox StandardFile prompts, the Mac sound engine's messages) —
   no copyright question.
7. **No Mac 1.2 retarget.** 1.2 stays a per-function **oracle**: when chasing a
   specific bug, diff that one function 1.0 vs 1.2 and port just the fix. A full
   retarget (jump table 1208 -> 1207, reorganised from index 13, every `jtNNN`
   re-derived, 22 of 23 CODE segments changed) would churn a working port for
   benefits nobody has demonstrated.

   **Amended 2026-07-24, both directions.** The 1.2 fork was found unextracted in
   `data/unlimited_adventures.sit` and is now staged
   (`docs/mac-release.md` "The 1.2 oracle"), so this decision's oracle is a
   capability rather than an aspiration. Measuring the delta made the decision
   *more* right for *different* reasons than stated above:

   - "22 of 23 CODE segments changed" is byte-level noise. 1.2 removes one
     jump-table entry, shifting every `jsr %a5@(…)` operand above it. Comparing
     mnemonic streams, **12 of the 22 changed segments are instruction-identical**
     and the whole release is **32 hunks of 1–15 instructions in 10 segments**.
   - `DATA` and `DREL` are **byte-identical** between 1.0 and 1.2, so the retarget
     cost this ADR feared for the A5 world is *zero* — `a4_map.c`, the DOS scalar
     positions map and `a5_scalars.c` would all carry over untouched.
   - What a retarget would actually cost is pure churn: renumbering `jtN` above
     the removed entry and re-keying 858 `lXXXX` helpers whose `(CODE, offset)`
     pairs moved, across 7,628 code references and ~22,277 doc mentions.

   Since the payload is 32 individually-portable hunks and the A5 world does not
   move, cherry-picking is strictly better than retargeting — which is what this
   decision already said. Both user-visible 1.2 messages are located: CODE 21
   `L4816` (the OVERLAND arm of JT[955], already lifted at `boot.c:48155`) and
   CODE 20 `L57a0`. Porting either is a deliberate divergence from 1.0's
   behaviour and wants its own ADR.

**Convergence and testing happen on the Falcon first.** It is the primary
target, the furthest along, the fastest, and it does not hang. Every
A5-reconstruction milestone is proven there before any bitplane machine sees
it; ST/StE and Amiga follow once the Falcon boot is deterministic and the
party/dungeon path is intact. The AGA build *feels* more responsive at the
pointer, but that is a rendering difference, not lower input latency and not
evidence of a faster machine: AGA drives the mouse from **hardware sprites 0+1**
(`platform/amiga/display_aga.c:19,80`) so pointer motion costs nothing, whereas
the Falcon runs a **VBL software cursor** that blits and re-erases from the
chunky surface every frame (`platform/display_videl.c:73-90`) and ECS is
explicitly software too (`platform/amiga/display_ecs.c:127`). It does not argue
against Falcon-first.

**Known-open risk — RESOLVED 2026-07-20, and the diagnosis above was wrong.**
The no-replay boot was *not* non-deterministic and showed no uninitialised-state
wandering. `g_a5_long(-13448)` — one of the 1016 A4/STRS pointer slots — stayed
0, so `l31cc`'s PORT-SAFETY guard skipped its copy, `g_str_22253` stayed empty,
the phase-5 check `ua_get_string(3) ("Boots") != g_str_22253 ("")` mismatched,
and the engine correctly ran the faithful **copy-protection challenge**
(`jt931`) — a modal that waits for input that never arrives headless. The
apparent run-to-run variance was the *test harness*: the run that reached the
menu did so because `beginplay`'s keystrokes fed the prompt; the run that "hung"
received none. Same binary, different stimulus.

Seeding that one slot from the user's own `STRS` (offset 1188, via `ua_strs_at`)
took the no-replay boot from ~4 min / indefinite to **10s, 10s, 11s over three
runs** — matching the replay build's 11s, whose party-screen frame stayed
byte-identical. Commit `a406adc`.

**The lesson worth keeping:** a zeroed A5 slot does not produce garbage
behaviour, it produces a *plausible, correctly-executed* wrong path. The engine
was working exactly as written. Reconstruction failures will therefore tend to
look like features, not crashes — so the grind needs behavioural checks against
a replay-on control, not just "does it boot".

**Still open under #67:** the interactive layer. With the replay off the party
screen still has no selection highlight, no `* NAME` marker and no command bar —
a *separate* root cause from the boot path. The ADR's core claim (the A5 world
is load-bearing) stands; only its account of the boot symptom was mistaken.

**Progress update (2026-07-20) — the A5 world's four sources, and two
corrections a deeper test forced.** The replay-off world is now rebuilt from:
(1) the A4/STRS pointer map + A5-internal relocations (`tools/a4map.py`,
generated position tables), (2) port-authored functional constants
(`src/engine/a5_scalars.c`, committed), (3) shared game-rule tables read from
the user's DOS `CKIT.EXE` with a per-run checksum (`src/engine/dos_scalars.c`),
and (4) the DATA replay itself, still the fallback. On the menu / Training-Hall
/ party path this reached byte-exact parity, and it was tempting to call the
scalar half characterised. Driving one screen deeper (HEIRS caravan → treasure →
3D-view HUD, #72) broke two claims:

- **The 7 above-A5 relocations are NOT harmless.** ADR text and the generated
  `a4_map.c` recorded them as "cannot be modelled, have not mattered". Isolated
  on the deep path they ARE load-bearing — dropping them alone leaves the
  compass rose a bare dome (no ticks, no cardinal letter). The port models only
  below-A5 (`g_a5_below[]`), so reproducing them needs an above-A5 region the
  port does not have. Genuinely open.
- **"2105 bytes of scalars" was a filtered lower bound presented as the whole.**
  `--refs-from` keeps only slots the lifted C *names*; the deep path also needs
  scalars reached *indirectly* (address-of-a-slot-then-walk, memcpy'd structs).
  Full parity on the deep path required the UNFILTERED set — 6445 bytes / 1298
  runs, of which 3786 are DOS-extractable and **2659 (41%, not 410) are the
  hand-authoring residue**. The 410 figure was for the shallow path only.

This is the same lesson as the boot symptom, one level up: a plausible parity
result on a shallow path is not parity. The scalar/relocation accounting must be
re-measured per screen as deeper paths are exercised (combat, chargen, shops,
the editor remain untested), and "unmodellable" gaps must be tested, not
assumed. Tracked in #71 (residue) and #72 (deep-path parity).

**Follow-up (2026-07-20) — the static A5 world is FULLY reconstructable, and
the above-A5 gap is NOT a data dependency.** Two findings retire most of the
alarm from the note above:

- **Zero missing static bytes.** Computed offline: every non-zero byte of the
  31336-byte `g_a5_below` image is covered by exactly one of the relocation
  slots or the scalar runs — 0 uncovered. So relocations + scalars reproduce
  the entire *static* A5 world byte-for-byte; there is no hidden fourth source.
  The 2659-byte residue of #71 is real work but it is a *known, bounded, and
  complete* set, not the tip of something larger.

- **Above-A5 is not load-bearing as data.** A clean single-variable test (with
  `jt442` now correctly wired, commit `db99a50c`) shows seeding the 7 above-A5
  slots does NOT restore the compass tick/letter — the earlier "above-A5 ==
  compass" parity was an artifact of the *dead* `jt442` stub leaving those slots
  holding the replay's out-of-bounds pointers. With `jt442` live it overwrites
  them identically in replay-on and replay-off builds, `g_a5_below` is provably
  identical between the two, yet the compass still differs. So the remaining
  difference is a **runtime / link-layout-sensitive effect** (the prime suspect
  is out-of-bounds access through those above-A5 pointers landing in whatever
  BSS follows `g_a5_below`, which differs by build config), NOT a missing datum
  the reconstruction can supply. Re-scoped to #73 as a memory-model question,
  not a data-reconstruction one.

Net: the DOS-only reconstruction of the *static* A5 world is complete and
proven. What is left is (a) port-authoring the #71 residue, (b) the #73 runtime
compass effect, and (c) exercising the untested deep paths.

**CLOSED 2026-07-24 (#67, commits 3bd348d8 + this one) — the three remaining
items above are done, and the last one paid off exactly as the ADR predicted.**

(a) and (b) closed as #71 and #73. (c) — "combat, chargen, shops, the editor
remain untested" — is now measured, on the Falcon, replay-ON control vs
Mac-free subject driven with an identical key stream (harness recipe in
`docs/a5-residue-accounting.md`). Combat went first (#74/#75). The rest:

| path | result |
|---|---|
| Skull Crag shop — merchant, stock + prices, POOL, exit | byte-identical |
| Temple of Tymora — greeting, services, exit | byte-identical |
| Map editor — module picker, 3D preview, map, compass, Wd/Ht | byte-identical |
| Training Hall, race/class picker, icon picker, save prompt, saved roster | byte-identical |
| Char-gen sheet | **DIVERGED** — Level 1 / age 10 vs level 6 / age 26 |

The divergence was the per-class XP threshold table (A5-30212, 32-bit longs)
and the racial age/HP quads (A5-30780, `short base; char dice; char sides`)
reaching the residue because #68's pass only modelled 16-bit word swapping.
Both are verbatim in the user's CKIT.EXE under the byte orders their element
types imply, so they ship as positions like everything else —
`A4_DOS_SWAP32` and `A4_DOS_SWAP16_Q`. Residue: 1,958 B → 1,121 B.

**The lesson, which is the ADR's own lesson one level down again:** the
reconstruction was "complete and proven" against every screen anyone had
driven, and stayed wrong on the first screen nobody had. Coverage claims here
are only as good as the deepest path actually exercised, and the failure is
always a plausible wrong value rather than a crash — a level-1 fighter looks
like a game rule, not a bug. Before extending the Mac-free claim to a path
(chargen was the last big one; character *training*, spell learning and the
event/NPC editors are still undriven), drive it against a control.

Also worth stating because it inverts a natural instinct: **probe CKIT.EXE
before authoring**. Both of #67's tables read as textbook authoring
candidates — AD&D rules, not Mac platform trivia — and both were sitting in
the player's own binary all along. Authoring them would have worked and been
the wrong call: source that must stay correct forever, in place of a position
table that verifies itself.

**Why an ADR:** it fixes the end-state that a large grind (#67) will be measured
against, and records *why* two tempting shortcuts are closed — dropping the
replay (measured: breaks the interactive layer) and retargeting to Mac 1.2
(measured: buys nothing for DOS, and the version numbers do not correspond
across platforms). Both would otherwise be re-proposed on plausible-sounding
reasoning. It also pins Falcon as the convergence target so cross-platform
symptoms are not chased before the reference platform is correct.

---

## ADR-0018 — Mac 1.2's bug fixes are cherry-picked into the 1.0 lift; the port is 1.0 + 1.2 fixes

**Status:** ratified 2026-07-24. Implements ADR-0017 decision 7 (1.2 as a
per-function oracle) rather than superseding it. Changes the port's *observable
behaviour* away from Mac 1.0 in a bounded, itemised way.

**Context.** The Mac 1.2 fork was found unextracted in
`data/unlimited_adventures.sit` and staged (`docs/mac-release.md`), which made
the delta measurable for the first time. It is small and surgical:

- `DATA` and `DREL` are **byte-identical** — the A5 world does not move at all.
- The jump table loses exactly one entry (1208 -> 1207).
- 12 of 22 changed CODE segments have **identical instruction streams**; the
  real delta is **38 instruction hunks in 10 segments**, 1–20 instructions each
  (`tools/mac12_diff.py --list`).

So the choice is not "retarget or stay" — it is "port 38 small fixes, or
knowingly ship known bugs". A retarget would buy the same 38 hunks at the price
of renumbering every `jtN` above the removed entry and re-keying 858 `lXXXX`
helpers across 7,628 references (`docs/function-audit-2026-07-24.md` §5).

**Decision.**

1. **Port the 1.2 hunks into the 1.0 lift, one at a time**, each carrying a `★
   Mac 1.2 FIX (ADR-0018)` comment naming the oracle hunk and the CODE/label so
   the change is auditable against `tools/mac12_diff.py`.
2. **The port's target stays Mac 1.0.** Addresses, `jtNNN` indices, `lXXXX`
   names, the A5 offsets and both position maps remain 1.0's. 1.2 is read, never
   retargeted to.
3. **This is a deliberate divergence from 1.0 and is stated as such**, not
   presented as faithfulness. `docs/decompilation.md`'s "faithful lift" standard
   still governs *how* each hunk is transcribed; ADR-0018 governs *which
   version* is the reference for behaviour. Where the two conflict, 1.2 wins and
   the comment says so.
4. **Strings 1.2 added are port-authored C literals.** The Mac 1.0 STRS pool the
   port loads has no slot for them (1.2 appends two entries), so there is nothing
   to read them from; they are UI diagnostics of the same class as the 37
   port-authored strings in `tools/rsrc_from_dos.py`, and DOS 1.2 — which the
   player may own — shipped both first.
5. **No build flag.** A behaviour-altering flag would mean nobody gets the fix
   and would violate the release policy (`make release` refuses such flags). The
   fixes are unconditional.

**Why an ADR:** every other behavioural question in this project is settled by
"what does Mac 1.0 do", and a future reader finding the port printing a message
Mac 1.0 never printed would reasonably call it a bug. This is the entry that
says it is not.

**Caveat worth keeping.** The two user-visible hunks are verified by reading both
disassemblies; the fix they implement is *bounds checking* and a *diagnostic*,
so neither is easy to exercise on demand (the overland guard needs a party at a
map edge, the transfer message needs a design being test-played). They are
transcription-verified, not yet observed firing — recorded in the hunk log rather
than claimed as live-tested.

### ADR-0016 addendum (2026-08-18) — B5: the ST viewport is stamped in planes and the composite is a COPY

B2.1 moved the dungeon viewport into a private CHUNKY scratch that the backend
converted to planes at present time. B5 removes the conversion: the engine now
stamps the viewport's bitplanes as it draws — wall tiles out of a planar cache,
the backdrop as cached per-band strips, the perspective-shell regions as planar
spans — and `st_vp_composite_copy()` moves those planes into both pages. The
chunky→planar bridge for the viewport is gone.

Interface (platform owns the layout, as the layer rule requires):

```
dsp_viewport_planes(&pitch)          page-layout plane buffer, absolute coords,
                                     NULL if the backend wants the chunky scratch
dsp_viewport_commit_planes(x,y,w,h)  "these planes hold the frame"
dsp_reband_pending()                 is a re-quantise due at the next present?
dsp_viewport_chunky_valid(v)         was the chunky scratch refreshed this frame?
```

A backend that never registers gets the old path by construction. Falcon/TT are
chunky-native and unaffected; **the Amiga is deliberately NOT on B5 yet** — its
backends do not register the hook, and the engine-side writers are excluded from
`FRUA_AMIGA` so an A1200 does not carry ~56 KB of BSS for caches it cannot use.

Measured on the ST walk (same drive, same profile window, one binary via the
`vpplanar=off` video.cfg key):

| | before | after |
|---|---|---|
| `l309c_tile` | 639.7M | 482.0M |
| `render_3d_faithful` | 313.7M | 159.0M |
| screen vs the pre-change build | — | AE = 0 |

~310M cycles, 10.4% of all cycles in the window, with the picture unchanged.

**THE CHUNKY PASS DID NOT GO AWAY — IT BECAME LAZY, AND THAT IS THE HONEST END
STATE.** `st_reband` copies the chunky viewport over a shadow of `s_chunky`
before quantising, so the 16-colour palette is derived from the walls and sky and
not merely the HUD. Deleting the pass outright costs AE = 45,472 (wall indices
fall to the RGB-nearest fallback and come back in HUD greys). It cannot move to
the planar side either: the planes hold SLOT numbers already collapsed through
the current remap, while the quantiser needs the original CLUT indices WITH
populations in order to CHOOSE that remap — circular. A sum of per-tile index
histograms is not a substitute, because it double-counts every overdrawn pixel
and can move the population-weighted exact-slot reservation.

So the pass runs only on frames where a re-quantise is coming (measured: 6 of 8
renders skipped it). The prediction over-predicts on purpose, and a wrong
prediction is COUNTED (`s_rb_stale`, logged once, shipped compiled in) rather
than silently quantising the previous frame's walls.

---

## ADR-0019 — The on-load DOS-art converter is back ON for the Amiga; installers stop converting

**Status:** ratified 2026-08-23. Supersedes ADR-0015.

**Context.** ADR-0015 (2026-07-18) compiled the on-load `.tlb`→`.ctl`
converter out on the Amiga over an intermittent art-loader hang — bisected to
the conversion hooks, root cause left open, with the ADR itself noting that
raising the stack "moved the stall but did not remove it". That predates the
engine moving onto a big StackSwap'd stack (`plat_run_big_stack`), which
resolved the same corruption signature elsewhere (the wedged ECS adventure
screen). The install-time replacement (`uaconv`, run by the installers with a
"conversion will happen / delete the originals?" exchange) worked on the real
A500 but added a step and two prompts to every install — friction the first
hardware sessions called out.

**Re-test (2026-08-23).** Guards flipped, five cold `.tlb`-only autoplay
runs — three AGA, two ECS — each forcing every touched library through the
on-load converter during boot and the walk (the exact scenario ADR-0015's
hang lived in). All five reached the menu, converted (11 libraries on the
coldest run), completed the script, and wrote back `.ctl` files bit-exact to
`make gamedata-dos`'s output. No stall in any run.

**Decision.** The converter is ON for the Amiga (the `!defined(FRUA_AMIGA)`
arms are gone; `FRUA_ARTCONV_OFF` remains the bisect hatch). The installers
copy files and nothing else: `instdisk` no longer runs `uaconv` or announces
a conversion, and the Workbench Install script drops its convert step and
delete-question. `uaconv` still ships on the engine disks as an OPTIONAL
tool — converting everything up front and reclaiming the `.tlb` space
(~5 MB) is still worth offering, just never required or prompted.

**Consequence.** A fresh install plays immediately; each art library converts
silently the first time the engine opens it (48 ms–2.4 s class, once ever —
the `.ctl` is written back beside the `.tlb`). If the ADR-0015 hang ever
resurfaces on real hardware, `FRUA_ARTCONV_OFF` plus `uaconv` restores the
ADR-0015 world without further code.

## ADR-0020 — Offline art pre-quantization to platform-native palettes

**Status:** ratified 2026-08-26 (user direction, repeated across several
sessions and confirmed after the first real-A500 test of v0.9.18-beta).

**Context.** The bitplane machines carry a 256-colour art corpus that the
engine reduces at runtime: a banded quantizer, re-band heuristics, palette
caches, raster splits — all CPU spent per boot and per screen change on a
7–16 MHz 68000, and all of it approximating offline work. The user's
verdict from hardware: the 16 MHz Mega STe is "pretty decent", but the
Amiga ECS — even bumped to 14 MHz — "is just that much slower". Precedent
is established on both sides: the community shipped EGA converters for the
DOS game's art, and SSI's own Curse of the Azure Bonds ST port used TWO
hand-authored 16-colour palettes with authoring-time dithering — no
runtime quantization at all (see the CAB-ST analysis). User: "if we make a
converter to work based on the greatest strengths of each platform, I
think we'll definitely have happier users", and separately: fewer-but-
stable colours beat more-but-noisy.

**Decision.** Build an OFFLINE converter — a separate workstream from the
engine — that rewrites an installed art set (base game or fan module,
`.tlb`/`.ctl` libraries) into palette-reduced art native to each target:
16 colours for ST/STE, 32 for Amiga ECS, with selective error-diffusion /
ordered dithering of gradient art while exact-match chrome stays crisp
(the quantsim.py strategy set). The converted set remains VALID art in the
existing formats, so the current engine plays it unchanged — the runtime
quantizer simply finds art already inside its budget and settles into a
stable palette with nothing to re-band. Engine-side fast paths (skipping
the quantizer entirely when the art declares itself pre-quantized) are a
later, optional optimization — not part of this decision.

**Consequence.** The heavy colour work runs once, on the PC (Python 3,
beside art_convert.py) or optionally on-machine via the uaconv route; the
weak machines render stable palettes with no quantizer churn. An
"EGA-nostalgia" preset (the community converters' look) becomes possible
for free. A module's converted art lives in its `.DSN` per ADR-0011 and
never overwrites the base game; the base game's converted set installs
beside the originals the same way `.ctl` conversion already does. ABC
(Leonard's GPU quantizer) serves as the quality oracle.

### ADR-0020 addendum (2026-08-26, measured) — the target is PER-BAND, not per-screen

The first prequant run against the real backends corrected the colour
target. Both bitplane backends paint through RASTER-SPLIT palettes, not
one palette per frame: Amiga ECS runs **25 copper bands x 32 colours**
(`ECS_NBANDS`, 8 scanlines each) and the ST runs its Timer-B viewport
split (24 colours a frame, `vpbands`). Quantizing the art offline to a
single 32-colour palette therefore THROWS CAPABILITY AWAY — verified by
eye on the ECS titles: the red gradient banded into hard diagonal
stripes and the stone border shifted green/purple, visibly worse than
what the runtime quantizer produces from the same art at the same 32
colours per band.

So v2's target is not "N colours per screen". It is: per-screen-family
palettes sized to the hardware's PER-BAND budget, dithered offline —
which keeps the raster capability and still removes the runtime
re-band churn (the actual win the user asked for). The v1 palette-snap
stays useful as the plumbing (it proved the rewrite is safe and the
engine plays converted art unchanged) and as an EGA-nostalgia preset,
where collapsing to one small palette is the POINT rather than a loss.

Corollary worth keeping: measure a quantization change against the
BACKEND, never against the source art alone. A metric that says "rms
22.3, better than 16 colours" ranked the worse-looking result first,
because it could not see the copper.

### ADR-0020 v2 (2026-08-26) — the per-band converter, and why it works

`tools/perband.py` implements the corrected target from the addendum above.
The constraint it enforces is the hardware's own:

    no 8-scanline strip of a picture may need more than `budget`
    distinct colours

A picture satisfying that can carry FAR more than 32 colours overall —
different strips spend their 32 on different colours, which is how Amiga
"rainbow" art and SSI's hand-authored ST art work — and every band then
quantizes EXACTLY at runtime: nothing is merged, so there are no per-band
seams and no nearest-luma stray pixels. The runtime quantizer is left with
nothing to do, which is the actual goal (the CPU win) as well as the
fidelity one.

**It needs no pixel rewriting.** A strip's cost is the number of distinct
RGB VALUES its indices resolve to, not the number of indices, so the budget
is met by MERGING PALETTE ENTRIES alone — the same palette-only rewrite v1
proved safe. Each merge takes the closest pair inside an over-budget strip,
so fidelity is spent only where the constraint binds. Pictures land at
screen y=24, a multiple of the 8-row band height, so a picture's own strips
align with the hardware's bands.

**Budget is deliberately below the hardware's 32** (default 24): a screen
band also carries frame chrome, the roster and the text box.

Measured, whole HEIRS corpus at budget 24: 364 pictures, none skipped,
worst band 197 -> 24 colours, 2323 over-budget bands -> 0, palette rms
error **7.6** — against 26.3 for a global 32-colour palette and 34.6 for
global 16. Better fidelity AND a hardware guarantee, which is the whole
argument for doing this offline.

Verified on ECS in amiberry, artefact rows in the HEIRS tavern picture:
stock art before #167 = 9 rows / severity 3.39; stock art after #167 = 3 /
0.85; **per-band art = 0 / 0.00**, with the picture's modelling intact.
So #167 (the copper timing fix) and this converter close the ECS
band-artefact family from both ends: the engine stops mis-timing the
palette, and the art stops needing a runtime cut at all.

Still open for v3: true pixel-level dithering (needs codec re-encoding, not
just palette merges) and the EGA-nostalgia preset.

## ADR-0021 — Amiga audio goes through AHI when it is there, and falls back to Paula when it is not

**Status:** ratified 2026-09-05. Extends ADR-0012 (the Amiga target).
**AHI CONFIRMED ON REAL HARDWARE the same day** — A1200 + Apollo V4 IceDrake,
`0.9.22dev-ahi`, DBG.LOG reads `snd: AHI backend up (ahi.device)`, plays, and
tears down cleanly through ExitToShell. That is the first time the AHI path had
executed anywhere. See Consequence for what is still unverified.

**Context.** The Amiga backend drove Paula directly, mixing all four Mac
four-tone voices, the swMode tone and the current effect into one ring and
looping it on **AUD0**. Paula hard-pans its channels — 0 and 3 left, 1 and 2
right — so the entire engine mix left the machine in the LEFT speaker only.
That went unnoticed because everything upstream is mono and emulator playback
sums to both ears.

It surfaced when the port was captured off real hardware (an A1200 with an
Apollo V4 IceDrake, through a StarTech USB3HDCAP). Measured on the FRUA intro:
**L−R = +18.1 dB**, reproduced to a tenth of a dB by two independent capture
paths (ffmpeg via PipeWire, and OBS), while the *State of the Art* demo through
the same rig measured +5.1 dB. Identical numbers from different capture software
put the imbalance in the port, not the capture.

A second defect turned up in the same code. In the general mix path four voices
already fill the sample — they sum to ±512 and `>>2` lands on exactly ±128 —
and a full-scale effect (±128) or beep (`amp & 0xff`, so ±255 before its shift)
was then added ON TOP and hard-clamped. Music alone (the fast path) could not
clip by construction; music *plus* an effect always did.

**Decision.**

1. **Paula plays the ring on AUD0 *and* AUD1** — mono centred, which is the
   faithful rendering of a mono source. Both are started by ONE `dmacon` write
   with identical LC/LEN/PER so they stay sample-aligned; enabling them
   separately would leave up to a full ring (160 ms) of offset and sound like an
   echo rather than a centre image. `plat_bard_stop` restarts both together and
   re-seats the play model, because the bard borrows AUD1.

2. **`plat_sound_init` prefers `ahi.device`, and falls back to Paula** when it
   is absent or will not open. AHI is the standard Amiga audio API, mixes and
   pans properly, and on an Apollo/Vampire its driver reaches the SAGA audio
   core ("Arne"). One backend serves both stock and accelerated machines; no
   Vampire special case.

3. **The general mix path ducks instead of clamping.** The voices give up 6 dB
   while something rides on top and the rider is scaled into the budget that
   frees, so the two combinations that actually occur land exactly on full
   scale: voices ±64 + effect ±64, and voices ±64 + beep ±63. Music-only
   playback never enters this path and is unchanged. This is a **deliberate
   divergence** from the Mac original, which clamped without ducking; the three
   shift values are commented so it is a one-line revert.

**Why not drive Arne directly.** Arne is documented by feature — 16 channels,
8/16-bit, rates to 56 kHz, per-channel panning, 24-bit internal mixing, samples
from any RAM — but **no register map is published**. The Apollo wiki's SAGA
register reference lists SD-card, video and SPI-flash registers and has no audio
entries at all, deferring audio to the original OCS/ECS/AGA registers, and the
other official source (apollo-core.com/sagadoc) serves no HTTPS. Writing to
guessed addresses on real hardware is not acceptable, so AHI is the route until
the Apollo team publishes a register map — at which point a third backend can
sit beside these two behind the same HAL.

**Implementation shape.** Both backends live in `platform/amiga/sound_paula.c`
and share `synth_render`; only the output stage differs. AHI uses the DEVICE api
(`OpenDevice("ahi.device")` + `CMD_WRITE` on an `AHIRequest`, double-buffered
through `ahir_Link`) rather than the low-level api, so it needs no library base,
no inlines and no link library — only `devices/ahi.h`, vendored under
`third_party/ahi/` because the Bebbo toolchain ships no AHI headers. The device
api blocks in `WaitIO`, so a small process ("OpenUA AHI") owns rendering and
`plat_sound_vbl` skips the ring refill while still running the engine sequencer
hook. Under AHI, Paula is untouched: no DMA, no ring, no LED filter change, and
the campfire bard (a direct-to-Paula tune) does not play.

**Consequence.** On a stock machine without AHI nothing changes except that the
sound is now centred and no longer clips under effects. With AHI installed the
output goes through a real mixer and, on Apollo hardware, through Arne.

Open, and deliberately so:

* **The AHI path has never been run.** It is written, compiles clean under
  `-Wall -Wextra` and links, and follows the AHI developer archive's own
  double-buffering example — but no hardware or emulator has executed it. First
  run should confirm: the fallback is silent when AHI is missing, playback does
  not stutter at 1024-sample buffers, and the loading screen is merely quiet
  (no bard) rather than broken.
* **Still 8-bit mono into AHI.** `synth_render` produces `signed char`, handed
  over as `AHIST_M8S`. This buys centring and AHI's mixing, not more bits.
  16-bit (`AHIST_M16S`) is where Arne's quality would actually show; the mixer
  already accumulates in `long` before the clamp, so it is an extension rather
  than a rewrite.
* The ducking in (3) can pump audibly if effects are dense. If that is heard on
  hardware, the alternative is a constant 6 dB cut everywhere (no pumping, a
  quieter game), and the choice should be made by listening, not by argument.

## ADR-0022 — Where a Macintosh reservation collides with DOS art, the DOS convention wins

**Status:** ratified 2026-09-20, recording three changes shipped in v0.9.26 to
v0.9.28. Extends ADR-0001 (port from the Mac release), ADR-0017 (a player needs
the DOS **or** the Mac release) and ADR-0019 (every target ships DOS art).

**Context.** The engine is a lift of the Macintosh binary, and the Mac release
carries conventions that were safe only because SSI authored its Mac art around
them. Every picture the port ships is now DOS-sourced (ADR-0019), and fan
modules are DOS art made in paint programs by people who never saw those
conventions. SSI's own DOS art happens to respect them, which is why the base
game never showed a fault; a module's art does not. Three cases surfaced on a
real A1200 running *The Curse of the Fire Dragon*, each settled against DOS
FRUA 1.2 in DOSBox with the same module:

1. **CLUT 0..15.** `l6e58`, the engine's SetEntries, clamped `start` up to 16:
   on the Mac those are the system colours. DOS loads all 256. A module's title
   marble painted with slots 11 and 15 showed the UI band's cyan and white (#23).
2. **Index 255 in an opaque piece.** `l2d4e`'s mode-2 arm (DOS drawing method
   18, compressed OPAQUE) skipped 255 as transparent, as the Mac's `L2bfc` does.
   No Mac mode-2 piece holds a 255 pixel (0 of 185) and no SSI method-18 piece
   does (0 of 184); the module keeps its brightest yellow there (#24).
3. **Colour-cycle ranges.** Not a convention but the same family — the port's
   own shortcut. On the Mac every palette install goes through the GLIB
   allocator, whose Phase 3a frees the cycle ranges a request overlaps. The
   port installs its wall, backdrop and UI bands directly, so a picture's cycle
   range outlived the picture and animated the walk's floor (#21).

**Decision.** When a Mac-side reservation or key exists only because Mac art
never exercised it, and DOS FRUA demonstrably behaves otherwise on the same
data, the port follows DOS: all 256 CLUT entries load; 255 is a colour in the
opaque drawing methods and stays the key in the transparent ones (modes 1/5/7);
and the port's direct palette installs do the allocator's Phase 3a bookkeeping.
The lifted routines keep their Mac structure and asm offsets; each divergence
is a marked block with the evidence in its comment.

**The test for a future case** is the one that settled these: run the same
module in DOS FRUA, align the frames, and compare. If the emulator shows the
same loss as the real machine, the fault is the port's; if only the machine
shows it, suspect the machine (`-DFRUA_PALTEST` exists for that question).

**Consequences.** A picture owns slots 0..15 while it is up; the menus
(`load_menu_ui`) and the walk (`port_hud_text_clut`) re-install their UI band
on entry, as they always did, so text over a full-palette fan picture takes
that picture's colours — exactly as in DOS. Mac-sourced art renders
identically (it never used the reserved values). The wall art's own 255 key
(`cw_shade`) is untouched: walls are a transparent method by construction.

## ADR-0023 — The other Gold Box games run through a second module driver, not through FRUA's event table

**Status:** Accepted (2026-11-16). First step landed: `tools/gbgeo.py`.

**Context.** FRUA never played the three Krynn games, or any other Gold Box
title, and the reason is structural. A FRUA design drives an area from a fixed
event table in the area file (100 slots of 20 bytes, ~35 event types). Every
other Gold Box game drives an area from **ECL**, a compiled bytecode script per
area, and keeps its map in a four-plane cell layout FRUA's files do not use.
Converting one of those games *into* a FRUA design is therefore lossy by
construction, and for The Dark Queen of Krynn it is impossible: 13 of its 20
retail maps exceed the 576 cells a FRUA area file can hold (up to 28x28 = 784).
The format research already exists — `docs/engine/research/` here, and the
Greybox project (`kblood/Greybox_CombinedGoldbox`, 0BSD), whose
`krynn-frua-map-capacity-port-comparison-2026-11-16.md` is the map-capacity
evidence cited above.

**What was checked in this engine before deciding.**

- The live map is the TAIL of one heap block: `jt211` allocates 3746 bytes
  (`g_a5_-12300`) = 290 header + 576 x 6 cell bytes, and `l7226` refuses a file
  whose `ds[2] * ds[3]` exceeds 576. About 69 sites read cells as
  `ds[290 + cell * 6 + field]`. Nothing lives after the map in that block, and
  both dimensions are single bytes, so widening the allocation leaves every
  accessor's arithmetic unchanged. Per-cell side tables (visited/automap state,
  the editor's clamp at boot.c:82810, the save writer's fixed `MAP <0xd80>`)
  have NOT been audited yet; that audit is the gate on raising the limit.
- FRUA's edge byte (`passability << 4 | texture`) is the same scheme the older
  games use, with the door state folded into the high nibble instead of a
  separate plane. A four-plane cell translates into a FRUA cell edge by edge.
- Proved rather than argued: `tools/gbgeo.py` turned Curse of the Azure Bonds'
  16 maps (the Atari ST release's `GEO.GLB`) into a FRUA design and the
  UNMODIFIED Falcon build walked the first one — east from (0,0) stops at (4,0),
  exactly where the source map has its wall.

**Decision.** One engine, two module drivers.

1. **Maps are translated at load into the engine's existing in-memory cell
   model** (the 6-byte FRUA cell). The renderer, the walk, the AREA map and the
   combat entry keep working on one representation. The translation is
   `tools/gbgeo.py` today and becomes a loader in the engine.
2. **Scripts are NOT translated.** An ECL interpreter is added beside the FRUA
   event dispatcher (`l709e`), entered from the same per-step hook. A module is
   either event-driven (FRUA) or script-driven (everything else).
3. **Rules are a per-game table**, starting from FRUA's Forgotten Realms AD&D
   and adding Dragonlance for the Krynn games.
4. **Order:** The Dark Queen of Krynn first (same engine generation as FRUA,
   same container family), then Champions and Death Knights of Krynn through the
   same map translation and interpreter, then the Forgotten Realms line.
5. **Game data stays user-supplied**, as with FRUA. Nothing from any of these
   games is committed.

**Not decided here:** how the engine's map storage is widened past 576 cells
(blocked on the side-table audit), the Dragonlance rules design, and which
machines get which games. Nothing in this ADR changes how a FRUA design loads,
plays or saves.

**Known losses in the first step, stated so they are not mistaken for done:**
locked and wizard-locked doors (the 2-bit door flag's values 2 and 3 are not
pinned down), the backdrop/zone plane, each game's own wall art (the converted
maps wear FRUA's stock walls), and everything a map's script does.

**A correction this turned up.** `tools/dsn.py` says a cell's four edge bytes
are N, E, S, W; `docs/geo-format.md` says W, S, E, N. Walking an asymmetric map
says **N, E, S, W** (the engine reads byte `facing >> 1`), with `tools/geo.py`'s
`row` as the east-west axis. A square test room cannot tell the orders apart,
which is how both readings survived. `tests/test_gbgeo.py` pins the measured one.

**Update, same day — the map storage is widened (`GEO_MAX_CELLS` = 784).**
The side-table audit came back clean for play: the cells are the tail of the
design-state block, and the only sites that assume 576 are in the area EDITOR
(the flood-fill work block, the "Map Area too large" check, the Save-3D-Map
writer). So the block is now allocated at 290 + 784 x 6 bytes, the editor stays
a 576-cell tool, and its writer refuses a larger map instead of truncating it.
Larger maps arrive as a **wide area**: the ordinary area container with a MAP
chunk of exactly `width * height * 6` bytes when that exceeds 576 cells
(`tools/geo.py`, `docs/geo-format.md`). An area of 576 cells or fewer is still
SSI's file byte for byte and is still held to the Mac's size checks.
Checked on The Dark Queen of Krynn's own data (the Steam DOS release): all 20
maps decode (10x10 up to 28x28, longest side 39), and the Falcon build walks
map 10 from (18,27) — cell 774 — west six steps to the wall the source
predicts. HEIRS loads and plays as before.

## ADR-0024 — Modules are installed from the desktop first

**Context.** Players want to fetch and install FRUA modules without unpacking
archives by hand. Many of the target machines will never have a network
stack, and the ones that do need STinG / MiNTnet (Atari) or a bsdsocket stack
(Amiga) set up first.

**Decision.** The first module manager runs on the player's desktop computer
and writes into an OpenUA folder: a mounted SD or CF card, a hard-disk image
mounted on the desktop, or an emulator's folder (`tools/modman`, Python 3 and
tkinter only, so Windows, macOS and Linux need nothing installed beyond
Python). An on-machine browser may follow; it would read the same catalog.

- **The catalog is plain text** (`[id]` sections of `key = value`), so a
  68000 could read it too. It points at where each module is already
  published; OpenUA does not re-host fan modules unless an author agrees.
- **An install never touches anything outside the module's own `.DSN`
  folder.** A module's replacement art stays inside it, where the engine
  looks first (ADR-0011).
- **Names go on the card as upper-case 8.3**, the strictest of our file
  systems (GEMDOS); Mac long art names take the 8.3 spelling the engine also
  probes (ADR-0013).
- **Reinstalling keeps the player's saved games**, over any the module
  ships. A copy that fails part-way leaves nothing behind.
- Old PKZIP 1.x "imploded" archives are read natively (Python's zipfile
  cannot); StuffIt and LHA archives must be unpacked first and added as a
  folder.

**Not decided here:** where the catalog lives and which modules it lists.

## ADR-0025 — The module manager runs on the ST and the Amiga, in C (supersedes ADR-0024)

**Context.** ADR-0024 put the first module manager on a PC desktop
(`tools/modman`, Python). That misread the request: the manager is for the
machines themselves — download and install modules on the ST and the Amiga.

**Decision.**
- **One C core shared by every front end:** `installer/modcore.c` decides what
  an archive holds (the three layouts fan archives use, what the engine reads,
  8.3 names, saved web pages and StuffIt/LHA files named as such) and reads
  members, PKZIP 1.x implode included. `uainst` uses it now; the downloader
  will too.
- **`tools/modman` stays as the reference** the C rules are held to
  (`tests/test_installer.py` compares their plans on synthetic archives and on
  every fan archive under `data/`), the way `ecl.c` is held to `ecl.py`.
- **An install is staged**: unpacked into `<DESIGN>.NEW`, converted there, then
  moved over `<DESIGN>.DSN` with the player's saved games carried across. On a
  file system that will not rename a folder (old GEMDOS) the files are moved
  one by one. An art file that will not convert is reported and does not stop
  the install (the engine converts again on first show).
- **Downloading comes next and is per machine:** HTTP/1.0 over STinG or
  MiNTnet on the Atari and `bsdsocket.library` on the Amiga. The fan archive
  is plain HTTP, so no TLS is needed on a 68000.

**Behaviour change in uainst:** it no longer flattens everything in an archive
into the design folder. DOS programs, temporary files, files in subfolders
other than SAVE, and files outside the design folder are left out and named;
names go in upper case; an archive with no GAME001.DAT is refused.

## ADR-0026 — The engine reads the Gold Box games' own files; classes and races come from a ruleset

**Status:** Accepted (2026-11-19). Extends ADR-0023: what that ADR does with
offline converters becomes the engine's job.

**Step (a) landed** (882dd08c, 4df2056d): `src/engine/rules.h` defines the
ruleset as pointers to the tables, `g_rules_frua` points at the A5 tables
themselves, and every reader of the ~20 class and race tables goes through
`g_rules`. Parity, with `-DFRUA_RNGSEED=12345` on the Falcon build against the
commit before: the HEIRS entry chain, and a halfling thief's creation
sheet plus two rerolls, are pixel-identical (AE 0; the baseline repeats at
AE 0). The positive control is a build whose THAC0 pointer is shifted one
class row: the sheet then reads THAC0 57 for 18 and the comparison catches it.
(Correction: this was first written up as "a seeded HEIRS combat map"; the
frame compared is the entry chain's treasure screen, and no fight is
reached there. The creation sheet is what caught the control.)

**Step (b) landed:** `tools/gbrules.py` (the reference) and
`src/engine/rules_gb.c` (plain C99, held to it by `tests/test_rules_gb.py`
on synthetic files and on the real DQK.EXE when present) read the Dark
Queen's 21 tables at fixed data-segment offsets, each checked by a 16-bit
sum. The tables sit in CKIT.EXE's order; what differs is Krynn's shapes:
7 player races, race-class rows of 14 kinds (the engine's row becomes 15
bytes), and XP, XP step and spell row kept for 11 PROGRESSIONS rather than
the 7 slots (clerics by their god's alignment, knights by order, mages by
robe). `rules.h` states the shapes (`race_kinds_stride`, `progressions`,
`xp_levels`); FRUA's readers keep FRUA's numbers until step (c). A design
holding DQK.EXE (`gbgeo.py --exe` copies it in) has the ruleset read when
its first script area comes up (checked in Hatari); `g_rules` is not
switched yet. Checked against the Adventurer's Journal: names, the level
caps, every progression's XP and step, Knights and Paladins lawful good, no
evil characters. Two things in the data are the games', not the port's:
DQK indexes its thief race adjustments, Pool of Radiance's rows in that
game's race order, by its own race number (a kender gets the half-orc's);
and FRUA's own saving-throw table gives thieves an all-zero row, its druid
row holding thief values (the Mac DATA agrees). Both are kept as they are.

**Step (c), first part (fd2349e1): the race numbers are free.** About
thirty race tests written into the engine (elves cannot be raised, humans
alone dual-class, 6 and up is a monster, three racial switches) read
`RACE(r)`, the ruleset's `race_base`: the FRUA race each race number plays
as. FRUA's is the identity; Krynn plays both elves as elves, both dwarves
as dwarves, kender as halflings, 7 and up as monsters. Proof for FRUA: with
`RACE()` compiled as the identity, `boot.c` is the old code instruction for
instruction but for the ruleset's field offsets and `__LINE__` pushes; the
seeded creation, entry-chain and kobold-fight runs are pixel-identical. A
fight that never reaches a spell or a save cannot see a change to those
tests (the monster-as-human control left the kobold fight unchanged), which
is why the object-code comparison is the proof here and the runs the check.

**Step (c) landed.** The creation screen builds its race and class lists
from the ruleset (labels, class rows, default race; DQK.EXE's own creation
labels; every DLItem index after the race list moves with the race count;
the class column moves right when a race label is long, "SILVANESTI ELF").
XP and spell slots are read by progression, and the spell-slot table is in
the ruleset (DQK.EXE's 8 rows at DS 0xb96). The training hall stops a Krynn
character at its race_level_cap, Krynn having one maximum per race and class
where FRUA has prime-requisite exceptions in code. A design holding DQK.EXE
now PLAYS by the Dark Queen's rules, chosen when the design is loaded (jt361)
and FRUA's for any other, with the game's own item type table (ITEMS.DAT)
when it holds one; gbgeo.py --exe copies both in, and gbmon / gbchar keep
the game's race numbers (monsters 7, kender 5, humans 6) for such a design.
Each slice was held to FRUA by object-code comparison where the change is
an indirection, and by seeded creation and entry-chain runs (AE 0).
Checked in Hatari on the converted design: Krynn's creation screen (a
Human Knight offered Lawful Good only, LEVEL 3 at 15,000 XP by the Crown's
table), the six heroes in the hall (Roland of Dust a level 11 Knight with
his Solamnic plate readied; Walker Keys a kender cleric/thief 10/12; Roark a
hill dwarf), and a fight with the game's own monsters played out by
FRUA_CBTPLAY. Open, for later steps: a character's god, order and robe (a
Krynn cleric is of good, a knight of the Crown, a mage a White Robe until
then; step (e)); Roland's AC recomputes to -7 against the -11 his save
carries (an item property, for (f)); money and item words are still FRUA's
("PLATINUM"); FRUA's age roll adds 10 years, unchecked against Krynn's.

**Step (d) landed.** The training hall reads the Dark Queen's own
character files. A `.QCH` in a design's SAVE folder is listed by Add
Character and loaded at boot beside the `.CCH` files; `src/engine/qch.c`
(plain C99, held byte for byte to `gbchar.py` on synthetic files and the six
Steam heroes by `tests/test_qch.py`) turns it into the `.CCH` stream, and
FRUA's own loader, jt577, reads that from memory (jt401 serves a reserved
refnum). The race stays the game's when the design holds DQK.EXE, as in
gbchar. The hall never writes a `.QCH`: a hero it saves gets a `.CCH` of
the same stem, which is read in its place from then on (its save on Add
writes the whole roster, so that happens the first time anyone is added),
and renaming or deleting a hero leaves the `.QCH` alone, so a deleted Dark
Queen hero returns as the game shipped him. gbgeo.py --exe copies the
game's SAVE\*.QCH in, and gbchar.py is now a reference only. A design with
heroes of its own is no longer migrated into: the 2026-08-03 migration of
an old install's flat CHAR*.CHR roster ran for ANY design whose SAVE held
no `.CCH`, and copied that roster into it. Checked in Hatari: the six heroes
listed from their `.QCH` files alone, Walker Keys added and shown as a
kender cleric/thief 10/12 with his hoopak readied, Roland AC -7 / 134 HP as
through gbchar, and after a save the list still six (read from the `.CCH`
files). FRUA: seeded HEIRS Add list, hall, entry chain and creation sheets
pixel-identical to the previous commit (with an A/A control).

**Step (e) landed: Knights, gods, robes and orders.** DQK.EXE 0xbb19
chooses the progression a class slot plays by from three bytes of the
character: the god (0x19b; 1 Paladine, 2 Majere, 3 Kiri-Jolith, 4 Mishakal,
5 Sirrion, 6 Reorx, 7 Shinare; above 4 a cleric of neutrality), the robe
(0x19d; white 1, otherwise red) and the knightly order (0x19a; Crown, Sword,
Rose). The engine keeps them packed in rec[91] (REC_KRYNN), which FRUA's
record leaves 0. No lifted or Mac code reads it, and every FRUA character
holds 0. PROG() asks the ruleset's prog(). The .QCH reader and gbmon keep
the bytes. Behind `g_rules->knights`: past level 5, Sword and Rose knights
add their order's row to all seven clerical slots (jt908, from DQK
0x62146); a Knight of the Rose below level 40 has no race cap (0xbc2c); and
creation sets the three bytes as the game does. A knight is of the Crown.
The robe follows alignment. A cleric picks his god in FRUA's list picker
from his alignment's gods, and a dwarf's is Reorx. Creation also gives a
Krynn knight his class slot, which FRUA's per-class setup (l13ee) never
did for kind 1. Checked, traced in Hatari: Cinder (Sirrion, WIS 18) has
6/6/4/3 cleric slots, the neutral row; a Sword copy of Roland has 4/2/1.
Characters created and read back: Mishakal, a red robe, Reorx, the Crown
with class mask 16. FRUA: seeded HEIRS runs match the previous commit.
Open:
- what each god grants (DQK 0x3ddff ORs a flag byte, 0x15e, which lands
  among FRUA's spellbook bits, and gives Kiri-Jolith and dwarven Reorx an
  ability point; not given, since as FRUA spells those bits would be wrong
  ones);
- promotion to the Sword and Rose, which scripts do through the character
  cells (DQK 0x406cd reads all three bytes; the cells are not mapped);
- a human who changes class into mage gets no robe (DQK's 0x3c3e5 does
  that; its caller is not traced);
- the sheet does not show the god, robe or order;
- GOLDBOX.DSN's monsters were converted before gbmon kept the bytes.

**Step (f) landed: items, monsters, maps, scripts and saves read natively.**
A design holding the Dark Queen's files needs none of the converted ones;
each reader is plain C99 held to its Python reference by tests on synthetic
files and on the real game's, when present. The design folder is looked in
first, then the game's DISK1..3 folders, so the files can stay where the
game installs them (a subfolder rides in the file name, "DISK1\ITEM.DAT":
joined with jt431 the shims cut it away and open FRUA's root file instead).
- `src/engine/gbitems.c` (6683e15c, reference gbgeo.py): the item table from
  the game's ITEM.DAT (17-byte entries made FRUA's 18), the item words past
  FRUA's 126 and the nine shop stock lists from DQK.EXE, at fixed positions
  checked by sums. Krynn's coin is steel (4adc8319): the same record word
  as FRUA's platinum, named from DQK.EXE's money table through
  `rules_money_name()` and `rules_coin_text()`.
- `src/engine/gbmon.c` (42f58bdf, reference gbmon.py): a script's monster
  read from member nnn of MONCHA.GLB when the design has no MONSTnnn.DAT,
  in FRUA's 450-byte layout, its items numbered from 1 (a6fd7f6f: entry k
  is item k + 1, settled by the two monsters both games share).
- `src/engine/gbmaps.c` (26ab9295, reference gbgeo.py): GEOnnn, GBDnnn,
  GBMAP and ECLnnn made from GEO.GLB and ECL.GLB when missing, the bytes
  gbgeo.py would write, all 20 maps and 47 scripts compared.
- `src/engine/gbsave.c` (ce6de3b9, 582f102f, reference gbsave.py): the
  game's own saved games, SAVGAMx.QSV, listed by the load menu beside the
  engine's slots (a .CSV of the same slot shadows it; the .QSV is never
  written). The layout was found by saving in the game in DOSBox: 1024
  script cells, the square and facing, then the party as .QCH records. A
  load restores the party (an NPC keeps its bit), the map, square and
  facing and the script memory, so the running script resumes; checked on
  a Caergoth save whose dragon ambush strikes on the same step as in DOS.
FRUA modules: every slice was held to the previous commit by seeded HEIRS
runs (Add list, hall, sheet, treasure chain; the saves also the load
picker, loaded party and walk) with an A/A control.
Open:
- a save's clock and the vault (VAULTx.DAT) are not carried;
- Roland's AC still recomputes to -7 against the -11 his save carries
  (closed 2026-11-23: the game itself shows -7 for him, in the hall, in
  camp and on the walk screen in DOSBox, so the -11 is not the AC it
  displays and the port agrees with it);
- writing back in the game's own formats (decision 4) is not begun.

**Step (g) landed: a design can bring its own rules** (6ddba91c, 9af5eb54).
`tools/rules.py` compiles RULES.DAT from a text file (a statement or a table
row a line, each row named in a comment) and decompiles one back; it writes
FRUA's rules (from the Mac resource fork) or the Dark Queen's (from DQK.EXE)
as text for a designer to start from. The file carries the shapes, names and
creation labels and every table of rules.h in the layout the engine indexes,
each chunk on a long boundary so the tables are played in place, and,
optionally, the progression a slot plays by picked by god, order or robe:
DQK.EXE 0xbb19 as data. The class list is carried, not derived, because
FRUA's creation screen gives its flagged cleric/thief the row before where
the rule used for DQK.EXE gives the row after. `src/engine/rules_file.c`
parses it (plain C99, held to rules.py by `tests/test_rules_file.py`), and a
design whose folder holds RULES.DAT plays by it, in place of DQK.EXE's rules
(a design started from Krynn's keeps the game's items and words). Checked in
Hatari, seeded: FRUA's rules as a file give HEIRS the same creation screen,
sheet and reroll as the built-in rules (AE 0, A/A 0); one start age changed
in that file changes the sheet; Krynn's file, a race renamed, lists its seven
races on a design with no DQK.EXE. HEIRS without the file matches the
previous commit. Open:
- the creation screen has room for seven races, so a file is held to seven;
- choosing a ruleset in the editor (Create New Design) is not done: the file
  is copied into the design folder;
- `rules.py frua` reads the Mac resource fork; a DOS-only install has no way
  to write FRUA's rules as text yet;
- what a ruleset cannot say is still code: the racial rules written into the
  engine are reached through `race_base`, and Krynn's knights, gods and robes
  through the `knights` flag.

**The character cells are mapped** (ADR-0023's open item). In the Dark Queen
a script variable at 0x401..0x800 is a field of the selected character:
DQK.EXE sorts every variable address (0x40637) and hands that range to a
reader (0x406cd: the memorized spells as a range, and a 58-case switch) and
a writer (0x41e1e: the spells and a 14-case switch); any other cell reads 0
and takes no write. `tools/gbcells.py` is the reference: it holds the tables
and recovers them from DQK.EXE's bytes (`parse_exe`, which interprets each
switch arm), and `tests/test_gbcells.py` holds the two together, holds
`src/engine/gbcells.c` (plain C99) to the reference, and checks that a cell
read on the engine's record of a converted .QCH gives what DQK reads on the
.QCH itself. A DQK offset goes through qch.c's field map and FRUA's
in-memory byte order (L0ce0), so steel is rec[76] as a big-endian word,
spells rec[198..], god/robe/order the packed rec[91]. The core asks the host
first on every numeric read and write a script makes (`ecl_host.var`,
`ecl_var_get/set`); string cells, and the host's own peeks and pokes, stay
in script memory, so the name at 0x401 is still served by gbs_seed_who.
Champions and Death Knights keep the range as script memory. Every cell the
Dark Queen's scripts use (19 of them; status is compared 112 times, steel
changed in about 60 places) is covered, and every one they write is one the
game makes writable. Checked in Hatari on GOLDBOX.DSN with Cinder: a test
script read steel 200, HP 78, status 0, STR 12, god 5, level 8, added 25
steel and took 1 HP, and the sheet then showed 225 and 77/78. Open:
- the race cell reads rec[88] as it is, which is the game's race only under
  the game's own rules (a design converted without DQK.EXE renumbers it);
- DQK's word at 0x19c (read with the god) and the nine bytes FRUA lacks read
  0; the knightly order is read-only in DQK too, so a promotion to the Sword
  or the Rose is not done through these cells.

**The clock and the vault are carried** (step (f)'s open item). The Dark
Queen keeps its clock nowhere but script cells 5..11 (rounds, minutes, tens
of minutes, hours, days, months, years), and they are FRUA's game clock: the
game record's h[5..11], the same seven fields with the same radices
(10/10/6/24/30/12/100, read out of the Mac DATA at -23228). Both carry
lazily, one radix a tick (save B has day 38 of month 0; save C, two minutes
later, day 8 of month 1). So `gbs_var` serves those cells from h[] (a script
that waits "until the turn of the hour" or sails 15 days moves the HUD
clock), and `gbs_qsv_load` copies them there; `tests/test_gbsave.py` reads
the README's four times out of the four real saves. The vault: area byte
0x31 before a COMBAT with no monsters, which the census showed beside
"WELCOME TO THE IMPERIAL BANK OF KRISTOPHAN.", so `gbs_vault` runs jt185,
FRUA's vault screen. VAULT<c>.DAT (the same name as FRUA's Vault<c>.DAT) is
FRUA's layout with the game's 17-byte items, padded to 50: always 866 bytes,
a size FRUA's never has, so jt74 reads either by size, and jt75 writes the
game's format in a design holding DQK.EXE so the game can still read a slot
the port saved into. Reference `gbsave.read_vault`. Checked in Hatari on
GOLDBOX.DSN: save D loads at 11:19 AM and the first step makes it 11:20; a
script adding 2 to cell 8 moved the HUD to 2:00 AM; 50 steel deposited came
back out of an 866-byte VaultT.DAT on the next visit; a vault written as the
game writes one, holding the first item of Cinder's .QCH, offered "40
ARROWS +2". A vault in that format holds 50 items, as the game's does
(DQK.EXE 0x5def7: "Vault is full!" past 50; FRUA's is 200).

**What each god grants** (step (e)'s open item). DQK.EXE 0x3ddff, run at
creation, sets bits of the character's byte 0x15e and, for Kiri-Jolith and
for a dwarf of Reorx, adds 1 to byte 0x87. 0x15e is byte 7 of the known-spell
bitmap (rec[346] here; bit k is spell 57 + k), and the bits are the god's own
spells: ids 57 and 59..64 are class 3 in both games' spell tables, which
FRUA kept from Krynn along with the "Special" level name and the label
"   Special :". 0x87 is the base THAC0 (rec[127]); DQK's recompute (0x624e1)
clears it, re-derives it from the class tables and adds the god's 1 again
(0x625d4), so the bonus stands. The shipped heroes carry exactly those bits.
How the game uses the spells was read off its own screen in DOSBox: Cinder's
grimoire lists BURNING HANDS under SPECIAL with "SPECIAL : 1", Balthir's
REMOVE CURSE, BLESS and CHARM PERSON with "SPECIAL : 3" (his fourth bit is
Cure Serious Wounds, class 0): one Special slot per god spell known. Where
DQK.EXE computes that count is not found; it is the inference that fits both.
Done, all behind `g_rules->knights`: the bits at creation, the +1 in all
three THAC0 derivations, `rec_spell_slots` (class 3 = the god spells known,
in place of FRUA's rec[355 + 27 - 1], which is the mage 9th-level slot) at
every slot read, class 3 cast as clerical in l4e2c (DQK's own check sends
class 3 down its class-0 arm), and a "Special :" row in the memorize grid.
Checked in Hatari with save D: Cinder's and Balthir's grimoires match the DOS
screens line for line, and memorizing Burning Hands takes Special to 0.
Open: the game's "Bonus :" row (spell bonus slots) is not drawn; casting a
Special spell in a fight is untested; DQK sets a global (DS:-0x387c, 0x15 or
0x2e by god) whose purpose is unknown.
(Later: the Bonus row and the Special spell in a fight are below. The
global is written only, never read at its own address: 0xff by default,
0x15 for the good gods (Paladine, Majere, Kiri-Jolith, Mishakal), 0x2e for
the neutral ones (Sirrion, Reorx, Shinare). A reader would have to come at
it through a base and an offset; none is found, so nothing is given for it.)

**Krynn's moons** (the "Bonus :" row above). The Bonus is a mage's moon:
DQK.EXE's grid (0x4501c) gives a character with mage levels and a robe 1
bonus spell when his moon is waxing and 2 at high sanction, less every mage
spell memorized past a level's slots, and its memorize lets a mage spell
through on it (0x45535). FRUA kept all of that around one byte, -22727:
jt961 clears it where DQK computes it, l0bc6 spends it, l54ac (DQK's trim,
0x4f41d) uses it. The moons are script cells: the phases of Solinari,
Lunitari and Nuitari at 0x1fa..0x1fc (0 low sanction, 1 waxing, 2 high, 3
waning), a day count each at 0x1fd..0x1ff. When the hours carry into a day
(DQK's carry, 0x60c8, serves the game clock and the rest countdown, as
FRUA's l025a does) 0x5e58 counts each moon on, and one whose count has
reached its period (8, 1, 6) moves a phase and counts from 0 (0x5d6e): 36-,
8- and 28-day cycles. A new game starts all three at high sanction
(0x37f0b). The start-up script stores "BALDRIC_NAME" at cell 509, one
character a cell, over the counts ('B', 'A', 'L'), so all three move on the
first day. (That the Journal's other moon effects were not in DQK.EXE,
as first written here, was wrong; see below.) The Special slots are now read out of DQK.EXE too (0x44ad8: a
cleric has 3 for Mishakal, none for Reorx, 1 for any other god), the
inference above confirmed. Checked in DOSBox against Hatari: from save D
Cinder (Red Robe) shows "Bonus : 2" in both; a sixth 1st-level mage spell
takes it to 1 in the port; after a day's rest the row is gone in both;
DOS save H (four days after D, kept in data/work/dqk-saves) holds exactly
the cells the rule gives (tests/test_krynn_moons.py), and one day after H
both show "Bonus : 1". Only the Dark Queen has the cells; elsewhere the
moons give nothing. Open: the moon jewels under the portrait (DQK draws
icons 0x1d + phase etc.) are not drawn on FRUA's frame.

**A god's spell, memorized and cast** (the open item above). A Special spell
is level 0 in the table, and FRUA's rest time (l0006_c21, 4 hours and 15
minutes a spell level) and its one-at-a-time pacing (l0876) both read that
level: no rest time, so the pending spell never memorized. DQK.EXE studies a
level-0 spell at the level 0x6cc9 gives it (Prot. from Evil 10' 4, Silence
15' 2, Remove Curse 3, the rest 1; its rest time 0x4480e and pacing 0x666e):
`spell_study_level`. Cinder's Burning Hands then rests 4:15 in both games.
And the caster level (jt17) had no arm for class 3 (the Mac's stack
garbage); DQK's copy of the switch (0xb56d) sends class 3 down the clerical
arm, now done under Krynn's rules. Checked in Hatari: the memorized Special
spell is offered by CAST in a fight (Caergoth's dragons) and targets with the
range rule (touch: only an adjacent dragon); the damage at the clerical level
was not seen, the fight never put Cinder beside a dragon with her turn free.

**The moons also shift saves and casting level.** Searching for a robe's
phase as 0x1f9 + robe found only the grid and the trim; DQK.EXE also reaches
it as 0x1f8 + robe, then + 1, in two more places. Its save routine (0x85cc,
FRUA's jt866) gives a mage of a robe -1 at low sanction and +1 at high, and
its caster level (0xb616, FRUA's jt17's mage arm) the same, the +1 only for
a mage above 5th level (the Journal's INT 15 is not asked). Both are given
now (`krynn_moon_save`, `krynn_moon_level`). A phase is only read where the
Dark Queen's cells exist; elsewhere krynn_moon_phase answers KRYNN_NO_MOON,
not low sanction, so no design without them takes the -1.

**Knight new Order.** The Dark Queen's hall has no Human Change Class; its
item 4 is "Knight new Order" (accelerator K), enabled as FRUA enables item
4, in a training hall. Under Krynn's rules the port's item 4 is that
(`krynn_knight_order`, DQK.EXE 0x487f2): a Crown knight may become a Sword
at knight level 3 with the Sword's level-3 XP, a Sword a Rose at level 4
with the Rose's level-4 XP (the game compares its XP table's own cells, so
the port asks jt26 at the next order), each with base abilities from
DS:0x7d98 / 0x7da4. Changing (0x48710) raises the order and takes away
knight levels while his XP is short of the new order's threshold for his
level, each with 2 HP above 9th level, else knight dice HP / (level + 1)
plus the CON bonus; then jt910 (DQK's 0x1d99:0x13af is FRUA's jt910 call
for call) and jt33. Checked in DOSBox and Hatari with the shipped Roland
of Dust (Crown 11, 1,000,001 XP, 134 HP): Sword 9 at 130 HP, then Rose 9,
the screens line for line. DOS's sheet shows his new THAC0 at once; FRUA's
View paints the stored one, so the port runs jt21 after the change (where
DQK derives it was not found). The sheet still says "Knight", not "Knight
of the Sword" (open, with god and robe).

**The sheet's words.** DQK's character sheet (0x5cd06) names a kind from
a table of its own ("Cleric/Fighter/Mage", where the kind names elsewhere
say "Magic-User"), then puts a cleric's god and a space in front, a
mage's robe and a space before the first "M?g", and a knight's order on
the end: "Sirrion Cleric/Fighter/Red Mage", "Knight of the Sword". Those
words are part of a ruleset now (`sheet_kind`, `god_name`, `robe_name`,
`order_name` in rules.h): rules_gb.c reads them from DQK.EXE (0x3af6,
0x3c0a, 0x3c2a, and the strings at 0x8d8c), RULES.DAT carries them in an
optional SHET chunk (`sheet_kind`/`god`/`robe`/`order` lines in the text),
and FRUA's rules have none, so FRUA's sheet is unchanged. The six shipped
heroes read as in DOS (tests/test_krynn_sheet.py).

**Context.** The Gold Box games OpenUA is opening up to (ADR-0023) do not all
share FRUA's classes and races. The Krynn games have seven player races
(Silvanesti and Qualinesti elves, half-elves, mountain and hill dwarves,
kender, humans) with Monster as race 7, and a Knight class where FRUA's slot 1
is a druid. Converting Dark Queen's ready-made heroes into FRUA characters
(`tools/gbchar.py`) showed the cost of pretending otherwise: the hall applied
druid equipment rules to a Knight and unreadied his sword and plate.

It also showed the cost of converting at all. Today a converted game needs a
PC and Python before it can be played (gbgeo, gbmon, gbchar), a converted
character can never go back to the original game, and every format is known
twice, once by a converter and once by the engine. The scripts already run
from the game's own bytecode (ecl.c), and the module manager does its work on
the machine (ADR-0025); the rest should follow.

An inventory of the engine (2026-11-19) found the class and race rules in two
forms:

- **About 25 tables in the A5 world**, from the Mac DATA resource (DOS: the
  CKIT.EXE runs of ADR-0017) or `a5_scalars.c`: class and race names, the
  race-to-class and class-to-alignment lists, ability minimums and race
  ability ranges, age, race level caps (-28048), XP per level, THAC0
  (-23184), saving throws (-28818), class bits for items (-23030) and the
  training hall (-23023), hit dice and name level, thief skills with race and
  DEX adjustments, spell progression rows and slot tables. None of them can
  be overridden by a design today.
- **Rules written into code**: racial stat modifiers and innate abilities
  (l24d2, l2284), class abilities at creation (l13ee), attack rates and HP
  past name level by slot (jt910, l5274/jt885), which slot casts which spell
  school (jt908, l4e2c), a second racial level-limit switch in training
  (jt557), dual class for humans only, Raise Dead refusing elves, elves'
  to-hit bonus with bows and swords. Two numbers are reserved: race 6 means
  "monster" in about nine combat and spell tests, and the creation screen
  builds its race and class lists from literal strings.

Dark Queen keeps its own tables in DQK.EXE's data segment in the same shapes;
the race-by-class level limits (DS:0x17dc, 8 races x 7 classes) match the
Adventurer's Journal's table row for row.

**Decision.**

1. **The engine reads each game's own files at run time.** Characters (.QCH
   and the other games' CHRDATA files), saves, item tables and item-name
   words, monsters, maps and rules are read in their native formats by
   loaders in C. The goal is to play a game from its own install folder,
   copied to the machine as it is. Reached in steps: until a loader exists,
   the offline tool stays the way in.
2. **The Python tools become reference implementations.** Each stays the
   tested description of its format, and the C loader is held to it by
   tests on synthetic data, as `ecl.c` is to `ecl.py` and `modcore.c` to
   `tools/modman`. They stop being a step a player has to run.
3. **One ruleset structure in the engine, filled by loaders.** FRUA's rules
   are built in, and a design that brings nothing gets them, so every FRUA
   module and save behaves exactly as before. A game's own rules are read
   from its files (DQK.EXE's data segment: tables found by shape, or at
   known offsets for an executable recognised by checksum), each table
   checked once against the game's printed tables. A new game, which has no
   original to read, writes its classes and races in a ruleset file
   (`tools/rules.py` compiles a readable text file and decompiles one back,
   so a designer can start from FRUA's or Krynn's).
4. **The character record does not change.** Seven class-level slots, one
   race byte, the 17 class kinds and the class-bit mask stay where they are;
   the ruleset says what each number means. Dark Queen's slot 1 is Knight,
   FRUA's stays Druid. A character or save is read under its own game's
   ruleset. Writing back in the game's own format, so characters move
   between OpenUA and the original, follows reading.
5. **Hard-coded rules become ruleset fields, one at a time, FRUA's values the
   default**, each held to the deterministic A/B harness (`-DFRUA_RNGSEED`):
   a FRUA module must play byte-identically before and after. The reserved
   race 6 becomes the ruleset's monster race.
6. **Rules FRUA never had are engine features, added when a game needs
   them** (the Knights' orders and their spells; the Krynn mages' robes and
   moons), each behind a ruleset flag.

**Order of work.** (a) the ruleset structure, FRUA's rules moved into it,
parity-proven; (b) Dark Queen's rules read from DQK.EXE; (c) the creation
screen, sheet and training hall driven by the ruleset, the monster race
freed, the hard-coded rules moved over; (d) .QCH characters read by the hall;
(e) Knights; (f) the item tables, words and monsters read natively, then the
maps, then saves; (g) choosing or writing a ruleset for a new design.

**Not decided here:** the editor's rules screen and Krynn's moon magic. (A
game's own install folder is already 8.3: they are DOS games. Only the Steam
wrapper folders around it have long names, and nothing needs those.)
