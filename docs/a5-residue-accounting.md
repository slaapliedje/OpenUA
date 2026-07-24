# A5 scalar residue — coverage accounting and the demand-driven policy

Status as of 2026-07-24 (#68 closed, commit d4bf87ff). This documents where
every byte of the Mac DATA image's scalar residue stands for the Mac-free
(replay-off) build, which extraction passes exist, and — equally important —
which further passes were **measured dry** so nobody re-runs them.

## The ledger

The DATA image holds 6,445 bytes of initialised scalars that no relocation
covers (`tools/a4map.py scalar_runs`). For the replay-off build they are
supplied by four mechanisms, in application order:

| Mechanism | Bytes | Ships? | Where |
|---|---|---|---|
| Zero-fill | (all zero bytes) | yes | `data_pool_replay` buffer init |
| Authored scalars | ~560 + matrices | yes | `src/engine/a5_scalars.c` (pinned byte-exact by `tests/test_a5_scalars.py`) |
| DOS positions map, exact | 113 runs / 5,544 B | yes (positions+checksum only) | generated into `a4_map.c`, applied by `dos_scalars.c` from the user's CKIT.EXE |
| DOS positions map, swap16 | 4 runs / 44 B | yes | same, `A4_DOS_SWAP16` flag |

Remaining residue: **1,958 bytes across 813 runs**, almost all under 6 bytes.

## The two facts that close the effort

1. **None of the residue is referenced.** `referenced_offsets` over
   `src/engine` + `compat` finds no named accessor within ±64 bytes of any
   residual run. The referenced A5 world is fully covered. (Caveat from the
   tool's own docstring: this is a lower bound — indirect indexing is
   invisible to it. The compass table (#73) was found that way, which is why
   the policy below exists.)

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
2. If the values are formulaic or are game rules, author them in
   `a5_scalars.c` and pin them in `tests/test_a5_scalars.py` (the #68
   rules-matrix / #75 terrain-matrix precedent).
3. If they exist in CKIT.EXE, extend the positions map. For word tables
   remember the two structural traps `swap16_pass` handles: x86 stores them
   little-endian, and the Mac nonzero-run extractor fragments them at
   interior `0x00` high bytes, so match a re-windowed image span (zeros
   included) at each sub-word alignment — never the bare run.
4. Only if all of that fails, fall back to the replay (Mac installs keep
   working regardless; this only gates the Mac-free goal).

## Symptom signature

A missing scalar seed does not crash: it yields a correctly-executed wrong
path (zeroed table → default/zero behaviour). The #75 combat-map wall
chunks and the #73 bare compass dome are the canonical examples. If a
replay-off build misbehaves where replay-on is fine, suspect a residual
slot reached indirectly, and start at step 1 above.
