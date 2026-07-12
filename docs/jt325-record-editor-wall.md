# jt325 — the design-record database engine / record editor (Phase D)

`jt325` = **CODE 9 + 0x22d8** (`l22d8` = `jt325`). The "design-record database
engine": it stages / loads / stores a fixed-size record AND runs the
**interactive record editor** over it. It is the shared engine behind several
deferred editors — lifting its tail unblocks all of them at once.

## Why this matters

`jt325` has **7 call sites** (boot.c):

| site | caller | type arg | record / screen |
|------|--------|----------|-----------------|
| 2077  | (combat setup) | 57 | NPC Editor |
| 14833 | GEO cell editor | `code` | map/area cell |
| 15808 | **jt251 (mode 4, Edit Modules)** | **53** | Game settings |
| 66208 | CODE 16 handler | `t348` | (spell/ability) |
| 72888 | (setup) | `jt348(a2)` | (dynamic) |
| 73206 / 73287 | jt263 (monster/NPC) | `type` | monster / NPC |

The immediate goal (Edit Modules → the sub-editors) needs the **type-53** path,
but the tail is a generic engine — type 53 takes the same field-editor path as
every other record type, so there is no shortcut: the tail must be lifted.

## The SCRIPT.GLB connection (verified 2026-07-10)

`jt325`'s prologue loads **SCRIPT.GLB** into FAR-pool group 24 (the group-24 /
MENU collision that hung Edit Modules is fixed — commit bee70a4;
[[area-map-post-event-clobber]] sibling). SCRIPT.GLB is a GLIB of **58 entries**
(`'DATA'` group). **Entry N is the field-layout script (a bytecode stream) for
record type N** — confirmed:

- entry 1  = `"combat event"`
- entry 53 = `"Game settings"` — first field `"adventure design name"` (the
  type-53 editor jt251/Edit-Modules drives)
- entry 57 = `"NPC Editor"` (the type-57 caller at 2077)

Each entry starts with a Pascal-ish title, then a field-descriptor bytecode the
**field codec L1ae2** interprets to serialize / edit the record's fields. Decode
the opcode set once L1ae2 is mapped (do NOT hand-guess the bytecode).

## Structure

- **Prologue 0x22d8..0x242c — DONE** (staging: file-group setup, SCRIPT.GLB load
  into grp 24, point the staging cursor at g_a5_-22208 / grab field buffer
  g_a5_-11656, per-cmd stage init, control-block header).
- **Tail 0x242c..0x30c2 — ✅ LIFTED (commit 482e008, 2026-07-10)** as
  `jt325_tail` (split from the prologue so all ~40 callees are in scope; the
  prologue tail-calls it). Builds clean (020 codegen 2022→2037), 129 tests
  pass, menu boots. jt325 is no longer a provisional-0 stub — cmd 2/3 now enter
  the live modal editor. **Live mouse-drive still PENDING** (Hatari/SDL isn't
  injecting synthetic clicks in this environment even with `--mousewarp no`;
  the in-game cursor never moves — orthogonal to the lift). Verified
  block-by-block vs CODE_09.s. The field-serialization + interactive editor.
  Dispatch on `cmd` (byte `fp@(25)`) and `type` (word `fp@(18)`):
  - **L242c** (cmd==3 fetch): calls JT[76], JT[447], **L1ae2** (field codec,
    fetch dir), then per-field descriptor math (18-byte entries in the -11656
    field buffer) + bitfield extract + L093a / L01a2.
  - **L258e**: cmd→status mapping (fp@-14 status, fp@-35 flag); type 1/33 → L0d84.
  - **L25ea → L2626+**: the interactive editor UI (JT[1089] "Page %2d" formatting,
    JT[155] driver, JT[452] menus) — the modal field-edit loop.

## Tail CFG (0x242c..0x30c2) — mapped 2026-07-10

| Block | Addr | Purpose |
|-------|------|---------|
| **A** cmd-3 field fetch | L242c 0x242c | if cmd==3: L1ae2 (fetch dir) + per-field descriptor math → pack value to row[10] |
| **B** cmd→status map | L258e 0x258e | non-interactive: map cmd→status(fp-14)/flag(fp-35); type 1/33→L0d84; L1ae2 (encode) → L2adc |
| **C** interactive editor UI | L2626 0x2626 | modal field-edit loop: rebuild list, draw name + "Page %2d", run driver, render+dispatch each widget row. nav JT[1]@0x27b2 {0=commit,1=page−,2=page+,4/27=cancel}; field-edit JT[1]@0x289a on row[16] {96,128..133}→pickers |
| **merge** finalize/commit | L2adc 0x2adc | validate (mnemonic scan, width loops via L0052/L06e0); per-cmd COMMIT (jt257/jt255/jt256/jt406/jt321); type 1/33→L0e00; jt461(24); return status. NB **jt406 direction**: Mac stack fp@8=staging,fp@12=src → port `jt406(src, staging, count)` (reversed BlockMove ABI) — copies edited staging back to caller. |
| **D** nested sub-editor | L30d4 0x30d4 | field-type 133: swaps -27932/-11660, -18485=5, own edit loop |

## Helper tree — worklist (mapped 2026-07-10)

| Helper | off / lines | status | purpose | inline switch |
|--------|-------------|--------|---------|---------------|
| l0052 | 0x0052 / 121 | ✅ LIFTED (`boot.c`, plain name) | typed field READER over stage(-11660) | JT[3]@0x008e 50..53 |
| l0006_c09 | 0x0006 / 29 | ✅ LIFTED (this session) | field byte-offset from descriptor | — |
| l0e2c=jt323 | 0x0e2c / 169 | ✅ LIFTED (jt323) | draw action direction-arrow row | — |
| l224a=jt324 | 0x224a / 45 | ✅ LIFTED (jt324) | readied-list kind scan → fire DLItem | — |
| l0e00 | 0x0e00 / 16 | ✅ LIFTED | type 1/33 editor FINALIZE (L308e) | — |
| l0d84 | 0x0d84 / 41 | ✅ LIFTED | type 1/33 editor SETUP (L258e) | — |
| l376a | 0x376a / 77 | ✅ LIFTED | value picker, field-type 132 (64-item jt169 list) | — |
| l3342 | 0x3342 / 107 | ✅ LIFTED | value picker, field-type 129 "Item Kind" (16/17 from -10908) | — |
| l348e | 0x348e / 125 | ✅ LIFTED | value picker, field-type 130 item (l3342 kind + jt188 filtered list) | — |
| l3876 | 0x3876 / 240 | ✅ LIFTED | editor validation-error dialog, JT[1]@0x388e codes 1/2/3/256/257/258 | JT[1]@0x388e |
| l06e0 | 0x06e0 / 203 | ✅ LIFTED | field WRITER — the l0052 counterpart (packs value → staging, dirty@2510) | JT[3]@0x071c 50..53 |
| l093a | 0x093a / 365 | ✅ LIFTED | widget-row APPLY+write (l06e0), 4 dispatch tables | JT[3]@0x096a, @0x0a48; JT[1]@0x0bde, @0x0cae |
| **l01a2_c09** | 0x01a2 / 434 | ✅ LIFTED | widget-row RECOMPUTE + display-text format (128..133) | JT[3]@0x01e8, @0x03b6, @0x0494 |
| l1ae2 | 0x1ae2 / 566 | ✅ LIFTED | the SCRIPT record LOOP — framework + ALL 11 arms (3-10, 32-34). | JT[1]@0x1c4c, JT[1]@0x21a8 |
| ↳ l100c | 0x100c / 913 | ✅ LIFTED | **the field-byte CODEC** — JT[3]@0x109a (48..79, ALL 32 arms). Sig `l100c(desc,rec,w2,w3,mode)`; header parse + field loop; label/banner/numeric/cell/value/string/flag-bit field types. Called 1× by l1ae2. | JT[3]@0x109a 48..79 |
| ↳ l3bbc | 0x3bbc / 330 | ✅ LIFTED | picture/item/class PANEL drawer — JT[3]@0x3bc8 (1..8): combat-pic frames (jt118×N loop), item-icon grid (jt28/jt479/jt184/jt444, like l01a2 c130), "%2d"/class rows. jt118 arg order VERIFIED (page ignored; port jt118(NULL,top=B,left=A,idx=C,handle) = Mac fp@10/fp@8/fp@12/fp@16). | JT[3]@0x3bc8 1..8 |
| jt373 | CODE8+0x4 / ~590 | 🔨 WIP | list-widget LDEF (arm 7's method) — head+msg switch+exit+default lifted; 10 handlers + local helper tree pending | JT[1]@0x003c (16 msgs) |
| l30d4 | 0x30d4 / 203 | ✅ LIFTED | nested type-133 SPELL-MEMORIZATION sub-editor (modal: memorize/forget/exit) | JT[3]@0x31a6 |

> **l1ae2 SCOPE (measured 2026-07-10):** "l1ae2" is really a ~1800-line
> subsystem = the loop (566) + l100c the field codec (**913**) + l3bbc the
> drawer (330); l100c/l3bbc are exclusive to l1ae2. This is the LAST and by far
> the LARGEST block of Phase D — a focused multi-session effort of its own; every
> other helper is done. Recon complete: JT[1]@0x1c4c (11 arms: 3-10,32-34),
> JT[1]@0x21a8 (3,32,33,34→one arm), l100c JT[3]@0x109a (48..79), l3bbc
> JT[3]@0x3bc8 (1..8) all decoded. CAVEAT to verify first: **jt118** (l3bbc's
> blitter, 14 sites) is itself `__attribute__((unused))` — confirm its 5-arg
> order (page,top,left,idx,handle) against CODE 6+0x37d6 before trusting l3bbc's
> `jt118(336,x,23,0,picbuf)` mapping; page=336 looks like a piece/coord id, not
> a pointer, so the port lift's arg order may need a second look.

> **Collision traps (confirmed):** bare `l01a2` is a PROBE stub for jt1079 → use
> **`l01a2_c09`**. `l0006`/`l0006_c15`/`l0006_c17` exist → **`l0006_c09`** (done).
> `l0052` unsuffixed is fine (no other segment claims 0x0052). L1ae2 pulls in two
> more CODE-9 locals **L3bbc** (0x2146) + **L100c** (0x218a) — alias-check before
> lifting. All JT-entry callees (jt399/406/452/468/1012/1089/384/397/423/455/…)
> are already lifted.

The Phase-D helper cluster lives with its CODE-9 siblings just before jt323
(~boot.c:68908), where every JT callee is already declared — not up by jt325
(line ~1700), which would need dozens of forward decls.

## l100c arm map (JT[3]@0x109a on the field type byte; CODE_09.s)

Sig `l100c(desc, rec, w2, w3, mode)` (2 longs + 3 words; rec=fp@12, mode=fp@21).
`base` = fp@(-16) (record header, saved). Loop: `while (remaining>0)` — each arm
advances `desc` past its data, then the loop bottom (L1acc) does `desc++;
remaining--` for the type byte. ✅ = lifted.

| type | arm | role / key calls |
|------|-----|------------------|
| ✅ 48 | L10e0 | title/label — jt1089 or jt452(38,...); color from desc[1] (0/1/2→135/139/140) |
| ✅ 49 | L119a | banner label from header (jt1161 fill + jt1089); +1 |
| ✅ 50/51/52 | L1222 | NUMERIC field — l01a2_c09 recompute, JT[7] div, l0006/l0052, jt452/jt193, L13ec word-wrap sub-loop (+3 advance) |
| ✅ 53 | L14ca | recompute (l01a2_c09 + JT[7]); variable advance desc[1]*2+2 |
| ✅ 54 | L1528 | cell/checkbox — JT[3] on desc[1] (50-53); jt444 (cmd 34 packs &jt323); optional jt1161 banner; +2 |
| 55 | L16c0 | (advances desc+1 @L16b4) |
| 56 | L174a | variable advance (`addl d0,desc` @L173a) |
| 57 | L17b2 | +4 advance (L180e) |
| 58-63 | L181a | jt358; +4 advance |
| 64-67 | L185c | — |
| 68 | L1884 | — |
| 69 | L18e6 | jt452; +1 |
| 70 | L1910 | l06e0 write; +1 |
| 71 | L1950 | l06e0 write; +2 |
| 72 | L19a4 | l06e0 write; +4 |
| 73 | L1a18 | — |
| 74-79 | L189e | l0006 + jt384; variable advance (`addl d0,desc` @L1ac0) |

WATCH: some arms jump to L1ad4 (test) directly after a full advance → in C those
cases must `continue` (skip the loop-bottom `desc++`); arm 48 uses L1acc so it
does NOT continue. Determine per-arm from where it branches.

## l1ae2 widget-build arms (JT[1]@0x1c4c on the field type; each -> L2158)

Inside `if(match){ row[0]=rec_ptr; row[12]=type; row[13]=jt455(); row[4]=0;
switch(type){...} }`. Arms draw the field's editor widget (jt452 DLItem menus w/
method ptrs jt327/328/335, jt1089 labels, l3bbc panels). Then the common L2158
does tbl[0]++/row+=18. base=fp@-16(record header); rec_ptr=fp@-12(field record).

| type | arm | role / key calls |
|------|-----|------------------|
| ✅ 3 | L1c7c | static label — jt1089 "%s" at (8000+base[3],8000+base[4]) colour 140; row[13]=-1, row[15]=0x8c |
| ✅ 4 | L1cce | jt452 shape-4 field menu (11 args) |
| ✅ 5 | L1d24 | direction/arrow-cell setup — row[15] tables (tbl+..550/546/706), jt452 shape-8 w/ jt335 method |
| ✅ 6 | L1e1c | (base[6]==126 skip) list column — jt452 shape-8 w/ jt328 + jt384 name |
| ✅ 7 | L1ec4 | jt452 shape-8 list column (jt373 method) — row*22 tables +2374/2378/2382/2390 |
| ✅ 8 | L1fa6 | labeled cell — jt1089 label + jt452 shape-1 (len-positioned) |
| ✅ 9/10 | L2076 | jt452 shape-8 w/ jt327 (2986 table) + jt384 |
| ✅ 32 | L2112 | row[13]=d0; tbl[542]=rec_ptr[3] |
| ✅ 33 | L2130 | row[13]=d0; l3bbc(rec_ptr[3]) — the panel draw |
| ✅ 34 | L214e | row[13]=d0 |

## Lift order (leaves → up)

1. ✅ l0052, l0006_c09, l0d84, l0e00, l376a, l3342, l348e (done); l0e2c=jt323,
   l224a=jt324 (already lifted). **All the leaves are cleared.**
2. mid: ✅ l3876, ✅ l06e0, ✅ l093a, ✅ l01a2_c09 — **the mid tier is cleared.**
4. big — l1ae2 subsystem (~1800 lines, its own multi-session arc):
   a. ✅ l3bbc (330, drawer) — DONE; jt118 arg order verified.
   b. ✅ l100c (913, the field-byte codec, ALL 32 arms) — DONE.
   c. l1ae2 (566, the loop) — wires l100c/l3bbc + builds the field rows.
   (l30d4 deferrable — only field-type 133 reaches it.)
5. ✅ the tail dispatch (blocks A/B/C/merge) → jt325's DEFERRED block dropped;
   lifted as `jt325_tail` (commit 482e008). **Phase D is COMPLETE.**

## Verification

Build clean, 129 tests pass, menu boots. Transcription verified block-by-block
vs CODE_09.s. **✅ LIVE-VALIDATED (2026-07-10)** by driving the menu with
KEYBOARD accelerators (no mouse): `driver.sh key e` = EDIT MODULES →
l0004_22(6) → l0096 mode 4 → jt251 → jt325 → the record editor renders "GAME
SETTINGS" with the real field labels/edit boxes + PAGE 1/3 + OK/PREV/NEXT/
CANCEL; `key n` = NEXT pages to "KEYS IN SPECIAL INVENTORY" (PAGE 2). Two l1ae2
field-build bugs found + fixed while driving it (commit f179eae): the pass-loop
`break`→`continue` (type>=51 built no fields) and the stage[454] page-count
byte→word write (read 768 pages). **stg[456]=0 TRACED — FAITHFUL, not a bug:**
the descriptor table (stg[456]/stg[458]) is built in l100c only for widget types
6/10 (`if(w3){if(base[0]==6||10){…stg[456]++}}` @0x1312), and Game Settings uses
types 5/7/8/9 (logged base[0]=9,8,7,5,…) → stg[456] stays 0 on the Mac too.
Non-6/10 fields save via the field-edit→l06e0 direct staging writes + the
jt406(src,staging,count) copy on OK — not the descriptor loop. Commit VERIFIED
clean (`key o`=OK returns to menu). Still untested headless: a field-EDIT
round-trip (click-to-focus, mouse unreliable) and the 6/10 write-back loop
(needs a list/mnemonic record, e.g. NPC/monster editor).

## The big text field (widget type 6 → jt328) — 2026-07-12

**Widget type 6 is the EVENT MESSAGE box** — the 6-row × 38-column text area
you type an event's prose into ("player reads:", "ask:"). It is far from rare:
**30 of SCRIPT.GLB's 58 record types carry one**, and they are exactly the event
types (1..38). Game Settings (53) has none, which is why the whole widget went
unexercised for so long. Scan for them with the field walk below — a field
record's fields advance by an LE16 length at bytes 1..2, terminated by 0x02;
the field-record chain terminates by 0x00.

Two independent defects had to fall before the field could appear:

1. **Its word-wrap layer was stubbed** — l2756 (segment table), l24e8 (row
   paint), l2410 (cursor cell). Now full lifts; see the commit for the two
   faithful vestiges (the dead `pos < 0` rebuild path; the unreachable `i > row`
   guard).

2. **`jt452` dropped every bare-setter stream.** The port initialised its
   stream target to `rec = NULL`, so a `jt452(cmd, val, 0)` call that did NOT
   open with a shape token 1..8 hit the `if (rec != NULL)` guards and did
   nothing. The Mac (CODE 3 + 0x29a0, L29ac) starts the target at the
   **last-allocated item**: `rec = pool + count * 32 - 32`. That is precisely
   how l100c hands the big field its edit buffer — l1ae2's arm 6 sends only
   cmd 35 (rec+8 = the 290-byte bound block: 250 revert copy + 40 label) and
   cmd 40 (position), and it is l100c's numeric arm (field types 50/51/52) that
   afterwards issues the standalone `jt452(39, stage + i*250 + 474, 0)` setting
   rec+12 = the live buffer. With the target NULL that never landed, `buf`
   stayed NULL, and jt328's paint bails at `if (buf == NULL) break;` — so the
   field drew *nothing at all*: no box, no label, no text. Fixed faithfully.

**LIVE (2026-07-12):** temp-repointing jt251's driver from type 53 to 7 renders
the TAVERN EVENT editor with "PLAYER READS:" and its 6-row box (measured 304×48
engine px = 38 cols × 8px, 6 rows × 8px — correct); to 37 renders TAVERN TALES
with "TALE 1:"/"TALE 2:". Before the jt452 fix both pages showed a blank
rectangle. Reverted after.

**STILL OPEN:** (a) the field does not take FOCUS on click — jt328 cmd 2/3 are
reached but the modal loop never focuses it, so typing falls through to the menu
accelerators; (b) **l1f6c** (mouse position → cursor index on the 38×6 grid) is
still a PROBE stub returning 0, so even once focus works a click will land the
cursor at the start of the text; (c) the box renders empty unless the record
supplies text — real event text is fetched by string index from the event
record, so seeing it live needs the actual event-editor flow, not a repointed
Game Settings record.

## Field FOCUS — the big field is typeable (2026-07-12)

`l2d3e`'s click path force-committed **every** hit (`rec[28] |= 0x10; return i;`),
so clicking a text field left the dialog instead of focusing it. The field was
therefore never focused, and — because the key phase walks every DLItem with
cmd 5 and only a FOCUSED item consumes the keystroke — typing fell straight
through to the menu accelerators and exited the editor.

Fix: before the commit, ask the item through its own protocol —
**cmd 128 = "are you focusable?"** (jt327/jt328 answer 1), then **cmd 18 =
"take focus"** (runs l1dd8, which sets rec[28] bit 2). If it really took focus,
repaint and STAY in the dialog. No shape test, and self-correcting: an item that
claims to be focusable but does not take focus falls through to the button path
unchanged. Keys then arrive on their own — Phase 5 was already faithful.

**LIVE:** click into TAVERN EVENT's "PLAYER READS:" box and type — the text
appears, word-wraps at the column-38 boundary, and the cursor block tracks.

**~~Why not the full Mac click sequence?~~ — IT IS THE SEQUENCE NOW (2026-07-12).**
L2dc2..L2e28 (`cmd 3` track → `cmd 4` activate → commit iff `rec[28]` bit 4) is
what the click path runs. The earlier attempt froze jt169's List Manager dialog
and I blamed unlifted action procs. **That diagnosis was WRONG: it was the
transposed click latch** (fixed in 3bffc1b). `l31b8` → `jt1132` hands back the
BUFFERED click, whose (v, h) the port had swapped, so cmd 3's track hit-tested
against crossed axes, decided the release landed OFF the item, and returned 0 —
no pick, for every button. With the latch corrected, jt169's Add/Exit, the main
menu and the Hall menu all set their own commit bit. **No action proc needed
lifting.** `menu_button_track()` and the `hr[28] |= 0x10; return i;`
force-commit are both gone.

**One stand-in remains** in the click block: the char-gen radio-container probe.
That one is real — removing it and re-testing, the RACE/CLASS picks stopped
moving (stuck on their defaults), so the row's own cmd-4 → container dispatch
is genuinely unlifted, not a latch artefact.

~~Also note `menu_button_track()` is a port reimplementation of l1676's cmd-3
track loop, and it CANNOT simply be replaced by `method(rec, 3, ...)`...~~
**SUPERSEDED — `menu_button_track()` is GONE (2026-07-12).** It only existed
because the faithful loop's hit test could not work: L31b8 -> jt1132 returns the
BUFFERED click, and the port's latch had (v, h) transposed, so cmd 2 was fed
swapped axes and the track never matched the item under the cursor. With the
latch corrected, `method(rec, 3, ...)` is the track. The ordering worry was
real but is handled: focusable items take their cmd 3 in the FOCUS branch
ABOVE (which returns), so a field never reaches the button track and jt328's
cmd-3 click poll is never entered after a release.

**PRE-EXISTING BUG (not from this work, A/B-confirmed against HEAD):** in the
party-roster picker, clicking **ADD** wedges the dialog — EXIT stops responding.
EXIT works fine if ADD is never pressed.

## Click-to-place-caret — l1f6c + a transposed click latch (2026-07-12)

`l1f6c` (CODE 8 + 0x1f6c) is now a full lift: JT[1113] reads the pointer,
JT[1139] maps it into the field's grid, the cell is `(v / 4, h / 4)` — the same
4-unit step L2410/L24e8 paint on — and the row's segment (start, end) clamps
where the caret may land (empty row -> end of text; last row -> min(end,
start+37); interior row -> end - 1, keeping the caret off the wrap's break
space).

Two things had to change around it before it could ever run:

1. **The focus branch must send cmd 3, not cmd 18.** `l1f6c` lives inside
   jt328's **cmd 3** arm (the same arm the Mac's l2d3e hands every clicked
   item): poll -> re-hit -> `buf[235] = l1f6c(...)` -> l1dd8. cmd 18 ("focus
   request") only runs l1dd8, so it focused the field but left the caret where
   it was. ALSO: jt328's **cmd 128 FALLS THROUGH into cmd 26 (defocus)**, so
   probing an already-focused item un-focuses it — a second click inside the
   field dropped focus and then committed the dialog. Only probe cmd 128 when
   rec[28] bit 2 is clear; an item holding focus is focusable by definition.

2. **★ THE CLICK LATCH WAS TRANSPOSED.** L6cba stores `ev->where` — a Point
   LONG whose HIGH word is v and LOW word is h — and latches
   `-908 = clamp(v, L04cc())`, `-906 = clamp(h, L04de())`, which is exactly the
   order JT[1113] hands back (`out_y` from -908, `out_x` from -906). The port
   had the two **swapped**, so every consumer of the buffered click got its axes
   transposed: jt328's cmd-3 hit test missed the field it had just been clicked
   on (so cmd 3 bailed before l1f6c and the caret never moved), and **l1676's
   cmd-3 track loop hit-tested against swapped axes too** — which is very likely
   why the port grew `menu_button_track()` as a stand-in for it. This is the
   SECOND transposed primitive found, after jt1089's MoveTo (#116). If a click
   consumer behaves as though the axes are crossed, check the latch.

**LIVE:** type into the big field, click on the "L" of LEANS mid-text — the caret
lands on that character — then type: `(big) ` is inserted THERE, the paragraph
re-wraps, and the caret tracks. Regression-swept: main menu, design-picker rows,
Hall roster rows, Add/Exit, char-gen race/class radio picks.


## l2d3e's click block is now fully faithful (2026-07-12)

The last stand-in is gone. `jt380` — the shape-3 method, which is what a char-gen
pick ROW is — was missing its **cmd 4 arm (L23ba)** entirely: the port sent every
cmd but 1 and 2 to `l1676`, whose action arm reads `rec[4]`, and a row carries no
proc there (jt452's cmd 34 sets it on the CONTAINER). So clicking a race or class
did nothing, and `l2d3e` had grown a stand-in that guessed the container by
scanning back for "the nearest item with a pointer at rec[8]".

The Mac's L23ba walks BACKWARD until it finds the item whose **method IS the
shape-2 handler** (`g_a5_-9278` — the handler table at -9282 holds one entry per
shape, so shape 2 lives at -9278), counting the steps: that count is the row's
CHILD INDEX. It hands that to the container through its own cmd 4 (jt381 — moves
the radio bit, writes the value-output global, fires jt566/jt567/jt568/jt569/
jt570), then fires the row's OWN action proc with its DLItem index if it has one
(the stand-in never did that second part). Comparing the METHOD, not guessing
from rec[8], is the difference.

**All three stand-ins in the click block are now gone** — `menu_button_track()`,
the `hr[28] |= 0x10` force-commit, and the radio-container probe. Two of the
three turned out to be masking a single broken primitive (the transposed click
latch); only this one was a genuinely missing lift.

**LIVE:** all four char-gen radio groups (RACE, CLASS, GENDER, and both ALIGNMENT
groups — which sit stacked, so they prove the walk finds the RIGHT container);
main menu; Hall menu; party-picker rows; Add/Exit; and the big text field.
