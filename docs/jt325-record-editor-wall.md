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
- **Tail 0x242c..0x30c2 — DEFERRED (this Phase).** The field-serialization +
  interactive editor. Dispatch on `cmd` (byte `fp@(25)`) and `type` (word
  `fp@(18)`):
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
| **merge** finalize/commit | L2adc 0x2adc | validate (mnemonic scan, width loops via L0052/L06e0); per-cmd COMMIT (jt257/jt255/jt256/jt406/jt321); type 1/33→L0e00; jt461(24); return status |
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
5. the tail dispatch (blocks A/B/C/merge) → drop jt325's DEFERRED block.

## Verification

Headless mouse now works (`driver.sh click x y`, `--mousewarp no`;
[[headless-arrows-and-roster-garbage]]) — so once the tail lands, drive Edit
Modules → screenshot the field editor → click a field/module → confirm.
