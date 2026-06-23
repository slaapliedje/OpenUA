# Stub inventory + campaign audit — 2026-06-23 (refresh)

## Snapshot 2026-06-23 (regen `tools/jt_progress.py`)

1205 JT entries: **845 done** (761 lifted, 20 noop, 64 alias), **142 stub**,
**218 missing**, 0 stand-in → **360 pending (29.9%)**. Top-100 fully lifted.
Live chunk / per-CODE-segment / leaf-stub tables are in
[`docs/jt-lift-progress.md`] (the tool output — trust it for counts).

Biggest pending blocks by subsystem (per-CODE table):
- **CODE 16 combat handler tier — 82** (81 stub): spell-effect / per-actor
  handlers; the deepest block, but NOT play-reachable until the combat exec
  loop (`l076e`, still stub) lands. Not the next target.
- **CODE 4 display — 63** (mostly SUPERSEDED by the VIDEL HAL — most need no
  lift), **CODE 5 core runtime — 51**, **CODE 3 Toolbox — 25**, **CODE 7
  list/text widgets — 24**, **CODE 14 area-map — 19**, **CODE 8 UI/file — 17**.
- **CODE 2 / 11 design editor — 9 / 6**: AUTHORING, not the play path (defer).

## Play-path priorities (what actually blocks PLAYING)

1. **EVENT SUBSYSTEM (l709e per-type arms) — the top play-path gap, and the
   root of "events fire but don't load the PIC / don't display".** The l709e
   dispatcher is lifted but ~18 of its event-type arms are still PROBE-only
   stubs: `l159a`(1 text), `l4d26`(2), `l1f76`(4), `l2d32`(6), `l4f9a`(7),
   `l2e42`(12), `l380a`(13), `l1ad8`(15), `l6020`(16), `l3ac6`(17),
   `l3cd6`(18/19/20), `l5bde`(22), `l2b2a`(26), `l398a`(29), `l38bc`(32),
   `l6436`(35) + `l3328`/`l3fba`/`l364e`/`l29cc` helpers. Already LIFTED:
   `l28b0`(3 treasure), `l5676`(5/11 stairs), `l216a`(9), `l5586`(8 shop),
   `l3b0e`/`l673e`(10/21 combat-entry). Pairs with CODE 10 (PIC/SPRIT/CPIC
   display, 8 pending) for the event pictures. THIS would also make the test
   harness's in-game navigation reliable (events currently no-op + mis-redraw).
2. **INVENTORY (CODE 9, 3 pending + the UI)** — the deferred inventory task:
   equipped-item display + ITEMS/TRADE/DROP (docs/inventory-subsystem-wall.md).
3. **port_* play-loop stand-ins** -> the faithful CODE 15-19 chain
   (port_run_encounter / port_rest / port_play_message / port_begin_adventure).

## Open render threads (harness-blocked, parked 2026-06-23)

- Fireplace colour cycle: subsystem works end-to-end (B.0 6e43c86 / B.1a dfdc2c3
  / B.1b+B.2 2863b8a — installs, stays stable, jt1067 rotates, no regression),
  but the on-screen fire does NOT animate — the facet-4 fireplace likely draws
  via `cw_shade` (boot.c:9539) not `l309c_tile`, so the cycle band 97..102 never
  reaches it. NEXT: instrument cw_shade for fire bytes 60..65. See
  dungeon-3d-stack-audit.
- Coord off-by-one (#124): Mac 14,7,N == port 14,8,N (port 2nd coord / row +1).

---

# Stub inventory + campaign audit — 2026-06-15 (earlier)

> **The canonical, always-current audit is the auto-generated
> [`docs/jt-lift-progress.md`](jt-lift-progress.md)** (run `tools/jt_progress.py`).
> As of 2026-06-19 it carries: a **50-entry chunk** progress table, a **Coverage
> by CODE segment** table (_what's used where_ — which subsystem each pending
> block belongs to), the **local lXXXX leaf-stub** list, and the **stand-in**
> register. This file keeps the hand-written triage / trap-list narrative below;
> trust the tool for counts.

A triage of the remaining stubs/missing in `src/engine/boot.c`, plus the
`port_*` stand-in debt and the trap list. Regenerate the numbers with
`tools/jt_progress.py` (status classifier over the 1205 JT entries).

## GLOBAL TOTALS (tools/jt_progress.py, 2026-06-15) — 1205 JT entries

| status   | count | % |
|---|---|---|
| LIFTED   | 715 | 59.3% |
| NOOP     |  19 | |
| ALIAS    |  52 | |
| **implemented (lifted+noop+alias)** | **786** | **65.2%** |
| STAND-IN |   0 | (jt114 + jt118 RESOLVED 2026-06-15 — were stale tags) |
| STUB     | 139 | |
| MISSING  | 280 | |
| **remaining (stub+missing+standin)** | **419** | **34.8%** |

UPDATE 2026-06-15: lifted 16 CODE-16 combat status-announce handlers (the
`l6114(target,0,0,0,0,msg)` family — jt606/609/624/625/632/634/654/656/660/662/
685/689/692/694/706/707). Also fixed a `tools/jt_progress.py` parser defect: an
inline `/* +0xNNNN; id NN */` comment carries a `;` that the body/forward-decl
scanner read as a forward-decl terminator, so those 16 already-lifted handlers
classified MISSING. Now comment-aware; LIFTED 699→715, band 9 4→18, band 6
33→35. The combat main loop l076e (~2.2KB) is still STUB, so the handlers are
not yet runtime-reachable — breadth-first lifts, untested at runtime.

NOTE: the only two JT stand-ins are now retired. jt114 (wall-tile blit) routes
through l309c -> l2d4e (raw tile byte = direct CLUT index, 255 transparent — the
faithful Mac model, NO band-rebase; the rebase only ever lived in l309c_tile,
now used by the item-portrait blits). jt118 = jt108(1)[=L38d0(1)] + jt114[~=the
Mac jt1001 blit] = the 1:1 Mac jt118. The binder-model walls work + the earlier
l309c switch already made both faithful; the STANDIN annotations were stale. The
garbled wall TILES are the separate piece-data puzzle (dungeon-3d-render-state),
not the blit.

## Band coverage (tools/jt_progress.py, 2026-06-15)

| band (rank) | done | stub | missing | note |
|---|---|---|---|---|
| 1 (1-100)    | 99/100 | 0 | 0 | jt1177 HAL row-blit (by design) |
| 2 (101-200)  | 100/100 | 0 | 0 | COMPLETE |
| 3 (201-300)  | 99/100 | 0 | 1 | jt947 = l709e (closes via #115) |
| 4 (301-400)  | 98/100 | 1 | 0 | band-4 campaign DONE (#119) |
| 5 (401-500)  | 100/100 | 0 | 0 | band-5 campaign DONE (#120) |
| 6 (501-600)  | 35/100 | 20 | 45 | NEXT campaign candidate |
| 7 (601-700)  | 29/100 | 7 | 64 | |
| 8 (701-800)  | 36/100 | 6 | 58 | |
| 9 (801-900)  | 18/100 | 76 | 6 | CODE 16/18 combat effect-handler band (#115); 16 status-announce handlers lifted 2026-06-15 |
| 10 (901-1000)| 95/100 | 5 | 0 | mostly Mac trap glue = shim |
| 11 (1001-1100)| 60/100 | 8 | 32 | |
| 12 (1101-1205)| 14/105 | 16 | 70 | tail; many editor-only |

DONE this session (not yet folded into a band campaign): RM #127 (FC pool
sizing/audit/dedup + walls routing via the binder model — l6eea/jt200_layer/
jt115 are now binder-correct) and the DISPLAY rearchitecture (16bpp LUT backend
+ 030 asm blit + VBL triple-buffer — fixed the input lag). Bands 1-5 + 10 are
effectively closed; the bulk of the remaining 439 is bands 6/7/8/9/11/12, with
band 9 (the CODE 18 combat exec tier, 90 stubs) the largest single block.

## 1. Faithful AS stubs — DO NOT lift

jt1/jt2/jt3 (THINK C inline-switch family — every call site reads its
own table), jt1163 (returns 0), jt1198 (returns 1), jt1170 (empty),
jt1061 (_SwapMMUMode — no-op on the 030), jt1177 (HAL row-blit; needs
the display backend, not a transcription), jt1081 (the 9-call teardown
chain runs only at fatal exit where jt415 exits immediately after).

## 2. Open jtN PROBE stubs by subsystem (34 genuine)

- **Combat/#115:** jt546 (CODE 14+0x4186, 568B ray-target picker),
  jt555 (CODE 14+0x19dc, 816B), jt502 (projectile trail), jt321,
  jt631, jt744/jt746/jt748/jt771/jt774/jt785 (CODE 18 effect handlers
  — the band-9 table; lift with #115's handler sweep).
- **Audio (.slb music engine, own session):** jt965, jt974.
- **TLB cache save path:** jt1022, jt1023, jt1024 (LBCreate — ALSO
  releases the jt1021 PORT GUARD; delete the guard in the same
  commit), jt1044, jt1050 (NewGWorld page descriptors).
- **UI/input:** jt1078 (CODE 5+0x440 modal line editor — jt98's box
  draws, editing pends), jt1123 (CODE 4+0x659a colour-cursor install
  — task #107's hook), jt1150 (dirty-rect mark), jt126/jt220 (file/
  icon load callbacks), jt203 (3D repaint prologue), jt1064.
- **CODE 4 system tier:** jt1115, jt1181, jt1183, jt1184, jt1188,
  jt1189, jt1191.
- **Band 4 leftovers:** jt79 (CODE 6+0x69f8, 226B), jt932 (CODE
  12+0x45ca, 300B), jt1076 (= l7ab4, the message paginator — callers
  jt1072/jt1074 lifted), jt77 (CODE 6+0x6920, 442B), jt184/jt196
  (CODE 7), jt279 (CODE 22+0x423e, 498B).

## 3. CODE-local leaf stubs (25)

l0004 (menu-sel), l0004_c6 (name char append), l005a, l006c, l035e
(file-group mode), l17ca_c22 + l2180 (slot-row repaint helpers),
l24aa, l2d78 (readied-item side effects ~500B), l341a (SFPutFile — no
GEMDOS equivalent), l3792 (cell->pixel), l3b1e (GLIB piece release —
jt55/jt56 callers), l3d8c, l4350, l48f4 (combat cast announce),
l4dee, l4faa, l59c2, l6114 (effect announcer — many CODE 16 handler
callers), l62e0 (monster-art bind), l6804, l7490 (icon load), l7a0e
(page spill), l7ab4 (paginator core = jt1076), l7de0.

## 4. port_* stand-in debt (24 symbols)

Load-bearing (replace with faithful lifts eventually):
port_draw_play_frame (the #114 HUD chrome over-blit — the "jank"),
port_show_intro (faithful title sequence equivalent, trace-matched),
port_load_savgame, port_always_load / port_frame_load /
port_menu_load / port_ui_group_base (GLIB bootstrap wiring),
port_hud_text_clut, port_play_message, port_rest, port_run_encounter,
port_begin_adventure, port_render_geo_* / port_render_topview (area
map), port_menu_bar.

Demo leftovers (delete when their harness blocks retire):
port_blit_demo, port_play_demo, port_sprite_demo, port_view_demo,
port_wall_demo, port_l6234_verify, port_test_seed_design.

## 5. Trap list (verified findings — check before lifting near them)

- **jt1021 is GUARDED** until jt1024 (LBCreate) lifts — un-guarding
  corrupts live GLIB groups at boot (bisected 2026-06-12).
- **Mac CODE 6 jt114/jt118/jt121 and jt200/jt999 take (h, v)** —
  h-first; the port's jt114/jt118/jt200 are 5-arg page variants with
  (page, top, left, ...); map per the l5b42 convention note. The
  3dview-trace doc's "(top, left)" labels are the BII hook's printf
  text, not the C param order.
- Name collisions (the lXXXX prefix is CODE-local): l5baa = CODE 7
  (not jt56's), l2f74 = CODE 17 (not jt35's), l6520 = CODE 14
  (CODE 8's is l6520_c8), l0004 = non-CODE-6 (CODE 6's is l0004_c6),
  l17ca = (CODE 22's is l17ca_c22), l0062/jt1081 same address,
  l7ab4 = jt1076 same address, jt1032 = CODE 5+0x506e vs the menu
  header CODE 22+0x506e.
- jt406's port spelling is (dst, src, n); the Mac ABI is (src, dst,
  n) — derive direction from the asm at every new call site.
- The port jt118 ≠ Mac jt118 (render-path variant): inline jt108 +
  jt1001 instead (the jt57/jt353 precedent).
