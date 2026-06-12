# Band 4 wall — ranks 301-400

Campaign worklist, opened 2026-06-12 after bands 2 and 3 closed.
63 non-LIFTED at open (56 MISSING + 7 STUB); ~16KB of asm. Update
this file in the same commit as each lift.

## Already lifted under the CODE-local name — recorded as ALIAS

| jt | CODE+addr | local | what |
|---|---|---|---|
| jt446 | 3+0x30ba | l30ba | (range walk helper) |
| jt1162 | 4+0x3e38 | l3e38 | idle/front-window dispatcher (level-2; L3e8e branch deferred) |
| jt43 | 6+0x579e | l579e | bigpic backdrop load |
| jt60 | 6+0x5f84 | l5f84 | (key fetch) |
| jt68 | 6+0x604e | l604e | (event poll pair) |
| jt62 | 6+0x6096 | l6096 | release a -21508 item node |
| jt192 | 7+0x4e3a | l4e3a | (record init) |
| jt368 | 8+0x6520 | l6520_c8 | monster-art class (band-3 lift) |
| jt532 | 14+0x635e | l635e | creature-cell redraw |
| jt886 | 19+0x1276 | l1276 | roster staging (jt904 chain) |
| jt899 | 19+0x5274 | l5274 | theoretical max-HP ceiling |

## NOOP (bare rts on the Mac)

jt252 (CODE 2), jt260 (CODE 10), jt234 (CODE 11), jt271 (CODE 22),
jt326 (CODE 9).

## Trivial one-liners (lifted in the opener commit)

- jt157 (CODE 7+0x38e4) = return g_a5_byte(-12648)
- jt72 (CODE 6+0x61d4) = return g_a5_word(-13048)
- jt354 (CODE 8+0x5ef8) = jt1160() thunk

## Naming traps

- boot.c's `l7ab4` IS jt1076 (CODE 5+0x7ab4, the message paginator)
  — currently a band-3 leaf stub, so jt1076 stays a TARGET.
- boot.c's `l1798` is CODE 22's but still a PROBE stub — jt299 stays
  a TARGET (50B).
- boot.c's `l0062` (CODE 5+0x62) and my `jt1081` stub are the SAME
  function (the 9-call teardown chain) — unify when lifting.

## Genuine targets by segment (size ≈ next-linkw distance)

| seg | targets |
|---|---|
| CODE 3 | jt392 186 (jtN stub), jt429 20 |
| CODE 5 | jt1025 1756 (the big one), jt1038 180, jt1060 480, jt1062 724, jt1076 326 (=l7ab4), jt1074 62, jt1081 70 (=l0062) |
| CODE 6 | jt130 82, jt129 104, jt116 92, jt105 76 (l3f3c REAL? verify), jt91 42, jt53 116, jt70 36, jt71 20, jt77 442, jt79 226 (stub) |
| CODE 7 | jt229 58, jt145 100 (band-2 leaf stub), jt168 26, jt184 210, jt196 410 |
| CODE 8 | jt338 288, jt362 164, jt352 510, jt347 254, jt355 272, jt357 512 |
| CODE 12 | jt932 300 |
| CODE 14 | jt555 816 (stub), jt546 568 (stub — the ray/target picker) |
| CODE 22 | jt319 22, jt275 36, jt285 72, jt277 94, jt283 96, jt284 198 (stub), jt299 50 (=l1798 stub), jt308 298, jt295 130, jt279 498 |

## Plan order

1. ~~Opener: aliases + NOOPs + the trivial trio.~~
2. Tiny batch (<60B): jt429, jt319, jt275, jt91, jt70, jt71, jt168,
   jt229, jt299, jt1074.
3. CODE 22 cluster (the menu segment): jt277/jt283/jt284/jt285/
   jt295/jt308/jt279.
4. CODE 6 cluster: jt53/jt105/jt116/jt129/jt130/jt77/jt79.
5. CODE 8 cluster + CODE 7 (jt184/jt196) + jt932.
6. Big CODE 5 (jt1025/jt1060/jt1062/jt1038/jt1076/jt1081) and
   CODE 14 combat (jt546/jt555 — coordinate with #115).

## Progress log

- 2026-06-12: campaign opened. 11 aliases, 5 NOOPs, 3 trivial lifts
  (jt157/jt72/jt354).
- 2026-06-12: tiny batch (9 FULL): jt429 (-9163/-9164 both-flags
  predicate), jt319 (clear -11714 rec byte 1), jt275 (two-nibble
  pack into -18476), jt91 (the jt98 accept flag read), jt70 ("%lu"
  into the -13061 scratch), jt71 (-13048 word set, jt72's pair),
  jt168 (list-dialog context stash), jt229 (record-table field-0
  clear), jt1074 (paginated message line over the l7a0e page-spill
  leaf stub; l7ab4 made variadic for the "%* %r" call). jt299
  deferred to the CODE 22 cluster (needs l17ca/l2180/jt308).
  Band 4 at 65/100.
- 2026-06-12: CODE 22 cluster (7 lifts): jt285/jt277/jt283 FULL (the
  map-cell wall-nibble read / low-set / high-set over the -12300
  design base, 6B per cell + 290; jt283 reads via the lifted l05ca =
  jt293), jt284 FULL (cell-interior click test, jt272's sibling),
  jt308 FULL (saved-game slot row painter: -11304 class string,
  "%15s" -13952 name, "%s %9s" status pair from -10796/-10804/
  -10812/-11320), jt299 (slot repaint sequence over l17ca_c22 +
  l2180 leaf stubs), jt295 (cell redraw sequence over l3792 +
  jt1150 leaf stubs; l3a1a was already lifted). Band 4 at 72/100;
  CODE 22 leaves jt279 (498B).
- 2026-06-12: CODE 6 small four + jt105 alias: jt105 = l3f3c (the
  bigpic palette-range install, already lifted full). jt53 FULL-over-
  stub (text cell -> char coords; l3c24 pen resolver = leaf stub),
  jt116 FULL (message-window rect fill in half-v/x4-h space), jt129
  FULL ("<name>%03d.dat" design-data load request via jt431 path
  concat + jt987 mode 4; jt126 callback = leaf stub), jt130
  FULL-over-stub (8-char basename build from the -31268 table;
  l0004_c6 char appender = leaf stub — the bare l0004 name belongs
  to another segment's lifted local). Band 4 at 77/100.
- 2026-06-12: glue sweep + jt392. ALIAS x4: jt1038 (_GetOSEvent/
  SetTrapAddress glue), jt1060 (async-PB volume glue), jt1062
  (_StripAddress — identity on the 030's flat 32-bit bus), and
  **jt1025 (1756B!) = the _SysEnvirons availability glue** — the
  scary CODE 5 "big one" was the THINK C compatibility shim section,
  not engine code. jt392 FULL (create-save-file: '%' spec = the Mac
  SFPutFile dialog via the l341a leaf stub, else L322c/L31fc split
  (already lifted) + l3386 = the shim's Create — TOS has no
  type/creator metadata, 'TEXT'/'KAHL' vs 'GAME'/'MAG;' stamps
  noted). Band 4 at 82/100.
- 2026-06-12: jt262 FULL (jt261's NOT-in-list inverse), jt145 FULL
  (combat repaint = a single jt1001 FRAME piece-6 blit — the band-2
  leaf stub upgraded). jt1081 stays a documented stub by design (its
  9-call teardown chain only runs on the fatal-exit path where jt415
  exits immediately after; lifting the chain is ceremony). Band 4 at
  84/100. LEFT (16): jt79 226 STUB, jt77 442, jt184 210, jt196 410,
  jt279 498 (CODE 22), jt338 288, jt347 254, jt352 510, jt355 272,
  jt357 512, jt362 164 (CODE 8), jt932 300 (CODE 12), jt1076 326
  (=l7ab4 paginator), jt1081 70 (by-design stub), jt546 568 +
  jt555 816 (CODE 14 combat, #115 overlap).
