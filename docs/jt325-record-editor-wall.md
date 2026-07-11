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
| l0e00 | 0x0e00 / 16 | ⬜ leaf | type 1/33 editor FINALIZE (L308e) | — |
| l0d84 | 0x0d84 / 41 | ⬜ leaf | type 1/33 editor SETUP (L258e) | — |
| l376a | 0x376a / 77 | ⬜ leaf | value picker, field-type 132 | — |
| l3342 | 0x3342 / 107 | ⬜ leaf | value picker, field-type 129 (→ val or <0) | — |
| l348e | 0x348e / 125 | ⬜ leaf | value picker, field-type 130 | — |
| l3876 | 0x3876 / 240 | ⬜ mid | editor error/beep dialog (codes 256/257/258/101/102), 5 sites | — |
| l06e0 | 0x06e0 / 203 | ⬜ mid | field WRITER — the L0052 counterpart (packs value → staging) | JT[3] type switch (verify) |
| l093a | 0x093a / 365 | ⬜ mid | widget-row RENDER (depends on l0006_c09) | JT[3]@0x096a 3..10 |
| **l01a2_c09** | 0x01a2 / 434 | ⬜ mid ⚠ | widget-row RECOMPUTE (row[16] switch mutates row[8]) | JT[3]@0x01e8 2..10 |
| l1ae2 | 0x1ae2 / 566 | ⬜ BIG | the field CODEC: loop over layout SCRIPT, per-field-type dispatch; jt468(24)/jt1012 read SCRIPT.GLB, JT[452]×6 | JT[1]@0x1c4c (11), JT[1]@0x21a8 (4) |
| l30d4 | 0x30d4 / 203 | ⬜ (defer) | nested type-133 sub-editor | own switches |

> **Collision traps (confirmed):** bare `l01a2` is a PROBE stub for jt1079 → use
> **`l01a2_c09`**. `l0006`/`l0006_c15`/`l0006_c17` exist → **`l0006_c09`** (done).
> `l0052` unsuffixed is fine (no other segment claims 0x0052). L1ae2 pulls in two
> more CODE-9 locals **L3bbc** (0x2146) + **L100c** (0x218a) — alias-check before
> lifting. All JT-entry callees (jt399/406/452/468/1012/1089/384/397/423/455/…)
> are already lifted.

## Lift order (leaves → up)

1. ✅ l0052, l0006_c09 (done); l0e2c=jt323, l224a=jt324 (already lifted).
2. small leaves: l0e00, l0d84, l376a, l3342, l348e.
3. mid: l3876, l06e0, l093a, l01a2_c09.
4. big: l1ae2 (+ its leaves l3bbc, l100c; decode JT[1]@0x1c4c/@0x21a8 first); l30d4 (defer).
5. the tail dispatch (blocks A/B/C/merge) → drop jt325's DEFERRED block.

## Verification

Headless mouse now works (`driver.sh click x y`, `--mousewarp no`;
[[headless-arrows-and-roster-garbage]]) — so once the tail lands, drive Edit
Modules → screenshot the field editor → click a field/module → confirm.
