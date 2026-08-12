# Exit-from-Play freeze — investigation (headless repro)

**Status: REPRODUCED headlessly and NARROWED, root cause not yet fixed.**
The freeze is real, current, and reproduces in Hatari; the leading suspect is
the `-DFRUA_NOVA`-in-every-binary consolidation (7e4d74b7), NOT the modify-char
work and NOT the `l2d3e` gate.

## The user report

> Before we merged the two binaries into one, the ST-Low version would let me
> click "Exit from Play" and it'd go back to the main screen. Now both in Nova
> mode and ST-Low, it freezes the game. It is specifically
> **P → C → D → E → E → Freeze.**

Decoded on the populated Training Hall:
- **P** Play the Game → Training Hall (needs an active party; see "Reaching the Hall").
- **C** Create Character → PICK RACE/CLASS screen.
- **D** DONE → the character-detail screen (`l618c`; STR/INT/…, REROLL/MODIFY/DONE/EXIT).
- **E** EXIT that screen → back to the Hall.
- **E** EXIT FROM PLAY → should repaint the main menu → **freezes instead.**

Note the path **does NOT click MODIFY**, so `jt178`'s shape-5 stat cells
(c3332ed5) are never installed on this path.

## What was PROVEN headlessly (Hatari `--machine ste`, the 68000 build)

Hatari STE with a card-less machine is a faithful stand-in for the user's
ST-Low: `dsp_detect` runs the Nova probe, reads `planes = 4`, and hands back to
the ST backend (`DBG.LOG`: "nova: not 8bpp - handing back to the ST backend").

1. **The freeze reproduces.** Full path PLAY→Hall→CREATE→DONE→char-detail→EXIT→
   Hall→EXIT-FROM-PLAY leaves the machine hung: the game-drawn cursor freezes in
   place, three consecutive frames are byte-identical, and neither a click on a
   different button nor `a`/`Escape` keys change anything. Hatari's process stays
   alive — i.e. the emulated CPU is spinning, exactly a freeze. Every earlier
   screen moved the cursor and responded, so this is not a dead harness.

2. **`jt178` is EXONERATED.** The repro path never enters the MODIFY sub-screen,
   so the shape-5 cells are never installed. Also matches the timeline:
   c3332ed5 predates the merge and was in the ST-Low build the user says worked.

3. **The `l2d3e` gate-revert (b89efa29) is DISPROVEN.** The build that froze in
   (1) *is* b89efa29 — the gate was already restored. Restoring it does not fix
   the freeze. (The restoration is harmless/faithful, but it is NOT the fix, and
   its commit message calling it a "freeze suspect" is wrong.)

4. **Quit-to-desktop does NOT freeze** in either build. PLAY→add-list→EXIT→
   "GAME NOT SAVED. QUIT ANYWAY?"→YES tears down cleanly to the GEM desktop in
   BOTH the NOVA and the no-`FRUA_NOVA` builds (frame reaches the desktop, fully
   responsive). So the teardown that hangs is specifically
   **"Exit from Play → return to the main menu," after the char-creation/`l618c`
   path** — not the general play teardown.

## The leading suspect: `-DFRUA_NOVA` (the consolidation)

The user's bisection ("before we merged the two binaries") points at exactly two
commits: 7e4d74b7 (add `-DFRUA_NOVA` to every Atari build) and 8b63aefa (two
named binaries). 8b63aefa is pure build plumbing. **The only functional code
change in the merge is `-DFRUA_NOVA`.** It:
- explains why BOTH Nova-mode AND ST-Low regressed at the same point (the define
  is compiled into the one binary both machines run), and
- adds, at boot, a `dsp_backend_nova()` probe that calls **`appl_init`** (AES) +
  `graf_handle` + `v_opnvwk` — something the pre-merge ST-Low binary NEVER did
  (FRUA is a GEMDOS PRG, not an AES app). On a card-less ST the probe is balanced
  (`v_clsvwk` + `appl_exit`) and hands back to the ST backend; on the card it
  stays open because the Nova backend renders through it.

Working hypothesis: registering the GEMDOS PRG with AES via `appl_init` at boot
poisons the later "Exit from Play → main menu" teardown (a screen re-init /
resource reload that conflicts with a registered-but-unserviced AES app),
hanging the CPU. This fits every observation, including that it hits the Nova
machine too (where `appl_init` stays open).

**Not yet proven**, because the definitive A/B needs the *populated Hall* path in
both builds, and the Hall is only reached with an active-party "current game"
state that the headless quit sequences consume and that could not be cleanly
rebuilt through the slow-STE mouse/key harness (Escape/keys get dropped).

## Reaching the Hall (for anyone continuing this)

- PLAY with an **empty** active party → the ADD-A-CHARACTER list directly, not
  the Hall. EXIT there → "QUIT ANYWAY?"; Escape (with a char added) is documented
  to drop to the Hall but the STE key injector drops it intermittently.
- The Hall (ADD/REMOVE/MODIFY/CREATE/TRAIN/… + LOAD/SAVE/BEGIN/EXIT-FROM-PLAY)
  needs an active party — persisted via `CHAR0000.CHR`… in the gamedata root and
  the current-game state. Re-establish that first, then PLAY lands on the Hall.
- Play loop: `jt953` / `L4be8` (boot.c ~5101). Exit-from-Play returns from the
  Hall dispatcher to the main-menu loop.

## Recommended next step

The decisive artifact is the **`DBG.LOG` from a real freeze** (the user
reproduces it in seconds). Instrument the boot Nova probe and the play→menu
teardown with **file-based** `dbg_file_num` markers (NOT `dbg_log`, which paints
the screen), build the Nova binary, reproduce, and read the last marker — it
pinpoints whether the hang is inside an AES/VDI trap left by `appl_init` or in
the engine's own re-init. A blind fix is risky because it must cover BOTH the
card-less ST and the Nova-card machine, and only the log distinguishes them.

Interim option for immediate ST-Low relief: build `FRUA.PRG` (68000) WITHOUT
`-DFRUA_NOVA` and keep Nova as a separate `FRUANOVA.PRG` again — i.e. partially
revert the consolidation — while the freeze is fixed properly.
