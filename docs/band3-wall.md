# Band 3 wall — ranks 201-300 (+ the jt883 band-2 rider)

Campaign worklist, opened 2026-06-12. 55 non-LIFTED entries at open
(39 MISSING + 16 STUB per `tools/jt_progress.py --band 3`), plus jt883
(band 2 leftover) by user request. Update this file in the same commit
as each lift.

## Already lifted under the CODE-local name — recorded as ALIAS

| jt | CODE+addr | local | what |
|---|---|---|---|
| jt395 | 3+0x46b2 | l46b2 | tolower |
| jt45 | 6+0x5700 | l5700 | slot-1 mode teardown |
| jt47 | 6+0x541a | l541a | resource-slot loader (PIC/SPRIT) |
| jt106 | 6+0x3880 | l3880 | GLIB cell blit |
| jt110 | 6+0x33ac | l33ac | binder open (the jt81 dep) |
| jt193 | 7+0x4fbe | l4fbe | (known trap; lifted earlier) |
| jt364 | 8+0x6e50 | l6e50 | clamp arg 0..40 -> -10374 |
| jt516 | 14+0x6554 | l6554 | creature-on-7x7-map predicate |
| jt66 | 6+0x6048 | l6048 | 6-byte thunk -> l604e |

NOT aliased: jt947 = l709e (CODE 20+0x709e) — the 39-case event
dispatcher is only a level-2 skeleton (#115); counts when it's full.

## Address-twin stubs (unify when lifting)

- **jt29 = l2f4c** (CODE 6+0x2f4c, ~40B) — both PROBE stubs with
  *conflicting* doc-stories (jt29: "rec[138] > JT[35](rec)";
  l2f4c: "entity-has-second-stat predicate") — resolve from the asm.
  Helper **jt35** (CODE 6+0x2f74, ~100B) MISSING. Lifting the trio
  makes jt40's multi-class secondary-stat arm live (38 call sites).
- **jt99 = l4b84** (CODE 6+0x4b84, ~114B) — current body is a thin
  jt175 wrapper; the real body is bigger. Verify + complete.

## Naming traps

- `l5baa` in boot.c = CODE **7**+0x5baa (cell bounds test), NOT
  jt56's CODE 6+0x5baa. Lift jt56 under the jt name only.
- `l2f74` in boot.c = CODE **17**+0x2f74, NOT jt35's CODE 6+0x2f74.
- jt1032 = CODE **5**+0x506e; the faithful menu header is CODE
  **22**+0x506e — same offset, different segments.

## Genuine targets by segment (size ≈ next-linkw distance)

| seg | targets (size B, STUB if a probe stub exists) |
|---|---|
| CODE 3 | jt475 74, jt469 246, jt416 38 STUB, jt415 10 STUB, jt405 50 |
| CODE 4 | jt1148 4 STUB, jt1154 6 STUB |
| CODE 5 | jt1007 586 STUB, jt1021 340 STUB, jt1028 430, jt1032 280, jt1045 396, jt1072 28 |
| CODE 6 | jt33 232, jt29 40, jt35 100, jt98 506, jt99 114, jt95 58, jt55 76 STUB, jt56 528 STUB, jt58 12 STUB, jt69 30 STUB, jt61 32, jt59 28 |
| CODE 7 | jt142 68 |
| CODE 8 | jt329 2, jt366 66, jt349 932, jt348 38, jt367 90 STUB, jt370 204 STUB, jt351 12, jt353 340 |
| CODE 10 | jt261 40 |
| CODE 12 | jt920 2 STUB, jt926 80 |
| CODE 13 | jt499 74, jt504 88 |
| CODE 16 | jt674 174 STUB, jt687 304 STUB |
| CODE 18 | jt736 8 STUB |
| CODE 22 | jt272 308, jt278 348, jt307 28 |
| rider | **jt883** CODE 19+0x4248 (~387 asm lines, band 2) |

Total ≈ 10 KB of asm.

## Plan order

1. ~~Alias batch~~ (this commit: jt_progress.py ALIAS_LIFTED + this file).
2. jt29 + jt35 trio — the multi-class stat gate goes live.
3. Tiny batch (<60B): jt329, jt920, jt1148, jt1154, jt736, jt415,
   jt351, jt58, jt59, jt61, jt69, jt307, jt348, jt261, jt416, jt405,
   jt106-class leftovers — group commits per segment.
4. Medium (60-350B) by segment: CODE 6 cluster first (jt95/jt99/jt55/
   jt33), then CODE 8 (jt366/jt367/jt370/jt353), CODE 22
   (jt272/jt278), CODE 5 (jt1021/jt1032/jt1072), the rest.
5. Big: jt349 (932), jt56 (528), jt1007 (586), jt98 (506), jt1028,
   jt1045, jt469, jt687, jt674 + the jt883 rider.
6. jt947 closes via #115 (l709e full lift), not here.

## Progress log

- 2026-06-12: campaign opened; 9 aliases recorded (jt395 jt45 jt47
  jt106 jt110 jt193 jt364 jt516 jt66).
- 2026-06-12: jt35 + jt29 LIFTED (full); l2f4c routed to jt29 —
  jt40's multi-class secondary-stat arm is live. The asm settled the
  doc conflict: jt29 = (jt35(rec) > rec[138], unsigned) ? -1 : 0.
- 2026-06-12: tiny batch (17 entries). FULL: jt415 (_ExitToShell —
  shim ExitToShell added: platform teardown + exit), jt69 (fatal
  error path; jt1081 teardown chain stubbed, l4d7a = bare rts
  elided), jt1148 (_ObscureCursor — shim already had it), jt55
  (combat slot release over an l3b1e leaf stub), jt58 (l31dc on the
  -27870 slot), jt416 (FSDelete via l45d6 + the JT[1054] _Delete
  glue = shim FSDelete), jt405+l4648 (strupr/islower), jt59 (jt478
  decimal scratch), jt61 (jt477 item-node alloc), jt261 (l3e0c
  28-list member test), jt307 (entry[4] ? 8 : 7), jt348 (-13038
  record-table field 0), jt351 (jt209(1)), jt1072 (message-flush
  loop over an l7ab4 leaf stub). NOOP-set: jt329 jt920 jt736
  (literal rts). ALIAS: jt1154 = jt1154_pg, jt1054 = shim FSDelete.
  Band 3 at 74/100 (19 MISSING + 7 STUB left).
- 2026-06-12: medium batch. FULL: jt367 (counter format over the
  -10612/-10608/-10600 A5 format slots — its caller is the live HUD
  clock line), jt95 (right-justify via jt483/jt488/jt384), jt142
  (click -> text cell via jt1139 (stub leaf) / 12), jt475 (indexed
  string-table get, -10280/-10276, STRS+0x41c0 fallback), jt499 +
  jt504 (combat-kind predicates over the -27944 16-byte entries),
  jt926 (editor dirty-flag poll, -25314x3 / -25302). ALIAS: jt99 =
  l4b84 (the Mac body is just jsr JT[175] — the 114B size estimate
  was the next-linkw artifact). jt366 deferred to the CODE 8 group
  (needs the CODE 8 locals l6520/l5f04/l62e0 — NOT CODE 14's
  l6520!). Band 3 at 82/100 (12 MISSING + 6 STUB left).
- 2026-06-12: CODE 22 pair. jt272 FULL (area-map click -> numpad
  move direction; stores the clicked cell into -12287/-12288; the
  L4900 diagonal gate lifted full = jt358 facing clamped to <= 4).
  jt278 STRUCTURAL (design-list entry painter: anchor/state/JT[1161]
  clear faithful, the four per-kind painters L2aaa/L2f24/L329c/L347a
  are leaf PROBE stubs). Band 3 at 84/100.
- 2026-06-12: jt33 FULL (Training Hall "can train?" gate: per class
  slot, JT[26] XP threshold + the -28048 class-cap table vs the XP
  long at rec+68 — L3038 was ALREADY lifted as jt26, same address).
  jt469 FULL (FCInsDel insert/delete-span over the FAR pool, with
  the JT[1084] "Invalid offset"/"bad length" modals) + L0e10 lifted
  full (group resize: slide later groups via l1020, advance the
  -10270 slot table, low-water in -9300). NAMING TRAP hit: CODE 3's
  L0d44 = jt459 and L0d1a = jt468 — boot.c's l0d44 is a CODE 19
  local. Band 3 at 86/100.
- 2026-06-12: CODE 8 group. jt370 FULL (kind -> capability flags;
  the stub doc's "bit6 if arg<6" was reversed — asm sets it on
  arg >= 6; case 1's bset #0 hides inside the JT[3] table bytes).
  jt366 FULL-over-stubs (monster-art show: l6520_c8 art-class
  classifier lifted full — NOT CODE 14's l6520; l5f04 release +
  l62e0 bind are leaf PROBE stubs). Band 3 at 88/100.
- 2026-06-12: jt56 FULL (numbered body/portrait library show:
  CH*/CB* trailing-letter trim via jt482, l338c kind 50/49/51 from
  the CPIC/COMSPR name compare, CPIC branch = "<name>1" l33ac bind
  + the b / b+38 overlay pieces, non-CPIC = mode-1 bind with id a
  or a+128; l31dc release). New leaf stub: l035e (file-group mode
  switch). jt593's body-picture call chain is now real down to
  l3b1e. Band 3 at 89/100.
- 2026-06-12: CODE 16 handler pair FULL: jt674 ("is affected"
  L6114 announcer, handler ids 5/11/18/22/29/60/77) and jt687
  (Remove Curse: jt872 type-43 short-circuit, equipped+cursed item
  walk at target+8, bit-7 strip via jt857/l77a0 type 127 + jt875
  re-resolve, jt503 announcements). docs/code16-wall.md updated.
  Band 3 at 91/100.
- 2026-06-12: jt1028 / jt1032 / jt1045 = THINK C **Mac trap glue**
  (_NewPtr / _DisposHandle / _GetVInfo PB thunks in the CODE 5
  0x4d00-0x5d00 glue library, same family as jt1054=_Delete) —
  recorded as shim aliases, no engine callers by jt-name. The
  "280-430B" size estimates were neighbouring-glue artifacts.
  Band 3 at 94/100; left: jt353, jt98, jt349, jt1007, jt1021,
  jt947 (#115).
- 2026-06-12: jt1021 FULL (LBInsert: list-block item insert — header
  bump, jt469 table-slot + data-span inserts, offset-table rewalk
  with the +4/+size shifts; validation via the JT[1084] modals).
  Band 3 at 95/100; left: jt353, jt98, jt349, jt1007, jt947 (#115).
- 2026-06-12: jt1007 FULL — it's the **cursor builder** (build the
  516-byte record at A5 -4172 from a GLIB piece: dims header, 256B
  image, 256B mask, the type-5 image/mask re-split) — NOT the
  "L2d3e selection-nav" the old stub claimed. jt1123 (CODE 4+0x659a)
  is the install that takes the record pointer = a 16x16 8-BIT
  COLOUR cursor — the faithful home of task #107's colour-cursor
  work (PROBE stub pending the HAL cursor-image path). jt304's
  jt1007(0, 26) = "set the play-screen cursor to ALWAYS piece 26".
  Band 3 at 96/100; left: jt353, jt98, jt349, jt947 (#115).
- 2026-06-12: jt883 rider LIFTED (full) — it's a 26-byte encumbrance
  adjuster (rec word +86 += delta); the band-2 "387-line" estimate
  measured the L4264/L4334 locals after the entry, not the function.
  Band 2 now 98/100 (jt882 + jt599 left).
- 2026-06-12: jt353 FULL (area-map icon painter: GEO-kind icon-bank
  +1 shift, -10366 / -22222 slot select, depth-3 raw blit inlined as
  jt108+jt1001 per the jt57 precedent — the port's jt118 is the
  incompatible render variant). New leaf stubs: l7490 + jt220 (icon
  loaders). ARG-ORDER FINDING: the Mac CODE 6 jt114/jt118/jt121
  take (h, v) — settled against jt121's verified lift; the
  3dview-trace doc's "JT[114] = (top, left)" label likely reflects
  the BII hook's printf labels, not the C param order. Band 3 at
  97/100; left: jt98, jt349, jt947 (#115).
