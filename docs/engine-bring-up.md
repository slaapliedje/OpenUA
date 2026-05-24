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
