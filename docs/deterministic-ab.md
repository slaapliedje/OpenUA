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

`FRUA_RNGSEED`, `FRUA_HALLFREE` and the `FRUA_ENTRY_*` family are
behaviour-altering build flags and `make release` rejects them
(`src/engine/release_guard.h`).

`FRUA_CBTDIAG`, `FRUA_HALLDIAG` and `FRUA_OVDIAG` are **pure diagnostics** —
they only add `dbg_file_num` output — so by `release_guard.h`'s own rule they
are deliberately NOT guarded: noisy in a release, not wrong. Add a flag to that
header when it changes behaviour, not when it changes volume.
