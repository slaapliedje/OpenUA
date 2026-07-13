# Fan modules and "hacks" — what the port supports

Investigated 2026-07-13 against `http://frua.rosedragon.org/pc/` (the FRUA fan
archive — see [[frua-fan-module-test-corpus]] for how to reach it: **HTTP only**).

## ⚠️ There is no `.HAK` file in FRUA

Searched exhaustively: **all 27 module letter directories, plus `hacks/`,
`hacks/Archive/`, `misc/`, `uashell/`, `modules/patches/`, `modules/unfinished/`
— zero files with `hak` in the name**, and no `.hak` inside any module examined.
The archive's own `uashell/hackdocs.txt` states it plainly:

> "**UA Shell is a hack manager for SSI's Unlimited Adventures.**"

(`.hak` is a *Neverwinter Nights* container. It is easy to conflate — the concept
"module needs extra content installed" is genuinely the same, and FRUA does have
that. It just isn't a file format.)

## What a FRUA "hack" actually is

**Replaced base-game files.** A module that uses hacks *ships the replacement
files inside its own `.DSN` folder*. Module readmes advertise this in a
`*HACK INFO*` block, e.g. THE CURSE OF THE FIRE DRAGON:

```
New Character Icons:  YES     New Wall Art:   YES     New Font:  YES
New Backdrops:        YES     New Frame Art:  YES     New Items: YES
New Title Art:        YES     New Music:      YES     New CKIT Edits: YES
```

Its payload on disk: **197 `.TLB`** (art) + 93 `.DAT` (design) + **8 `.xmi`**
(XMIDI music) + **`.PAT`/`.AD`** (AdLib/GUS instrument banks) + **1 `.FON`**
(font) + 1 `.GLB` + `DIFF.TBL`.

Some *engine* hacks (the AD&D-2e rules hack, BUGFIX) patch **UA.EXE itself** —
those are DOS-executable patches and are inapplicable to a Mac-derived port by
construction. Only the *data* hacks are portable.

## Support matrix (measured, not assumed)

| Payload | Port | Evidence |
|---|---|---|
| Design data (`GAME001/GEO*/MONST*/STRG*.DAT`) | ✅ **works** | byte-identical DOS↔Mac; THE LOST SWORDS, GIANTS, CURSE all load |
| **Custom art** (`.TLB`, `.GLB`) — walls, backdrops, icons, title, frames | ✅ **works** | CURSE's DUNGEON 01 renders its custom **tree-corridor wall set + starry backdrop**, correct colours, 0 bus errors |
| Custom music (`.xmi` + `.PAT`/`.AD`) | ❌ **ignored** | DOS AdLib/XMIDI. The Mac release uses a **four-tone wavetable synth** ([[music-four-tone-synth]]); the engine never opens these names. |
| Custom font (`.FON`) | ❌ **ignored** | DOS font format; never opened |
| `DIFF.TBL` / CKIT edits | ❌ **ignored** | never opened |
| Engine hacks (UA.EXE patches) | n/a | DOS binary patches — inapplicable |

**Nothing crashes.** The unsupported payloads are simply never opened, so a
"hacked" module still loads and plays — it just falls back to the base game's
music and font. That is graceful degradation, not breakage.

## The one real gap: custom music

`.XMI` is the only payload a player would *notice* missing. It is **not a lift** —
the Mac release has no XMIDI path at all, so supporting it means either an
XMI→four-tone converter or a new playback subsystem. That is a feature decision,
not a decompilation task, and it should not be started without asking.

## Not fully checked

- **GIANTS' intro picture rendered with odd magenta/cyan tones.** CURSE's art is
  clean, so this is more likely that module's own palette than a decoder bug — but
  it has not been chased. If a custom-art module ever looks wrong, start here.
