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

Twenty-five of the 33 are **observed firing** (ON vs OFF produce different
measured state, same seed). Two cannot fire at all and that is a finding, not a
gap. The rest each need a specific situation, listed so the next pass does not
have to re-derive it.

### Observed firing (25)

| hunks | situation | evidence |
|---|---|---|
| 24 | HEIRS GEO011 ev44, cell(5,17) area 11 | `rec[56]` 0 vs 2; AE 60696 |
| 35 | overland row 37 facing 2 | `cand-row` 38 raw vs 37 clamped, `oob` 1 vs 0 |
| 36, 37, 38 | same overland step | `has_str` 1, `msg[0]` 84 (`'T'`), `blocked t` 1 — vs all zero in bounds |
| 1 | authored NOPERMA.DSN, `-DFRUA_PARTYHP=1` | `over 10` -> status **5** (ON) vs **6** (OFF) |
| 15 | same module, 2 monsters so round 2 arrives | `BLEED SUPPRESSED, status stays 5` (ON) vs `bleed tick mc[16] 5` (OFF) |
| 27, 28 | authored TEMPLE.DSN, `-DFRUA_TMPDIAG` | at the live jt933 call: raw `1677721600` vs swapped `100` — BOTH from one run |
| 9 | Monster Editor, BASILISK (id 42): Strength 10 -> 18 and % 0 -> 50, then Ok | the SAVED `MONST042.dat` differs in exactly **two** bytes across all 450: `[113]` 18 (ON) vs 10 (OFF), `[125]` 50 (ON) vs 0 (OFF) |
| 3, 5, 6, 23 | authored DIGTEST.DSN — a type-10 encounter prompt whose STRG option string is `^TAKE 3 GEMS ^LEAVE IT`, `-DFRUA_DIGDIAG` | three separate one-hunk-at-a-time A/Bs, each with its own visible failure: the bar reads `Take 3 gems \| Leave it` (ON) vs `Take \| S gems` (23 off), `Take \| 3 gems` (5/6 off), `Take \| 3 gems \| Leave it` (3 off) |
| 34 | the same exploding-item trick with `[15] = 73`, camp -> Magic -> Display | same effect on the record, same name pointer: the screen reads **`Immune to Dragon Breath`** (ON) vs **`<No Spell Effects>`** (OFF); `in whitelist` **1** vs **0** |
| 18 | authored FXTEST.DSN + an exploding item patched into BARBARUS's `.CHR`, `-DFRUA_FXDIAG` | same source, same victim: `jt822 VICTIM BASILISK`, `fx148 node value` **1** (ON) vs **0** (OFF) |
| 19-22 | authored SHOPPIC.DSN (`tools/mk_bigpic_design.py --shop`), Items -> Sell, `-DFRUA_ITMDIAG` | `jt893 ENTRY saved -22281` is **1** in both, then `in-browser` / loop-top / `jt182 confirm sees -22281` are **0 0 0** (ON) vs **1 1 1** (OFF) |
| 17 | authored NOPERMA.DSN with **monster 42 (BASILISK)**, `-DFRUA_CBTPLAY -DFRUA_NPDIAG` | same gaze, same seed: `final-status` **5** (ON) vs **7** (OFF) on `subject BARBARUS side 0`; ON the party walks on at 1 HP, OFF the screen reads *"The monsters rejoice, for the party has been destroyed!"* |
| 16 | authored caster (`tools/mk_caster_chr.py`), Hall -> View -> Spells | the picker's command bar reads **`Exit`** (ON) vs **blank** (OFF); `-24126` `0 FF ..` vs the stale `1 'S' 7 'E'`; `l2184("Exit") -> "Exit"` vs `""` |
| 30 | authored TPTEST.DSN (`tools/mk_testplay_design.py`), map editor -> Utilities -> **Test module**, step onto the transfer | same burst position in both runs: the bottom row reads **`Transfer module ends testing!`** (ON) vs the ordinary `Area \| Cast \| View \| ...` command bar (OFF) — 7848 changed pixels vs 224 (the clock) |
| 31 | the SAME run, second half: Escape -> Done, then step onto a question cell | at the question's `l709e` iteration both builds inherit `-4943 = 4` from the transfer; at the tail it is **0** (ON) vs **4** (OFF), and `-> RE-SCAN landed cell` appears in OFF only |
| 29 | authored MOVETEST.DSN / MOVERESC.DSN (`tools/mk_movetest_design.py`), `-DFRUA_MOVDIAG` | **two** A/Bs off one line, diverging in OPPOSITE directions: the auto-chain variant prints *"THE CHAIN FIRED"* (OFF) vs nothing (ON); the `--rescan` variant prints *"THE DESTINATION EVENT FIRED"* (ON) vs nothing (OFF) |

Hunk 9 is the strongest evidence of the set, because the observable is a FILE
rather than a log line: two full runs of the same click script, byte-diffed,
differ only at the two offsets the fix writes. It also exercises BOTH statements
of the hunk — the `i = 0..5` loop (offset 113 = current Strength) and the
separate trailing percentile pair (offset 125) — which needed two edited fields,
not one. The live `l611c` diagnostic shows the divergence the fix repairs:
`perm 18 / cur 10`, `pct perm 50 / pct cur 0`. That is precisely 1.0's bug: the
editor writes the PERMANENT byte and 1.0 saved the record with the CURRENT byte
still holding the loaded value.

The recipe is in "Reaching the Monster Editor headless" below.

Hunk 16 is the only one so far whose divergence is visible on SCREEN rather
than in state: a whole-frame pixel diff of the two runs differs in exactly one
region, x 24..103 / y 428..449 — the command-bar button — and nowhere else.
1.0's spell picker simply has no `Exit` button. The recipe is in "Reaching the
spell picker headless" below.

Hunk 17 is the most CONSEQUENTIAL of the set — the only one where the two
builds end the session differently. Full recipe below; the short version is
that the whole no-permadeath family (1, 15, 17) now fires in a single run.

### Reaching TEST-PLAY headless (hunks 30 and 31)

These two are the same code path — `l5676`'s early return at CODE 20 `0x57ac`,
taken when a type-11 transfer fires while a design is being test-played. Hunk
30 is the message it adds; hunk 31 is the `-4943` leak that the same return
creates. One scripted run measures both.

**Test-play is reachable**, and the route is not obvious: the map editor's
**Utilities -> Test module** is the ONLY writer that leaves `-18485` non-zero
(`l3236` case 7, CODE 11 `0x3348`). Everything else that sets it — `l30d4`'s
spell sub-editor — restores 0 on the way out.

```sh
python3 tools/mk_testplay_design.py data/work/gamedata --current
```

Then, **in a normal play session first**, make a save — `-18485 != 0` sends
`l07dc` down `jt582()` instead of the Training Hall and bails with
"No saved games!" without one:

```sh
make EXTRA_CFLAGS='-DFRUA_TPDIAG -DFRUA_RNGSEED=12345'
D=.claude/skills/run-falcon-port/driver.sh
env -u DISPLAY FALCON_TOS=/usr/share/hatari/tos404.img $D start
for k in p a Return Escape b; do env -u DISPLAY $D key $k; sleep 3; done
env -u DISPLAY $D key e     # Encamp
env -u DISPLAY $D key s     # Save
env -u DISPLAY $D key a     # -> TPTEST.DSN/SavGamA.csv
```

Restart (the save is on disk) and run the measurement:

```sh
env -u DISPLAY $D key e                       # main menu -> Edit Modules
env -u DISPLAY $D click 58 439                # Open (Dungeon 01 = the design's area 5)
env -u DISPLAY $D drag 555 67 555 229         # Utilities -> Test module
env -u DISPLAY $D key p                       # -> the load picker (NOT the Hall)
env -u DISPLAY $D key a                       # load slot A
env -u DISPLAY $D key Up                      # step onto the transfer  <- hunk 30
env -u DISPLAY $D key Escape                  # -> camp
env -u DISPLAY $D key d                       # Done -> the outer level reload
env -u DISPLAY $D key Left; env -u DISPLAY $D key Up   # onto a question cell <- hunk 31
```

Measured, same script both builds:

```
                                   1.2 (ON)      1.0 (OFF)
  bottom row after the transfer    "Transfer     the ordinary
                                    module ends   Area|Cast|View|...
                                    testing!"     command bar
  AE vs the landing frame          7848          224   (just the clock)

  at the question's l709e iteration
    inherited -4943                4             4     (both leak)
    -4943 at the tail              0             4
    "-> RE-SCAN landed cell"       absent        PRESENT
```

Five things that each cost a run:

- **`jt101` is a DWELL, not a modal.** It draws on row 24 via `jt94` and then
  `l4bac` just waits `jt476(-17518[hdr[18]*2])` — the design's message-speed
  setting. `driver.sh shots` waits for a STABLE frame and always misses it.
  Burst-grab with plain `shot` in a loop starting ~0.5 s after the key; the
  banner held for the first five frames here.
- **The editor pulldowns need `drag`, and the menu stays painted afterwards** —
  so you CAN screenshot it to read the row geometry (the "never screenshot
  during a drag" rule is about a HELD button). Utilities is at x 555, y 67; its
  rows are Display access 90, Replace globally 110, Clear module 130, Entry
  points 170, Place entry 190, **Test module 229**. Log `jt341`'s
  `(group, item)` under `FRUA_TPDIAG` and one drag tells you exactly which row
  you hit — much cheaper than guessing pixels, and it keeps you off **Clear
  module**, which is three rows up and would wipe the design.
- **Test module does not enter play.** It sets `-18485`, exits the editor to the
  MAIN MENU, and leaves the flag set; "Play the Game" then takes the test-play
  branch. That looks like the command failed.
- **`-27982` gates the whole measurement, and only the camp round-trip clears
  it.** The transfer raises it, which is the point of hunk 31 — but it also
  keeps `l709e`'s convergence block switched off for the rest of the session,
  so the `-4942 && -4943` test can never run. There is no route from the camp
  menu back to the main menu (Done and Exit both return to the walk view), but
  `jt948`'s `res == 4` arm breaks to the OUTER level reload when `-27982` is
  set, and that reload clears it. Escape then Done is the whole trick.
- **Nothing else may fire in between.** Any other event's `l709e` iteration runs
  a full tail, and 1.0's tail clear takes the leak with it. A step onto an
  event-free cell is safe (`l709e(0)` breaks before its body) — which is why the
  design puts the question on every cell except the entry and the transfer,
  rather than on one square: after the reload the party's facing is not
  predictable headlessly, and two runs disagreed about it.

**The OFF build for 31 must restore hunk 33 as well.** 31 and 33 are a pair —
1.2 moved the clear from the tail to the top of the loop body — so an honest 1.0
build has exactly one of the two clears, not zero. Dropping both would have
made the flag leak forever and overstated the difference.

Honest limit on hunk 31: the re-scan `jt201` returned **0** here, because
`l4144` (which runs in both builds, just above the test) restores the saved
position first and that cell has no special. So 1.0 performs a spurious cell
re-scan that happens to find nothing. The difference is real and attributable —
the fix's whole content is whether that scan runs — but it is state-level, not
something a player would see in this particular module.

### An animated passage that ends the chain (hunk 29)

Hunk 29 appends one store to `l2e42`, the type-12 "scripted movement" handler
that walks the party across the map over `ev[6]` animated frames:

```c
g_a5_byte(-4942) = 1;        /* transition done */
```

`-4942` is `l709e`'s chain-control flag, and the event loop's tail reads it
**twice, in opposite senses** — which is why one line gives two A/Bs that
diverge in opposite directions. Both are authorable, and
`tools/mk_movetest_design.py` builds one module for each:

| | tail test | 1.2 (fix ON) | 1.0 (fix OFF) |
|---|---|---|---|
| default | `if (-18484 && -4942 == 0) idx = ev[3]` | chain SUPPRESSED | `ev[3]` fires |
| `--rescan` | `if (-4942 && -4943) idx = jt201(row, col)` | landed cell RE-SCANNED | nothing |

```sh
python3 tools/mk_movetest_design.py data/work/gamedata --current            # chain
python3 tools/mk_movetest_design.py data/work/gamedata --current --rescan   # re-scan
make EXTRA_CFLAGS='-DFRUA_MOVDIAG -DFRUA_RNGSEED=12345'
```

then `driver.sh start` and the five `beginplay` keys (`p a Return Escape b`).
**Do not use `driver.sh beginplay` itself here** — it ends with a `Right`/`Left`
view nudge, and either key dismisses the message box that IS the observable.

Measured, from `DBG.LOG` (the `-4942` column is the whole diff):

```
              ON                                OFF
  -4942 on exit          1                        0
  -4943 (ev[7] & 0x20)  32                       32     (--rescan)
  ev[3] (chain link)     2                        2     (default)
  -> RE-SCAN landed cell, event 3      -> AUTO-CHAIN to event 2
```

and on screen: OFF shows `THE CHAIN FIRED - EVENT 2 RAN AFTER THE MOVE.` over
`Press Return to continue.`; ON shows the bare walk view. The `--rescan` pair is
the same two frames with the roles swapped.

Two traps, both measured rather than assumed:

- **Facing 0 decrements the COLUMN, not the row.** `jt201` indexes
  `height*col + row` and takes its arguments as `jt201(-12288, -12287)` =
  `(row, col)`. The party started at `(col 3, row 3)` and two frames put it at
  `(col 1, row 3)`. `_walled_room` names edge 0 "N" and hangs it off `row == 0`,
  so the perimeter wall is not even on the axis this walk travels. Hook the
  destination event to the wrong cell and `--rescan` measures nothing at all —
  `jt201` just returns 0 and the run looks like the fix is dead.
- The re-scan needs BOTH flags, and `-4943` comes from the event's own
  `ev[7] & 0x20`. Leave that bit clear (the default variant does) and only the
  auto-chain half is in play.

**Hunks 30 and 31 share one situation, and it is not this one.** `-4943` is set
by exactly three handlers (`l5676`'s `ev[12] & 4`, `l3cd6`'s `ev[10] & 0x20`,
`l2e42`'s `ev[7] & 0x20`), and 1.0 clears it in `l709e`'s tail — which is inside
`if (-27982 == 0)`. So the flag can only leak out of a call when the SAME event
both sets `-4943` and leaves `-27982` raised, and the one handler that does that
is `l5676`'s early return at `0x57ac`: a **type-11 transfer while test-playing a
design** (`-18485 != 0`), which is hunk 30's site. Chaining a later
`-27982`-raising event (e.g. a type-16 variable event with `ev[4] & 16`) does
NOT work — the earlier iteration's tail has already cleared the flag. Reaching
test-play once therefore gets hunk 30 (its message is on screen) and arms
hunk 31 for the next `l709e` call.

### A prompt with a digit (hunks 3, 5, 6, 23)

Four hunks, one string. They are one coherent "digits are not letters" fix
split across two CODE segments: 23 is `l0098`'s tolower, 5 and 6 are `l2184`'s
two word-boundary tests, 3 is `l1a0c`'s scan. Feed a digit through the option
prompt and all four have somewhere to go wrong.

Two things had to be got right to author the string:

- **`~` is NOT in the STRG 6-bit alphabet** — it folds to a space. Design
  strings mark options with **`^`** (code 30), which is exactly why `l0098`
  accepts both and converts `^` to `~` on the way through.
- **The alphabet folds everything to uppercase**, and `l0098` lowercases A-Z on
  the way out. The letter immediately after a marker escapes that, because
  deleting the marker shifts it into an already-visited index — that is the
  mechanism that capitalises each option's first letter. Digits sit in the
  middle of an option, so they take the full treatment.

`l2184` runs BEFORE `l1a0c` in `l206e`, so the `l2184 -> ->` probe line
attributes cleanly to 5/6 and the `l1a0c boundary char` line to 3.

```python
a5.strg_write(["", "^TAKE 3 GEMS ^LEAVE IT"])
ev = bytearray(EVENT_SIZE)
ev[0] = 10                 # encounter prompt -> l3b0e -> l0098 + jt182/l2184
ev[8] = 2; ev[9] = 0       # option-string id, little-endian; l3b0e does id-1
a5.set_event(0, bytes(ev))
_hook(a5, 3, 3, special=1) # entry cell, so beginplay alone raises it
```

```sh
make EXTRA_CFLAGS='-DFRUA_DIGDIAG -DFRUA_RNGSEED=12345'
env -u DISPLAY PLAY_STEP_DELAY=5 .claude/skills/run-falcon-port/driver.sh beginplay
```

One hunk off at a time, each with its own visible failure:

| build | `l0098 OUT` | `l2184 ->` | `l1a0c` boundary | the bar |
|---|---|---|---|---|
| 1.2 (all on) | ` Take 3 gems  Leave it` | `Take 3 gems  Leave it` | 76 `L` | `Take 3 gems \| Leave it` |
| **23** off | ` Take **S** gems  Leave it` | `Take S gems  ` | — | `Take \| S gems` |
| **5, 6** off | ` Take 3 gems  Leave it` | `Take 3 gems  ` | — | `Take \| 3 gems` |
| **3** off | ` Take 3 gems  Leave it` | `Take 3 gems  Leave it` | **51 `3`**, 76 | `Take \| 3 gems \| Leave it` |

Read the three failures:

- **23** corrupts the text AND loses an option. `'3'` (51) + 32 = `'S'` (83), so
  the digit becomes a letter — and because it is now UPPERCASE, `l2184` counts
  it as a word start, the two option slots are spent on `Take ` and `S gems `,
  and **`Leave it` never reaches the bar**. The player cannot choose it.
- **5, 6** lose the option without corrupting the text: the digit is intact but
  starts a spurious word, so the same slot exhaustion drops `Leave it`.
- **3** goes the other way and INVENTS a button: `l1a0c` splits at the digit, so
  the bar grows a third option that the design never wrote.

Note the OFF side for 23 is a reconstruction — the port is already at 1.2 here
(A-Z only) and the hunk log records that this hunk "needed no further code", so
1.0's digit clause has to be pasted back in to measure it.

### The camp effects screen (hunk 34) — and why "cannot fire" was wrong

Hunk 34 adds effect id **73** to `l1374`'s display whitelist, so an active
effect 73 shows on the camp spell-effects screen instead of being silently
omitted. It was filed as "cannot fire — nothing produces effect 73". The
producer census was accurate and the conclusion was not: the hunk-18 route
produces ANY effect kind from item data, 73 included.

It also had a second way to be inert, which had to be measured rather than
assumed: `l1374` skips any whitelisted effect whose `-20096` name is empty
(`namebuf[0] == 0`), so adding 73 to the list would change nothing if 73 were
nameless — the shape of hunk 8. Measured: the name pointer is non-null and
reads **"Immune to Dragon Breath"**. The fix is live.

No combat is needed — `l1374` just walks each member's `rec+4` list. Use a bare
room with no events (HEIRS opens on its caravan intro, which eats the keys):

```python
# a walled room, no events at all
a5 = _walled_room(entry=(3, 3), facing=0)
d = Design("CAMPTEST"); d.start_area = 5; d.start_entry = 1; d.add_area(5, a5)
d.write("data/work/gamedata", make_current=True)
```

```python
# arm BARBARUS's helm with effect 73 (see the hunk-18 recipe for the offsets)
b = bytearray(open('data/work/gamedata/CHAR0000.CHR','rb').read())
b[398+15] = 73; b[398+16] = 0x80
open('data/work/gamedata/CHAR0000.CHR','wb').write(bytes(b))
```

Then `beginplay` -> `v` -> `i` -> click the item row -> `Rdy` x3 -> `Exit` ->
`Exit` -> `e` (Encamp) -> click `Magic` (152,439) -> click `Display` (424,439).

| | the effects screen |
|---|---|
| 1.2 (ON) | `BARBARUS` / **`Immune to Dragon Breath`** |
| 1.0 (OFF) | `BARBARUS` / **`<No Spell Effects>`** |

The log confirms the divergence is the whitelist and nothing else — both runs
report `l1374 effect on member: id 73` and the same name pointer, and differ
only at `in whitelist` **1** vs **0**.

### Building an exploding item (hunk 18)

Hunk 18 lives in `jt822`, the explosion burst, which the `-25242` hook table
holds at slot **137**. `jt868(15)` sweeps that slot at the top of EVERY
combatant turn, so the hook is polled constantly — but `l026e` only fires it
when the actor CARRIES effect 137 on its `rec+4` list, and a census says
nothing on hand produces it:

| producer | measured |
|---|---|
| spell table `def[10]` (the kind `l6114` hands `jt871`) | 137 spells dumped; distinct kinds top out at **123**, no 137 |
| the id-117 random-effect roller (`a5 -15024`) | **106** |
| every literal `jt876` / `jt871` / `l3dfe` call site | none uses 137 |
| item templates (byte 15 -> node `[55]`) | 12 tables, 1,600+ records; max **126**, no 137 |

So it looks unreachable — and stopping there would have been wrong. The live
probe says the hook IS installed (`hook installed 1`), and one more measurement
opens the door: `l77a0`'s override slot `-24734` is **non-zero and equals
`jt820`**. That matters because `jt820` is the one function that turns item data
into an effect — it mirrors an item's byte-15 effect id onto its bearer — and it
is reachable ONLY through that override, never through the type table. The full
chain, every link measured:

    ready an item whose [15] = 137 and [16] bit7 = 1
      -> jt882 -> l2d78 case 0 (kind = [16] & 0x7f = 0), sets -23187
      -> l77a0(137, ...) takes the OVERRIDE -> jt820
      -> jt876(bearer, 137, ...) puts effect 137 on rec+4
      -> next combat turn: jt868(15) -> l026e(137) -> jt41 finds it
      -> l77a0(137) with -23187 clear -> the type table -> jt822
      -> per victim: jt876(source, 148, 1)          <- the hunk

Nothing ships such an item, so patch one into a test character. A saved `.CHR`
carries its own 18-byte template copy per item, so this needs no `ITEM.DAT`
edit — item *k* starts at `398 + k*18`:

```python
b = bytearray(open('data/work/gamedata/CHAR0000.CHR','rb').read())
o = 398 + 0*18                 # item 0 = BARBARUS's Helm, already readied
b[o+15] = 137                  # node[55] — the l77a0 effect id
b[o+16] = 0x80                 # node[56] — bit7 = run the l2d78 pass, kind 0
open('data/work/gamedata/CHAR0000.CHR','wb').write(bytes(b))
```

Build the module with combat one step AWAY from the entry cell, so the item can
be readied first (`mk_noperma_design.py` puts it ON the entry cell; move the
hook to the four neighbours). Then: `beginplay` -> `v` -> `i` -> click the item
row -> click `Rdy` x3 -> `Exit` -> `Exit` -> `Up` into the fight.

    ON   jt822 ENTRY source BARBARUS / VICTIM BASILISK / fx148 node value 1 / count 1
    OFF  jt822 ENTRY source BARBARUS / VICTIM BASILISK / fx148 node value 0 / count 1

Traps:

- **`node[2]` is a WORD**, not a byte — `jt876` does `*(short *)(node+2) = c`.
  A byte read returns the HIGH half and reports 0 for BOTH builds, which looks
  exactly like a hunk that does not fire. This cost a run.
- **Pick an item that is already readied.** The first attempt armed the
  Composite Long Bow, which BARBARUS cannot ready at all (sword + shield fill
  both hands) — `Rdy` just silently does nothing. An already-worn item toggles
  off and back on, and the ON transition is what applies the effect.
- **`Rdy` needs three clicks to end READIED**, because `l2d78` runs on both
  transitions and the click/toggle bookkeeping is off by one. Screenshot and
  confirm the row reads `Yes.` before leaving the browser — a readied item is
  what puts the effect on the record.
- **jt822 fires happily OUTSIDE combat** and reports zero victims (the field
  staging table is empty). Seeing `jt822 ENTRY` is not evidence; seeing
  `jt822 VICTIM` is.

### Reaching an Items-browser prompt with the flag live (hunks 19-22)

Three conditions, and the third is the one that ate two runs:

1. **`-22281` must be 1 on `jt893` entry.** `l442e` sets it for any event whose
   picture id is >= 240 — a bigpic backdrop (`boot.c` ~45211).
   `tools/mk_bigpic_design.py` authors that.
2. **Do not step.** `l085e` clears the flag on every move (~45676), and the
   tactical-combat setup clears it too (~46687, ~49124) — which is why it can
   never come from a fight. Open the browser standing on the entry cell.
3. **The arm matters as much as the flag.** Outside a vault (`-27990 != 10`)
   `l11a8` offers arm **4**, not arm 3 — so the visible `Drop` button IS the
   trade/give arm, the one arm 1.0 ALREADY suppressed. A run through it gives
   `confirm sees 0` on both builds. That is a correct negative control, not a
   dead hunk, and it is easy to misread as one.

Use the `--shop` variant: `jt183` puts the play mode at 1, and `l11a8` then also
offers arms 7 (`Sell` -> `jt189`) and 8 (`Id` -> `jt190`). Both raise `jt159`
confirms 1.0 never suppressed.

```sh
python3 tools/mk_bigpic_design.py data/work/gamedata --current --shop
rm -f src/engine/boot.o
make EXTRA_CFLAGS='-DFRUA_ITMDIAG -DFRUA_RNGSEED=12345'
D=.claude/skills/run-falcon-port/driver.sh
env -u DISPLAY FALCON_TOS=/usr/share/hatari/tos404.img $D start
env -u DISPLAY PLAY_STEP_DELAY=5 $D beginplay      # -> the shop, bigpic up
env -u DISPLAY $D key v                            # View Character
env -u DISPLAY $D key i                            # Items -> jt893
env -u DISPLAY $D click 150 141                    # an item row  (REQUIRED first)
env -u DISPLAY $D click 480 439                    # Sell
```

    ON   jt893 ENTRY saved -22281 1 / in-browser 0 / loop top 0, 0 / confirm sees 0
    OFF  jt893 ENTRY saved -22281 1 / in-browser 1 / loop top 1, 1 / confirm sees 1

The OFF build is the real 1.0 SHAPE, not just the fix deleted: entry save+clear
and exit restore removed (19, 22) AND the per-case pair put back inside case 4
(20, 21). That is what makes the case-4 negative control meaningful.

**What the flag actually does, and the honest limit of this measurement.**
`jt182` passes it to `l23b4`, where it gates a per-iteration animation block
(`jt46(3, 3, arg_lo, -24205)` + `jt80`) — so 1.0 left combat sprite animation
running behind Items-browser prompts. That block is itself gated on
`g_a5_-24321 > 0 && g_a5_-24206 >= 1`, i.e. an animation being staged, and the
shop stages none. So the two frames here are **pixel-identical** (compared: 0
differing pixels) and the divergence is purely in state. A VISIBLE
demonstration still needs a bigpic-prompt context with an animation loaded.

Two more traps:

- **Click an item row before the verb.** The verb click alone does nothing —
  no log line, no reaction — which reads exactly like a dead button.
- **Keyboard does not drive the browser bar at all.** `key d` for Drop is
  inert; these are DLItem buttons and want the injected click #84 restored.

### The no-permadeath family, and the monster that completes it (hunk 17)

The blocker recorded here for months was "needs a caster" — hunk 17 fires from
`jt860` with status 6/7/8, and the routes named in the hunk log are spells
(`jt612` slays with 6, `jt615` with 8). That premise was WRONG, and chasing it
cost most of a session:

- The lethal spells do not fit the auto-turn's spell filter. Measured off the
  stock table with the `FRUA_SPLDIAG` dump: 111 Disintegrate and 114 Flesh to
  Stone are single-target but carry a cast time; 110 Death Spell and 125 Power
  Word Kill are instant but area-mode. `cbtplay_pick_spell` wants instant AND
  single-target (the Magic-Missile shape), so it picks none of them. Forcing one
  needs a new harness flag.
- **A MONSTER gets there with no caster at all.** Monster **42, the BASILISK**,
  has the petrifying gaze — `jt842` (hook 203) calls `jt860(target, 7, "is
  turned to stone")` on a party member. Build the module with `--monster 42` and
  the situation constructs itself.

```sh
python3 tools/mk_noperma_design.py data/work/gamedata --current --monster 42
rm -f src/engine/boot.o
make EXTRA_CFLAGS='-DFRUA_CBTPLAY -DFRUA_RNGSEED=12345 -DFRUA_NPDIAG'
D=.claude/skills/run-falcon-port/driver.sh
env -u DISPLAY FALCON_TOS=/usr/share/hatari/tos404.img $D start
env -u DISPLAY PLAY_STEP_DELAY=5 $D beginplay
```

One run, three hunks, everything upstream byte-identical across ON and OFF
(`dealt 23`, `target hp 28`, `attack dir 0`):

    ON   jt39 no_perma 1 ...                      <- hunk 1  (damage route)
         jt860 req-status 7
            subject BARBARUS   side 0
            hdr29 1
            final-status 5                        <- hunk 17 (status route)
         l102a dying member, hdr29 1
            BLEED SUPPRESSED, status stays 5      <- hunk 15 (the clock)

    OFF  jt860 req-status 7
            subject BARBARUS   side 0
            hdr29 1
            final-status 7
         (no l102a line at all -- nobody is dying, he is stone)

And the two screens are not comparable, they are opposite outcomes:

| | after the fight |
|---|---|
| 1.2 (ON) | BARBARUS is back in the roster at **1 HP** — `l33d8` revived the status-5 character — and play continues |
| 1.0 (OFF) | **"The monsters rejoice, for the party has been destroyed!"** — game over |

In a module that explicitly declared characters never die permanently. That is
the hole 1.2 plugs, demonstrated end to end rather than argued from the asm.

Two traps, one of which ate a run:

- **An authored `.CHR` caster is not combat-ready.** With `mk_caster_chr.py`'s
  MERLIN as the ONLY party member, combat entered (`COMBAT ENTRY ev[12] 96 /
  hdr[29] seeded 1`) and then fell straight back to the walk view with no party
  turn and no `cbtplay:` line at all. The record is synthetic and zero-filled,
  so its combat-side fields are not what the fight loop expects. Isolated by
  control: the SAME module with the stock party fights normally, with monster
  id 1 as well as 42 — so it is the character record, not the monster and not
  the design. Use the stock party for combat work; the authored caster is for
  the out-of-combat spell screens (the hunk-16 recipe).
- **Do not read the monster id as the cause of an empty fight.** That was the
  first hypothesis here and it was wrong; the one-run control above is what
  settled it. The monster id chooses which hunks are REACHABLE (only 42 reaches
  17), not whether the fight happens.

### Reaching the spell picker headless (the hunk-16 recipe)

Two things have to be true at once, and the second is the one that is easy to
miss:

1. **A caster with memorized spells.** The synthetic boot roster is fighters
   with an empty `rec[198..338]`, so `jt904`'s `cond1` is false, the sheet
   offers no `Spells` verb, and `jt595` is unreachable. `tools/mk_caster_chr.py`
   writes a Magic-User into the pool — a 398-byte `.CHR`, which is exactly the
   record with empty inventory and spell-book chains.
2. **No inventory on that caster.** This is what makes the bug observable.
   `jt904` builds its command bar with `jt155`, which writes each verb's index
   into `-24126[i*2]`. With items the first call is `jt155(0)` and the stale
   `[0]` is 0 — the same value `jt179(0)` would write, so the fix changes
   nothing. Without items the first call is `jt155(1)`, `[0]` is 1, and 1.0
   carries that into the picker.

```sh
python3 tools/mk_caster_chr.py data/work/gamedata     # -> CHAR0004.CHR, MERLIN
make EXTRA_CFLAGS='-DFRUA_SPLDIAG'
D=.claude/skills/run-falcon-port/driver.sh
env -u DISPLAY FALCON_TOS=/usr/share/hatari/tos404.img $D start
env -u DISPLAY $D key p                       # Play the Game -> Training Hall
env -u DISPLAY $D key a                       # Add Character -> the pool list
env -u DISPLAY $D key Down Down Down Down     # -> MERLIN (row 4)
env -u DISPLAY $D shots /tmp/sel.png          # CONFIRM the highlight (see below)
env -u DISPLAY $D key Return                  # add
env -u DISPLAY $D key Escape                  # back to the Hall
env -u DISPLAY $D key v                       # View Character -> jt904
env -u DISPLAY $D key s                       # Spells -> jt595(0,0) -> l4faa
```

Then read `l4faa DEFAULT arm` in `DBG.LOG` for the before/after table and
`l2184 -> ->` for the extracted verb word.

Traps:

- **Verify the add-list highlight before pressing Return.** One run lost all
  four `Down` keys — the list is still building and the engine drains keys
  typed during the build — so `Return` added row 0 (BARBARUS, a fighter WITH
  items) and the whole A/B silently measured the wrong character. The symptom
  is `jt904 items? 1 / cond1 0` in the log. A screenshot between the arrows and
  the Return costs one second and catches it.
- **The list order is the pool order**, and the pool is whatever `CHAR*.CHR`
  the gamedata dir holds — not a fixed set. Check it in the screenshot rather
  than assuming row 4.
- **`save_roster` rewrites every `.CHR` during the run** (and deletes files for
  slots past the pool count). Restore the roster between A/B runs or the second
  run starts from a different party.
- **Spell ids are game-data indices, not ours.** Against the stock tables
  1..8 are cleric level 1, **9..21 are mage level 1**, 22.. cleric level 2.
  `l4e2c` drops anything the character's class cannot cast, and if that leaves
  the list empty `jt595` returns BEFORE `l4faa` — no arm runs, nothing to
  measure. The `FRUA_SPLDIAG` probe in `jt597` prints each id's class, level
  and castable verdict, which is how the 9..21 band was measured.

Hunk 1's run carries its own negative control: a second hit in the SAME run with
`over 6` gives status 5 either way, so the divergence is specific to the
`over > 9` overkill branch rather than a blanket change.

### Cannot fire — established, not outstanding (2)

| hunk | why |
|---|---|
| 8 | **No-op by construction.** The line above fills all 768 bytes of `clutbuf` with 1, so 1.2's `jt399(clutbuf+96, 432, 1)` writes the value already there. 1.0's `0` punched a hole; 1.2 stops. There is no state to diff. |
| 33 | **No observable effect.** It deletes a `-4943` clear made redundant by hunk 31's clear at the loop top. `l709e` is the only reader of `-4943` anywhere, so the sole difference is the value left after it returns, which nothing looks at. Confirmed from the other side while promoting 31: the two are a PAIR, and an honest 1.0 build restores this clear as it removes 31's — with exactly one of the two present the flag behaves identically, which is precisely why 33 on its own has nothing to measure. |

**This list used to have four entries, and two of them were wrong.** Hunks 34
and 23 were filed here on the reasoning "nothing in the shipped data produces
the input". That is a statement about the DATA, not about the code, and it does
not belong in the same category as 8 and 33 — which are no-ops by construction
and stay. Hunk 34 is now observed firing (below); 23 has moved to "needs a
situation". The general lesson, learned the hard way on hunk 18: a producer
census over shipped data proves the situation is not SHIPPED, never that it is
unreachable. Check whether it is AUTHORABLE before closing the case. The two
that remain here are unreachable for reasons internal to the code.


### Reaching the Monster Editor headless (the hunk-9 recipe)

Fully keyboard + injected-mouse; no special build flag beyond the diagnostic.
Work in a THROWAWAY design — the editor writes into the current design folder.

```sh
cp -r data/frua-mac/joined/HEIRS.DSN data/work/gamedata/MONTEST.DSN
python3 -c "n=b'MONTEST.DSN'; open('data/work/gamedata/start.dat','wb').write(n+b'\0'*(35-len(n)))"
make EXTRA_CFLAGS='-DFRUA_MONDIAG'
D=.claude/skills/run-falcon-port/driver.sh
env -u DISPLAY FALCON_TOS=/usr/share/hatari/tos404.img $D start
env -u DISPLAY $D key m           # main menu -> Monster Editor (hotkey 'M')
env -u DISPLAY $D click 352 439   # "Edit"  (list bar: Leave|View|Rename|Edit|Default|Copy)
env -u DISPLAY $D click 291 305   # the Strength value box
env -u DISPLAY $D key 1 8         # -> 18
env -u DISPLAY $D key Return      # commit the field
env -u DISPLAY $D click 395 305   # the exceptional-Strength % box
env -u DISPLAY $D key 5 0         # -> 50
env -u DISPLAY $D key Return
env -u DISPLAY $D click 400 108   # Name field — deactivates the numeric field
env -u DISPLAY $D click  46 439   # "Ok"  — FIRST click only deactivates
env -u DISPLAY $D click  46 439   # "Ok"  — SECOND click actually fires it
```

Four traps, each cost a run:

- **`Ok` needs TWO clicks after a field edit.** The first click off an active
  TextEdit only deactivates it; the button takes the second. A single click
  produces no `jt263 jt325 -> ...` line at all, which reads exactly like a
  dead button.
- **The button bar is overdrawn by `jt360`'s "Valid numbers: 3 - 30" banner**
  and never repaints while the form is open. `Ok`/`Prev`/`Next`/`Cancel` are
  still live underneath — the banner is plain text (`jt94` at row 24), not a
  DLItem. Do not conclude the buttons are gone because you cannot see them.
- **The saved file is `MONSTnnn.dat`, lower-case**, from the `"%s%03d.dat"`
  format in `jt129` — a case-sensitive `ls MONST042.DAT` finds nothing on a
  Linux host even though the write succeeded.
- **Saving also rewrites the design's `STRG001.DAT`**, one byte: the name-table
  flag at offset 29 goes `0x06 -> 0x46` (`v8 = v7 | 64` in `l611c`'s `jt350`
  call). Restore it between A/B runs or the second run starts from different
  state. This is why the recipe uses a throwaway design.

`ctx[3]` is the monster id, so the list row you edit picks the file: the first
row, BASILISK, is id **42**, not 0 — the list is the stock table, and HEIRS'
own MONST101/102/108/109 are far down it.

A note on why an UNEDITED save proves nothing: every shipped MONST record
already has `rec[113+2i] == rec[112+2i]`, so loading one and pressing Ok makes
the fix write back the bytes that were already there. Verified on all four of
HEIRS' records and on stock BASILISK. The divergence has to come from an edit.

### Needs a situation (6)

| hunks | the situation still to construct |
|---|---|
| 13 | the editor's test-play Hall — and the obvious route is a DEAD END. `l07dc` picks the Hall only in its `else`: `if (g_a5_-18485 != 0) { jt582(); ... } else { jt918(1); }`, and `jt918` is `l0aae`'s sole caller. So a non-zero `-18485` at play entry means the Hall is never reached — the flag has to go non-zero AFTER `jt918`'s loop is already running. `l30d4` (the spell-memorization sub-editor) is not it: it sets 5 on entry and restores 0 on exit. The one writer that leaves it set is `l3236` case 7 (CODE 11 @0x3348, the GEO editor's tool command — 1, then 2 when `jt318` agrees), so the situation is: enter the editor from `jt918`, issue that command, and see whether the loop re-enters `l0aae`. **That command is now driven** — it is Utilities -> Test module, the same one hunks 30/31 used; see their recipe. What is left is finding a path that re-enters `l0aae` with the flag still set. |
| 7 | the `L3f80` picker modal. |
| 10, 11 | delete a monster from a design folder. |
| 14 | a party wipe with a summoned creature still on the combatant list. |
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

**All four are unblocked now that #84 has landed.** Hunk 9 is promoted (above) —
and its run is the end-to-end proof that the stash/drain fix works, since every
step after `key m` is an injected click into a modal the pump used to starve:
the list bar, the record form's two numeric fields, and the `Ok` button.

A note on how 27/28 got measured anyway: with no input delivered, `l23b4` exits
by its own TIMEOUT (`-24138` / `-13006`) and `l25b6` returns a cached result,
which reached the `jt933` call. So the bug is what let that measurement happen
without fixing it first — and the diagnostic logs the 1.0 reading and the 1.2
reading side by side from the same live event record, which is stronger than an
ON/OFF pair.

### Machinery added for this

- **`tools/mk_noperma_design.py`** — authors `NOPERMA.DSN`: one room whose entry
  cell fires a combat with `ev[12]` bit 6 (no-permadeath) and bit 5 (start
  adjacent) set. `--noflag` builds the bit-6-clear control, so two runs differ
  in one bit of one event byte. `--monster <id>` picks the opposition: the
  default 1 only ever deals damage (hunks 1 and 15), **42 (BASILISK)** adds the
  petrifying gaze that reaches hunk 17. No shipped design sets bit 6 on
  the HEIRS path, so the family was unreachable without this.
- **`-DFRUA_PARTYHP=<n>`** — clamps every combatant's current HP at combat
  entry, so a fight reaches the dying/destroyed branches deterministically
  instead of hoping the dice cooperate. Note it walks `-27928`, which is the
  whole combatant list, not just the party (the same fact that makes hunk 14
  matter). Release-guarded.
- **`tools/mk_bigpic_design.py`** — authors a module whose entry cell raises a
  BIGPIC prompt, so `-22281` is live when the Items browser opens (hunks 19-22).
  `--shop` is the variant that offers the Sell / Id arms and actually diverges;
  the plain message variant reaches only arm 4, the negative control.
- **`tools/mk_caster_chr.py`** — authors a Magic-User with memorized spells into
  the design's saved-character pool, so the spell screens are reachable at all.
  NOT usable as a combat party member (see the hunk-17 traps above).
- **`-DFRUA_SPLDIAG`** — dumps the whole `-16906` spell table once (id, name,
  class, level, targeting mode, in-combat flag, cast time). The spell id IS the
  effect index — `jt547` hands it straight to `jt599` and the `-24066` UA_FX
  table — so this doubles as the map from a spell to the handler it runs. It
  also logs `l4faa`'s slot table and `l2184`'s word extraction (hunk 16).
- **`-DFRUA_FXDIAG`** — the hunk-18 chain: `l026e`'s code-137 sweep (hook
  installed? actor carrying it? the two data-driven producers? is the `l77a0`
  override `jt820`?), `jt820`'s item->bearer mirror, and `jt822`'s entry,
  victims and resulting effect-148 node value.
- **`tools/mk_movetest_design.py`** — authors a module whose entry cell fires a
  type-12 scripted-movement event (hunk 29). Default builds the auto-chain
  variant (`ev[3]` links to a message); `--rescan` sets `ev[7] & 0x20`, clears
  `ev[3]` and hooks the message to the cell the walk lands on. The two variants
  diverge in OPPOSITE directions, which is the point.
- **`-DFRUA_MOVDIAG`** — logs `l2e42`'s exit state (frames, `ev[7]`, `-4943`,
  `-4942`, the party cell) and `l709e`'s whole tail decision (`-4945`, `-4942`,
  `-4943`, `-18484`, `ev[3]`, the cell) plus which arm won — `AUTO-CHAIN to
  event N` or `RE-SCAN landed cell, event N`. Reading the party cell here is
  what caught the facing-0-moves-along-columns trap.
- **`tools/mk_testplay_design.py`** — authors the module hunks 30/31 need: one
  event-free cell for the editor cursor and the normal-play save, one type-11
  transfer with `ev[12]` bit 2 set, and a once-only Yes/No question on every
  other square.
- **`-DFRUA_TPDIAG`** — the test-play chain: every menu pick as `(group, item)`
  from `jt341` (which is how you calibrate an editor pulldown without guessing
  pixels), `l3236` case 7's `-18485` + editor cursor, `l5676`'s transfer return
  (`ev[12]`, the `-4943` it leaks, `-27982`), and `l709e`'s per-iteration
  inherited `-4943` + the `-27982` that gates the whole convergence block.
- **`-DFRUA_NPDIAG`** — logs the flag seed at combat entry and the decision at
  all three no-permadeath sites; the `jt860` site also names the subject and its
  side, which is what proved hunk 17 lands on a PARTY member and not a monster. **`-DFRUA_OVDIAG`** now also logs the overland
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
