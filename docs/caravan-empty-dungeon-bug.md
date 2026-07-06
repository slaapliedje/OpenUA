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

## Reproduction (needs a human at the mouse-gated menu)

Instrumented build: add `FRUA_XFER_TRACE` probes (this session's set,
reverted) or the `FRUA_SKIP_ENTRY_EVENTS` harness, then
`make run-game DSN=HEIRS.DSN EXTRA_CFLAGS="…"`; PLAY → Load save → Begin
Adventuring → walk to caravan → take reward → stand in the dungeon.
`DBG.LOG` lands in `data/work/gamedata/DBG.LOG`. The Falcon display can be
screenshotted off the live `:0` (import -window root + XAUTHORITY from the
hatari PID's /proc environ).
