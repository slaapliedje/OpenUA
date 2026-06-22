# Save / Load subsystem — findings + worklist

Goal: a faithful round-trip of a full FRUA save — the adventuring **party** *and*
the **design-state** block — through the real CODE-15 serializer, with the A–J
slot pickers and the load-confirm modal, retiring the `port_load_savgame`
stand-in. Party-only round-trip already works (#141); the design-state block is
the main gap.

Layer: the serializer core is **CODE 15** (jt577–jt587). Char-gen review/finalize
(jt570–jt573, CODE 17) is a *separate* concern and **not** part of save/load.

## What renders / runs today (traced, confirmed)

Two live entry points converge on the same CODE-15 core:
- **Camp menu (faithful):** `jt957` (camp dispatcher, boot.c:54544) case 6 →
  `jt585()` save (boot.c:54636) + `jt159` confirm; case 5 → design-reload load.
- **Training-Hall menu (stand-in):** `l1142` (boot.c:50848) →
  `port_save_game()` / `port_load_game()` (boot.c:28892/28900) — these **bypass
  the A–J picker and hardcode slot A**.

### Status table

| Fn | jumptable | Status | boot.c / asm | Role |
|----|-----------|--------|--------------|------|
| `jt585` | CODE15+0x1a24 | **LIFTED + Hatari-verified** (A–J pick → 10284B save) | 29955 | "Save Which Game" A–J picker; stamps position into player rec; `l00e0(fn,jt580)` |
| `jt582` | CODE15+0x153e | **LIFTED + Hatari-verified** (A–J pick → party restore) | 30091 | "Load Which Game" picker; flat-dir glob (jt990/jt991) → `l143e`→jt579 → restore position |
| `jt580` | CODE15+0x182c | **LIFTED** (party block + design-state pad, asm L19ca) | 28806 | Write player rec + position/state + count + per-member + pad-to-10284 |
| `jt579` | CODE15+0x124c | **LIFTED** | 28793 | Read mirror of jt580; rebuilds party `-27928` |
| `jt578` | CODE15+0x0934 | **LIFTED** | 28936 | Write one 398-byte record + inventory items + container sub-items + spells |
| `jt577` | CODE15+0x03fe | **LIFTED** (record + inventory round-trip self-test PASS) | 29118 | Read mirror of jt578; container capacity guard (jt903, 120-slot cap) |
| `jt584` | CODE15 (chargen) | **LIFTED** | 29198 | Save one character to a named `.cch` ("Update %s?" via jt159) |
| `l00e0` | CODE15+0x00e0 | **LIFTED** (port-adapted) | 28845 | Write opener: FSDelete+Create('FRUA','SAVE')+FSOpen → serializer cb |
| `l143e` | CODE15+0x143e | **LIFTED** (port-adapted) | 30044 | Read opener: FSOpen → jt579 |
| `l0ce0_c15` | CODE15+0x0ce0 | **LIFTED** | 28914 | Endian-swap the record's multi-byte fields native↔little-endian |
| `jt990`/`jt991` | CODE5+0x1b76/0x1cb6 | **LIFTED** (GEMDOS Fsfirst/Fsnext) | 23092/23112 | SAVE-dir enumeration for the picker |
| `jt159` | CODE7+0x16ea | **LIFTED** | 28594 | Yes/No confirm modal (via jt182) |
| `l005a` | CODE15+0x005a | **LIFTED** (always true) | ~28685 | Mac "insert save disk" — HD always present |
| `jt412` | CODE3+0x3888 | **LIFTED** | 42265 | GetFPos/tell — used by the design-state pad math (asm only today) |
| `jt581` | CODE15+0x1c76 | **LIFTED** | 23174 | `jt147(&head);jt147(&tail)` list-splice (Mac jt579 uses it) |
| `jt587` | CODE15+0x08e8 | **STUB — keep (port reimplemented)** | 23176 | Mac: jt399(dest,398,0) + (if -22733==1) `l08ba_c15(name,dest)` + jt21 + jt910 — a file-based "reset+reload a party slot from its .cch". The port replaced this with the in-memory jt590/cg_pool party splice (boot.c:27872), so the lone caller's `jt587(fresh_slot, matched)` is **vestigial** — faithfully lifting it would zero `matched` + open a record as a filename. Do NOT lift; the jt477/jt165/jt587 trio is dead next to jt590 |
| `l08ba_c15` | CODE15+0x08ba | **LIFTED** | 29412 | Load a `.cch` named `name` into `rec` (jt384 + l_cch_read; replaces Mac L0006). jt587's case-1 loader |
| `port_save_game`/`port_load_game` | — | **STAND-IN driver** | 28892/28900 | Bypass picker; drive jt580/jt579 over fixed slot A |
| `port_load_savgame` | — | **STAND-IN** | 15810 | Non-faithful BasiliskII `SAVGAMA.CSV` party scanner (bring-up roster) |

> CORRECTION to prior notes: **jt570/jt571/jt572/jt573 are char-gen action
> procs/finalizers (CODE 17), NOT save/load.** jt573 = char review (Save/Cancel
> → jt593); jt572 = race/class finalize commit. The actual *character* `.cch`
> save is `jt584`. The game-save serializer core is jt577–jt580 in CODE 15.

## Data model — what's in a save

**On-disk `SavGam<A..J>.csv` = 10284 bytes** (type/creator `'SAVE'/'FRUA'`; the
FSOpen shim flattens the Mac `<design>/SAVE/` HFS path to the staged dir,
boot.c:28837):

1. **player record** — 1024 B (from `g_a5_-28006 +1`)
2. **position + state block** — `-12288` row/col/facing (5 B), `-27989` (1 B)+pad,
   `-27990` (1 B)+pad, 2 reserved shorts (4 B)
3. **party count** — 2 B little-endian, ≤8 (via jt1180)
4. **N × per-member `.cch` block** (jt578)
5. **design-state pad** — asm jt580 L19ca (CODE_15.s:2093) does `jt412`(tell);
   if pos < 10284 writes `(10284 − pos)` bytes from **`g_a5_-27920`** — the
   item-record **template table** (`jt387(4590)`, boot.c:1210; 255 × 18-byte
   item records, boot.c:51770). **This is the TODO at boot.c:28783** — the port
   writes no pad, so its files are short of 10284.

**Per-character `.cch` record (jt578/jt577) = 398 bytes** (asm `#398`/0x18e):
- swapped fields: word@82, words@76/78/80, longs@68/72, words@84/86 (l0ce0_c15).
- inventory list: head `rec+8`, `.next` at item`+0`; 18 B from item`+40`, words
  `+44/+46` swapped; container (`item[40]==73/'I'`) → `item[53]` sub-items at `+58`.
- spell list: head `rec+4`, 10 B/node, word `+2` swapped, `.next` at `+6`.

> Open: asm jt579 (L124c) reads player+position+count+members only — it does
> **not** read the design-state pad. So the read side of `-27920` lives in the
> **design-reload** path (camp Load sets `-27982=1` to trigger it), not in jt579.
> Must trace where `-27920` is repopulated on load to close the round-trip.

## What works vs stub/missing/stand-in

- **Works (Hatari-verified, #141):** faithful party round-trip — player rec +
  position + state + count + per-member 398-byte records with inventory & spells;
  endian handling; dir scan; file shim; jt159 confirm. Reachable from both menus.
- **Stub/missing:** the **design-state ~10KB block** (jt580 TODO); jt581/jt587;
  boot auto-load; design-select-on-load; jt159 "abandon game?" gate on camp Load
  (boot.c:50949 skips it). The A–J pickers are lifted but bypassed (slot A only).
- **Stand-in:** `port_load_savgame` reads a BasiliskII `SAVGAMA.CSV` and
  heuristically scans 398-byte records (printable name@96, abilities@112 ∈ 3..19,
  maxHP@82), dedups by name, restores **party only** — the bring-up roster source.

## Blockers + open questions

1. **Design-state block (the ~10KB) is the main blocker.** Source confirmed =
   `g_a5_-27920` template table, padded to 10284 in jt580 (asm L19ca). The
   **read** side isn't jt579 — find where the design-reload re-ingests it.
2. **A–J picker live-exercise** needs a populated player handle `g_a5_-28006`;
   jt585 is NULL-guarded and not boot-reachable (gated on `g_a5_-14429`, @29950).
3. **jt581 splice vs cg_pool rebuild:** Mac jt579 splices loaded members via
   jt581/jt147; the port rebuilds via cg_pool/jt590 — confirm member ordering
   matches for a faithful round-trip.
4. **jt577 untested** — needs a Hatari `.cch` round-trip against jt578.
5. Shim flattens `<design>/SAVE/` — slot files aren't design-namespaced; confirm
   no cross-design collision.

## Plan (smallest-first, each independently testable)

1. ~~**jt580 design-state write tail**~~ — **DONE** (asm L19ca: `jt412`(tell) +
   `jt410(-27920, 10284-pos)`). Saved slot is now byte-length faithful (10284,
   Hatari-verified). `-27920` over-allocated to 10284 so the pad read stays
   in-bounds (Mac over-reads into adjacent design-state heap; port's heap isn't
   laid out the same — pad content is non-meaningful, the load path ignores it).
2. ~~**jt577 round-trip test**~~ — **DONE** (boot self-test, `#ifdef
   FRUA_ENGINE_PROBE`): the empty-record test plus a new one-inventory-item
   round-trip (jt578→jt577) — exercises jt577's pool-allocating inventory loop +
   the +44/+46 swap (the empty test skipped both). PASS Hatari-verified. (Spell
   list still un-exercised — a trivial follow-up: rec+4 head, 10 B/node.)
   GOTCHA captured: the `-21508` item pool is null until l4cc0 (design-load), so
   the boot self-test inits it first.
3. ~~**jt579 design-state read**~~ — **INVESTIGATED → no pad read exists.** asm
   jt579 (L124c, lines 1486–1727) reads player+position+count+members then does
   post-load setup (jt56 portraits, jt214/jt951/jt952 play-mode dispatch) — it
   does **not** read the 10284-byte design-state pad, and jt582→l143e→jt579 so
   the picker doesn't either. The pad is **write-only** (fixed slot length). The
   design-state *progress* restores via the `-27982` level/design **reload from
   the `.DSN` files**, a separate subsystem — NOT the save-slot pad. So the
   party+position round-trip is already complete; there is no pad read-side to
   wire. (Design-progress persistence = a separate, larger future card: trace the
   Mac `-27982` reload + whether play mutates the `.DSN`/GEO files.)
4. **jt581/jt587** — lift the two CODE-15 stubs so the port can use the Mac's
   member-splice instead of the cg_pool rebuild.
5. ~~**A–J slot picker wiring**~~ — **DONE 2026-06-21, Hatari-verified
   end-to-end.** The Training-Hall Save (l1142) drives the faithful `jt585`
   **"SAVE WHICH GAME A B C D E F G H I J"** picker; pressing **A** saves a
   byte-faithful **10284-byte SavGamA.csv** (6 HEIRS members, all names present,
   count word `0600`) and returns to the Hall.

   ROOT CAUSE (corrects the prior "rewrite jt585's loop to the DLItem-selection
   model" hypothesis — that was **wrong**). The Mac jt585 (CODE15 0x1a82–0x1ace)
   uses `jt182()`'s return **directly** as the 0-based slot index, and the port's
   jt585 already matched it — EXCEPT the loop-break test was **inverted**. Mac
   `0x1a94 cmpiw #10; bcss` exits the loop on a *valid* pick `slot < 10`; the port
   wrote `if (slot_idx >= 10) break;`, so a valid pick (0–9) never broke — the
   modal just re-armed and waited for more input. Fix: `if (slot_idx < 10) break;`
   + drop the scaffold `iter_guard` (the Mac loops unbounded; jt182/l23b4 block
   each pass). The whole resolution chain was already correct and is proven by a
   live trace: `jt179(9)` seeds `-24126[i*2]=i`; `l2184("A B …J")` fills the
   accelerator codes (`-24126` = {0:'A'…9:'J'}); l23b4 blocks, returns the matched
   item; **l25b6 mode = 9 → the fallback** reads the matched label char ('A'=65)
   and returns slot **0**. (The earlier "l25b6 maps to a control code by mode" was
   a *static inference* about modes 1/4/…; the save picker runs in mode 9 → the
   accelerator fallback, which returns the slot index.) The picker prompt slots
   `g_a5_-13776`="A B C D E F G H I J" / `-13904`="save which game: " are
   DATA-pool-seeded and correct at boot.

   SECONDARY FIX (newly exposed by the now-working save path): `port_load_savgame`
   (the bring-up HEIRS loader) `memcpy`s each 398-byte record raw but does **not**
   reconstruct the inventory/spell node lists, leaving stale Mac heap pointers in
   the list-head fields (spell @4, inventory @8). jt578's faithful list walk
   followed member 2's garbage head → bus error mid-save. Fixed by nulling
   `dst+4`/`dst+8` after the memcpy (empty lists = the honest representation of
   what the stand-in loads; also protects jt903 / the inventory screens — likely
   the same root cause as the [[inventory-subsystem-next]] "rec[12] stale-ptr bus
   error"). The faithful jt579/jt577 load path already zeroes the slot, so it was
   never affected.
6. ~~**jt582 LOAD picker + jt159 load-confirm**~~ — **DONE 2026-06-22,
   Hatari-verified.** Training-Hall **Load Saved Game** now runs the faithful
   chain: jt159 **"Game not saved. Load anyway?"** confirm (when an unsaved game
   exists) → clear party → **jt582 "LOAD WHICH GAME A…J" picker** → pick A →
   l143e→jt579 reads the 10284B slot → the 6 HEIRS members restore. Three fixes:
   (a) **menu-case routing** — the JT[3] table (CODE12 0xefc, decoded) maps case
   8 = L10ca (the LOAD handler: jt159 + jt582) and case 11 = L120c (Exit); the
   port's `k_jt918_menu_items` had "Load"/"Exit" at the wrong slots (8↔11), so
   "Load Saved Game" reached L120c's slot-A stand-in. Swapped the two rows (the
   `phrase` y-id travels with each label, so positions stay) — mirror of the
   earlier 9/10 Save/Begin swap. (b) **flat-dir enumeration** — jt582 built the
   Mac `<design>:SAVE` HFS path; the port stages saves flat, so it now globs
   `SavGam*.csv` via case-insensitive Fsfirst (jt407 made case-insensitive,
   slot-letter upper-cased). (c) **L120c made faithful** (Mac 0x120c: gated
   jt159(-14284) + jt585) instead of the port_load_game load stand-in.
   GOTCHA: L10ca clears the party *before* jt582 (faithful), so a Load with **no
   slot present** leaves an empty roster — only press Load when a save exists.

7. **Faithful design-reload (lift the full L143e) → then retire port_load_savgame.**
   INVESTIGATED 2026-06-22. The "design-reload" the boot auto-load needs is the
   **post-load dungeon restore inside the Mac `L143e`** (CODE15 0x143e), which the
   port has as a bare `FSOpen+jt579` STUB (boot.c:30215). The faithful Mac L143e
   (disasm 0x143e–0x153c) does:
   ```
   -6924 = h[58]; -22218 = -6923; saved_lv = h[19];   // h = g_a5_-28006
   L0006 -> jt579(file)                                 // header->-28006, pos, party
   for (m = -27932; m; m = m->next)                     // reload portraits
       if (m[147] < 128) l03d2(1);                      //   jt56("CBODYS", m[188], m[189])
       else jt56("CPIC", m[181], m[189]);
   -27932 = -27928;
   h = -28006;
   if (-18485) { h[19]=saved_lv; h[49]=0; h[133]=0; }   // overland
   else        jt198(h[19]);                            // DUNGEON: reload the GEO for the saved level!
   if (h[19] <= 4) { jt951(); jt214(); }                // overland play setup
   else            jt952();                             // dungeon play setup
   if (-6924 != h[58]) jt85(h[58]);                     // wall group / palette refresh
   ```
   **The level is design-header byte 19** (`h[19]`); `jt198(h[19])` reloads the
   GEO; `jt952` sets dungeon mode. This is the whole "reload the design/level from
   the save" chain — it replaces port_load_savgame's hand-rolled level/GEO/resume
   tail (boot.c:16061) entirely.

   **4 missing deps — all small, sub-deps all present:**
   - `jt951` (CODE20 0xb88, 26B): `h[34]=0; h[36]=1; -27990=3` (overland mode).
   - `jt952` (CODE20 0xba2, 26B): `h[34]=1; h[36]=0; -27990=4` (dungeon mode).
   - `l03d2` (CODE15 0x3d2, 44B): `jt56("CBODYS", (-27932)[188], (-27932)[189])`.
   - `jt85`  (CODE6  0x6ada, 108B): wall group/palette manager — cache in `-13040`;
     on change loads "frame" group (jt997/jt468/jt993, all lifted). Deps jt1200/
     jt1163/jt468/jt993/jt997 all exist.
   Use `g_a5_byte(-6924/-6923/-27986)` / `g_a5_word(-13040)` directly (no macros).

   **Integration is faithful (verified by reading l10ca):** the Training-Hall Load
   (l10ca → jt582 → L143e) stashes the loaded play-mode (`-27989 = -27990; -27990
   = 0`) for the subsequent Begin-Adventuring, so L143e SHOULD do the full
   jt198/jt952 dungeon setup mid-load. TEST HARNESS: the now-working Training-Hall
   Load — after the lift, Load slot A should reload the GEO + set dungeon mode;
   verify Begin Adventuring resumes at the saved level/cell, and the Save→Load
   round-trip still works. RISK: jt198 (GEO load) fires mid-Training-Hall; the Mac
   does it unguarded — HEIRS save targets level 5 (GEO005 ships) so it's safe, but
   a foreign save level would jt69-fatal (port_load_savgame guards this; decide
   whether to keep a GEO-exists guard in the port L143e).

   **THEN retire port_load_savgame:** with L143e doing the faithful resume, the
   boot auto-load becomes `l143e("SavGamA.csv")` (loads party + reloads the
   dungeon). port_load_savgame's heuristic scan + manual resume tail both go.
   COUPLING NOTE: jt952/jt951 are also the faithful play-mode setup the port's
   l07dc/l63c0 entry reimplements — full convergence is part of task #100.

8. **boot auto-load + design-select-on-load** — a boot path that auto-loads
   slot A (retiring `port_load_savgame`) + the camp Load "abandon game?" gate
   (boot.c:50949) + design-select-on-load. The case-sensitivity of the *shipped*
   `SAVGAMA.CSV` vs the port's `SavGamA.csv` is now handled in the picker, but a
   cross-design save namespace (the shim flattens `<design>/SAVE/`) is still open.

Related: [[inventory-subsystem-wall]] (the 398 B record's item serialization is
the same format), [[party-model-migration]] (the `-27928` party list jt579
rebuilds).
