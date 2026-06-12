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
