# Engine bring-up probe

The engine's entry path (`ua_main` in `src/engine/boot.c`) calls several
dozen functions that are still no-op stubs awaiting lifting. Bringing up
the shim was step one; the next priorities want to be driven by what the
engine actually tries to do per frame, not by guessing.

## Running a probe

Build with the `ENGINE_PROBE=1` knob — every stub in `boot.c` and
`master.c` then logs `stub: <name>` as it's called, `jt315` (the play-loop
predicate) fires exactly once so the per-iteration body runs, and the
trail mirrors to the host terminal via Hatari's `--conout 2`.

```sh
make clean
make ENGINE_PROBE=1 run
```

Default builds are unaffected; the macro expands to `((void)0)` when
`FRUA_ENGINE_PROBE` isn't defined.

## First probe — 2026-05-23

The probe ran `ua_main` end to end (rc = 0) — no crashes, no missing
symbols. The full sequence:

```
main: entered → display up → shim up

# Phase 3 — screen-mode setup (boot.c)
jt398 → jt411 → jt1129

# master_init() — Toolbox + page setup + fc_init
l0eda → jt1157 → jt1155 → jt1138 → l01a2 → l024c → (fc_init, real)
   → l35e2 → l27a4

# Phase 4 — secondary init, first UI handler
jt480 → jt989 (handler "Pod") → l4d98 → l0444 → jt361 → jt920 → jt1009

# Phase 5 — string check, second UI handler
jt919 (string-2 mismatch — no resources loaded yet)
l6ada → jt977 → l3918 → jt989 (handler "Pod") → jt1130 → l5888

# Phase 6 — play loop, one iteration via the probe
jt315 (firing) → jt949 → jt956 → jt920 → l07dc → jt315 (done)

# Shutdown
jt445 → l5ac0 → (master_shutdown)
   l27bc → l35f8 → (fc_cleanup, real) → jt1156 → l01ac
   → jt1119 → jt1114 → jt0f14 → jt1158
jt415

main: ua_main rc = 0
```

## What the probe tells us

- **The entire bring-up surface is sufficient.** No crash, no unlinked
  symbol; every Toolbox call the engine makes during boot resolves to a
  lifted function or a no-op stub that returns cleanly.
- **The per-frame call surface is four entries:** `jt949`, `jt956`,
  `jt920`, `l07dc`. `l07dc` is documented as the per-iteration body —
  this is the highest-impact lift for visible engine behaviour. `jt315`
  is the predicate that gates the loop.
- **`jt919` runs unconditionally** because `ua_get_string(2)` returns
  empty (no FRSC archive loaded). Lifting the FRSC packer (`tools/rsrcpack`)
  and feeding the engine real STR# resources changes that branch.

## Next-up priorities driven by the probe

> **2026-05-25 refresh.** The list below is the original 2026-05-23
> snapshot. See the **Current frontier (2026-05-25)** section at the
> bottom of this file for the up-to-date status.

1. `l07dc` — the per-frame body. Largest visible effect.
2. `jt315` — the play-loop predicate. Determines when the engine quits.
3. `jt398` — control-file probe (`:DISK4:ALWAYS.CTL`). Drives the
   "small / large screen mode" branch in phase 3.
4. `jt989` — UI-handler registration. Called twice during init; the
   registered handlers run later from the event loop.

Re-run the probe after each engine lift to see which stubs fall out of
the trace and which new ones appear.

## Seventh probe — after lifting jt480 + synthetic strtab

jt480 (CODE 3 + 0x3c6) is the string-table setter — two instructions:
`movew arg1, A5_-10276; movel arg2, A5_-10280`. ua_main forwards its
own (arg1, arg2) here, so on the Mac the THINK C runtime's
DATA+DREL-computed string table flows in before phase-4 starts. main()
now passes a small synthetic table (index 2 = "Heart") because the
runtime's pool replay isn't lifted yet.

First behavioral change in the probe trace: `jt919` no longer fires.
Phase 5's `if (ua_strcmp(ua_get_string(2), "Heart") != 0) jt919()`
gate finally evaluates to false because index 2 returns "Heart"
through the now-real ua_get_string.

```
... → jt989 → l4d98 → l0444 → jt361 → jt920 → jt1009
                                            ^^^^^^ jt919 used to be here
   → l6ada → jt977 → l3918 → jt989 → jt1130 → l5888 → ...
```

The shutdown's matching `if (ua_strcmp(ua_get_string(2), "Heart") == 0)
fc_dump(0L)` now runs fc_dump (real — no PROBE marker, so it's not in
the trace) at exit. ua_main still rc=0.

## Sixth probe — after lifting JT[399]

JT[399] (CODE 3 + 0x39d2) is the engine's memset-equivalent: fill `size`
bytes at `buf` with the low byte of `fill`. Mac C signature places
`buf` first, `size` second, `fill` third — the right-to-left pushed
order puts the first C arg at fp@(8), which earlier stubs of jt399 had
in the wrong order. Lifting forces the fix: the four call sites in
L5124 and jt918 now pass arguments correctly, and the memset runs for
real.

Trace unchanged from the previous probe — the lift is behavioral
correctness without new visible work, since the buffers being filled
are A5-world bytes the engine hasn't started reading yet.

## Fifth probe — after lifting jt942 / jt943

jt942 (CODE 20 + 0x472a) and jt943 (CODE 20 + 0x4738) are a paired
setter / getter on g_a5_4944, the byte that gates L07dc's inner loop:
jt942 stores the low byte of its arg, jt943 reads it back. One-line
lifts each.

Trace inside the play-loop iteration is unchanged from the L5124 probe
(jt943 isn't reached because jt918 still declines and the new-game
branch `goto cleanup`s before the predicate check at the bottom of the
loop). The lift is structural: the next caller that sets g_a5_4944 to
a non-zero value gets the loop continuation correctly.

## Fourth probe — after the L5124 lift

L5124 (CODE 6 + 0x5124) is L07dc's first-time init: zeros three buffers
through JT[399], sets a handful of fields inside the player-data handle,
resets ~30 A5-world bytes / shorts / longs to game-start defaults, then
calls JT[174] (per-segment graphics init).

Trace inside the play-loop iteration:

```
stub: l07dc → l5124 → jt399 → jt174 → jt942 → l5888 → jt918 (skeleton)
            → jt399 → jt131 → l5888
```

Two of L5124's three JT[399] calls are guarded by NULL checks on the
handle pointer (`g_a5_28006`) and the 2000-byte buffer pointer
(`g_a5_13038`); neither is set yet, so only the third JT[399] (6 bytes
into adjacent A5 statics) fires. Lifting whichever engine code creates
those handles unblocks the full L5124 init.

## Third probe — after the jt918 skeleton lift

jt918 is the new-game / select-design dialog at CODE 12 + 0x0d90 — a
~1300-byte function with ~30 inner calls. The first cut captures only
the entry side effects (set three A5 globals, populate a 4-byte buffer
via `JT[399]`, kick off the UI via `JT[131](6)`) and returns 0 to
preserve the prior "user declined" behaviour. The main loop at L0dd4 →
L125e is documented in the skeleton's docstring but not yet executed.

```
stub: l07dc → l5124 → jt942 → l5888 → jt918 (skeleton)
            → jt399 → jt131 → l5888
```

`jt918`'s main loop is the next pass; the body dispatches the Delete /
Create / Select / Play / Edit menu through `L0aae` and per-segment
`JT[3]`.

## Second probe — after the L07dc lift

Lifting L07dc replaced the single-line `stub: l07dc` with the body's own
call sequence — five fresh callees out of the eleven L07dc dispatches
to. With the mode flag clearing to "new game" and `jt918` declining, the
loop short-circuits to the cleanup tail:

```
stub: l07dc → l5124 → jt942 → l5888 → jt918 → l5888
```

`l5124`, `jt918`, and the cleanup `l5888` are the obvious next targets.
The other six callees inside L07dc (`l4b40`, `l67ca`, `l68f8`, `l2cb0`,
`jt582`, `jt941`, `jt937`, `jt938`, `jt217`, `jt948`, `jt943`) only run
once the mode flag is non-zero or `jt918` returns non-zero — they wait
for those branches to enable.


## Current frontier (2026-05-25)

The lifts since the seventh probe close out a wide swath of the
engine's state-transition + UI loops. The high-impact paths now run
as lifted C end-to-end (with the last leg of each chain still a PROBE
stub waiting for its own pass).

### Lifted since the seventh probe

#### Sub-resource release chain
- **`jt131`** (CODE 6 + 0x35e) — state-transition manager. `a` →
  `g_a5_31234` with a JT[3] case table picking a tear-down arm for
  the previous mode (`l5700` / `l5864` / no-op), then runs `jt209(1)`
  on entry to state 4.
- **`jt209` + `jt204`** (CODE 7 + 0x70e8 / 0x6ed8) — walk three slot
  pointers / one slot pointer through `jt115`, stamp the per-slot
  sentinel.
- **`jt115`** (CODE 6 + 0x31dc) — generic slot release: if the slot
  is non-NULL and the cached block's tag is non-negative, call
  `jt461(tag)` and stamp the block's first word to -1.
- **`jt461`** (CODE 3 + 0xb66) — stamp `g_a5_10074[tag] = 0xFF` in
  the engine's free-id byte table.

The full chain `jt131 → jt209/jt204 → jt115 → jt461` now runs end to
end without a PROBE stop.

#### Cache compactor
- **`jt465`** (CODE 3 + 0xb7a) — flush records matching a key.
- **`l103c`** (CODE 3 + 0x103c) — compactor: renumbers the 48-byte
  freemap + companion `g_a5_9354` array, BlockMoves the offset table
  and the 14-byte record table.
- Small CODE-3 string helpers along the way: `l466a` (isupper),
  `l46b2` (tolower), `l39ae` (strlen), `l3bda` (case-insensitive equal),
  `l3cfa` (basename-after-colon strcpy), `l1020`/`l366a` (BlockMove
  wrappers), `jt384` (strcpy).

#### Mode-cleanup helpers
- **`l5700`** / **`l5864`** (CODE 6 + 0x5700 / 0x5864) — slot-1 and
  slot-2 tear-down arms `jt131` dispatches into.
- **`l5f4e`** (CODE 6 + 0x5f4e) — three-line `jt399(buf, size, 0)`
  wrapper used by the tear-downs.

#### Saved-game branch
- **`l67ca`** (CODE 6 + 0x67ca) — `jt76` reset + two `l66e6` clears +
  `jt80(2)` mode flip + two `jt1001(8000, 8000, 1, 9 / 21)` redraw-prep
  calls + JT[1] direction switch on `g_a5_27980[g_a5_12286 * 3]` (E/N/S/W
  → channels 25/22/23/24, default fall-through) + `l08e6(1)` redraw
  flag.
- **`jt80`** (CODE 6 + 0x68ae) — secondary-mode toggle.
- **`l08e6`** (CODE 6 + 0x08e6) — one-line set of the post-transition
  redraw flag.

#### New-game / select-design dialog (`jt918`)
- **`jt918`** body (CODE 12 + 0x0d90) — entry side effects, the
  `L0dd4` prologue (`jt112`/`jt108`/`jt81`), the `L0df6`/`L0e98`
  cluster setup for the 12-byte c79x flag array, the `L0ec6` input
  poll, and the JT[3] switch on `local 0..11`. The exit-edge plumbing
  (cases 8 / 10 return 1, case 11 returns 0) flows back through the
  switch via the case bodies' `int` return.
- All twelve **`l0f1a..l120c`** case bodies — Train / Modify / Delete /
  Create / Remove / Add / View / Human Change Class / Exit / Begin /
  Save / Load. Each routes the c79x flag check to the matching action
  in CODE 17 / CODE 18 / CODE 19 / CODE 20 / CODE 15 (still PROBE
  stubs — `jt574` / `jt556..560` / `jt876..878` / `jt904` / `jt942` /
  `jt584` / `jt585` / etc.).
- **`l0aae`** (CODE 12 + 0x0aae) — design-menu builder: `jt174` +
  `jt447` init, twelve `jt452` item installs (Train / Modify / etc.
  with their accelerator keys), the c79x flag walk that enables /
  disables via `jt444`, the `jt449` / `jt112` / `jt117` finalize +
  `jt453` selection poll + `jt146` shortcut override.
- **`L185e`** (CODE 12 + 0x185e) — Human Change Class "Drop NAME
  forever?" confirmation arm. Fully lifted, including a working
  `jt488` `sprintf` over the static buffer `g_a5_10362[256]`.

#### Channel-write wrapper
- **`jt1001`** (CODE 5 + 0x31ac) — the workhorse called by `jt76` /
  `l66e6` / `jt80` / `l67ca` / `l0aae`. Lifted as `l309c(a, c,
  jt468(b), d)` with `jt468` + `l309c` as PROBE stubs.

### Compat-side companion lifts

These compat managers gained substantial new surface in the same
window. They're not in the engine probe trace, but they're what makes
the engine paths render meaningfully:

- **Menu Manager** (`compat/menus.c`) — `NewMenu`, `GetMenu` (MENU
  resource), `AppendMenu` with `/`-meta key-equivalents, full menu
  bar + pull-down tracking, `MenuKey` for Cmd-key dispatch,
  `MenuSelect` with save-and-restore bits.
- **Dialog Manager** (`compat/dialogs.c`) — `GetNewDialog` / `NewDialog`
  (DLOG resource), `DrawDialog`, `ModalDialog` / `DialogSelect`. DITL
  types 4/5/6 → `ControlHandle` via `NewControl`; type 0x10 →
  `TEHandle` via `TENew`. Double-frame default-item ring.
- **Control Manager** (`compat/controls.c`) — push button / checkbox /
  radio / scroll bar (procIDs 0/1/2/16). `TrackControl` with
  hilite-tracks-mouse + scroll-bar thumb drag + arrow auto-repeat.
  `KillControls` hooked into `DisposeWindow`.
- **TextEdit** (`compat/textedit.c`) — `TERec` at the Mac layout,
  `TENew` / `TEDispose` / `TESetText` / `TEKey` (printable insert +
  backspace + arrows) / `TEClick` (caret-by-pixel) / `TEActivate` /
  `TEDeactivate` / `TEUpdate` / `TEIdle` (caret blink).
- **Sound Manager** (`compat/sound.c`) — Mac API surface (`SndPlay`
  parses `snd ` format-1 → Falcon DMA backend in `platform/sound_falcon.c`
  via `Locksnd` / `Setbuffer` / `Buffoper`). Sample-rate match picks
  the closest of the eight Falcon CODEC rates.

### What this means for the bring-up trace

The play-loop body itself is no longer the bottleneck. With `l07dc` →
`l5124` → `jt918` body running, the iteration enters the design-menu
loop via `l0aae`; with `jt453` returning 0 (its PROBE stub default)
the loop's `iter_guard` breaks after one pass and the function
returns 0, matching the previous skeleton. The PROBE trace inside
one iteration now reads:

```
stub: l07dc → l5124 → jt399 → jt174 → jt942 → l5888 → jt918
            → jt131 → l5700 → jt115 → jt461 → jt209 → jt115 → jt461
                                            → jt204 → jt115 → jt461
            → l0aae → jt174 → jt447 → jt452 ×13 → jt166 → jt158
                    → jt444 ×12 → jt449 → jt112 → jt117 → jt453
                    → jt146 → jt451
            → jt76 → jt108 → l4bf6 → jt1001 ×5 → jt174
            → l02dc → (case body — currently no-op for local = 0)
            → l5888 (iter_guard breaks)
            → jt131 → l5700 → ...
```

### Remaining engine PROBE frontier

The lifts above push the engine surface deep into CODE 12. The
remaining PROBE-only stubs cluster into three groups by code segment:

1. **CODE 12 local helpers.** `L02dc` (character-stat display, ~600
   bytes), `L15e2` (Modify Character body, ~600 bytes), `L12a0` (View
   Character dispatcher, ~800 bytes). Each builds its own sub-window
   via `jt103` / `jt179` and runs a `jt3` sub-switch. Same shape as
   `l0aae` — the lifts are tractable but each is its own commit.

2. **CODE 7 dialog runtime.** `jt158` (display menu), `jt166` (set
   menu mode), `jt179` (event pump), `jt176`, `jt146` / `jt161`
   (shortcut), `jt19` (consume / clear), `jt42` (append message),
   `jt159` (yes/no prompt), `jt904`. These are the actual UI
   primitives — lifting them turns the design menu into a real
   on-screen list.

3. **CODE 4 / 15 / 17 / 18 / 19 / 20 action bodies.** `jt574` (Train),
   `jt556` / `jt557` / `jt560` (Delete / Create / Modify), `jt584` /
   `jt585` (Save / Load), `jt876` / `jt878` (Add / Remove), `jt942`
   (Begin Adventuring), `jt1199` / `jt1118`+ (data-byte read /
   probe), `jt904` (Add Character). Each is one of `jt918`'s twelve
   case bodies' destination — these only fire when the user actually
   picks the matching menu item.

4. **`jt468` / `l309c`.** `jt1001` is lifted but its body is still
   PROBE-only. Lifting these closes the 8000-channel chain.

### Next-up priorities (2026-05-25)

1. **CODE 7 dialog runtime** (`jt158` + `jt166` + `jt179`). With these
   lifted, `l0aae`'s menu actually opens visibly and `jt453` returns
   a real user selection. Largest visible jump in the demo.
2. **DATA + DREL replay.** The unfilled A5 globals (notably the
   direction table at `g_a5_27980` and the strtab indices) take their
   real Mac values, unlocking every comparison-based engine branch.
3. **`L02dc` / `L15e2` / `L12a0`.** Once `jt158` / `jt179` exist, the
   per-action CODE 12 dispatchers become tractable.

## End of PROBE-lift phase (2026-05-28) — see ADR-0010

After ~30 commits across six bring-up rounds, the boot trace has been
exhausted of meaningful PROBE-only stubs. Current 15-second probe state:

- **2017 PROBE log lines** across **117 unique labels**, zero bus or
  address errors.
- **L725c fully routed**: every event arm (mouseDown / mouseUp /
  keyDown / autoKey / updateEvt / activateEvt / osEvt / diskEvt)
  dispatches through a lifted handler.
- **L2d3e** (dialog event loop) iterates 30 times correctly per boot.
- **L1676** (DLItem base handler) fires 222 times with all command
  arms covered (cmd=1, 2, 3 mouse-track, 4 action, 5 select, 16..22
  setters, 32..44 field setters).
- **L4d88** (InvalRect dispatcher) fires 60 times — twice per L2d3e
  iteration, balanced (30 from jt1134 + 30 from jt1118).
- **L6804** (front-window check) fires 60 times — once each from
  jt1134 and L731e.
- **jt397 / jt413** (min/max in text-bounds chain) at 60 each.

### Regression fingerprint

These counts are the boot-trace fingerprint. A change that shifts
any of them is a regression to investigate:

| Label       | Calls |
|-------------|-------|
| L1676       | 222   |
| jt382       | 92    |
| jt381/380/379/378 | 62 each |
| jt377       | 61    |
| L4d88 / L6804 / jt397 / jt413 | 60 each |
| jt468       | 34    |
| L2d3e / jt1134 / jt1005 / L725c / L31ea / L3198 / L66e8 / L6538 / L62fa / L731e / jt1118 / L2856 / jt1153 / jt1200 / jt376 | 30 each |
| jt452       | 14    |
| jt444       | 12    |
| jt115       | 5     |
| L309c / jt1001 | 4     |
| jt174 / l5888 | 3     |

### Phase transition

Per ADR-0010, the remaining unlifted bodies all sit on infrastructure
that doesn't exist yet:

| Blocker | Functions stuck behind it |
|---------|----------------------------|
| Display HAL (pixel destination + font metrics) | L4fae, L4e12, L309c, L3e38 deep blit, L3d8c, jt1084, jt1064, L448c, L4350 |
| Input HAL (Hatari → engine events) | jt1132, L690e cases 3/5/6 + drag/zoom paths, L6dd0 Cmd/key paths, L6cba mouseUp body, cmd=3 / cmd=4 / cmd=5 of L1676 |
| Palette Manager (or VIDEL bridge) | L24aa |
| Audio HAL (Falcon DMA / DSP / YM2149) | jt1122 menu-slot side, audio module SFX |
| Resource manager + module loader | jt361, jt81, jt449, jt938, jt942, jt918, l0444, L0aae, l07dc, jt977, jt956 (some), jt942 |

These don't get attacked by another PROBE-lift session — they need their
own HAL or runtime layer. When that infrastructure lands, the dormant
arms come alive without further engine-side lifting.

### Small lifts still possible without HAL

A handful of PROBE-only stubs are genuinely small but currently dormant
(0-1 calls in boot). Quick wins if/when their dependencies fire:

- `l4350(short flag)` — depth-swap dispatch in L3e8e branch
- `jt1064(long msg, long scaled, short flag)` — activateEvt hit-test
- `jt391` / `jt422` — isprint / printable-index for Cmd-key handler
- `jt1051` / `jt1052` — referenced by jt1142 (mouse-related)
- `L0004` — segment-entry / menu dispatcher (large, but the front edge
  is reachable from MenuKey paths)
