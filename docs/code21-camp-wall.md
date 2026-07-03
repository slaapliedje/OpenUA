# CODE-21 Encampment (camp / rest) subsystem — worklist

The "Encamp" command (g_walk_cmd 4 in `l63c0`, and the inn event) opens the
camp menu. Chain: **Encamp/inn → `l473e` → `jt957`** (the camp-menu dispatcher).

## Done (slice 1, 2026-06-21)

- **`jt957`** (CODE 21 + 0x2f9a) — the camp-menu dispatcher. Faithful structural
  lift: enters camp mode (mode 2; prev saved in -27989), draws the frame/title
  (jt103/jt94) when not in an overlay (-18486), loops a 9-row command menu
  (jt155 rows 0..8, gated: 2/4 drop at rec[44]>=100; 5/6 vs 7 by -18485),
  prompts via jt160, supports the quick-keys auto-select (-24139 → jt934/jt936),
  dispatches JT[3] 0..7, and on exit restores the prev mode + repaints
  (jt84/jt23 for overland mode 3, else jt78/jt937/jt938).
  - Working today: **View** (case 0 → jt904), **Rest** (2, slice 2 below),
    **Save** (6 → jt585 + jt159 confirm), **Load** (5 → jt942, with the
    unsaved-game confirm), **Exit** (7).
- **`l473e`** (CODE 20 + 0x473e) — the rest/Encamp trigger. Resolves the party
  cell (jt197), primes rec[44]=100 when the cell allows a full heal, opens
  `jt957`, recomposes on success. Reached from the Encamp command (res==4,
  `boot.c` play loop) and the inn event.

## Done (slice 2, 2026-06-21) — the REST action works end to end

- **`l09ea`** (CODE 21 + 0x09ea, camp case 2) — finds the longest per-member
  rest time (`l0006_c21`), splits it into the day/hour/ten-min/min display
  fields (-23206/-23208/-23210/-23212), then runs the **already-lifted** rest
  engine **`jt915`(1)** which advances the clock and heals/memorizes
  (interruptible "Stop Resting?"). So Rest now actually passes time + heals.
- **`l0006_c21`** (CODE 21 + 0x0006) — per-member rest minutes = type*60 +
  (Σ pending spell levels)*15 (type 4, or 6 with any level-3+ spell). Named
  `_c21` to avoid the lXXXX collision with the existing `l0006`.

`L09ea` is also called by the Magic camp action (`l1850`, case 1, case 4=Rest).

## Done (slice 3, 2026-06-21) — the MAGIC menu dispatcher

- **`l1850`** (CODE 21 + 0x1850, camp case 1) — the Magic menu. Faithful
  structural lift: 6-row menu (jt160), row 4 (Rest) dropped at rec[44]>=100,
  quick-keys auto-select, JT[3] dispatch 0..4. **Rest (case 4 → l09ea) and Exit
  (row 5) work**; the four spell screens are PROBE-stubbed (slices 5+):
  `l06d6` (0), `l0bc6` (1, ~190 ln), `l0df2` (2, ~458 ln), `l1374` (3, ~366 ln)
  — memorize / view / cast / scribe. Each is a large screen of its own.

## Done (slice 5, 2026-07-03) — the MEMORIZE + CAST magic screens

The encamp/memorize loop is LIVE end to end. Both magic-menu spell screens
lifted (CODE 21), sharing five helpers:
- **`l06d6`** (magic case 0, CAST) — camp cast: `l05c4(1)` precondition, then
  the `jt595(0,1)` in-Memory picker loop → `jt599` effect. An offensive spell
  in camp hits the faithful "<spell> CAN'T BE CAST HERE... LOSE IT? YES/NO"
  discard (frees the memorized slot).
- **`l0bc6`** (magic case 1, MEMORIZE) — the core loop: `l05c4(2)` precondition,
  `jt597(5)` builds the "to Memorize" pending list, `jt595(5,8)` confirm
  ("SPELLS OK? KEEP/EXIT", the `*` marks a pending slot), then loops the
  `jt961` capacity grid + `jt595(1,2)` grimoire picker, stamping
  `rec[198+k] = id|0x80` per pick (`jt959` re-sort).
- shared: **`l05c4`** (can-do-magic precondition + rejection message),
  **`l0214`** (clear pending memorize flags), **`l03b2`** (per-(class,level)
  remaining capacity, with the mage-Int / cleric-Wis high-level gates),
  **`jt961`** (=L0798, the "Cleric/Druid/Magic-User :" capacity grid + any-free
  return), **`jt959`** (=L0aba, the slot sort). Leaf deps all pre-lifted
  (jt595/jt597/jt599/jt83/jt18/jt103/jt94/jt384/jt404/jt23).

Hatari-verified: Encamp → Magic → CAST discards Lightning Bolt (LOSE IT? Yes),
→ MEMORIZE shows "SPELLS TO MEMORIZE / *LIGHTNING BOLT / SPELLS OK? KEEP",
KEEP stages the pending flag. Barbarian correctly hits "Cannot do magic".

REMAINING for the loop to fully close: the memorize COMPLETION happens during
REST, in the CODE 19 rest-engine leaves that are still PROBE stubs — `l0d86`
(memorize-completion poll), `l07e6` (healing), `l0572_c19` (clock repaint),
`l0418`/`l035c`/`l0cb8`/`l0e3e` (time/expiry/upkeep/encounter). jt915 (the
rest driver) is lifted; these leaves are the next slice — then REST advances
the clock and converts the `*` pending flags to memorized spells. SCRIBE
(`l0df2`) and DISPLAY (`l1374`) magic screens also still stubbed.

## Done (slice 6, 2026-07-03) — the REST loop closes: time/heal/memorize-completion

jt915's eight leaves were all stubs; critically **L0418 (advance time) was a
no-op, so jt915's while() INFINITE-LOOPED whenever anything was pending**
(a latent hang the memorize slice would have triggered). Lifted the
loop-closing set (CODE 19):
- **`L0418`** — decrement the rest COUNTDOWN (the -23214 mixed-radix array;
  -23212/-23210/-23208/-23206 = clk[1..4] = mins/tens/hours/days). Borrows up
  through units, wipes (jt65) when exhausted, then `l032c` re-normalizes.
  `l025a`/`l032c` (carry/spill) were already lifted — repointed.
- **`L0528`** (num->str two-digit), **`L0572_c19`** (the "Rest Time:" repaint).
- **`L07e6`** — heal 1 HP/member per 288-tick game-day (jt869), "The Whole
  Party Is Healed" + HUD refresh.
- **`L0d86`** + **`L0876`** — the memorize-completion: L0d86 counts down each
  member's rec[126] timer (every 12 ticks); at 0, L0876 clears the pending
  bit7 on the slots and the spell is memorized. **BUG fixed mid-lift**: L0d86
  gates the L0876 call on L0980's RETURN value (not `*out`) — the first pass
  had it on `*out` so L0876 never ran.

Hatari-verified END TO END: Encamp -> Magic -> CAST-discard Lightning Bolt
(LOSE IT? Yes -> pending) -> MEMORIZE KEEP -> **REST** (no hang; countdown
advances) -> CAST shows LIGHTNING BOLT re-memorized. Full-HP no-pending REST
returns immediately (counters 0).

DEFERRED (both filed):
- **The "<name> has memorized <spell>" announce** (l48f4 in L0876) bus-errors
  the non-interactive rest loop — it draws a jt103 panel + jt92 (l4bac
  present) mid-loop. Suppressed; the spell still memorizes. Fix when l4bac's
  present is safe outside the event pump.
- ~~The WORLD CLOCK does not advance during rest~~ **RESOLVED (slice 6b)**:
  the clock advancer was hiding in plain sight — **L035c = JT[914]**, ALREADY
  LIFTED in the port (jt914: copy hdr[5..11] into a 7-word array, bump unit
  `idx` count times with the l025a mixed-radix carry, write back, run L006c).
  jt915 was calling a fresh `l035c` PROBE stub instead — the stub-shadowed-
  twin class from docs/stub-alias-audit.md, AGAIN (and my own `l0528` from
  slice 6 duplicated the lifted jt913 the same way — both repointed).
  Hatari-verified: a level-1 pending rest (type 4 = 255 min) advances the
  HUD clock 12:00 AM -> 4:15 AM, to the minute. The -23228 radix words
  (10/6/... carries) are runtime-seeded from the save/design state — BSS
  in DATA, probe-confirmed live values.
  Still stubbed: L006c (post-advance timed-effect expiry, ~490B — jt914
  calls it, so expiry is the remaining gap), L0cb8 (a second -23201-keyed
  memorize path), L0e3e (encounter roll), L0694 (memorize setup).

## Done (slices 6c + 7, 2026-07-03) — rest engine COMPLETE + the ALT tree

**Rest engine (CODE 19) — every jt915 leaf now lifted:**
- `l0694` — the interactive "REST TIME: DD:HH:MM" editor (jt182 bar: REST/
  DAYS/HOURS/MINS/ADD/SUBTRACT/EXIT; arrows 132/136 = subtract/add; the
  l0572 highlight paints the edited field colour 11). Hatari-verified:
  +15 min +1 hr rests exactly 1:15 and the world clock lands on 1:15 AM.
- `l006c` — post-advance timed-effect expiry: (count,unit) -> minutes via
  the radixes, <=10-min steps over each flagged member's effect list at
  rec+4 (+0 id / +2 duration / +6 next; 0 = permanent), expiring via jt878.
- `l0cb8` — the per-spell memorize pacing tick (-23201 countdown; re-arms
  level*3 via the l0876 REPORT mode).
- `l0e3e` — the random-encounter roll (zone chance/period from the design
  entry; l694e validity; jt870 rolls; -13030 ctx save/restore).
- `l0572_c19` upgraded: per-field colour array + the highlight arg.

**ALT camp action (CODE 21) — the whole tree:**
- `l1e44` dispatcher: ORDER | DROP | SPEED | ICON | LEVEL | EXIT.
- `l1abc` ORDER — pick-up/place latch (jt149/jt157), arrows move the
  member up/down the -27928 order. **jt960 was ANOTHER stub-shadowed
  export** (the jt156 roster-click helper): real body = move-later-with-
  head-wrap; `l198c` (unlabeled Mac local) = move-earlier. Verified:
  SELECT + Down x2 moved Barbarus slot 1 -> 3.
- `jt917` DROP — "Drop %s forever?"/"Are you sure?" confirms, farewell
  strings, jt19 removal, l02dc repaint.
- `l1cb8` SPEED — "GAME SPEED (0=FASTEST 9=SLOWEST)" hdr[18] editor
  (verified FASTER 4 -> 3); JT[1] table decoded via tools/jt1_extract.py.
- `l1c2a` LEVEL — hdr[39] difficulty picker (1..5).
- ICON = jt573(1) (already lifted; NPC-gated on rec[147] bit7).
- `l038a`/`l026e_c21` — camp-entry clear of pending-scribe scroll marks.

NIT (cosmetic, known family): the rest-editor / speed digits overprint on
repaint (jt94 transparent-bg over old text — the prompt-plate bg rule).

## Remaining (slices 4..N) — still PROBE-stubbed

| case | fn | size | what it is |
|-----:|----|------|------------|
| 4 (camp) | `l2d7e` | ~3.5KB total | FIX auto-heal: L23dc gate, L2106/L2422 cure planning, two SILENT jt915 rests (-23189), L288c cast pass, L1fcc/L25dc/L2310/L27ec re-memorize bookkeeping |
| 2 (magic) | `l0df2` | ~458 ln | SCRIBE (+ L0980 scroll-learn + l5726 scanner) |
| 3 (magic) | `l1374` | ~366 ln | DISPLAY (the spellbook viewer) |
| — | `l48f4` announce | — | deferred: bus-errors the non-interactive rest (jt92 present mid-loop) |

The Magic spell screens (`l0df2` ~458 ln etc.) are the largest remaining piece;
`l038a` is the cheapest but pulls in `L026e_c21` (CODE 21 0x026e → `jt638`).

## Map of the camp globals

- `-27990` current mode (2 = camp), `-27989` prev mode, `-27987` flag
- `-18486` "in overlay" (suppress frame draw), `-18485` Save/Load gate
- `-24140` jt160 menu-state carry, `-24139` quick-keys mode
- `-22307` row counter (build loop), `-24126` 40-byte menu buffer
- `rec[44]` (rec = -28006) heal allowance (0 / 100); set by `l473e`
- `-27982` "keep adventuring" / reload flag (Save/Load/Exit set it)
- `-13952`/`-13660` jt160 menu title/block; `-14104` camp title; `-14288` save name
