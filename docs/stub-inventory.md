# Remaining stub frontier — full triage (2026-07-04, post-jt592)

> **2026-07-12 — run `python3 tools/stub_audit.py` before trusting anything below.**
> It parses every function in `boot.c`, classifies the body, and reconciles it
> against what the header comment CLAIMS. As of the sweep: **2127 functions, 90
> genuine PROBE stubs, 0 stale claims.**
>
> The sweep existed because the comments had rotted badly: 47 header comments
> still described functions as "PROBE stub pending its own lift" that had been
> fully lifted, some of them months earlier. Three separate sessions lost time
> hunting for work that did not exist — `l3ac6`/`l40b4` (never stubs; `l40b4`
> is not even an audio function), `jt985`/`l11a2` (lifted; the table said STUB),
> and the whole `l076e`/`l08b4` combat chain (fully playable from the keyboard
> while its header said the handlers "land as PROBE stubs").
>
> `tests/test_stub_audit.py` now fails the build if a stale claim reappears, so
> **lifting a stub means updating the comment that describes it.** The numbers in
> this file below are from 2026-07-04 and are NOT maintained; the tool is.

After jt954 + jt592, the scoreboard reads **1134/1205 done, 14 stub +
57 missing**. This pass individually re-scouted **all 14 remaining
stubs** (asm body + port caller + deps) to answer one question: *is any
of them a clean, isolated, live, screenshot-validatable gap-fix like
jt954/jt592 were?* Answer: **no — the clean live-stub frontier is
exhausted.** Every one is blocked by at least one hard barrier below.
Recorded so the next session doesn't re-derive it. (`--wiring` lists
these by asm weight; sizes are label→rts in `data/work/disasm`.)

| stub | CODE | port | disposition — why it is NOT a clean live gap-fix |
|---|---|---|---|
| jt933 | 12 | 3 | **Modal menu loop** — builds items via JT[155], selects via JT[160] (mouse), 9-arm JT[3] dispatch, loops until "exit". Mouse-gated + non-HEIRS. UNVALIDATABLE headless. |
| jt931 | 12 | 1 | **Rule-book copy-protection challenge** menu (already documented below: unreachable once the slot-3 "Boots" marker seeds). Same menu family. |
| jt919 | 12 | 1 | Same CODE 12 modal-menu family (JT[155]/JT[160] driven). Mouse-gated. |
| jt1081 | 5 | 1 | **Global teardown chain**, reached ONLY on the jt69 fatal-exit path; jt415/ExitToShell runs immediately after, so the per-subsystem releases are MOOT on the port. 5 of its 9 deps (l27bc, jt1156, l01ac, jt1119, l0f14) are still MISSING. |
| jt1064 | 5 | 1 | **Toolbox A-trap glue** (`.short 0xaded`/`0xade7` = Menu/Window Mgr traps, computed `bsr` dispatch). NOT clean C — same class as jt1059=l5e30. Port handles window-content hit-test via its own event system. |
| jt955 | 18 | 0 | 2-instruction leaf, **port=0** — nothing in the port calls it. DEAD. |
| jt985 | 5 | 0 | "Song out of range (%d/%d)"-checked music-play; **port=0**. DEAD (audio anyway). |
| jt974 | 5 | 0 | **port=0**. DEAD. |
| jt1008 | 5 | 2 | jt10's Pod-UI key sink — but its only caller jt10 is `__attribute__((unused))` (jt989 dispatch ABI not wired) AND dep L0ab6 is MISSING. UNREACHABLE. |
| jt965 | 5 | 1 | **Sound/voice engine** (guarded by jt1154 sound-enabled). The DMA-sound HAL is stubbed; audio, so unvalidatable via screenshot. HAL-BLOCKED. |
| jt587 | 15 | 1 | Party-add 398-byte record init (`jt399` clear → JT[3] case-1 `L08ba` → jt21 + jt910). But the **caller reimplements party-add via the `cg_pool` stand-in**, so a faithful jt587 is unwired without a #141-style migration; dep L08ba is MISSING; copy-direction is corruption-risky. STAND-IN-ENTANGLED — follow-up to #141, not an isolated fill. |
| jt365 | 8 | 1 | **Monster-art loader** — needs the CODE 8 art pipeline (l62e0 bind etc.). GRAPHICAL/ART subsystem. |
| jt428 | 3 | 1 | Text print — needs the console/HAL path. HAL-BLOCKED. |
| jt1144 | 4 | 1 | **Mac Toolbox app-init** (`_InitGraf`/`_InitWindows`/`_InitMenus`/`_TEInit`/`_InitDialogs`/`_FlushEvents`). The port's compat shim + boot sequence IS the analog — there is no Toolbox to init. NOOP-class (like jt9's trap-unpatch), not a pending body; candidate to move stub→noop in `jt_progress.py`. |

**The barriers, grouped:** interactive/mouse-gated menu loops
(jt933/931/919) can be *rendered + screenshotted* but not *clicked
through* headless — validation needs the user's mouse (see #144 note).
Audio/HAL (jt965/jt428) and monster-art (jt365) need subsystem/HAL
build-out first. jt1081 is moot (exits after) with missing deps;
jt1064/jt1144 are Toolbox glue with no faithful C body; jt955/985/974
are dead (port=0); jt1008 is unreachable; jt587 is stand-in-entangled.

**So jt954 and jt592 were the last isolated clean live-stub lifts.**
Forward paths from here are (a) the mouse-gated menus, lifted *with the
user driving Hatari to click through them* (jt933 party menu is the
highest-value), or (b) the MISSING subsystem chains — jt426 design-LOAD
parser, CODE 2 recorder (jt246/253/254/258), CODE 8 monster-art
(jt365/371/372), CODE 10/11 viewers (jt267/jt244) — each a multi-lift
buildout, not a leaf fill. Neither is a "keep grinding leaves" step;
both want an explicit scope decision.

---

# Stand-in / stub audit — 2026-07-04 (live-probe refresh)

## Snapshot 2026-07-04 (regen `tools/jt_progress.py` + FRUA_ENGINE_PROBE_ONCE)

1205 JT entries: **974 done**, ~45 stub, 183 missing after this
session's fixes. Two new audit instruments this refresh:

1. **Alias re-sweep** (caller-segment resolution): NINE stub-shadowed
   twins repointed (c4ceb06) — jt52 sound dispatch, jt69 fatal-error,
   jt85 FRAME palette, jt19 unlink, jt170/jt175 dialog leaves, jt586
   vault save, jt524 combat field commit, jt303 status header; plus
   l23ee=jt312 (65ef359). Full table + the l2f24 same-offset trap in
   docs/stub-alias-audit.md.
2. **Live-probe coverage** (`make EXTRA_CFLAGS=-DFRUA_ENGINE_PROBE_ONCE
   ...`, drive the play loop, harvest DBG.LOG): only ~14 distinct stubs
   fire across boot->menu->hall->load->entry->event->walk->camp->exit.
   ALL resolved 2026-07-04 (65ef359): l40b4/l6804/l15bc/l4738/jt146/
   jt1088/l4226/l4268 lifted; jt1115/jt956/jt1137 disasm-verified
   faithful NOOPs; jt931 = the rule-book copy-protection challenge
   (unreachable once the slot-3 "Boots" marker seeds, f518e7b);
   l005a = documented GEMDOS divergence (save medium always reachable).

**Boot-fatal regression lesson (f518e7b):** repointing a stub to a real
body can ARM dormant faithful behaviour — jt69 (fatal content-load
error) made the phase-5 string-table integrity check lethal because the
port's curated string table lacked the slot-3 "Boots" marker. Smoke-boot
in Hatari after every repoint batch, not just `make test`.

**Camp-exit black-view: RESOLVED (two fixes, probe-verified).** The
chain: (1) cw_wallfile_load's sibling dispose did `jt461(group);
binder = NULL` — a bare drop that leaked one -18468 binder slot per
8X8DB<->8X8DC flip; after ~6 flips the 10-slot pool was full and
l33ac fell through to group 12 (topview's) → the invisible "Group 12
in use" modal → black frame. Fix = release through l31dc (slot AND
group). (2) jt948's ENCAMP arm was `l473e(1); break;` — the Mac
(L4c18→L4cda) KEEPS WALKING when -27982 is clear; the unconditional
break dumped every post-camp player to the main menu. Fix = continue
the walk loop when the flag is clear. probe10: camp→EXIT→dungeon
live, 0 overflows, 0 modals. Full story: docs/play-loop-wall.md 04b.
OPEN NIT: post-camp right-hand panels show the AREA/MARK overlay
(mode restore lands in area-map mode); view + command bar correct.

---

# Stand-in / stub audit — 2026-07-01 (refresh)

## Snapshot 2026-07-01 (regen `tools/jt_progress.py`)

1205 JT entries: **955 done** (875 lifted, 20 noop, 60 alias), **59 stub**,
**191 missing**, 0 JT stand-in → **250 pending (20.7%)**. Since 06-23:
+110 done, stubs 142→59. The top-500 ranks are closed except 3 stubs
(jt45, jt68, jt1081 — jt1081 is a faithful-by-design teardown stub).
Counts/tables: [`docs/jt-lift-progress.md`](jt-lift-progress.md).

Closed since the last audit: **all 16 l709e event-type arms** have real
bodies (the event campaign), **CODE 16 handler tier 114/115**, and the
**faithful jt169 List Manager went LIVE 2026-06-28** (#146 cutover;
`jt169_reimpl` is a dead fallback).

## LIVE stand-ins (the actual replace-with-disassembly debt)

Everything below has real behaviour on a reachable path but is a port
reimplementation, ordered by how much it distorts the faithful game:

1. ~~`port_load_savgame` boot auto-load~~ — **RETIRED 2026-07-01**: the
   heuristic SAVGAMA scan is deleted; the boot pool comes from .CHR
   files and the party is built in-game via the Hall's Load Saved Game
   (jt918 → jt582 → l143e → jt579), Hatari-verified end-to-end
   (docs/play-entry-wall.md). Remaining #100: the CURRENT.TXT
   design-name seed + the synthetic roster seed.
2. **`fill_backdrop`** — "tuned interior tile" GEN.CTL fill, not the
   faithful piece-placed gen backdrop. Live under `menu_run`, `jt574`
   (char-gen entry), `cg_train_screen`, `cg_draw_sheet` (`cg_message`
   was deleted in the sweep); the Hall now paints `jt81()` over it
   every frame (ab8a567). RE the gen piece placement, then delete.
3. **Dungeon HUD chrome**: `port_draw_play_frame` (2 sites) +
   `port_hud_text_clut` + `port_draw_compass` — the #114 coarse
   over-blit; faithful composer = jt304 → L3fd8 (a few jt1001 FRAME
   pieces + jt216/L4430 panels).
4. **Play-loop**: `port_run_encounter` (2 sites) + `port_play_message`
   (3 sites) — over the faithful CODE 15-20 encounter chain (#115:
   l3b0e + CODE-20 L026e + l03f6).
5. **`port_show_intro`** — title/credits, trace-matched, not lifted
   from CODE 22.
6. **GLIB bootstrap wiring**: `port_frame_load` / `port_always_load` /
   `port_menu_load` / `port_ui_group_base` — faithful = jt464 +
   jt997/jt1014 plain-name loader → flip the live loader to the FAR
   pool (groundwork lifted, b96a694).
7. **`l309c_tile`** — the register note "off the wall path" is STALE:
   jt114 routes the wall blit through it again as the blit-time
   colour-band rebase (32/64/96) reproducing the GLIB colour-range
   allocator's relocate+remap (jt1069 ncopy) at blit time instead of
   load time. Faithful = remap pixels at load, blit raw l309c/l2d4e.
8. ~~Hall Remove/View chrome~~ — **RETIRED 2026-07-02**: the label-crossed
   dispatch was straightened (case 4 Remove → the already-lifted L1060
   body: jt584 .cch save + jt19 unlink; case 7 Change-Class → the
   faithful L0f74 skeleton), and the whole cg_draw_sheet / cg_rename /
   cg_modify_sheet / cg_remove_from_party cluster deleted (~290 lines
   incl. its orphaned tables). Every Training Hall screen is now
   faithful code. OPEN NIT: one blank frame after Remove until the next
   input (suspect jt55 → l3b1e → jt1022 on -27866, or the l036a
   invalid-item modal — see docs/training-hall-wall.md).
9. **`menu_run` + CODE 22 menu chrome** — mirrors the faithful
   jt315/jt313 build; per faithful-main-menu notes no traceable Mac
   path draws per-command bars, so this is low-distortion.
10. **l0aae shape-7 DLItem passes NULL where the Mac passes jt916**
    (keyboard-accel handler) — small input divergence.

## DEAD stand-ins — DELETED 2026-07-01 (the sweep; git history has them)

Removed (~2,100 lines, boot verified unchanged in Hatari):
`port_menu_bar`, `menu_draw_plates`, `port_rest`,
`port_begin_adventure` (+ `g_adventure_mode`), `port_save_game` /
`port_load_game`, `cg_add_character`, `jt169_reimpl` (+ its
`jt169_pick` / `picker_button_track` / `picker_cmd_button` helpers),
`port_render_geo_map` / `port_render_geo_tiles` /
`port_render_geo_contact` / `port_render_topview`, `port_blit_demo` /
`port_sprite_demo` / `port_view_demo` / `port_wall_demo` /
`port_l6234_verify` / `port_play_demo` (+ the `FRUA_MAP_DEMO` /
`FRUA_3D_DEMO` / `FRUA_L6234_VERIFY` ifdef blocks and the `make walk`
target), orphaned helpers `cg_message` / `cg_collect_addable` /
`cg_party_setforth_screen` / `draw_map_tiles` / `blit_glyph_1bpp` /
`draw_party` / `edge_color` / `map_demo_palette` / `geo_hdr_word` /
`ctile_blit` / `clut_nearest` / `sprite_row` / `g_wall_*`, and the
demo decls in boot.h. Parked faithful lifts kept with
`__attribute__((unused))`: `bp_present`, `jt215`.
`port_test_seed_design` stays (live harness seeding, not play-path).

## Stub queue (PROBE-only, pending lifts)

- **59 jtN stubs + 191 missing** — per-chunk and per-CODE tables in
  jt-lift-progress.md. Largest pending blocks: CODE 4 display (63,
  mostly superseded by the HAL), CODE 5 core runtime (51), CODE 3
  Toolbox (24), CODE 8 UI/file (17), CODE 2/11 editor (15, authoring).
- **88 local lXXXX leaf stubs** (list in jt-lift-progress.md).
- **21 `TODO` markers** in boot.c (deferred structural-skeleton arms).

---

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
