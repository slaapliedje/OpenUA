# Mac Toolbox → Atari mapping

Tracks how each Mac Toolbox manager used by FRUA is satisfied on the Atari
side. The `compat/` shim implements the Mac-facing API; the right-hand column
is what it is built on.

Status: `planned` · `wip` · `done` · `native` (migrated off the shim).

| Mac Toolbox manager | FRUA use                         | Atari implementation                                        | Status  |
|---------------------|----------------------------------|-------------------------------------------------------------|---------|
| QuickDraw           | All drawing, `CopyBits`, regions | Software blitter over the display HAL 8-bit surface         | planned |
| Color/Palette Mgr   | 256-colour CLUT animation        | HAL `set_palette` → XBIOS `VsetRGB` (Falcon) / `EsetPalette`| planned |
| Offscreen GWorlds   | Sprite/buffer composition        | 8-bit paletted offscreen surfaces in the shim               | planned |
| Resource Manager    | `GetResource`, resource fork     | Reader over a flat `(type,id)` archive built by `tools/rsrcpack` (ADR-0007) | planned |
| Memory Manager      | `NewHandle`/`NewPtr`, `HLock`    | Handle table over GEMDOS `Malloc`/`Mxalloc`                 | planned |
| File Manager        | `FSOpen`/`FSRead`, HFS paths     | GEMDOS `Fopen`/`Fread`/`Fclose` + path translation          | planned |
| Sound Manager       | `SndPlay`, sound channels        | Falcon DMA sound via XBIOS; YM2149 fallback on TT030        | planned |
| Event Manager       | `GetNextEvent`, mouse/keyboard   | IKBD + BIOS `Bconin`/`Kbshift`; AES `evnt_multi` in tools   | planned |
| Menu/Window/Dialog  | Editor & tools UI                | Reimplemented in the shim; Mac-style widgets drawn into the HAL surface (ADR-0006) | planned |
| TextEdit            | Editor text fields               | Reimplemented in the shim, alongside the Dialog Manager (ADR-0006) | planned |
| OSUtils / ToolUtils | Misc (time, fixed-point, etc.)   | Case by case; mostly trivial inline equivalents             | planned |

## Decided

- **Resource fork** → flat `(type, id)` archive built by `tools/rsrcpack`,
  served by the Resource Manager shim. See ADR-0007 and `tools/README.md` for
  the archive format.
- **Editor UI** → Menu/Window/Dialog/Control/TextEdit reimplemented inside the
  shim, drawn into the HAL surface. See ADR-0006.

## Still open

- The exact Mac-fork input format(s) `rsrcpack` must accept (MacBinary,
  AppleDouble, raw resource fork, HFS disk image) — depends on how the Mac
  release is supplied.
- Phasing: bring up the play-UA-modules runtime before the design tools, or
  both together. (Scope itself — the full package — is unchanged.)
