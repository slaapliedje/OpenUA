# Amiga in-game HUD wall (task #25) — investigation log

**Symptom (user-reported):** on the AGA build, loading a save (or entering the
dungeon walk) shows the frame chrome and the 3D view, but the party roster and
the command bar have **no text** — blank grey panels.

**It is NOT a colour/CLUT bug.** It is the visible symptom of a deeper
**Amiga-only in-game freeze**. Findings, bisected live under amiberry with the
`FRUA_HUDTRACE` facility (guarded traces in `jt1089` and `port_hud_text_clut`):

1. The roster text **is drawn** — `jt1089` logs 43 HUD glyph runs on the Amiga
   ("Name", "AC HP", "BARBARUS", the whole party). So `DrawChar`/the font work.
2. `port_hud_text_clut` — which restores the UI text colours (indices 1, 6–15)
   into the dungeon CLUT 129 so the roster/command-bar text is readable — runs
   **once on the Falcon** and **never on the Amiga** (trace count 0). Without
   it the roster glyphs render in wall-palette colours ≈ the panel grey, i.e.
   invisible; the command bar (`l2c60`) is never drawn at all.
3. `port_hud_text_clut` is only reached from the play-HUD compose in `l63c0`
   (and `jt312`'s non-deferred HUD block). **`l63c0` is never entered on the
   Amiga** (its ENTER trace never fires), and `jt240` (the dungeon walk driver
   that calls it) is never reached either (no `jt240` marker).
4. The freeze is upstream of the play loop. Trace order on the Amiga ends at:
   entry roster draw (`jt937` @ boot.c ~4685, the 43 glyphs) → `jt935` @ ~4704
   → `jt312` → `render_3d_faithful` → `jt199` → the `j200_dump` diagnostic
   (`J200DIFF.TXT`, the LAST log line) → **stop**. The play-loop branch trace
   (`jt948` L4be8, ~4713) never fires, and `jt312`'s own HUD block is skipped
   (`s_view_first` already consumed), so the freeze is in the render **unwind**
   between `jt199` returning and the play loop — a bad memory access the CPU
   does not survive (logging stops mid-flow, no "Out of memory" / no marker).
5. It is deterministic-ish in WHERE it lands but the exact faulting instruction
   is not visible to log tracing. `fastmem_size=4` (vs the config's 8) makes it
   worse — the engine stalls in early boot (insufficient far-pool memory), so
   it is not a simple "too much fast RAM" issue.

**This is the same family as the earlier Amiga in-game hangs** (the on-load
converter hang, ADR-0015). The menu — light on memory and presents — is fine;
the walk — heavy c2p/flip presents and the deep render recursion — freezes.

## BREAKTHROUGH (2026-07-18): it is 68EC020-specific — the AGA build WORKS on a 68030

Booting the identical `frua` under a **68030** amiberry config (`cpu_model=68030`,
`mmu_model=68030`) instead of the A1200's stock **68EC020**: the walk runs to
completion, `port_hud_text_clut` fires, and the **full HUD renders** — roster
with AC/HP, compass + position "10,8" + clock, the AREA/CAST/VIEW command bar,
the 3D dungeon (screenshot: `amiga_68030_hud.png`). The freeze is entirely
gone. So the "blank HUD" was never a colour/CLUT or draw bug — it is the
68EC020 walk **freeze**, and the freeze only happens on the MMU-less 020 core.

Isolation: `68EC020 + cpu_compatible=true` still freezes (so it is NOT an
emulation-accuracy setting); `68030` works. Signature — **fatal without an
MMU, harmless with one** — is the classic wild / NULL-area access: on the
68030+MMU the bad address lands in mapped RAM harmlessly; on the bare 68EC020
it corrupts something and hangs.

### MuForce does NOT apply here (important)

MuForce/Enforcer require an **MMU**. The 68EC020 has none — so MuForce cannot
run on the very CPU where the bug lives. Switching to a 68030 to get an MMU
*removes the bug* (it works there), and MuForce also would not install on
amiberry's 68030/68040 MMU emulation in this setup (no banner in its log — the
mmu.library never took control; the full MMULib/SetPatch MMU init is needed and
amiberry's 030 MMU emulation is incomplete). Net: **MuForce is the wrong tool
for a 68EC020-only bug.** Abandoned.

## What it needs next

The bug is a wild/NULL-area access that only bites on the MMU-less 68EC020, and
MuForce can't reach it (see above). Options, best first:

1. **Ship the AGA build for 68020 (68030 recommended).** It fully works on any
   Amiga with an 030+ — a large fraction of AGA machines (A1200 with an 030/040
   accelerator, A4000/030, A4000/040). Set the run/recommended AGA config to
   68030 and note the stock-68EC020 caveat.
2. **Find the 020 wild access by source bisection**, not a memory debugger:
   binary-search the render-unwind path (`render_3d_faithful` tail → `jt312`
   tail → `jt935` tail, after `jt199`/`j200_dump`) by short-circuiting pieces
   and re-testing on the 68EC020 config until the freeze moves — a
   platform-address-dependent pointer / a write past an array is the target.
   NOT stack depth (256 KB → 1 MB `__stack` changed nothing).
3. **Confirm whether it is even a real-hardware bug** vs an amiberry 68EC020
   emulation artifact — needs a real 68EC020 A1200, or cross-checking against
   another 020 emulator.

Once the walk stops freezing on the 020, the HUD renders (the CLUT install is
already correct — on the 68030 it already produces the complete HUD).

## Repro

    make MACHINE=amiga EXTRA_CFLAGS='-DFRUA_HUDTRACE -DFRUA_AREATEST -DFRUA_SKIP_ENTRY_EVENTS'
    cp frua data/work/amiga-mount/frua && rm -f data/work/amiga-mount/DBG.LOG
    flatpak run com.blitterstudio.amiberry -f ~/Amiberry/Configurations/openua.uae -G
    # then: grep -a 'HUD\|l63c0\|j200_dump' data/work/amiga-mount/DBG.LOG
    # Falcon comparison (HUD renders, port_hud_text_clut count=1):
    make EXTRA_CFLAGS='-DFRUA_HUDTRACE -DFRUA_AREATEST -DFRUA_SKIP_ENTRY_EVENTS'
