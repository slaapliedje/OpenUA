# Resource Manager (FC group cache) — worklist

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
1. **`l338c` is a STUB** (`PROBE("L338c")` only) — it is the bigpic "select
   load kind", called by `l579e` before `l33ac`. Stubbed -> the load kind is
   never set, so the bigpic load fails (the SysBeep alert + blank background
   seen when wiring jt214/jt44). **Lift this first — it unblocks the bigpic.**
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
