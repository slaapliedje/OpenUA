# Play-entry chain (task #100) — worklist

Lift the faithful CODE 15/19 play-entry chain and retire the synthetic
scaffold (`port_test_seed_design` + `port_load_savgame`). The chain functions
are already lifted and LIVE; this card-deck is about replacing the *seed*
scaffold with faithful boot + load behaviour.

## The chain (lifted, LIVE, reaches the dungeon)

```
main.c: data_pool_replay → boot_a5_seed_defaults → ua_main
  ua_main:  l4cc0() [2018]  → l4d98() → jt361(1)  → menu loop:
    while (jt315())  {   // main menu (CODE 22)
        l07dc();        // play entry
    }
  l07dc:  if (g_a5_18485==0) l5124();   // game-start init (zeroes -28006)
          port_test_seed_design();
          for (;;) { jt942; l5888; jt918(1) [Training Hall];
                     jt948() [adventure loop → l63c0 → l0bbc] }
```

`jt918` case 10 = `l115a` "Begin Adventuring"; case 8 = `l10ca` → `jt582`
picker → `l143e` "Load Saved Game" (the faithful in-game resume, e160820).

## GROUND TRUTH (2026-06-22, L0bbc one-shot diag, HEIRS save A)

Captured at the FIRST dungeon entry (Begin Adventuring), HEALTHY build:

```
-28006 ptr    = 2045343      g_area_state = 1924110   ← DIFFERENT buffers
p[134] resume = 0            ← FRESH entry, not resume
p[67/68/17]   = 0/0/0        ← real header fully zeroed (l5124)
p[19] level   = 0
g_a5_18485    = 0            ← l5124 ran
g_a5_18878    = 5            ← level rode in via g_port_entry_level
-12288/-12287 = 0/0 (pre-l0bbc)
```

### What this overturns

- **The synthetic `g_area_state` is ORPHANED.** `port_load_savgame` does
  `g_a5_28006 = g_area_state` during `boot_a5_seed_defaults` (BEFORE
  `ua_main`), then `ua_main`'s `l4cc0()` (2018) unconditionally repoints
  `-28006` to a fresh `jt387(1024)` buffer. So `g_area_state` and its sparse
  writes (`[67]/[68]/[134]/[17]`) are dead by play-entry. **"Render off the
  synthetic header" never happened** — the render runs off the l4cc0 buffer,
  which `l5124` zeroes.
- **The live dungeon entry is FRESH, not a resume.** `p[134]=0`, so `l0bbc`
  takes the fresh branch: `jt198(g_a5_18878)` loads the GEO and reads the
  party cell from the **GEO's authored start tile** (`ds + 18488*4`, bytes
  14/15/16). The save's CELL is ignored; only the save's LEVEL survives (via
  `g_port_entry_level → g_a5_18878`).
- `port_load_savgame`'s direct `g_a5_12288/-12287` writes are also dead
  (zeroed by `l5124`, overwritten by `l0bbc`).

### Why the boot-seed header install fails (H1 vs H2, isolated 2026-06-22)

Loading the real header at the seed point needs `-28006` allocated, which
needs an early `l4cc0()`. Test: early `l4cc0()` **with the header memcpy
skipped** STILL corrupts the menu (garbage + SysBeep). So the failure is
**H2 — the early `l4cc0()` reorders the design-buffer allocations** (it
allocates GEO buf, -28006, NCR/STRG, item table, -12300, ... ahead of the
faithful `ua_main` spot, and the idempotent guard then skips the real one) —
**NOT the header bytes (H1)**. Conclusion: the real header can only be
installed POST-`l4cc0` (play-entry), never at the boot seed.

## What `port_test_seed_design` + `port_load_savgame` actually still do

After removing the dead writes from the picture, the scaffold's *live* effect
is just:
1. Seed the design name from `CURRENT.TXT` (stand-in for the `jt315`/`l494e`
   picker, which IS lifted but interactive).
2. Load the roster heuristically (ability-signature scan) instead of the
   faithful `jt579`/`jt577` deserialiser or the menu Load path.
3. Set `g_port_entry_level` from the GAME header / save byte 18.

## Remaining faithful work (the real #100)

`l0bbc`'s RESUME branch (`p[134]!=0`) only restores position — it does NOT
call `jt198`, so the GEO must already be resident. The faithful resume vehicle
is **`l143e`** (jt579 header+pos load → `jt198(h[19])` → portraits → `jt85`),
which works IN-GAME from Begin Adventuring (e160820) but not at the seed
(same H2 / not-ready coupling). `jt579` itself also needs `-28006` allocated:
its 1024-byte header read is guarded `if (player != NULL)`, so at boot (NULL)
it would mis-advance the file pointer and read position/members from the wrong
offset. So the faithful save load MUST run at play-entry.

### Two directions (decision needed)

- **(A) Faithful resume at play-entry.** Keep the boot auto-load convenience,
  but route the resume through `l143e` at play-entry so the party resumes at
  the SAVED cell (real header, `l0bbc` resume branch) instead of the GEO start
  tile. Visible, faithful win; smaller. Recommended first.
- **(B) Retire the boot auto-load entirely.** Boot loads only the design
  (empty roster); the player loads saves via the faithful Load Saved Game
  menu (`jt918` case 8 → `l143e`, already works). Most faithful, but drops the
  one-key HEIRS demo auto-resume and needs the empty-roster boot to be solid.

## Card 1 attempt (faithful roster load) — BLOCKED on the inventory subsystem

User chose "faithful roster load first". Attempt: call the faithful `jt579`
deserialiser at play-entry (post-`l4cc0`, `-28006` live), header+position
snapshot/restored so it's roster-only, heuristic scan as fallback.

Result (2026-06-22, HEIRS save A): **bus error reading $2c (NULL+44) at
`jt577:29275`** — `*(short *)(item + 44)` with `item == NULL` because
`jt477` returned NULL (the -21508 item-node pool exhausted/unready) while
rebuilding a member's `rec[8]` inventory list. The in-game Load path
(`l10ca` → `jt582` → `l143e` → `jt579`) did NOT bus-error on the same save
(timing-dependent), but its actual parse couldn't be confirmed.

This is the **documented inventory-deserialiser blocker**
(docs/inventory-subsystem-wall.md): the shipped 1993 save's records carry
**stale Mac heap pointers** at `rec[8/12/16/20]`; `jt577` rebuilds only
`rec[8]`/`rec[4]` and is pool-timing- + stale-ptr-fragile. The heuristic scan
in `port_load_savgame` exists *precisely* to sidestep this — it nulls every
list head. So a faithful `jt579`/`jt577` roster load **cannot replace the
scan until the inventory subsystem is solid** (jt477 pool-timing for the
`rec[8]` rebuild + the `rec[12]/16/20` re-equip pass).

REVERTED. The common dependency for ALL faithful save-load work (header via
card A, roster via card 2, the in-game `l143e`) is a robust `jt577`. That is
the real unblocker.

## Card 0 (the jt577 unblocker) — DONE 2026-06-22

The `jt577` bus error was NOT a deserialiser bug — it was the `-21508`
item-node pool failing to allocate at 4MB. Root cause: `jt463` (the FAR pool,
`glib_pool_open` in `master_init`, runs BEFORE `l4cc0`) sized itself as
`FreeMem() - 32K` = its full 768KB cap, leaving only ~28KB for `l4cc0`'s
non-purgeable design buffers, so `NewPtr(39680)` for the item pool returned 0
and `jt477` handed `jt577` a NULL node. The Mac's 32K reserve worked because
its design buffers were purgeable Handles. **Fix:** `jt463` reserves 256K
(not 32K). Measured at 4MB: pool drops to 620KB (> the ~461KB dungeon peak),
251KB free, item pool allocates, `jt579`/`jt577` deserialize all 6 HEIRS
members, dungeon + event picture render, no crash. PROVEN that the faithful
`jt579` roster load works once the pool exists (tested at play-entry with a
header/position snapshot — reverted; it's card 2 below, which still needs the
`rec[12]/16/20` work to avoid the sheet-paint crash).

## Cards (re-ordered)

1. (A) Faithful play-entry resume via `l143e` (now unblocked — needs the
   header-timing care from the model finding, + the `rec[12]/16/20` work).
2. Faithful roster load via `jt579`/`jt577` replacing the scan — works now,
   but gate on the `rec[12]/16/20` re-equip pass (inventory wall blocker #2)
   so the char sheet (`jt28(rec[12])`) doesn't bus-error on the stale ptr.
3. Faithful design-load separation (`jt356`/`jt361`, post-`l4cc0`).
4. Save-load as a menu action; retire the boot auto-load (direction B).
5. Retire the synthetic roster seed.

## Discarded approaches

- Load the real header at the boot seed (early `l4cc0()`): corrupts the menu
  (H2, proven in isolation). REVERTED.
- "Render off the synthetic g_area_state": there is no such render path —
  the synthetic is orphaned by `ua_main`'s `l4cc0`.
