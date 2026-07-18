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

## What it needs next

An **Amiga memory debugger** (Enforcer or MuForce under amiberry) to catch the
faulting access directly — log tracing has taken this as far as it can. The
prime suspects to point it at, in order:

1. The render **unwind after `jt199`** (`render_3d_faithful` tail → `jt312`
   tail → `jt935` tail): a **pointer** overrun that only trips on the Amiga's
   memory layout. NOT stack depth — bumping `__stack` from 256 KB to 1 MB left
   the freeze unchanged (2026-07-18), so rule stack out and look for a bad
   pointer / buffer overrun (a write past an array, a stale handle, a
   platform-address-dependent cast).
2. The AGA display present path under the walk's pattern (`aga_present` /
   `c2p_amiga` / the copper flip) — though `aga_present` itself has no loop.

Once the walk stops freezing and reaches `l63c0`, the HUD text should appear
(the CLUT install is already correct — it just never runs today).

## Repro

    make MACHINE=amiga EXTRA_CFLAGS='-DFRUA_HUDTRACE -DFRUA_AREATEST -DFRUA_SKIP_ENTRY_EVENTS'
    cp frua data/work/amiga-mount/frua && rm -f data/work/amiga-mount/DBG.LOG
    flatpak run com.blitterstudio.amiberry -f ~/Amiberry/Configurations/openua.uae -G
    # then: grep -a 'HUD\|l63c0\|j200_dump' data/work/amiga-mount/DBG.LOG
    # Falcon comparison (HUD renders, port_hud_text_clut count=1):
    make EXTRA_CFLAGS='-DFRUA_HUDTRACE -DFRUA_AREATEST -DFRUA_SKIP_ENTRY_EVENTS'
