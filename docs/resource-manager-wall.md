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

## Lifted already (verify end-to-end)

- `l33ac` (jt110) binder — lifted (comment says "skeleton level 2"; re-audit:
  jt464/jt997/jt1014 are now real, so it may be fuller than the comment says)
- `jt464` group register + MRU cache (-10026 records / -10074 freemap /
  -9354 MRU / -9306 count)
- `jt104` per-file numeric-name callback; `jt987` open+callback driver
- `jt997`/`jt1014`/`jt972` plain-name CTL/TLB loaders
- `jt1013` find-by-numeric-name, `jt1011` size, `jt1016` load-into-pool
- `jt1021/jt1023/jt1024/jt462/jt461/jt465` TLB cache build
- `jt468` group->pool-base resolver (currently PROBE-marked — verify body)

## Worklist (each = one focused commit)

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
