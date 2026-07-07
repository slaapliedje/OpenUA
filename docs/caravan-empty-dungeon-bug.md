# BUG: HEIRS caravan → "empty" dungeon (walls blit but invisible + can't walk)

**Status:** OPEN, well-narrowed (2026-07-06). Two distinct bugs on the
HEIRS "Load save → Begin Adventuring → intro caravan → take reward"
path. Diagnosed live with a `FRUA_XFER_TRACE` DBG.LOG probe set (since
reverted) + a Basilisk (Mac) ground-truth blit trace supplied by the user.

## Symptom

After taking the caravan reward, the party is in HEIRS **DUNGEON 01
(GEO level 5, 19×19)**. The 3D view shows the **night-sky backdrop +
receding stone floor but NO WALLS**, and the party **cannot walk**
(arrow keys do nothing). The user reports the Mac (Basilisk) flashes the
corridor walls *before* the caravan event fires, then unloads the event —
i.e. the walls should be visible, and the breakage is present *before* the
event even matters.

## What is PROVEN (DBG.LOG + Basilisk diff)

1. **Area/data is valid.** `jt198` loads level 5, `GEO bytes=12962`
   (correct), dims 19×19, wall zone bytes `ds[4/5/6] = 5/8/1`;
   `cells_with_walls = 245/361`. Not empty, not a wrong/half-loaded area.
2. **It is normal in-play mode** (`g_a5_27990 == 4`), NOT the editor. The
   CODE 22 editor dispatcher `l0096` is never called (DCE'd). "Editor
   cross-talk" is disproven; the "MARK CELL WALL / bars-on-top" screen was
   the **caravan event display**, and the game *does* return to the clean
   play HUD (real roster: BARBARUS/LADY ILLIS/… with AC/HP, pos 10,8).
3. **The walls BLIT CORRECTLY.** In `jt200_layer` (boot.c ~10843, the live
   path `render_3d_faithful → jt199 → jt200 → jt200_layer → jt114 →
   l309c_tile`): `binder0`, `base=jt468(binder[0])`, and
   `sub=l37aa(base,…)` are ALL non-zero, so `jt114` IS called. The tile
   indices emitted (**Wall2/grp 1: idx 1/2/30**) **match the Basilisk
   ground-truth JT200 trace cell-for-cell** (`code=6/9 … JT114 idx=1/2/30`).
   So tile selection, wall-set load, binder, and geometry are all correct.
4. ⇒ **The walls are drawn but INVISIBLE.** Narrowed to exactly two
   candidates:
   - the **wall CLUT band** (32/64/96) is wrong/black (the `load_wall_groups`
     / `cw_finalize` / `cw_load_slot` palette install), or
   - the **blit coordinates** (l309c_tile → `jt1135`) land off the 88×88
     view hole (VL/VT/VR/VB = 24/24/112/112) and get clipped.
5. **Second, separate bug — the walk loop stalls.** The render dispatch
   (boot.c ~3117, `jt221`) fires **once at entry** and never again; two
   up-arrows left the party at **10,8** with no re-render. So the
   interactive exploration loop does not resume after the caravan's `l709e`
   event dispatch → can't walk, and the view is a frozen entry frame.

## Wrong turns (ruled OUT — do not re-chase)

- **`jt357` / `jt993(jt1004())` palette-clobber.** `jt357` (CODE 8+0x76aa)
  contains `jt993(jt1004())` for Wall3, which looked like it slammed a
  stale 224-colour picture palette over CLUT 32..255. But **`jt357` is
  never called in the 3D dungeon render** (`J357 dumps = 0`) — it serves
  other views. A "fix" there was a no-op (reverted).
- **The `EVPIC pal start=32 count=224` storm.** That is `jt993` from the
  **merchant reward dialog** (event picture palette cycling) — a red
  herring for the walls; it fires while the merchant is shown and stops on
  return to the dungeon.
- **The transition / area load** (l5676): there is NO transfer — the
  caravan events (l709e types 2/3/4; reward = type 3 `l28b0`) fire in place
  in level 5. Not a stairs/transfer bug.
- **The `8x8db3001.ctl` "have to clip" warnings** are Hatari GEMDOS 8.3
  noise; the port intentionally probes a per-set override that never
  exists and falls back to `8X8DB.CTL`. NOT the cause; long-filename
  support (MiNT/MagiC/EmuTOS) is irrelevant here.

## Regression hypothesis (the audit target)

The walls rendered at some point ([[dungeon-walk-live]] #124,
[[dungeon-3d-render-state]] "SOLVED"). Since the tiles now blit correctly
but paint invisibly, suspect a **replaced/regressed function in the wall
CLUT-band install or the blit-coord transform** — the partial-lift /
accidental-repoint class of bug. Audit the render/palette path by git
history + against the Mac:
- Palette: `load_wall_groups`, `cw_finalize`, `cw_load_slot`,
  `l309c_tile`'s 32-based rebase into bands 32/64/96, `g_wallfile_buf`
  zeroed-palette handling (boot.c:11564).
- Coord: `jt1135` (scale/hole transform — [[dungeon-3d-transform-mon-verified]]),
  `l309c_tile(page,left,top,…)` swap, `cw_view_clip`.
- **Config-specificity:** this level has a **night-sky "town/outdoor"
  backdrop** (`cell_backdrop_id → load_backdrop`, band 144..175) and
  **Wall3 present (`ds[6]=1`)**. #124's working levels may have been
  indoor / no-Wall3 — so this may be an *incomplete* outdoor/Wall3 path,
  not a pure regression. Check whether an INDOOR HEIRS dungeon level (no
  night-sky, `ds[6]==0`) still renders walls.

## Next concrete steps

1. **Blit-coord probe** (1 run): log `l309c_tile`'s `jt1135` screen (x,y)
   per tile; diff `top=/left=` against the Basilisk trace. Splits
   coord-vs-palette immediately.
2. **CLUT-band probe:** sample CLUT bands 32/64/96 after
   `load_wall_groups` (needs a small getter into `compat/quickdraw.c`'s
   `g_palette[256]`) — is the Wall2 band 64 black?
3. **Existing harness:** `make … EXTRA_CFLAGS="-DFRUA_SKIP_ENTRY_EVENTS
   -DFRUA_ENTRY_LEVEL=5 -DFRUA_ENTRY_COL=10 -DFRUA_ENTRY_ROW=8"` dumps
   `J200DIFF.TXT` for a slot-by-slot diff vs `docs/mac-blit-trace-heirs-l5.md`
   — built for exactly this (note: skips the caravan, tests the level in
   isolation → also answers the regression-vs-incomplete question).
4. **Walk-loop stall:** trace why the `l709e`/event dispatch does not return
   to the exploration/movement loop (jt953 / l63c0) after the caravan.

## AUDIT VERDICT (2026-07-06) — it is the wall-PALETTE model, not a coord bug and not a deleted function

Root cause: **the port fabricates a separate per-set 37-entry CLUT band for
each wall set and rebases tile bytes into it, but the Mac uses ONE shared
dungeon palette that every set indexes directly.** Live constants (the
"32/64/96" comments are stale): `g_cw_base[CW_SLOTS] = {32, 69, 106}`,
`CW_BAND = 37` (boot.c:9454/9443), backdrop `BACK_PAL_BASE = 144`.

- `l309c_tile` rebase (boot.c:10585-10612): `off = v - 32; if (0 <= off <
  CW_BAND) v = base + off; else v written UNCHANGED`.
- **Wall1 = set5 STONE, slot 0, base 32** → rebase is the *identity*, and
  set5's bytes (41..60) sit inside the band → renders ~OK. This is why
  stone-dominated levels (#124, [[dungeon-3d-render-state]]) looked fine.
- **Wall2 = set8 WOOD, slot 1, base 69** → set8's tile bytes span **0..53**;
  the low bytes (0..31) hit `off < 0` and are written **UNCHANGED into CLUT
  0..31 (the UI/chrome band)**, the rest read set8's item-0 (not the shared
  palette) → wood walls paint in chrome/near-black indices = **invisible**,
  doubly so over the dark night sky.
- **Wall3 = set1, slot 2, base 106** → same class of failure.

So it is intrinsically per-set / config-specific (Wall2/Wall3 wood + night
sky), NOT a clean regression — matching this doc's own hedge. Corroborated by
`docs/dungeon-view-wall.md:186-197` (shared overlapping palette; "the per-set
32-band rebase can't serve them") and `docs/dungeon-3d-stack-audit.md` Card 1
("the faithful l3f3c/jt1069/jt1066 palette path is DEAD for the 3D view … the
wood-vs-stone / wrong-colour root").

**Mechanism inside it:** `cw_load_slot` (boot.c:9743-9758) treats sub-GLIB
item-0 as a self-contained 37-colour RGB palette and **zero-fills entries past
`pe` = BLACK**; `pe` also over-counts because item-0 now includes trailing
colour-cycle records (added by `2863b8a`) read as RGB garbage.

**Closest thing to a genuine regression (secondary):** `63bbf75`'s
clobber-recovery sets `g_wallfile_which = -1`, but `cw_wallfile_load`
(boot.c:2508) only consults it in the **fallback resident-buffer path**
(:2571); the live **FC-pool path** (:2517-2563) ignores it → the "re-read the
wall library after a CLUT clobber" recovery is a **no-op post far-pool-stage4**.

**RULED OUT:** the coordinate path (Mac traces confirm faithful 8000-space
placement; `dungeon-3d-stack-audit` Card 2 RESOLVED; `g_cwf_ox/oy` unused *by
design* per `8b4418f`). And `90b7bd9`'s 16 deleted stand-ins are all
0-live-reference retired-software-renderer code — no live wall-path function
was wrongly deleted.

**FIX DIRECTION (known + RISKY):** retire the per-set bands; load the Mac's
single shared dungeon palette into CLUT 32+ (the `l3f3c` / GLIB path currently
gated off behind `g_dungeon_bigpic_overlay = 0`) and blit tile bytes DIRECT (no
rebase). This is `dungeon-view-wall.md` Card 1 — and note the standing warning
`dungeon-view-wall.md:137`: "do NOT touch the palette, it regressed badly last
time." So this is a deliberate, well-tested rework, not a quick patch.

**Confirm-probe:** in `l309c_tile` for `g_cwf_slot == 1` (Wall2) blits, log the
raw tile byte `v` and the final written value — any `v < 32` (falling into the
UI band) or a near-black final value confirms. The separate walk-loop stall is
still open and untouched by this audit.

## FIX APPLIED (2026-07-07) — the colour-cycle install was the regression

**Root cause (corrected — the per-set palette model is RIGHT, see
[[wall-palette-per-set-is-correct]]):** the dungeon wall fireplace/torch
**colour-cycle** (Card B.1b/B.2, commit `2863b8a`) — which never fully worked —
regressed the walls invisible.  `cw_finalize`'s `jt1069` install seeds the `-3258`
cycle entries + the `-3394` work buffer; the per-frame **`jt1067`** rotation then
commits its `[min..max]` range straight to the hardware CLUT (`l6e58`), overwriting
the per-set wall bands (`g_cw_base = {32,69,106}`) with rotated/garbage colour every
frame → walls blit correctly but paint near-black, worst over the night sky.  The
static `qd_set_palette` install (boot.c:9819) is correct and gives visible walls on
its own.

**Fix:** gate the colour-cycle install OFF — `static int g_cw_wall_cycle = 0;`
around the `jt1069` block in `cw_finalize`.  `jt1067` then never touches the wall
bands; walls render from the correct static per-set palette.  Reversible (flip to 1
once the cycle path is genuinely fixed).  NOT a shared-palette rewrite (that earlier
"audit verdict" was a wrong turn — struck below).

**⚠ Needs VISUAL confirmation** (dungeon entry is mouse-gated, can't verify
headless): boot the level-5 harness (`FRUA_SKIP_ENTRY_EVENTS`/`FRUA_ENTRY_LEVEL=5`,
HEIRS staged), PLAY → Load save → walk to a wall — walls should now be visible.

The "shared-palette / per-set-is-wrong" audit-verdict section below is SUPERSEDED
(kept only for the ruled-out trail).

## "What you broke" reconciled (2026-07-07) — deliberate design + a never-run path

Not an accidental regression.  Two threads, both confirmed against the current tree
and git history:

1. **The per-set CLUT-band model is a deliberate design**, introduced by `cac2002`
   ("per-set wall palette (CTL item 0 @ clut[32]) — kills candy cast"), extended by
   `f5aef40` (per-edge sets, `g_cw_base = {32,64,96}`) and retuned to `{32,69,106}`
   by `2863b8a`.  `l309c_tile` (boot.c:10585-10617) rebases `off = v-32`; for the
   WOOD sets (Wall2 = set8, bytes 0..53) the low bytes `v<32` fall through UNCHANGED
   into CLUT 0..31 (the UI/chrome band) → invisible, worst over the night sky.  This
   is the audit-verdict root cause, still live.
2. **The faithful Mac shared-palette path was lifted but NEVER RUN.**  `l58c4`
   (boot.c:2894, the backdrop overlay that installs the dungeon picture palette via
   `l3f3c(32,255)` = JT[105]) is gated by `g_dungeon_bigpic_overlay`, which was born
   `0` in `ddecbb8` and has never been flipped.  So the shared-palette CLUT model has
   literally never executed → the Wall2/Wall3-wood + night-sky config never worked.
   **Incomplete path, not a break** — matches the audit hedge.

**FIX = enable the faithful path, carefully:** turn on the shared dungeon palette
(the `l58c4`/`l3f3c` install) and blit tile bytes DIRECT (drop the `l309c_tile`
rebase), retiring the per-set bands — per the standing `dungeon-view-wall.md` Card 1.
RISKY (the "palette regressed badly last time" warning).  **Do the low-risk
confirm-probe FIRST** (below) to capture the exact band/coord ground truth before the
rework; HEIRS.DSN is staged and the `FRUA_SKIP_ENTRY_EVENTS`/`J200DIFF` level-5
harness runs headless.

## Reproduction (needs a human at the mouse-gated menu)

Instrumented build: add `FRUA_XFER_TRACE` probes (this session's set,
reverted) or the `FRUA_SKIP_ENTRY_EVENTS` harness, then
`make run-game DSN=HEIRS.DSN EXTRA_CFLAGS="…"`; PLAY → Load save → Begin
Adventuring → walk to caravan → take reward → stand in the dungeon.
`DBG.LOG` lands in `data/work/gamedata/DBG.LOG`. The Falcon display can be
screenshotted off the live `:0` (import -window root + XAUTHORITY from the
hatari PID's /proc environ).
