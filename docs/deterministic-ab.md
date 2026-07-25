# The deterministic A/B harness

Built 2026-07-25. It answers one question the older parity recipe could not:
**did this code change actually do anything?**

## Why it was needed

`docs/a5-residue-accounting.md`'s two-build recipe compares frames with
`compare -metric AE` and treats AE=0 as a pass. That works for A5-residue gaps,
but it cannot evaluate a *behavioural* change, because two boots of the same
binary already differ: the engine seeds its RNG from the wall clock, so dice,
initiative and encounter rolls diverge. The old recipe worked around this by
measuring a noise floor (AE 868 on the chargen sheet) and cropping to
deterministic rows.

That workaround failed on the first real test. The Mac 1.2 hunk-24 A/B
(2026-07-24) produced AE=1012 and the honest reading was "inconclusive": the
combat map was pixel-identical and only the highlighted combatant differed
(OGRE 22 HP vs GNOLL 10 HP / Halberd), which is initiative RNG, not the fix.
A diff that cannot separate signal from noise cannot confirm *or* refute.

## The mechanism

`-DFRUA_RNGSEED=<n>` pins `jt1143()`, the engine's **only** entropy source:

- `g_a5_-4902` is the single LCG state — `state = state * 0x6d25 + 1` in
  `jt1083`, with `jt485` / `jt870` / `ua_rand` all routed through it (#152
  merged the two former roller states).
- `jt1143()` = `GetDateTime() ^ TickCount()` is planted into it once at app
  init by `boot_a5_seed_defaults`.
- There is no `rand()` / `srand()` / `random()` anywhere in `src/`, `compat/`
  or `platform/` — checked, not assumed.

So fixing that one return value makes the whole dice/encounter stream
reproducible. Other `TickCount()` readers stay live (caret blink, scroll
repeat, sleep deadlines, and the `g_a5_-130` tick ORIGIN which is only ever
read as a difference); they affect *when* a frame settles, not what it
contains, and the measurement below confirms that empirically.

## The three controls — run all of them

A harness that reports AE=0 is worthless until you have shown it can report
anything else. Measured on Falcon/Hatari, HEIRS, boot → seat party → dungeon →
combat event → tactical map:

| control | command | result | proves |
|---|---|---|---|
| **determinism** | same seed, two independent boots | **AE 0** | the pipeline is reproducible end to end |
| **sensitivity** | change a string drawn on the compared frame | **AE 1016** | the comparison detects real change |
| **the change under test** | fix ON vs fix OFF, same seed | see below | attributable to the change |

The sensitivity control matters more than it looks. My first attempt at it
changed `"A battle begins..."` and got AE=0 — because that banner is not on the
frame being captured. **Pick a string you can see in the screenshot**; a
positive control that silently tests nothing is worse than none.

## Recipe

```sh
# 1. the seed and the landing cell (buildstamp trap: ALWAYS purge boot.o first)
rm -f src/engine/boot.o
make EXTRA_CFLAGS="-DFRUA_RNGSEED=12345 \
  -DFRUA_ENTRY_LEVEL=11 -DFRUA_ENTRY_ROW=17 -DFRUA_ENTRY_COL=5 -DFRUA_ENTRY_FACING=0"

# 2. drive a fixed key sequence
env -u DISPLAY FALCON_TOS=/usr/share/hatari/tos404.img \
  .claude/skills/run-falcon-port/driver.sh start
for k in p a Return Escape; do ... driver.sh key $k; sleep 4; done
... driver.sh key b; sleep 14
for i in 1 2 3 4; do ... driver.sh key Return; sleep 4; done
... driver.sh shots /tmp/a.png

# 3. flip the one line under test, rebuild (purge boot.o!), re-run, compare
compare -metric AE /tmp/a.png /tmp/b.png null:
```

**Prefer a state diff to a pixel diff where you can get one.** A `dbg_file_num`
of the variable the change touches is unambiguous and needs no positive
control. `-DFRUA_CBTDIAG` does this for the combat starting range; `FRUA_HALLDIAG`
for the Training Hall. Pixels then corroborate.

## Result: Mac 1.2 hunk 24, the first fix observed firing

HEIRS `GEO011` event 44 (`ev[12] = 0x20`, bit5 set; `ev[14]` range bits = 2),
reached by landing on cell(col=5, row=17) of area 11.

State, same seed:

| | `rec[56]` start range | `rec[55]` depth |
|---|---:|---:|
| hunk 24 ON | **0** | **0** |
| hunk 24 OFF | 2 | 2 |

Pixels: **AE 60696**, and the two frames tell the story:

- **ON** — BARBARUS is on the tactical map **adjacent to three Driders**, with
  the party command bar up (Aim / Use / Guard / Quick / Delay / View / Speed /
  End). The party acts, at melee range.
- **OFF** — the party is off-screen at range 2 and the Driders get the first
  round: "DRIDER … Composite Long Bow", "DRIDER is Unaffected",
  `Spell: Mirror Image`.

So 1.0 ignored the designer's "start adjacent" flag at this site, and an
encounter authored as a melee ambush played as a ranged shooting gallery with
the monsters getting a free bow-and-spell round. That is a substantial
gameplay fix, and it is now demonstrated rather than asserted.

## Result: Mac 1.2 hunk 35, and reaching the OVERLAND map

The second fix promoted with this harness, and the one that showed a state diff
beats a pixel diff. `-DFRUA_OVDIAG` logs the candidate cell `l3af2` computes and
whether the 1.2 bounds guard sees it as off-map.

**Overland is reachable headlessly** — this was not obvious and is worth
recording: `g_a5_18878 <= 4` routes play entry into `l0b88()` (play-state
`-27990 = 3`, the wilderness screen) instead of `l0ba2()`, and
`FRUA_ENTRY_LEVEL` sets that byte. So `-DFRUA_ENTRY_LEVEL=1` plus `beginplay`
lands in HEIRS' overland map, no dungeon involved. The facing deltas are
`drow(-27862) = {0,1,1,1,0,-1,-1,-1}` and `dcol(-27853) = {-1,-1,0,1,1,1,0,-1}`,
so facing 2 is a pure +1 row step: seat the party at row 37 (last row of the
38x15 map) facing 2 and the next step goes off the edge.

| | `cand-row` (-4904) | `ov_step_out_of_bounds()` |
|---|---:|---:|
| hunk 35 ON (1.2) | **38** — raw | **1** |
| hunk 35 OFF (Mac 1.0's clamp restored) | 37 — clamped back | 0 |

The step was refused and the party stayed at row 37; the two following records
(a turn, then an in-bounds step) are identical across runs, so the difference is
attributable to the single deleted clamp. And because that guard is what hunks
36-38 test, this one measurement promoted **four** hunks at once — with 1.0's
clamp in place the guard reads 0 even at a genuine map edge, so 36-38 had never
been able to fire.

Two things worth carrying forward:

- **The OFF side was a transient edit**, per the recipe — 1.0's clamp pasted
  back into `l3af2`, measured, reverted. Back up the file first
  (`cp src/engine/boot.c <scratch>`) and diff for the marker afterwards; a
  leftover A/B toggle in a lift is worse than no measurement.
- **A harness flag can be silently inert.** `FRUA_ENTRY_ROW`/`_COL` did nothing
  on the overland path: `p[37]`/`p[38]` are the party cell there and they are
  copied from `-12288`/`-12287` in the branch ABOVE the override, so the party
  landed on the design's start cell (row 13) while only the facing took. The
  first run looked like a successful measurement of the wrong cell — the same
  class of mistake as the `"A battle begins..."` positive control. Log the
  input you think you set (`cur-row`), not just the output.

## Promotion status of the ported 1.2 fixes

Six of the 33 are **observed firing** (ON vs OFF produce different measured
state, same seed). Four cannot fire at all and that is a finding, not a gap.
The rest each need a specific situation, listed so the next pass does not have
to re-derive it.

### Observed firing (9)

| hunks | situation | evidence |
|---|---|---|
| 24 | HEIRS GEO011 ev44, cell(5,17) area 11 | `rec[56]` 0 vs 2; AE 60696 |
| 35 | overland row 37 facing 2 | `cand-row` 38 raw vs 37 clamped, `oob` 1 vs 0 |
| 36, 37, 38 | same overland step | `has_str` 1, `msg[0]` 84 (`'T'`), `blocked t` 1 — vs all zero in bounds |
| 1 | authored NOPERMA.DSN, `-DFRUA_PARTYHP=1` | `over 10` -> status **5** (ON) vs **6** (OFF) |
| 15 | same module, 2 monsters so round 2 arrives | `BLEED SUPPRESSED, status stays 5` (ON) vs `bleed tick mc[16] 5` (OFF) |
| 27, 28 | authored TEMPLE.DSN, `-DFRUA_TMPDIAG` | at the live jt933 call: raw `1677721600` vs swapped `100` — BOTH from one run |

Hunk 1's run carries its own negative control: a second hit in the SAME run with
`over 6` gives status 5 either way, so the divergence is specific to the
`over > 9` overkill branch rather than a blanket change.

### Cannot fire — established, not outstanding (4)

| hunk | why |
|---|---|
| 8 | **No-op by construction.** The line above fills all 768 bytes of `clutbuf` with 1, so 1.2's `jt399(clutbuf+96, 432, 1)` writes the value already there. 1.0's `0` punched a hole; 1.2 stops. There is no state to diff. |
| 33 | **No observable effect.** It deletes a `-4943` clear made redundant by hunk 31's clear at the loop top. `l709e` is the only reader of `-4943` anywhere, so the sole difference is the value left after it returns, which nothing looks at. |
| 34 | **Nothing produces effect 73.** The whitelist entry is real, but no `jt876` call anywhere in the port applies kind 73 (the kinds used are 255/0/97/55/105/8/62/31/15/12/95/7...). Unreachable until an effect-73 producer is lifted or a design supplies one. |
| 23 | **No data in the wild.** Needs an option string carrying a digit; across every design on hand 406 strings have a `~`/`^` marker and not one contains a digit. Authorable, but nothing shipped exercises it. |

### Needs a situation (19)

| hunks | the situation still to construct |
|---|---|
| 17 | a spell naming status 6/7/8 in a no-permadeath fight — `jt612` ("is slain") or `jt615`. Needs a caster; the seeded party is a fighter. |
| 19–22 | **UI reachable since #84** — the Items button now activates (`P1CLICK cy 191 cx 31 -> hit 1`, `jt893 ENTRY`) and the browser renders in full: "Ready Item", the inventory list, and the `Rdy \| Use \| Drop \| Halve \| Join \| Exit` bar. What is still missing is the STATE: `jt893 ENTRY saved -22281 0`, so the hoisted save+clear has nothing to suppress. `-22281` is set to 1 at `boot.c` ~45204 (an event path that loads a bigpic) and ~90842/90853 (the PIC layer), but the tactical-combat setup CLEARS it (~46522, "the battle flags"). So the browser must be entered from a bigpic-prompt context, not from inside a fight. |
| 3, 5, 6 | a prompt containing a digit. Author a STRG string with a digit plus a `~` marker and watch `l2184`'s word extraction. |
| 31 | a chained event pair where the first sets `-4943` (`ev[12]&4`, passage `ev[10]&0x20`, combat `ev[7]&0x20`) and the second would inherit it. Authorable. |
| 29 | an animated passage followed by a chained event. Authorable. |
| 9 | the Monster Editor: edit an ability, save, re-read `rec[113+2i]`. Mouse-driven; clicks do inject. |
| 13 | the editor's test-play Hall (`-18485` = 5, set by `l30d4`). |
| 16 | the `l4faa` picker in mode 0/5/7 after a different menu has left the `-24126` table dirty. |
| 18 | a spell that routes through `jt822` (hook id 137, the explosion burst) with victims. |
| 19–22 | a prompt inside the Items browser (`jt893`). |
| 7 | the `L3f80` picker modal. |
| 10, 11 | delete a monster from a design folder. |
| 14 | a party wipe with a summoned creature still on the combatant list. |
| 30 | a type-11 transfer during a test-play session. |
| 2 | reading the clobbered FC object or the dangling `-22222` pointer — no clean observable; likely only ever visible as corruption. |

### The UI-navigation blocker — ROOT CAUSE FOUND (TaskList #84)

It is not a hit-test problem and not a mouse-injection problem. **Two competing
event readers race, and the pump wins.**

`l23b4` (the modal poll behind `jt160`) runs this every iteration:

    rc   = jt1085();      // -> l0088 -> jt441 -> jt1118 -> l731e(3)  = THE PUMP
    item = l2d3e();       // -> l3198 -> jt1125 -> WaitNextEvent      = THE READER

`l731e` pumps AND DISPATCHES (`l66e8` + `l725c`). The port's `jt1125` then calls
`WaitNextEvent` independently — but the event is already gone. Measured in the
temple verb bar, one Return plus one click:

| reader | events seen |
|---|---:|
| `l731e` (the pump, via jt1085) | **2** |
| `l2d3e` (the DLItem poll) over 401+ calls | **0** |

Two more measurements pin the shape of it:
- The combat command bar produces NO `l2d3e` activity at all, even for a click
  that visibly works — combat uses a different reader, which is why clicks
  "work in combat and not in modals".
- On the MAIN MENU `l2d3e` does receive events (`key 1`). So the starvation is
  specific to contexts where `l23b4`'s loop calls the pump first.

**Attempt 2 (landed): stash in `l725c`, drain in `jt1125`.** Same idea as attempt
1, but this time with counters at BOTH ends — which is how I learned attempt 1's
post-mortem was itself wrong. The stash works: 3 pushed, 3 popped in a temple-bar
run. It restores events the pop was destroying and the Mac never destroys.

**IT DOES make clicks land** — verified end to end on the temple verb bar. My
first reading of this run said otherwise, and was wrong for the third time in
the same investigation: the Phase-2 counter I was watching NEVER RUNS for a
click, because `l2d3e` handles clicks in a Phase-1 block that returns early.
Measuring the right place shows:

    P1CLICK cy 190  cx 28
       item iy 0     ix 0     has method 1  hit 0    <- unpositioned, skipped
       item iy 8094  ix 8003  has method 1  hit 1    <- HIT

and the screen confirms it: the verb bar rebuilt from
`Heal | Donate | View | Pool | Leave` to `Heal | View | Exit`. Before the stash
the click never arrived at all.

Two notes on the machinery that remain true:

- Two of the three pops are keyDowns, and `jt1125` correctly returns 0 for them.
  `l2d3e` passes `kind = 7`, `7 & (keyDownMask|autoKeyMask)` is 0, so keys are
  masked off BY DESIGN — on the Mac they flow through the `-818`/`-820` pending
  path, not as poll events. That is faithful, not a bug.
- The mouseDown reaches `l2d3e` itself. Resolving the caller return address
  confirmed it (`_l2d3e + 68`; `l3198` is inlined at -O2, so the frame belongs
  to `l2d3e`). The earlier "one of seven other callers took it" was an artefact
  of the same misplaced counter.

Regression-checked before landing: main menu, Training Hall and play entry all
BYTE-IDENTICAL to their pre-change captures, Return still advances the event
chain, 371 tests pass.

**A correction to attempt 1's post-mortem.** I recorded that the push "never
fired" because `l725c`'s masked `WaitNextEvent` never took the events. Wrong on
both counts: `l725c` demonstrably eats them (`l725c ATE ev.what 3`) and the push
did fire. What actually happened is that I never instrumented either end, so
"l2d3e still sees nothing" got attributed to the nearest plausible story. The
underlying lesson from that write-up survives and is still the right one: My `l731e PUMPED` log fires on the
`EventAvail(mask, &ev)` PEEK in the while condition — which is non-destructive.
The actual pop happens one level down in `l725c`, which runs its OWN
`WaitNextEvent(mask, ...)` with a MASK that `l731e` narrows
(`mask &= ~0x08` when `-820` is set, `mask &= ~0x07` when no mouseDown is
available). So the push never fired: `l731e` demonstrably SEES keyDown/mouseDown
in the peek, but that does not mean `l725c` took them. **A peek in the loop
condition is not the consumer** — instrument the destructive call, not the test
that guards it. Next step is the mask computation and `l66e8`, which together
decide whether `l725c` ever pops these events at all.

**A real bug fell out of it and IS fixed:** `g_event_was_click` was assigned only
after a successful fetch, so `jt1125`'s no-event early return — which runs on
every idle poll, hundreds of times a second — left the flag STALE at 1 from the
last real click, paired with the (0,0) coordinates it had just zeroed. That is
precisely the symptom `l2d3e`'s Phase 2 comment describes ("on an idle/hover pass
it tested the stale (0,0) event coords") and works around in Phase 4 instead of
clearing the flag. It also made my own first measurement unreadable: "clicks seen
403" was 403 idle polls, not 403 clicks. Now cleared on the no-event path;
verified the Hall renders byte-identically and play entry is unchanged.

**The fix is to finish the faithful architecture.** `jt1125`'s own doc records
it: *"The Mac body pulls from an internal event buffer (g_a5_904 / 912 / 910
cluster) that L731e fills from IRQ + Toolbox."* The Mac has ONE reader — the
pump fills a buffer and `jt1125` drains it. The port's `jt1125` shortcuts to
`WaitNextEvent`, so the two readers compete. Route `l725c`/`l731e`'s events into
that buffer and have `jt1125` read from it.

Hunks gated on this: **9** (Monster Editor), **13** (editor Hall), **16** (the
l4faa picker), **19-22** (Items browser). 6 of the remaining 19.

A note on how 27/28 got measured anyway: with no input delivered, `l23b4` exits
by its own TIMEOUT (`-24138` / `-13006`) and `l25b6` returns a cached result,
which reached the `jt933` call. So the bug is what let that measurement happen
without fixing it first — and the diagnostic logs the 1.0 reading and the 1.2
reading side by side from the same live event record, which is stronger than an
ON/OFF pair.

### Machinery added for this

- **`tools/mk_noperma_design.py`** — authors `NOPERMA.DSN`: one room whose entry
  cell fires a combat with `ev[12]` bit 6 (no-permadeath) and bit 5 (start
  adjacent) set, six groups of 31. `--noflag` builds the bit-6-clear control, so
  two runs differ in one bit of one event byte. No shipped design sets bit 6 on
  the HEIRS path, so the family was unreachable without this.
- **`-DFRUA_PARTYHP=<n>`** — clamps every combatant's current HP at combat
  entry, so a fight reaches the dying/destroyed branches deterministically
  instead of hoping the dice cooperate. Note it walks `-27928`, which is the
  whole combatant list, not just the party (the same fact that makes hunk 14
  matter). Release-guarded.
- **`-DFRUA_NPDIAG`** — logs the flag seed at combat entry and the decision at
  all three no-permadeath sites. **`-DFRUA_OVDIAG`** now also logs the overland
  refusal string and blocked flag, which is what promoted 36–38.

**Tuning the threshold is legitimate and worth recording:** `FRUA_PARTYHP=2`
gave `over 9` — one short of the `over > 9` branch, so ON and OFF agreed and the
run proved nothing. `=1` gave `over 10` and the divergence appeared. When a fix
guards a threshold, aim the harness at the threshold.

## Cell → event: the off-by-one

`docs/geo-format.md` states it (`special = event index + 1`) and it is easy to
lose. A cell's `special` byte *N* fires ENCR record *N−1*; the port does the
subtraction in `l709e` (`ev = base + ((idx & 0xff) - 1) * 20`). Scanning for
"which cell fires event *i*" therefore means `cell_special(c, r) == i + 1`.

Comparing the special byte to the index directly is what sent the first hunk-24
A/B to the wrong cell — event 12's flags were read from disk but the event that
actually fired was record 13, whose bit5 is clear, so the guard never ran and
AE=0 was a true negative about the wrong scenario. The event-byte counts (11 of
HEIRS' 175 combat events affected) were unaffected by the slip; only the cell
attributions were wrong.

## Never ship

`FRUA_RNGSEED`, `FRUA_HALLFREE`, `FRUA_PARTYHP` and the `FRUA_ENTRY_*` family are
behaviour-altering build flags and `make release` rejects them
(`src/engine/release_guard.h`).

`FRUA_CBTDIAG`, `FRUA_HALLDIAG` and `FRUA_OVDIAG` are **pure diagnostics** —
they only add `dbg_file_num` output — so by `release_guard.h`'s own rule they
are deliberately NOT guarded: noisy in a release, not wrong. Add a flag to that
header when it changes behaviour, not when it changes volume.
