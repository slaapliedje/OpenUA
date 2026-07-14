# Inventory / item-list subsystem — findings + worklist

Goal: show each character's equipped items on the sheet (BasiliskII reference:
BARBARUS shows **LONG SWORD +1** / **PLATE MAIL +1** below DAMAGE), then make
the sheet's **ITEMS / TRADE / DROP** buttons functional. All of it reads the
same reconstructed item lists.

## EQUIPPED-ITEM DISPLAY — FIXED 2026-06-26 (held pending Hatari test)

The whole load+name chain was already faithful and WORKING — a runtime
diagnostic (INVTRACE) proved every member's `rec[8]/12/16/20` populate after
the menu Load (jt579 -> jt577 deserialize + jt21 categorise), and `jt28` built
the correct names ("Long Sword" + "+1", "Plate" + "Mail" + "+1") from the
`-13628` name-part table. The ONLY break: `jt28` paints via `l3fd6`, and the
port had `l3fd6` as a **bogus duplicate PROBE stub**. `l3fd6` IS `JT[94]` (both
= CODE 6 + 0x3fd6, the engine text painter) — already fully lifted as `jt94`
(155 call sites). The Mac jt28 ends with a same-segment `jsr L3fd6`; the port
called the stub, so names were computed but never drawn. **Fix:** `l3fd6` now
forwards to `jt94` (`jt94(a,b,c,d,"%s",out)`). LESSON: when a CODE-local
`lXXXX` shares an address with a `JT[N]` export, it's the SAME function — don't
stub it; alias to the lifted `jtN`. (Same class as the jt-aliases trap.)
Remaining below = the ITEMS/TRADE/DROP handlers; the display is done.

## What renders the equipment (traced, confirmed)

The char-sheet painter `l1276` (= jt886, CODE 19+0x1276) ends with:
```
jt898 (l19ac)            money/exp panel
jt892 (l1abe)            AC/THAC0/Damage/Encumbrance/Movement panel (ends at Damage row 19)
if (rec[12] != 0) jt28(rec, rec[12], slot 21)   <-- EQUIPPED items (rows 20/21)
if (rec[20] != 0) jt28(rec, rec[20], slot 22)   <-- effects
```
- `jt28` (CODE 6+0x0dc6, `a5@(258)`) is the list/item painter and **is lifted**.
- The port's `l1276` comment calls rec[12] "memorised spells" — that's WRONG.
  BARBARUS is a Fighter (no spells) yet the Mac paints weapon+armor via slot 21,
  so **rec[12] = the equipped-items list**. Spells are rec[4] (see jt577).

## Why nothing shows / why it crashes

`port_load_savgame` just `memcpy`s the 512-byte record. So:
- `rec[8/12/16/20]` are **stale Mac heap pointers** (e.g. BARBARUS rec[12] =
  bytes [246,8,147,87]). The item nodes are never deserialized.
- The sheet *did* render before only because the synthetic test party had
  rec[12]==0 (skipped). With the real party, rec[12]≠0 → `jt28` walks a garbage
  pointer. (Empirically: rebuilding rec[8] but leaving rec[12] stale → **Bus
  Error reading at $20** during the sheet paint.) So rec[12]/16/20 MUST be
  cleared and/or rebuilt before the sheet draws.

## ITEMS / TRADE / DROP handlers — MAP (2026-06-26, jt904 submenu)

The View-character popup (jt182, labels from g_a5_-13804 = STRS 0x1db4 =
"Items Spells Trade Deposit Drop Lay Cure Exit") dispatches (JT[3] @ 0x232a):

| slot | label | handler | identity | size |
|---|---|---|---|---|
| 0 | Items | l25ce(out_done) | item browser + per-item sub-menu (JT[3]@0x28f8: Rdy/Use/Trade/Deposit/Drop/Halve/Join/Sell/Id/Exit) | 558 insns |
| 1 | Spells | jt595 | CODE-16 spell handler (lifted) | — |
| 2 | Trade | l4334 | trade items between chars | 236 |
| 3 | Deposit | l46e0(1) | deposit coins to vault (-27990==10) | 273 |
| 4 | Drop | l46e0(1) | drop coins (same arm, mode toggle) | (273) |
| 5 | Lay | l4f2c(char) | Paladin Lay-on-Hands (NOT import/export — fix that comment) | 58 |
| 6 | Cure | l4ff6(char) | Paladin Cure-Disease | 116 |
| 7 | Exit | — | loop ends | — |

MISSING helpers to create: l596a (target "…whom?" picker, shared by Trade/Lay/
Cure), l4264 (Trade), l35a0 (~1.5KB item-Join). Stub JTs in the arms: jt71/72/79
(item helpers), jt147, jt189 SELL/jt190 ID, jt423/jt483 (string width/len —
should alias l39ae). l4e56/l4ec6 gates are real (= Paladin ability checks).

CORRECTIONS to the agent map (verified 2026-06-26):
- `l3fd2` is NOT missing — it IS `jt891` (CODE 19 + 0x3fd2 = entry_jt891), the
  numeric "How much…" entry dialog, ALREADY LIFTED (boot.c ~63205, already
  called at 63345/63755). One less thing to lift.
- `l3f16` (treasure-type classifier: 'G'->Gems/1, 'P'/'S'->Platinum/0,
  'J'->Jewelry/2; writes the prefix, returns the code) LIFTED 2026-06-26
  (boot.c, before the l25ce stub block; marked unused until l46e0/l4334 call it).
  JT[1] @ 0x3f58 decoded with tools/jt1_extract.

SHARED-HELPERS lift status: l3fd2=jt891 ✓ (already done), l3f16 ✓, l596a ✓
(2026-06-26). jt153/jt162 turned out REAL one-liners (set/get -13014), not
stubs — the agent mislabeled them. l596a sig = l596a(prompt, flag1 cancellable,
flag2->-24148, long *member_io); nav left/up=next(wrap head), down/right=prev
(wrap tail), Return confirm, ESC cancel; loop key UNSIGNED (init 0xFF).
Still MISSING: l4264 (Trade), l35a0 (~1.5KB Items-Join).

HANDLER lift status: l46e0 (Drop/Deposit) ✓ LIFTED 2026-06-26 (boot.c ~31279).
Full lift of CODE 19+0x46e0: builds a "name<pad>amount" row per non-empty coin
slot rec[76+i*2] (jt477 node from -21156, jt59 number, 18-col space-pad loop,
jt488 "%s%s%s"), runs jt179+jt169 list dialog, re-derives the coin type from the
row (l3f16) for the prompt (-27990==10 vault: len>7 "%sdo you deposit?" / Gems
"How many %swill..." / else "How much %swill..."; else "How much %swill you
drop?"), reads the amount (jt891, capped) and transfers (l465c). Loops until the
wallet is empty or cancelled (jt169 node 0 / key 1 / key 27 with -24139). Sibling
of the party-pool jt924 (works -25314 instead of rec[76+]). WIRING was already
faithful: jt904 JT[3]@0x2326 sends BOTH case 3 (Deposit) and case 4 (Drop) to
L236a = l46e0(1); l19ac() — l46e0 picks deposit-vs-drop text internally, so the
existing `case 3: case 4: l46e0((short)1); l19ac();` matches the asm verbatim.
l4334 (Trade) + l4264 ✓ LIFTED 2026-06-26 (boot.c ~31259). l4264(src_pp,dst_pp,
amount,coinType): capacity-guards the recipient (jt901 low word vs rec[86]+amt ->
"Overloaded" jt42/jt102/jt66), then deducts src rec[76+type*2]+weight (jt897) and
credits dst+weight (jt883). l4334 outer-loops a partner pick (l596a, prompt
-14360), inner-loops the active char's coin list (same build as l46e0 but jt423
widths + -14388/-14356 prompt fragments + l4264 transfer to &partner). Coin-list
cancel backs out to partner pick (fp@-119 outer / fp@-120 inner done flags; Mac
left -119 uninit, loop intends 0). jt904 case 2 already dispatches l4334().
l25ce / JT[893] Items browser — DISPATCHER ALREADY LIFTED (prior session, boot.c
jt893 ~64308): the full 558-insn browse/menu(l11a8)/jt169-dialog/JT[3]@0x28f8
loop + BOTH inline arms (case 3 drop-into-vault, case 4 trade/give) + the
l23d2_c19 "may this item be parted with?" gate. The 7 per-arm sub-helpers are
PROBE stubs: l30bc examine, l3b6e ready/unready, l3228 use (+l4c9a MISSING),
l32c4 halve/split (~634B), jt889=L35a0 join bundle (~1.5KB), jt189 sell (CODE7),
jt190 identify (CODE7).

CONTEXT TRAP (2026-06-26): jt893 is IN-GAME ONLY. Its two real callers are camp
(jt185 case 4) and combat (case 2) — contexts with an active area. Wiring it to
the Training-Hall View-Character popup (jt904 case 0) BUS-ERRORED: the menu-build
"Ready" arm does `p = *(long*)(rec+64); if (p[2])` and rec[64] is a TRANSIENT
in-combat pointer that's stale (garbage, non-NULL) for a Hall char freshly loaded
from a save — reached because BARBARUS has a readied weapon (rec[382]!=0) and the
mode (-27990) is 0 in the Hall. (-28006 is NOT the culprit; it's a boot-allocated
1024B header.) REVERTED jt904 case 0 to the safe l25ce no-op stub. To offer Items
faithfully from the Hall, the char load must CLEAR the transient record pointers
(rec[64] and friends — same class as the rec[12]/16/20 stale-ptr fix), OR confirm
the Mac's Hall popup omits Items. The in-game Items browser (camp/combat) is fine.
ARM LIFTS (2026-06-26/27): 4 of 7 done —
  - l32c4 Halve/split ✓ (d0dfb8a)
  - l3228 Trade-item + l4c9a fit-check ✓ (92907ab)
  - jt190 shop Identify ✓ (4de112d)
  - jt189 shop Sell ✓ (af44157)
ARM 5 (Ready/examine, case 0): L30bc = JT[882] was ALREADY LIFTED as jt882 (+ its
slot validator L2f6e = the lifted l2f6e). DONE 2026-06-27 (995017a) by repointing
jt893 case 0 from the dead l30bc stub to jt882 (no new lift). LESSON: l30bc/l2f6e
were duplicate-trapped — check for the JT-export name (jtNNN) before lifting an
lXXXX arm; same as l25ce=jt893.

ARM 6 (Join, case 6): jt889/L35a0 ✓ LIFTED 2026-06-27 (4302108) + helper
l3540_c19 (party scroll counter, named to dodge the CODE-13 l3540 collision).
Two paths: scroll/bundle convert+splice and generic 255-cap stack-merge. Same
commit FIXED l32c4's reversed jt406 src/dst (Mac JT[406]=BlockMove(src,dst) copies
arg1->arg2 per the L57f8 core; port jt406(dst,src) is arg-swapped, so Mac
jt406(A,B) -> port jt406(B,A)). JT406 DIRECTION LESSON: always map Mac
jt406(last-pushed, 2nd-to-last) -> port jt406(dst=2nd-to-last, src=last).

ARM 7 (Use, case 1): l3b6e ✓ LIFTED 2026-06-27 (8ec4367) + its lone missing dep
jt496 (CODE 13+0x276c, the in-combat map refresh: jt77/jt527/jt521). l3b6e's JT[1]
@ 0x3dae effect-index remap (57->0x7f, 59-65->0x80-0x86, 95/97/99->0x87-0x89)
decoded with tools/jt1_extract. Special-item usability roll matches the asm
exactly (fumble when jt40(6)<=9 OR 1d100>75; jt40(6) always evaluated, jt870
short-circuited).

*** ITEMS BROWSER COMPLETE + LIVE EVERYWHERE (2026-06-27) ***
  Ready/examine = jt882 (wired) | Use = l3b6e (+jt496) | Trade = l3228 (+l4c9a) |
  Halve = l32c4 | Join = jt889/l35a0 (+l3540_c19) | Sell = jt189 | Identify = jt190.
The full Items browser (jt893 dispatcher + all 7 arms + both inline drop/give
arms + l23d2_c19 gate) is lifted and wired into jt904 case 0 (View-Character
"Items"), camp (jt185 case 4) and combat (case 2).

CONTEXT-TRAP RESOLVED (2b88fc7): the earlier Hall bus error was NOT stale rec[64]
— it was a menu-build LIFT BUG. L25ce's "Ready" arm (0x263a) dereferences rec[64]
(a live combat pointer) ONLY when -27990==5: modes 2/3/4 add the arm
unconditionally, mode 5 adds it iff rec[64]->[2], all else skip. The prior C lift
inverted it (deref when mode NOT in {2,3,4,5}), so the Hall (mode 0) faulted.
Fixed to match the asm; rec[64] is now never dereferenced outside combat, so NO
clear-on-load is needed and jt904 case 0 routes to jt893 in every context.
The whole inventory subsystem is now done.
ALSO PENDING: clear rec[64] (and sibling transient ptrs) on char load to safely
re-wire jt904 case 0 -> jt893 for the Hall (see CONTEXT TRAP above).

LIFT ORDER (cheapest-first per the map): l596a -> l4f2c Lay -> l4ff6 Cure ->
l46e0 Drop/Deposit (+l3f16/l3fd2) -> l4334 Trade (+l4264) -> l25ce Items (+l35a0).
The "inventory" trio the user wants = l25ce/l4334/l46e0. jt904 dispatch is
already lifted (boot.c ~59789); only the handler BODIES are stubs (31231/31485).
Forward-decl note: handlers sit at ~31231 but many callees are defined later —
forward-declare or move the bodies down near jt904 (~59600).

## The item serialization (same as the .cch format jt577 reads)

After the 398-byte fixed record, `rec[193]` item nodes are serialized inline
(this is why SAVGAMA records are variable-length). Confirmed for BARBARUS:
`rec[193] = 6`; bytes at rec+398 = `41,0,111,41,100,0,244,1,…` (the first item).
NIVLOC = 4 items.

`jt577` (.cch reader, CODE 15+0x3fe, port @ ~29035) is the faithful template:
- per item: allocate a **62-byte node** from the `-21508` pool (`jt477`), copy
  the **18-byte on-disk record to node+40**, byte-swap the words @44/@46
  (`jt1180`), clear node+0 (next) and node+58 (sub-list).
- `item[40] == 73` ('I') = a container holding `item[53]` sub-items, linked
  through node+58.
- spells: a separate list at **rec[4]** (10-byte nodes linked via node+6).

A standalone in-memory deserializer (mirroring this, reading from the record
copy instead of a file) was written + **validated** (parsed 6/4 items correctly)
but reverted because of the two blockers below.

## Blockers (must solve before wiring)

1. **Pool timing — ROOT CAUSE FOUND + FIXED (2026-06-22).** `l311c` inside
   `l4cc0` *does* try to allocate the `-21508` pool (`NewPtr(640*62 = 39680)`),
   but at 4MB it returns **0**: only ~28KB is free by then because `jt463`
   (the FAR pool, `glib_pool_open` in `master_init`, BEFORE `l4cc0`) grabbed
   `FreeMem() - 32K` = its full 768KB cap, leaving nothing for `l4cc0`'s
   non-purgeable design buffers. The Mac got away with a 32K reserve because
   ITS design buffers were purgeable Handles. **Fix:** `jt463` now reserves
   256K (not 32K) so the post-pool design buffers fit; at 4MB the pool drops
   to 620KB (still > the ~461KB dungeon peak) and `NewPtr(39680)` succeeds.
   Verified: pool base non-zero, `jt579`/`jt577` deserialize the full 6-member
   HEIRS party, dungeon + event picture render, no bus error. The lazy/deferred
   rebuild is no longer needed — the pool is up by `l4cc0`. See
   docs/play-entry-wall.md.
2. **rec[12] routing.** `jt577` only builds rec[8] (inventory) + rec[4] (spells).
   The sheet reads rec[12] (equipped) + rec[20] (effects). Need to find how the
   Mac populates rec[12]/16/20 — likely a re-equip/categorise pass over rec[8] by
   each item's location/worn flag (find that field in the 18-byte record), OR the
   SAVGAMA native load (NOT jt577) reconstructs them directly. Trace the Mac
   SAVGAMA character-load (not the .cch jt577) to get this faithful.

## Plan (next session)

1. Deserialize rec[8] (inventory) + rec[4] (spells) from the in-memory record,
   mirroring jt577. Run it lazily once the pool is up. Keep the full serialized
   tail (the 512-byte copy truncates >~6 items) — read items from the original
   save buffer, or stash the tail per character.
2. Trace the Mac's rec[12]/16/20 population (the equip/categorise step) and lift
   it, so jt28(rec[12]) walks a real list. Clear stale rec[12]/16/20 first to
   avoid the bus error.
3. Verify the sheet shows BARBARUS's LONG SWORD +1 / PLATE MAIL +1.
4. Then the ITEMS / TRADE / DROP handlers (`l25ce`/`l4334`/`l46e0` in jt904) on
   the now-real lists.

---

## ★★ NO SPELLCASTER COULD EVER CAST — the ADD path dropped jt587's tail (FIXED 2026-07-14)

**Symptom:** a level-6 MAGIC-USER pressing CAST gets **"ZOLTAN CANNOT DO MAGIC"**.
So does a level-6 cleric. The magic subsystem — cast, memorize, scribe — was
**completely unreachable for every character in the game**, and had been for the
whole project. No audit caught it: nothing is stubbed, `stub_audit` reports 0
live gaps, and every function involved is fully lifted.

**Root cause.** `l05c4` refuses magic when the per-class memorize CAPACITY grid
`rec[355]/[364]/[373]` is all zero. That grid is built by **`jt908`**, which runs
only from **`jt910`** (the post-level-change recompute). The Mac reaches it on
the ADD-CHARACTER path: its ADD is a disk load — `jt477` (alloc slot) then
**`jt587`**, whose body is *zero → load-from-disk → `jt21` → `jt910`*
(CODE 15 @0x0920 / @0x092a).

The port **cannot use jt587's load** (its saved characters are flat cg_pool
dumps, not the `.cch` stream `jt577` walks; and party members must live inside
`cg_pool` to be walkable) — a deliberate, documented deviation. But dropping
`jt587` wholesale **also dropped its RECOMPUTE TAIL**, and that tail is where the
spells come from. The port substituted the load and silently lost the `jt910`.

**Fix:** run `jt21` + `jt910` on the picked record at the Mac's own point in the
ADD sequence (before the L1486 caps walk). These are the Mac's own functions —
this restores faithful behaviour, it does not stand in for it.

**Verified:** ZOLTAN (level-6 M-U) now opens **SPELLS IN GRIMOIRE** — CHARM
PERSON / DETECT MAGIC / ENLARGE / MAGIC MISSILE / READ MAGIC / SHIELD / SLEEP /
INVISIBILITY — with the capacity line **MAGIC-USER : 4 2 2**, exactly the
progression `jt908` computes from the `-29876` table (selector 3, row
`4 2 2 0 0 0 0 0 0`). Memorizing consumes a slot (4 2 2 -> 3 2 2). The A5
progression table was seeded correctly all along; nothing ever called it.

### ★ Why every audit missed it, and the tell that cracked it

- **Nothing was stubbed.** `jt908`, `jt910`, `jt587`, `l05c4` are all fully
  lifted and correct. The gap was a **missing CALL**, which no stub audit can
  see. Same blind spot as the empty switch arm (`docs/enhancements.md`).
- **The character EDITOR worked.** `l5044` (Modify -> Keep) calls `jt908`
  directly, so an *edited* character got spells and an *added* one did not.
  **That asymmetry is the tell** — it localised the bug to the ADD path.
- ★★ **When a port must skip a Mac function, enumerate what else that function
  did.** `jt587` was skipped for its LOAD; nobody checked its TAIL. A deviation
  documented as "we can't take this path" quietly took the rest of the path with
  it.
