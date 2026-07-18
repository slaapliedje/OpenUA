# Amiga in-game HUD — RESOLVED (task #25)

**Original symptom:** on the AGA build the in-game roster / command bar showed
no text; deeper testing showed the walk froze on the stock A1200's **68EC020**
(no MMU) while working on a 68030.

**Root cause (found by source bisection + a malloc heap-integrity probe, not a
memory debugger — MuForce needs an MMU the 68EC020 lacks): the AREATEST DEBUG
DUMPS, not the engine.** `dbg_dump_view` / `rm_audit` (the render-state dumps
that write `VIEWDIAG.TXT` / `RMAUDIT.TXT` under `FRUA_SKIP_ENTRY_EVENTS`)
carry buffer overruns — `dv_app` appended to a `char buf[512]` / `buf[4096]`
with no bounds check. The overrun clobbered adjacent memory; on the MMU-less
68EC020 that corrupted the exec/allocator free-list, so the **next `malloc`
hung** (surfacing far from the write, e.g. inside `load_backdrop`'s
`NewPtr(163840)`). The 68030's MMU-managed layout absorbed the same write, so
the bug looked like a "CPU-specific engine freeze." **The engine itself is
clean on the 68EC020** — proven by disabling the dumps: the full HUD renders
(roster, compass, position, clock, AREA/CAST/VIEW bar, 3D view), 587 log lines,
no freeze.

## The fix

- `dv_app` now bounds-checks against `DV_BUF` before every append.
- `rm_audit`'s buffer grew from `512` to `DV_BUF` (4096).
- The three AREATEST dumps (`dbg_dump_view` / `rm_audit` / `j200_dump`) are
  compiled out on the Amiga (`!defined(FRUA_AMIGA)`): they are host-debug tools
  with more 68EC020-unsafe accesses than just those two overruns, and they were
  never part of real play. AREATEST on the Amiga now runs to the HUD.

## Why MuForce did not apply

MuForce/Enforcer require an MMU. The 68EC020 has none, and switching amiberry to
a 68030 to get one *removed the bug* (it works there). The malloc heap-probe
(a `NewPtr(163840)` that hangs iff the free-list is already corrupt) did the job
instead — bisecting the corrupting write down to `dv_app`.

## Real-hardware note

The stock-A1200 / CD32 target (68EC020, ~2 MB chip + 2 MB fast) is now exercised
green in amiberry. The `openua-mmu.uae` (68030) config is kept as an alternate.
