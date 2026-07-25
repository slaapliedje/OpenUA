# A5 scalar residue — coverage accounting and the demand-driven policy

Status as of 2026-07-24 (#67 closed, commit 3bd348d8; #68 before it). This
documents where every byte of the Mac DATA image's scalar residue stands for the
Mac-free (replay-off) build, which extraction passes exist, and — equally
important — which further passes were **measured dry** so nobody re-runs them.

## The ledger

The DATA image holds 6,445 bytes of initialised scalars that no relocation
covers (`tools/a4map.py scalar_runs`). For the replay-off build they are
supplied by four mechanisms, in application order:

| Mechanism | Bytes | Ships? | Where |
|---|---|---|---|
| Zero-fill | (all zero bytes) | yes | `data_pool_replay` buffer init |
| Authored scalars | ~560 + matrices | yes | `src/engine/a5_scalars.c` (pinned byte-exact by `tests/test_a5_scalars.py`) |
| DOS positions map, verbatim | 113 runs / 5,544 B | yes (positions+checksum only) | generated into `a4_map.c`, applied by `dos_scalars.c` from the user's CKIT.EXE |
| DOS positions map, byte-swapped | 264 runs / 3,942 B | yes | same, `A4_DOS_SWAP16` / `SWAP32` / `SWAP16_Q` |

Remaining residue: **1,121 bytes across 313 runs**, almost all under 6 bytes.

### The byte-order passes (why there are three)

CKIT.EXE holds the same tables, but in x86 order, and *how* they are reversed
depends on the element type. Getting this wrong does not fail loudly — it
leaves the table zeroed, which the engine executes correctly and wrongly:

| flag | element | example |
|---|---|---|
| `A4_DOS_SWAP16` | every 16-bit word | the XP ladder at A5-17514 (#68) |
| `A4_DOS_SWAP32` | every 32-bit long | per-class XP thresholds, A5-30212 |
| `A4_DOS_SWAP16_Q` | the leading word of a 4-byte record, trailing bytes untouched | racial age/HP quads, A5-30780 (`short base; char dice; char sides`) |

The last two landed with #67: word-swapping alone left both chargen rules
tables in the residue, and a Mac-free build rolled **Level 1, age 10**
characters where the replay gives level 6, age 26. Locating them also needed
the fragments re-windowed (`swap_pass`, ±8 bytes), clustered
(`swap_coalesce_pass`, the analogue of `coalesce_runs_by_dos`), grown to the
full span DOS agrees over (`grow_swapped_run`) and re-fused (`merge_swapped`,
so boot does not pay a seek per crumb).

`tests/test_a4map_dos.py` pins all of it: every swapped run must reproduce the
Mac image from CKIT.EXE under its declared transform, the two chargen tables
must be fully covered, and A5-804 must stay residue.

## The two facts that close the effort

1. **None of the residue is referenced.** `referenced_offsets` over
   `src/engine` + `compat` finds no named accessor within ±64 bytes of any
   residual run. The referenced A5 world is fully covered. (Caveat from the
   tool's own docstring: this is a lower bound — indirect indexing is
   invisible to it. The compass table (#73) was found that way, and so were
   #67's chargen tables, which is why the policy below exists.)

2. **The residue is not in the DOS release.** Probed 2026-07-24 against
   every file in the GOG tree (CKIT.EXE, all DISK1-3 data files, ~40 files):
   raw, 16-bit-swapped and 32-bit-swapped, at all four sub-word alignments,
   windowed over the image with interior zeros included. Yield: one 7-byte
   chance match inside DOSBox.exe. The ≥6-byte residue totals only 524
   bytes; the rest is fragments below any credible matching threshold.

So the unmatched remainder is some mix of Mac-only constants (the A5-804
pitch table has this proven character — it exists in no DOS file) and tiny
flag/count fragments, none of which the lifted engine reads today.

## The demand-driven policy

Do **not** author or extract residue speculatively. When a future lift
references a slot the seeds do not cover, in order of preference:

1. Check the generated residue report (`a4map.py --residue-report`): it
   lists every residual run with an `[R]` mark when a named reference lands
   in its cluster.
2. **Probe CKIT.EXE before authoring anything.** #67's tables looked like
   textbook authoring candidates (AD&D XP ladders and racial ages — rules, not
   Mac trivia) and both turned out to sit verbatim in the user's own binary
   under a byte order no pass had modelled yet. Probing costs one script; a
   hand-authored table is source that has to stay correct forever. Take the
   whole span at once (zeros included, at each sub-word alignment) in raw,
   swap16, swap32 and swap16-per-quad form — never the bare fragment.
3. If it is there, extend the positions map (a new `_SWAP_MODES` entry if the
   element type is new) rather than the source.
4. If it is genuinely absent and the values are formulaic or are game rules,
   author them in `a5_scalars.c` and pin them in `tests/test_a5_scalars.py`
   (the #68 rules-matrix / #75 terrain-matrix precedent).
5. Only if all of that fails, fall back to the replay (Mac installs keep
   working regardless; this only gates the Mac-free goal).

## Symptom signature

A missing scalar seed does not crash: it yields a correctly-executed wrong
path (zeroed table → default/zero behaviour). The #75 combat-map wall
chunks, the #73 bare compass dome and #67's **Level 1, age 10** char-gen are
the canonical examples — the last one is the clearest, because "age 10" is
literally `jt870(0,0) + 0 + 10`, the age formula reading an empty table. If a
replay-off build misbehaves where replay-on is fine, suspect a residual slot
reached indirectly, and start at step 1 above.

## How to find one: the two-build parity harness

The measurement that caught #67's tables, reusable for any screen:

```sh
# control — Mac DATA replay (root frua.rsc supplies the pool at build time)
make
# subject — Mac-free: stub pool, A5 world rebuilt from relocs + authored
# scalars + CKIT.EXE (data/work/gamedata/frua.rsc must be the DOS-built one,
# i.e. STRS only, so there is no DATA/ZERO/DREL to replay)
make NOEMBED=1
```

Drive the SAME key/click stream on both under
`.claude/skills/run-falcon-port/driver.sh` and diff the frames
(`compare -metric AE`); `AE=0` is the pass. `data_pool:` in the log says which
mode you got — `replayed bytes = 31336` for the control, `no compiled pool and
no runtime DATA/DREL` plus `dos_scalars: runs applied` for the subject.

Two traps this harness has:

- **`-DFRUA_ENTRY_LEVEL/_ROW/_COL/_FACING`** (`l0bbc`) drops the party on any
  cell, which is the only sane way to reach a shop or temple repeatably — but
  `.machine`'s buildstamp hashes only the FIRST `EXTRA_CFLAGS` word and their
  count, so switching COL between two runs does **not** trigger a rebuild.
  `rm -f src/engine/boot.o` first or you will compare a binary against itself.
- **Anything random is not a parity signal.** Char-gen rolls abilities, so run
  the control twice first to measure the noise floor (AE 868 here) and compare
  the *deterministic* fields — crop age and level, which must be `AE=0`.
