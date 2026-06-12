# Band 5 wall — ranks 401-500

Campaign worklist, opened 2026-06-12 after bands 2/3/4 closed.
60 non-LIFTED at open (~17KB). Update this file in the same commit
as each lift.

## Aliases (lifted under the CODE-local name)

| jt | CODE+addr | local | note |
|---|---|---|---|
| jt1124 | 4+0x4d88 | l4d88 | (rect helper, CODE 4 system family) |
| jt988 | 5+0x17e2 | l17e2 | (the .slb loader entry; jtN stub also exists — unify) |
| jt48 | 6+0x5864 | l5864 | jt115 release of -24260 + the 0xFF flag |
| jt514 | 14+0x6520 | l6520 | (the CODE 14 range check) |
| jt950 | 20+0x0b20 | l0b20 | (the -28006 record helper) |

## NOOP

jt709 (CODE 16+0x0004, bare rts — raw bytes checked).

## Trivial (opener commit)

- jt107 (CODE 6+0x36da) = return g_a5_byte(-18397)
- jt67 (CODE 6+0x5f48) = return g_a5_byte(-13084) — the old stub
  returned a constant 0

## Naming traps

- boot.c `l0f60` = CODE 12's (jt918 case 3), NOT jt543's CODE
  14+0x0f60.
- boot.c `l0004` = CODE 4's, NOT jt709's CODE 16+0x0004.
- boot.c `l48f4` = my CODE 16 leaf stub — jt670 IS that function
  (1336B, the big combat announcer); lifting jt670 replaces it.
- jt988 has BOTH a jtN stub and the lifted l17e2 — route the stub.

## Genuine targets by segment

| seg | targets |
|---|---|
| CODE 2 | jt257 104, jt247 410 (Game Settings!), jt245 40 |
| CODE 3 | jt486 10, jt467 118 |
| CODE 4 | jt1192 100, jt1194 132, jt1126 1066, jt1123 334 (the colour-cursor install, #107!) |
| CODE 5 | jt1006 134, jt1024 90 (LBCreate — releases the jt1021 guard!), jt1022 304, jt1033 192, jt1052 34, jt1059 616, jt1071 122 |
| CODE 6 | jt24 324 (l2000 partial), jt109 38, jt93 62, jt88 302 (l5124 partial), jt83 466 |
| CODE 7 | jt226 26, jt224 128, jt165 66, jt156 252, jt173 796, jt188 134, jt194 62 |
| CODE 8 | jt327 2244 (the big one), jt343 160, jt350 98, jt359 38, jt345 82 |
| CODE 12 | jt925 184 |
| CODE 13 | jt502 178 (the projectile-trail leaf stub) |
| CODE 14 | jt543 304, jt545 244 |
| CODE 16 | jt623 156, jt602 834, jt607 354, jt661 476, jt670 1336 (=l48f4) |
| CODE 18 | jt859 512 |
| CODE 19 | jt915 584, jt906 628 |
| CODE 22 | jt316 50, jt317 34, jt320 12, jt289 330, jt290 806, jt294 24, jt274 44 |

## Plan order

1. ~~Opener: aliases + NOOP + jt107/jt67.~~
2. Tiny batch: jt486, jt320, jt294, jt274, jt317, jt316, jt226,
   jt359, jt1052, jt109.
3. **jt1024 (LBCreate) early — it releases the jt1021 guard.**
4. jt1123 (the colour-cursor install — lands #107 with HAL work).
5. CODE 22 cluster, CODE 7 cluster, CODE 16 effect tier
   (jt602/jt607/jt623/jt661/jt670 — #115 overlap), the rest.
6. jt327 (2244B) last — likely a dispatcher; check for glue first.

## Progress log

- 2026-06-12: campaign opened; 5 aliases + 1 NOOP + jt107/jt67
  trivial lifts.
- 2026-06-12: jt1024 (LBCreate) + jt467 (the pool appender) FULL —
  and the jt1021 PORT GUARD IS DELETED: l36e0's "activ" claim now
  creates a real list-block ('GLIB' header + first offset 20 via
  jt464/jt467/l0ab8) before jt1021's LBInsert runs. Hatari-verified:
  boot + Training Hall clean — the band-3 boot-corruption scenario
  is structurally fixed. Band 5 at 49/100.
- 2026-06-12: jt1123 FULL — the colour-cursor INSTALL chain lands
  (#107's wiring): depths 0/1 convert the 256B image+mask to 1-bit
  plane 0 (L112c/L11f8 — planes 1-3 dropped, noted), L6330 stages
  into the A5 -892 record which IS a 68-byte Mac Cursor (32B data +
  32B mask + hotspot words at +64/+66), and the EXISTING l6538 is
  COMPLETED — its documented-deferred SetCursor calls now run
  through the shim (arrow = GetCursor(0), engine cursor = the -892
  record; the l62fa in-window gate was already lifted). The jt304 ->
  jt1007 -> jt1123 -> SetCursor chain is wired end-to-end.
  Hatari-verified: dungeon renders, no regression. Band 5 at 50/100.
- 2026-06-12: tiny batch (10): jt486 (present+poll), jt316/jt317/
  jt320 (the -11714 state-record accessors), jt294 (the region
  action proc — merged into its existing proc-signature stub; stamps
  -11666 word +6), jt274 (6-byte record swap), jt226 (list scroll
  over the new l501e leaf stub), jt359 (record-table pointer,
  jt348's sibling), jt109 (GLIB piece draw over the new jt994 leaf
  stub), jt1052 noted = the _Eject trap glue (hard-disk install:
  no-op). Band 5 at 59/100.
- 2026-06-12: CODE 22/7 batch: jt289 FULL (click -> cell-edge
  snapper for the wall editor: dominant axis via jt388, cell/2 or
  facing-biased offsets), jt165 FULL (linked-list n'th node — merged
  into the existing stub), jt194 FULL (text-record dims *4/3 via
  l4ab6/-12304), jt224 FULL (text-window page up/down through
  l0264; the Mac's two trailing args are unread per the existing
  l0264 lift), jt188 FULL (the 20-bit cell trigger scan over the
  new l4910 leaf stub; -12645 event table). jt327 + jt290 recorded
  as own-session dispatchers (14-case design-record edit; the
  806B editor click tool over 5 unlifted locals). Band 5 at 65/100.
- 2026-06-12: CODE 16 effect pair: jt623 FULL (the Hold handler —
  save modifier by target count, -2 for effect 23 single-target;
  the Mac's default arm reads an uninitialised local, port keeps 0;
  announce via the new l7026 leaf stub) and jt607 FULL (the area-
  damage handler — underwater veto except 51, dice from jt870/jt17,
  the underwater jt508 radius target-table rebuild, the jt521 burst
  render, jt873 d6 damage +rolls for 115, l6114 announce).
  Band 5 at 67/100.
- 2026-06-12: CODE 8 trio + jt93: jt343 FULL (paint every jt338-
  registered command-bar menu via the new l35d6_c8 leaf stub — the
  bare l35d6 name belongs to jt416's CODE 3 address), jt345 FULL
  (monster-name fetch: kind 3 = the -10508 default, else jt366),
  jt350 FULL (monster-record fetch over the new l6432 leaf stub +
  the jt362 STRG refresh = the Mac's L600c), jt93 FULL (the
  "%( %)" grouped-value text write through jt94). Band 5 at 71/100.
