# Port roadmap — where the port stands, and what is left

> Rewritten 2026-11-03. The previous version of this file was the 2026-06-19
> plan for reaching the play loop; every item on it has since landed, and it
> was still describing the dungeon as "the frontier" and the editor as
> "deferred". That plan is in the git history if you want it. For the
> per-subsystem register see `docs/subsystem-status.md`; for the measured
> list of what is open, `docs/TODO.md` ("Genuinely open").

## 1. The engine is lifted

`python3 tools/jt_progress.py` (2026-11-03): **1206 distinct jump-table entries
are called; 1201 are done** (1072 lifted, 54 faithful no-ops, 75 lifted under a
CODE-local alias), 1 stub, 4 missing. `python3 tools/stub_audit.py --stubs`:
**0 live gaps**, 0 deferred arms. The three stub bodies left are unreachable on
these machines by construction — a Mac `_Gestalt` probe that can only answer
"absent", the Mac `SFPutFile` save dialog on an arm FRUA's own code never
enters, and a `jt1206` arm gated on a mode value the port never produces.

What that means for the player: title, credits, menu, design picker, character
generation, the Training Hall, save/load, the dungeon walk, every event type,
shops, temples, encampment (rest, memorize, fix), the automap, inventory,
spells and combat through to victory or a party wipe all run, on every target.
DOS fan modules play with their own art (ADR-0017/0019/0022), installed from
the ZIP by `uainst` or dropped into a `.DSN` folder.

The **editor** is lifted too, not deferred: Create New Design makes a loadable
`.DSN`, the GEO (3D map) editor edits and saves, the event editor authors an
event that then fires in play. It is less exercised than play — expect rough
edges, and report them.

## 2. The targets

Eight display backends behind one HAL (`platform/`): VIDEL (Falcon), TT
shifter, ST-low with a Timer-B raster split, Nova-class graphics cards via the
VDI, Amiga AGA, ECS and RTG, and X11 for Atari System V / AMIX. The bitplane
machines render natively in planes (ADR-0016). Sound is the Mac four-tone synth
and sampled effects through DMA sound, Paula or AHI, with the DOS release's
MT-32 score available out the MIDI or serial port instead (`sound.cfg`).

Real hardware has run five of them: Falcon030, TT030, Mega STe (most sessions
with an ATW800/2), A500 + ACA500plus, A1200 + Apollo IceDrake. The RTG backend has
only run in amiberry. See the status table in `README.md` and the installation
notes in `HARDWARE.md`.

## 3. What is left

Nothing on this list is a feature. In rough order of visibility:

1. **Quantiser seams on the 16/32-colour machines.** About three faint rows at
   band boundaries in ECS pictures, and the faint seams the README admits on a
   16-colour ST frame. The known answer is palette continuity across bands
   (generalise the border slot trick to boundary colours), or ADR-0020's offline
   pre-quant giving the art a palette that is stable across bands.
2. **ECS title marks.** Short beige dashes in the title's red gradient on ECS
   only. `tools/artview.py` proved the art clean, so the port puts them there;
   the one candidate left is the copper palette install on non-boundary rows
   (TODO item 12's residual).
3. **Plain ST MIDI.** The 68000 build has no MIDI pump because it has no sound
   vblank to ride; the 020 Atari builds, the Amiga and the Unix build all do.
   Needs its own VBL or timer slot.
4. **Amiga MIDI on hardware.** Verified against the score in amiberry only.
5. **DOS save parity.** DOS writes 10 285 bytes to our 10 284. Saves load both
   ways regardless; the byte has never been chased.
6. **TT draw-time planar writers.** The TT still converts the rows a writer
   would have stamped — about 6 % of its old conversion work — because the
   writer layer is `FRUA_PLANAR`, which the shared Falcon/TT binary does not
   define. Deliberately left (see the TT section of `docs/planar-plan.md`).

## 4. The 1.0 gate

`docs/release.md`: every release is a beta and a GitHub prerelease, and
**1.0.0 is reserved for the port being confirmed on real hardware across its
targets; do not spend it on features.** With the feature list closed, the next
cut is the 1.0.0-beta, and the five-machine refresh that follows it is the
confirmation that turns it into 1.0.0. One cut per testing round — see
`docs/release.md` for the sequence.
