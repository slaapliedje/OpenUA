# Resource Manager (FC group cache) — worklist

## PALETTE-COMMIT CHAIN (2026-06-13, faithful trace — NOT yet committed)

The "4 colours" picture bug (merchant event-art #125, bigpic, GLIB pictures)
is the picture-palette commit being gated off. Faithful chain (verified vs asm):
- A loaded picture's palette commits through `L3eea(handle)` -> `jt468` group
  lookup -> `jt1017` type check -> **`jt993`** (TNPalette) -> reads the palette
  GLIB item via `l2856`, copies the RGBs, installs via **`jt1069`/`jt1066`**.
- `jt993` count = `jt1200() ? 16 : 256`, and BOTH `L3eea` and `L3f3c` gate on
  `jt1163() || jt1200()==0`. jt1163()=0 (faithful), so it needs **jt1200()==0**.
- `jt1200()` = `(g_a5_2347==0)?3:((g_a5_1312!=0)?0:1)`. g_a5_1312 (the 320x200
  8-bit-colour flag) was NEVER set -> jt1200()=1 -> the gate was always false
  -> pictures installed no palette. **FAITHFUL fix:** the Mac boot init (CODE 4
  @0x47b4) sets g_a5_-1312 = (-2344 && !-1314 && screen_depth(-1318) >= 8) ?
  1 : 0 — i.e. 1 for an 8-bit screen, which the Falcon VIDEL always is. So seed
  g_a5_-1312 = 1 (NOT a shortcut — the lifted value for this hardware).

EVIDENCE (FRUA_PAL_PROBE -> C:\PAL.TXT, harness P->B to the merchant): with
g_a5_-1312=1, jt993 fired and committed **224 real colours** (start=32, RGBs
151,167,183 / 87,87,87) — the chain WORKS. But:
1. The merchant PIC still renders dark/few-colour. Its palette commits to
   indices 32..255; suspect the **pixel->CLUT band offset** — the PIC's pixels
   may not be offset into 32..255 (or are mostly low/UI indices), the SAME band
   issue as the wall jt114 stand-in. Check the l3880/jt1001 picture blit + a Mac
   pixel compare before declaring the colours right.
2. g_a5_-1312=1 UNMASKS a text-clear regression (event text overwrites instead
   of clearing) — a separate port bug the wrong -1312=0 was hiding; lift it
   faithfully (find the play/event clear path that assumed jt1200()==1).
So the faithful sequence is: seed -1312 (faithful) + fix the text-clear lift +
fix the picture pixel-band offset. Reverted for now to keep the build green;
do all three together. See [[feedback-no-shortcuts]].


The faithful "Resource Manager" FRUA uses for its art (.CTL/.TLB GLIB
libraries) is the **FC group cache**: on-demand, by-numeric-name, cached
(purgeable) loading of library items into a shared pool. The port's art
loaders currently *bypass* it with positional `l37aa` shortcuts, which both
(a) picks the wrong tiles (wood-vs-stone wall bug, #126) and (b) forces
whole-file resident loads (the 4MB pressure + perf, see
docs note port-memory-vs-mac-1mb). Goal: route the art loaders through the
FC group system so resolution is faithful AND loading is cached/purgeable.

## How the faithful path works (reverse-engineered 2026-06-13)

Wall-set load chain on the Mac:
- `L6148` (CODE 7 @0x6148) per-frame: for each wall group type 0/1/2 calls
  `L6eea(id, type)`.
- `L6eea` (CODE 7 @0x6eea): id picks the 'b'/'c' letter only; builds name
  `"8x8d<letter><type+1>"` (e.g. `8x8db1/2/3`); calls
  `JT[110](name, id, 0, 0, &handle[-27894+type*4])`.
- `JT[110]`=`L33ac` (CODE 6 @0x33ac, **lifted as `l33ac`**): claims a -18468
  binder slot; if the name ends in a digit, strips it, builds the concrete
  file `"<base><group+1><id:03d>.ctl"` (e.g. `8x8db1005.ctl`, a per-design
  override) and the fallback `"<base>.ctl"` (`8x8db.ctl` = the shared file);
  registers the group via `jt464`, reads the file into the pool, stamps the
  binder context (-18402 name / -18408 kind=id / -18406 group / -18404 digit
  / -18398 mode), and dispatches `jt987(..., jt104)`.
- `jt104` (per-file callback, **lifted**): finds the item by **NUMERIC NAME**
  via `jt1013(refnum, id)` — fallback `jt1013(refnum, id + 100*(group+1))` —
  then `jt1011` size + `jt1016` load into the pool group (-18406), or the
  TLB cache build (`jt1024/jt1021/jt1023`).
- The handle at `-27894+type*4` then resolves through the group
  (`jt468(group)` -> pool base) and `jt114`/`l2856` index the item.

KEY: items are addressed by **numeric name**, not position. `l37aa(base,8)`
(port shortcut) returns the 8th sub-GLIB; the faithful path returns the item
*named* 8. They differ -> wrong texture.

## CURRENT-STATE AUDIT (2026-06-13, via tools/jt_progress.py --wiring/--standins)

The RM is **~90% lifted** — this is completion + routing, not a cold build.
Status of the FC-group surface (class / asm-calls / port-calls):

| fn | class | asm | port | note |
|----|-------|----:|----:|------|
| jt110 (l33ac) | real | 6 | 0 | binder; just got the 8.3 base-fallback fix (2b5efd4) |
| jt464 | LIFTED | 6 | 5 | group register + MRU cache (-10026/-10074/-9354/-9306) |
| jt468 | LIFTED | 64 | 39 | group -> pool-base resolver (heavily used) |
| jt987 | LIFTED | 9 | 6 | open + per-file callback driver |
| jt997 / jt1014 / jt972 | LIFTED | — | — | plain-name CTL/TLB loaders |
| jt104 | LIFTED | 2 | 0 | per-file numeric-name cb (port=0 OK: passed as a fn-ptr) |
| jt1013 / jt1011 / jt1016 | LIFTED | — | — | find-by-name / size / load-into-pool |
| jt1024 / jt1021 / jt462 / jt461 / jt465 | LIFTED | — | — | TLB cache build |
| jt111 / jt115 / jt405 | LIFTED | — | — | handle resolution |
| jt214 / jt124 / jt131 | LIFTED | — | — | bigpic id select / palette commit / mode |

**GAPS (the actual RM work):**
1. ~~`l338c` is a STUB~~ — **CORRECTED 2026-06-13: `l338c` is FAITHFUL** (a
   one-liner `g_a5_-18396 = (jt1200()==3)?mode:52`, verified vs the asm; my
   audit heuristic false-flagged the one-statement body). The real bigpic
   blocker is the **id selection**: `jt214` (faithful) computes `id = ds[8]`
   (=8 for HEIRS) else `rec[19]+239`. But the base libraries index ids
   **240-247 (BIGPIC) / 248-252 (BIGPIX)** — there is NO id 8, and HEIRS ships
   NO bigpic override (only base BIGPIC/BIGPIX exist). So `l579e(8)` -> looks
   up id 8 -> not found -> SysBeep + blank (and the render stalls). `ds[8]` is
   the design's Backdrop1 (the 3D-view backdrop, used correctly by
   cell_backdrop_id); `jt214` reads the same byte as the bigpic id, which is
   only valid when 0 (-> the generic rec[19]+239 path, 240+) or a real
   override id. ROOT: either the port's design-state load has the wrong value
   at ds[8] for jt214's purpose (a #128 layout issue — compare port ds bytes
   to the Mac's), or jt214/l579e needs a graceful "id absent -> generic /
   skip" fallback so it doesn't stall. Resolve THIS before re-wiring jt214/jt44.
   - **EMPIRICAL (2026-06-13):** added the jt214 generic fallback (2d92114) so
     the id is always a valid base id, then re-wired port_draw_play_frame ->
     jt214+jt44. It STILL SysBeeps + stalls (frame chrome draws, but no bigpic,
     no roster, no walls; render never reaches jt199). So the blocker is NOT
     the id — it is **downstream in the load/blit/palette chain** (jt987 read /
     l3f3c palette range / l3880 blit / l3eea palette commit). Next: instrument
     each step (file-based dump — conout stalls in play mode) to find the
     hang/alert. The bigpic wiring is reverted; grey-fill + l67ca preserved.
2. **`jt1023` is a STUB** — a TLB-cache-build leaf. Lift it so .TLB groups
   (the deep/640 path) build their cache faithfully.
3. **Routing**: the art loaders still bypass the group system with `l37aa`/
   static-buffer shortcuts — `cw_load_slot`/`cw_wallfile_load` (walls),
   `l579e` (bigpic; now base-fallback but not pool-resolved), `port_frame_load`
   (frame). Re-point them at `jt468(group)` so loading is cached/purgeable and
   handle resolution is faithful.

## STEP 2 RESULT (2026-06-13) — numeric-name theory DISPROVEN

Parsed 8X8DB.CTL's directory (item 0 = [count][id u16, index u16] pairs, the
table jt1013 scans): it is **IDENTITY** (id N -> index N for 1..9). Sub-GLIBs
(sets) have NO remap directory (their item 0 is the palette). So
`jt1013(refnum, 8)` returns index 8 = DB8 — the SAME tile the port's
positional `l37aa(base, 8)` returns. **Routing l6eea through the FC group
cache would NOT change wood->stone.** L6148 also passes ds[4/5/6]=5/8/1 raw to
L6eea (no design-level remap).

Rendered the EXACT trace tiles: set5 idx8 = full flat STONE-BRICK face; set8
idx2/idx30 (the code-9/6 near tiles) = tiny WOOD edge SLIVERS. So on the Mac
the prominent flat walls are set5/group0 (stone, codes 2/5/1) and set8 wood
shows only as thin receding edges. The port renders wood-DOMINANT instead ->
the bug is in the **render geometry** (l5b42/jt200 coord+scale transform
sizing/placing the slot tiles wrong) or a per-slot divergence, NOT the wall
set and NOT the RM. -> The wall fix is the jt200 SLOT-BY-SLOT DIFF vs
docs/mac-blit-trace-heirs-l5.md (instrument the port's jt200 with #/top/left/
code/sub/idx, diff all 25 slots, find where the port draws wood big). Moved
to #126; de-linked from the RM.

The RM (#127) is STILL worth doing — for memory (purgeable on-demand loading,
the 4MB/ST-port path) and perf (cache-hit per-frame L6148) — but it is NOT
the wall-texture fix. Reprioritize: RM is a memory/perf effort now.

## REVISED WORKLIST (2026-06-13, post-audit — each = one focused commit)

0. [x] AUDIT the FC-group surface (above): ~90% lifted; gaps = l338c, jt1023,
   and the loader routing. 8.3 override fallback already landed (2b5efd4).
1. [ ] **Lift `l338c`** (the bigpic "select load kind", CODE 6) — the first
   blocker. Then re-wire `port_draw_play_frame` -> `jt214` + `jt44` and confirm
   the real bigpic background renders (vs the grey-fill stand-in + SysBeep).
2. [ ] **Lift `jt1023`** (TLB cache-build leaf) so the .TLB/deep path is whole.
3. [ ] Add a group-state probe dump (like VIEWDIAG): pool base, the -10026/
   -9354 records, binder -18468 slots, and `jt468(group)` for the loaded libs,
   so end-to-end wiring is verified from live state, not inspection.
4. [ ] Route `cw_load_slot`/`cw_wallfile_load` (walls) + `port_frame_load`
   (frame) through `jt468(group)` instead of `l37aa`/static buffers.
5. [ ] Purgeable caching: evict LRU groups under memory pressure (the MRU
   -9354 list is already maintained) -> the 1-4MB / ST-port footprint.
6. [ ] Perf: confirm per-frame L6148 is a cache-hit once resident.

## (historical) Worklist (each = one focused commit)

1. [ ] AUDIT: confirm the FC-group data model is fully wired — the pool
   (-3622/-18406), the group records (-10026/-10074/-9354/-9306), the binder
   slots (-18468), and that `jt468(group)` returns the loaded library base.
   Add a probe dump (like VIEWDIAG) of the group state after a load.
2. [ ] Verify `jt1013` numeric-name lookup against 8X8DB.CTL: dump the item
   NAMES/ids vs positions (do the items carry numeric names that differ from
   their positions?). This proves the wood-vs-stone hypothesis concretely.
3. [ ] Re-wire `l6eea` to call the faithful `l33ac`(jt110) path instead of
   `cw_wallfile_load`+`l37aa`. Validate HEIRS start renders STONE (DB5-style)
   near walls + wood door, matching data/fura_mac_heirs_start.png.
4. [ ] Re-point `cw_load_slot`'s palette/facet copy at the group-resolved
   library (or retire it in favour of the jt114 handle path).
5. [ ] Route the other art loaders (backdrop BACK.CTL, sprites, UI ALWAYS/
   FRAME via jt468 groups) through the same path; retire the l37aa shortcuts.
6. [ ] Purgeable caching: make the pool evict LRU groups under memory
   pressure (the MRU -9354 list is already maintained) so the resident set
   shrinks — the path to a 1–4MB footprint (enables an Atari ST port; see
   memory port-memory-vs-mac-1mb).
7. [ ] Perf: confirm the per-frame L6148 reload is cache-hit (no re-read)
   once a group is resident; measure 3D-view frame time before/after.

## Ground truth

- `data/fura_mac_heirs_start.png` — real Mac HEIRS save A start: STONE brick
  walls, stone floor, starlit ceiling, wood DOOR one step east, 6 party
  members. The port must match this.
- `docs/mac-blit-trace-heirs-l5.md` — the 25-slot jt200 trace (codes/idx).
  The port's jt200 already emits identical codes/idx; only the *tiles the
  handle points to* are wrong, which this worklist fixes.

## 2026-06-14 — RM-COMPLETION SCOPE (user directive: take #127 to 100%, then FC group)
User redirected off the 3D view: "go back to tracking through the Resource Manager
and get that implemented, with all dependencies... to 100% completion. Then the FC
Group code." (The 3D pipeline is proven faithful slot-for-slot vs the Mac trace —
docs/dungeon-view-wall 14h/14i — so it's parked, not abandoned.)

PRECISE remaining work (audited 2026-06-14):
1. **jt1023** (CODE 5+0x47d6) is the LAST PROBE stub in the RM surface — the
   signature-keyed TLB cache-build dispatcher. It walks the converter registry
   (-3654 sig[] / -3638 fnptr[] / -3656 count), reads the file via JT[414] into
   the L37aa item, and on a sig match calls the registered builder, shrinking the
   group offset table to the built size. DEPS NOT YET LIFTED:
     - **l46a6** (CODE 5+0x46a6) = LBResize: resize list-block `item` to `size`
       via L3834 (offset) + L3736 (count) + jt406 shuffles (+ the LBResize-invalid
       L036a modals). ~130 B.
     - **jt414** (CODE 3+0x3d98) = file read leaf (linkw -52). NOT lifted.
   Then DELETE the jt1021 `if(1) return;` port guard (jt1024 LBCreate is already a
   full body) — same commit.
2. **Routing** (the texture-resolution angle the user is after): re-point
   cw_load_slot / cw_wallfile_load (walls), l579e (bigpic), port_frame_load (frame)
   at jt468(group) + the numeric-name jt104/jt1013 path instead of the positional
   l37aa / static-buffer shortcuts. NOTE the earlier "STEP 2 numeric-name DISPROVEN"
   was for 8X8DB (identity directory) — re-verify per-library before assuming.
3. **Purgeable caching** (#127 memory goal): LRU-evict groups via the -9354 MRU
   list under pool pressure.
4. Group-state probe dump (pool base / -10026 / -9354 / binder -18468 / jt468(grp))
   to verify wiring from live state.

ORDER: leaf deps (jt414, l46a6) -> jt1023 + drop jt1021 guard -> routing -> probe ->
purgeable. Each = one focused, build-green commit. This is multi-commit, not one turn.

## 2026-06-14b — SCOPE CORRECTION after reading the LB cluster
- jt414 (file read) LIFTED (FSRead-based, builds green) — the one truly-missing leaf.
- l46a6 is NOT missing: it IS **jt1022** (LBResize), already a full faithful body.
- So jt1023's deps all EXIST. The real blocker: **jt1021 (LBInsert) AND jt1022
  (LBResize) are LIFTED-BUT-GUARDED** (`if(1) return;`) — jt111's boot-time call
  corrupts the FAR pool (SysBeep modal before the menu; 'GLIB' magic passes, so the
  fault is in the resize arithmetic vs the PORT's pool layout, not the transcription).
  jt1024 (LBCreate) has a full body. To finish jt1023 (and faithful pool list-block
  ops generally) we must UN-GUARD jt1021/jt1022, which requires INSTRUMENTING jt111's
  actual call values + the pool layout to find why the resize corrupts (vs the Mac).
- NOTE jt1023/TLB-cache is the COLD .TLB (deep/640) path, NOT the hot .CTL dungeon
  path — so it is RM-completeness, not the dungeon-texture fix.
REVISED ORDER: (1) instrument jt111 -> jt1021/jt1022 pool corruption, fix it, drop
both guards. (2) wire jt1023 on top. (3) loader routing (the dungeon-relevant part).
(4) purgeable. The pool-corruption fix (1) is the keystone + the riskiest — do it
first, carefully, with a live group-state dump.

## 2026-06-14c — ROUTING: grounded mechanism (the FC pool doesn't load these yet)
Mapped the shortcut loaders. KEY FINDING: the FC pool does NOT currently load
ALWAYS.CTL / FRAME.CTL / 8X8DB.CTL — there are NO jt997/jt1014/l33ac calls for
them. The port loads each RESIDENT into a static buffer (port_always_load ->
g_always_base, port_frame_load -> g_frame_base, cw_wallfile_load -> g_wallfile_buf)
and jt468 SHORT-CIRCUITS groups 0/1 to those buffers (port_ui_group_base). So
removing the jt468 shortcut alone would resolve to an EMPTY pool slot -> break the
menu. (l579e/bigpic is the ONE loader already routed through l33ac.)

ROUTING per loader = TWO coordinated changes:
  (1) wire the FC pool LOAD: jt464(name, group) register + l33ac/jt997 read the
      .CTL into the pool group;
  (2) switch the resolver: drop the jt468 port_ui_group_base shortcut (UI libs)
      / point cw_wallfile_load at jt468(group) (walls).
Verify each renders before the next.

ORDER (low->high risk): ALWAYS.CTL (group 0, 8KB) -> FRAME.CTL (group 1, 40KB) ->
walls (8X8DB/DC 296KB — ALSO needs purgeable caching, item 3, to fit the FAR pool;
the static buffer exists precisely because NewPtr(320KB) failed at 4MB,
[[dungeon-walls-4mb-fix]]). So: do ALWAYS+FRAME routing first (small, safe, proves
the FC pool load path end-to-end), then purgeable caching, THEN walls.
Each is one focused commit; verify the menu/chrome/walls render after each.

## 2026-06-14d — PURGEABLE CACHING (item 3): the machinery was already lifted;
the real gap was the POOL SIZE, plus a DUPLICATE jt463.

FINDING: the LRU/purge machinery is ALL faithfully lifted AND wired already —
`l103c` (release a group, slide the heap), `l11ca(purge)` (one compaction pass:
scan ORPHANED groups — those with no freemap g_a5_10074 reference — by sequence
or by the -9354 MRU companion under jt1083(3)), `l0a6e(need)` (compact until
`need` free on the tail), and the grow allocators `l0ab8`/`l0e10`/`jt460`/`jt467`
all route through `l0a6e`. The RNG `jt1083` and MRU search `l3e0c` are real, not
stubs. So the Mac's purge model (reclaim only orphaned groups; -9354 picks
eviction ORDER among orphans) was complete. Nothing to lift there.

THE ACTUAL GAP — jt463 (CODE 3+0x538) sizing was a STAND-IN: it reserved a flat
768K with a halve-to-128K fallback, instead of the Mac's free-memory negotiation.
And there were TWO lifts of JT[463]: `fc.c::fc_init` (an earlier standalone lift,
own non-A5 globals g_fc_buffers/g_fc_records — DEAD: nothing in the live load/
render path reads them) AND `boot.c::jt463`/glib_pool_open (the A5-world one wired
to the live pool g_a5_10270, jt464/jt467/jt468). master_init called BOTH, so
fc_init allocated a 214–450K buffer that nothing read AND stole that memory from
FreeMem() before the real pool was sized. Decoded jt463 fully from the disasm:
  jt463(minKB, maxKB):                       (jt1079 passes 214/450 on the normal
    minbytes = minKB*1024  (JT[4] long mul)   path = ALWAYS.CTL present; 160/400
    maxbytes = maxKB*1024                      on the fallback path)
    want = min(maxbytes, max(minbytes, FreeMem()-32K))   (L04f4=min, L0516=max,
    base = NewPtr(want)                                   JT[1026]=_FreeMem)
    if !base: want=minbytes; base=NewPtr(want)           (JT[1028]=_NewPtr)
    slot[0]=base; -9304=base+want; err if !base||minbytes>want; -9300=want
  L35e2 (glib_lb_init) is the caller-side step right after — verified at the
  CODE5+0x56 call site.

FIX (this commit): boot.c::jt463 carries the real size negotiation, threaded
minKB/maxKB via glib_pool_open(kb_min,kb_max) <- master_init. Added glib_pool_close
= the faithful JT[466]/FCCleanup (jt465(NULL) reset + DisposePtr the real
g_a5_10270[0]). Dropped the redundant fc_init/fc_cleanup calls from master_init/
master_shutdown (they operated on the dead fc.c parallel pool). The pool is now
Mac-sized (≤450K, grabbed at boot when memory is free), so the purge layer is
load-bearing. fc.c is now fully dead duplicate code (only fc_dump, a no-op, is
still referenced by boot.c) — DELETE-LATER cleanup, kept this commit minimal.
Build green, 020/soft-float intact.

NOTE for WALLS routing (next): with the Mac-sized pool, 296K walls + a resident
165K bigpic exceed 450K, so the bigpic group MUST become orphaned (jt461/jt465
release on leaving the event-picture screen) for l11ca to reclaim it. Confirm the
faithful release is wired at the event-pic teardown before routing walls, else the
pool deadlocks ("Out of FAR memory!"). HEIRS 8X8DB directory is IDENTITY -> routing
won't change the resolved tile (no behaviour change) and does NOT fix garbled walls.

## 2026-06-15 — WALLS STAGE 4 DONE + VERIFIED (binder model — walls survive events)
Implemented the binder-slot model. -27894/-27890/-27886 now hold per-type -18468
BINDER slots (slot[0] = FC group), written by l6eea via l33ac, exactly like every
other loader:
  - l6eea: build "8x8d%c%d" (letter by id band, %d=1 colour / type+1 else) and
    l33ac(name, id, 0, 0, &(-27894+type*4)) -> binder into the pool; g_wall_set[type]
    holds the set sub-GLIB index (from wallset_for_id). l33ac's l31dc-of-prior makes
    the per-frame l6148 call the faithful dispose-and-reload.
  - jt200_layer (the blit): resolve sub = l37aa(jt468(binder[0]), g_wall_set[group])
    FRESH each tile -> jt114 -> l309c. jt468 follows the live pool base (purge-safe);
    matches the Mac jt114 (CODE 6 L3804: pushes *handle -> jt1001 -> jt468(*handle)).
  - jt209 -> jt115 (the teardown jt131 runs on every event mode change) now stamps
    the binder (slot[0]=-1) and jt461(slot[0]) UNBINDS THE REAL GROUP (orphan ->
    purgeable), instead of writing -1 into the tile data. This is the fix.
  - BONUS: jt124->l3eea (CODE 6 L6234 prologue palette commit) ALSO expects a binder
    (*p -> jt468); the old sub-pointer fed it garbage. Now correct.

The static fallback is retired from the wall render (l6eea uses l33ac directly, not
cw_wallfile_load). The full faithful dance now works: event fires -> jt115 orphans
the walls -> bigpic purges them (l11ca) -> RETURN -> l6148 reloads from the pool.

VERIFIED (HEIRS caravan, user-driven): walls render before AND after the caravan
(the white/gray empty box is GONE — the jt115 tile corruption is fixed); hatari
survived the event (no crash); zero FAR errors; the render is the same garble
before/after (the separate, parked texture-slot puzzle — the binder blit
resolution matches stage 3's, so the routing is render-equivalent). Commit: <this>.
RM #127 walls routing COMPLETE (stages 2+3+4). The remaining garble is
[[dungeon-3d-render-state]], orthogonal to the RM.

## 2026-06-14i — WALLS STAGE 4: ROOT CAUSE = the -27894 sub-pointer model (binder model REQUIRED)
Attempted stage 4 (release the lingering event bigpic in render_3d_faithful so the
pool frees for the walls) — REVERTED, it was the wrong fix. The real blocker:

When an event picture loads, l579e calls jt131(3); transitioning OUT of mode 0
runs jt209(0) -> jt115(&g_a5_27894[i]) on all THREE wall slots. jt115 is:
  if (*block >= 0) jt461(*block);  *block = -1;  *slot = NULL;
The port stores a DIRECT SUB-GLIB POINTER in -27894 (stages 2/3), so:
  - jt461(*block) reads the tile's first word (a HEIGHT like 8/16/56) as a group
    tag -> harmless no-op (heights exceed the ~7-group count), BUT
  - *block = -1 WRITES -1 INTO THE WALL TILE HEADER (the pool tile data),
    corrupting it; the stage-3 reload returns the same cached (corrupted) base
    (jt468(group)!=0 -> no re-read) -> walls render broken/gone after ANY event.
This is PRE-EXISTING (the pre-stage-2 static buffer had the identical corruption;
the walls-after-event path just wasn't exercised). User-confirmed: HEIRS caravan
renders, then the 3D view is white/gray (no walls); also the post-dialog give-item
(100pp + Ring) is SKIPPED (a SEPARATE l709e event-chain gap, not walls).

FAITHFUL FIX = the binder-slot model: -27894/-27890/-27886 must hold a -18468
BINDER SLOT (slot[0]=group id), like l33ac writes for every other loader, so:
  - jt115 stamps -1 on the descriptor (releasing the binder) NOT the tile data;
  - jt461(slot[0]) unbinds the REAL wall group (orphan -> purgeable);
  - the blit (jt114 @ boot.c:10027) resolves binder->jt468(group)->l37aa(base,set)
    each blit instead of a cached raw pointer.
That is the deep render-path rewrite flagged from the start — effectively redoing
the l6eea store + the jt114 blit on the binder model + the jt111/jt123 near-tile
synthesis + the per-set CLUT band. It is the real stage 4, a dedicated multi-
session effort. Stages 2+3 (walls in the pool, purge-safe re-resolve, verified for
the NO-EVENT path) STAND and are not a regression; the event teardown
incompatibility is the pre-existing -27894 model, orthogonal to where the bytes
live. Mechanism fully mapped in 2026-06-14f above.

## 2026-06-14h — WALLS ROUTING STAGE 3 DONE + VERIFIED (purge-safe per-frame re-resolve)
l6148 now re-resolves all three wall handles (-27894/-27890/-27886) from the FC
pool EVERY render via l6eea, instead of only on id-change/handle==0. l6eea ->
cw_wallfile_load is a cache hit (jt468) when the group is resident, so it is cheap
and re-reads the 296K library only after an actual purge (jt460 reserves space
before any I/O, so a contended retry does no read). Re-resolving each render is
PURGE-SAFE: a pool compaction (an event bigpic load evicting an earlier group)
slides the wall data, so a cached raw pointer would dangle — jt468(group) always
returns the group's current base, so l37aa(jt468,set) stays fresh. Dropped the
s_w1/s_w2/-12296/handle==0 guards (they only existed to dodge a per-frame re-read
the FC cache now makes free); -12296 stays updated for jt209's teardown sentinel.
Self-healing: a failed pool load leaves the group unbound (jt461 in jt104's fail
path), so jt468==0 and the next render retries the pool; falls back to the static
buffer only while contended.

VERIFIED (HEIRS, skip-events, user walked several steps): hatari survived multiple
movement frames (no dangling-pointer crash); h0/h1/h2 (3216088/3323136/3087176)
all inside the pool (pool_start=2934860 .. pool_usedend=3383492); the pool base
SHIFTED ~308B from the stage-2 run (3216396->3216088) and the handles tracked it
-> the per-frame re-resolve follows the live base (the purge-safety mechanism
working); wallfile_which=-1 (fallback never used); wall1/2/3=5/8/1, 10,8,E
unchanged; no FAR error. Commit: <this>. Stage 4 (event-path wall release so the
bigpic can purge the walls, then drop the static fallback) remains.

## 2026-06-14g — WALLS ROUTING STAGE 2 DONE + VERIFIED (cw_wallfile_load -> FC pool)
cw_wallfile_load now loads 8X8D{B,C}.CTL through the faithful l33ac -> FC pool
path (l338c kind 50 + "8x8d%c1" name -> override-aware group loader), caching the
bound group per file and re-resolving jt468(group) each call (purge-safe re the
pointer). l6eea/l6148/the blit are UNCHANGED — they still do l37aa(base, set); only
`base` moved from the static g_wallfile_buf to the pool. The static buffer is kept
as a FALLBACK (migration scaffold): if the pool can't hold the 296K library (it
collides with a resident event bigpic until stage 4 lands), it serves the static
buffer so walls never regress — worst case = today's behaviour.

VERIFIED (HEIRS, FRUA_SKIP_ENTRY_EVENTS build, VIEWDIAG.TX + RMAUDIT.TXT):
  pool_start=2935168 pool_usedend=3383800 (450K pool) ; wallfile_buf=1518926
  h0=3216396 h1=3323444 h2=3087484  -> ALL THREE wall handles INSIDE the pool
  wallfile_which=-1                 -> static fallback NEVER used
  pool_freebytes=12168              -> 296K library fit in the pool (12K spare)
  no "Out of FAR" ; wall1/2/3=5/8/1 row=10 col=8 facing=2 ; J200DIFF 393 lines
  unchanged -> render byte-identical (still the parked gold-centre garble).
Commit: <this>. Stage 2 is the faithful LOAD; stages 3 (binder/jt468 purge-safe
blit, retire the cached sub-pointer) + 4 (dispose-reload so the bigpic can purge
the walls and drop the fallback) remain. The fallback makes the bigpic-coexistence
case safe in the meantime (walls fall back to the static buffer while the bigpic
is resident; no regression).

## 2026-06-14f — WALLS -> FC POOL ROUTING: full mechanism mapped (the big one)
Mapped CODE 7 L6eea + CODE 6 l33ac end-to-end. The faithful path is NOT a simple
load swap; it is a coupled render-path change. Pieces:

FAITHFUL L6eea(id, type):
  1. l338c(50)                                  ; JT[113] load-kind
  2. letter = (id < 10) ? 'b' : 'c'
  3. arg    = (jt1163() || jt1200()==0) ? 1 : (type+1)   ; colour path -> 1
  4. name   = jt394("8x8d%c%d", letter, arg)    ; e.g. "8x8db1"
  5. l33ac(name, id, 0, 0, &slot[-27894+type*4]) ; LOAD INTO FC POOL GROUP
        - l33ac claims a -18468 binder slot (slot[0]=group id=i+2, slot[1]=kind),
          builds the per-design override name "<base><digit><kind:03d>.ctl";
          base "8x8db" is 5 chars > 4 so on GEMDOS it ALWAYS clips -> faithful
          "no override" -> jt997("8x8db", group) plain-loads 8X8DB.CTL into the
          pool. The 3 wall types share one file -> jt464 DEDUPS to one ~296K
          record bound to 3 groups.
  6. jt107() validate
  7. SYNTHESIS LOOP (items 4..47, step 3, +1 special at 10): if the loaded
     group's item size (jt468(slot[0]) -> JT[1015]=l3834 LBISize) < 16, run
     JT[111] (CODE 6+0x3b1e, near-tile build) + JT[123] (CODE 6+0x3828). COLOUR
     .ctl ships near tiles present (size>=16) so this is SKIPPED in the port's
     path; only the 1bpp .tlb deep path needs it. (Currently a deferred TODO.)
  8. type 0, colour: fill -266[0..255] = id byte; -234[0..36] = type*37 + i + 32
     (the per-set 37-entry CLUT band at base 32 — the port does this differently
     in cw_finalize at 32/64/96).
BLIT consumption (the part that must change to be purge-safe):
  - jt200 folds a wall code -> group; jt114(page, top, left, idx, HANDLE) at
    boot.c:10027 reads -27894+group*4. PORT stores a DIRECT sub-GLIB pointer
    there; FAITHFUL stores a binder-slot ptr and resolves binder -> jt468 ->
    l37aa(base, set) each blit. jt115 teardown (boot.c:4373, jt209 over the 3
    slots) already fits the binder model; the port's raw pointer does NOT.

THE HARD TRUTH (why this can't be a safe partial):
  296K walls (in the pool) + 165K event bigpic + 48K UI = 509K > the 450K
  Mac-sized pool. The port keeps walls in a 320K static buffer OUTSIDE the pool
  precisely to dodge this. Routing them IN regresses the event-picture bigpic
  ("Out of FAR memory") UNLESS the Mac's dispose-the-wall-groups-every-frame
  (L6234 prologue -> l31dc orphan) + l6148 reload (jt464 cache-hit or
  repopulate) is ALSO implemented, so the bigpic can purge the (orphaned) walls
  and the next frame repopulates them. Stages 2 (l33ac load) + 3 (binder/jt468
  blit) + 4 (dispose-reload + purge interplay) are COUPLED — none ships safely
  alone. Verify any attempt with the J200DIFF slot trace (must stay
  byte-identical) + a merchant-event bigpic-still-loads check; the walls are
  already garbled ([[dungeon-3d-render-state]]) so "faithful-but-garbled" is
  hard to tell from "broke it" by eye.
PAYOFF: per-design 8x8db<grp><id> overrides + true purgeable memory (sub-4MB).
  Does NOT fix the garble (separate slot-faithful puzzle). For HEIRS (identity
  dir, no overrides) there is NO visible change — only memory model + risk.

## 2026-06-14e — FC CACHE CORRECTNESS AUDIT (39/39 PASS)
Added fc_cache_audit() (boot.c, probe-only, run from boot_a5_seed_defaults after
the pool self-test). It exercises the REAL lifted FC cache end-to-end on an
isolated 4096-byte scratch pool — saving/restoring the entire live FC A5 state
(9306/10074/10026/10270/9354 + watermarks) around itself, stood up by hand (no
jt463, so the converter registry + live pool are untouched), using group tags 5..8
so jt468's UI shortcut (0/1/24 -> resident ALWAYS/FRAME) never fires:
  T1 fresh-pool invariants (count/cap/free)
  T2 jt464 register (new=0, count, freemap bind, MRU front, jt468 resolve, jt459=0)
  T3 jt467 append + jt459 size + free accounting
  T4 second group + MRU ordering + jt468 base offset + size
  T5 cache HIT (jt464 returns 1) + MRU promotion + jt468 aliasing
  T6 jt459 of an unbound group == 0
  T7 orphan (jt461) + l11ca purge + l103c compaction (survivor size + free intact)
  T8 jt462 drop-in-progress (count--, freemap unbound)
  T9 jt465(NULL) full flush (count 0, freemap all free)
RESULT: "FC AUDIT pass = 39 / fail = 0", boot continues clean immediately after
(save/restore sound). Also confirms the new pool sizing: "GLIB pool: capacity =
460800" = 450K. Repeatable via `make fc-audit` (clean probe build -> run -> grep ->
non-zero exit on any failure). The FC group cache (jt464/jt468/jt467/jt459/l11ca/
l103c/jt462/jt465 + the -9354 MRU) is now a verified, regression-guarded foundation.
