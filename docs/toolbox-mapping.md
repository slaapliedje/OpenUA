# Mac Toolbox → Atari mapping

Tracks how each Mac Toolbox manager used by FRUA is satisfied on the Atari
side. The `compat/` shim implements the Mac-facing API; the right-hand column
is what it is built on.

Status: `planned` · `wip` · `done` · `native` (migrated off the shim).

| Mac Toolbox manager | FRUA use                         | Atari implementation                                        | Status  |
|---------------------|----------------------------------|-------------------------------------------------------------|---------|
| QuickDraw           | All drawing, `CopyBits`, regions | Geometry core, the `GrafPort` / `CGrafPort` and Color QuickDraw types, `NewPixMap`, rectangular regions, `GetPort`/`SetPort`, the screen port over the display HAL back buffer (`qd_attach_screen`), the rect primitives (`EraseRect`, `PaintRect`, `FrameRect`), the line family (`MoveTo`, `LineTo`, `GetPen`, `PenSize`, `PenMode`, `PenPat`), the ovals (`PaintOval`, `FrameOval`), `CopyBits` (8-bit same-size `srcCopy`), `ClipRect`, the colour entries (`RGBForeColor`/`RGBBackColor` against a cached CLUT via `qd_set_palette`), and the text family (`TextFont`/`TextSize`/`TextFace`/`TextMode` state, `DrawChar`/`DrawString`/`CharWidth`/`StringWidth` over the sparse 8x8 fallback font in `compat/font_8x8.c`) — every primitive intersects `portRect ∩ visRgn ∩ clipRgn`, pen-using primitives honour `pnSize`, the pen pat-modes (`patCopy`/`patOr`/`patXor`/`patBic`) combine `fgColor` (and at `patCopy`, `bkColor`) with the destination pixel, the 8x8 pen pattern (`pnPat`) gates each pen pixel, the colour entries resolve to the nearest sum-of-squared-difference index, and text honours `srcCopy` / `srcOr` through the glyph bits, in `compat/quickdraw.c`; fill patterns, real NFNT fonts, scaling, and the remaining source transfer modes still to come | wip |
| Color/Palette Mgr   | 256-colour CLUT animation        | Shim caches the CLUT via `qd_set_palette` (forwarding to HAL `set_palette` → XBIOS `VsetRGB` Falcon / `EsetPalette` TT); Mac Palette Manager (`NewPalette`, `SetPalette`, ...) to follow | wip |
| Offscreen GWorlds   | Sprite/buffer composition        | 8-bit paletted offscreen surfaces in the shim               | planned |
| Resource Manager    | `GetResource`, resource fork     | FRSC-archive reader in `compat/resources.c` — `GetResource` / `GetIndResource` / `CountResources` / `SizeResource` / `ReleaseResource` / `ResError` over the `(type,id)` archive, plus the resource-file machinery (`OpenResFile` / `OpenRFPerm` / `UseResFile` / `CurResFile` / `CloseResFile` / `HomeResFile` / `CreateResFile`) over a 4-slot refnum table whose entries all alias the single in-memory archive. Real multi-archive support, file-backed `OpenResFile`, resource attributes, named lookups, and writing resources still to come; `rsrcpack` (the packer) still to build. | wip |
| Memory Manager      | `NewHandle`/`NewPtr`, `HLock`    | `NewPtr` / `NewHandle` over the C heap (`compat/macmemory.c`); handles get a stable master pointer (heap never relocates); plus `BlockMove` / `BlockMoveData` (memmove), `PtrToHand` / `HandToHand` / `HandAndHand` / `PtrAndHand` cross-allocation helpers. `Munger` and pointer-block resizing still to come | done |
| File Manager        | `FSOpen`/`FSRead`, HFS paths     | High-level data-fork API over GEMDOS in `compat/files.c` — `FSOpen` / `FSClose` / `FSRead` / `FSWrite` / `GetEOF` / `SetEOF` / `GetFPos` / `SetFPos` / `Create` / `FSDelete` / `GetFInfo` / `SetFInfo` / `GetVol` / `SetVol` / `FlushVol`. The GEMDOS file handle doubles as the Mac refNum; Mac Pascal paths collapse to their trailing filename; FInfo (type / creator / flags) round-trips through a small in-shim cache because GEMDOS has no Finder bytes. Resource-fork I/O (`OpenRF` / `CreateResFile`) stays in `compat/resources.c` — served by the flat `(type,id)` archive (ADR-0007). | wip |
| Sound Manager       | `SndPlay`, sound channels        | Falcon DMA sound via XBIOS; YM2149 fallback on TT030        | planned |
| Event Manager       | `GetNextEvent`, mouse/keyboard   | `EventRecord`, the masks / modifiers, a 16-slot posted FIFO, `TickCount` over `_hz_200`, `GetNextEvent` / `EventAvail` / `PostEvent` / `FlushEvents` / `WaitNextEvent`, keyboard via BIOS `Bconstat` / `Bconin` + `Kbshift`, mouse position / button via the IKBD-packet handler (a Supexec-installed `mousevec` trampoline in `platform/input.c` decoding the 3-byte packets into `g_mouse_x` / `g_mouse_y` / `g_mouse_btn`), `mouseDown` / `mouseUp` synthesised from button-state edges on each `GetNextEvent` pump (re-posted into the queue when the caller's mask drops them, so up/down pairs never get lost), and updateEvt synthesis from any window's non-empty `updateRgn` (`compat/events.c`); `activateEvt` and `autoKey` follow | wip |
| Window Manager      | Editor & play-UI windows         | Reimplemented in the shim — window record, list, lifecycle, geometry, the update mechanism, b&w / colour / resource-loaded windows, structure / content regions in global screen coords, the title in a Handle, the desktop-side frame drawing (1-pixel black outline, grey title bar with the title centred, close box on left) painted into the screen port on `ShowWindow` / `SelectWindow`, and the user-action plumbing (`FindWindow` over the window stack, `DragWindow` with an XOR outline + `MoveWindow` on release, `TrackGoAway` against the close box) — `DragWindow` / `TrackGoAway` spin on `Button()` / `GetMouse()` so they go live the moment the IKBD-packet driver promotes the stubs, in `compat/windows.c`; active/inactive title-bar styling and `GrowWindow` / `SizeWindow`'s grow box follow | wip |
| Menu / Dialog       | Editor & tools UI                | Reimplemented in the shim; Mac-style widgets drawn into the HAL surface (ADR-0006) | planned |
| TextEdit            | Editor text fields               | Reimplemented in the shim, alongside the Dialog Manager (ADR-0006) | planned |
| OSUtils / ToolUtils | Misc (time, fixed-point, etc.)   | Case by case; mostly trivial inline equivalents             | planned |

## Toolbox startup

`compat/toolbox.c` provides the seven Mac Toolbox manager init entry points
— `InitGraf`, `InitFonts`, `InitWindows`, `InitMenus`, `TEInit`,
`InitDialogs`, `FlushEvents` — and `toolbox_init()`, which calls them in the
standard Mac startup order. The engine wires this up via `src/engine/master.c`'s
`master_init`, which calls `toolbox_init()` in place of the lifted `JT[1144]`
prologue. `src/main.c` brings up the display HAL and the input HAL, then
hands off to `ua_main` (`src/engine/boot.c`); the engine takes over from
there.

## Decided

- **Resource fork** → flat `(type, id)` archive built by `tools/rsrcpack`,
  served by the Resource Manager shim. See ADR-0007 and `tools/README.md` for
  the archive format.
- **Editor UI** → Menu/Window/Dialog/Control/TextEdit reimplemented inside the
  shim, drawn into the HAL surface. See ADR-0006.
- **Phasing** → the play-UA-modules runtime is ported first; the design tools
  follow as a second phase. See ADR-0008.

## Still open

_None right now._ The Mac release has been unpacked (see
`docs/mac-release.md`); the application resource fork is extracted raw, so
`rsrcpack` consumes a raw resource fork and `tools/appledouble.py` handles
the AppleDouble unwrapping upstream.
