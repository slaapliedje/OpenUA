# Running FRUA under BasiliskII (reference / capture)

A 68k Mac emulator running the real game is useful for two things:
checking the original's behaviour against the port, and (the longer goal)
capturing the runtime view-layout globals that would unblock a pixel-exact
`jt199` render — see `docs/TODO.md`. Everything here is host-local; nothing
copyrighted is committed (`data/` is git-ignored).

## What's set up

`~/.config/BasiliskII/prefs` already points at a working stack:

- ROM: `PERFORMA.ROM` (68k), `modelid 14` (Quadra), 68040 + FPU.
- Boot: `basilisk-40.img` (System 7.5.3) + `SYSTEM_7-5-3-RETAIL.ISO`.
- The three FRUA DiskCopy images (`Unlimited Adventures 1/2/3.image`).
- `extfs` shared folder: `…/Emulation/Apple/Macintosh68k/Shared`
  (read/write from both host and the emulated Mac — the file bridge).

## Automating the launch

- **Skip the launcher GUI:** `BasiliskII --nogui true` boots straight to
  the Mac (command-line override; leaves the GUI prefs untouched). A normal
  `BasiliskII` shows the Volumes/… launcher and waits for *Start*.
- **Auto-launch the game:** inside the Mac, drop an alias of the FRUA
  application into `System Folder ▸ Startup Items` on the boot disk. It
  then launches on every boot. (Persists in `basilisk-40.img`; do it once
  interactively, or inject it later with `hfsutils` — not yet installed.)
- The emulator renders on a real X display; drive it interactively there.
  (In this sandbox its SDL screen window isn't grabbable the way Hatari's
  is, so screenshot/clicks are done on the host.)

## Copy protection

FRUA's doc-check asks for a word by position: "page P, paragraph G,
word W" from the manual. Answer it with:

    tools/manual_lookup.py P G W            # prints just that word
    tools/manual_lookup.py P --show         # page mapping + paragraph count
    tools/manual_lookup.py P G W --context  # show the paragraph to verify

It reads the manual PDF's embedded text layer (`data/frua_man.pdf`, a 2-up
scan — two printed pages per landscape PDF page; no OCR needed) and crops
the correct half by region. If a page's paragraph/word counting is off
(headers, captions), use `--show` then `--pdf-page`/`--half` to adjust.

## Longer goal: capture the view-layout globals

The pixel-exact `jt199` path is blocked because the slot-layout globals
`g_a5_-12240..-12196` map off-screen with the static DATA values and no
disassembled code writes them (`docs/TODO.md`). Dumping those A5 offsets
from the live game at the moment it renders the 3D view — via a Mac-side
memory tool (e.g. MacsBug) writing to the shared folder — would give the
real coordinates and let `l5b42`'s placement be reconstructed.
