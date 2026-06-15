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
