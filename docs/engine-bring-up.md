# Engine bring-up probe

The engine's entry path (`ua_main` in `src/engine/boot.c`) calls several
dozen functions that are still no-op stubs awaiting lifting. Bringing up
the shim was step one; the next priorities want to be driven by what the
engine actually tries to do per frame, not by guessing.

## Running a probe

Build with the `ENGINE_PROBE=1` knob — every stub in `boot.c` and
`master.c` then logs `stub: <name>` as it's called, `jt315` (the play-loop
predicate) fires exactly once so the per-iteration body runs, and the
trail mirrors to the host terminal via Hatari's `--conout 2`.

```sh
make clean
make ENGINE_PROBE=1 run
```

Default builds are unaffected; the macro expands to `((void)0)` when
`FRUA_ENGINE_PROBE` isn't defined.

## First probe — 2026-05-23

The probe ran `ua_main` end to end (rc = 0) — no crashes, no missing
symbols. The full sequence:

```
main: entered → display up → shim up

# Phase 3 — screen-mode setup (boot.c)
jt398 → jt411 → jt1129

# master_init() — Toolbox + page setup + fc_init
l0eda → jt1157 → jt1155 → jt1138 → l01a2 → l024c → (fc_init, real)
   → l35e2 → l27a4

# Phase 4 — secondary init, first UI handler
jt480 → jt989 (handler "Pod") → l4d98 → l0444 → jt361 → jt920 → jt1009

# Phase 5 — string check, second UI handler
jt919 (string-2 mismatch — no resources loaded yet)
l6ada → jt977 → l3918 → jt989 (handler "Pod") → jt1130 → l5888

# Phase 6 — play loop, one iteration via the probe
jt315 (firing) → jt949 → jt956 → jt920 → l07dc → jt315 (done)

# Shutdown
jt445 → l5ac0 → (master_shutdown)
   l27bc → l35f8 → (fc_cleanup, real) → jt1156 → l01ac
   → jt1119 → jt1114 → jt0f14 → jt1158
jt415

main: ua_main rc = 0
```

## What the probe tells us

- **The entire bring-up surface is sufficient.** No crash, no unlinked
  symbol; every Toolbox call the engine makes during boot resolves to a
  lifted function or a no-op stub that returns cleanly.
- **The per-frame call surface is four entries:** `jt949`, `jt956`,
  `jt920`, `l07dc`. `l07dc` is documented as the per-iteration body —
  this is the highest-impact lift for visible engine behaviour. `jt315`
  is the predicate that gates the loop.
- **`jt919` runs unconditionally** because `ua_get_string(2)` returns
  empty (no FRSC archive loaded). Lifting the FRSC packer (`tools/rsrcpack`)
  and feeding the engine real STR# resources changes that branch.

## Next-up priorities driven by the probe

1. `l07dc` — the per-frame body. Largest visible effect.
2. `jt315` — the play-loop predicate. Determines when the engine quits.
3. `jt398` — control-file probe (`:DISK4:ALWAYS.CTL`). Drives the
   "small / large screen mode" branch in phase 3.
4. `jt989` — UI-handler registration. Called twice during init; the
   registered handlers run later from the event loop.

Re-run the probe after each engine lift to see which stubs fall out of
the trace and which new ones appear.

## Seventh probe — after lifting jt480 + synthetic strtab

jt480 (CODE 3 + 0x3c6) is the string-table setter — two instructions:
`movew arg1, A5_-10276; movel arg2, A5_-10280`. ua_main forwards its
own (arg1, arg2) here, so on the Mac the THINK C runtime's
DATA+DREL-computed string table flows in before phase-4 starts. main()
now passes a small synthetic table (index 2 = "Heart") because the
runtime's pool replay isn't lifted yet.

First behavioral change in the probe trace: `jt919` no longer fires.
Phase 5's `if (ua_strcmp(ua_get_string(2), "Heart") != 0) jt919()`
gate finally evaluates to false because index 2 returns "Heart"
through the now-real ua_get_string.

```
... → jt989 → l4d98 → l0444 → jt361 → jt920 → jt1009
                                            ^^^^^^ jt919 used to be here
   → l6ada → jt977 → l3918 → jt989 → jt1130 → l5888 → ...
```

The shutdown's matching `if (ua_strcmp(ua_get_string(2), "Heart") == 0)
fc_dump(0L)` now runs fc_dump (real — no PROBE marker, so it's not in
the trace) at exit. ua_main still rc=0.

## Sixth probe — after lifting JT[399]

JT[399] (CODE 3 + 0x39d2) is the engine's memset-equivalent: fill `size`
bytes at `buf` with the low byte of `fill`. Mac C signature places
`buf` first, `size` second, `fill` third — the right-to-left pushed
order puts the first C arg at fp@(8), which earlier stubs of jt399 had
in the wrong order. Lifting forces the fix: the four call sites in
L5124 and jt918 now pass arguments correctly, and the memset runs for
real.

Trace unchanged from the previous probe — the lift is behavioral
correctness without new visible work, since the buffers being filled
are A5-world bytes the engine hasn't started reading yet.

## Fifth probe — after lifting jt942 / jt943

jt942 (CODE 20 + 0x472a) and jt943 (CODE 20 + 0x4738) are a paired
setter / getter on g_a5_4944, the byte that gates L07dc's inner loop:
jt942 stores the low byte of its arg, jt943 reads it back. One-line
lifts each.

Trace inside the play-loop iteration is unchanged from the L5124 probe
(jt943 isn't reached because jt918 still declines and the new-game
branch `goto cleanup`s before the predicate check at the bottom of the
loop). The lift is structural: the next caller that sets g_a5_4944 to
a non-zero value gets the loop continuation correctly.

## Fourth probe — after the L5124 lift

L5124 (CODE 6 + 0x5124) is L07dc's first-time init: zeros three buffers
through JT[399], sets a handful of fields inside the player-data handle,
resets ~30 A5-world bytes / shorts / longs to game-start defaults, then
calls JT[174] (per-segment graphics init).

Trace inside the play-loop iteration:

```
stub: l07dc → l5124 → jt399 → jt174 → jt942 → l5888 → jt918 (skeleton)
            → jt399 → jt131 → l5888
```

Two of L5124's three JT[399] calls are guarded by NULL checks on the
handle pointer (`g_a5_28006`) and the 2000-byte buffer pointer
(`g_a5_13038`); neither is set yet, so only the third JT[399] (6 bytes
into adjacent A5 statics) fires. Lifting whichever engine code creates
those handles unblocks the full L5124 init.

## Third probe — after the jt918 skeleton lift

jt918 is the new-game / select-design dialog at CODE 12 + 0x0d90 — a
~1300-byte function with ~30 inner calls. The first cut captures only
the entry side effects (set three A5 globals, populate a 4-byte buffer
via `JT[399]`, kick off the UI via `JT[131](6)`) and returns 0 to
preserve the prior "user declined" behaviour. The main loop at L0dd4 →
L125e is documented in the skeleton's docstring but not yet executed.

```
stub: l07dc → l5124 → jt942 → l5888 → jt918 (skeleton)
            → jt399 → jt131 → l5888
```

`jt918`'s main loop is the next pass; the body dispatches the Delete /
Create / Select / Play / Edit menu through `L0aae` and per-segment
`JT[3]`.

## Second probe — after the L07dc lift

Lifting L07dc replaced the single-line `stub: l07dc` with the body's own
call sequence — five fresh callees out of the eleven L07dc dispatches
to. With the mode flag clearing to "new game" and `jt918` declining, the
loop short-circuits to the cleanup tail:

```
stub: l07dc → l5124 → jt942 → l5888 → jt918 → l5888
```

`l5124`, `jt918`, and the cleanup `l5888` are the obvious next targets.
The other six callees inside L07dc (`l4b40`, `l67ca`, `l68f8`, `l2cb0`,
`jt582`, `jt941`, `jt937`, `jt938`, `jt217`, `jt948`, `jt943`) only run
once the mode flag is non-zero or `jt918` returns non-zero — they wait
for those branches to enable.

