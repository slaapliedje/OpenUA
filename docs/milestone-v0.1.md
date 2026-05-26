# Milestone v0.1 — interactive FRUA demo

Tagged at `v0.1-demo`. First release where `make run` shows real FRUA
resources on screen and accepts real mouse input.

## What the demo does

`make` builds `frua.prg` and (if `data/work/UnlimitedAdventures.rfork`
is present) packs `frua.rsc` from it. `make run` boots in Hatari:

1. Brings up the Falcon VIDEL display, attaches the QuickDraw shim to
   the back buffer, installs the IKBD-packet mouse driver.
2. Loads `frua.rsc` through the File Manager and hands the bytes to
   the Resource Manager.
3. Installs FRUA's `clut 129` 256-entry palette and FRUA's `FONT`
   `-27001` bitmap font.
4. Runs `ua_main(strtab_count, strtab_ptr)` — the FRUA application
   entry. The engine bootstrap (CODE 6 + 0x58a) runs to completion
   through every phase (core_init, master_init via toolbox_init +
   fc_init, phase-4/5 stubs, the no-op play loop while `jt315`
   returns 0, master_shutdown). `ua_main` returns 0.
5. Loads `WIND` 1 / 2 / 3 from the live archive, MoveWindows each
   to a different offset, ShowWindows in z-order, SelectWindows the
   topmost so it gets the active-stripe title-bar styling.
6. Loads `ALRT 200` (FRUA's "A disk error occurred!" alert), parses
   its DITL, opens a modal dialog with framed OK button and the
   static error text, waits for any key / click to dismiss.
7. Drops into an interactive event loop: mouse clicks raise windows
   to front (`FindWindow` + `SelectWindow`); title-bar drags follow
   the mouse via an XOR outline (`DragWindow`) and snap-move on
   release; keypress exits.

## Layers wired

### Platform HAL (`platform/`)

- VIDEL 256-colour back-buffer with mode switch, palette upload,
  chunky→planar present
- IKBD mouse driver: Supexec-installed `mousevec` decodes 3-byte
  packets into a screen-clamped position + button state
- BIOS keyboard pump via `Bconstat` / `Bconin` + `Kbshift`
- 60 Hz tick counter scaled from `_hz_200` via `Supexec`

### Toolbox shim (`compat/`)

- **Memory Manager** — `Ptr` / `Handle`, `BlockMove`, `PtrToHand`,
  `HandToHand`, `HandAndHand`, `PtrAndHand`
- **QuickDraw** — full primitive set (rects / lines / ovals /
  `CopyBits` / `ClipRect`), pen size / mode / pattern, RGB
  foreground / background with nearest-CLUT lookup, text family
  over both an embedded 8x8 fallback and the real Mac FONT loader
- **Window Manager** — full lifecycle, structure / content regions
  in global coords, title in a Handle, frame paint with active-stripe
  styling, `FindWindow`, `DragWindow` (XOR outline), `TrackGoAway`
- **Event Manager** — `EventRecord`, posted FIFO, `TickCount`,
  keyboard pump, mouse-edge synthesis for `mouseDown` / `mouseUp`,
  `updateEvt` synthesis, `activateEvt` synthesis on focus change,
  `WaitNextEvent`
- **Resource Manager** — FRSC archive reader (`(type, id)`,
  binary-search lookup), `OpenResFile` / `UseResFile` / `CurResFile`
  / `CloseResFile` / `HomeResFile` / `CreateResFile`
- **File Manager** — `FSOpen`, `FSClose`, `FSRead`, `FSWrite`,
  `GetEOF` / `SetEOF`, `GetFPos` / `SetFPos`, `Create`, `FSDelete`,
  `GetVol` / `SetVol` / `FlushVol`, `GetFInfo` / `SetFInfo` (all
  over GEMDOS file calls)
- **Dialog Manager** — `Alert` (loads ALRT + DITL, paints buttons +
  static text, modal event loop)
- **Toolbox startup** — `toolbox_init` runs the seven manager inits
  in Mac order; the engine's `master_init` calls it
- **Mac FONT loader** — `mac_font_load` parses the classic-Mac FONT
  format; `DrawChar` / `CharWidth` consult it when loaded

### Engine (`src/engine/`)

Lifted from the Mac CODE segments:
- `ua_main` (CODE 6 + 0x58a) runs end-to-end
- `core_init`, `master_init` / `master_shutdown`, the `fc` file-cache
  subsystem
- Allocator / string / RNG / error helpers
- Frontier from the play-loop body: `L07dc`, `L5124` (first-time init),
  `jt942` / `jt943` (loop-flag pair), `JT[399]` (memset), `jt174`,
  `jt76`, `jt480` (string-table setter), `jt398` (control-file probe),
  `jt918` (entry-only skeleton)

Unlifted callees ship as PROBE-instrumented stubs. `make ENGINE_PROBE=1`
emits the per-frame call sequence — `docs/engine-bring-up.md` tracks
each probe.

## What's missing

- The THINK C `DATA + DREL` replay — currently a hand-curated 1-entry
  strtab gets `ua_get_string(2)` returning "Heart" through the real
  STRS resource; every other index returns the empty fallback. The
  proper replay would unlock all string-driven engine branches.
- Most of CODE 6 / 12 / 15 / 20 — the play-loop dispatch chain,
  the new-game / select-design dialog body, the per-iteration
  drawing helpers
- Menu Manager — no menus yet
- Dialog Manager beyond `Alert` — no `ModalDialog`, no edit fields,
  no item hit-testing
- Sound Manager — no audio
- NFNT colour fonts, FOND family resolution
- Real persistent state (the engine reads / writes its own data
  files through the File Manager but no gameplay yet calls those
  paths)

## How to verify

```sh
make            # builds frua.prg + frua.rsc (skips rsrcpack if rfork absent)
make run        # boots in Hatari; click windows to raise, drag title bars,
                # press any key to dismiss the ALRT, then again to exit
make ENGINE_PROBE=1 run    # adds per-stub trace to the console
make test       # host-side pytest suite over tools/
```

The boot trace ends at `main: WIND 1/2/3 stacked` followed by
`main: ALRT 200 dismissed` (interactive) before the click loop.

## What's next

Likely candidates for the next milestone:

1. **DATA + DREL replay** — the multi-turn project that puts all of
   `ua_get_string` on real fork data. Unlocks the remaining string-
   comparison branches in lifted engine code.
2. **More CODE 6 / 12 lifts** — `l5888` (the JT[1] dispatch),
   `l67ca` / `l68f8` / `jt941` (L07dc's saved-game branch), `jt131`
   (jt918's UI kick).
3. **Menu Manager** — `NewMenuBar`, `AppendMenu`, `MenuSelect` so
   the engine's eventual menu code has somewhere to land.
4. **Sound Manager** — `SndPlay` over Falcon XBIOS `Dsound`; YM2149
   fallback on TT030.
