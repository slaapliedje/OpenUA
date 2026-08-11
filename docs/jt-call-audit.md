# JT-call audit — lifted C bodies that drop a call the Mac makes

`tools/jt_call_audit.py` diffs every annotated function in `src/engine/boot.c`
against its original body in `data/work/disasm/CODE_NN.s` and reports the
`JT[n]` targets the asm calls and the C does not.

It exists because of #137. The char-gen icon grid rendered black on all five
backends, and the cause was 65 hand-written lines standing in for one
`jsr JT[124]`. Nothing flagged it — the code compiled, ran, and carried a
comment explaining why the JT call was skipped. See the `l09dc` comment and
commit `12917a32`.

**Correction (2026-08-10):** the original write-up said the workaround was
correct when written and broke later, when the GLIB pool flip (`43d813f5`,
2026-07-13) changed `jt468` to return the extracted sub-GLIB. That dating was
inferred from commit messages, never measured, and `tools/icongrid_repro.sh`
has now disproved it: the grid is **already black at `43d813f5^`**, the commit
immediately before the flip. The mechanism at HEAD is measured and stands
(`l37aa(set1, 0)` returns 0, so no palette is installed); the causation does
not. No commit the probe can judge has ever shown a coloured grid through the
LIVE route — at `e05e625b`, the commit whose message claims the colours were
fixed, the route cannot even reach char-gen (it stops on the Hall screen), so
that claim was almost certainly verified through the `FRUA_BODY` harness, which
calls `jt573` directly and passes where the live `jt574` path fails. The
likeliest reading is that this was never a regression at all.

**A hand-rolled substitution encodes an assumption about the data or the
loader, and nothing re-checks that assumption when either changes.** Lifted
code follows them automatically. That is the whole argument for this check.

## Running it

```sh
python3 tools/jt_call_audit.py                                  # everything
python3 tools/jt_call_audit.py --func l09dc                     # one function
python3 tools/jt_call_audit.py --max-missing 2 --min-clines 40 --skip-todo
```

The last form is the **#137 shape**: a substantial C body (so not a PROBE stub),
dropping only one or two calls (so not an undone lift), with no admitted TODO.

Calibration is part of the tool's history, not a claim about it: run it against
`git show 12917a32^:src/engine/boot.c` and it reports `l09dc … NOT in C: JT[124]`;
run it against HEAD and `l09dc` is clean. `tests/test_jt_call_audit.py` pins the
behaviour on synthetic fixtures (the real disassembly is git-ignored).

## THINK C runtime is excluded

CODE 1's low jump-table entries are compiler runtime, not engine calls. A
faithful lift never calls them, so counting them buried the real findings under
100+ false hits on the first run:

| entry | what it is | what the C writes |
|---|---|---|
| `JT[1]` | sparse switch on a word | `switch` |
| `JT[2]` | sparse switch on a long | `switch` |
| `JT[3]` | range switch (min/max/default table) | `switch` |
| `JT[4]` | 32×32 → 32 multiply | `a * b` |
| `JT[5]` / `JT[6]` | unsigned long divide / modulo | `a / b`, `a % b` |
| `JT[7]` / `JT[8]` | signed long divide / modulo | `a / b`, `a % b` |

Verified by disassembling CODE 1 `0x130..0x20c` and against boot.c's own lifts
of `jt4`..`jt8`, which are literally `return a * b;` / `return a / b;`.

## The three most-dropped targets — all idioms, not bugs

`JT[1200]`, `JT[399]` and `JT[394]` led the first drop list by a wide margin.
They are not 113 near-misses; they are three functions the port legitimately
expresses in C. Documented here so nobody re-investigates them:

| entry | CODE | what it is | why a lift may not call it |
|---|---|---|---|
| `JT[1200]` | 4+0x04f0 | display-mode query | the HAL often answers the mode question itself, or the port only ever takes one branch |
| `JT[399]` | 3+0x39d2 | `memset(buf, fill, size)` | C writes `memset()` or an explicit zeroing loop |
| `JT[394]` | 3+0x4796 | `sprintf(buf, fmt, ...)` | C writes `sprintf`/`snprintf`, or builds the string directly |

Spot-checked: `jt21` and `jt910` zero their buffers with plain
`for (i…) x[i] = 0;` loops rather than `jt399`. Faithful, and invisible to a
call-presence check.

### `JT[1200]` picks the file EXTENSION — keep this one in mind

The most useful thing found in this sweep. In the GLIB binder `l33ac`
(CODE 6+0x33ac) the display mode chooses which art library to open:

```
3478: jsr JT[1200]                 ; which display mode?
3486: pea "%s.ctl"    → JT[394]    ; colour
349e: pea "%s.tlb"    → JT[394]    ; deep / 1bpp
34c2: jsr JT[1200]                 ; again, for the numbered form
34dc: pea "%s%d%03d.ctl"
3500: pea "%s%d%03d.tlb"
```

Our `l33ac` does this faithfully — `jt394(path, (jt1200() == 3) ? "%s%d%03d.tlb"
: "%s%d%03d.ctl", …)`. And the distinction is live in the DOS-converted set:
all 23 `.CTL`/`.TLB` pairs have the **same size but different bytes**, so they
are genuinely different art, not a duplicated file. (The matching sizes are a
coincidence of the container format and briefly looked like a collapse — they
are not.) This is the mechanism behind the mono/B&W art requirement recorded in
the project memory: mode 3 asks for `.TLB`.

## Is any of this 1.0 vs 1.2?

**No.** The audit compares our C against the **1.0** disassembly, which is the
lift target (ADR-0001), so every drop is 1.0-internal by construction. And the
jump table is *permuted* between the forks (1208 entries vs 1207), so JT numbers
are not even comparable across them — see `tools/mac12_diff.py`'s header.

Of the three, only `JT[399]` appears in the 1.2 worklist at all: hunk 8 changes
its fill value from 0 to 1 in `l36e0_c10` (CODE 10 `L38ba`), and that is already
ported and marked done in `docs/mac12-hunk-log.md`. Both 1.2 passes are complete
(33 of 38 structural hunks ported, the rest analysed and closed), so the DOS
data switch — not a version skew — remains the thing that changed under this
code.

## Result of the full sweep (2026-08-09)

**Every lead was verified by hand. One real drop, now fixed; everything else is
idiom substitution or a documented port deviation.**

1469 annotated functions compared, **75 drop at least one call**, **16 match the
#137 shape** — and all 16 were read against the asm and cleared:

| function | drops | verdict |
|---|---|---|
| `l309c` | `JT[1124]` | **REAL — fixed** (pending-text flush, see below) |
| `l309c` | `JT[1200]` | benign: `l2d4e` re-parameterised, `* 8` moved inside |
| `jt1161` | `JT[397]`, `JT[413]` | idiom — `max()` / `min()` |
| `jt463` | `JT[1026]`, `JT[1028]` | idiom — `_FreeMem` / `_NewPtr` via the compat shim |
| `jt452`, `jt453` | `JT[1084]` | omits the error-alert path only |
| `jt94`, `l6eea` | `JT[394]` | idiom — `vsprintf` |
| `jt921`, `jt590` | `JT[399]` | idiom — `memset` |
| `l78fa` | `JT[118]` | inlined: `jt118` IS `jt108(1) + jt1001(...)`, which it calls |
| `l5c1e` | `JT[486]`, `JT[1134]` | documented: the port pumps `l725c` via `jt1134` instead |
| `jt240` | `JT[148]`, `JT[179]` | documented deviation (the 8-slot bar overran 320px) |
| `l01be` | `JT[431]`, `JT[576]` | documented: `design_save_path` (GEMDOS, not HFS `:`), record read folded in |
| `l1276` | `JT[25]` | documented: the sheet paints its own name, `jt25` is the roster painter |
| `l3fd8` | `JT[108]` | documented substitution (`jt112` in its place) |
| `l0004` | `JT[983]` | desk-accessory plumbing — the HAL-moot class, no Atari analogue |
| `l6eea` | `JT[109]` | documented colour band-remap tail |

So the honest score for the whole exercise: **one genuine bug found** (`l309c`,
fixed) out of ~30 raised, with the rest split between three C idioms and
deviations the code already explained. That is a reasonable yield for a
structural check, but it means **a hit here is weak evidence** — read the asm.

## Five defects this sweep found in the tool itself

The tool was wrong more often than the engine was. Each of these inflated the
findings, and each was caught by reading the asm rather than trusting output:

1. **THINK C runtime counted as calls.** `JT[1]`/`JT[2]`/`JT[3]` are switch
   dispatchers and `JT[4]`..`JT[8]` are 32-bit mul/div helpers; a faithful lift
   writes `switch`, `*`, `/`. 174 hits -> 120.
2. **Multi-line forward declarations parsed as definitions.** The `;`-test only
   read the first line, so brace-matching ran into the next function. Produced a
   confident false positive on `l33ac` (the GLIB loader) for `JT[1200]` and
   `JT[394]` — exactly the calls a loader would make, so it read as a real find.
   Its actual body makes both.
3. **Wrapper/implementation splits.** `jt110` is an 8-line forwarder to `l33ac`,
   both CODE 6+0x33ac; comparing the full asm to the wrapper gave 13 phantom
   drops. Now follows one level of delegation.
4. **Callbacks installed by ADDRESS.** `l63c0(..., (long)&jt237, (long)&jt236)`
   never matches a `name(` pattern, so every callback installation in the engine
   was reported: `jt241`, `jt240`, `jt169`, `l1bfe`, `jt511`, `l2558`, `jt239`,
   `jt127`, `l100c`. Now matches bare identifiers — with comments and string
   literals stripped first, or `PROBE("jtNNN")` and the doc comments would
   satisfy every check and the tool would find nothing at all.
5. **Extracted helpers.** The port splits one Mac function three ways and the
   dropped call lives in the piece: `<name>_suffix` (`l0980_slots` holds
   `l0980`'s `JT[661]`), a suffixed lift of the entry itself (`jt1154_pg` IS
   `JT[1154]`), and a helper named after an asm label *inside* the parent's span
   (`l45f0_menu` holds `jt955`'s four `jt155` calls and its `jt182` "Blocked:"
   prompt). All three are credited now.

Net: 174 -> 75 drops, 32 -> 16 shape matches. The calibration held through every
change — pre-fix `l09dc` reports `JT[124]`, HEAD is clean.

## Known limits

- Function spans in the asm end at the first `unlk`+`rts` (framed) or first
  `rts` (leaf), with the next known entry as a backstop. A function with two
  epilogues reads short, which **under**-reports. That is the deliberate
  direction to be wrong in.
- Call *counts* are not compared, only presence. Calling `jt117` once where the
  Mac calls it three times is invisible here.
- Only `src/engine/boot.c` is scanned, and only functions carrying a
  `(CODE NN + 0xXXXX)` annotation — 1422 of them.
