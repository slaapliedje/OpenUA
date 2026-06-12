# Stub inventory + campaign audit — 2026-06-12

A triage of every PROBE-only stub left in `src/engine/boot.c` after the
band-2/3/4 campaigns, plus the `port_*` stand-in debt and the trap
list. Supersedes the 2026-06-10 inventory (86 jtN stubs then; 41 now,
plus 25 CODE-local leaf stubs). Regenerate with the trivial-body
classifier (PROBE-only bodies, <= 6 statements).

## Band coverage (tools/jt_progress.py, 2026-06-12)

| band (rank) | done | open | note |
|---|---|---|---|
| 1 (1-100)    | 99/100 | jt1177 | HAL row-blit, by design |
| 2 (101-200)  | 100/100 | — | COMPLETE |
| 3 (201-300)  | 99/100 | jt947 | = l709e, closes via #115 |
| 4 (301-400)  | 90/100 | 10 | see band4-wall.md |
| 5 (401-500)  | 40/100 | 60 | next campaign candidate |
| 6 (501-600)  | 29/100 | 71 | |
| 7 (601-700)  | 26/100 | 74 | |
| 8 (701-800)  | 34/100 | 66 | |
| 9 (801-900)  | 4/100  | 96 | the CODE 18 effect-handler table band |
| 10 (901-1000)| 95/100 | 5  | mostly Mac trap glue = shim |
| 11 (1001-1100)| 60/100 | 40 | |
| 12 (1101-1208)| 14/100 | 86 | tail; many editor-only |

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
