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
| `jt585` | CODE15+0x1a24 | **LIFTED** (L2, picker live but boot-gated) | 29955 | "Save Which Game" A–J picker; stamps position into player rec; `l00e0(fn,jt580)` |
| `jt582` | CODE15+0x153e | **LIFTED** (L2) | 30091 | "Load Which Game" picker; dir-scan (jt990/jt991) → `l143e`→jt579 → restore position |
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
| `jt581` | CODE15+0x1c76 | **STUB** | 23059 | `jt147(&head);jt147(&tail)` list-splice (Mac jt579 uses it) |
| `jt587` | CODE15+0x08e8 | **STUB** | 23061 | no-op helper |
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
3. **jt579 design-state read** — trace + wire the read side (the `-27982`
   design-reload). Closes the design-state round-trip.
4. **jt581/jt587** — lift the two CODE-15 stubs so the port can use the Mac's
   member-splice instead of the cg_pool rebuild.
5. **A–J slot picker wiring** — replace port_save_game/load_game's hardcoded
   slot A with the live jt585/jt582 pickers (needs `g_a5_-28006` populated).
6. **jt159 Load-confirm + boot auto-load + design-select-on-load** — the camp
   Load "abandon game?" gate (boot.c:50949) + a boot path that auto-loads slot A,
   retiring `port_load_savgame`.

Related: [[inventory-subsystem-wall]] (the 398 B record's item serialization is
the same format), [[party-model-migration]] (the `-27928` party list jt579
rebuilds).
