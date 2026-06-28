# Dialog / render / input stack audit (2026-06-27)

Goal: find the *code-level* gaps (true stubs, stand-ins, deferred commits)
behind the in-game-dialog breakage the user keeps hitting — so we fix at the
source instead of poking the running game. Verified against current `boot.c`
and the Mac disasm; not point-in-time guesswork.

Three observed symptoms, each traced to one root:

| Symptom (user) | Root cause | Class | Fix owner |
|---|---|---|---|
| text on **stone** instead of grey box (Trade prompt, "Already using…") | message-bar **plate** (FRAME item 4) not covering the row-24 prompt — **NOT the CLUT** (palette verified correct in-game) | **render/compose** | #147 (re-scoped) |
| can't **type a number** in Trade (jt891); roster keys do nothing | event-pump, runtime | **runtime** (not a stub) | new |
| arrowing the party list does a **huge redraw/recolour**; can't mouse-highlight | present-once + missing keystroke filter | **mixed** | #144 + jt138/jt139 |

---

## 1. The "stone box" is a PLATE-compose bug, NOT a CLUT bug (VERIFIED 2026-06-27)

The original CLUT-clobber theory in this audit was **disproven by a live runtime
diagnostic.** A one-shot log of the hardware CLUT inside `jt891` (the money
amount-entry widget), captured the moment the Trade "how much platinum" prompt
shows in-game, returned:

```
menu_state = 1
clut7 = (187,187,187)   ← light grey, correct
clut8 = (103,103,103)   ← neutral dark grey (r=g=b), correct, NOT clobbered
```

So the in-game palette is **correct and unclobbered**. (The earlier claim that
`g_menu_state != 1` in the dungeon was wrong: `g_menu_state` goes `0 → -1 → 1`
once at first menu load and is **never reset** — boot.c:20066/20147 — so the UI
band restores do run in-game.) Picture/portrait palettes also can't touch the
reserved band: `l6e58` clamps `start ≥ 16` (boot.c:59108), matching the Mac's
0..15 reservation.

The render path is also faithful, verified against the Mac disasm:
- `jt891 → jt94(0, 24, width=7, 0, prompt)` — the Mac caller (CODE 12 `0x2486`)
  pushes the **same** `width=7`.
- `jt94` row==24 (boot.c:6049) faithfully does `c==7→15` (white text, Mac
  `0x4034: moveq #15`) and `bg=8` (Mac `0x403c: moveq #8`). So **white text on
  an index-8 box is exactly what the Mac does** — making it black-on-light-grey
  would be a deliberate DOS-style divergence, not a faithful fix.
- `jt94` does **not** fill its own box: `bg=8` is the transparent case (`jt1089`
  only PaintRects when `(bg==15||fg==0)&&bg!=8`), and `l3f88` paints only thin
  edges. The grey backing comes from the **message-bar plate = FRAME.CTL item 4**,
  painted by **`jt176`** (boot.c:30455 — `jt1173` erase + `jt1001(8000,8000,1,4)`
  + `jt1193` + `l2062`), which is faithfully lifted and matches the Mac (L162e).

**Therefore "white on stone" = the FRAME item-4 plate is not covering the
row-24 prompt** in the inventory/trade context (the dialog is opened over the
character sheet, not the dungeon HUD, so the plate jt176 paints lands elsewhere
or is the wrong piece). The white text falls on the bare backdrop. This is a
**chrome/compose geometry** problem, not a palette one.

**Re-scoped fix (#147):** make `jt176`'s message-bar plate actually back the
row-24 prompt in every context it is invoked (inventory / trade / shop), so the
prompt sits on the grey plate as on the Mac. Needs a screenshot of the live
trade prompt to pin whether FRAME item 4 is mispositioned, the wrong sub-piece,
or absent. NOT a palette change — `jt1000`/`jt1006`/`l6e58`/the L4d98 resident
commit are all fine as-is.

---

## 2. Dialog keyboard (jt891 "can't type") — RUNTIME, not a leaf

`jt891` (boot.c:65015) is faithful: `jt1134()` flush → `key = jt60()` →
digit test `jt389` (b54231, body matches Mac `cmpib #48/#57`, correct) →
shifted-symbol remap. **No stub in its path.** The key never arrives, so the
bug is in the event pump:

    jt60 → jt1133 (spin while jt1118()==0) → jt1118 → l731e(3) → l725c →
    WaitNextEvent → kb_to_event → plat_kb_poll (BIOS Bconstat/Bconin) → l6dd0
    (sets -818 key / -820 pending)

Suspect: `jt1134()` at the top of the loop pumps `l725c(0x8140)` every
iteration; 0x8140 excludes keyDown (0x08) so it shouldn't *return* a key, but
if the pump drains/consumes the BIOS key before `jt60` runs, the key is lost.
**Needs instrumentation (log plat_kb_poll vs l6dd0 vs jt60 return), not a
lift.** The dungeon reads keys on a *separate* direct path (port_play_demo
~b15048) which is why movement works and dialogs don't.

---

## 2b. Roster arrow-nav garbage = SOLVED: an unclean crash (shape-7 wild jsr) (2026-06-28)

**ROOT CAUSE FOUND + FIXED.** The garbage was screen residue from an
**intermittent unclean crash**, not a display-compose bug (the user's tip —
"the garbage is usually caused by an unclean crash to the desktop" — was the
key). Hatari `--trace cpu_exception` caught it: `cpu exception 11 ... currpc 14
... op 4e90` — a wild `jsr (a0)` to address **0x14**.

Chain: l0aae appends the Hall "page-switch" DLItem with `jt452(7L, 20L, 0L)` —
shape 7 stores `rec[4] = 20 (= 0x14)` as a *"non-NULL marker"* for the page-flip
callback. But `jt376` (the shape-7 handler) on `cmd==5` (Phase 5's accelerator
walk) does `proc = rec[4]; if (proc != 0) ((fn)proc)(idx,a)` — so it **calls**
0x14 → wild jsr → crash. Intermittent because Phase 5 only reaches that item
(index 12) when no earlier item is hit and an accelerator/Return key is live.

**Fix (boot.c l0aae):** pass `jt452(7L, 0L, 0L)` — NULL, not 20. `jt376` is
built to fall through to `l1676` (the faithful no-proc default) when
`rec[4]==0`. Verified: 3× heavy-nav stress runs (9 keys each) → 0 wild-jsr
exceptions, clean roster, nav works (highlight moves). The real Mac passes an
actual page-flip proc here; lifting it is a separate enhancement (multi-page
rosters) — NULL is correct for the port's single-page roster.

Below is the earlier (display-layer) investigation, retained because it PROVED
the engine render is clean (which is why the crash, not a compose bug, was the
answer):

### Earlier: ruled out the display/compose layer

The Training Hall roster "wrong colours above Barbarus / below Stranilla" (a
band of ~4 cyan-bordered boxes of RGB static over the top rows) appears **only
when an arrow key actually moves the selection** (`l0848` → jt918 re-render).
The initial roster and any frame where the arrow does *not* navigate are clean.

Traced end-to-end with the harness (see §5 for how to drive arrows headless):
- **The chunky surface is 100 % clean.** A full vertical scan of
  `qd_screen_pixels()` at both l0aae's present and jt453's present on the
  post-nav frame returns index 8 (the grey plate) for y=8..72; only y=0..4 hold
  the normal FRAME chrome. So `l02dc`/`l0aae`/`jt76` paint **no** garbage — the
  engine lift is faithful.
- **The 16bpp display buffers are clean at present time.** Scanning all three
  `g_screen[0..2]` at jt453's present found only FRAME-chrome / plate-border
  pixels (no static). `Physbase` == `g_screen[g_disp]`, and a red-marker written
  into `g_screen[g_disp]` from the VBL handler shows up exactly where painted —
  so the addressing is right and g_disp **is** what VIDEL scans.
- **The garbage enters g_disp AFTER jt453's present**, during the l2d3e input
  spin, and persists (nothing re-covers that band). The only post-present writer
  is the VBL cursor handler, but the cursor sits at screen-centre (the harness
  warps the pointer to window-centre), not the top — so the exact write was not
  pinned. Smells like a triple-buffer / rapid-VsetScreen race on the nav's
  back-to-back presents (l0aae **and** jt453 both present per loop iteration).

**Conclusion:** this is a **port display-compositor bug, not an engine-lift
bug** — squarely #144 (present-once / off-screen compose). The fix direction is
to compose the whole logical roster frame off-screen and present it **once** per
jt918 iteration (drop the redundant jt453 present for the non-modal Hall menu),
so a freshly-blitted buffer is always what gets latched. NOT a CLUT or chunky
fix. The two jt918 fixes already landed this session (remove the bare-backdrop
present in the loop; default `-27932` to the party head) cured the *first*
dark-grey flash; the arrow-nav band remains for #144.

## 3. True leaf stubs ON the dialog/list path (these DO need lifting)

| fn | boot.c | Mac sz | role | note |
|---|---|---|---|---|
| **jt138** | 52524 | 0x86 (C7) | list-item keystroke filter: loop `jt1118`/`jt1133`, arrows 338/339→`jt50`/`jt51` scroll, translate via JT[1133] | installed as DLItem method (b52712); scroll targets `l5ac2`/`l5ad8` are real |
| **jt139** | 52525 | 0x6e (C7) | same, plus stores the chosen key in `-12914` (ESC/`` ` ``) | the "Enter returns selection" variant |
| **jt140** | 21122 | 0xe4 (C7) | list column/row measure: divides text width by 4, walks the `-27928` party list via `jt1139` | list-layout helper |
| **l2062** | 30440 | 0x10 (C7=JT[174]) | sets dirty flags `-12911`/`-12912`=1 (commit-deferred-paint) | **ALIAS TRAP** — see below |

**`l2062` alias trap (real bug, confident fix):** `l2062` (the PROBE stub at
b30440) and **`jt174`** (b5784) are the **same Mac function** — both are CODE 7
+ 0x2062 (the alias map confirms it). `jt174` is the faithful lift
(`g_a5_-12911 = g_a5_-12912 = 1`); `l2062` is a leftover duplicate stub. `jt176`
(and other sites) call `l2062()`, so the dirty-flag set is currently a **no-op**.
Per the CLAUDE.md `lXXXX`=`jtN` rule, the fix is to **forward** `l2062` to
`jt174` (`static void l2062(void) { jt174(); }`), not re-lift. This gates the
jt138/jt139 prompt-row content re-render (`l1bfe`) and the shape-5 button
installs, so it matters once those paths are live — but it is *not* the jt891
"stone box" (that's the plate, §1).

`jt50`/`jt51` (the page-scroll the filters call) are already real
(`l5ac2`/`l5ad8`, b19782/19786). So lifting jt138/jt139 closes keyboard nav for
list dialogs (roster, picker) without further deps.

---

## 4. Other true stubs (functionality gaps, NOT dialog-critical)

Real Mac bodies, currently `PROBE`-only. Listed largest-first; lift when their
subsystem comes up, not for the dialog work:

- **jt520** 0x3c0 (C14, →jt52) · **jt560** 0x29e (C17 char-gen — *harmless*,
  real path is l618c now) · **jt541** 0x224 (C14, record init: THAC0/AC via
  jt868) · **jt930** 0x156 (C12, leave-area cleanup) · jt1109 0x5e (C4) ·
  jt985 0x4e (C5) · jt1142 0x32 (C4) · jt68 0x26 (C6, yield/pump) · jt45 0x2e
  (C6) · jt1081 0x24 (C5).
- Tiny leaves (≤0x14): jt146, jt736, jt1152, jt1121, jt1137, jt512, jt859,
  jt920, jt956, jt1130.
- CODE-local `lXXXX` leaves (mostly editor / combat tail, off the dialog path):
  L4144 L10a0 L1176 L40b4 L1c92 L1cd2 L4f2c L4ff6 L15bc L157c l2aaa l2f24
  l329c l347a l0572 l0418 l035c l07e6 l0cb8 l0d86 l0e3e L0062 L038a L1e44
  L2d7e L06d6 L0bc6 L0df2 L1374.

Note: many functions still carry a `PROBE("…")` marker but have a **faithful
body** (e.g. jt389/jt408/jt420 ctype, jt527, l5ac2/l5ad8). They are NOT stubs —
the marker is just left in. Don't re-lift those; the 53 above are the genuine
empty/return-0 stubs.

---

## Recommended order

1. **#147 message-bar plate** (re-scoped — see §1) — make `jt176`'s FRAME item-4
   plate back the row-24 prompt in the inventory/trade/shop contexts. Needs a
   screenshot of the live trade prompt to pin the plate geometry. NOT a palette
   change (the CLUT is verified correct in-game).
2. **jt138/jt139** — small, self-contained list-dialog keystroke filters;
   restores keyboard nav in list dialogs. (`l2062` is NOT a stub — it is
   faithfully lifted at boot.c:5785, setting the `-12911`/`-12912` dirty flags
   the jt138/jt139 path reads.)
3. **jt891 keyboard** — runtime instrumentation pass (separate from lifting).
4. **#144 present-once** — stops the full-screen redraw on every list arrow AND
   the roster arrow-nav garbage band (§2b).

---

## 5. Headless harness: driving cursor ARROWS + capturing logs (2026-06-28)

Two gotchas that blocked reproducing the arrow-driven bugs headless, now solved:

- **`--conout 2` swallows the cursor arrows.** That redirect (how `dbg_log`
  reaches the host terminal) routes BIOS device 2 to the host, so the engine
  falls back to GEMDOS `Cconis`/`Crawcin`, which only surface keys *with an
  ASCII code*. Letters + Return arrive; the arrows (ascii==0, scancode only) do
  **not** — `plat_kb_poll` never sees them. Verified: a low-level dump of the
  raw key code fired for p/l/a/Return but never for Down.
- **Fix / workaround:** `FRUA_NO_CONOUT=1 tools/hatari_ui.sh start` drops
  `--conout 2`, so the engine reads keys via `Bconin(2)` and injected arrows
  reach `l2d3e`/`jt1125` (Down→scan 0x50→260→`l0848`). The screen stays clean
  (`dbg_log`'s `Cconws` lands on Logbase, not the displayed triple-buffer). It
  implies `READY_MARKER=-` (no engine markers in the log) and a longer warm-up
  (`READY_GRACE`, default 18s) so the whole boot finishes under fast-forward.
- **Logging WITHOUT `--conout 2`:** use `dbg_file_num(label, val)` (platform/
  dbglog.c) — it appends to `C:\DBG.LOG` (= `data/work/gamedata/DBG.LOG` on the
  host), truncated on the first call per run. The only debug-trace channel that
  survives no-conout. (`dbg_log_num` still works *with* `--conout 2`.)
- Mouse-button clicks still do NOT inject (only keyboard via XTEST). The driver
  warps the host pointer to window-centre before each key, so the in-game cursor
  sits at screen-centre during headless runs.
