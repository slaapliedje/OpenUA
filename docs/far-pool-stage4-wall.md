# FAR-pool "stage 4" — purgeable dispose/reload (scoping)

The FAR pool (FC group cache, #127) caches GLIB art libraries keyed by group
tag. The Mac runs it as a ~450KB LRU: purgeable groups load on demand and evict
under pressure. The port does NOT evict under pressure yet — it keeps art
resident and papers over it with three stand-ins. "Stage 4" is the missing
dispose/reload orchestration. This doc scopes it.

## Why it matters (the payoff)

The missing dispose/reload is the common root under several symptoms:
- **#129** — the ~165KB event bigpic and the ~296KB wall library can't both fit
  the Mac's 450KB pool (`boot.c:1984-1991`, `2405-2408`: *"collides with a
  resident event bigpic in the 450K pool until the dispose-reload/purge
  interplay (stage 4) lands"*). The port raised the cap to 768KB to hold both.
- **Memory footprint** (port-memory-vs-mac-1mb) — the oversized pool + the
  resident fallbacks are why the port needs >1MB.
- **The 256K design-buffer reserve** (`jt463`, 11fdbdf) — needed only because
  the oversized pool starves `l4cc0`'s buffers.

With stage 4 these all shrink toward the Mac's 1MB:
| stand-in | now | with stage 4 |
|---|---|---|
| FAR pool cap | 768KB | ~450KB (−~320KB) |
| resident `g_wallfile_buf` fallback (`boot.c:2387`) | 327680 B | retired (−320KB) |
| `jt463` design reserve | 256KB | toward the Mac's 32KB (−~224KB) |

## Current state (mapped 2026-06-22)

**FC core — COMPLETE and faithful.** `jt463` open, `jt464` register (cache
hit/miss + MRU), `jt465` flush-by-key (sweeps + `l103c` compacts the matched
records = the DISPOSE primitive), `jt466` close, `jt468` resolve, `jt459` size,
`jt460` append, `jt462` unwind, `l4010` commit/relocate, `jt1016`/`jt972`/`jt403`
read path, `jt1023` list-block. The reclaimer + make-room are lifted:
- `l11ca` (`boot.c:46751`) — one compaction pass. **ORPHANS ONLY**: picks a
  group no freemap entry references (`l3e0c(...) >= 48`) and `l103c`-releases
  it. Random scan order (`jt1083(3)`): by sequence index or by the `-9354` MRU
  table. **Verified against the Mac (CODE_03.s L11ca @0x11ca): both arms evict
  orphans only — the port lift is faithful, the Mac does NOT evict live groups
  here.**
- `l0a6e` (`boot.c:46798`) — `fc_make_room`: loop `l11ca(0)` until `need` tail
  bytes free; on exhaustion logs `"glib: Out of FAR memory!"` and returns 0
  (the load then fails through `jt1016`→`jt462`).

**Reload-on-demand — MOSTLY wired.** Every blit re-resolves
`l37aa(jt468(group), item)` fresh (purge-safe; `boot.c:2513-2523`, the
fc-group-cache checklist). `cw_wallfile_load` already re-issues `l33ac` when
`jt468(group)==0` (purged) (`boot.c:2420-2429`). So a purged group reloads when
next requested.

**The GAP — no explicit dispose at the dungeon↔event boundary.** Because
`l11ca` only reclaims ORPHANS and nothing turns the not-currently-visible
group into an orphan, the working set never shrinks. The Mac purges via
caller-driven `jt465(key)` (remove+compact a named group); the port never makes
that call when transitioning dungeon→event or event→dungeon. The three
stand-ins above exist *instead* of that call.

## CARD 1 RESULT (2026-06-22) — the dispose is ALREADY lifted; stage 4 is mostly verification

Traced the Mac and the port. The "missing dispose orchestration" was a wrong
premise — it is already present and wired:

- **Dispose primitive:** `jt461(tag)` = `g_a5_10074[tag] = 0xFF` (clear the
  freemap entry → unbind → the group becomes the ORPHAN `l11ca` reclaims).
  boot.c:4519-ish. LIFTED.
- **Release a binder:** `jt115(slot)` → `jt461(*slot's group)` + clear slot.
  boot.c:4583. LIFTED.
- **Release the 3 wall binders** `-27894[0..2]`: `jt209`. LIFTED.
- **The orchestration:** `jt131` (= `l035e` = JT[131], the mode-transition
  setter, boot.c:4711) switches on the OLD mode being left: **case 0 (leaving
  the dungeon 3D view) → `jt209(0)` + `jt204()`**; entering mode 4 → `jt209`.
  Verified against Mac CODE_06.s `l035e` @0x035e (case 0 → JT[209]@0x0390 +
  JT[204]@0x0396; mode-4 entry → JT[209]@0x03b8). The port's jt131 is faithful.
- **The trigger:** `l579e` (bigpic loader) calls `jt131(3)` at entry — so
  loading an event bigpic from the dungeon view (old mode 0) fires `jt209` and
  releases the walls before the bigpic loads.
- **Reload-on-demand:** `cw_wallfile_load` (boot.c:2409) reloads via `l33ac`
  when `jt468(group)==0` (purged). WIRED.

**And the faithful wall→pool load now WORKS at 4MB** (post the 11fdbdf reserve
fix): instrumented `cw_wallfile_load` and it logs `FAITHFUL pool load`
(group base non-zero), NOT the resident `g_wallfile_buf` fallback. The reserve
fix didn't just fix the item pool — it freed enough memory that the walls load
into the FC pool. So the whole load→dispose→reload chain is live.

**Why the dispose isn't EXERCISED yet:** at the current 620KB pool (4MB, after
the reserve fix), walls (~296K) + bigpic (~165K) = ~461K both fit (< 620K), so
`l11ca` never needs to reclaim. The dispose machinery only *bites* once the
pool is small enough that they can't coexist — i.e. at the Mac's ~450K.

### Re-scoped cards (the work is verification + shrinking, not new lifts)

1. ~~Find the Mac dispose site~~ — DONE (jt131/jt209/jt461, all lifted +
   faithful; faithful wall-pool load confirmed at 4MB).
2. ~~Shrink the pool to the Mac's 450K~~ — DONE 2026-06-22. `master_init`
   768/640 → 214/450 (both branches). Verified at 4MB with HEIRS: the dispose
   FIRES (`jt131` mode 0→3 on the event → `jt209` WALL DISPOSE, traced), the
   bigpic loads, the walls `(re)load` on return — and `"glib: Out of FAR
   memory!"` never fires (count 0) across dungeon→event→dismiss→walk. The
   intro event picture + the dungeon HUD render with no regression (the black
   3D viewport is the pre-existing wall-decode issue #129, identical at 620K).
   The `jt131` old-mode (`-31234`) bookkeeping is faithful (the caveat is
   resolved). Pool comment at boot.c:1984 updated.
3. ~~Retire `g_wallfile_buf`~~ — DONE 2026-06-22. Converted the 327680-byte
   resident fallback from a static BSS array to a LAZILY-NewPtr'd pointer:
   nothing is allocated unless the faithful FC-pool load ever fails (it doesn't
   at 450K/4MB — card 2 confirmed "FAITHFUL pool load"), and if it ever does
   and NewPtr fails, cw_wallfile_load returns 0 and cw_load_slot bails cleanly
   (no crash). Kept as a best-effort net rather than deleting it, since #129
   (black 3D viewport) blocks visual multi-level verification. MEASURED reclaim:
   FreeMem at master_init jumped 896KB → 1225KB (+329KB ≈ the BSS array); BSS
   dropped ~1276KB → 948KB. Boot + dungeon + event healthy, 0 crash/OOM.
4. ~~Shrink the `jt463` reserve~~ — DONE 2026-06-22: 256K → 160K, sized to the
   MEASURED post-pool footprint (FreeMem 430KB→316KB across L4cc0+L4d98 = ~112KB,
   ~128KB with the intro). NOTE — this does NOT reclaim 4MB memory: with the
   450K cap (card 2) the CAP binds at 4MB (FreeMem−160K = 736K > 450K), so the
   pool is 450K and ~446K is already free for the design buffers regardless of
   the reserve. The reserve now only governs the LOW-memory floor; a tighter
   value keeps the pool nearer 450K when FreeMem is low so the faithful wall
   load still fits the pool (not the resident fallback). The Mac's 32K is too
   small for the port's NON-purgeable design buffers. Verified: 0 crash/OOM,
   item pool + dungeon + event all healthy at 4MB. (The real footprint reclaim
   is card 3 — the resident g_wallfile_buf BSS.)
5. ~~Re-verify #129 + soak~~ — DONE 2026-06-22, and the result is bigger than
   a cross-check: **stage 4 RESOLVED #129's black 3D viewport.** Walking the
   HEIRS dungeon past the intro event, the 3D view renders correctly — stone
   walls in perspective, a wooden door ahead, the night sky + tiled floor,
   the caravan scene sprite — and movement/rotation update it. Cell re-entry
   (the old "event re-fire stomps the wall pool" hazard) no longer corrupts the
   view. 0 OOM/crash across walk/turn/about-face/re-entry. This is exactly what
   [[dungeon-3d-render-state]] predicted: the START frame was corrupted by
   FAR-pool contention with the intro bigpic; the dispose/reload fixed it.
   Measured footprint: FreeMem at boot 896KB → 1225KB (+329KB), pool 768K →
   450K. The lazy g_wallfile_buf is LEFT in place (best-effort net) — deleting
   it outright adds risk for no memory gain (already reclaimed).

## STATUS: stage 4 substantially COMPLETE
Cards 1-5 done. The purgeable dispose/reload is live and verified, the pool is
Mac-sized, ~329KB reclaimed, and the dungeon 3D view renders. A future, broader
multi-level/combat soak could justify deleting the lazy fallback entirely, but
the core goal is met.

CAVEAT to confirm in card 2: that `jt131`'s OLD-mode tracking (`-31234`) is
faithfully maintained so `jt131(3)` from the dungeon sees old==0 and fires
`jt209`. If the port's mode bookkeeping is off, the dispose won't trigger even
though it's wired.

## The faithful mechanism (original scoping — superseded by the card-1 result above)

1. Entering an event that shows a bigpic, the dungeon view is fully covered, so
   the wall groups are not blitted. The Mac DISPOSES them (`jt465` on the wall
   library key → free ~296KB), then `l579e` loads the bigpic into the freed
   space.
2. Leaving the event, the 3D view returns; the wall loader (`l6148`/
   `cw_wallfile_load`) re-runs and reloads the walls — which in turn disposes
   the (now-unneeded) bigpic. No thrashing: walls and bigpic are never needed
   simultaneously, so the working set is always ≤ pool.

`jt465(key)` is the dispose primitive (already lifted). Reload is already
purge-safe. So stage 4 is small in the FC core — it's mostly finding and
lifting the Mac's dispose CALLS in the play/event orchestration.

## Cards

1. **Find the Mac dispose site.** Trace the Mac's dungeon→event transition (the
   `l709e` event dispatcher / the `l442e` bigpic event arm / `l579e`'s caller)
   for the `jt465`/group-dispose call that frees the wall library before the
   bigpic loads (and the reverse on event exit). Confirm whether the Mac
   disposes by key, by group, or by tearing the pool back to the resident
   groups. This is the keystone — everything else follows.
2. **Wire the dispose** at the lifted transition point; keep the reload
   (`cw_wallfile_load`/`l6148` already reload-if-purged). Verify with HEIRS: the
   intro event bigpic loads, then walking returns to the 3D view and the walls
   reload — at a 450KB pool, no "Out of FAR memory", no resident fallback.
3. **Shrink the pool** back to the Mac's cap (`master_init(... ,214,450)`),
   confirm the faithful wall path (not `g_wallfile_buf`) carries the dungeon.
4. **Retire `g_wallfile_buf`** (`boot.c:2387`, 327680 B) once the faithful
   reload is proven across HEIRS levels.
5. **Shrink the `jt463` reserve** from 256K toward the Mac's 32K (re-measure the
   4MB budget — with a 450KB pool there's ~300KB more headroom).
6. **Re-verify #129** (no bigpic/wall collision) and measure the footprint
   toward the 1MB target.

## Risks / subtleties

- **Reload performance.** Reloading the ~296KB wall library on every event-exit
  could be slow on a 16MHz 030. The Mac accepted this (purgeable). Measure;
  the FC cache de-dups by filename, so a re-`l33ac` of the same library is a
  file re-read. If too slow, that's a perf card, not a correctness blocker.
- **Lock-while-blit.** A group must not be disposed mid-blit. The port's
  re-resolve-every-blit (`l6148`) makes eviction-between-frames safe; the
  dispose calls must land at transition points (mode change), never mid-render.
- **Don't dispose resident groups.** Groups 0 (ALWAYS) / 1 (FRAME) are resident
  (`fc-group-cache` residency table); `jt465(key)` must target only the
  purgeable wall/bigpic keys, never the UI libraries.
- **Design-buffer reserve interaction.** Card 5 must keep `NewPtr(39680)` for
  the item pool working (the 11fdbdf fix) — shrink the reserve only as far as
  the 4MB budget allows.

## References
- docs/play-entry-wall.md (the 11fdbdf reserve fix that exposed this)
- fc-group-cache + resource-manager (MCP `frua-reference`): the purge/reload
  discipline, the residency table, `jt468`/`l37aa` purge-safe resolution.
- Mac disasm: CODE_03.s L11ca @0x11ca (orphan-only reclaimer), L0a6e @0xa6e.
