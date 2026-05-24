# Mac Toolbox → Atari mapping

Tracks how each Mac Toolbox manager used by FRUA is satisfied on the Atari
side. The `compat/` shim implements the Mac-facing API; the right-hand column
is what it is built on.

Status: `planned` · `wip` · `done` · `native` (migrated off the shim).

| Mac Toolbox manager | FRUA use                         | Atari implementation                                        | Status  |
|---------------------|----------------------------------|-------------------------------------------------------------|---------|
| QuickDraw           | All drawing, `CopyBits`, regions | Geometry core, the `GrafPort` / `CGrafPort` and Color QuickDraw types, `NewPixMap`, rectangular regions, `GetPort`/`SetPort`, the screen port over the display HAL back buffer (`qd_attach_screen`), the rect primitives (`EraseRect`, `PaintRect`, `FrameRect`), the line family (`MoveTo`, `LineTo`, `GetPen`, `PenSize`, `PenMode`), the ovals (`PaintOval`, `FrameOval`), `CopyBits` (8-bit same-size `srcCopy`), and `ClipRect` — every primitive intersects `portRect ∩ visRgn ∩ clipRgn`, pen-using primitives honour `pnSize`, and the pen pat-modes (`patCopy`/`patOr`/`patXor`/`patBic`) combine `fgColor` with the destination pixel, in `compat/quickdraw.c`; `PenPat`, patterns, text, scaling, and the source transfer modes still to come | wip |
| Color/Palette Mgr   | 256-colour CLUT animation        | HAL `set_palette` → XBIOS `VsetRGB` (Falcon) / `EsetPalette`| planned |
| Offscreen GWorlds   | Sprite/buffer composition        | 8-bit paletted offscreen surfaces in the shim               | planned |
| Resource Manager    | `GetResource`, resource fork     | FRSC-archive reader in `compat/resources.c` — `GetResource` etc. over the `(type,id)` archive; `rsrcpack` (the packer) still to build | wip |
| Memory Manager      | `NewHandle`/`NewPtr`, `HLock`    | `NewPtr` and `NewHandle` over the C heap (`compat/macmemory.c`); handles get a stable master pointer — the heap never relocates | done |
| File Manager        | `FSOpen`/`FSRead`, HFS paths     | GEMDOS `Fopen`/`Fread`/`Fclose` + path translation          | planned |
| Sound Manager       | `SndPlay`, sound channels        | Falcon DMA sound via XBIOS; YM2149 fallback on TT030        | planned |
| Event Manager       | `GetNextEvent`, mouse/keyboard   | IKBD + BIOS `Bconin`/`Kbshift`; AES `evnt_multi` in tools   | planned |
| Window Manager      | Editor & play-UI windows         | Reimplemented in the shim — window record, list, lifecycle, geometry, the update mechanism, b&w / colour / resource-loaded windows (`compat/windows.c`); frames & drawing await the HAL | wip |
| Menu / Dialog       | Editor & tools UI                | Reimplemented in the shim; Mac-style widgets drawn into the HAL surface (ADR-0006) | planned |
| TextEdit            | Editor text fields               | Reimplemented in the shim, alongside the Dialog Manager (ADR-0006) | planned |
| OSUtils / ToolUtils | Misc (time, fixed-point, etc.)   | Case by case; mostly trivial inline equivalents             | planned |

## Toolbox startup

`compat/toolbox.c` provides the seven Mac Toolbox manager init entry points
— `InitGraf`, `InitFonts`, `InitWindows`, `InitMenus`, `TEInit`,
`InitDialogs`, `FlushEvents` — and `toolbox_init()`, which calls them in the
standard Mac startup order. FRUA's `JT[1144]` (CODE 4) runs that sequence as
its prologue. These are first-cut stubs: the API surface and the startup
order, ahead of the managers themselves.

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
