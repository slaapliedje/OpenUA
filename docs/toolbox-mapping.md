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
| Resource Manager    | `GetResource`, resource fork     | Reader over a converted flat resource archive (no fork)     | planned |
| Memory Manager      | `NewHandle`/`NewPtr`, `HLock`    | Handle table over GEMDOS `Malloc`/`Mxalloc`                 | planned |
| File Manager        | `FSOpen`/`FSRead`, HFS paths     | GEMDOS `Fopen`/`Fread`/`Fclose` + path translation          | planned |
| Sound Manager       | `SndPlay`, sound channels        | Falcon DMA sound via XBIOS; YM2149 fallback on TT030        | planned |
| Event Manager       | `GetNextEvent`, mouse/keyboard   | IKBD + BIOS `Bconin`/`Kbshift`; AES `evnt_multi` in tools   | planned |
| Menu/Window/Dialog  | Editor & tools UI                | GEM AES, or a custom widget set (TBD)                       | planned |
| TextEdit            | Editor text fields               | Custom, or AES — decide with the Dialog Manager approach    | planned |
| OSUtils / ToolUtils | Misc (time, fixed-point, etc.)   | Case by case; mostly trivial inline equivalents             | planned |

## Open questions

- **Resource fork:** Atari filesystems have no resource fork. A host-side tool
  (`tools/`) will extract the Mac resource fork into a flat archive the shim
  reads at runtime. Format TBD.
- **Editor UI:** the *Unlimited Adventures* design tools are dialog-heavy.
  Deciding between AES and a custom widget set affects Menu/Window/Dialog/
  TextEdit together — treat as one decision.
