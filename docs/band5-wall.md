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
- 2026-06-12: jt906 FULL (the five-saving-throw recompute: the
  protection-ring scan (flag-bit-7, low flags 6, readied), the
  -28818 class*110 + level*5 + slot table minimum across live
  classes (jt40, level cap 21), and the slot-0 rec[121]-keyed
  adjustments — the Mac applies them inside the class loop,
  preserved). Hall-verified. Band 5 at 72/100.
- 2026-06-12: small four: jt245 FULL (Return/Escape settings-key
  latch into -10372), jt257 FULL (keystroke recorder into the
  -12190/-12090 rings, cap 99/20), jt1006 FULL (colour fill pattern
  — the depth-0 B&W arm expands through the -4572 dither table into
  16 row words), jt1071 FULL (the centered message-line print:
  (86-len)/2 right-shift via the overlap-safe jt406, jt399 space
  fill, l7ab4). Band 5 at 76/100.
- 2026-06-12: jt502 FULL (the projectile-trail selector — diagonals
  via jt472 pick bank 1/0 with the 5/7 mirror flag, cardinals
  decompose into frame+bank; over the new l1888 frame leaf stub)
  and jt925 FULL (bank the party's pooled coin/XP words into the
  -25314 pool longs with the jt897 credit hook (leaf stub) and the
  jt399 clear). Band 5 at 78/100.
- 2026-06-12: REGRESSION + guard: jt1022's live body corrupts the
  pool from jt111's boot-path resize arm (the 'GLIB' magic passes —
  the .CTL pools DO keep headers — so the fault is in the resize
  arithmetic vs the port pool layout; suspect the l3834 offsets or
  the jt469 anchor). Guarded OFF pending instrumentation against
  jt111's actual call values. Boot re-verified.
- 2026-06-12: the CODE 16 HEAVIES, all three FULL:
  - jt661 (the charge-consume: slots at item[54..56] & 127, the
    100-biased count at +41; kind-73 bundles walk the +58 sub-stack
    chain with the emptied-stack unlink and the last-stack collapse
    into the head; plain items jt30-remove when spent).
  - jt602 (the lingering-hazard spawner, ids 34/91/101: a 34-byte
    -20800 node, the -24085 spread pattern through -27862/-27853,
    per-cell occupant dedup via l62ec/jt528, the jt522 terrain gate
    (new leaf stub) + -27848 kind-255 check, the -25318 map glyph
    stamps, the -23234 hazard-list push, the jt521 burst, and the
    jt879 hits on every deduplicated occupant. Effect 34 = radius 9
    glyph 28; 91 = radius 4 glyph 27 triple duration; default 4/29).
  - jt670 = l48f4 (the cast announce — the band-2 leaf stub's full
    body: the combat jt18 "Casts a Spell" window + jt145 + the
    "Spell:%s" line, or the out-of-combat jt103 panel + jt25 header
    + the caller string and spell name + jt92/jt20. The "1336B"
    estimate included the separately-lifted L49e6 registration).
  Band 5 at 87/100. LEFT: jt24/jt88 partials, jt173, jt247, jt859,
  jt915, jt1059, jt1022-debug + the two own-session dispatchers.
- 2026-06-12: jt915 REST engine (committed 021b9b0, see log there).
- 2026-06-12: band-5 closer:
  - jt859 = bare rts on the Mac (CODE 18+0x77f6: `4e75`) — NOOP set;
    the "512B" estimate was the next-linkw artifact again.
  - jt1059 ALIAS — the async-PB file trap glue (FSDispatch selectors
    9/10/16/17/11, sync/async pairs) = the shim's files.c; same
    THINK C glue band as jt1060/jt1062/jt1025.
  - jt173 FULL + jt141 FULL (the AREA-MAP cell picker — jt171's
    overland sibling on 12-unit cells over the 7x7 grid: same
    L2062/L206e/L23b4/L25b6 spine, two jt452 streams of four edge
    strips (keys 9/11/5/7) + four corner blocks (10/4/6/8) around
    the cached cell (-12668/-12667), plus a shape-7 key source
    (JT[141]) that latches SPACE into -13003 and returns it as ' ').
  - jt247 FULL + l2558 FULL (the per-event GAME SETTINGS prompt:
    title from the -11000 string table by *rec's low-6-bit type
    (type 6 formats the +8-bit count), prompt = the -10724/-10716/
    -10696/-10692 word combos, then the L2558 modal — centered
    title at v=8034 colour 143, JT[148] prompt bar, the JT[245]
    Return/Esc source when buttonless, JT[152]-classified poll,
    verdict stored as (max - cmd) in *rec's low byte).
  - jt1192 FULL (GLIB dither-pattern row fill: 16-word pattern
    cycled per row at the word-aligned cursor (new l05dc helper),
    stride -3084) and jt1194 FULL (the table-driven bit-stream
    decoder: 256+256-byte symbol/control banks per level, bit-7
    extend hops 512 to the next level, 16-bit window with byte
    refill; returns the last consumed src address).
  - jt1126 recorded PENDING by design — the page scroll-blit runs
    SetPort/CopyBits over the -2570 NewGWorld page-descriptor table
    (the jt1146/jt1177 bus-error family) + needs L053e/L04de/L77fe/
    L78e8; closes with task #105's page-record init.
  **BAND 5 CLOSED at 97/100** — open: jt1126 (above), jt290/jt327
  (own-session editor dispatchers). jt1022 stays GUARDED (the
  resize-debug rider, counts LIFTED).

## Post-close riders (the own-session pair)

- 2026-06-12: jt290 FULL (the map-editor BRUSH CLICK, CODE 22+0x0bc0):
  tool dispatch on state byte +5 — tool 0 delegates to L1240 (edit) /
  L0ee6 (locked, both new leaf stubs); tools 1/2 save + paint the cell
  byte-295 packed codes; tool 3 arms the mode-5 pick (L475e protect
  gate + L07be commit, leaf stubs) and latches -11702/-11701/-11700;
  the adv tail steps the brush via JT[218] -> L1798 off-map / L423e/
  L3998 repaint + the JT[213] party restamp + L23ee status (stubs).
  RIDERS: jt288/jt306/jt298 FULL (the byte-295 code A/B accessors —
  CODE 22 exports L069a/L0716/L073e) + l0674 FULL + the l06e2/l0788
  automap-colour stubs upgraded to full lifts over them.
- 2026-06-12: jt327 FULL (the design tools' editable TEXT-FIELD DLItem
  method, CODE 8+0x0de4, all 14 JT[1] arms): default = (re)bind (stage
  the 39-byte revert copy into the bound record, clamp width 1..38,
  right-place after the label, default colour 0x87); 0 = paint (the
  -27914 glyph-state counter, JT[448] glyph, right-aligned label, the
  JT[1135]/JT[1161] box, L23d2 text, the L2338 inverted cursor cell);
  1 = JT[1139] hit test; 27 = the JT[1132] modal edit entry; 3/18/128 =
  focus / focusable / defocus; 26 = keystroke (ESC revert from the
  staged copy, CR/LF commit, TAB walk to the next cmd-128 answerer,
  else the L2df8 editor + incremental repaint); 5/42 = width/aux
  setters; 36/40 = l1676 delegates. NEW FULL helpers: l1dd8 (take
  focus), l1eec (drop focus), l20a0 (segment-mode reset), l1f18
  (mouse -> cursor cell), l2338 (one-cell draw), l23d2 (text draw).
  LEAF STUB: l2df8 (the ~1.1KB segmented keystroke primitive with the
  +236 validation table — lift with jt328). NOTE: jt328 (CODE 8+0x16a8)
  is the 250-byte BIG-field sibling, still MISSING by design.
  Both own-session riders DONE — jt290 + jt327 closed.
- 2026-06-12: **JT[1] TABLE-FORMAT DISCOVERY + the l2df8/jt328 pair.**
  The JT[1] inline table is [count][(off,val) PAIRS][default_off] —
  offsets FIRST in each pair, default LAST (per the CODE 1+0x130
  dispatcher; tools/jt1_extract.py always had it right). The session
  hand-decodes assumed [count][default][val,off], shifting EVERY arm
  one case over. FIXED: jt327 relabelled (the arm map now matches the
  DLItem cmd conventions: 1=paint, 2=hit, 5=key), jt599's cast-sound
  (47->12, 51->9, default->3) and explosion tables re-bound. AUDIT of
  all other hand-decoded JT[1] sites = task #122.
  - l2df8 FULL (the line-editor keystroke primitive: left/home 262/263,
    right/end 258/261, segment prev/next 264/260 via L21a4, BS 8 falls
    into DEL 266, printable [32,126] insert with the trailing-space
    reuse + JT[1080] beep; the seg-mode validation tail with the
    recursive UNDO on L2756 failure and the gap re-clamp).
  - L20c2 FULL (segment lookup + advance predicate), L21a4 FULL
    (segment hop, column-preserving), L2724 FULL (250-byte tail scrub).
  - jt328 FULL (the multi-line BIG-field method, all 14 arms: cursor at
    buf[235], maxw 233, seg mode; ESC restores the full 250-byte copy;
    the 24x152 grid box; L1dd8/L1eec flag-0 paths now live).
  - LEAF STUBS left: l2756 (the ~1.7KB segment re-tokenizer + its
    L2dca/L2d5e char classes), l24e8/l2410/l1f6c (the word-wrap paint
    layer) — the next design-tools slice.
