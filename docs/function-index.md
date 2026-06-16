# Function index (auto-generated)

Regenerate with `python3 tools/function_index.py`. **Grep this before writing a new function** — most of what you need is already lifted. `jt` = jump-table lift, `l` = CODE-local lift, `port` = port-side stand-in (re-implementation risk), `shim` = Toolbox/HAL.

Totals: jt 868, l 494, other 435, port 48, shim 37, **1882 total**.

| Function | Origin | Kind | Purpose | File:line |
|---|---|---|---|---|
| `Alert` | — | other | — | dialogs.c:425 |
| `AppendMenu` | — | other | — | menus.c:528 |
| `BeginUpdate` | — | other | BeginUpdate — begin handling an update event for `w`: narrow the port's visRgn to the area that needs redrawing, and clear the update region | windows.c:670 |
| `BlockMove` | — | other | block copies + cross-allocation helpers | macmemory.c:157 |
| `BlockMoveData` | — | other | — | macmemory.c:164 |
| `Button` | — | other | — | events.c:219 |
| `CharWidth` | — | other | CharWidth / StringWidth — report the cell width | quickdraw.c:1503 |
| `ClearMenuBar` | — | other | — | menus.c:635 |
| `ClipRect` | — | other | ClipRect — set the current port's clipRgn to the rectangle `r` | quickdraw.c:951 |
| `CloseResFile` | — | other | — | resources.c:266 |
| `CloseWindow` | — | other | CloseWindow — remove the window from the screen and dispose of its regions and any port-owned resources, but leave the WindowRecord itself to the caller | windows.c:450 |
| `CopyBits` | — | other | CopyBits — blit a rectangular region of 8-bit pixels | quickdraw.c:853 |
| `CountMItems` | — | other | — | menus.c:550 |
| `CountResources` | — | other | — | resources.c:154 |
| `Create` | — | other | — | files.c:271 |
| `CreateResFile` | — | other | — | resources.c:286 |
| `CurResFile` | — | other | — | resources.c:260 |
| `DeleteMenu` | — | other | — | menus.c:618 |
| `DialogSelect` | — | other | — | dialogs.c:771 |
| `DisableItem` | — | other | — | menus.c:579 |
| `DisposeControl` | — | other | — | controls.c:170 |
| `DisposeDialog` | — | other | — | dialogs.c:738 |
| `DisposeHandle` | — | other | — | macmemory.c:100 |
| `DisposeMenu` | — | other | — | menus.c:514 |
| `DisposePixMap` | — | other | DisposePixMap — free a NewPixMap PixMap and its colour table. | quickdraw.c:167 |
| `DisposePtr` | — | other | — | macmemory.c:31 |
| `DisposeRgn` | — | other | — | quickdraw.c:193 |
| `DisposeWindow` | — | other | — | windows.c:471 |
| `DragWindow` | — | other | — | windows.c:744 |
| `Draw1Control` | — | other | — | controls.c:667 |
| `DrawChar` | — | other | DrawChar — render one glyph at pnLoc with the baseline at pnLoc.v, then advance the pen by the cell width | quickdraw.c:1379 |
| `DrawControls` | — | other | — | controls.c:693 |
| `DrawDialog` | — | other | — | dialogs.c:758 |
| `DrawMenuBar` | — | other | — | menus.c:656 |
| `DrawString` | — | other | DrawString — Pascal string: str[0] is the length, str[1..len] the bytes | quickdraw.c:1481 |
| `EmptyRect` | — | other | — | quickdraw.c:61 |
| `EmptyRgn` | — | other | — | quickdraw.c:210 |
| `EnableItem` | — | other | — | menus.c:572 |
| `EndUpdate` | — | other | EndUpdate — finish an update: restore visRgn to the full content. | windows.c:682 |
| `EqualRect` | — | other | — | quickdraw.c:90 |
| `EraseRect` | — | other | — | quickdraw.c:501 |
| `EventAvail` | — | other | — | events.c:261 |
| `ExitToShell` | JT[415] | other | ExitToShell — Process Manager process termination (the _ExitToShell trap; FRUA reaches it through JT[415], e.g | toolbox.c:88 |
| `FSClose` | — | other | — | files.c:174 |
| `FSDelete` | — | other | — | files.c:295 |
| `FSOpen` | — | other | the API | files.c:156 |
| `FSRead` | — | other | — | files.c:180 |
| `FSWrite` | — | other | — | files.c:199 |
| `FindControl` | — | other | — | controls.c:756 |
| `FindWindow` | — | other | user-action plumbing: FindWindow, DragWindow, TrackGoAway | windows.c:690 |
| `FlushEvents` | — | other | FlushEvents — drop every queued event matching `whichMask` | events.c:311 |
| `FlushVol` | — | other | — | files.c:376 |
| `FrameOval` | — | other | — | quickdraw.c:767 |
| `FrameRect` | — | other | FrameRect — outline `r` in the port's foreground colour | quickdraw.c:612 |
| `FreeMem` | — | other | The Mac Memory Manager's _FreeMem reports total free heap space; the Atari stand-in returns the largest free block (GEMDOS Malloc(-1)), which is the more useful figure for sizing a single large alloca | macmemory.c:47 |
| `FrontWindow` | — | other | — | windows.c:603 |
| `GetControlMaximum` | — | other | — | controls.c:266 |
| `GetControlMinimum` | — | other | — | controls.c:252 |
| `GetControlReference` | — | other | — | controls.c:309 |
| `GetControlTitle` | — | other | — | controls.c:291 |
| `GetControlValue` | — | other | — | controls.c:236 |
| `GetCursor` | — | other | — | quickdraw.c:1172 |
| `GetDialogItem` | — | other | — | dialogs.c:919 |
| `GetDialogItemText` | — | other | — | dialogs.c:952 |
| `GetEOF` | — | other | — | files.c:214 |
| `GetFInfo` | — | other | — | files.c:314 |
| `GetFPos` | — | other | — | files.c:241 |
| `GetHandleSize` | — | other | — | macmemory.c:108 |
| `GetIndResource` | — | other | — | resources.c:138 |
| `GetMenu` | — | other | — | menus.c:462 |
| `GetMenuHandle` | — | other | — | menus.c:645 |
| `GetMenuItemText` | — | other | — | menus.c:557 |
| `GetMouse` | — | other | — | events.c:224 |
| `GetNewCWindow` | — | other | As GetNewWindow, but the window's port slot is a colour CGrafPort. | windows.c:438 |
| `GetNewControl` | — | other | — | controls.c:132 |
| `GetNewDialog` | — | other | — | dialogs.c:697 |
| `GetNewWindow` | — | other | Create a window from the 'WIND' resource with id `windowID`. | windows.c:432 |
| `GetNextEvent` | — | other | — | events.c:230 |
| `GetPen` | — | other | GetPen — read the pen location of the current port. | quickdraw.c:645 |
| `GetPort` | — | other | static GrafPtr g_thePort; QuickDraw's current drawing port | quickdraw.c:116 |
| `GetResource` | — | other | — | resources.c:122 |
| `GetVol` | — | other | GetVol / SetVol — the engine reads / sets the default working directory | files.c:360 |
| `HGetState` | — | other | — | macmemory.c:144 |
| `HLock` | — | other | — | macmemory.c:132 |
| `HSetState` | — | other | — | macmemory.c:149 |
| `HUnlock` | — | other | — | macmemory.c:138 |
| `HandAndHand` | — | other | — | macmemory.c:205 |
| `HandToHand` | — | other | — | macmemory.c:187 |
| `HideControl` | — | other | — | controls.c:205 |
| `HideCursor` | — | other | — | quickdraw.c:1150 |
| `HideWindow` | — | other | — | windows.c:509 |
| `HiliteControl` | — | other | — | controls.c:214 |
| `HiliteMenu` | — | other | — | menus.c:701 |
| `HomeResFile` | — | other | — | resources.c:276 |
| `InitCursor` | — | other | static unsigned char g_cursor_save[16 * 16]; pixels under the cursor | quickdraw.c:1135 |
| `InitDialogs` | — | other | InitDialogs — Dialog Manager startup | toolbox.c:59 |
| `InitFonts` | — | other | InitFonts — Font Manager startup. | toolbox.c:35 |
| `InitGraf` | — | other | InitGraf — QuickDraw startup | toolbox.c:29 |
| `InitMenus` | — | other | InitMenus — Menu Manager startup | toolbox.c:45 |
| `InitWindows` | — | other | InitWindows — Window Manager startup. | toolbox.c:40 |
| `InsertMenu` | — | other | — | menus.c:586 |
| `InsetRect` | — | other | — | quickdraw.c:53 |
| `InvalRect` | — | other | InvalRect — add `r` to the update region of the window whose port is current, marking that area for redraw | windows.c:617 |
| `KillControls` | — | other | — | controls.c:183 |
| `LineTo` | — | other | LineTo — draw a one-pixel line from the current pen to (h, v) in the port's foreground colour, then move the pen to (h, v) | quickdraw.c:663 |
| `MemError` | — | other | — | macmemory.c:37 |
| `MenuKey` | — | other | — | menus.c:809 |
| `MenuSelect` | — | other | — | menus.c:729 |
| `ModalDialog` | — | other | — | dialogs.c:889 |
| `MoveTo` | — | other | MoveTo — set the pen location in the current port. | quickdraw.c:635 |
| `MoveWindow` | — | other | MoveWindow — move the window so its content top-left is at (h, v), and bring it to the front when `front` is set | windows.c:580 |
| `NewCWindow` | — | other | — | windows.c:372 |
| `NewControl` | — | other | creation | controls.c:96 |
| `NewDialog` | — | other | — | dialogs.c:651 |
| `NewHandle` | — | other | unsigned char state; Mac handle-state flags | macmemory.c:71 |
| `NewHandleClear` | — | other | — | macmemory.c:91 |
| `NewMenu` | — | other | — | menus.c:447 |
| `NewPixMap` | — | other | NewPixMap — allocate and initialise a PixMap | quickdraw.c:137 |
| `NewPtr` | — | other | static OSErr g_mem_error; result of the most recent call | macmemory.c:24 |
| `NewRgn` | — | other | regions (rectangular) --- The shim's regions are rectangular — a region is its bounding box | quickdraw.c:182 |
| `NewWindow` | — | other | — | windows.c:363 |
| `ObscureCursor` | — | other | — | quickdraw.c:1161 |
| `OffsetRect` | — | other | — | quickdraw.c:45 |
| `OpenRFPerm` | — | other | — | resources.c:242 |
| `OpenResFile` | — | other | — | resources.c:223 |
| `PaintOval` | — | other | — | quickdraw.c:726 |
| `PaintRect` | — | other | — | quickdraw.c:510 |
| `PenMode` | — | other | PenMode — set the current port's pen transfer mode | quickdraw.c:984 |
| `PenPat` | — | other | PenPat — set the current port's 8x8 one-bit pen pattern | quickdraw.c:1001 |
| `PenSize` | — | other | PenSize — set the current port's pen rectangle to (h, v) | quickdraw.c:966 |
| `PostEvent` | — | other | — | events.c:280 |
| `Pt2Rect` | — | other | The smallest rectangle spanned by two points. | quickdraw.c:104 |
| `PtInRect` | — | other | A point is inside if left <= h < right and top <= v < bottom. | quickdraw.c:97 |
| `PtrAndHand` | — | other | — | macmemory.c:221 |
| `PtrToHand` | — | other | — | macmemory.c:171 |
| `RGBBackColor` | — | other | — | quickdraw.c:1350 |
| `RGBForeColor` | — | other | — | quickdraw.c:1337 |
| `RectRgn` | — | other | — | quickdraw.c:204 |
| `ReleaseResource` | — | other | — | resources.c:172 |
| `ResError` | — | other | — | resources.c:188 |
| `SectRect` | — | other | Intersection | quickdraw.c:67 |
| `SelectWindow` | — | other | — | windows.c:522 |
| `SetControlMaximum` | — | other | — | controls.c:257 |
| `SetControlMinimum` | — | other | — | controls.c:243 |
| `SetControlReference` | — | other | — | controls.c:302 |
| `SetControlTitle` | — | other | — | controls.c:271 |
| `SetControlValue` | — | other | — | controls.c:223 |
| `SetCursor` | — | other | — | quickdraw.c:1142 |
| `SetDialogItemText` | — | other | — | dialogs.c:941 |
| `SetEOF` | — | other | — | files.c:231 |
| `SetEmptyRgn` | — | other | — | quickdraw.c:198 |
| `SetFInfo` | — | other | — | files.c:341 |
| `SetFPos` | — | other | — | files.c:254 |
| `SetHandleSize` | — | other | — | macmemory.c:113 |
| `SetPort` | — | other | — | quickdraw.c:121 |
| `SetPt` | — | other | #include "input.h" plat_mouse_pos — software cursor | quickdraw.c:31 |
| `SetRect` | — | other | — | quickdraw.c:37 |
| `SetVol` | — | other | — | files.c:369 |
| `ShowControl` | — | other | accessors | controls.c:195 |
| `ShowCursor` | — | other | — | quickdraw.c:1155 |
| `ShowWindow` | — | other | — | windows.c:495 |
| `SizeResource` | — | other | — | resources.c:166 |
| `SizeWindow` | — | other | SizeWindow — resize the window's content to width x height | windows.c:553 |
| `SndDisposeChannel` | — | other | — | sound.c:168 |
| `SndDoCommand` | — | other | — | sound.c:197 |
| `SndDoImmediate` | — | other | — | sound.c:207 |
| `SndNewChannel` | — | other | — | sound.c:145 |
| `SndPlay` | — | other | — | sound.c:182 |
| `SndSoundManagerVersion` | — | other | — | sound.c:216 |
| `StringWidth` | — | other | — | quickdraw.c:1511 |
| `SysBeep` | — | other | — | sound.c:227 |
| `TEActivate` | — | other | — | textedit.c:258 |
| `TEClick` | — | other | — | textedit.c:228 |
| `TEDeactivate` | — | other | — | textedit.c:267 |
| `TEDispose` | — | other | — | textedit.c:80 |
| `TEGetText` | — | other | — | textedit.c:114 |
| `TEIdle` | — | other | — | textedit.c:323 |
| `TEInit` | — | other | TEInit — TextEdit startup. | toolbox.c:51 |
| `TEKey` | — | other | Insert printable ASCII / handle backspace, arrows. | textedit.c:141 |
| `TENew` | — | other | — | textedit.c:54 |
| `TESetSelect` | — | other | — | textedit.c:120 |
| `TESetText` | — | other | — | textedit.c:89 |
| `TEUpdate` | — | other | — | textedit.c:275 |
| `TestControl` | — | other | hit-testing | controls.c:711 |
| `TextFace` | — | other | — | quickdraw.c:1365 |
| `TextFont` | — | other | text drawing | quickdraw.c:1364 |
| `TextMode` | — | other | — | quickdraw.c:1366 |
| `TextSize` | — | other | — | quickdraw.c:1367 |
| `TickCount` | — | other | public API | events.c:214 |
| `TrackControl` | — | other | — | controls.c:887 |
| `TrackGoAway` | — | other | — | windows.c:822 |
| `UnionRect` | — | other | The smallest rectangle enclosing both sources. | quickdraw.c:82 |
| `UpdateControls` | — | other | — | controls.c:703 |
| `UseResFile` | — | other | — | resources.c:249 |
| `ValidRect` | — | other | ValidRect — remove `r` from the update region of the window whose port is current, the inverse of InvalRect | windows.c:646 |
| `WaitNextEvent` | — | other | WaitNextEvent — like GetNextEvent, but spins for up to `sleep` ticks waiting for an event | events.c:336 |
| `alloc_refnum` | — | other | static short g_current_refnum; 0 = none | resources.c:210 |
| `aux_of` | — | other | TEHandle *tes; one slot per item; NULL OK | dialogs.c:65 |
| `bar_hit` | — | other | Hit-test screen point against the bar; returns bar index or -1. | menus.c:178 |
| `bar_left_x` | — | other | x position of the left edge of the menu at bar index `i`. | menus.c:163 |
| `be16` | — | other | Read a big-endian 16-/32-bit field. | resources.c:33 |
| `be32` | — | other | — | resources.c:38 |
| `blit_glyph_1bpp` | — | other | blit_glyph_1bpp — from-scratch GLIB glyph rasterizer (the option-B blit from docs/decompilation.md) | boot.c:8712 |
| `boot_a5_seed_defaults` | — | other | Forward — jt127 (design-data loader) lifts further down; the probe self-test below calls it. | boot.c:8329 |
| `bp_blit` | L2d4e | other | bp_blit — the 1:1 clipped tile blit, i.e | boot.c:9986 |
| `bp_blit_andnot` | JT[1184] | other | bp_blit_andnot — lift of JT[1184] (CODE 4 + 0xd7c), the AND-NOT / clear primitive: like bp_blit_or but the source word is inverted (notw), windowed, inverted again (notl), and AND'd into the dest (*de | boot.c:9916 |
| `bp_blit_or` | — | other | #define BP_ROWS 480 covers the shim surface height | boot.c:9872 |
| `bp_present` | — | other | bp_present — expand the 1bpp bit-packed page to the 8-bit shim buffer: each set bit -> fg, clear -> bg. | boot.c:9953 |
| `cell_backdrop_id` | — | other | cell_backdrop_id — the BACK.CTL backdrop for the party's current cell | boot.c:9509 |
| `cell_edge` | — | other | cell_edge — the edge byte of cell (x,y) in direction `f` (0 if open; off-map returns a standard wall code so boundaries are textured) | boot.c:8968 |
| `cg_add_character` | — | port | Add Character — page the benched pool characters with Up/Down; Return brings the highlighted one into the active party (sets CHAR_INPARTY), up to CG_PARTY_MAX slots | boot.c:47564 |
| `cg_allowed_aligns` | — | port | 0x049, Ranger: any Good | boot.c:21459 |
| `cg_allowed_classes` | — | port | Build the allowed-class index list for `race`; returns the count. | boot.c:21376 |
| `cg_build_record` | — | port | Build a character record from the finished pick state and append it to the roster (g_a5_-27928 linked list, next ptr at +0) | boot.c:21531 |
| `cg_char_fn` | — | port | Pascal filename "CHARnnnn.CHR" for pool slot 0..15. | boot.c:15108 |
| `cg_char_sheet` | L29ae | port | L29ae tail / L2da6 review loop — the faithful character-sheet driver: paint the sheet (L1276=jt886), roll the abilities (L24d2), then loop on the action bar | boot.c:29428 |
| `cg_collect_addable` | — | port | Collect benched pool characters (CHAR_INPARTY == 0) into `out` — the Add picker's candidates. | boot.c:47286 |
| `cg_collect_party` | — | port | Collect the active party (linked list, .next at +0) into `out`; the cap is `max` | boot.c:47263 |
| `cg_collect_pool` | — | port | Collect every pool character into `out` (Delete picker). | boot.c:47276 |
| `cg_delete_character` | — | port | Delete Character — page the whole pool with Up/Down; Return picks the highlighted character and asks Y/N, then erases it from the pool (and so the party) permanently and persists. | boot.c:47658 |
| `cg_draw` | L3666 | port | Draw the char-gen screen from the pick state: each region shows its current choice in cyan once reached, the rest light grey; the class and alignment lists show only their gated options | boot.c:21485 |
| `cg_draw_sheet` | — | port | — | boot.c:47371 |
| `cg_message` | — | port | A one/two-line notice on the shared chrome; waits for any key. | boot.c:47296 |
| `cg_modify_sheet` | — | port | Modify Character — page the party (Up/Down); N renames, R re-rolls the ability scores (race/class-legal, re-deriving AC from the new DEX — HP is earned over levels, so it's left intact) | boot.c:47511 |
| `cg_party_relink` | — | port | Rebuild the active-party list (g_a5_-27928) from the in-party pool records, in pool order — the roster grid (l02dc) and the party screens walk this list. | boot.c:15122 |
| `cg_party_setforth_screen` | — | port | "The party sets forth" — list the assembled party on the shared chrome before descending, so it's clear which characters are adventuring | boot.c:47725 |
| `cg_party_size` | — | port | — | boot.c:15140 |
| `cg_remove_from_party` | — | port | Remove Character — page the active party with Up/Down; Return benches the highlighted member (clears CHAR_INPARTY — it stays in the pool, addable again later) | boot.c:47616 |
| `cg_rename` | — | port | Inline name editor for Modify — types into the record's name (+96) live (the gold sheet name updates as you go); Return commits + persists, Esc restores the original. | boot.c:47466 |
| `cg_roll_stats` | — | port | Roll the six ability scores: 3d6 each (the FRUA LCG) + the race adjustment, clamped 3..18, and re-rolled per stat until the chosen class's minimum is met (so the character is class-legal — the game gu | boot.c:21421 |
| `cg_sheet_modal` | L23fa | port | L23fa (CODE 17 + 0x23fa) — the char-sheet action bar: four shape-1 buttons Done / Reroll Stats / Modify / Exit (install order = jt453 index 0..3), installed via JT[452] and run modally by JT[453] | boot.c:29405 |
| `cg_train_screen` | — | port | — | boot.c:21743 |
| `cg_train_threshold` | — | port | Train Character — the port's leveling screen (case 0) | boot.c:21738 |
| `cg_view_sheet` | — | port | View Character — read-only sheet; Up/Down page the party, Esc/Return back out to the Training Hall. | boot.c:47428 |
| `clut_nearest` | — | other | clut_nearest — index of the clut-129 entry closest to an 8-bit RGB. | boot.c:14863 |
| `color_mode_init` | JT[1200] | other | color_mode_init — CODE 4 jt1144 @0x44b8 + @0x47b4, lifted faithfully | boot.c:562 |
| `copy_dta_name` | JT[589] | other | directory enumeration --- The Mac File Manager walks a folder with indexed PBGetCatInfo; the faithful roster scanner (JT[589]) needs that to list saved-character files | files.c:394 |
| `core_alloc_table_arena` | — | other | — | core.c:124 |
| `core_alloc_table_far` | L30cc | other | core_alloc_table_far / core_alloc_table_arena — the folded L30cc / L30f4 / L311c / L3154 / L317c / L31a4 | core.c:117 |
| `core_init` | L4cc0 | other | core_init — L4cc0 | core.c:138 |
| `count_all_visitor` | — | other | — | dialogs.c:486 |
| `cpstr_copy` | — | other | Pascal-string helpers (shared with menus.c). | controls.c:38 |
| `ctile_blit` | L2d4e | other | ctile_blit — decode and blit one of FRUA's COLOUR tiles (the mode-1 "C"-file art: CBODY paperdolls, COMSPR combat sprites, CPIC creatures) straight to the 8-bit screen | boot.c:14832 |
| `current_mouse` | — | other | — | events.c:46 |
| `cursor_composite` | — | other | — | quickdraw.c:1235 |
| `cursor_restore` | — | other | — | quickdraw.c:1286 |
| `cw_blit_piece` | — | other | cw_blit_piece — blit one full-set piece `idx` (1-based) at screen (top,left), 1:1, into the faithful blit surface | boot.c:10186 |
| `cw_finalize` | — | other | cw_finalize — after the slots are loaded, push every loaded slot's band into the clut, set the ceiling/floor fallback colours, build the per-slot per-depth darken remap (nearest darker colour within t | boot.c:9332 |
| `cw_load_slot` | — | other | cw_load_slot — load wall-set (`file`,`set`) into slot `slot`: copy its plain-wall piece (item 8) and its 32-colour palette band into the slot's storage | boot.c:9251 |
| `cw_seed_ui_band` | — | other | Seed pal[0..31] with the live UI palette (clut 0..3, 6..31) so a full-CLUT wall install doesn't wipe the chrome/text band | boot.c:9004 |
| `cw_shade` | — | other | cw_shade — sample slot `slot`, facet `facet` at (col,row) and return the depth-shaded clut index, or CW_CLEAR if the pixel is transparent (the global key 255 or the per-set magenta key) | boot.c:9154 |
| `cw_view_clip` | — | other | Set the faithful-view clip rect (and the surface dims it blits into). | boot.c:9102 |
| `cw_wallfile_load` | — | other | cw_wallfile_load — return the GLIB base of 8X8D{C,B}.CTL `file` (0=DC, 1=DB) | boot.c:2289 |
| `db_base` | CODE 5 + 0x2850 | other | db_base — CODE 5 + 0x2850 (jump-table entry 1004): the DB arena base. | core.c:71 |
| `db_init` | CODE 5 + 0x2822 | other | db_init — CODE 5 + 0x2822 (jump-table entry 1002, "DBInit") | core.c:62 |
| `dbg_dump_view` | — | other | — | boot.c:10684 |
| `decode_glib_t7` | JT[1195] | other | Decode a "type 7" GLIB piece (metric[7] & 0x0f == 7): the chunky transparency RLE that the faithful blit hands to JT[1195] (CODE 4 + 0xc08) one strip at a time | boot.c:18758 |
| `dialog_get_edit_text` | — | other | per-dialog edit-text helpers | dialogs.c:961 |
| `dialog_set_edit_text` | — | other | — | dialogs.c:983 |
| `ditl_hit_button` | — | other | — | dialogs.c:348 |
| `ditl_hit_edit` | — | other | — | dialogs.c:384 |
| `dlg_aux_dispose` | — | other | — | dialogs.c:628 |
| `dlg_aux_make` | — | other | — | dialogs.c:578 |
| `draw_arrow_glyph` | — | other | — | controls.c:443 |
| `draw_bevel` | L148a | other | Draw a 1px 3D bevel on the pixel rect [x1,x2) x [y1,y2) | boot.c:5807 |
| `draw_checkbox` | — | other | — | controls.c:575 |
| `draw_front_face` | — | other | draw_front_face — draw wall-set slot `slot`'s plain wall (facet 0) scaled into the rect [xl,xr]x[yt,yb], then overlay its facet decoration at the facet's bearing (transparent pixels show the wall) | boot.c:9682 |
| `draw_map_tiles` | JT[202] | other | draw_map_tiles — render the currently-loaded design-state map as TOPVIEW automap tiles (shared with port_render_geo_tiles, edge order [N,E,S,W] per JT[202]) | boot.c:9804 |
| `draw_party` | — | other | draw_party — a marker at the party cell with a nub pointing in the facing direction (CLUT index 3). | boot.c:8952 |
| `draw_party_panel` | — | other | Draw the active-party status panel down the right of the dungeon view (the ~220px viewport leaves x>=226 free): a gold header, then each member's name (white) and HP/AC | boot.c:13939 |
| `draw_plate` | — | other | A bevelled plate over the backdrop: flat warm-grey face + a 1px bevel | boot.c:19348 |
| `draw_pulldown` | — | other | Paint the pull-down for menu at bar slot `i` with item `hi_item` (1-based, 0 = none) inverted. | menus.c:282 |
| `draw_pushbutton` | — | other | — | controls.c:541 |
| `draw_radiobutton` | — | other | — | controls.c:622 |
| `draw_scrollbar` | — | other | — | controls.c:497 |
| `draw_title` | — | other | Paint one title at bar slot `i` | menus.c:201 |
| `draw_title_panel` | — | other | — | boot.c:19531 |
| `dsp_detect` | — | shim | — | display_videl.c:365 |
| `dungeon_view_setup` | — | other | dungeon_view_setup — one-time bring-up for the first-person view | boot.c:11271 |
| `dv_app` | — | other | dbg_dump_view — one-shot render-time state dump to C:\VIEWDIAG.TXT | boot.c:10668 |
| `edge_color` | — | other | Decode a tile edge byte to a CLUT index | boot.c:8598 |
| `encounter_check` | — | other | encounter_check — after a dungeon step, read the new cell's monster zone (high 6 bits of the cell's 6th byte) and, if non-zero, roll for a random encounter | boot.c:14265 |
| `encounter_screen` | — | other | Fill the screen black and set the white/gold HUD clut for encounter text. | boot.c:14061 |
| `event_matches` | — | other | — | events.c:99 |
| `event_post_full` | — | other | — | events.c:286 |
| `facet_for_edge` | — | other | facet_for_edge — the decoration facet (0-4) for a map edge byte: the low nibble's position within its group | boot.c:9565 |
| `fc_cache_audit` | — | other | — | boot.c:46008 |
| `fc_dump` | JT[458] | other | JT[458] (CODE 3+0x846) — FCDump: the Mac dumps the file-group table to the screen when FAR memory is exhausted (via JT[1089] formatted print + JT[1153] page select) | boot.c:44500 |
| `fca_check` | — | other | — | boot.c:45998 |
| `files_find_first` | — | other | — | files.c:417 |
| `files_find_first_attr` | — | other | — | files.c:406 |
| `files_find_is_dir` | — | other | Whether the entry the last find_first/find_next landed on is a directory | files.c:424 |
| `files_find_next` | — | other | — | files.c:429 |
| `fill_backdrop` | — | other | Tile the GEN.CTL stone field across the screen as the backdrop | boot.c:19328 |
| `fill_common` | — | other | — | events.c:56 |
| `fill_wall_trap_c` | — | other | fill_wall_trap_c — perspective-correct trapezoid for one side wall: the plain wall (facet 0) tiled in a 32x32 window (offset `voff` down) of slot `slot`, depth-shaded | boot.c:9189 |
| `find_item` | — | other | — | dialogs.c:407 |
| `find_te` | — | other | — | dialogs.c:70 |
| `finfo_alloc` | — | other | — | files.c:136 |
| `finfo_find` | — | other | — | files.c:127 |
| `first_edit_item` | — | other | — | dialogs.c:79 |
| `folder_is_dsn` | — | other | True if the Pascal-string range [start, end) ends in ".DSN". | files.c:63 |
| `gemdos_err` | — | other | Map a GEMDOS error code (negative) to a Mac OSErr | files.c:42 |
| `geo_hdr_word` | — | other | port_render_geo_contact — load every GEOnnn (1..40) in turn and draw each as a small thumbnail in a grid, to confirm the 24x24 / 6-byte tile layout holds across a whole design | boot.c:14807 |
| `get_new_window` | — | other | Shared body of GetNewWindow / GetNewCWindow: load the 'WIND' resource `windowID` and parse it — boundsRect (8 bytes), procID (a word), the visible and goAway flags (a 2-byte field each, nonzero means | windows.c:400 |
| `glib_lb_init` | L35e2 | other | L35e2 (CODE 5+0x35e2) — _LBInit: clear the registry and register the one built-in signature, 'GLIB' -> L4010 (recursive descent) | boot.c:44736 |
| `glib_lb_register` | L35fa | other | L35fa (CODE 5+0x35fa) — _LBRegister: record one (signature, converter) pair | boot.c:44721 |
| `glib_pool_close` | JT[466] | other | JT[466] (CODE 3+0x632) — FCCleanup: tear the pool down | boot.c:45487 |
| `glib_pool_open` | JT[463] | other | Exported wrapper: master_init runs the pool open at the Mac's jt1079 call site, passing the same kb_min/kb_max the Mac forwards to JT[463] (see boot.h / master_init) | boot.c:45475 |
| `glib_pool_selftest` | — | other | GLIB FAR-pool self-test (probe-gated) | boot.c:45505 |
| `hit_edit_item` | — | other | Variant of hit_item that fires for edit-text items only — used to give an editText field focus when the user clicks it. | dialogs.c:364 |
| `hit_item` | — | other | Point where; in global screen coordinates | dialogs.c:328 |
| `hud_text` | — | other | hud_text — blit a string straight to the chunky screen buffer at (x,y) in `color` (a clut index), clipped to the screen | boot.c:13897 |
| `ikbd_mouse_handler` | — | other | IKBD packet: byte 0 = 0xF8 \| (left ? 2 : 0) \| (right ? 1 : 0); byte 1 = signed delta-X; byte 2 = signed delta-Y (positive = down, the IKBD default) | input.c:101 |
| `init_ctrl_item` | — | other | — | dialogs.c:513 |
| `init_te_item` | — | other | — | dialogs.c:553 |
| `install_supervisor` | — | other | — | input.c:131 |
| `intro_load_cycles` | — | other | Read set `set`'s palette-piece cycle records (metric byte 6 holds the count; the records follow the RGB block) | boot.c:19094 |
| `intro_load_palette` | — | other | Overlay a TITLE.CTL screen's RGB palette (item 0 of set `set`) onto `dst[]`, honouring the palette's allotted CLUT range | boot.c:19049 |
| `j200_dump` | — | other | j200_dump — write the recorded jt200 slots (one per line: #, top, left, code, sub, group, farIdx, nearIdx) to J200DIFF.TXT for the slot-by-slot diff against docs/mac-blit-trace-heirs-l5.md. | boot.c:10774 |
| `j2_begin` | — | other | static short g_j2_tx[J2_MAX], g_j2_ty[J2_MAX]; near tile landed x0,y0 | boot.c:10327 |
| `jt1` | JT[1] | jt | JT[1] (CODE 1 + 0x130) / JT[2] (CODE 1 + 0x144) — THINK C's *sparse* inline switch runtime, the sibling of JT[3] | boot.c:5262 |
| `jt100` | JT[100] | jt | JT[100] (CODE 6 + 0x4bd2) — a hundredths-of-a-second time base: the 60 Hz TickCount (jt1134) rescaled *100/72 (jt4 multiply, jt7 signed long divide) | boot.c:39081 |
| `jt1000` | JT[1000] | jt | JT[1000] (CODE 5+0x28ce) — load the 16-byte palette template at `src` into the -4188 slot (the GLIB text-palette template). | boot.c:48615 |
| `jt1001` | JT[1001] | jt | JT[1001] (CODE 5 + 0x31ac) — select GLIB resource group `c`, blit its item `d` at pen (a, b) | boot.c:4825 |
| `jt1002` | JT[1002] | jt | JT[1002] (CODE 5 + 0x2822) — allocate the GEO read buffer into g_a5_-4582 | boot.c:760 |
| `jt1004` | JT[1004] | jt | JT[1004] (CODE 5 + 0x2850) — return the GEO read buffer pointer. | boot.c:859 |
| `jt1004_handle` | JT[1004] | other | jt1004 (JT[1004], CODE 5 + 0x2850) — return the current wall-tile library handle (g_a5_-4582), the DUNGCOM GLIB the dungeon view blits its pre-rendered slot tiles from | boot.c:10043 |
| `jt1005` | — | jt | — | boot.c:5991 |
| `jt1006` | JT[1006] | jt | JT[1006] (CODE 5+0x28ea) — fill pattern for colour `idx` (& 15) | boot.c:44004 |
| `jt1007` | JT[1007] | jt | JT[1007] (CODE 5+0x330c) — build + install the cursor from GLIB piece `item` of `group`, full lift | boot.c:17693 |
| `jt1009` | JT[1009] | jt | JT[1009] (CODE 5 + 0x0a34) — push paint-stack frame | boot.c:600 |
| `jt101` | JT[101] | jt | JT[101] (CODE 6+0x4b40 = L4b40) — the CODE 6 "%r" ALERT: scroll- clear (jt176), draw the formatted message in the alert box (L3fd6 row 24), commit the paint (L3994), scroll-advance (L4bac), clear agai | boot.c:49018 |
| `jt1011` | JT[1011] | jt | JT[1011] (CODE 5+0x3a0e) — load library item `item`: validate the GLIB header, seek to the index entry, read the item's [offset,end] pair, seek to the item data, and return its byte size (file left po | boot.c:40296 |
| `jt1012` | JT[1012] | jt | JT[1012] (CODE 5 + 0x37aa) IS L37aa — the GLIB library item lookup (same address) | boot.c:6112 |
| `jt1013` | JT[1013] | jt | JT[1013] (CODE 5+0x396c) — find the item index for resource `id`: load the directory (item 0), read its entry count, then scan the [id,index] pairs | boot.c:40332 |
| `jt1014` | JT[1014] | jt | JT[1014] (CODE 5+0x36a4) — the plain-name library loader | boot.c:46146 |
| `jt1015` | JT[1015] | jt | JT[1015] (CODE 5 + 0x3834, = LBISize) — byte size of GLIB item `item` in library `lib`: validate the 16-byte 'GLIB' header, range-check the item against the header's count (hdr[8] word), then return t | boot.c:44749 |
| `jt1016` | JT[1016] | jt | JT[1016] (CODE 5+0x3640) — read one library/picture item into a group and bind it: jt460 reads the bytes, jt459 sizes the group, L4010 commits/relocates | boot.c:45399 |
| `jt1017` | JT[1017] | jt | JT[1017] (CODE 5 + 0x38be, = LBIndxType) — the GLIB index-type byte (hdr[11]) of `lib`, after validating the 'GLIB' header. | boot.c:45179 |
| `jt1018` | JT[1018] | jt | JT[1018] (CODE 5 + 0x3736) — "LBCount": read a GLIB library file's 16-byte header into a scratch, verify the "GLIB" magic, and return the entry count (the word at header offset 8) | boot.c:46620 |
| `jt102` | JT[102] | jt | JT[102] (CODE 6 + 0x4b90, 29 sites) — "pause for one game-speed beat": reads the speed byte at offset 18 of the g_a5_28006 record, scales it by 100, and hands it to the tick-paced wait jt476. | boot.c:23582 |
| `jt1020` | JT[1020] | jt | JT[1020] (CODE 5 + 0x38fa) — look up key `target` in the GLIB group's remap sub-table: l37aa loads the group, the first word is the entry count, then `count` 4-byte (key, value) records follow | boot.c:45197 |
| `jt1021` | JT[1021] | jt | JT[1021] (CODE 5+0x4414) — LBInsert: insert a new item of `size` bytes at index `item` of the list-block in GLIB group `group`, full lift | boot.c:45769 |
| `jt1022` | JT[1022] | jt | JT[1022] (CODE 5+0x46a6) — LBResize: grow/shrink list-block item `item` of `group` to `newsize`, full lift | boot.c:43586 |
| `jt1023` | — | jt | — | boot.c:45717 |
| `jt1024` | JT[1024] | jt | JT[1024] (CODE 5+0x43ba) — LBCreate: bind `name` into `group` (jt464; nonzero = it already existed, nothing to do) and lay down a fresh list-block: the 16-byte header ('GLIB' magic, total 20, count 0, | boot.c:45841 |
| `jt103` | — | jt | — | boot.c:5830 |
| `jt104` | JT[104] | jt | JT[104] (CODE 6+0x3214) — the per-file load callback jt987 invokes after opening a library/picture file | boot.c:45866 |
| `jt1044` | CODE 5 + 0x5716 | jt | jt1044 (CODE 5 + 0x5716), jt1050 (CODE 5 + 0x59ee) — Window Manager helpers L747a / L74ae call | boot.c:16361 |
| `jt1050` | — | jt | — | boot.c:16368 |
| `jt1061` | JT[1061] | jt | JT[1061] (CODE 5 + 0x6456, 38 sites) — _SwapMMUMode ($A05D) glue: d0.b = *modep; d0.b = _SwapMMUMode(d0.b); *modep = d0.b; the documented Memory Manager trap that flips the 68k between 24-bit and 32-b | boot.c:786 |
| `jt1064` | JT[1064] | jt | JT[1064] (CODE 5 + 0x4992) — hit-test? L70e0 calls this after computing scaled coords | boot.c:16719 |
| `jt1066` | JT[1066] | jt | JT[1066] (CODE 5 + 0x759a) — GLIB palette COMPACT + COMMIT | boot.c:44984 |
| `jt1067` | JT[1067] | jt | JT[1067] (CODE 5 + 0x7772) — GLIB palette colour CYCLING | boot.c:45122 |
| `jt1068` | JT[1068] | jt | JT[1068] (CODE 5 + 0x70d2, "DNPInit") — initialise the GLIB palette subsystem: NewPtr the two 768-byte (256*3) RGB buffers (-3394 work, -3390 live), zero them, set the -3386 used-bitmap all-255 (commi | boot.c:44822 |
| `jt1069` | — | jt | — | boot.c:44860 |
| `jt107` | JT[107] | jt | JT[107] (CODE 6+0x36da) — read the -18397 byte (the active file-group kind latch) | boot.c:43316 |
| `jt1071` | JT[1071] | jt | JT[1071] (CODE 5+0x7bfa) — print a CENTERED message line: format (the Mac's "%r" + arg tail = the port's vsprintf bridge), shift the text right by (86 - len)/2 (jt406 self-move), space-fill the left p | boot.c:44033 |
| `jt1072` | JT[1072] | jt | JT[1072] (CODE 5+0x7c74) — flush `n` pending message-window lines: call L7ab4(NULL) n times (the asm pre-decrements, so exactly n). | boot.c:40583 |
| `jt1074` | — | jt | — | boot.c:40572 |
| `jt1078` | JT[1078] | jt | JT[1078] (CODE 5 + 0x440) — the modal line editor backing jt98's input box | boot.c:40955 |
| `jt108` | JT[108] | jt | JT[108] (CODE 6 + 0x38d0, 30 sites) — mark GrafPort dirty + commit deferred paint | boot.c:3725 |
| `jt1080` | JT[1080] | jt | JT[1080] (CODE 5 + 0x0156) — "no-hit" feedback chime | boot.c:18038 |
| `jt1081` | JT[1081] | jt | JT[1081] (CODE 5 + 0x62) — global teardown chain (9 calls: L27bc, L35f8, jt466, jt1156, L01ac, jt1119, jt1114, L0f14, jt1158 — releases every subsystem before an exit) | boot.c:869 |
| `jt1083` | JT[1083] | jt | JT[1083] (CODE 5 + 0x1ae) — linear-congruential PRNG, returns a uniform short in [0, n-1] for n > 1, else 0 | boot.c:4043 |
| `jt1085` | L23b4 | jt | New PROBE-stub helpers L23b4 needs. | boot.c:39091 |
| `jt1086` | JT[1086] | jt | JT[1086] (CODE 5+0x208) — clear the whole screen to `fill` through the jt1161 rect filler. | boot.c:48516 |
| `jt1087` | CODE 4+0x61f8 | jt | static void jt1148(void) { PROBE("jt1148"); ObscureCursor(); } CODE 4+0x61f8: _ObscureCursor + rts | boot.c:12764 |
| `jt1088` | — | jt | — | boot.c:12722 |
| `jt1089` | JT[1089] | jt | JT[1089] (CODE 5 + 0x334) — text paint at logical (x, y) | boot.c:6201 |
| `jt109` | JT[109] | jt | JT[109] (CODE 6+0x3af8) — draw piece `b` of GLIB group `grp` through JT[994] on the jt468-resolved base | boot.c:44362 |
| `jt1090` | JT[1090] | jt | JT[1090] (CODE 5+0xda) — wait for a key/click: flush (L00a8), poll L0088 until it reports input, flush again. | boot.c:48526 |
| `jt10_handler` | JT[989] | other | UI-handler callbacks main() registers through JT[989]. | boot.c:1899 |
| `jt110` | JT[110] | jt | JT[110] (CODE 6 + 0x33ac) — GLIB slot loader (linkw -420; walks the -18468 slot table for a free entry and opens the named GLIB into it) | boot.c:23045 |
| `jt1108` | JT[1108] | jt | JT[1108] (CODE 4+0x62e4) — pump the event buffer (mode 1) and report the pending-event byte (g_a5_-904). | boot.c:46284 |
| `jt1109` | — | jt | — | boot.c:40066 |
| `jt111` | — | jt | — | boot.c:6629 |
| `jt1113` | — | jt | — | boot.c:11802 |
| `jt1114` | JT[1114] | jt | JT[1114] (CODE 4 + 0x61ee) — flag "engine initialized." g_a5_-900 = 1; Set by master_shutdown so the next master_init can clean up any pending state | master.c:44 |
| `jt1115` | JT[1115] | jt | JT[1115] (CODE 4+0x4cb2) — empty body on the Mac (linkw/unlk/rts); a faithful no-op (a compiled-out registration hook). | boot.c:48342 |
| `jt1116` | JT[1116] | jt | JT[1116] (CODE 4+0x5d8c) — is the menu/cursor state word "up"? (-2352 == -2354) | boot.c:46380 |
| `jt1118` | JT[1118] | jt | JT[1118] (CODE 4 + 0x6710) — "should we continue polling?" gate | boot.c:16025 |
| `jt1119` | JT[1119] | jt | JT[1119] (CODE 4 + 0x797e) — no-op (bare rts in the Mac body) | master.c:90 |
| `jt112` | JT[112] | jt | JT[112] (CODE 6 + 0x38fe, 43 sites) — paint-mode setter | boot.c:3704 |
| `jt1121` | — | jt | — | boot.c:40074 |
| `jt1122` | JT[1122] | jt | JT[1122] (CODE 4 + 0x7690) — menu-bar slot setter / blinker | boot.c:17794 |
| `jt1123` | JT[1123] | jt | JT[1123] (CODE 4+0x659a) — install the 516-byte cursor record (4-byte header: hotspot x/y + dims; 256B image; 256B mask — the 16x16 8-bit colour cursor jt1007 builds; NULL = the arrow), full lift | boot.c:17640 |
| `jt1125` | — | jt | short g_event_was_click; set by jt1125: 1 if the last event was a click | boot.c:16065 |
| `jt1126` | — | jt | — | boot.c:26217 |
| `jt1128` | JT[1128] | jt | JT[1128] (CODE 4+0x5104) — the Mac's double-buffer PAGE FLIP: bump the -2354/-2352 page indices, point -3076 at the new page's pixels (the 108-byte GrafPort table at -2570) and run the L3e38 commit | boot.c:6121 |
| `jt1129` | JT[1129] | jt | JT[1129] (CODE 4 + 0x4756) — 1-case JT[3] switch on `a` | boot.c:534 |
| `jt113` | JT[113] | jt | JT[113] (CODE 6 + 0x338c) — set the record file format byte (g_a5_-18396): the caller's value when the platform probe (jt1200) reports 3, else the default 52. | boot.c:1442 |
| `jt1130` | JT[1130] | jt | JT[1130] (CODE 4+0x61f6) — bare rts on the Mac; a faithful no-op. | boot.c:582 |
| `jt1131` | JT[1131] | jt | JT[1131] (CODE 4 + 0x7760) — semitone -> menu-slot dispatcher | boot.c:18006 |
| `jt1132` | JT[1132] | jt | JT[1132] (CODE 4 + 0x6288) — mouse poll | boot.c:7191 |
| `jt1133` | JT[1133] | jt | JT[1133] (CODE 4 + 0x6742) — keyboard input read | boot.c:17522 |
| `jt1134` | CODE 4 + 0x7980 | jt | jt1134 (CODE 4 + 0x7980) — yield-to-OS / drain idle paint | boot.c:17309 |
| `jt1135` | — | jt | — | boot.c:5864 |
| `jt1137` | JT[1137] | jt | JT[1137] (CODE 4+0x7a10) — FlushEvents: a no-op in this port (returns 0). | boot.c:46292 |
| `jt1138` | JT[1138] | jt | JT[1138] (CODE 4 + 0x66f8) — reset engine input state | master.c:62 |
| `jt1139` | — | jt | — | boot.c:6062 |
| `jt114` | JT[114] | jt | JT[114] (CODE 6 + 0x3804) — blit wall tile `idx` from a wall-set tile library | boot.c:10272 |
| `jt1140_pg` | JT[1140] | other | JT[1140] (CODE 4+0x77d0) — set the page-scroll latch; when clearing it (v==0) reset the scroll state via JT[1151] (=L765c). | boot.c:18489 |
| `jt1141` | — | jt | — | boot.c:6081 |
| `jt1142` | — | jt | — | boot.c:40072 |
| `jt1145` | JT[1145] | jt | JT[1145] (CODE 4 + 0x7628) — show menu bar | boot.c:16464 |
| `jt1146` | JT[1146] | jt | JT[1146] (CODE 4 + 0x5c82) — page-flip / full-page blit | boot.c:17451 |
| `jt1147` | JT[1147] | jt | JT[1147] (CODE 4+0x77f6) — _SysBeep(6) (the alert beep). | boot.c:46406 |
| `jt1148` | — | jt | — | boot.c:12763 |
| `jt1149` | JT[1149] | jt | JT[1149] (CODE 4 + 0x79c2) — ticks since the sound-timer base (g_a5_-130). | boot.c:17851 |
| `jt115` | JT[115] | jt | JT[115] — generic slot-release service | boot.c:4428 |
| `jt1150` | JT[1150] | jt | JT[1150] (CODE 4+0x61fc) — mark a screen rect dirty for the next present | boot.c:41691 |
| `jt1151` | JT[1151] | jt | JT[1151] (CODE 4 + 0x765c) — hide menu bar | boot.c:16491 |
| `jt1152` | — | jt | — | boot.c:40070 |
| `jt1153` | JT[1153] | jt | JT[1153] (CODE 4 + 0x5d34) — select back-page for rendering | boot.c:17370 |
| `jt1154_pg` | JT[1154] | other | JT[1154] (CODE 4+0x77e8) — read the page-scroll latch g_a5_-806. | boot.c:18485 |
| `jt1155` | JT[1155] | jt | JT[1155] (CODE 4 + 0x7972) — stamp the engine's tick origin | master.c:82 |
| `jt1156` | JT[1156] | jt | JT[1156] (CODE 4 + 0x670e) — no-op (bare rts in the Mac body) | master.c:73 |
| `jt1157` | — | jt | — | master.c:37 |
| `jt1158` | JT[1144] | jt | CODE 4 — the Mac Toolbox / Window Manager layer | master.c:36 |
| `jt116` | — | jt | — | boot.c:41769 |
| `jt1160` | JT[1160] | jt | JT[1160] (CODE 4 + 0x67c6) — is the TOP-DOWN / automap view active? Tests bit 1 of g_a5_-2592 (the view-mode flags) | boot.c:11993 |
| `jt1161` | — | jt | — | boot.c:6363 |
| `jt1163` | JT[1163] | jt | JT[1163] (CODE 4 + 0x0532, 36 sites) — return 0 | boot.c:4121 |
| `jt1164` | — | jt | — | boot.c:45250 |
| `jt1166` | JT[1166] | jt | JT[1166] / JT[1179] (CODE 4+0x4cc / +0x4de) — screen height / width: 200 x 320 in the colour mode (-2347 set), 300 x 480 in the Mac's B&W window. | boot.c:48502 |
| `jt1167` | JT[1167] | jt | JT[1167] (CODE 4+0x17b8) — restore the saved clip bounds. | boot.c:46396 |
| `jt1169` | — | jt | — | boot.c:45237 |
| `jt117` | JT[117] | jt | JT[117] (CODE 6 + 0x3994, 56 sites) — alias for L3994 (GrafPort save snapshot) | boot.c:6992 |
| `jt1170` | JT[1170] | jt | JT[1170] (CODE 4 + 0x0536) — empty body (linkw / unlk / rts) | boot.c:4130 |
| `jt1171` | JT[1171] | jt | JT[1171] (CODE 4 + 0x108e) — the Mac `_UnpackBits` trap (0xa8d0) wrapper | boot.c:1260 |
| `jt1173` | JT[1173] | jt | JT[1173] (CODE 4 + 0x164c) — set the engine clip rect | boot.c:27609 |
| `jt1175` | — | jt | Row horizontal-mirror primitives (CODE 4) — flip `n` bytes of one pixel row in place: reverse the byte order, then reverse within each byte at the row's pixel depth | boot.c:45226 |
| `jt1177` | JT[1177] | jt | JT[1177] = L053e (CODE 4 + 0x053e) — seed the GLIB blit cursor at (row, col): caches the pair in -3080/-3078 and points -3076 at base + (rowbytes >> depth-shift) * row + (col >> depth-shift) | boot.c:26177 |
| `jt1179` | — | jt | — | boot.c:48508 |
| `jt118` | — | jt | — | boot.c:23601 |
| `jt1180` | JT[1180] | jt | JT[1180] (CODE 4 + 0x228c, 67 sites) — 16-bit byte swap | boot.c:19964 |
| `jt1181` | — | jt | — | boot.c:4910 |
| `jt1182` | JT[1182] | jt | JT[1182] (CODE 4+0x17d2) — load the 16-byte palette template at `src` into the live g_a5_-3016 slot | boot.c:12526 |
| `jt1183` | — | jt | — | boot.c:4892 |
| `jt1184` | — | jt | — | boot.c:4918 |
| `jt1186` | JT[1186] | jt | JT[1186] (CODE 4+0x1468) — commit a whole 768-byte (256 x RGB) palette to the CLUT | boot.c:48566 |
| `jt1188` | — | jt | — | boot.c:4901 |
| `jt1189` | — | jt | — | boot.c:4926 |
| `jt119` | JT[119] | jt | JT[119] (CODE 6 + 0x3d3a) — save-under: capture the GLIB backing store for a sprite at (x,y) into the g_a5_18288 row buffers (144 bytes/row) | boot.c:26332 |
| `jt1191` | — | jt | — | boot.c:4935 |
| `jt1192` | — | jt | — | boot.c:26082 |
| `jt1193` | JT[1193] | jt | JT[1193] (CODE 4 + 0x16e0) — reset the accumulated dirty/clip rectangle to the full screen: the max corner (left/top, -3056/-3054) to 0 and the min corner (bottom/right, -3050/-3052) to the screen ext | boot.c:27634 |
| `jt1194` | — | jt | — | boot.c:26117 |
| `jt1197` | JT[1197] | jt | JT[1197] (CODE 4 + 0x083e) — save-under: read a `count` x `runlen` block from the GLIB cursor (row stride g_a5_3084) into the packed buffer `buf`, leaving the cursor (g_a5_3076) at the end | boot.c:26034 |
| `jt1198` | JT[1198] | jt | JT[1198] (CODE 4 + 0x52e) — Mac body returns 1 always | boot.c:4846 |
| `jt1199` | JT[1199] | jt | JT[1199] (CODE 4+0x22aa) — byte-swap a long (endian reverse; the design/save data is little-endian on disk). | boot.c:26608 |
| `jt11_handler` | CODE 6 + 0x0538 | other | static void jt10_handler(void) { } CODE 6 + 0x0538 (jump-table entry 10) | boot.c:1900 |
| `jt120` | JT[120] | jt | JT[120] (CODE 6 + 0x3918) — set the "current view" cache | boot.c:29156 |
| `jt1200` | JT[1200] | jt | JT[1200] (CODE 4 + 0x04f0) — encounter-mode state classifier | boot.c:6599 |
| `jt1201` | JT[1201] | jt | JT[1201] (CODE 4+0x1636) — jt1186 plus the Mac's page flip (L5104) | boot.c:48577 |
| `jt1202` | JT[1202] | jt | JT[1202] (CODE 4 + 0x06be) — draw: copy a `w`-row x `h`-byte block from `src` (per-row stride `stride2`) to the GLIB cursor (row stride g_a5_3084) and mark it dirty (l05ea). | boot.c:26053 |
| `jt1205` | JT[1205] | jt | JT[1205] (CODE 4+0x179e) — save the clip bounds into the spare slots. | boot.c:46385 |
| `jt121` | JT[121] | jt | JT[121] (CODE 6 + 0x379c) — draw a tile glyph `c` from GLIB `handle` at map cell (x,y) in doubled space (x*4+8004, y*4+8004), via jt108 + jt1001. | boot.c:26318 |
| `jt122` | JT[122] | jt | JT[122] (CODE 6 + 0x3e10) — draw: blit the g_a5_18288 row buffers back to the GLIB at (x,y) (the restore/paint paired with jt119's save), same transform and row loop but via jt1202. | boot.c:26360 |
| `jt123` | JT[123] | jt | JT[123] (CODE 6 + 0x3828) — mirror the sprite piece keyed by `b` in the group `*(short*)handle`: jt992(jt468(group), b) flips it horizontally. | boot.c:6662 |
| `jt124` | L3eea | jt | jt124 IS L3eea (same address); the arg is the art-handle pointer (already dereferenced at the call site), so delegate. | boot.c:12923 |
| `jt126` | JT[126] | jt | JT[126] (CODE 6+0x02d2) — the numbered-.dat load callback jt129 registers with jt987 | boot.c:41781 |
| `jt127` | — | jt | — | boot.c:27305 |
| `jt128` | JT[128] | jt | JT[128] (CODE 6 + 0x3f6) — persist the current design (g_a5_-31336, 34 bytes) + the g_a5_-18476 flag to "start.dat" so the next boot resumes it | boot.c:40125 |
| `jt129` | — | jt | — | boot.c:41796 |
| `jt13` | JT[13] | jt | JT[13] / L14fc (CODE 6 + 0x14fc) — does `entity` carry any of the four effect types in the g_a5_-25296 table? Scans i=0..3, jt41(entity, type[i], &node) for each, returns 1 if any is present (the "(He | boot.c:23762 |
| `jt130` | JT[130] | jt | JT[130] (CODE 6+0x0040) — build the design's 8-char file basename from the 17-byte -31268 name table (L0004 per char, leaf stub), truncate at 8, uppercase (jt405) | boot.c:41826 |
| `jt131` | CODE 6 + 0x35e | jt | jt131 — engine state-transition manager | boot.c:4556 |
| `jt132` | JT[132] | jt | JT[132] (CODE 6 + 0x0092) — set the current file-group id | boot.c:709 |
| `jt133` | JT[133] | jt | JT[133] (CODE 6 + 0x3c2) — make `name` the current design: back the old name g_a5_-31336 up into g_a5_-31302, then copy `name` in. | boot.c:40108 |
| `jt134` | JT[134] | jt | JT[134] (CODE 6 + 0x0138) — set the file-group sub-id byte (g_a5_-31235) | boot.c:1433 |
| `jt135` | JT[135] | jt | JT[135] (CODE 6 + 0x3e6) — restore the design name jt133 backed up. | boot.c:40116 |
| `jt137` | — | jt | static short jt1166(void); defined just below | boot.c:48384 |
| `jt138` | L1bfe | jt | New PROBE-stub helpers L1bfe references. | boot.c:38674 |
| `jt139` | — | jt | — | boot.c:38675 |
| `jt14` | JT[14] | jt | JT[14] (CODE 6+0x269e) — post-heal report: "is fully healed" when at max HP (rec[129]), else "is partially healed", announced through jt18; repaint the member's roster line when not in the combat view | boot.c:35987 |
| `jt140` | JT[140] | jt | JT[140] / JT[156] (CODE 7 + 0x1e58 / 0x1d5c) — item-callback PROCs that JT[158] passes as the "draw" function pointer in its JT[452] push | boot.c:19838 |
| `jt141` | JT[141] | jt | JT[141] (CODE 7 + 0x29b0) — jt173's shape-7 key proc: SPACE (32) latches g_a5_-13003 (the "space pressed" flag jt173's tail converts back to ' '), pokes the item with cmd 20, and reports the key consu | boot.c:39555 |
| `jt142` | JT[142] | jt | JT[142] (CODE 7+0x1d18) — screen point -> text cell: JT[1139] maps (y, x) against the (8004, 8004) origin, then the offsets divide by the 12-unit cell pitch into the A5 -13010 (row) and -13008 (col) c | boot.c:40609 |
| `jt145` | JT[145] | jt | JT[145] (CODE 7+0x1686) — combat post-action repaint: blit FRAME piece 6 at the (8000, 8000) anchor | boot.c:37994 |
| `jt146` | — | jt | — | boot.c:6997 |
| `jt147` | JT[147] | jt | JT[147] (CODE 7 + 0x18d4) — free a singly-linked list whose .next is at offset 0: walk from *head, releasing each 40-byte node back to the g_a5_21156 bucket via jt471. | boot.c:23502 |
| `jt148` | CODE 7 + 0x33dc | jt | jt148 (CODE 7 + 0x33dc) — the play-screen command/prompt bar paint, the piece jt240/jt241 call to lay the bottom bar | boot.c:39042 |
| `jt149` | JT[149] | jt | JT[149] (CODE 7+0x38d6) — set the dialog-default flag byte -12648. | boot.c:48358 |
| `jt150` | JT[150] | jt | JT[150] (CODE 7+0x38d6's sibling, +0x38ea) — set the dialog flag byte -12647 (pairs with jt149's -12648). | boot.c:49041 |
| `jt151` | JT[151] | jt | JT[151] (CODE 7+0x1572) — register jt137 as registry callback 1, remembering the previous one in -12918. | boot.c:48491 |
| `jt152` | JT[152] | jt | JT[152] (CODE 7 + 0x3370) — classify a poll result as a command-bar command or "not a command" | boot.c:39683 |
| `jt153` | JT[153] | jt | JT[153] (CODE 7 + 0x159a) — set the current-pick record (-13014). | boot.c:29645 |
| `jt155` | JT[155] | jt | JT[155] (CODE 7 + 0x11a8, 99 callsites) — append one byte to the g_a5_24126 index buffer | boot.c:19889 |
| `jt156` | JT[156] | jt | JT[156] (CODE 7+0x1d5c) — the roster-pane CLICK handler, full lift over the jt960/l15ae leaf stubs | boot.c:43664 |
| `jt157` | JT[157] | jt | JT[157] (CODE 7+0x38e4) — read the -12648 byte (the text-cursor column latch) | boot.c:44373 |
| `jt158` | JT[158] | jt | JT[158] — walk the design list, then either add a menu item per design (modes 7 / 9 / 12 / other) or disable a stale slot (when the count shrank) | boot.c:20050 |
| `jt159` | CODE 7 + 0x16ea | jt | jt159 (CODE 7 + 0x16ea) — a yes/no confirmation prompt; returns 1 to confirm, 0 to cancel | boot.c:27590 |
| `jt16` | JT[16] | jt | JT[16] (CODE 6 + 0x1f3e) — strength damage adjustment: maps the effective strength from l1d54 (18/xx exceptional folded to 19..23) to the AD&D melee damage bonus. | boot.c:34422 |
| `jt160` | — | jt | — | boot.c:49163 |
| `jt161` | — | jt | — | boot.c:6998 |
| `jt162` | JT[162] | jt | JT[162] (CODE 7 + 0x15a8) — get the current-pick record (-13014). | boot.c:29653 |
| `jt163` | — | jt | — | boot.c:29669 |
| `jt164` | CODE 7 + 0x2fa4 | jt | jt164 (CODE 7 + 0x2fa4) — the horizontal button-bar picker | boot.c:39641 |
| `jt165` | JT[165] | jt | JT[165] (CODE 7+0x15c2) — the n'th node of a linked list (next pointer at +0); 0 when the list is shorter | boot.c:21856 |
| `jt166` | — | jt | g_a5_24126 → macro array (data_pool replay buffer) | boot.c:19854 |
| `jt167` | JT[167] | jt | JT[167] (CODE 7 + 0x183a) — build a chain of `count` 40-byte nodes drawn from the pool whose header pointer is g_a5_-21156, linking them through offset 0 (.next) and zeroing each node's .next (+0) and | boot.c:23463 |
| `jt168` | JT[168] | jt | JT[168] (CODE 7+0x35e6) — stash the list-dialog context: the head pointer (-12654) and the two index bytes (-12650/-12649). | boot.c:41351 |
| `jt169` | — | jt | — | boot.c:21894 |
| `jt169_pick` | — | other | Fill *idx + *next (the chosen node) from the selected row index. | boot.c:21877 |
| `jt17` | JT[17] | jt | JT[17] (CODE 6 + 0x2716) — effective level / class-cap lookup for the current character (g_a5_27932) | boot.c:24461 |
| `jt171` | — | jt | — | boot.c:39506 |
| `jt173` | — | jt | — | boot.c:39577 |
| `jt174` | — | jt | g_a5_12911 → macro (data_pool replay buffer) | boot.c:5449 |
| `jt175` | CODE 7+0x17f8 | jt | jt175 (CODE 7+0x17f8) — show the prompt (L177a) then run the modal poll (jt453) until the user dismisses it. | boot.c:18611 |
| `jt176` | JT[176] | jt | JT[176] (CODE 7 + 0x162e, 36 sites) — window paint init / commit | boot.c:27657 |
| `jt179` | JT[179] | jt | JT[179] — initialise the 40-byte slot-index table at a5@(-24126) | boot.c:19866 |
| `jt18` | JT[18] | jt | JT[18] (CODE 6 + 0x22da, 115 callsites) — record-window paint dispatcher | boot.c:5583 |
| `jt180` | JT[180] | jt | JT[180] (CODE 7 + 0x38f8) — read the char-gen "name-edit active" flag byte (g_a5_-12647) | boot.c:20639 |
| `jt181` | JT[181] | jt | JT[181] (CODE 7 + 0x1806) — emit a one-line status/prompt string | boot.c:31267 |
| `jt182` | — | jt | — | boot.c:39436 |
| `jt184` | JT[184] | jt | JT[184] (CODE 7+0x483e) — build the conjured magic-scroll item record, full lift: jt65 zero-fill of 18 bytes, kind 39 (scroll) at +0/+2, 102 at +1, +8 = 1, word +4 = 1, word +6 = 3000 (the value), +12 | boot.c:43147 |
| `jt188` | — | jt | — | boot.c:44257 |
| `jt19` | JT[19] | jt | JT[19] (CODE 6+0x2cb0) — remove the ACTIVE member (-27932) from the party list (-27928) and destroy the record (L2b40) | boot.c:27543 |
| `jt191` | JT[191] | jt | JT[191] (CODE 7+0x4c88) — decode a 6-bit-packed name (4 chars in 3 bytes, the SSI radix coding) into `out`, up to `max` chars | boot.c:41153 |
| `jt194` | JT[194] | jt | JT[194] (CODE 7+0x50c0) — bind the text record (L4ab6) and report its dims in depth-3 scale: *out = width word * 4 / 3, returns height word * 4 / 3 (the -12304 bound record) | boot.c:44202 |
| `jt196` | JT[196] | jt | JT[196] (CODE 7+0x4aee) — pack a C-string name into the 6-bit 4-chars-in-3-bytes coding (jt191's inverse), full lift: each group packs c0<<2 \| c1>>4 — c1<<4 \| c2>>2 — c2<<6 \| c3, with the L4a7a fold/m | boot.c:43181 |
| `jt197` | JT[197] | jt | JT[197] (CODE 7+0x601a) — secret-door style class for cell (row, col): 0 when out of bounds (L5baa), else bits 2..4 of the cell's byte 295 in the level map (-12300, 6-byte stride). | boot.c:49081 |
| `jt198` | JT[198] | jt | JT[198] (CODE 7 + 0x7124) — load GEOnnn.DAT for the given level number | boot.c:1035 |
| `jt199` | — | jt | — | boot.c:10622 |
| `jt199_band` | JT[3] | other | jt199_band — a mid/far-band scan (JT[3] cases 1 and 0) | boot.c:10583 |
| `jt199_front` | JT[199] | other | jt199_front — one FRONT-wall pass (JT[199]'s pass 3 / pass 4) | boot.c:10556 |
| `jt199_side` | JT[199] | other | jt199_side — one SIDE-wall pass of the frustum walker (JT[199]'s pass 1 / pass 2) | boot.c:10514 |
| `jt1_dispatch` | — | other | — | boot.c:5280 |
| `jt2` | — | jt | — | boot.c:5269 |
| `jt20` | JT[20] | jt | JT[20] (CODE 6 + 0x241e, 94 callsites) — record-window opener | boot.c:5615 |
| `jt200` | — | jt | — | boot.c:10356 |
| `jt200_layer` | JT[114] | other | jt200_layer — draw one wall-tile layer for jt200 | boot.c:10289 |
| `jt201` | JT[201] | jt | JT[201] (CODE 7 + 0x5f6a) — return the special-feature byte of map cell (x,y) | boot.c:2738 |
| `jt202` | JT[202] | jt | JT[202] (CODE 7 + 0x5e52) — read a cell's wall nibble in a given facing | boot.c:31024 |
| `jt203` | JT[203] | jt | JT[203] (CODE 7+0x6148) — 3D-view repaint prologue | boot.c:43041 |
| `jt204` | CODE 7 + 0x6ed8 | jt | jt204 — release one sub-resource and trip its sentinel | boot.c:4512 |
| `jt205` | JT[205] | jt | JT[205] (CODE 7 + 0x5f18) — wall-STYLE test, twin of JT[212]: same GEO cell record read (lvl + 290 + (lvl[3]*col + row)*6 + ((edge&6)>>1)) but returns the LOW nibble (the wall graphic/style; JT[212] r | boot.c:12511 |
| `jt209` | CODE 7 + 0x70e8 | jt | jt209 — release three sub-resources, optionally trip the entry sentinel | boot.c:4491 |
| `jt21` | JT[21] | jt | JT[21] (CODE 6 + 0x16aa, 26 sites) — recompute every derived field of a character record `mp` from its inventory and ability scores | boot.c:28541 |
| `jt210` | JT[210] | jt | jt210 / l5bfa (JT[210], CODE 7 + 0x5bfa) — read the wall-art code of map cell (row, col)'s edge facing direction `dir` | boot.c:10161 |
| `jt211` | JT[211] | jt | JT[211] (CODE 7 + 0x57bc) — allocate the 3746-byte design-state buffer, park it in g_a5_-12300, and clear its first short | boot.c:666 |
| `jt212` | JT[212] | jt | JT[212] (CODE 7 + 0x5cc8) — wall-bit test for the automap | boot.c:12788 |
| `jt213` | JT[213] | jt | JT[213] (CODE 7 + 0x56f2) — draw map cell (a,b) only if it falls inside the visible viewport | boot.c:12709 |
| `jt214` | CODE 7 + 0x71c6 | jt | jt214 (CODE 7 + 0x71c6) — pick the play-screen "bigpic" backdrop id and load it via JT[43] (L579e) | boot.c:12894 |
| `jt215` | JT[215] | jt | JT[215] (CODE 7 + 0x57a6) — set the automap cell pixel size | boot.c:12170 |
| `jt216` | JT[216] | jt | JT[216] (CODE 7 + 0x5752) — draw the party-position marker on the automap | boot.c:12943 |
| `jt217` | CODE 12 + 0x0562 | jt | static void jt938(void); CODE 12 + 0x0562 — clock/position HUD, lifted after its deps | boot.c:2183 |
| `jt218` | JT[218] | jt | JT[218] (CODE 7 + 0x52b8) — overland move + view recentre | boot.c:12046 |
| `jt22` | JT[22] | jt | JT[22] (CODE 6 + 0x2e76) — Constitution hit-point adjustment per hit die (the plain switch variant; jt881 below is the table variant) | boot.c:34588 |
| `jt220` | JT[220] | jt | JT[220] (CODE 7+0x6ea2) — load area-map icon `n` into the -22222 art slot (the shared-icon variant) | boot.c:40824 |
| `jt221` | JT[221] | jt | JT[221] (CODE 7 + 0x6076) — render the play view at the party position (x,y,facing) | boot.c:2701 |
| `jt224` | JT[224] | jt | JT[224] (CODE 7+0x0866) — page the scrolling text window: keys rec[14]+2 / rec[14]+3 move the view row (rec word 10) one page (rec word 4 - 1) up/down through L0264 (the pen/scroll setter — the Mac pa | boot.c:44222 |
| `jt226` | JT[226] | jt | JT[226] (CODE 7+0x0200) — scroll the list dialog to 1-based row `n` (L501e on n-1) | boot.c:44331 |
| `jt229` | JT[229] | jt | JT[229] (CODE 7+0x00d6) — clear field 0 of record `n` (1..100) of the -13038 record table (20-byte records); 1 on success. | boot.c:41362 |
| `jt23` | JT[23] | jt | JT[23] (CODE 6+0x2890) — the play-frame redraw dispatcher. | boot.c:46877 |
| `jt231` | JT[231] | jt | JT[231] (CODE 7 + 0x0004) — allocate the per-design NCR (2000 B, g_a5_-13038) and string-table (7168 B, g_a5_-13034) buffers and initialise them | boot.c:849 |
| `jt232` | JT[232] | jt | JT[232] (CODE 7 + 0x0138, 27 sites) — fetch in-game string `num` from record `rec` into `out`, then expand the first two '^' markers to the active character's name (g_a5_-27932 + 96), shifting the buf | boot.c:23734 |
| `jt236` | JT[236] | jt | JT[236] (CODE 11 + 0x5868) — L63c0's cb2 (default / dungeon arm): is the party's current cell blocked? Cell index = level_width*party_y + party_x into JT[276]; returns 1 when that cell tests as 0 (imp | boot.c:12309 |
| `jt237` | JT[237] | jt | JT[237] (CODE 11 + 0x5236) — the automap render (L63c0's cb1 in the wilderness arm) | boot.c:13073 |
| `jt238` | CODE 22+0x17ca | jt | static void jt304(void *rec_v, short batch); CODE 22+0x17ca, defined below | boot.c:12118 |
| `jt240` | JT[240] | jt | JT[240] (CODE 11 + 0x4ffe) — the DEEP (first-person) dungeon walk driver, the dungeon counterpart of jt241 (which is the top-down wilderness walker) | boot.c:13313 |
| `jt241` | JT[241] | jt | JT[241] (CODE 11 + 0x5514) — the play-action state machine | boot.c:13149 |
| `jt245` | JT[245] | jt | JT[245] (CODE 2+0x4024) — keyboard-settings filter: Return (13) and Escape (27) latch into -10372 and report 1; anything else clears it | boot.c:43829 |
| `jt247` | CODE 2 + 0x23be | jt | jt247 (CODE 2 + 0x23be) — the per-event GAME SETTINGS prompt: read the event-type code from *rec's low 6 bits, build the title from the -11000 string-pointer table and the prompt from the settings wor | boot.c:43909 |
| `jt25` | CODE 6 + 0x4b0e | jt | jt25 (CODE 6 + 0x4b0e) — paint a roster entry's NAME | boot.c:5469 |
| `jt251` | JT[251] | jt | JT[251] (CODE 2 + 0x4284) — the mode-4 handler: the design/play action dispatcher | boot.c:14377 |
| `jt257` | JT[257] | jt | JT[257] (CODE 2+0x20ee) — record one keystroke event: the -13038 record table's field 0 for record byte -12191 + `off` gates (or the -12192 bit 0 override); the value appends to the -12190 ring (count | boot.c:43976 |
| `jt26` | JT[26] | jt | JT[26] (CODE 6 + 0x3038) — experience-points-for-level lookup | boot.c:23436 |
| `jt261` | JT[261] | jt | JT[261] (CODE 10+0x62c6) — is byte `val` one of the 28 entries of the -11652 list? -1 when found (JT[409]'s index < 28), else 0. | boot.c:40434 |
| `jt262` | JT[262] | jt | JT[262] (CODE 10+0x62ee) — jt261's inverse: -1 when byte `val` is NOT in the 28-entry -11652 list (JT[409]'s index >= 28). | boot.c:41841 |
| `jt263` | JT[263] | jt | JT[263] (CODE 10 + 0x5acc) — monster/NPC setup state machine | boot.c:1578 |
| `jt272` | JT[272] | jt | JT[272] (CODE 22+0x087c) — area-map click -> move direction | boot.c:40719 |
| `jt273` | JT[273] | jt | JT[273] (CODE 22+0x4900) — deep/turn-counter gate: returns the counter jt358() when it is <= 4, else 0 | boot.c:11668 |
| `jt274` | JT[274] | jt | JT[274] (CODE 22+0x4922) — swap two 6-byte records. | boot.c:44310 |
| `jt275` | JT[275] | jt | JT[275] (CODE 22+0x04b2) — pack two nibbles into the -18476 byte: (a & 15) \| ((b & 15) << 4). | boot.c:41313 |
| `jt276` | JT[276] | jt | JT[276] (CODE 22 + 0x475e) — area-map cell topology test | boot.c:12183 |
| `jt277` | JT[277] | jt | JT[277] (CODE 22+0x056c) — set the LOW wall nibble for cell/side to `val`; 0 when unchanged, 1 when written. | boot.c:41393 |
| `jt278` | JT[278] | jt | JT[278] (CODE 22+0x294e) — paint one design-list entry | boot.c:40779 |
| `jt279` | — | jt | — | boot.c:43262 |
| `jt28` | — | jt | — | boot.c:28674 |
| `jt280` | JT[280] | jt | JT[280] (CODE 22 + 0x265e) — the dungeon position / compass readout drawn at the top of the play view (all via jt1089, the real text drawer): line 1: the party (x, y) coordinate — format g_a5_-11332 w | boot.c:11759 |
| `jt283` | JT[283] | jt | JT[283] (CODE 22+0x0614) — set the HIGH wall nibble (the decor half l05ca/jt293 reads) for cell/side to `val`; 0/1 as jt277. | boot.c:41411 |
| `jt284` | JT[284] | jt | JT[284] (CODE 22+0x09b0) — is the click on a cell INTERIOR? jt272's sibling: same viewport translate (-2 outside) and cell store into -12287/-12288, then 1 when the facing gate (L4900) passes or the i | boot.c:41432 |
| `jt285` | JT[285] | jt | JT[285] (CODE 22+0x0524) — read the LOW wall nibble for map cell `cell`, side `side` ((side & 6) >> 1 picks direction 0..3): the byte at -12300 design base + cell*6 + dir + 290 | boot.c:41378 |
| `jt287` | JT[287] | jt | JT[287] (CODE 22 + 0x1bc6) — the dungeon-walk KEYBOARD action proc l6256 installs on the shape-7 keyboard source | boot.c:12231 |
| `jt288` | JT[288] | jt | JT[288] = L069a (CODE 22 + 0x069a) — write cell code A; returns 1 when the value actually changed. | boot.c:11217 |
| `jt289` | — | jt | — | boot.c:44166 |
| `jt29` | JT[29] | jt | JT[29] (CODE 6 + 0x2f4c) — full lift | boot.c:29489 |
| `jt290` | — | jt | — | boot.c:41542 |
| `jt294` | JT[294] | jt | JT[294] (CODE 22+0x1c26, the region action proc) — stamp word +6 of the -11666 record (when bound) with `x`; flag/y unread. | boot.c:12248 |
| `jt295` | — | jt | — | boot.c:41715 |
| `jt296` | JT[296] | jt | JT[296] (CODE 22 + 0x3792) — map a cell (x,y) to screen coords within the visible map window | boot.c:12478 |
| `jt297` | JT[297] | jt | JT[297] (CODE 22 + 0x1c3e) — the keyboard movement handler l63c0 dispatches arrow keys (257..264) to | boot.c:11890 |
| `jt298` | JT[298] | jt | JT[298] = L073e (CODE 22 + 0x073e) — write cell code B; returns 1 when the value actually changed. | boot.c:11244 |
| `jt299` | JT[299] | jt | JT[299] (CODE 22+0x1798) — repaint one saved-game slot: L17ca state advance (leaf stub), L2180 box clear (leaf stub), then the jt308 row paint | boot.c:41681 |
| `jt3` | JT[3] | jt | JT[3] (CODE 1 + 0x158) — THINK C inline switch runtime | boot.c:5322 |
| `jt30` | JT[30] | jt | JT[30] (CODE 6 + 0x21a0) — dispose a list node and unlink it from its parent | boot.c:23396 |
| `jt303` | JT[303] | jt | JT[303] (CODE 22 + 0x2180) — the play-view status header, drawn with jt1089 | boot.c:11715 |
| `jt304` | JT[304] | jt | JT[304] (CODE 22 + 0x17ca) — the automap-view setup jt237 runs before its cell pass | boot.c:12732 |
| `jt305` | JT[305] | jt | JT[305] (CODE 22 + 0x07be, 18 sites) — set a list-row's value byte and optionally re-label + repaint its DLItem | boot.c:23619 |
| `jt306` | JT[306] | jt | JT[306] = L0716 (CODE 22 + 0x0716) — cell code B: (byte 295 >> 2) & 7. | boot.c:11231 |
| `jt307` | JT[307] | jt | JT[307] (CODE 22+0x48e4) — design-entry kind: 8 when the entry's byte 4 (the locked/flag byte) is set, else 7. | boot.c:40447 |
| `jt308` | JT[308] | jt | JT[308] (CODE 22+0x22c4) — paint one saved-game slot row | boot.c:41632 |
| `jt31` | JT[31] | jt | JT[31] (CODE 6+0x2144) — flag a creature's matching ability slot active: scan rec[198..338] for the first byte == `type`; set its high bit (type\|128) and stop | boot.c:32586 |
| `jt311` | JT[311] | jt | JT[311] (CODE 22 + 0x1a6e) — directional move on the overland / automap | boot.c:12070 |
| `jt312` | JT[312] | jt | jt312 (JT[312], CODE 22 + 0x23ee) — the dungeon-view render, the play-loop site that draws the first-person view | boot.c:11513 |
| `jt313` | — | jt | static void jt50(void); page keys — defined with the jt511 family | boot.c:18633 |
| `jt315` | CODE 22 + 0x4d8a | jt | jt315 (CODE 22 + 0x4d8a) — the main menu screen + event loop, on the shared menu_run | boot.c:19798 |
| `jt315_decorate` | CODE 22 + 0x506e | other | The main menu's decorate callback: the recessed title plate (phase 0) + the five-line banner (phase 1) | boot.c:19722 |
| `jt316` | — | jt | The -11714 design-list state record accessors (CODE 22): jt316 clears byte 0, jt317 sets byte 1, jt320 reads byte 1 (jt319, the byte-1 clear, was lifted with the band-4 batch). | boot.c:44290 |
| `jt317` | — | jt | — | boot.c:44296 |
| `jt318` | JT[318] | jt | JT[318] (CODE 22+0x0488) — current cell's terrain byte: the byte at the -11714 pointer (the cell cursor the walk loop maintains). | boot.c:49033 |
| `jt319` | JT[319] | jt | JT[319] (CODE 22+0x049c) — clear byte 1 of the -11714 record. | boot.c:41304 |
| `jt32` | JT[32] | jt | JT[32] (CODE 6+0x1238) — the HP column: draws current HP (p[395]) red (colour 11) when below max (p[129]) else white (7); `forceRed` overrides to red; when `showMax` is set, appends "/<max>" in white | boot.c:5517 |
| `jt320` | — | jt | — | boot.c:44302 |
| `jt321` | JT[321] | jt | JT[321] (CODE 22 + 0x0476) — set the byte pointed to by g_a5_-11714 to 1 (a single-flag raise through an A5 pointer slot). | boot.c:7085 |
| `jt325` | JT[325] | jt | JT[325] (CODE 9 + 0x22d8) — the design-record database engine: stage / load / store a fixed-size record (monsters, items, map tiles, ...) keyed by a record type (1/21/33/51/52) and a command (0..7) | boot.c:1486 |
| `jt327` | — | jt | — | boot.c:42324 |
| `jt328` | — | jt | — | boot.c:42631 |
| `jt33` | JT[33] | jt | JT[33] (CODE 6+0x2d8e) — can this member train a level? For each class slot 0..6 with a live level byte (rec[157+i] in 1..39): the next level's XP threshold comes from JT[26] (the L3038 XP table), and | boot.c:40676 |
| `jt338` | JT[338] | jt | JT[338] (CODE 8+0x5504) — register one play-screen command-bar menu: the 12-slot table at -10474 (count) / -10473 (style bits) / -10472 (8-byte slots: rec ptr, x, width) | boot.c:42860 |
| `jt34` | JT[34] | jt | JT[34] (CODE 6+0x11a0) — the THAC0/AC-modifier column: draws the signed value (p[385] - 60) as "\|v\| " (a trailing space), with a '-' prefix when the raw value is positive | boot.c:5499 |
| `jt343` | JT[343] | jt | JT[343] (CODE 8+0x58e0) — paint every registered command-bar menu: walk the jt338 table at -10472 (count -10474) and L35d6 each (y 8000, the slot x, width/4 in chars, the rec's label, style bit 0 as t | boot.c:44069 |
| `jt345` | JT[345] | jt | JT[345] (CODE 8+0x6dfe) — the monster-name fetch: kind 3 copies the -10508 default string (jt384), else jt366 decodes entry kind*10 + id with art-kind 13 into buf | boot.c:44092 |
| `jt346` | JT[346] | jt | JT[346] (CODE 8+0x6f9e) — decode an item-position flags byte into a slot class | boot.c:49328 |
| `jt347` | — | jt | — | boot.c:41891 |
| `jt348` | JT[348] | jt | JT[348] (CODE 8+0x6db2) — field 0 of record `n`-1 in the 20-byte record table at A5 -13038 (the jt23 record-table base); 0 when n == 0 (record ids are 1-based). | boot.c:40457 |
| `jt349` | — | jt | — | boot.c:41178 |
| `jt35` | JT[35] | jt | JT[35] (CODE 6 + 0x2f74) — the multi-class lead stat byte | boot.c:29469 |
| `jt350` | JT[350] | jt | JT[350] (CODE 8+0x66b0) — fetch a monster record: prime the kind-classed table (l5f04/l6520_c8), L6432 the record, and on a hit refresh the STRG names through jt362 (the Mac's L600c) | boot.c:44119 |
| `jt351` | JT[351] | jt | JT[351] (CODE 8+0x73f8) — fixed-arg wrapper: JT[209](1). | boot.c:40469 |
| `jt352` | — | jt | — | boot.c:42957 |
| `jt353` | — | jt | — | boot.c:40844 |
| `jt354` | L4268 | jt | static void l4268(void *rec) { PROBE("L4268"); (void)rec; } CODE 11-local | boot.c:11675 |
| `jt355` | JT[355] | jt | JT[355] (CODE 8+0x70dc) — query/show a spell school: kind 2 when the mask's bit 6 is up else 1 (L5f04 resident check; 0 on a miss) | boot.c:42911 |
| `jt356` | JT[356] | jt | JT[356] (CODE 8 + 0x5efe) — reset the design-state long g_a5_-10370. | boot.c:40082 |
| `jt357` | — | jt | — | boot.c:43059 |
| `jt358` | JT[358] | jt | JT[358] (CODE 8+0x6e4a) — read the game counter byte g_a5_-10374 (shown in the status line via jt367; gated by jt273). | boot.c:11679 |
| `jt359` | JT[359] | jt | JT[359] (CODE 8+0x6dd8) — pointer to record `n` of the -13038 record table (20-byte records, 1-based; 0 when n == 0) — jt348's address-returning sibling. | boot.c:44341 |
| `jt36` | JT[36] | jt | JT[36] (CODE 6 + 0x223c) — give a copy of `item` to `holder`: reserve a 62-byte node from the g_a5_21508 item bucket (jt477), copy the item in (jt479), clear its next ptr (node[0]), and append it to t | boot.c:33894 |
| `jt361` | JT[361] | jt | JT[361] (CODE 8 + 0x71ec) — load the design GAME header | boot.c:1714 |
| `jt362` | JT[362] | jt | JT[362] (CODE 8+0x600c) — (re)load the STRG spell-name table for group `num` into `handle` (NULL = the resident -10370 slot, only when its "%3s@%1d" name tag at handle-6 matches "STR@num" — jt393 comp | boot.c:41858 |
| `jt363` | JT[363] | jt | JT[363] (CODE 8 + 0x5f04) — load STRGnnn.DAT (a phrase table) for level `num`, returning its byte size (the on-disk count word is little-endian; size == (count+1)*14, 14 bytes per phrase record) | boot.c:1206 |
| `jt365` | CODE 8+0x5ef8 | jt | static short jt354(void) { PROBE("jt354"); return (short)jt1160(); } CODE 8+0x5ef8: thunk to JT[1160] | boot.c:11676 |
| `jt366` | JT[366] | jt | JT[366] (CODE 8+0x6594) — show monster art for `kind`/`id`: the L6520 art class primes L5f04's release of the current slot, then L62e0 binds the new entry from the -10370 group | boot.c:1411 |
| `jt367` | JT[367] | jt | JT[367] (CODE 8+0x6e78) — format counter `v` (masked to 6 bits) into buf, full lift: v > 4 -> jt394 with the -10612 A5 format string on v-4; v in 1..4 -> the -10608 format on v; v == 0 -> strcpy of th | boot.c:11685 |
| `jt37` | JT[37] | jt | JT[37] (CODE 6 + 0x1554) — attack-count recompute | boot.c:36976 |
| `jt370` | JT[370] | jt | JT[370] (CODE 8 + 0x6ed2) — kind byte -> capability flags, full lift: bit 7 when mode > 0, bit 6 when the kind byte >= 6 (the old stub doc had this reversed; the asm skips the bset on `bcs`), then the | boot.c:1336 |
| `jt376` | JT[376] | jt | jt376 — shape 7 method: the keyboard / command source | boot.c:7595 |
| `jt377` | — | jt | — | boot.c:7624 |
| `jt378` | L25ba | jt | jt378 — shape 5 method dispatcher | boot.c:7646 |
| `jt379` | — | jt | — | boot.c:7691 |
| `jt38` | — | jt | — | boot.c:23787 |
| `jt380` | L2278 | jt | jt380 — shape 3 method dispatcher | boot.c:7800 |
| `jt381` | L20c4 | jt | jt381 — shape 2 method: the radio-GROUP container (L20c4) | boot.c:7881 |
| `jt382` | — | jt | static void port_hud_text_clut(void); install UI text colours into the dungeon clut | boot.c:7971 |
| `jt383` | JT[383] | jt | JT[383] (CODE 3+0x4782) — append one byte to the buffered-output write cursor the engine keeps in the A5 long at -9168: read the cursor, advance the stored cursor, store the byte at the old position | boot.c:3875 |
| `jt384` | JT[384] | jt | JT[384] — strcpy | boot.c:3845 |
| `jt387` | JT[387] | jt | JT[387] (CODE 3 + 0x36bc) — allocate a heap block | boot.c:656 |
| `jt388` | JT[388] | jt | JT[388] (CODE 3+0x3b70) — abs() of a short. | boot.c:5483 |
| `jt389` | JT[389] | jt | JT[389] (CODE 3+0x3738) — isdigit(c): '0'..'9' -> 1, else 0. | boot.c:39930 |
| `jt39` | JT[39] | jt | JT[39] (CODE 6+0x257c) — apply `dmg` to a combatant record's HP and resolve the resulting status | boot.c:31984 |
| `jt390` | JT[390] | jt | JT[390] (CODE 3 + 0x3e3c) — scan `set` for `ch`; return a pointer to the match, or to the terminating NUL when not found (strchr that yields the NUL slot instead of NULL). | boot.c:6427 |
| `jt391` | JT[391] | jt | JT[391] (CODE 3 + 0x3702) — is_letter(ch) | boot.c:17078 |
| `jt392` | JT[392] | jt | JT[392] (CODE 3+0x351c) — create a save file from `spec`, full lift | boot.c:40032 |
| `jt393` | JT[393] | jt | JT[393] (CODE 3 + 0x3b8c) — signed strcmp (returns -1 / 0 / 1) | boot.c:38896 |
| `jt394` | JT[394] | jt | JT[394] (CODE 3 + 0x4796) — vsprintf into the caller's buffer | boot.c:26845 |
| `jt396` | JT[396] | jt | JT[396] (CODE 3 + 0x3bda) — public entry for the case-insensitive string-equal lifted as l3bda earlier in this file | boot.c:22057 |
| `jt397` | JT[397] | jt | JT[397] (CODE 3 + 0x3b4e) — signed short max(a, b) | boot.c:3997 |
| `jt398` | CODE 3 + 0x37e4 | jt | jt398 — CODE 3 + 0x37e4 | boot.c:478 |
| `jt399` | CODE 3 + 0x39d2 | jt | Local helpers and JT entries jt918 calls | boot.c:3658 |
| `jt3_dispatch` | — | other | — | boot.c:5332 |
| `jt4` | JT[4] | jt | JT[4] (CODE 1 + 0x0174, 87 callsites) — 32-bit signed multiply | boot.c:19910 |
| `jt40` | JT[40] | jt | JT[40] (CODE 6 + 0x2fd8, 38 sites) — max of two stat bytes | boot.c:27371 |
| `jt400` | — | jt | — | boot.c:27209 |
| `jt400_run` | — | other | — | boot.c:26954 |
| `jt401` | — | jt | — | boot.c:27233 |
| `jt402` | JT[402] | jt | JT[402] (CODE 3+0x45d6) — c2pstr: build the Pascal string `dst` from the C string `src` (length byte at dst[0], chars from dst[1]; the source NUL is copied through but not counted) | boot.c:3858 |
| `jt403` | JT[403] | jt | JT[403] (CODE 3+0x38ea) — query a file's length without moving its mark (Mac PBGetEOF via JT[1047]) | boot.c:46119 |
| `jt404` | JT[404] | jt | JT[404] (CODE 3 + 0x3976, 34 sites) — strcat | boot.c:4106 |
| `jt405` | JT[405] | jt | JT[405] (CODE 3+0x46d8) — uppercase a C string in place: each lowercase byte gets -32 added (the asm's addib #-32). | boot.c:40397 |
| `jt406` | JT[406] | jt | JT[406] (CODE 3 + 0x366a) — overlap-safe block copy | boot.c:3687 |
| `jt407` | CODE 3 + 0x3c34 | jt | jt407 (CODE 3 + 0x3c34) — filename prefix test used by the load picker: non-zero when `name` begins with `prefix` (the "SavGam" save slots) | boot.c:28910 |
| `jt408` | JT[408] | jt | JT[408] (CODE 3+0x466a) — isupper('A'..'Z'). | boot.c:39934 |
| `jt41` | JT[41] | jt | JT[41] (CODE 6 + 0x2526) — linked-list search by first-byte key | boot.c:22634 |
| `jt410` | — | jt | — | boot.c:27259 |
| `jt412` | JT[412] | jt | JT[412] (CODE 3+0x3888) — seek (mode 0 = from start, 1 = from mark, 2 = from end); returns the new absolute position, or -1 | boot.c:40277 |
| `jt413` | — | jt | — | boot.c:4003 |
| `jt414` | JT[414] | jt | JT[414] (CODE 3+0x3d98) — read `count` bytes from open file `refnum` into `buf` | boot.c:45689 |
| `jt415` | JT[415] | jt | JT[415] (CODE 3 + 0x37da) — wraps _ExitToShell: terminate now | boot.c:526 |
| `jt416` | JT[416] | jt | JT[416] (CODE 3+0x35d6) — delete the file named by C string `spec`, full lift: L45d6 converts to a Pascal string, JT[1054] (the _Delete Pascal trap glue at CODE 5+0x5b74) = the shim's FSDelete(name, 0 | boot.c:40058 |
| `jt419` | JT[419] | jt | JT[419] (CODE 3 + 0x3c7e) — set/replace a filename's extension in place | boot.c:27695 |
| `jt42` | JT[42] | jt | JT[42] (CODE 6 + 0x22a6) — append a message to the play window's scrolling text area | boot.c:27400 |
| `jt420` | JT[420] | jt | JT[420] (CODE 3+0x4648) — islower('a'..'z'). | boot.c:39938 |
| `jt421` | JT[421] | jt | JT[421] (CODE 3 + 0x36d6) — NewPtr(size) | boot.c:750 |
| `jt422` | JT[422] | jt | JT[422] (CODE 3 + 0x468c) — toupper(ch) | boot.c:17093 |
| `jt423` | — | jt | — | boot.c:4009 |
| `jt425` | JT[425] | jt | JT[425] (CODE 3 + 0x4ca0) — copy `name`'s file extension (the text after the last '.') into `out`; empty if there's none, or a ':' is hit first | boot.c:40091 |
| `jt429` | JT[429] | jt | JT[429] (CODE 3+0x4a28) — both the -9163 and -9164 flags up? | boot.c:41295 |
| `jt431` | JT[431] | jt | JT[431] (CODE 3 + 0x4b8e, 42 sites) — HFS path-concat with ':' | boot.c:22069 |
| `jt433` | JT[433] | jt | JT[433] (CODE 3+0x49a2) — emit one character to the message window/print stream | boot.c:40486 |
| `jt437` | — | jt | JT[437..441] (CODE 3) — the thin event-queue wrappers l036a/jt987 poll. | boot.c:46296 |
| `jt438` | — | jt | — | boot.c:46298 |
| `jt439` | — | jt | — | boot.c:46300 |
| `jt44` | CODE 6+0x5822 | jt | static void l5822(void); CODE 6+0x5822 — full backdrop refresh (defined below) | boot.c:2689 |
| `jt440` | — | jt | — | boot.c:46303 |
| `jt441` | — | jt | — | boot.c:46308 |
| `jt442` | — | jt | Stubbed as no-ops so core_init() reads in its true shape; each is replaced when its segment is lifted (the fc_dump no-op-stub pattern). | core.c:98 |
| `jt444` | JT[444] | jt | JT[444] (CODE 3 + 0x3056, 96 callsites) — DLItem method dispatch | boot.c:7024 |
| `jt445` | JT[445] | jt | JT[445] (CODE 3 + 0x294e) — Mac body is empty (just rts) | boot.c:521 |
| `jt447` | JT[447] | jt | JT[447] (CODE 3 + 0x298a) — DLItem manager state reset | boot.c:7055 |
| `jt448` | JT[448] | jt | JT[448] (CODE 3 + 0x148a) — blit glyph `glyph` (a font index) at (x,y) in colour | boot.c:12930 |
| `jt449` | CODE 3+0x2c60 | jt | static void l2c60(short force_paint); defined below (CODE 3+0x2c60) | boot.c:7068 |
| `jt450` | JT[450] | jt | JT[450] (CODE 3+0x2950) — the -9282 callback registry: return slot `n` (1-based), installing `val` first when non-zero. | boot.c:48479 |
| `jt451` | — | jt | static void l30ba(short start, short end, short cmd); defined below | boot.c:7075 |
| `jt452` | — | jt | Faithful Mac stream parser | boot.c:15631 |
| `jt452_init` | — | other | g_a5_9254 / g_a5_9250 / g_a5_9288 / g_a5_9248 — all macros over the data_pool replay buffer; capacity (=64) and active-flag (=1) are seeded by boot_a5_seed_defaults() in main.c's startup path. | boot.c:8304 |
| `jt453` | — | jt | g_a5_9247 → macro (data_pool replay buffer) | boot.c:18432 |
| `jt454` | JT[454] | jt | JT[454] (CODE 3+0x3108) — DLItem QUERY by (item, cmd): cmds 16..22 test rec[28] bit (cmd-16); cmd 36 returns the word at rec[24]; cmd 42 the word at rec[22]; anything else 0 | boot.c:49136 |
| `jt455` | JT[455] | jt | JT[455] (CODE 3 + 0x2c5a) — return the live DLItem count (g_a5_-9250). | boot.c:39018 |
| `jt459` | JT[459] | jt | JT[459] (CODE 3+0xd44) — size query over the pool | boot.c:44430 |
| `jt46` | L534a | jt | jt46 IS L534a (same address, same signature); delegate | boot.c:39096 |
| `jt460` | JT[460] | jt | JT[460] (CODE 3+0xc0a) — append `length` raw bytes from the open refnum onto the current group | boot.c:44624 |
| `jt461` | — | jt | g_a5_10074 → macro array (data_pool replay buffer) | boot.c:3956 |
| `jt462` | JT[462] | jt | JT[462] (CODE 3+0xb16) — drop the in-progress (last) group: decrement the count, unmap any freemap entry that referenced it, and shift the g_a5_9354 priority companion down one | boot.c:44680 |
| `jt463` | JT[463] | jt | JT[463] (CODE 3+0x538) — _LBOpen: stand up the FAR pool | boot.c:45435 |
| `jt464` | JT[464] | jt | JT[464] (CODE 3+0x644) — register a named library under a group id, the FAR pool's group-creation | boot.c:45927 |
| `jt465` | JT[465] | jt | JT[465] — flush records by key | boot.c:4364 |
| `jt467` | JT[467] | jt | JT[467] (CODE 3+0x07d0) — append `n` bytes from `src` to the in-progress FAR-pool group, full lift: the current group size is slot[count] - slot[count-1]; l0ab8 extends the region (0 when the pool can | boot.c:45819 |
| `jt468` | — | jt | static long port_ui_group_base(short group); groups 0/1 -> ALWAYS/FRAME GLIB | boot.c:4599 |
| `jt469` | JT[469] | jt | JT[469] (CODE 3+0xf2a) — FCInsDel: insert (`length` > 0) or delete (`length` < 0) a span at `offset` inside the group bound to freemap id `id`, full lift | boot.c:44582 |
| `jt471` | JT[471] | jt | JT[471] (CODE 3 + 0x02e8) — slot free (sister of JT[477] right before it in CODE 3) | boot.c:22097 |
| `jt472` | JT[472] | jt | JT[472] (CODE 3 + 0x035e) — test the low bit of a word (odd/even). | boot.c:28649 |
| `jt475` | JT[475] | jt | JT[475] (CODE 3+0x03da) — indexed string-table get with fallback: entry `idx` of the pointer table at A5 -10280 (count at -10276), or the empty STRS string at +0x41c0 when the table is absent, the ind | boot.c:40624 |
| `jt476` | JT[476] | jt | JT[476] (CODE 3 + 0x046a, called by jt102/jt92/jt100) — tick-paced wait | boot.c:23555 |
| `jt477` | JT[477] | jt | JT[477] (CODE 3 + 0x0214, 65 sites) — slot reserve from a bucket | boot.c:22137 |
| `jt478` | JT[478] | jt | JT[478] (CODE 3+0x6) — sprintf("%d") of a word into the caller buffer. | boot.c:5477 |
| `jt479` | JT[479] | jt | JT[479] (CODE 3 + 0x036c) — block copy via the memmove core L366a, in true Mac BlockMove(src, dst, count) order | boot.c:24047 |
| `jt481` | JT[481] | jt | JT[481] (CODE 3 + 0x01ba) — in-place delete of `count` chars from `str` starting at 1-based `offset` | boot.c:31726 |
| `jt482` | JT[482] | jt | JT[482] (CODE 3 + 0x0024) — substring extract into g_a5_-10362 | boot.c:31695 |
| `jt483` | JT[483] | jt | JT[483] (CODE 3 + 0x0058, 36 sites) — duplicate public alias for strlen-as-short | boot.c:4019 |
| `jt484` | JT[484] | jt | JT[484] (CODE 3 + 0x158) — insert string `ins` into `target` at 1-based position `pos`: open a gap of len(ins) by shifting the tail right (l366a), then copy `ins` into it | boot.c:31745 |
| `jt485` | JT[485] | jt | JT[485] (CODE 3 + 0x0388) — thin wrapper over JT[1083] | boot.c:4065 |
| `jt486` | JT[486] | jt | JT[486] (CODE 3+0x039a) — present (jt117) then poll for a pending event (jt1118); returns the poll result. | boot.c:44279 |
| `jt488` | — | jt | stdarg.h / stdio.h moved to the top so jt452 can use va_list too. | boot.c:26816 |
| `jt49` | JT[49] | jt | JT[49] (CODE 6 + 0x5730) — blit the cached overlay picture (-24320 slot) at the given frame/depth, then commit its palette | boot.c:46763 |
| `jt490` | JT[490] | jt | JT[490] (CODE 13 + 0x242c) — recount the per-side combatant tallies g_a5_25298/g_a5_25297: walk the -27928 party list and bump the side counter (indexed by rec[95], the combat-side byte) for every in | boot.c:34210 |
| `jt491` | JT[491] | jt | JT[491] (CODE 13+0x2bde) — resolve the item a combat member actually deals damage with: member[12] is the wielded item; its item-type row (the -27944 table, 16-byte stride) carries flags in byte 14 | boot.c:35905 |
| `jt492` | — | jt | — | boot.c:49206 |
| `jt493` | JT[493] | jt | JT[493] (CODE 13 + 0x2406) — predicate: return 1 when the record's byte at offset 95 is zero, else 0. | boot.c:7094 |
| `jt494` | JT[494] | jt | JT[494] (CODE 13 + 0x25f4) — path distance (in whole cells) from `rec_l`'s combat position to entity `ent_l`: rebuild the -19170 list around rec's own footprint with occupancy blocking off (map header | boot.c:24421 |
| `jt495` | — | jt | — | boot.c:6673 |
| `jt497` | JT[497] | jt | JT[497] (CODE 13 + 0x18de, 17 sites) — install actor `actor`'s 2x2 large- sprite overlay: four jt495 tiles (glyphs 77..80), committing the middle two | boot.c:6689 |
| `jt498` | JT[498] | jt | JT[498] (CODE 13 + 0x26ea) — reset the combat-actor's order block (rec->[64]): clear the queued-move word (+4) and the state bytes (+0, +8 guard, +9) | boot.c:23531 |
| `jt499` | JT[499] | jt | JT[499] (CODE 13+0x279c) — does combat record `rec` have a multi-unit kind? rec+12 -> aux record; aux[40] indexes the 16-byte entries at A5 -27944; true when entry[12] > 1. | boot.c:40639 |
| `jt5` | JT[5] | jt | JT[5] / JT[6] / JT[7] / JT[8] (CODE 1 + 0x1aa..0x20c) — 32-bit div runtime helpers | boot.c:19930 |
| `jt50` | JT[50] | jt | JT[50] (CODE 6+0x5ac2) / JT[51] (CODE 6+0x5ad8) / JT[64] (CODE 6+0x5f3a) — the pause/page-key handlers (combat-loop keys 338/339 and Esc/`) | boot.c:32238 |
| `jt500` | — | jt | — | boot.c:49372 |
| `jt501` | — | jt | — | boot.c:26415 |
| `jt502` | JT[502] | jt | JT[502] (CODE 13+0x2b2c) — set the projectile-sprite trail for direction `dir`, full lift over the L1888 leaf stub: diagonal directions (jt472) pick bank 1/0 with the mirror flag for 5/7; cardinals de | boot.c:37973 |
| `jt503` | JT[503] | jt | JT[503] (CODE 13 + 0x21ec) — report a combat effect result on screen | boot.c:24835 |
| `jt504` | JT[504] | jt | JT[504] (CODE 13+0x27e6) — JT[499] AND the entry's flag byte 14 has both bits of mask 20 (0x14) set. | boot.c:40655 |
| `jt505` | — | jt | — | boot.c:24207 |
| `jt506` | — | jt | — | boot.c:24154 |
| `jt507` | JT[507] | jt | JT[507] (CODE 13 + 0x6f68) — one Bresenham step along the dominant axis (cost 2; +1 when the minor axis also steps) | boot.c:24103 |
| `jt508` | JT[508] | jt | JT[508] (CODE 13 + 0x76da) — build the -19170 target list around the source footprint `shape` at (x, y): for every placed combatant (g_a5_27472 row m, footprint cells via l5d92/JT[517]), take the chea | boot.c:24328 |
| `jt509` | JT[509] | jt | JT[509] (CODE 13 + 0x6eba) — init the walker from x0/y0/x1/y1. | boot.c:24085 |
| `jt51` | — | jt | — | boot.c:32239 |
| `jt511` | JT[511] | jt | JT[511] (CODE 13+0x05a6) — the COMBAT MAIN LOOP (and the other half of the -24070 registration: installs jt538 at entry, l0006 restores jt601 on the way out) | boot.c:32362 |
| `jt513` | JT[513] | jt | JT[513] (CODE 14 + 0x6b94) — field 5 of the same 6-byte id-table row (the item "category" byte L026e branches on) | boot.c:20015 |
| `jt515` | JT[515] | jt | JT[515] / L6c22 (CODE 14 + 0x6c22) — scan the six combat-grid directions out of `entity`'s cell for the best adjacent target/feature (used by the effect applicator jt879) | boot.c:25197 |
| `jt519` | JT[519] | jt | JT[519] (CODE 14+0x6bbe) — find a combatant in the active-actor table: scan the -25676 long-table (1-based) for the entry == `key`, stopping at the table count (-27468) | boot.c:38412 |
| `jt52` | JT[52] | jt | JT[52] (CODE 6 + 0x5888) — the sound/music command dispatcher | boot.c:17933 |
| `jt520` | JT[520] | jt | JT[520] (CODE 14+0x6de8) — area-map combat cleanup leaf; PROBE stub until the CODE 14 combat-status block is lifted (called by jt860). | boot.c:32439 |
| `jt521` | JT[521] | jt | JT[521] (CODE 14 + 0x6836, 17 sites) — scroll/redraw the overland-map view one step in direction `dir` from base cell (bx,by) | boot.c:25837 |
| `jt522` | JT[522] | jt | JT[522] (CODE 14+0x7488) — combat-map occupant-class fetch for a cell | boot.c:30459 |
| `jt523` | JT[523] | jt | JT[523] (CODE 14 + 0x6a10) — set a combatant's pose/facing byte and keep the combat view current | boot.c:25686 |
| `jt524` | JT[524] | jt | JT[524] / L61ae (CODE 14 + 0x61ae) — rebuild the 50x25 combat occupancy grid (g_a5_26922): clear it (jt65), then for every active actor record (g_a5_27472, 6 bytes: x word @0, y word @2, facing @5) st | boot.c:25255 |
| `jt525` | JT[525] | jt | JT[525] (CODE 14 + 0x6b40, 52 sites) — field 1 of the record matching key | boot.c:19997 |
| `jt526` | — | jt | — | boot.c:25323 |
| `jt527` | — | jt | — | boot.c:39907 |
| `jt529` | JT[529] | jt | JT[529] (CODE 14 + 0x7894) — facing octant from combatant a toward combatant b: placement coords (jt525/jt531, sign-extended) through the l7638 compass | boot.c:25667 |
| `jt53` | — | jt | — | boot.c:41748 |
| `jt530` | JT[530] | jt | JT[530] / L73cc (CODE 14 + 0x73cc) — stage a combat-status redraw of `entity` in the live-map header (g_a5_25318): byte 6 = `c`, byte 7 = the actor's facing; when scroll-into-view (g_a5_22626) is on, | boot.c:25290 |
| `jt531` | — | jt | — | boot.c:20005 |
| `jt537` | JT[537] | jt | JT[537] (CODE 14 + 0x5670) — the BEHOLDER: up to four eye-ray attacks per activation, a used-ray bitmask making each ray fire at most once | boot.c:38253 |
| `jt538` | — | jt | — | boot.c:30018 |
| `jt539` | JT[539] | jt | JT[539] (CODE 14 + 0x3b6c) — the in-combat interactive crosshair pick: arms the -24140 pick mode, runs the cursor UI over the combat map out to `range` cells from `actor`, and fills `buf` (rec + cell) | boot.c:29908 |
| `jt54` | JT[54] | jt | JT[54] (CODE 6+0x5af8) — load + show picture library `name` (kind b) through the binder: L338c(50), optional picture slot select (-14682 = c when non-zero), L33ac into the -27870 slot, then L3eea comm | boot.c:49066 |
| `jt541` | JT[541] | jt | JT[541] (CODE 14+0x0006) — per-member per-round prep. | boot.c:32231 |
| `jt542` | JT[542] | jt | JT[542] (CODE 14+0x5434) — combat-field setup run once at entry. | boot.c:32228 |
| `jt543` | JT[543] | jt | JT[543] (CODE 14+0x0f60) — resolve the actor's attacks-per-round (rec[387]), full lift over the L1090 table leaf stub | boot.c:43728 |
| `jt545` | JT[545] | jt | JT[545] (CODE 14+0x43be) — "The Gods intervene!" combat wipeout, full lift | boot.c:43787 |
| `jt546` | JT[546] | jt | JT[546] (CODE 14+0x4186) — pick the next combat target into the actor's sub-record slot +12, full lift over the L10c4 LOS leaf stub | boot.c:37840 |
| `jt548` | — | jt | — | boot.c:39273 |
| `jt55` | JT[55] | jt | JT[55] (CODE 6+0x5b5e) — release one combat resource slot, full lift: free piece `id` of the -27866 combat overlay GLIB, and (for ids < 38) its paired piece at id+38 (the second art bank) | boot.c:32261 |
| `jt555` | JT[555] | jt | JT[555] (CODE 14+0x19dc) — execute one melee/missile attack, full lift over the L7894/L14bc/L2b24 leaf stubs | boot.c:37054 |
| `jt556` | — | jt | — | boot.c:22659 |
| `jt557` | — | jt | — | boot.c:22701 |
| `jt558` | JT[558] | jt | JT[558] (CODE 17 + 0x6bee) — multi-class active-slot finder | boot.c:20932 |
| `jt559` | — | jt | — | boot.c:39274 |
| `jt56` | JT[56] | jt | JT[56] (CODE 6+0x5baa) — load + show a numbered body/portrait picture library, full lift | boot.c:22404 |
| `jt560` | — | jt | — | boot.c:22965 |
| `jt562` | JT[562] | jt | JT[562] (CODE 17 + 0x854) — grid mouse-click proc | boot.c:23171 |
| `jt563` | JT[563] | jt | JT[563] (CODE 17 + 0xfc8) — grid keyboard proc | boot.c:23210 |
| `jt564` | JT[564] | jt | JT[564] (CODE 17 + 0x9ce) — grid "Exit" button proc: cancel. | boot.c:23001 |
| `jt565` | JT[565] | jt | JT[565] (CODE 17 + 0x9c6) — grid "Done" button proc: accept. | boot.c:23010 |
| `jt566` | JT[566] | jt | JT[566] (CODE 17 + 0x3222) — the RACE list action proc | boot.c:20857 |
| `jt567` | JT[567] | jt | JT[567] (CODE 17 + 0x3372) — the GENDER list action proc: store the picked gender (g_a5_-7020, 1-based) into rec[92]. | boot.c:20735 |
| `jt568` | — | jt | — | boot.c:20648 |
| `jt569` | JT[569] | jt | JT[569] (CODE 17 + 0x336c) — alignment axis-1 (law/chaos) action proc: recompute the linear alignment index. | boot.c:20745 |
| `jt57` | — | jt | — | boot.c:6715 |
| `jt570` | JT[570] | jt | JT[570] (CODE 17 + 0x334c) — alignment axis-2 (good/evil) action proc: redraw the list highlight, set DLItem 32 to the axis-2 row, recompute the linear alignment index. | boot.c:20755 |
| `jt571` | JT[571] | jt | JT[571] (CODE 17 + 0x2f6c) — the "Exit" button action proc: set the cancel/result flag g_a5_-7038 = 1 so L3666's poll (jt453) ends. | boot.c:20767 |
| `jt572` | JT[572] | jt | JT[572] (CODE 17 + 0x2e6c) — the "Done" button finalize: commit the picked character | boot.c:20897 |
| `jt573` | JT[573] | jt | JT[573] (CODE 17 + 0x1346 = L1346) — the REVIEW / MODIFY screen driver | boot.c:23310 |
| `jt574` | JT[574] | jt | JT[574] (CODE 17 + 0x3b5e) — the character create/train entry (l0f1a / case 0) | boot.c:21578 |
| `jt577` | JT[577] | jt | JT[577] (CODE 15 + 0x3fe) — deserialize a character record (read mirror of jt578) from the open file `refNum` into g_a5_-6902 | boot.c:27918 |
| `jt578` | JT[578] | jt | JT[578] (CODE 15 + 0x934) — serialize the current character record (set in g_a5_-6902) to the open file `refNum` | boot.c:27819 |
| `jt579` | CODE 15 + 0x124c | jt | jt579 (CODE 15 + 0x124c) — the LOAD serializer, mirror of jt580: read the game state back via jt401 (FSRead) | boot.c:27754 |
| `jt58` | JT[58] | jt | JT[58] (CODE 6+0x5eba) — combat-screen art teardown, full lift: release the -27870 slot through L31dc (jt461 on the live tag, then tag = -1, pointer cleared). | boot.c:32275 |
| `jt580` | CODE 15 + 0x182c | jt | jt580 (CODE 15 + 0x182c) — the SAVE serializer callback: stream the game state into the open slot file via jt410 (FSWrite) | boot.c:27738 |
| `jt581` | CODE 7+0x18d4 | jt | static void jt147(void *headp); CODE 7+0x18d4, defined below | boot.c:22179 |
| `jt582` | CODE 15 + 0x153e | jt | jt582 (CODE 15 + 0x153e) — LOAD a saved game | boot.c:28974 |
| `jt584` | JT[584] | jt | JT[584] (CODE 15 + 0xb5a) — save a character record to its .cch file | boot.c:28081 |
| `jt585` | JT[585] | jt | JT[585] (CODE 15 + 0x1a24, 104 lines) — save / load slot picker | boot.c:28838 |
| `jt589` | CODE 15 + 0x362 | jt | jt589 (CODE 15 + 0x362) — roster-list builder entry | boot.c:22361 |
| `jt59` | JT[59] | jt | JT[59] (CODE 6+0x60d4) — format `value` into the A5 -13073 scratch buffer via JT[478] (short -> decimal string) and return the buffer pointer. | boot.c:40411 |
| `jt590` | — | jt | — | boot.c:22379 |
| `jt593` | JT[593] | jt | JT[593] (CODE 15+0x03d2) — show the active member's (-27932) body picture: jt56("CBODYS", rec[188], rec[189]). | boot.c:22458 |
| `jt595` | JT[595] | jt | JT[595] (CODE 16+0x7284) — the SPELL LIST dialog | boot.c:39875 |
| `jt596` | — | jt | — | boot.c:30292 |
| `jt598` | JT[598] | jt | JT[598] (CODE 16 + 0x73ea) — dispatch a spell/status-effect handler: call through the -24066 fn-ptr table slot `code` (the table jt610 fills at combat init; empty until that registration is lifted). | boot.c:30436 |
| `jt599` | JT[599] | jt | JT[599] (CODE 16+0x64a8) — cast a spell effect by id, full lift (~1.25KB; the beholder self-casts 84/55/21 through it, and the Cast command's pipeline ends here) | boot.c:38028 |
| `jt6` | — | jt | — | boot.c:19937 |
| `jt600` | JT[600] | jt | JT[600] (CODE 16 + 0x5f26) — spell range, in combat-map cells, from the -16906 hazard row for `code`: byte2 = base, byte3 = per-level multiplier | boot.c:29878 |
| `jt601` | — | jt | — | boot.c:29817 |
| `jt602` | JT[602] | jt | JT[602] (CODE 16+0x0a62; handler ids 34/91/101) — spawn a lingering area hazard (wall of fire / cloudkill family), full lift over the jt522 leaf stub | boot.c:30483 |
| `jt603` | — | jt | — | boot.c:30569 |
| `jt604` | — | jt | static void jt603(void) { PROBE("jt603"); } +0x2806; id 84 | boot.c:30570 |
| `jt605` | — | jt | static void jt604(void) { PROBE("jt604"); } +0x38d6; id 117 | boot.c:30571 |
| `jt607` | JT[607] | jt | JT[607] (CODE 16+0x16de; the area-damage handler) — underwater vetoes everything except effect 51 ("That has no effect underwater!") | boot.c:30591 |
| `jt608` | — | jt | — | boot.c:30636 |
| `jt61` | JT[61] | jt | JT[61] (CODE 6+0x6076) — reserve one 62-byte node from the -21508 item bucket (JT[477]) and return its pointer (0 when the bucket is full) — the allocation half of jt36's give-item. | boot.c:40422 |
| `jt610` | JT[610] | jt | JT[610] (CODE 16 + 0x4b48) — combat-init registration: install jt601 as the -24070 targeting callback, clear -25258/-23230, set -25256 = 1, then fill the -24066 spell-effect handler table (ids 45..137 | boot.c:30957 |
| `jt611` | — | jt | — | boot.c:30644 |
| `jt612` | — | jt | static void jt611(void) { PROBE("jt611"); } +0x2f2e; id 98 | boot.c:30645 |
| `jt613` | — | jt | static void jt612(void) { PROBE("jt612"); } +0x4338; id 125 | boot.c:30646 |
| `jt614` | — | jt | static void jt613(void) { PROBE("jt613"); } +0x07f2; id 26 | boot.c:30647 |
| `jt615` | — | jt | static void jt614(void) { PROBE("jt614"); } +0x030e; id 12 | boot.c:30648 |
| `jt616` | — | jt | static void jt615(void) { PROBE("jt615"); } +0x2634; id 81 | boot.c:30649 |
| `jt617` | — | jt | static void jt616(void) { PROBE("jt616"); } +0x2242; id 70 | boot.c:30650 |
| `jt618` | — | jt | static void jt617(void) { PROBE("jt617"); } +0x1ee2; id 58 | boot.c:30651 |
| `jt619` | — | jt | static void jt618(void) { PROBE("jt618"); } +0x3218; id 103 | boot.c:30652 |
| `jt620` | — | jt | static void jt619(void) { PROBE("jt619"); } +0x0da4; id 35 | boot.c:30653 |
| `jt621` | — | jt | static void jt620(void) { PROBE("jt620"); } +0x0122; id 3 | boot.c:30654 |
| `jt622` | — | jt | static void jt621(void) { PROBE("jt621"); } +0x2e08; id 135 | boot.c:30655 |
| `jt623` | JT[623] | jt | JT[623] (CODE 16+0x0756; the Hold handler) — the saving-throw modifier scales with the target count (-23510): 1 target = -3 (-2 for effect 23), 2 = -1, 3/4 = 0 (the Mac's default arm reads an uninitia | boot.c:30662 |
| `jt626` | — | jt | — | boot.c:30696 |
| `jt627` | — | jt | static void jt626(void) { PROBE("jt626"); } +0x2552; id 78 | boot.c:30697 |
| `jt628` | — | jt | static void jt627(void) { PROBE("jt627"); } +0x20b6; id 66 | boot.c:30698 |
| `jt629` | — | jt | static void jt628(void) { PROBE("jt628"); } +0x1dfa; id 55 | boot.c:30699 |
| `jt630` | — | jt | static void jt629(void) { PROBE("jt629"); } +0x155e; id 42 | boot.c:30700 |
| `jt631` | JT[631] | jt | JT[631] (CODE 16 + 0x19c8) — the bolt damage applier: a bouncing ray from the caster through the pick, damaging occupants along the reflected path (jt509/jt507 walks + l62ec occupant checks + the L1d2 | boot.c:31004 |
| `jt633` | — | jt | — | boot.c:30708 |
| `jt635` | — | jt | — | boot.c:30716 |
| `jt636` | — | jt | static void jt635(void) { PROBE("jt635"); } +0x2960; id 85 | boot.c:30717 |
| `jt637` | — | jt | static void jt636(void) { PROBE("jt636"); } +0x239c; id 74 | boot.c:30718 |
| `jt638` | JT[638] | jt | JT[638] (CODE 16+0x46ca) — is the record an undead-ish type? 1 when rec[40] is 39, 40 or 73; 0 otherwise (or rec NULL). | boot.c:49050 |
| `jt639` | — | jt | static void jt637(void) { PROBE("jt637"); } +0x1fce; id 131 | boot.c:30719 |
| `jt64` | — | jt | — | boot.c:32240 |
| `jt640` | — | jt | static void jt639(void) { PROBE("jt639"); } +0x4458; id 126 | boot.c:30720 |
| `jt641` | — | jt | static void jt640(void) { PROBE("jt640"); } +0x1168; id 39,106 | boot.c:30721 |
| `jt642` | — | jt | static void jt641(void) { PROBE("jt641"); } +0x2f64; id 137 | boot.c:30722 |
| `jt643` | — | jt | static void jt642(void) { PROBE("jt642"); } +0x089a; id 27 | boot.c:30723 |
| `jt644` | — | jt | static void jt643(void) { PROBE("jt643"); } +0x0468; id 13 | boot.c:30724 |
| `jt645` | — | jt | static void jt644(void) { PROBE("jt644"); } +0x323c; id 104 | boot.c:30725 |
| `jt646` | — | jt | static void jt645(void) { PROBE("jt645"); } +0x26b2; id 82 | boot.c:30726 |
| `jt647` | — | jt | static void jt646(void) { PROBE("jt646"); } +0x22e8; id 71 | boot.c:30727 |
| `jt648` | — | jt | static void jt647(void) { PROBE("jt647"); } +0x1f1a; id 128 | boot.c:30728 |
| `jt649` | — | jt | static void jt648(void) { PROBE("jt648"); } +0x3424; id 110 | boot.c:30729 |
| `jt65` | JT[65] | jt | JT[65] (CODE 6 + 0x5f4e) — zero-fill `size` bytes at `ptr` (jt399 with fill 0). | boot.c:23492 |
| `jt650` | — | jt | static void jt649(void) { PROBE("jt649"); } +0x0ff6; id 36 | boot.c:30730 |
| `jt651` | — | jt | static void jt650(void) { PROBE("jt650"); } +0x0158; id 4 | boot.c:30731 |
| `jt652` | — | jt | static void jt651(void) { PROBE("jt651"); } +0x2e28; id 96 | boot.c:30732 |
| `jt653` | — | jt | static void jt652(void) { PROBE("jt652"); } +0x19aa; id 48 | boot.c:30733 |
| `jt655` | — | jt | — | boot.c:30741 |
| `jt657` | — | jt | — | boot.c:30749 |
| `jt658` | — | jt | static void jt657(void) { PROBE("jt657"); } +0x2604; id 79 | boot.c:30750 |
| `jt659` | — | jt | static void jt658(void) { PROBE("jt658"); } +0x20e8; id 67,107 | boot.c:30751 |
| `jt66` | JT[66] | jt | JT[66] (CODE 6 + 0x6048 -> L604e) — post-action input pump: drain a pending key (via the L5f84 reader when JT[1118] reports one) and refresh the IKBD / cursor with JT[1125](7) | boot.c:29391 |
| `jt661` | — | jt | — | boot.c:43490 |
| `jt663` | — | jt | — | boot.c:30766 |
| `jt664` | — | jt | static void jt663(void) { PROBE("jt663"); } +0x09e0; id 32 | boot.c:30767 |
| `jt665` | — | jt | static void jt664(void) { PROBE("jt664"); } +0x00c6; id 1,62 | boot.c:30768 |
| `jt666` | — | jt | static void jt665(void) { PROBE("jt665"); } +0x3d02; id 123 | boot.c:30769 |
| `jt667` | — | jt | static void jt666(void) { PROBE("jt666"); } +0x05aa; id 20 | boot.c:30770 |
| `jt668` | — | jt | static void jt667(void) { PROBE("jt667"); } +0x2ab8; id 86 | boot.c:30771 |
| `jt669` | — | jt | static void jt668(void) { PROBE("jt668"); } +0x23ea; id 75 | boot.c:30772 |
| `jt67` | JT[67] | jt | JT[67] (CODE 6+0x5f48) — combat-abort poll: the -13084 flag byte (the Mac body is moveb a5@(-13084),d0; rts — the old stub's constant 0 replaced with the real read). | boot.c:32245 |
| `jt671` | — | jt | static void jt669(void) { PROBE("jt669"); } +0x2008; id 132 | boot.c:30773 |
| `jt672` | — | jt | static void jt671(void) { PROBE("jt671"); } +0x2f9e; id 100 | boot.c:30774 |
| `jt673` | — | jt | static void jt672(void) { PROBE("jt672"); } +0x118a; id 40 | boot.c:30775 |
| `jt674` | JT[674] | jt | JT[674] (CODE 16+0x0188; handler ids 5,11,18,22,29,60,77) — the "is affected" announcer: L6114 on the -25262 current target with no stat deltas | boot.c:30785 |
| `jt675` | — | jt | — | boot.c:30791 |
| `jt676` | — | jt | static void jt675(void) { PROBE("jt675"); } +0x096a; id 28 | boot.c:30792 |
| `jt677` | — | jt | static void jt676(void) { PROBE("jt676"); } +0x04ec; id 14 | boot.c:30793 |
| `jt678` | — | jt | static void jt677(void) { PROBE("jt677"); } +0x355e; id 111 | boot.c:30794 |
| `jt679` | — | jt | static void jt678(void) { PROBE("jt678"); } +0x2776; id 83 | boot.c:30795 |
| `jt680` | — | jt | static void jt679(void) { PROBE("jt679"); } +0x2320; id 72 | boot.c:30796 |
| `jt681` | — | jt | static void jt680(void) { PROBE("jt680"); } +0x1f96; id 129 | boot.c:30797 |
| `jt682` | — | jt | static void jt681(void) { PROBE("jt681"); } +0x381a; id 116 | boot.c:30798 |
| `jt683` | — | jt | static void jt682(void) { PROBE("jt682"); } +0x111e; id 37 | boot.c:30799 |
| `jt684` | — | jt | static void jt683(void) { PROBE("jt683"); } +0x2f0e; id 136 | boot.c:30800 |
| `jt686` | — | jt | — | boot.c:30808 |
| `jt687` | JT[687] | jt | JT[687] (CODE 16+0x15ae; handler ids 43,61,89) — Remove Curse, full lift | boot.c:30818 |
| `jt688` | — | jt | — | boot.c:30855 |
| `jt69` | JT[458] | jt | static void fc_dump(long need); JT[458]/L0846 — defined in the FC cluster | boot.c:879 |
| `jt690` | — | jt | — | boot.c:30863 |
| `jt691` | — | jt | static void jt690(void) { PROBE("jt690"); } +0x21c8; id 68 | boot.c:30864 |
| `jt693` | — | jt | — | boot.c:30872 |
| `jt695` | — | jt | — | boot.c:30880 |
| `jt696` | — | jt | static void jt695(void) { PROBE("jt695"); } +0x00ec; id 2 | boot.c:30881 |
| `jt697` | — | jt | static void jt696(void) { PROBE("jt696"); } +0x2d18; id 93 | boot.c:30882 |
| `jt698` | — | jt | static void jt697(void) { PROBE("jt697"); } +0x2fbe; id 102 | boot.c:30883 |
| `jt699` | — | jt | +0x01a8; ids 6,7,16,17,52,53,54,57,69,122 — the shared "is protected" handler: l6114(-25262 hazard id, 0,0,0,0, the message). | boot.c:30898 |
| `jt7` | — | jt | — | boot.c:19944 |
| `jt70` | JT[70] | jt | JT[70] (CODE 6+0x60f0) — format unsigned long `v` into the -13061 scratch ("%lu" via JT[394]) and return the pointer. | boot.c:41332 |
| `jt700` | — | jt | — | boot.c:30903 |
| `jt701` | — | jt | static void jt700(void) { PROBE("jt700"); } +0x2b90; id 87 | boot.c:30904 |
| `jt702` | — | jt | static void jt701(void) { PROBE("jt701"); } +0x24a0; id 76 | boot.c:30905 |
| `jt703` | — | jt | static void jt702(void) { PROBE("jt702"); } +0x2084; id 134 | boot.c:30906 |
| `jt704` | — | jt | static void jt703(void) { PROBE("jt703"); } +0x1da6; id 51 | boot.c:30907 |
| `jt705` | — | jt | static void jt704(void) { PROBE("jt704"); } +0x1270; id 41,46 | boot.c:30908 |
| `jt708` | — | jt | — | boot.c:30923 |
| `jt71` | JT[71] | jt | JT[71] (CODE 6+0x61c6) — set the -13048 word (jt72's pair). | boot.c:41342 |
| `jt710` | JT[710] | jt | JT[710] (CODE 18 + 0x3308) — flame-tongue style retaliation: in the combat view the effect's linked entity takes 4d6 Fire\|Magic damage (jt867 category 2) against a category-4 save. | boot.c:35394 |
| `jt711` | JT[711] | jt | JT[711] (CODE 18 + 0x2b78) — coughing fit (gas aftermath): in the combat view with a sub-record, announce "is coughing" while sub[2] is up, clear sub[1]/sub[2], recompute derived stats (jt21), -2 on b | boot.c:34292 |
| `jt712` | JT[712] | jt | JT[712] (CODE 18 + 0x301a) — feeblemind hook: on the apply pass INT and WIS (rec[115]/rec[117]) drop to 3 ("loses intellect"); in the combat view a spell being cast (sub[0]) is lost (jt31 flags the ab | boot.c:34476 |
| `jt713` | JT[713] | jt | JT[713] (CODE 18+0x2914) — tick the "fighting with snakes" effect: drain the effect node's counter node[4] by rec[388]+rec[387]; when it would run out, remove the effect (type 3, jt878) | boot.c:32565 |
| `jt714` | JT[714] | jt | JT[714] (CODE 18 + 0x336c) — charm: the apply pass sets the charm flag, swaps in the 0xb3/0xb2 creature placeholder, runs the JT[508] target pick and joins the opposite side of the picked combatant (l | boot.c:35419 |
| `jt715` | JT[715] | jt | JT[715] (CODE 18 + 0x2c04) — dispel evil: on the rolling pass, the effect's linked entity (sub[12] -> g_a5_25250) is dispelled (jt860 status 8) when it is evil (ent[191] bit 0) and fails its save (jt8 | boot.c:34323 |
| `jt716` | JT[716] | jt | JT[716] (CODE 18 + 0x310e) — energy-drain damage stage: when the effect's linked entity (sub[12] -> g_a5_25250) has trait bit 0 of byte 192 set, stage 6d6 (jt873) + the actor's strength damage adjustm | boot.c:34510 |
| `jt717` | JT[717] | jt | JT[717] (CODE 18 + 0x360c) — "breathes poison": the dragon-breath area attack | boot.c:32723 |
| `jt718` | JT[718] | jt | JT[718] (CODE 18+0x2988) — count a creature trait: when out of the triggering pass (flag==0) and the actor has a monster sub-record (rec[64]), cache it at -25250 and, if its trait byte [192] has bit 2 | boot.c:32545 |
| `jt719` | JT[719] | jt | JT[719] (CODE 18 + 0x3486) — retaliation hook, 2d6 variant of jt710 (Fire\|Magic, category-4 save, jt867 category 2). | boot.c:35457 |
| `jt72` | JT[72] | jt | JT[72] (CODE 6+0x61d4) — read the -13048 word | boot.c:44382 |
| `jt720` | JT[720] | jt | JT[720] (CODE 18 + 0x2cba) — confusion tick: each apply pass rolls d100 and the victim 1-10 runs away (effect 35 stripped, sub[18] set, charm flag rec[383] + placeholder creature 0xb3, jt871 status 11 | boot.c:34358 |
| `jt721` | JT[721] | jt | JT[721] (CODE 18 + 0x3170) — confusion-charm ("goes berserk"): the apply pass sets the charm flag and swaps the creature id to the 0xb3/0xb2 placeholder pair; in the combat view it runs the JT[508] ta | boot.c:34539 |
| `jt722` | JT[722] | jt | JT[722] (CODE 18 + 0x3808) — "breathes lightning": as jt717 but the charge re-seeds whenever node[4] > 3 too, hazard code 51, the pick out-param is the A5 global -22308, damage-type flags 36, sound jt | boot.c:32786 |
| `jt723` | JT[723] | jt | JT[723] (CODE 18 + 0x29c4) — side-flip effect (charm/confusion): needs a sub-record (rec[64]) | boot.c:34236 |
| `jt724` | JT[724] | jt | JT[724] (CODE 18 + 0x34ea) — charm cleanup: runs only on the removal pass; restores the creature id / charm flag and clears the sub-record's fled marker (sub[18]). | boot.c:35480 |
| `jt725` | JT[725] | jt | JT[725] (CODE 18 + 0x2ef6) — feeblemind-style save hook: on the apply pass, when the effect node's word at sub[4] is still zero the save is auto-failed (-25259 flag set, rolled throw -25255 forced to | boot.c:34402 |
| `jt726` | JT[726] | jt | JT[726] (CODE 18 + 0x32bc) — level-drain hook (one level): in the combat view, drain the effect's linked entity by one level. | boot.c:35362 |
| `jt727` | JT[727] | jt | JT[727] (CODE 18 + 0x3940) — "gazes": the gaze attack | boot.c:32826 |
| `jt728` | JT[728] | jt | JT[728] (CODE 18+0x2aa2) — clear a transient combat sub-state: out of the triggering pass (flag==0) and with a sub-record (rec[64]), if sub[2] is set announce "is <state>" (the -20012 name), re-flag t | boot.c:33588 |
| `jt729` | JT[729] | jt | JT[729] (CODE 18 + 0x352e) — vorpal: in the combat view, on the apply pass, when the attack roll was a good one (-25240) and the linked entity isn't immune (byte 192 bit 5), it "is beheaded" (jt860 st | boot.c:35502 |
| `jt73` | JT[73] | jt | JT[73] (CODE 6+0x6114) — free the pending-item list (-25302): zero the 12-byte staging block (-25314), then for each entry free its [58]-chain when it is a [40]==73 container (size 62 -> the -21508 bu | boot.c:49100 |
| `jt730` | JT[730] | jt | JT[730] (CODE 18 + 0x35e4) — clear the sub-record's byte 8 on both passes; when -25244 is set the -25264 stage flag is dropped too. | boot.c:35549 |
| `jt731` | JT[731] | jt | JT[731] (CODE 18 + 0x32e2) — level-drain hook (two levels). | boot.c:35377 |
| `jt732` | JT[732] | jt | JT[732] (CODE 18 + 0x3a18) — "breathes %s": the generic elemental breath (the dragon attack) | boot.c:32869 |
| `jt733` | JT[733] | jt | JT[733] (CODE 18 + 0x2b3c) — on the rolling pass, latch the effect's linked entity (sub[12]) into g_a5_25250 and, when that entity's trait byte rec[191] has bit 7, +1 on the rolled throw. | boot.c:34271 |
| `jt734` | JT[734] | jt | JT[734] (CODE 18 + 0x3580) — retaliation hook, identical to jt719 (2d6 Fire\|Magic vs a category-4 save). | boot.c:35527 |
| `jt735` | JT[735] | jt | JT[735] (CODE 18 + 0x2fca) — petrify hook: on the apply pass the effect's linked entity (sub[12] -> g_a5_25250) makes a category-1 save or "is turned to stone" (jt860 status 7). | boot.c:34452 |
| `jt736` | JT[736] | jt | JT[736] (CODE 18+0x71fe) — empty body on the Mac (linkw/unlk/rts); a faithful no-op like jt1130/jt920. | boot.c:32430 |
| `jt737` | JT[737] | jt | JT[737] (CODE 18 + 0x3e84) — on the rolling pass, +5 on the AC scratch g_a5_25252 and +1 on the rolled throw g_a5_25255. | boot.c:33766 |
| `jt738` | JT[738] | jt | JT[738] (CODE 18 + 0x3e9a) — the inverse of jt737: -5 off the AC scratch (clamped at 0) and -1 off the rolled throw. | boot.c:33779 |
| `jt739` | JT[739] | jt | JT[739] (CODE 18 + 0x3ec2) — on the rolling pass, -7 off the rolled throw when the active roster member (g_a5_27932) has trait bit 1 of rec[191] set. | boot.c:33796 |
| `jt740` | JT[740] | jt | JT[740] (CODE 18 + 0x3ee6) — Electricity immunity: negate the pending damage when element bit 4 is up (l3e40). | boot.c:33809 |
| `jt741` | JT[741] | jt | JT[741] (CODE 18 + 0x3efe, 2 sites) — save-modifier hook: on the rolling pass (flag 0), classes 2/5/8 of the active roster member (g_a5_27932, rec[93]) get +2 on the save accumulator g_a5_25269 and -2 | boot.c:33653 |
| `jt742` | JT[742] | jt | JT[742] (CODE 18 + 0x3f44, 2 sites) — as jt741 but for classes 0/3/6. | boot.c:33670 |
| `jt743` | JT[743] | jt | JT[743] (CODE 18 + 0x3f88) — cold-resistance hook: on the rolling pass, Cold damage (g_a5_25266 bit 1) is halved (signed) and the save accumulator bumped by 3. | boot.c:33689 |
| `jt744` | JT[744] | jt | JT[744] (CODE 18 + 0x3fb2, 2 sites) — faithful no-op handler. | boot.c:33703 |
| `jt745` | JT[745] | jt | JT[745] (CODE 18 + 0x3fba) — magic-save hook: on the rolling pass, +1 on the save accumulator unless the damage is Magic (g_a5_25266 bit 8), and -1 off the rolled throw either way. | boot.c:33713 |
| `jt746` | JT[746] | jt | JT[746] (CODE 18 + 0x3fda) — faithful no-op handler. | boot.c:33726 |
| `jt747` | JT[747] | jt | JT[747] (CODE 18 + 0x3fe2) — apply effect 26 (strength node[4], duration 10) via l3dfe; if it took and the target still has more than 1 HP (rec[395]), clear the element flags and deal 1 point of untyp | boot.c:33738 |
| `jt748` | JT[748] | jt | JT[748] (CODE 18 + 0x405a) — faithful no-op handler. | boot.c:33757 |
| `jt749` | JT[749] | jt | JT[749] (CODE 18 + 0x4062) — on the rolling pass, raise the save stat rec[385] to at least 57, +1 on the save accumulator, and negate the pending damage when the staged feature sound is 15 (g_a5_25262 | boot.c:33821 |
| `jt750` | JT[750] | jt | JT[750] (CODE 18 + 0x409e) — on the rolling pass, a 30% chance (jt870 1d100 <= 30) clears any pending type-11 and type-53 damage (l3dda). | boot.c:33840 |
| `jt751` | JT[751] | jt | JT[751] (CODE 18 + 0x40d4) — fire resistance: on the rolling pass, Fire damage (g_a5_25266 bit 0) is halved (signed) and the save accumulator bumped by 3 (the Cold twin is jt743). | boot.c:33856 |
| `jt752` | JT[752] | jt | JT[752] (CODE 18 + 0x40fe) — delayed-poison removal: on the rolling pass, if the actor carries a type-55 effect (jt41) it "dies from poison" (jt860 status 6), then the type-15 effect is stripped (jt87 | boot.c:33873 |
| `jt753` | JT[753] | jt | JT[753] (CODE 18 + 0x4156) — conjured-item effect ("Gains an item", type 6 / id 121, e.g | boot.c:34057 |
| `jt754` | JT[754] | jt | JT[754] (CODE 18 + 0x430c) — on the rolling pass, when the active roster member does NOT carry a type-24 effect (jt41): arm the g_a5_25259 flag and -4 off the rolled throw. | boot.c:34122 |
| `jt755` | JT[755] | jt | JT[755] (CODE 18 + 0x433c) — mirror image: in the combat view, a hit rolls 1d(images+1) (images = node[4] >> 5); on anything above 1 an image absorbs it — unless the standing-hazard pass (-25257) is o | boot.c:34144 |
| `jt756` | JT[756] | jt | JT[756] (CODE 18 + 0x4424) — on the rolling pass, shave a (signed) quarter off the pending damage word. | boot.c:34178 |
| `jt757` | JT[757] | jt | JT[757] (CODE 18 + 0x4446) — on the rolling pass, -4 across the board: rolled throw (-25255), the actor's save stats rec[385]/rec[386], and the save accumulator (-25269). | boot.c:34191 |
| `jt758` | JT[758] | jt | JT[758] (CODE 18 + 0x446c) — composite hook: on the apply pass run the type-43 and type-44 removal hooks in turn (l77a0). | boot.c:35579 |
| `jt759` | JT[759] | jt | JT[759] (CODE 18 + 0x44b2) — -4 throw / -4 save modifier. | boot.c:35590 |
| `jt76` | CODE 6 + 0x4bf6 | jt | static void jt103(short top, short left, short right, short bottom); CODE 6 + 0x4bf6 | boot.c:5218 |
| `jt760` | JT[760] | jt | JT[760] (CODE 18 + 0x44c8) — aging: once per node (bit 5 of the magnitude byte) the victim "ages" (+1 to the age word rec[82], roster-dirty flag -27916); every apply pass doubles the staged -25263/-25 | boot.c:35605 |
| `jt761` | JT[761] | jt | JT[761] (CODE 18 + 0x4540) — drop the staged damage when the active feature-sound id (-25262) names a hazard whose class byte (-16906[id*16+1]) is below 8. | boot.c:35628 |
| `jt762` | JT[762] | jt | JT[762] (CODE 18 + 0x4632) — apply pass: when the active member's weapon-in-use record exists and its byte 48 (ranged class) is clear, the melee attack can always be avoided (l4580 at 100%). | boot.c:35676 |
| `jt763` | JT[763] | jt | JT[763] (CODE 18 + 0x466e) — halve the staged -25263/-25264 counters on the apply pass. | boot.c:35695 |
| `jt764` | JT[764] | jt | JT[764] (CODE 18 + 0x4694) — weakness: stage effect 43 (l3dfe, duration 60); when it lands, STR above 3 drops by one ("is weakened"), else a type-31 incapacity effect is added. | boot.c:35709 |
| `jt765` | JT[765] | jt | JT[765] (CODE 18 + 0x470c) — wounding: stage effect 44 (duration 10); when it lands, an actor above 1 HP takes 1 unflagged damage (jt867) with an out-of-combat roster repaint (jt936), else the type-31 | boot.c:35732 |
| `jt766` | JT[766] | jt | JT[766] (CODE 18 + 0x47a0) — -4 throw vs the active roster member when its trait bit 2 is set. | boot.c:35755 |
| `jt767` | JT[767] | jt | JT[767] (CODE 18 + 0x47c4) — -4 throw vs the active roster member when its trait bit 5 is set. | boot.c:35771 |
| `jt768` | JT[768] | jt | JT[768] (CODE 18 + 0x47e8) — alignment-keyed save bias: the side encoded in the node's magnitude bit 7 gets +1 throw/+1 modifier (l3e6e), the other side -1/-1. | boot.c:35788 |
| `jt769` | JT[769] | jt | JT[769] (CODE 18 + 0x4840) — cold resistance / fire vulnerability: Cold damage gives +2 save modifier and is halved (or zeroed when the -25245 negate flag is up); otherwise unresisted Fire damage is d | boot.c:35811 |
| `jt77` | JT[77] | jt | JT[77] (CODE 6+0x6920) — compose the PLAY-SCREEN frame chrome, full lift (the faithful #114 composer): l38d0(0) paint-begin, the two stone panels via jt103 (= the Mac L4bf6: the bottom content window | boot.c:43108 |
| `jt770` | JT[770] | jt | JT[770] (CODE 18 + 0x48a0) — fire resistance / cold vulnerability: the mirror image of jt769. | boot.c:35834 |
| `jt771` | JT[771] | jt | JT[771] (CODE 18 + 0x48f2) — faithful no-op. | boot.c:35856 |
| `jt772` | JT[772] | jt | JT[772] (CODE 18 + 0x48fa) — paired type-25 effect: the apply pass adds it (duration 1, magnitude 12), the removal pass strips it in the combat view. | boot.c:35866 |
| `jt773` | JT[773] | jt | JT[773] (CODE 18 + 0x497e) — the petrifying gaze ("is turned to stone") | boot.c:32969 |
| `jt774` | JT[774] | jt | JT[774] (CODE 18 + 0x4b5c) — faithful no-op handler. | boot.c:36035 |
| `jt775` | JT[775] | jt | JT[775] (CODE 18 + 0x4bd2) — the attacker needs a +3 weapon (or monk level) to damage this creature; runs L4b64 on both passes. | boot.c:36044 |
| `jt776` | JT[776] | jt | JT[776] (CODE 18 + 0x4be4) — fire-damage reduction: -2 per staged damage die (-25261), floored at the staged die count; counts as a resistance (+4 to -25269) and absorbs the hit (l3dda(0)) unless the | boot.c:36056 |
| `jt777` | JT[777] | jt | JT[777] (CODE 18 + 0x4c4c) — regeneration tick: refresh effect 62 (magnitude node[4], duration 60) and, when that lands, heal one point (jt869) and report it (jt14) | boot.c:36084 |
| `jt778` | JT[778] | jt | JT[778] (CODE 18 + 0x4c98) — absorb the hit when the active feature-sound id names a hazard of class 3 or less (-16906 byte 1; companion of jt761's <8 and jt858's <=4 rules). | boot.c:36101 |
| `jt779` | JT[779] | jt | JT[779] (CODE 18 + 0x4cd8) — apply pass: the save-or-die effect-55 inflict (l2f28) with no save modifier. | boot.c:36119 |
| `jt78` | JT[78] | jt | JT[78] (CODE 6 + 0x67ca) — repaint the dungeon play frame: base (l670c), the two side FRAME pieces (l66e6 16/12), clear any pending marker (l68ae 2), the viewport + status frame pieces (jt1001 9/21), | boot.c:46784 |
| `jt780` | JT[780] | jt | JT[780] (CODE 18 + 0x4cf2) — halve the staged damage when the active member's weapon-in-use has a type-row class byte (+7) of 1 or less | boot.c:36132 |
| `jt781` | JT[781] | jt | JT[781] (CODE 18 + 0x4d48) — halve the staged damage when the active member's weapon-in-use has bit7 set in its type-row class byte (+7) | boot.c:36155 |
| `jt782` | JT[782] | jt | JT[782] (CODE 18 + 0x4d98) — apply pass: a defender with any of trait bits 0/2/6 (rec[191] & 0x45) lowers the rolled throw (-25255) by 4 unless effect 24 is already on the record. | boot.c:36178 |
| `jt783` | JT[783] | jt | JT[783] (CODE 18 + 0x4dd4) — the confusion gaze ("stares blankly") | boot.c:33029 |
| `jt784` | JT[784] | jt | JT[784] (CODE 18 + 0x4f12) — apply pass: -2 on both save-target rows rec[385]/rec[386]. | boot.c:36196 |
| `jt785` | JT[785] | jt | JT[785] (CODE 18 + 0x4f30) — faithful no-op handler. | boot.c:36210 |
| `jt786` | JT[786] | jt | JT[786] (CODE 18 + 0x4f38) — apply pass: absorb the hit (l3dda(0)) when damage-element flag 32 is staged. | boot.c:36219 |
| `jt787` | JT[787] | jt | JT[787] (CODE 18 + 0x4f58) — apply pass: double the -25263 staged counter (companion of jt760's *2 / jt763's /2). | boot.c:36232 |
| `jt788` | JT[788] | jt | JT[788] (CODE 18 + 0x4f72) — apply pass: replace the hit with effect 34 (l3dda(34)). | boot.c:36245 |
| `jt789` | JT[789] | jt | JT[789] (CODE 18 + 0x4f8a) — apply pass: +1 on the save-modifier accumulator (-25269). | boot.c:36257 |
| `jt79` | JT[79] | jt | JT[79] (CODE 6+0x69f8) — close the play screen's paint pass: L670c, L66e6(2), L3918(0) | boot.c:39842 |
| `jt790` | JT[790] | jt | JT[790] (CODE 18 + 0x4f9c) — fire-elemental style reaction to the staged damage element: magical Fire (1\|8) HEALS the staged amount ("Is Healed.", clamped at max HP rec[129]); Electricity (4) instead | boot.c:36273 |
| `jt791` | JT[791] | jt | JT[791] (CODE 18 + 0x5078) — halve the staged damage unless the active member's weapon-in-use enchantment (item[48], signed) is at least +3 | boot.c:36310 |
| `jt792` | JT[792] | jt | JT[792] (CODE 18 + 0x50b6) — as jt791 but the threshold is +4. | boot.c:36325 |
| `jt793` | JT[793] | jt | JT[793] (CODE 18 + 0x50f4) — slaying weapon: when the weapon in use is item type 74 the defender "is slain" outright (status 6) | boot.c:36342 |
| `jt794` | JT[794] | jt | JT[794] (CODE 18 + 0x5136) — apply pass: the save-or-die effect-55 inflict (l2f28) with a -2 save modifier. | boot.c:36359 |
| `jt795` | JT[795] | jt | JT[795] (CODE 18 + 0x5152) — apply pass: paralysing touch | boot.c:36373 |
| `jt796` | JT[796] | jt | JT[796] (CODE 18 + 0x5190) — once-only throw spoiler keyed on node bit 16: while neither the breath scratch (-22721) nor the rolled throw (-25255) is live, keep stripping the high node[4] bits; the fi | boot.c:36394 |
| `jt797` | JT[797] | jt | JT[797] (CODE 18 + 0x51f4) — apply pass: replace the hit with effect 111 (l3dda(111)). | boot.c:36414 |
| `jt798` | JT[798] | jt | JT[798] (CODE 18 + 0x520c) — l3e40(1) on both passes. | boot.c:36425 |
| `jt799` | JT[799] | jt | JT[799] (CODE 18 + 0x521e) — apply pass, attack forms 0/2/4 (-25246): save-modifier bonus from the defender's rec[121] tier — 4-6 -> +1, 7-10 -> +2, 11-13 -> +3, 14-17 -> +4, 18-20 -> +5 (hidden moveq | boot.c:36438 |
| `jt8` | — | jt | — | boot.c:19951 |
| `jt80` | JT[80] | jt | JT[80] / L68ae — toggle the secondary mode flag, with a per-state cleanup | boot.c:5399 |
| `jt800` | JT[800] | jt | JT[800] (CODE 18 + 0x52b4) — apply pass: 90% of the time the hit is replaced by effects 11 and 53 (l3dda twice). | boot.c:36466 |
| `jt801` | JT[801] | jt | JT[801] (CODE 18 + 0x52ea) — as jt799 but only for attack forms 4 and 2 (not 0); same rec[121] tier table (hidden moveq #1 confirmed at 0x533e). | boot.c:36482 |
| `jt802` | JT[802] | jt | JT[802] (CODE 18 + 0x537a) — apply pass: absorb the hit (l3dda(0)) when the Cold element flag is staged. | boot.c:36509 |
| `jt803` | JT[803] | jt | JT[803] (CODE 18 + 0x539a) — both passes: re-roll the defender's rec[388] column — 1-in-4 chance of 8, else 4. | boot.c:36522 |
| `jt804` | JT[804] | jt | JT[804] (CODE 18 + 0x53cc) — fear aura, both passes: every opposing party member (-27928 list) of level (m[137]) below 6 and status OK, not already feared (effect 120) and not protected (effect 92), g | boot.c:36540 |
| `jt805` | JT[805] | jt | JT[805] (CODE 18 + 0x5a88) — both passes: "+1 weapon to hit" check. | boot.c:36577 |
| `jt806` | JT[806] | jt | JT[806] (CODE 18 + 0x5a9a) — apply pass: when the linked entity (the attacker, latched into -25250) is within 1 cell (jt494) and its descriptor flag byte 191 has bit 3 set, add the slot-4 ability scor | boot.c:36589 |
| `jt807` | JT[807] | jt | JT[807] (CODE 18 + 0x5af6) — both passes: recount the linked-entity tally via jt498. | boot.c:36614 |
| `jt808` | JT[808] | jt | JT[808] (CODE 18 + 0x5b08) — REMOVAL pass only: re-derive rec[128] from the slot-3 ability score (score/5 + 1, unsigned divide). | boot.c:36624 |
| `jt809` | JT[809] | jt | JT[809] (CODE 18 + 0x5b3c) — apply pass: when the active member is within 1 cell of `rec` (jt494), reflect the attack back: double the staged damage and deal it to the ACTIVE member as element-type 8 | boot.c:36642 |
| `jt81` | JT[81] | jt | JT[81] (CODE 6+0x6a10) — paint the standard STONE WINDOW CHROME (the menu/Hall screens): bind "gen" through the library binder (L338c kind 51 + L33ac -> the -13044 slot; lands in binder slot 1 = GLIB | boot.c:3770 |
| `jt810` | JT[810] | jt | JT[810] (CODE 18 + 0x5bd8) — apply pass: paste the two-part flavor string (-14444 + -19644) into the message line. | boot.c:36673 |
| `jt811` | JT[811] | jt | JT[811] (CODE 18 + 0x5c12) — the paralyzing gaze ("is paralyzed") | boot.c:33074 |
| `jt812` | JT[812] | jt | JT[812] (CODE 18 + 0x5e00) — both passes: the natural-attack flavor message + damage stage | boot.c:36697 |
| `jt813` | JT[813] | jt | JT[813] (CODE 18 + 0x5fee) — both passes: -4 to both save rows. | boot.c:36756 |
| `jt814` | JT[814] | jt | JT[814] (CODE 18 + 0x6006) — both passes: fire-shield effect 119 | boot.c:36772 |
| `jt815` | JT[815] | jt | JT[815] (CODE 18 + 0x6096) — both passes: low-level (rec[137] < 3) targets get a recount; the staged damage drops by 2 (floor 0) and the attack-roll stage -25255 by 2. | boot.c:36798 |
| `jt816` | JT[816] | jt | JT[816] (CODE 18 + 0x60ce) — both passes: +4 to both save rows. | boot.c:36815 |
| `jt817` | JT[817] | jt | JT[817] (CODE 18 + 0x60e6) — both passes: absorb against attack forms 11/35/52/68/111. | boot.c:36828 |
| `jt818` | JT[818] | jt | JT[818] (CODE 18 + 0x6120) — both passes: "+2 weapon to hit" check. | boot.c:36841 |
| `jt819` | JT[819] | jt | JT[819] (CODE 18 + 0x6132) — both passes: when the bit-4 element flag is staged, SET the bonus stage -25269 to 20 (not add). | boot.c:36851 |
| `jt82` | JT[82] | jt | JT[82] (CODE 6 + 0x6788) — stand up the empty 3D viewport frame: clear box (l670c) + the three panel dividers (l66e6) + the viewport FRAME piece (jt1001 group 1 item 8) + reset the view cache (jt120 N | boot.c:29178 |
| `jt820` | JT[820] | jt | JT[820] (CODE 18 + 0x7206) — ITEM hook (second arg is the item, not an effect node): clear the -23187 hook override, then mirror the item's linked effect type (item[55]) onto the bearer — removal pass | boot.c:37802 |
| `jt821` | JT[821] | jt | JT[821] (CODE 18 + 0x614a) — both passes: standard paralysis (l3d3a with no save modifier). | boot.c:36891 |
| `jt822` | JT[822] | jt | JT[822] (CODE 18 + 0x6162) — both passes: explosion burst | boot.c:36907 |
| `jt823` | JT[823] | jt | JT[823] (CODE 18 + 0x627e) — both passes: absorb attack forms 11 and 53. | boot.c:36945 |
| `jt824` | JT[824] | jt | JT[824] (CODE 18 + 0x629a) — both passes: absorb attack forms 55 and 52; when the attack-form byte -25246 is 0 the bonus stage -25269 is SET to 100. | boot.c:36957 |
| `jt825` | JT[825] | jt | JT[825] (CODE 18 + 0x62c2) — both passes: attack-roll stage -2. | boot.c:37166 |
| `jt826` | JT[826] | jt | JT[826] (CODE 18 + 0x62ce) — both passes: absorb attack form 52. | boot.c:37175 |
| `jt827` | JT[827] | jt | JT[827] (CODE 18 + 0x62e0) — both passes: when the active member's held item (slot +12) is type 54, restage the damage as 1d6+1. | boot.c:37185 |
| `jt828` | JT[828] | jt | JT[828] (CODE 18 + 0x631c) — both passes: when -25265 bits 0 and 3 are both set, add the staged dice count -25261 to the damage. | boot.c:37203 |
| `jt829` | JT[829] | jt | JT[829] (CODE 18 + 0x6340) — both passes: halve the damage when the active member attacks with a plus-bonus weapon (item[48] > 0). | boot.c:37215 |
| `jt83` | JT[83] | jt | JT[83] (CODE 6+0x6908) — jt79's sibling paint-pass close with count 16: l670c, l66e6(16), l3918(0) | boot.c:43631 |
| `jt830` | JT[830] | jt | JT[830] (CODE 18 + 0x6374) — both passes: infect the linked entity (rec[64]+12) with disease unless already protected (effect 50) or already diseased (effect 215): effect 215 for 14400, an effect-172 | boot.c:37234 |
| `jt831` | JT[831] | jt | JT[831] (CODE 18 + 0x6420) — both passes: fear wave over the party list | boot.c:37263 |
| `jt832` | JT[832] | jt | JT[832] (CODE 18 + 0x6508) — both passes: rot tick | boot.c:37301 |
| `jt833` | JT[833] | jt | JT[833] (CODE 18 + 0x65b8) — both passes: when the bit-2 element flag is staged, absorb the hit and heal 8 HP | boot.c:37331 |
| `jt834` | JT[834] | jt | JT[834] (CODE 18 + 0x65e8) — both passes: Fire/Cold half-damage with save: a made row-4 save zeroes the damage entirely when the hazard row's byte 8 is set; otherwise (made or failed) the damage halve | boot.c:37348 |
| `jt835` | JT[835] | jt | JT[835] (CODE 18 + 0x6646) — both passes: halve the damage. | boot.c:37367 |
| `jt836` | JT[836] | jt | JT[836] (CODE 18 + 0x665c) — both passes: engulf | boot.c:37381 |
| `jt837` | JT[837] | jt | JT[837] (CODE 18 + 0x6782) — both passes: suffocation countdown — node[4] ticks down; at zero the victim Suffocates (status 6). | boot.c:37415 |
| `jt838` | JT[838] | jt | JT[838] (CODE 18 + 0x67b8) — engulf maintenance on the ENGULFER's effect 179 (node[4] = the victim's actor index) | boot.c:37439 |
| `jt839` | JT[839] | jt | JT[839] (CODE 18 + 0x6916) — armor dissolve | boot.c:37490 |
| `jt84` | JT[84] | jt | JT[84] (CODE 6 + 0x68f8) — minimal play-frame reblit: lock the port (l670c) and draw frame piece 16 (l66e6) | boot.c:31256 |
| `jt840` | JT[840] | jt | JT[840] (CODE 18 + 0x6a46) — both passes: magic-weapon-required check (l4b64(255) — the wrap trick means only magic bonuses pass); an unarmed attacker without the -25265 bit-3 ability does no damage a | boot.c:37546 |
| `jt841` | JT[841] | jt | JT[841] (CODE 18 + 0x6a78) — both passes: if anyone in the party carries effect 195, the bearer rec gains effect 203 (255 mag, 4 rounds) once. | boot.c:37562 |
| `jt842` | JT[842] | jt | JT[842] (CODE 18 + 0x6ae0) — the gorgon's breath: a petrifying area breath | boot.c:33130 |
| `jt843` | JT[843] | jt | JT[843] (CODE 18 + 0x6d26) — both passes: disease tick | boot.c:37584 |
| `jt844` | JT[844] | jt | JT[844] (CODE 18 + 0x6d72) — both passes: unless the Fire element flag is staged, gain effect 218 at 3d6 magnitude | boot.c:37602 |
| `jt845` | JT[845] | jt | JT[845] (CODE 18 + 0x6e08) — both passes: gain effect 225 (3 mag, 255 rounds) unless already under 226 or 225. | boot.c:37618 |
| `jt846` | JT[846] | jt | JT[846] (CODE 18 + 0x6e5c) — both passes: troll-style rise | boot.c:37635 |
| `jt847` | JT[847] | jt | JT[847] (CODE 18 + 0x6ea6) — both passes: when the Fire element flag is staged, knock 1 off the damage per staged die (-25261), flooring at the die count itself. | boot.c:37653 |
| `jt848` | JT[848] | jt | JT[848] (CODE 18 + 0x6ee8) — both passes: bear hug | boot.c:37675 |
| `jt849` | JT[849] | jt | JT[849] (CODE 18 + 0x6f90) — both passes: clear the sub-record's byte 8 (the jt37 working count) and, if -25244 is set, the -25264 stage too. | boot.c:37701 |
| `jt850` | JT[850] | jt | JT[850] (CODE 18 + 0x6fb8) — bear-hug maintenance on the HUGGER's effect 222 (node[4] = the victim's actor index); the 221/222 sibling of jt838, without the jt37 rebuild and with a local out byte for | boot.c:37721 |
| `jt851` | JT[851] | jt | JT[851] (CODE 18 + 0x70ac) — "Breathes Fire": the single-target fire breath (the hell hound) | boot.c:33198 |
| `jt852` | JT[852] | jt | JT[852] (CODE 18 + 0x71d0) — both passes: when the linked entity is not status-OK, inflict paralysis (l3d3a; second arg 63 here, dead in the body). | boot.c:37760 |
| `jt853` | JT[853] | jt | JT[853] (CODE 18 + 0x6db8) — both passes: gain effect 226 (255 rounds). | boot.c:37776 |
| `jt854` | JT[854] | jt | JT[854] (CODE 18 + 0x6dd8) — both passes: regenerate 3 HP, capped at the derived max rec[129]. | boot.c:37786 |
| `jt855` | JT[855] | jt | JT[855] (CODE 18+0x3e5c) — reset the actor's combat order block (jt498). | boot.c:32434 |
| `jt856` | JT[856] | jt | JT[856] (CODE 18 + 0x725c) — build the 230-entry per-creature hook table at A5-25242 that jt857/l77a0 dispatches through: every slot defaults to jt736 (the no-op handler), then the specific special-at | boot.c:38339 |
| `jt858` | JT[858] | jt | JT[858] (CODE 18 + 0x493e) — drop the staged damage when the active feature-sound id names a hazard whose class byte is 4 or less (companion of jt761's <8 rule). | boot.c:35882 |
| `jt859` | JT[859] | jt | JT[859] (CODE 18+0x77f6) — empty (rts only); faithful no-op. | boot.c:32532 |
| `jt86` | JT[86] | jt | JT[86] (CODE 6+0x4c96) — ITEMS.DAT load callback (glib_load_cb): read the 2048-byte item-template table into the -27944 buffer (128 x 16-byte records); 1 if the full size arrived. | boot.c:48817 |
| `jt860` | JT[860] | jt | JT[860] (CODE 18+0x0006) — set an actor's status (e.g | boot.c:32480 |
| `jt861` | JT[861] | jt | JT[861] (CODE 18 + 0x267e, 2 sites) — age/expire the standing clouds (wall of fog, stinking cloud, ...) | boot.c:33511 |
| `jt862` | JT[862] | jt | JT[862] (CODE 18+0x137a) — a saving throw: roll 1d20 (jt870), apply the d20 special cases (natural 1 auto-fails, natural 20 -> 100), let jt868 case-16 adjust the roll (-25255), then test roll + `saveC | boot.c:32089 |
| `jt863` | JT[863] | jt | JT[863] (CODE 18+0x1956) — if a new (index,value) contribution beats the actor's current channel (rec[112] index, rec[125] value at index 18), encode it into *out (l18c8) and return 1; else return 0. | boot.c:32519 |
| `jt864` | JT[864] | jt | JT[864] (CODE 18 + 0x1414, 3 sites) — full saving throw for `entity` against `threshold` | boot.c:33382 |
| `jt865` | JT[865] | jt | JT[865] (CODE 18+0x17ec) — remove all of the actor's standard status effects (the 18-entry type table at -5895); then a special-case: if a type-77 effect remains and the actor is creature 179, clear r | boot.c:32202 |
| `jt866` | JT[866] | jt | JT[866] (CODE 18 + 0x1532, 17 sites) — the "fits / encumbrance gate" probe for the member at `member`, returning a can-carry flag (g_a5_25245) | boot.c:24697 |
| `jt867` | — | jt | — | boot.c:32621 |
| `jt868` | JT[868] | jt | JT[868] (CODE 18 + 0x0420, 30 sites) — dispatch the item-grant code list for game-event `sel` | boot.c:24623 |
| `jt869` | JT[869] | jt | JT[869] (CODE 18+0x23e6) — heal `amount` HP | boot.c:32448 |
| `jt87` | JT[87] | jt | JT[87] (CODE 6+0x5252) — item.dat load callback (glib_load_cb): 255-fill the 4590-byte item-record table (-27920, 255 x 18-byte records), read 4572 bytes at +18 (records 1..254), then fix the words at | boot.c:48833 |
| `jt870` | JT[870] | jt | JT[870] (CODE 18 + 0x15f4, 95 sites) — "count d item" dice roll | boot.c:4088 |
| `jt871` | — | jt | — | boot.c:24906 |
| `jt872` | JT[872] | jt | JT[872] (CODE 18+0x1864) — "cure" a single status effect: if the actor carries an effect of `type` (jt41), print "<name> is Cured" (jt18) and remove it (jt878) | boot.c:32185 |
| `jt873` | JT[873] | jt | JT[873] (CODE 18 + 0x1638) — stash the popup's selected row index (-25261) and return the pixel height of `a` rows of item `b` (jt870 = sum of jt485(b)+1). | boot.c:23520 |
| `jt874` | — | jt | — | boot.c:25392 |
| `jt875` | JT[875] | jt | JT[875] (CODE 18 + 0x1b14, 18 sites) — recompute one of a character's ability/derived fields (`idx`) from its base plus every relevant magic-item effect, via L18fa (decode) / L19b2 (keep-best) / L19f8 | boot.c:25010 |
| `jt876` | JT[876] | jt | JT[876] (CODE 18 + 0x1666) — append an item node to an entity's list (the inverse of jt878's remove) | boot.c:23343 |
| `jt877` | JT[877] | jt | JT[877] (CODE 18 + 0x16fc, 3 sites) — kill/remove `entity` from the combat field with a death message | boot.c:33438 |
| `jt878` | JT[878] | jt | JT[878] (CODE 18 + 0x009e, 39 sites) — remove item from inventory | boot.c:24753 |
| `jt879` | JT[879] | jt | JT[879] (CODE 18 + 0x0ede, 4 sites) — apply the ongoing "standing in a hazard" combat effect to `entity` for the cell it occupies | boot.c:33246 |
| `jt880` | JT[880] | jt | JT[880] (CODE 18+0x17b6) — remove every type-25 effect the actor carries (loop: find via jt41, remove via jt878, until none remain). | boot.c:32217 |
| `jt881` | JT[881] | jt | JT[881] (CODE 19 + 0x58d4; the asm's local label L58d4 — jt885 calls it PC-relative) — Constitution hit-point adjustment per hit die: the base value from the -30246 table indexed by CON (ent[121]), pl | boot.c:34630 |
| `jt882` | JT[882] | jt | JT[882] (CODE 19+0x30bc) — ready/un-ready an item, full lift | boot.c:33989 |
| `jt883` | JT[883] | jt | JT[883] (CODE 19+0x4248) — adjust a member's encumbrance: the word at rec+86 += delta | boot.c:44396 |
| `jt885` | — | jt | — | boot.c:34787 |
| `jt888` | — | jt | — | boot.c:29709 |
| `jt89` | — | jt | — | boot.c:12527 |
| `jt892` | JT[892] | jt | JT[892] (CODE 19 + 0x1abe) — the combatant record-sheet stats block: Hit Points / Armor Class / Encumbrance / THAC0 / Movement | boot.c:47335 |
| `jt895` | JT[895] | jt | JT[895] (CODE 19 + 0x1f00) — per-ability score DISPLAY for ability i (row i+9 on the char-gen sheet) | boot.c:20407 |
| `jt897` | JT[897] | jt | JT[897] (CODE 19+0x420e) — per-coin-bank XP credit hook | boot.c:43324 |
| `jt90` | CODE 6+0x4d7c | jt | static void jt89(void) { PROBE("jt89"); (void)jt1134(); jt1182(&g_a5_byte(-17466)); } CODE 6+0x4d7c | boot.c:12528 |
| `jt900` | JT[900] | jt | JT[900] (CODE 19+0x5234) — seed the idle-timer baseline -5802 with the current tick (its +0x523e sibling tests "<48 ticks since"). | boot.c:48351 |
| `jt903` | JT[903] | jt | JT[903] (CODE 19 + 0x3540) — total container-slot count across the party: sum item[53] over every container (type byte +40 == 'I'/73) in every party member's inventory list (g_a5_-27928 head, +0 = nex | boot.c:27881 |
| `jt904` | — | jt | #define g_a5_13804 g_a5_long(-13804) roster cluster arg for jt182 | boot.c:46961 |
| `jt906` | JT[906] | jt | JT[906] (CODE 19+0x687e) — recompute the five saving throws, full lift | boot.c:20790 |
| `jt907` | — | jt | — | boot.c:20846 |
| `jt908` | JT[908] | jt | JT[908] (CODE 19 + 0x5df2; asm local label L5df2) — rebuild the spell-slot limit table at ent[355..381] (3 rows x 9 levels: cleric 355, druid 364, mage 373 — the limits l54ac fits memorized spells aga | boot.c:35036 |
| `jt909` | JT[909] | jt | JT[909] (CODE 19 + 0x6af2) — "is this record dead?" predicate: rec[88] == 5 (status 5 = dead) | boot.c:20644 |
| `jt91` | JT[91] | jt | JT[91] (CODE 6+0x3fac) — the jt98 text-input accept flag (-17528) | boot.c:41323 |
| `jt910` | JT[910] | jt | JT[910] (CODE 19 + 0x631c) — recompute the level-derived combat stats after a level change: ent[127] = best per-class value from the -23184 table (22 bytes/class, level capped at 21), ent[137] = highe | boot.c:35169 |
| `jt911` | JT[911] | jt | JT[911] (CODE 19 + 0x5ba0; asm local label L5ba0) — fill the cleric spell-slot row (ent[355..361]) from the -29876 progression table (261 = 29 levels x 9 spell levels per class sub-table; selector byt | boot.c:34953 |
| `jt912` | JT[912] | jt | JT[912] (CODE 19 + 0x5d8a; asm local label L5d8a) — Intelligence (ent[115]) gates on the high mage spell-slot rows: INT < 12 clears 6th (ent[378]), < 14 7th (ent[379]), < 16 8th (ent[380]), < 18 9th ( | boot.c:35002 |
| `jt913` | JT[913] | jt | JT[913] (CODE 19 + 0x528) — format a 0..N number into buf with a leading zero when < 10 (the minutes field of the game clock: "05", "45"). | boot.c:5713 |
| `jt914` | JT[914] | jt | JT[914] (CODE 19+0x035c) — ADVANCE GAME TIME: copy the game record's 7 date/time bytes (h[5..11]) into a word array, bump field `idx` once per `count` with the L025a carry normalize each step, write t | boot.c:49300 |
| `jt915` | JT[915] | jt | JT[915] (CODE 19+0x0ffe) — the REST/Encamp engine, full CFG over the eight rest-tier leaf stubs | boot.c:43396 |
| `jt918` | CODE 12 + 0x0d90 | jt | jt918 — new-game / select-design dialog driver | boot.c:48051 |
| `jt918_count_visible_designs` | — | other | Count entries in the design linked-list | boot.c:48022 |
| `jt919` | — | jt | — | boot.c:1877 |
| `jt92` | JT[92] | jt | JT[92] (CODE 6 + 0x4bac, 22 sites) — public alias for L4bac (scroll-advance helper jt42 reaches in its message chain). | boot.c:27349 |
| `jt920` | — | jt | — | boot.c:583 |
| `jt925` | JT[925] | jt | JT[925] (CODE 12+0x1c8a) — bank the party's pooled coin/XP words: each live member (status 0 or the 179 charm placeholder) adds its three words at +76 into the -25314 pool longs and runs the jt897 cre | boot.c:43336 |
| `jt926` | — | jt | — | boot.c:44410 |
| `jt93` | JT[93] | jt | JT[93] (CODE 6+0x475e) — write a "%( %)"-grouped value at text cell (a, b) colour c (jt94 with style = the colour repeated, the THINK C group format passing value d) | boot.c:44133 |
| `jt931` | CODE 12 + 0x1b12 | jt | static void jt919(void) { PROBE("jt919"); } CODE 12 + 0x1b12 | boot.c:1878 |
| `jt932` | JT[932] | jt | JT[932] (CODE 12+0x45ca) — scale an experience award, full lift: base clamps up to 1 and multiplies by `mult` (jt4) when > 1, then kind dispatches — 0: nothing; 1..8: divide by the -5302 table byte (j | boot.c:43222 |
| `jt934` | JT[934] | jt | JT[934] (CODE 12 + 0x0848, 15 sites) — roster cursor navigation: move the active party member (g_a5_-27932) to the next or previous entry in the party list (head g_a5_-27928, .next at member offset 0) | boot.c:2769 |
| `jt935` | JT[935] | jt | JT[935] (CODE 12 + 0x4) — the play-screen refresh | boot.c:2810 |
| `jt936` | — | jt | — | boot.c:2860 |
| `jt937` | JT[937] | jt | JT[937] (CODE 12 + 0x02dc, 28 sites) — public alias for L02dc (Modify Character roster grid, lifted further down). | boot.c:2178 |
| `jt938` | CODE 12 + 0x562 | jt | jt938 (CODE 12 + 0x562) — the play-screen clock + party-position panel (the Mac HUD's "11,6 / 12:05 AM" box) | boot.c:5726 |
| `jt94` | JT[94] | jt | JT[94] (CODE 6 + 0x3fd6) — text paint with style / coord remap | boot.c:5656 |
| `jt941` | CODE 15 + 0x153e | jt | static void jt582(void); CODE 15 + 0x153e — load (lifted near jt585) | boot.c:2141 |
| `jt942` | L07dc | jt | Cross-segment JT entries L07dc calls — most are stubs; jt918 / jt942 / jt943 are lifted (jt942 and jt943 are the paired setter / getter on the loop predicate flag g_a5_4944). | boot.c:2134 |
| `jt942_caseN` | — | other | — | boot.c:47100 |
| `jt943` | — | jt | — | boot.c:3530 |
| `jt948` | JT[948] | jt | JT[948] (CODE 20 + 0x4a12) — the adventure-level dungeon loop, the body L07dc runs once the party is assembled | boot.c:3320 |
| `jt949` | JT[949] | jt | JT[949] (CODE 20 + 0x77a2) — Mac body is just `rts` | boot.c:1881 |
| `jt95` | JT[95] | jt | JT[95] (CODE 6+0x4c5c) — right-justify string `s` in place to `width` chars: while strlen (JT[483]) is short, re-format as " %s" (JT[488]) and copy back (JT[384]) | boot.c:40595 |
| `jt953` | JT[953] | jt | jt953 (JT[953] = CODE 21 + 0x4038) — the exploration command processor | boot.c:39727 |
| `jt955` | — | jt | — | boot.c:2934 |
| `jt956` | — | jt | — | boot.c:584 |
| `jt96` | CODE 6 + 0x43c4 | jt | jt96 (CODE 6 + 0x43c4) — render `val` word-wrapped into the cell rect (a,b)-(c,d) in char coords; s6 = style/colour, s7 != 0 draws the box | boot.c:6472 |
| `jt960` | JT[960] | jt | JT[960] (CODE 21+0x1a34) — the roster-cycle sound/feedback tick | boot.c:43641 |
| `jt964` | JT[964] | jt | JT[964] (CODE 5+0x7cce) — convert a loaded sound bank's samples in place: for each piece in GLIB group `tag` (l3736 count, walked back to front via l37aa) skip the 2-byte type word, read the 4-byte pi | boot.c:48704 |
| `jt965` | — | jt | — | boot.c:17914 |
| `jt966` | JT[966] | jt | JT[966] (CODE 5 + 0x2d6) — jt400's emit sink for screen text | boot.c:6324 |
| `jt967` | JT[967] | jt | JT[967] — the "%v" conversion: move the pen vertically | boot.c:6290 |
| `jt968` | JT[968] | jt | JT[968] — the "%k" conversion: move the pen horizontally | boot.c:6299 |
| `jt969` | JT[969] | jt | JT[969] — the "%a" conversion: set the text attribute (color/style) to the field-width value via L024c. | boot.c:6308 |
| `jt97` | JT[97] | jt | JT[97] (CODE 6 + 0x42a0) — draw `a` copies of char `ch` across cells from (col,row), via jt1089 "%c" | boot.c:5772 |
| `jt972` | JT[972] | jt | JT[972] (CODE 5+0x3682) — the library-load callback jt1014 hands to jt987: read the whole open file into the current group (-3622) via jt1016 (jt403 sizes it) | boot.c:46134 |
| `jt974` | JT[974] | jt | JT[974] (CODE 5+0x1304, ~600B) — the sound-mixer pump jt986 installs at -4774 (walks the -4848 channel table each tick, jt1131 output) | boot.c:48652 |
| `jt975` | JT[975] | jt | JT[975] (CODE 5+0x1042) — the ".slb" sound-bank load callback (glib_load_cb, registered by jt986): read the 10-byte bank header into -4766 (piece-count byte lands at -4758), the (count+1)-word offset | boot.c:48627 |
| `jt976` | JT[976] | jt | JT[976] (CODE 5+0xa9c) — set the sound replay mode byte -4850. | boot.c:48544 |
| `jt977` | JT[977] | jt | JT[977] (CODE 5 + 0x0aaa) — pop paint-stack frame | boot.c:621 |
| `jt978` | JT[978] | jt | JT[978] (CODE 5+0xa82) — store `v` in the sound-channel word slot selected by the -4886 index (word array at -4860). | boot.c:48537 |
| `jt979` | JT[979] | jt | JT[979] (CODE 5 + 0x0f48) — count active voices in the 5-slot table (a slot is live when its leading long is non-zero). | boot.c:17878 |
| `jt98` | — | jt | — | boot.c:41091 |
| `jt980` | JT[980] | jt | JT[980] (CODE 5 + 0x0f9c) — service one voice with the current timer tick. | boot.c:17872 |
| `jt981` | CODE 7 + 0x57bc | jt | static void jt211(void) { } TODO: lift CODE 7 + 0x57bc | core.c:101 |
| `jt983` | JT[983] | jt | JT[983] (CODE 5 + 0x0f74) — menu-bar visibility setter | boot.c:16517 |
| `jt984` | JT[984] | jt | JT[984] (CODE 5 + 0x0f1e) — stop all voices: reset the active-song word, clear the 5-slot voice table, refresh (jt1151). | boot.c:17893 |
| `jt985` | JT[985] | jt | JT[985] (CODE 5 + 0x12b4) — play song index `n`, range-checked against the song count g_a5_-4758 (else "Song out of range") | boot.c:17907 |
| `jt986` | JT[986] | jt | JT[986] (CODE 5+0x10f0) — open the "<name>.slb" sound bank: build the filename (jt384 + jt419 "slb"), load it through the GLIB loader with jt975 as the per-file callback, then install the jt974 mixer | boot.c:48662 |
| `jt987` | — | jt | — | boot.c:45629 |
| `jt989` | JT[989] | jt | JT[989] (CODE 5 + 0x1b56) — register handler config | boot.c:640 |
| `jt990` | — | jt | static char g_dir_ret[16]; stable name handed back to caller | boot.c:22214 |
| `jt991` | — | jt | — | boot.c:22233 |
| `jt992` | JT[992] | jt | JT[992] (CODE 5 + 0x1d70, = "Mirror") — horizontally flip the sprite piece `kind` of group-handle `spec` in place, for facing-direction sprites | boot.c:45277 |
| `jt993` | JT[993] | jt | JT[993] (CODE 5 + 0x20d0, "TNPalette") — commit one GLIB picture's palette | boot.c:45060 |
| `jt994` | JT[994] | jt | JT[994] (CODE 5+0x1f22) — draw a GLIB piece from a group base (the jt109 path) | boot.c:44353 |
| `jt995` | — | jt | — | boot.c:5042 |
| `jt996` | JT[996] | jt | JT[996] (CODE 5+0x3556, "TPalette") — look palette resource `idx` up in GLIB group `tag` and commit it RAW to the CLUT (no colour-range allocator, unlike jt993): must be a type-8 block ("Invalid TPale | boot.c:48591 |
| `jt997` | — | jt | — | boot.c:46169 |
| `kb_to_event` | — | other | source pumps | events.c:141 |
| `key_col_width` | — | other | Pixel width reserved on the right of each pull-down row for the Cmd-key column | menus.c:245 |
| `l0004` | L0004 | l | L0004 (CODE 4 + 0x0004) — segment entry / menu dispatch | boot.c:17104 |
| `l0004_22` | L0004 | other | L0004 (CODE 22 + 0x4) — the play/edit state-machine ENTRY | boot.c:14504 |
| `l0004_c6` | L0004 | l | L0004 (CODE 6+0x0004 — NOT the lifted menu-selection l0004, hence the _c6 suffix) — append one classified character to the name buffer jt130 builds | boot.c:41816 |
| `l0006` | CODE 13+0x0006 | l | CODE 13+0x0006 — combat teardown | boot.c:32310 |
| `l0006_20` | CODE 21+0x453c | other | static void jt955(void) { PROBE("jt955"); } CODE 21+0x453c — used by a deferred jt948 arm | boot.c:2935 |
| `l0006_c17` | L0006 | l | L0006 (CODE 17 + 0x0006) — body-icon finalize | boot.c:20956 |
| `l005a` | L005a | l | L005a (CODE 15 + 0x5a) — "is the save medium reachable?" precondition for the roster builders | boot.c:27683 |
| `l0062` | L0062 | l | L0062 (CODE 5+0x62) — the "quit from the error dialog" teardown (jt466/ jt1156/jt1119/jt1114/jt1158 + L27bc/L35f8/L01ac/L0f14) | boot.c:46412 |
| `l006c` | CODE 19+0x006c | l | CODE 19+0x006c (local, ~700B) — the post-advance side effects (timed-effect expiry over the party) | boot.c:49288 |
| `l0088` | L0088 | l | L0088 (CODE 5+0x88) — is any event available? | boot.c:46313 |
| `l0096` | — | l | — | boot.c:14449 |
| `l0098` | L0098 | l | L0098 (CODE 20 + 0x0098) — parse the encounter prompt string for its menu options | boot.c:31762 |
| `l00a8` | L00a8 | l | L00a8 (CODE 5+0xa8) — drain the event queue (pending keys, then kind-7). | boot.c:46324 |
| `l00da` | L00da | l | L00da (CODE 5+0xda) — drain, then block until the next event, then drain. | boot.c:46337 |
| `l00e0` | L00e0 | l | L00e0 (CODE 15 + 0x00e0) — open the slot file for WRITE and run the serializer (jt580) over it | boot.c:27773 |
| `l0116` | CODE 13+0x0116 | l | CODE 13+0x0116 — post-combat treasure/aftermath sequence (only when -27916 is set). | boot.c:32299 |
| `l0156` | L0156 | l | L0156 (CODE 5+0x156) — the brief "working" cursor flash: pulse jt1122 (slot 1189) for ~5 ticks, clear it, wait one more. | boot.c:46349 |
| `l01a2` | — | l | CODE 5 — intra-segment helpers. | master.c:95 |
| `l01ac` | — | l | — | master.c:96 |
| `l01be` | L01be | l | L01be (CODE 15 + 0x1be) — build the in-memory saved-character roster | boot.c:22290 |
| `l01de` | L01de | l | L01de (CODE 18 + 0x01de) — "auto-cast on entry" hook for cases 6/9: if the entity carries effect byte [196] and the active context flags (-25268 / -25266 bit 3) allow it, roll L15f4 against a slot-sca | boot.c:24517 |
| `l024c` | L024c | l | L024c (CODE 5+0x24c) — set the GLIB pen colour (low word + high byte). | boot.c:46364 |
| `l025a` | CODE 19+0x025a | l | CODE 19+0x025a (local) — normalize the 7-word date/time array against the per-field limits at -23228: each field that reaches its limit carries into the next | boot.c:49245 |
| `l0264` | L0264 | l | L0264 (CODE 5+0x264) — set the GLIB pen position (8000-space -> screen). | boot.c:46373 |
| `l026e` | L026e | l | L026e (CODE 18 + 0x026e) — the per-item engine | boot.c:24568 |
| `l026e_c20` | CODE 20 + 0x026e | l | L026e_c20 (CODE 20 + 0x026e) — the interactive encounter CHOICE prompt | boot.c:31809 |
| `l026e_list` | — | other | — | boot.c:24611 |
| `l02dc` | — | l | 2, 16 Magic-User/Thief -> Mage | boot.c:6870 |
| `l0306` | L0306 | l | L0306 (CODE 5 + 0x306) — draw a formatted template at the current pen | boot.c:6338 |
| `l032c` | CODE 19+0x032c | l | CODE 19+0x032c (local) — normalize the LIVE clock array (-23214) and fold the pending healing-rest bonus: -23206 += -23220 * -23204 (then clear -23204), capped at 99. | boot.c:49272 |
| `l0334` | L0334 | l | L0334 (CODE 5 + 0x334) — draw a formatted template at (top, left) in colour `attr` | boot.c:40899 |
| `l035c` | — | l | — | boot.c:43369 |
| `l035e` | L035e | l | L035e (CODE 6+0x035e) — file-group mode switch on the -31234 current-mode global (JT[3] over 0..6) | boot.c:22388 |
| `l036a` | L036a | l | L036a (CODE 5+0x36a) — the modal "Error: <msg>" alert (JT[1084]) | boot.c:46425 |
| `l0380` | L0380 | l | L0380 (CODE 20 + 0x0380) — present the type-21 encounter list dialog | boot.c:31853 |
| `l03f6` | L03f6 | l | L03f6 (CODE 20 + 0x03f6) — the type-21 encounter prompt's LIST renderer | boot.c:31876 |
| `l0418` | — | l | — | boot.c:43368 |
| `l0434` | CODE 13+0x0434 | l | CODE 13+0x0434 — per-round init: builds the -22624 initiative slot array (slot i at -22624 + 4i, first actor in -22620). | boot.c:32287 |
| `l0444` | — | l | Intra-CODE-6 helpers, still to lift. | boot.c:1888 |
| `l04cc` | L04cc | l | L04cc / L04de (CODE 4) — screen-dimension constants gated by g_a5_-2347 (color QD flag) | boot.c:16893 |
| `l04d6` | CODE 22 + 0x04d6 | l | l04d6 (CODE 22 + 0x04d6) — return a map cell's floor/ceiling decoration byte (cell byte 4, at design_state + cell*6 + 294). | boot.c:11188 |
| `l04de` | — | l | — | boot.c:16898 |
| `l04f0` | L04f0 | l | L04f0 (CODE 4 + 0x04f0) — the GLIB pixel-depth shift: 3 (1bpp packed) when g_a5_2347 is clear, else 0/1 by the g_a5_1312 colour-mode flag. | boot.c:25993 |
| `l050a` | L050a | l | L050a (CODE 4 + 0x050a, CODE-local) — page byte-count selector | boot.c:17402 |
| `l0572_c19` | L0572 | l | The CODE 19 rest-engine locals — leaf PROBE stubs pending their own lifts: L0572 = the camp clock/status repaint, L0694 = the spell-memorize setup (0 = nothing to rest for), L0418 = advance game time | boot.c:43366 |
| `l05ca` | L05ca | l | L05ca (CODE 22 + 0x05ca) — wall code on cell `cell`'s edge facing `dir`: edge = (dir & 6) >> 1 (0..3); returns the HIGH nibble of the cell record's edge byte (lvl + cell*6 + 290 + edge), the wall-art/ | boot.c:11824 |
| `l05dc` | L05dc | l | L05dc (CODE 4 + 0x05dc) — the GLIB blit cursor, word-aligned. | boot.c:26072 |
| `l05e4` | L05e4 | l | L05e4 (CODE 4 + 0x05e4) — the current GLIB blit cursor (dest pointer). | boot.c:25987 |
| `l05ea` | L05ea | l | L05ea (CODE 4 + 0x05ea) — after a blit of `n` columns x `row` rows, mark the affected GLIB region dirty (InvalRect, depth/mode-adjusted) and advance the blit column g_a5_3080 | boot.c:26005 |
| `l0674` | L0674 | l | L0674 (CODE 22 + 0x0674) — cell code A: byte 295 & 3. | boot.c:11205 |
| `l0694` | — | l | — | boot.c:43367 |
| `l06e2` | L06e2 | l | L06e2 (CODE 22 + 0x06e2) — cell code A by (x, y): l0674 of cell (width * y + x), width = design byte 3 | boot.c:12532 |
| `l0730` | L0730 | l | L0730 (CODE 20+0x0730) — the combat-damage message: emit "<name> is hit FOR <dmg> points of Damage." (or "<name> dies | boot.c:32038 |
| `l076e` | CODE 13+0x076e | l | CODE 13+0x076e — execute one actor's combat turn (the big one: status gates at sub +17/+20/+9, jt868(7), jt516 visibility, jt530 pose, jt21 recompute, then the action state machine). | boot.c:32292 |
| `l0788` | L0788 | l | L0788 (CODE 22 + 0x0788) — cell code B by (x, y): jt306 of cell (width * y + x) | boot.c:12774 |
| `l079a` | L079a | l | L079a (CODE 17 + 0x79a) — draw the selection-marker frame (four jt57 edge glyphs) for cell (row, col) | boot.c:23021 |
| `l07be` | L07be | l | L07be (CODE 22 + 0x07be) — tool 3's pick commit (runs after the state record is armed with mode 5). | boot.c:41489 |
| `l07dc` | — | l | g_a5_27990 / g_a5_28006 are macros at the top of the file. | boot.c:3544 |
| `l07e6` | — | l | — | boot.c:43370 |
| `l085e` | L085e | l | L085e (CODE 20 + 0x085e) — refresh the per-cell view caches and reset the display-state struct | boot.c:3084 |
| `l08ce` | L08ce | l | L08ce (CODE 20 + 0x08ce) — the event-display SPRITE/PIC composite | boot.c:46818 |
| `l08e6` | L08e6 | l | L08e6 — store the byte arg into the redraw flag g_a5_31230 | boot.c:5417 |
| `l08f4` | L08f4 | l | L08f4 (CODE 6 + 0x08f4) — recompute the equipped-weapon combat columns from the weapon in player[12] | boot.c:28429 |
| `l09ba` | L09ba | l | L09ba (CODE 17 + 0x9ba) — flush a grid redraw (jt593(1)). | boot.c:23034 |
| `l09dc` | L09dc | l | L09dc (CODE 17 + 0x9dc) — paint the review screen: first (when the -7004 seed flag is set) the 7x7 body-icon grid from DUNGCOM1 — all 49 shapes tiled, the character's current icon marked (jt57 kind 31 | boot.c:23065 |
| `l0a6e` | L0a6e | l | L0a6e (CODE 3+0xa6e) — guarantee `need` free bytes on the pool tail, compacting (L11ca) until enough is freed | boot.c:44511 |
| `l0aae` | L0aae | l | L0aae — build the design-menu, walk the c79x flag cluster to enable / disable each item, display it, return the user's selection 0..11 | boot.c:20148 |
| `l0aae_unused_warn` | — | other | — | boot.c:48210 |
| `l0ab8` | L0ab8 | l | L0ab8 (CODE 3+0xab8) — extend the in-progress group's region by `size` bytes (advance slot[count]), after ensuring the tail has room | boot.c:44529 |
| `l0ac2` | L0ac2 | l | L0ac2 (CODE 20 + 0x0ac2) — composite animation loop | boot.c:31086 |
| `l0b20` | L0b20 | l | L0b20 (CODE 20 + 0x0b20) — draw an event's text buffer into the dungeon message box | boot.c:31239 |
| `l0b3e` | L0b3e | l | L0b3e (CODE 6 + 0x0b3e) — set movement rate (player[396]) from an item whose type record (g_a5_27944 + item[40]*16) is kind 2 (a mount/vehicle): by the item's speed field item[44] (>399 -> 6, <=150 -> | boot.c:28270 |
| `l0b88` | L0b88 | l | L0b88 (CODE 20 + 0x0b88) — set "look/edit" play mode: clear the player record's in-play flag (offset 34), set offset 36, and put the play-state byte g_a5_-27990 = 3. | boot.c:1071 |
| `l0ba2` | L0ba2 | l | L0ba2 / JT[952] (CODE 20 + 0x0ba2) — set "in-play" mode: player record offset 34 = 1, offset 36 = 0, play-state g_a5_-27990 = 4. | boot.c:1085 |
| `l0bbc` | L0bbc | l | L0bbc (CODE 20 + 0x0bbc) — ENTER A LEVEL | boot.c:1107 |
| `l0be0` | — | l | — | boot.c:28302 |
| `l0cb8` | — | l | — | boot.c:43371 |
| `l0ce0_c15` | L0ce0 | l | L0ce0 (CODE 15 + 0xce0) — byte-swap the multi-byte fields of a character record between native and the little-endian .cch file layout: the word at 82, the three words at 76/78/80, the two longs at 68/ | boot.c:27797 |
| `l0d44` | L0d44 | l | L0d44 (CODE 6 + 0x0d44) — clamp a character's movement rate (player[396]) by encumbrance: the remaining allowance is the carry capacity (player[86]) less the Strength weight allowance (L2000), buckete | boot.c:28231 |
| `l0d86` | — | l | — | boot.c:43372 |
| `l0e10` | L0e10 | l | L0e10 (CODE 3+0xe10) — resize the group bound to freemap id `id` by `delta` bytes: clamp a shrink to the group's size (jt459), ensure tail room on a grow (l0a6e), slide the data of every later group ( | boot.c:44548 |
| `l0e3e` | — | l | — | boot.c:43373 |
| `l0eda` | — | l | — | master.c:98 |
| `l0ee6` | L0ee6 | l | L0ee6 (CODE 22 + 0x0ee6) — the tool-0 click with the editor locked (the play-map select/move arm, ~860B). | boot.c:41472 |
| `l0f14` | — | l | — | master.c:99 |
| `l0f1a` | — | l | — | boot.c:21837 |
| `l0f2e` | L0f2e | l | L0f2e — case 1 (Modify Character) | boot.c:47110 |
| `l0f3e` | L0f3e | l | L0f3e — case 2 (Delete Character) | boot.c:47130 |
| `l0f48` | L0f48 | l | L0f48 (CODE 5 + 0x0f48, CODE-local) — count non-empty menu slots | boot.c:16437 |
| `l0f60` | L0f60 | l | L0f60 — case 3 (Create Character) | boot.c:47150 |
| `l0f74` | L0f74 | l | L0f74 — case 4 (Remove Character) | boot.c:47193 |
| `l0faa` | L0faa | l | L0faa (CODE 5 + 0x0faa) — dispatch the registered voice-service callback (g_a5_-4774) with `arg`; no-op when none is registered (the port never registers one yet, so this currently does nothing). | boot.c:17864 |
| `l1020` | L1020 | l | L1020 / L366a (CODE 3 + 0x1020 / 0x366a) — two three-arg wrappers around the engine's internal BlockMove (L57f8) | boot.c:4211 |
| `l102a` | CODE 13+0x102a | l | CODE 13+0x102a — end-of-round bookkeeping; may set *done. | boot.c:32295 |
| `l1036` | L1036 | l | L1036 — case 5 (Add Character) | boot.c:47235 |
| `l103c` | L103c | l | L103c — compact one entry out of the four parallel cache tables | boot.c:4255 |
| `l104c` | L104c | l | L104c — case 6 (View Character) | boot.c:47793 |
| `l1060` | L1060 | l | L1060 — case 7 (Human Change Class) | boot.c:47821 |
| `l1090` | L1090 | l | L1090 (CODE 14+0x1090) — attacks-per-round from the -25264 kind | boot.c:43712 |
| `l10c4` | L10c4 | l | L10c4 (CODE 14+0x10c4) — line-of-sight / validity check between an actor and a candidate target | boot.c:37817 |
| `l10ca` | L10ca | l | L10ca — case 8 (Exit From Play / proceed-to-adventure) | boot.c:47874 |
| `l112c` | L112c | l | L112c (CODE 4+0x112c) — convert a 16x16 8-bit pixel block to bitplanes for the 1-bit cursor | boot.c:17580 |
| `l1142` | L1142 | l | L1142 — case 9 = "Save Current Game" | boot.c:47916 |
| `l115a` | L115a | l | L115a — case 10 (Save Current Game) | boot.c:47935 |
| `l11ac` | L11ac | l | L11ac (CODE 17 + 0x11ac) — build + run one frame of the review screen: seed the marker, draw the grid (L09dc) and frame (L079a), then register the shape-5 grid DLItem (action proc jt562) plus the Exit | boot.c:23260 |
| `l11ca` | L11ca | l | L11ca (CODE 3+0x11ca) — one compaction pass: pick an orphaned group (loaded but referenced by no freemap entry) and release it via L103c, freeing its bytes from the tail | boot.c:44464 |
| `l11f8` | L11f8 | l | L11f8 (CODE 4+0x11f8) — the depth-0 variant of the same conversion (different source pixel packing on the Mac); the port sources 8-bit pixels either way, so it shares L112c's core. | boot.c:17600 |
| `l120c` | L120c | l | L120c — case 11 (Load Saved Game) | boot.c:47999 |
| `l1240` | L1240 | l | L1240 (CODE 22 + 0x1240) — the tool-0 click with the editor unlocked (the wall-pencil edit arm, ~1.4KB) | boot.c:41463 |
| `l1266` | L1266 | l | L1266 (CODE 12 + 0x1266) — list-filter helper | boot.c:22473 |
| `l1276` | JT[892] | l | static void jt892(const unsigned char *rec); JT[892] CODE 19+0x1abe (below) | boot.c:29228 |
| `l12a0` | L12a0 | l | L12a0 (CODE 12 + 0x12a0) — View Character dispatcher | boot.c:26668 |
| `l13e8` | L13e8 | l | L13e8 (CODE 3 + 0x13e8, CODE-local) — keyboard shortcut matcher | boot.c:7236 |
| `l13ee` | — | l | — | boot.c:20778 |
| `l143e` | L143e | l | L143e (CODE 15 + 0x143e) — open the picked slot file for READ and run the load serializer (jt579) over it, the mirror of jt585's L00e0/jt580 writer | boot.c:28927 |
| `l1476` | L1476 | l | L1476 (CODE 20 + 0x1476) — marker depth probe | boot.c:31054 |
| `l148a` | L148a | l | L148a (CODE 3 + 0x148a) — per-item glyph/marker paint dispatcher | boot.c:5195 |
| `l14bc` | — | l | — | boot.c:37023 |
| `l14d0` | L14d0 | l | L14d0 (CODE 3 + 0x14d0) — shape-3 (list-row / radio-item) paint | boot.c:7738 |
| `l1554` | L1554 | l | L1554 (CODE 6 + 0x1554) — recompute movement (player[396]) and the attacks-per-round count (g_a5_25263) | boot.c:28482 |
| `l157c` | — | l | — | boot.c:40068 |
| `l159a` | — | l | — | boot.c:3101 |
| `l15ae` | L15ae | l | L15ae (CODE 7+0x15ae) — mode-12 post-pick hook | boot.c:43648 |
| `l15bc` | — | l | — | boot.c:39272 |
| `l15e2` | L15e2 | l | L15e2 (CODE 12 + 0x15e2) — design-delete confirmation dialog | boot.c:22514 |
| `l162e` | L162e | l | L162e is just an alias for JT[176] — the L address is what CODE 7's internal call sites use; the JT entry is the same body at CODE 7 + 0x162e | boot.c:38887 |
| `l1672` | L1672 | l | L1672 (CODE 17 + 0x1672) — per-ability racial/class min-max + aging finalize, called by the L24d2 stat roll once per ability i (0=STR..5=CHA) | boot.c:20327 |
| `l1676` | — | l | — | boot.c:7277 |
| `l16c6` | — | l | — | boot.c:39949 |
| `l177a` | L177a | l | L177a (CODE 7+0x177a) — lay the "Press <Return> to continue." prompt: pump (jt66/L6048), open the frame (jt108 + L162e), draw the text (jt94), build the Return button DLItem (jt447 + jt452), paint (jt | boot.c:18591 |
| `l1798` | — | l | — | boot.c:11997 |
| `l17ca_c22` | L17ca | l | CODE 22 locals L17ca (per-entry state advance) and L2180 (entry box clear) — leaf PROBE stubs pending their own lifts. | boot.c:41666 |
| `l17e2` | — | l | — | boot.c:45546 |
| `l1806` | — | l | g_press_to_continue (forward-declared up by g_walk_input): set only while the "Press [Return] to continue" event/encounter modal (l1806) is polling, so l2d3e dismisses it on a keypress WITHOUT affecti | boot.c:29628 |
| `l185e` | L185e | l | L185e — Human Change Class "Drop NAME forever?" confirmation arm jt918 case 7 jsrs into | boot.c:27430 |
| `l1888` | L1888 | l | L1888 (CODE 13+0x1888) — select the projectile sprite frame | boot.c:37961 |
| `l18c8` | L18c8 | l | L18c8 (CODE 18+0x18c8) — encode an (index,value) effect pair back into a magnitude byte (the inverse of l18fa): index 18 -> value+1, else index+100. | boot.c:32507 |
| `l18fa` | — | l | — | boot.c:24941 |
| `l1908` | L1908 | l | L1908 (CODE 22 + 0x1908) — commit a first-person move/turn: normalise the new facing to [1,8], write the party cell (-12287 row / -12288 col / -12286 facing), recentre the view (JT[218]), mirror the c | boot.c:11845 |
| `l1944` | L1944 | l | L1944 / L1972 (CODE 13 + 0x1944 / 0x1972) — map column -> screen x: the native (v*32)/3 + 24 in render mode 3, else the doubled-space v*4 + 8004 | boot.c:25898 |
| `l1972` | — | l | — | boot.c:25906 |
| `l19a0` | L19a0 | l | L19a0 (CODE 13 + 0x19a0) — draw tile glyph `c` (GLIB handle `e`) at map cell (x,y): in render mode 3 convert to the native screen ((v*32)/3+24) and blit via jt108+jt1001 (the faithful jt118 forwarding | boot.c:26389 |
| `l19ac` | JT[898] | l | JT[898] (CODE 19 + 0x19ac) — the per-class experience rows at the bottom of the char-gen sheet | boot.c:29362 |
| `l19b2` | — | l | — | boot.c:24959 |
| `l19f8` | — | l | — | boot.c:24975 |
| `l1a0c` | L1a0c | l | L1a0c (CODE 7 + 0x1a0c) — prompt-string word splitter | boot.c:38618 |
| `l1ad8` | — | l | — | boot.c:3116 |
| `l1aea` | L1aea | l | L1aea (CODE 7 + 0x1aea) — per-row roster paint loop | boot.c:38728 |
| `l1b14` | L1b14 | l | L1b14 (CODE 18+0x1b14) — recompute one of a combat actor's derived stat channels (`class`) from its active effects | boot.c:23885 |
| `l1ba4` | L1ba4 | l | L1ba4 (CODE 6 + 0x1ba4) — the to-hit / damage style adjustment keyed on player[119] (a low attribute score): 1-3 -> -4, 4-6 -> -3..-1, 15-18 -> +1..+4, 19-20 -> +4, 21-23 -> +5, 24-25 -> +6, else 0. | boot.c:28251 |
| `l1bfe` | L1bfe | l | L1bfe (CODE 7 + 0x1bfe) — roster-row content renderer | boot.c:38829 |
| `l1c92` | — | l | — | boot.c:27710 |
| `l1c92_c6` | L1c92 | l | L1c92 (CODE 6 + 0x1c92) — a second adjustment keyed on player[119] (used by L08f4 as a magic-weapon to-hit term): 0-2 -> -4, 3-5 -> -3..-1, 6-15 -> 0, 16-18 -> +1..+3, 19-20 -> +3, 21-23 -> +4, 24-25 | boot.c:28403 |
| `l1cd2` | — | l | — | boot.c:27711 |
| `l1d54` | L1d54 | l | L1d54 (CODE 6 + 0x1d54) — map a character's Strength (player[113], plus the exceptional-strength percentile player[124] when STR is 18) to the dense 0..30 index the weight/encumbrance tables key on | boot.c:28181 |
| `l1dd6` | CODE 14 + 0x1dd6 | l | CODE 14 + 0x1dd6 (local, ~292 B) — pick the next target from the already-built area list (repeat picks, cnt != 0, no area re-aim) | boot.c:29924 |
| `l1dd8` | L1dd8 | l | L1dd8 (CODE 8 + 0x1dd8) — take focus: broadcast cmd 128 (defocus) to every DLItem, stage the revert copy (edit buffer -> bound record; 39 bytes for the small field, 250 for jt328's big one), set bit 2 | boot.c:42244 |
| `l1e30` | L1e30 | l | L1e30 (CODE 20+0x1e30) — single-target attack/effect resolution: roll the effect's XdY+Z damage (jt873 dice + ev[11] bonus), run the saving throw (jt862 for save-category 3, else jt866), deal the resu | boot.c:32128 |
| `l1e58` | L1e58 | l | L1e58 (CODE 6 + 0x1e58) — the AD&D Strength "to-hit" adjustment, keyed on the Strength index (L1d54): -3 at STR 1-3 .. | boot.c:28359 |
| `l1eec` | L1eec | l | L1eec (CODE 8 + 0x1eec) — drop focus: the no-flag path runs the L20a0 commit hook on the edit buffer; clear bits 2 + 7, raise -11622. | boot.c:42262 |
| `l1efa` | CODE 14 + 0x1efa | l | CODE 14 + 0x1efa (local) — one target pick for jt538 | boot.c:29956 |
| `l1f18` | L1f18 | l | L1f18 (CODE 8 + 0x1f18) — map the current mouse position (JT[1113]) into a cursor index for the field at (v, h): JT[1139] grid-maps the click (outs land in the v/h slots like the Mac), the column /4 g | boot.c:42277 |
| `l1f3e` | L1f3e | l | L1f3e (CODE 7 + 0x1f3e) — size the button bar to the active party | boot.c:39473 |
| `l1f3e_c6` | L1f3e | l | L1f3e (CODE 6 + 0x1f3e) — the AD&D Strength "damage" adjustment, keyed on the Strength index (L1d54): -2 at STR 1-2 .. | boot.c:28382 |
| `l1f6c` | L1f6c | l | L1f6c (CODE 8 + 0x1f6c) — map the mouse position into a cursor index on the 38x6 grid (row via the segment table, col /4) | boot.c:42599 |
| `l1f76` | — | l | — | boot.c:3105 |
| `l2000` | L2000 | l | L2000 (CODE 6 + 0x2000) — the AD&D weight-allowance adjustment (tenths of a pound) for a character's Strength index (via L1d54): negative for weak STR, climbing steeply for 18/xx and giant grades. | boot.c:28206 |
| `l2062` | — | l | — | boot.c:27641 |
| `l206e` | L206e | l | L206e (CODE 7 + 0x206e) — prompt cluster builder | boot.c:38960 |
| `l20a0` | L20a0 | l | L20a0 (CODE 8 + 0x20a0) — reset the field record's segment mode pair (+248 = 0, +249 = 6) — the commit hook L1eec runs on defocus. | boot.c:41943 |
| `l20c2` | L20c2 | l | L20c2 (CODE 8 + 0x20c2) — find `pos`'s segment in the +236 table (up to 6 rows of (start, end) byte pairs at buf[236+2i]/buf[237+2i]) and, when `pos` sits exactly on a segment end with room to advance | boot.c:41957 |
| `l216a` | — | l | — | boot.c:3110 |
| `l2170` | — | l | — | boot.c:39270 |
| `l217e` | L217e | l | L217e / L2170 (CODE 7 + 0x217e / +0x2188) — getter/setter for the alert's button-base index in g_a5_-13016 (the word L206e stamps during layout) | boot.c:39268 |
| `l2180` | — | l | — | boot.c:41671 |
| `l2184` | L2184 | l | L2184 (CODE 7 + 0x2184) — prompt-word extractor | boot.c:38491 |
| `l21a4` | L21a4 | l | L21a4 (CODE 8 + 0x21a4) — hop the cursor one segment over (`down` != 0 hops to the previous segment, 0 to the next): keep the column within the segment (clamped 0..37), land on the target's start + co | boot.c:41985 |
| `l21d0` | L21d0 | l | L21d0 (CODE 5 + 0x21d0, CODE-local) — clip-mask helper used by jt995 to compute a partial-pixel byte mask | boot.c:4863 |
| `l2284` | L2284 | l | CODE 17-local record finalizers jt572 calls (L2284 = pre-finalize, L13ee = derived-field setup), plus the CODE 19 stat-finalize entries JT[907] (run when rec[163] > 0) / JT[906] | boot.c:20777 |
| `l2338` | L2338 | l | L2338 (CODE 8 + 0x2338) — draw ONE field cell: in-range cells via jt1089 "%c" at h + idx*4; the past-the-end cursor block as a jt1161 fill (top/bottom in the RAW v, left/right in the jt1135-mapped h | boot.c:42209 |
| `l238e_c17` | L238e | l | L238e (CODE 17 + 0x238e) — character-NAME entry | boot.c:21196 |
| `l23b4` | L23b4 | l | L23b4 (CODE 7 + 0x23b4) — poll-loop opcode selector | boot.c:39170 |
| `l23d2` | L23d2 | l | L23d2 (CODE 8 + 0x23d2) — draw the field text from cell `from`: jt1089 "%s" of buf+from at h + from*4. | boot.c:42230 |
| `l23ee` | L23ee | l | L23ee (CODE 22 + 0x23ee) — the editor status-line refresh after a brush step. | boot.c:41510 |
| `l2410` | L2410 | l | L2410 (CODE 8 + 0x2410) — draw ONE grid cell: L20c2 locates `idx`'s segment for the row, the column is idx - segment start | boot.c:42590 |
| `l24aa` | L24aa | l | L24aa (CODE 4 + 0x24aa) = JT[1178] — palette restore on suspend/resume | boot.c:16569 |
| `l24d2` | L24d2 | l | L24d2 (CODE 17 + 0x24d2) — the character ability roll | boot.c:20463 |
| `l24e8` | L24e8 | l | L24e8 (CODE 8 + 0x24e8) — draw the field text from `from`, walking the segment table row by row (clamps `from` to buf[248] first) | boot.c:42580 |
| `l2558` | L2558 | l | L2558 (CODE 2 + 0x2558) — the settings/choice modal jt247 runs: paint the centered title (colour 143 at v=8034, 4-unit cells over a 38-col line), lay the prompt bar via JT[148], optionally install the | boot.c:43849 |
| `l25a8` | L25a8 | l | L25a8 (CODE 18 + 0x25a8) — re-stamp every remaining standing cloud onto the map: walk the g_a5_23234 object list and for each active segment k (obj[18+k]) write the cloud's feature byte (obj[32]) into | boot.c:33472 |
| `l25b6` | L25b6 | l | L25b6 (CODE 7 + 0x25b6) — interactive roster select | boot.c:39322 |
| `l25ce` | — | l | — | boot.c:29352 |
| `l2724` | L2724 | l | L2724 (CODE 8 + 0x2724) — zero-fill the 250-byte field buffer past the string (jt328's bind-time scrub). | boot.c:42020 |
| `l2756` | L2756 | l | L2756 (CODE 8 + 0x2756) — the segment-table maintainer: pos < 0 wipes + re-tokenizes the whole buffer into word segments (the L2dca/L2d5e char classes, 38-col lines); pos >= 0 adjusts the table around | boot.c:42035 |
| `l2788` | — | l | — | boot.c:11746 |
| `l27a4` | — | l | — | master.c:100 |
| `l27bc` | — | l | — | master.c:101 |
| `l2856` | — | l | — | boot.c:5952 |
| `l2858` | L2858 | l | L2858 (CODE 7 + 0x2858) — stash the picker "mode" word that the bar's party-row layout (L1f3e) branches on | boot.c:39459 |
| `l28b0` | — | l | static void l4d26(void *ev); message/text event — defined after its deps | boot.c:3103 |
| `l28b2` | L28b2 | l | L28b2 (CODE 18 + 0x28b2) — "clear pending effect" hook for case 6: if a context flag (-25266 bit 7 / bit 6) and the entity's matching status bit ([192] bit 4 / bit 3) are both set, clear the pending-e | boot.c:24545 |
| `l29ae` | L29ae | l | L29ae (CODE 17 + 0x29ae) — character max-HP finalize | boot.c:20275 |
| `l29cc` | — | l | — | boot.c:3122 |
| `l2aaa` | L2aaa | l | CODE 22 design-entry painters (L2aaa / L2f24 / L329c / L347a) — one per entry kind 0..3; leaf PROBE stubs pending their own lifts | boot.c:40762 |
| `l2b24` | — | l | — | boot.c:37028 |
| `l2b2a` | — | l | — | boot.c:3125 |
| `l2b40` | CODE 6+0x2b40 | l | CODE 6+0x2b40 (local) — destroy one party member record `*pp`: free the [64] sub-record (jt471 size 26 -> -20448; its byte[21] "keep" flag survives), every [8]-chain item (size 62 -> -21508; [40]==73 | boot.c:27486 |
| `l2c60` | L2c60 | l | L2c60 (CODE 3 + 0x2c60) — DLItem paint walker | boot.c:15818 |
| `l2cb0` | — | l | — | boot.c:2115 |
| `l2cf4` | JT[44] | l | static void jt44(void) { PROBE("jt44"); l5822(); } JT[44] = L5822: reblit the cached bigpic backdrop | boot.c:2690 |
| `l2d32` | — | l | static void l5676(void *ev, short t); stairs / level change — defined after its deps | boot.c:3107 |
| `l2d3e` | — | l | — | boot.c:18212 |
| `l2d4e` | CODE 5 + 0x2d4e | l | l2d4e (CODE 5 + 0x2d4e) — the GLIB bitmap blit dispatcher + clipper, the leaf L309c delegates the actual pixel write to | boot.c:4659 |
| `l2d78` | L2d78 | l | L2d78 (CODE 19+0x2d78) — apply/remove the readied magic item's side effects (the flag-bit-7 hook walk; ~500B) | boot.c:33917 |
| `l2df8` | L2df8 | l | L2df8 (CODE 8 + 0x2df8) — the line-editor keystroke primitive (FULL lift) | boot.c:42062 |
| `l2e42` | — | l | static short l673e(void *ev, short a, short *pn); encounter outcome dispatch — defined after its deps | boot.c:3113 |
| `l2f24` | — | l | — | boot.c:40764 |
| `l2f28` | L2f28 | l | L2f28 (CODE 18) — save-or-succumb to effect 55 for the node's linked entity (sub+12): roll the row-0 save with modifier `mod`, inflict effect 55 via jt871 ("is %s" with the -19876 status name), repain | boot.c:36008 |
| `l2f4c` | L2f4c | l | L2f4c (CODE 6 + 0x2f4c) = JT[29] — the multi-class predicate jt40 branches on | boot.c:27360 |
| `l2f6e` | L2f6e | l | L2f6e (CODE 19+0x2f6e) — can the active member ready `item`? Full lift | boot.c:33935 |
| `l2f74` | L2f74 | l | L2f74 (CODE 17 + 0x2f74) — fold the two alignment-axis grid coords into the record's linear option index: rec[93] = (-7024 - 1)*3 + -7022 - 1. | boot.c:20612 |
| `l2f8e` | L2f8e | l | L2f8e (CODE 17 + 0x2f8e) — re-validate the alignment grid position | boot.c:20531 |
| `l2fd8` | L2fd8 | l | L2fd8 (CODE 6 + 0x2fd8) — the higher of a class field player[idx+157] and, when L2f4c says the class has the alternate progression, player[idx+164]. | boot.c:28345 |
| `l309c` | L309c | l | L309c (CODE 5 + 0x309c) = JT[999] — the UI GLIB glyph blit entry | boot.c:4783 |
| `l309c_tile` | — | other | — | boot.c:10066 |
| `l30ba` | L30ba | l | L30ba (CODE 3 + 0x30ba) — call method(rec, cmd, ...) for items [start..end] | boot.c:15780 |
| `l30cc` | L30cc | l | L30cc (CODE 6 + 0x30cc) — allocate the record staging buffer: g_a5_-22212 = n, g_a5_-22210 = 398, g_a5_-22208 = NewPtr(n*398) | boot.c:1423 |
| `l30de` | L30de | l | L30de (CODE 17 + 0x30de) — redraw the option-list highlight: for each of the three columns, test whether the current selection ((-7024-1)*3 + col - 1) is a valid option value and enable (group 24) or | boot.c:20583 |
| `l3118` | — | l | — | boot.c:3130 |
| `l3154` | L3154 | l | L3154 (CODE 6 + 0x3154) — allocate the STRG load scratch buffer: g_a5_-21152 = arg, g_a5_-21150 = 10, g_a5_-21148 = NewPtr(arg*10) | boot.c:1152 |
| `l3198` | L3198 | l | L3198 (CODE 3 + 0x3198) — wraps JT[1125] for the event-read prelude. | boot.c:17463 |
| `l31b8` | L31b8 | l | L31b8 (CODE 3 + 0x31b8) — thin wrapper over JT[1132]. | boot.c:7210 |
| `l31cc` | L31cc | l | L31cc (CODE 6 local) — copy the default design-name string (the -13448 DATA string-pointer global) into the -22253 name buffer. | boot.c:48768 |
| `l31d4` | L31d4 | l | L31d4 (CODE 17 + 0x31d4) — pass-2 fallback: when the current selection has no valid grid cell, reset the highlight position by class (g_a5_-7018 - 1): the multi-class-ish set {6,12,14,15,16} -> (2,2), | boot.c:20510 |
| `l31dc` | L31dc | l | L31dc (CODE 6+0x31dc) — release a GLIB group slot: free its tag (jt461), mark the slot word free (-1), and clear the caller's slot pointer. | boot.c:45671 |
| `l31ea` | L31ea | l | L31ea (CODE 3 + 0x31ea) = JT[441] — JT[1118] passthrough returning "should continue?" boolean. | boot.c:17472 |
| `l31f0` | L31f0 | l | L31f0 (CODE 3 + 0x31f0) = JT[439] — JT[1133] tail call. | boot.c:17563 |
| `l31fc` | L31fc | l | L31fc (CODE 3 + 0x31fc) — strip a path to the bare filename after the last ':' | boot.c:409 |
| `l322c` | L322c | l | L322c (CODE 3 + 0x322c) — path-type classifier | boot.c:385 |
| `l328e` | L328e | l | L328e (CODE 3 + 0x328e) — open a file | boot.c:449 |
| `l329c` | — | l | — | boot.c:40766 |
| `l32e2` | — | l | jt398's CODE-3 callees — all stubs. | boot.c:373 |
| `l3328` | — | l | — | boot.c:3119 |
| `l3386` | L3386 | l | L3386 (CODE 3+0x3386) — create the file with a Mac type/creator stamp | boot.c:40019 |
| `l338c` | L338c | l | L338c (CODE 6 + 0x338c) — select the library load-mode byte (g_a5_-18396) that L33ac/jt997 pass to the loader as the file kind: the caller's `mode` in deep mode (jt1200()==3, the .tlb tile path), else | boot.c:46548 |
| `l33ac` | — | l | — | boot.c:46190 |
| `l341a` | L341a | l | L341a (CODE 3+0x341a) — the Mac SFPutFile save dialog ("File to save" picker; no GEMDOS equivalent yet) | boot.c:40007 |
| `l347a` | — | l | — | boot.c:40768 |
| `l35d6_c8` | L35d6 | l | L35d6 (CODE 8+0x35d6 — NOT jt416's CODE 3+0x35d6) — paint one command-bar menu title bar | boot.c:44056 |
| `l35e2` | — | l | — | master.c:102 |
| `l35f8` | L35f8 | l | L35f8 (CODE 17 + 0x35f8) — draw the four character-creation PICK headers (race / alignment / gender / class) via the jt1089 text path, at their 8000-anchored screen coords | boot.c:20239 |
| `l364e` | — | l | — | boot.c:3121 |
| `l3666` | L3666 | l | L3666 (CODE 17 + 0x3666) — character-creation screen init + header draw | boot.c:21235 |
| `l366a` | — | l | — | boot.c:4218 |
| `l36e0` | L36e0 | l | L36e0 (CODE 6 local) — claim a -18468 binder slot for the "activ" TILE group: release the previous binder (l31dc) if any, take the first free slot (word[0] < 0), stamp it (word[0] = group = index+2, w | boot.c:48793 |
| `l3736` | L3736 | l | L3736 (CODE 5 local) — count the pieces in a GLIB pool: check the 'GLIB' magic in the 16-byte header ("LBCount: Invalid Library File" otherwise) and return the count word at +8. | boot.c:48677 |
| `l3780` | L3780 | l | L3780 (CODE 6 + 0x3780) — realize a loaded GLIB group: resolve the group tag (the handle's first word) via jt468, then count its entries (jt1018). | boot.c:46635 |
| `l3792` | L3792 | l | CODE 22 local L3792 (cell -> pixel transform with scroll) — leaf PROBE stub; reports failure through -1 coords | boot.c:41701 |
| `l37aa` | L37aa | l | L37aa (CODE 5 + 0x37aa) — GLIB library item lookup | boot.c:5897 |
| `l3804` | L3804 | l | L3804 (CODE 6+0x3804) — blit one GLIB cell at raw 8000-space (c1,c2). | boot.c:46467 |
| `l3806` | L3806 | l | L3806 (CODE 22 + 0x3806) — the FLAT automap renderer (jt304's depth-0 path; the live top-down area map) | boot.c:12663 |
| `l380a` | — | l | — | boot.c:3114 |
| `l3834` | L3834 | l | L3834 (CODE 5+0x3834) — LBISize: the byte size of list-block item `item` | boot.c:43554 |
| `l384c` | CODE 6+0x384c | l | CODE 6+0x384c (local) — blit the bound library slot's (group, item) pair at cell (row, col): jt1001(row*4+8000, col*4+8000, slot[0], slot[1]) | boot.c:3754 |
| `l3880` | L3880 | l | L3880 (CODE 6+0x3880) — blit one GLIB cell at cell (a,b): each cell is 4 units, offset into the 8000-origin view space. | boot.c:46476 |
| `l38bc` | — | l | — | boot.c:3128 |
| `l38d0` | L38d0 | l | L38d0 (CODE 6 + 0x38d0) — one-shot GWorld arm for the offscreen marker layer: unless already armed (-18395) or in mode 2 (-18394), optionally allocate the GWorld page (jt1146, only when `flag`) and lo | boot.c:46646 |
| `l3918` | CODE 6 + 0x0444 | l | static void l0444(void) { PROBE("l0444"); } CODE 6 + 0x0444 | boot.c:1889 |
| `l398a` | — | l | — | boot.c:3127 |
| `l3994` | L3994 | l | L3994 — GrafPort save / paint-state commit | boot.c:6149 |
| `l3998` | — | l | — | boot.c:41502 |
| `l39ae` | L39ae | l | L39ae (CODE 3 + 0x39ae) — strlen, returns short | boot.c:3981 |
| `l3a1a` | L3a1a | l | L3a1a (CODE 22 + 0x3a1a) — draw one flat-automap cell | boot.c:12549 |
| `l3a32` | — | l | — | boot.c:3124 |
| `l3ac6` | — | l | — | boot.c:3118 |
| `l3b0e` | L3b0e | l | L3b0e (CODE 20 + 0x3b0e) — the encounter PROMPT | boot.c:31477 |
| `l3b1e` | L3b1e | l | L3b1e (CODE 6+0x3b1e) — release one piece of the -27866 combat overlay GLIB (args: 0L, 0, 0, group handle, piece id) | boot.c:32251 |
| `l3bda` | L3bda | l | L3bda (CODE 3 + 0x3bda) — case-insensitive string equal | boot.c:4140 |
| `l3bee` | L3bee | l | L3bee (CODE 20 + 0x3bee) — queue encounter `v` into the 8-slot pending table at -4938 | boot.c:31407 |
| `l3c24` | L3c24 | l | L3c24 (CODE 6+0x3c24) — message-grid cell (idx) -> pixel pen for the -27866 glyph group | boot.c:41734 |
| `l3cb2` | L3cb2 | l | L3cb2 (CODE 6 local) — free all 10 GLIB binder slots (the -18468 pool, 6-byte stride): word[0] < 0 marks a slot free. | boot.c:48779 |
| `l3cd4_c17` | L3cd4 | l | L3cd4 (CODE 17 + 0x3cd4) — proficiency / spell-school bitfield finalize (jt575) | boot.c:21081 |
| `l3cd6` | — | l | — | boot.c:3120 |
| `l3cfa` | L3cfa | l | L3cfa (CODE 3 + 0x3cfa) — strcpy(dst, basename_after_colon(src)) | boot.c:4161 |
| `l3d3a` | L3d3a | l | L3d3a (CODE 18 local) — paralysis inflict on the effect's linked entity (rec[64]+12 -> -25250): row-0 save with `savemod`, then effect 52 for 10(+save) rounds — "is paralyzed" if the target carries ef | boot.c:36866 |
| `l3d8c` | L3d8c | l | L3d8c / L7de0 / L448c / L4350 / L1084 — L3e38's helper chain | boot.c:16578 |
| `l3dda` | L3dda | l | L3dda (CODE 18 + 0x3dda) — clear the pending-damage scratch: when `param` is 0 (unconditional) or matches the staged effect code g_a5_25268, zero the damage word g_a5_25242 and the code itself. | boot.c:33613 |
| `l3dfe` | L3dfe | l | L3dfe (CODE 18 + 0x3dfe) — apply effect `type` to `rec` (magnitude `mag`, duration `dur`, announce flag 1 via jt876) unless the suppress flag g_a5_25258 is up | boot.c:33638 |
| `l3e0c` | — | l | — | boot.c:40375 |
| `l3e38` | L3e38 | l | L3e38 (CODE 4 + 0x3e38) = JT[1162] — window content repaint dispatcher | boot.c:16651 |
| `l3e40` | L3e40 | l | L3e40 (CODE 18 + 0x3e40) — negate the pending damage when its element flags (g_a5_25266) intersect `mask` (immunity gate over l3dda). | boot.c:33627 |
| `l3e50` | — | l | — | boot.c:44701 |
| `l3e62` | L3e62 | l | L3e62 (CODE 3 + 0x3e62) — JT[400]'s custom-handler lookup | boot.c:26867 |
| `l3e6e` | L3e6e | l | L3e6e (CODE 18 + 0x3e6e) — +1 save-modifier / +1 throw on the apply pass (the inverse of the -1 pair in jt768's else arm). | boot.c:35566 |
| `l3eea` | L3eea | l | L3eea (CODE 6+0x3eea) — commit the GLIB picture + palette for handle *p | boot.c:46487 |
| `l3ef8` | L3ef8 | l | L3ef8 (CODE 20 + 0x3ef8) — post-event view refresh | boot.c:31363 |
| `l3f22` | L3f22 | l | L3f22 (CODE 20 + 0x3f22) — pre-move event predicate / prompt | boot.c:31301 |
| `l3f3c` | L3f3c | l | L3f3c (CODE 6 + 0x3f3c) — install the loaded bigpic's own palette into the upper colour range [lo..hi] | boot.c:46563 |
| `l3f88` | L3f88 | l | L3f88 (CODE 6+0x3f88) — five-arg passthrough to JT[1161] (the filled-rect painter); the Mac kept it as a separate thunk. | boot.c:40887 |
| `l3fb2` | L3fb2 | l | L3fb2 (CODE 6 local) — audio bring-up tail: jt1009(8094, 1), select channel value 143 (jt978), enable replay (jt976(1)). | boot.c:48552 |
| `l3fba` | — | l | — | boot.c:3115 |
| `l3fd8` | — | l | — | boot.c:12975 |
| `l4010` | L4010 | l | L4010 (CODE 5) — _LBConvert: validate the just-loaded 'GLIB' header in place, then relocate its index table | boot.c:45318 |
| `l40b4` | — | l | — | boot.c:3104 |
| `l4108` | L4108 | l | L4108 (CODE 20 + 0x4108) — stash the current map position into the live record's saved-position fields (rec[23]/rec[24]) | boot.c:31281 |
| `l4144` | — | l | — | boot.c:3076 |
| `l4184` | L4184 | l | L4184 (CODE 20 + 0x4184) — apply a freshly-loaded saved position to the live cursor | boot.c:31333 |
| `l4226` | — | l | — | boot.c:11673 |
| `l423e` | L423e | l | L423e / L3998 (CODE 22 + 0x423e / 0x3998) — repaint the brush cell after a step (unlocked / locked variants). | boot.c:41497 |
| `l4268` | L4226 | l | static void l4226(void *rec) { PROBE("L4226"); (void)rec; } CODE 11-local | boot.c:11674 |
| `l429c` | CODE 7+0x2858 | l | static void l2858(short mode); CODE 7+0x2858 | boot.c:12129 |
| `l42a0` | L42a0 | l | L42a0 (CODE 6 + 0x42a0) — draw `ch` `count` times from char-cell (col,row), each glyph one cell (4 units in 8000-space) apart, in colour (s6<<4)\|s5 (s6 defaults to style 8) | boot.c:6447 |
| `l42c2` | — | l | static int jt41(long handle_long, short find_byte, void *descriptor); defined below | boot.c:2944 |
| `l4334` | — | l | — | boot.c:29353 |
| `l4336` | — | l | — | boot.c:3075 |
| `l433a` | L433a | l | L433a (CODE 6 + 0x433a) — is `ch` a word-break delimiter? Returns the char (non-zero) when it's one of "()[]{}-.,?!\":;", else 0. | boot.c:6437 |
| `l4350` | — | l | — | boot.c:16615 |
| `l435a` | L435a | l | L435a (CODE 6+0x435a) — slow-text per-glyph pacing: accumulate the party speed (handle[18]) into g_a5_-17522, convert each 6 units into a target tick on g_a5_-17526, and busy-wait jt1134() up to that | boot.c:18562 |
| `l43ac` | L43ac | l | L43ac (CODE 20 + 0x43ac) — clear the "available" bit for encounter `idx` in the live record's per-level once-only bitmap | boot.c:31390 |
| `l442e` | L442e | l | L442e (CODE 20 + 0x442e) — the event-display PAINTER | boot.c:31116 |
| `l4430` | L4430 | l | L4430 (CODE 22 + 0x4430) — draw one automap cell | boot.c:12805 |
| `l448c` | L448c | l | L448c (CODE 4 + 0x448c) — probe current screen pixel depth | boot.c:16606 |
| `l4580` | L4580 | l | L4580 (CODE 18, local) — the avoidance roll: when the active member holds an item and stands more than 1 cell from `rec` (jt494 path distance), a 1d100 at or under `pct` lets rec "Avoid it": staged da | boot.c:35652 |
| `l45d6` | L45d6 | l | L45d6 (CODE 3 + 0x45d6) — C string -> Pascal string | boot.c:426 |
| `l45ea` | L45ea | l | L45ea (CODE 18) — the weapon in use for damage purposes: jt491's resolution when it is conclusive and produced an item, otherwise fall back to the wielded item member[12] itself (NULL when unarmed). | boot.c:35938 |
| `l4648` | L4648 | l | L4648 (CODE 3+0x4648) — islower: 'a' <= ch <= 'z' | boot.c:40389 |
| `l466a` | L466a | l | L466a (CODE 3 + 0x466a) — isupper | boot.c:3969 |
| `l46b2` | L46b2 | l | JT[CHAR_AC] / L46b2 (CODE 3 + 0x46b2) — tolower | boot.c:3975 |
| `l46e0` | — | l | — | boot.c:29354 |
| `l4738` | L47f2 | l | static void l47f2(void) { PROBE("L47f2"); } CODE 20-local | boot.c:3296 |
| `l473e` | — | l | — | boot.c:3294 |
| `l475e` | L475e | l | L475e (CODE 22 + 0x475e) — "cell protected" predicate gating tool 3's pick in edit mode; stub returns 0 (permissive) until lifted. | boot.c:41480 |
| `l476e` | L476e | l | L476e (CODE 11 + 0x476e) — set up the play-view interior rect | boot.c:12138 |
| `l47f2` | L473e | l | static void l473e(short a) { PROBE("L473e"); (void)a; } CODE 20-local | boot.c:3295 |
| `l4810` | — | l | — | boot.c:12174 |
| `l48f4` | L48f4 | l | L48f4 = JT[670] (CODE 16+0x48f4) — the "casts a spell" announce, full lift | boot.c:37929 |
| `l4900` | L4900 | l | L4900 (CODE 22+0x4900) — diagonal-click direction gate: the current facing (JT[358]) when it's a cardinal code (<= 4), else 0. | boot.c:40703 |
| `l4910` | L4910 | l | L4910 (CODE 7+0x4910) — run one triggered cell event | boot.c:44245 |
| `l494e` | L494e | l | L494e (CODE 22 + 0x494e) — the "Select a Design" picker | boot.c:40151 |
| `l49e6` | L49e6 | l | CODE 16 local L49e6 — the low half of the -24066 effect-handler fill (ids 1..44), tail-called by jt610; a true local, no JT export. | boot.c:30928 |
| `l4a30` | L4a30 | l | L4a30 (CODE 7 + 0x4a30) — byte offset of string `count` in the packed data: sum of the first `count` length-table entries, skipping any 255 (unused-slot) marker. | boot.c:23648 |
| `l4a7a` | L4a7a | l | L4a7a (CODE 7+0x4a7a) — char -> 6-bit code for the packed-name coding: lowercase folds (-32 past 95), then & 63 | boot.c:43130 |
| `l4ab6` | L4ab6 | l | L4ab6 (CODE 7 + 0x4ab6) — point the string-table cursors at a (possibly new) base | boot.c:795 |
| `l4b64` | L4b64 | l | L4b64 (CODE 18) — "needs a +N weapon to hit": zero the staged damage (-25242) unless the active member's weapon-in-use enchantment (item[48], sign-extended) covers `need` — or, unarmed, the member is | boot.c:35957 |
| `l4b84` | L4b84 | l | L4b84 (CODE 6+0x4b84) — thin wrapper over jt175 (the modal prompt). | boot.c:18618 |
| `l4bac` | L4bac | l | L4bac (CODE 6 + 0x4bac) — message-area scroll advance | boot.c:27343 |
| `l4c46` | L4c46 | l | L4c46 (CODE 6+0x4c46) — the pagination "press a key to continue" pause: pump events (L6048) then run the modal prompt (L4b84 -> jt175). | boot.c:18622 |
| `l4c88` | — | l | — | boot.c:23670 |
| `l4cc0` | L4cc0 | l | L4cc0 (CODE 6 + 0x4cc0) — design subsystem bring-up: allocate every per-design buffer at design open | boot.c:1167 |
| `l4d26` | L4d26 | l | L4d26 (CODE 20 + 0x4d26) — the MESSAGE / event-text handler (l709e cases 2/14) | boot.c:31629 |
| `l4d88` | L4d88 | l | L4d88 (CODE 4 + 0x4d88) — flush deferred _InvalRect | boot.c:16239 |
| `l4d98` | L4d98 | l | L4d98 (CODE 6+0x4d98) — the game-session init proper | boot.c:48879 |
| `l4db4` | L4db4 | l | L4db4 (CODE 7 + 0x4db4) — set up the string-table region: the 6-byte header (count@0, -1@2, 0@4) followed by a 400-byte index at +6, then a zero-filled body | boot.c:811 |
| `l4dee` | CODE 14 + 0x4dee | l | CODE 14 + 0x4dee (local) — repeat-pick for the area modes that re-aim per target (cnt != 0, areaflag set, byte6 != 8): re-runs the jt508 area scan around the spell's special-case rules (codes 87 / 51 | boot.c:29940 |
| `l4e12` | — | l | — | boot.c:16197 |
| `l4e3a` | L4e3a | l | L4e3a (CODE 7 + 0x4e3a) — re-point the cursors at `base` (L4ab6) then byte-swap the 3-word string-table header into native order. | boot.c:834 |
| `l4e56` | L4e56 | l | L4e56 (CODE 19 + 0x4e56) — "import character?" gate for jt904's jt155(5) call | boot.c:29516 |
| `l4ec6` | L4ec6 | l | L4ec6 (CODE 19 + 0x4ec6) — "export character?" gate for jt904's jt155(6) call | boot.c:29561 |
| `l4f22` | CODE 13+0x4f22 | l | CODE 13+0x4f22 — combat entry staging (run once after jt542). | boot.c:32283 |
| `l4f2c` | — | l | — | boot.c:29459 |
| `l4f9a` | — | l | — | boot.c:3108 |
| `l4faa` | — | l | — | boot.c:39859 |
| `l4fae` | L4fae | l | L4fae / L4e12 / L5d8c — InvalRect dispatch helpers | boot.c:16188 |
| `l4fbe` | L4fbe | l | L4fbe (CODE 7 + 0x4fbe) — decompress string `index` of record `rec` into `out` | boot.c:23707 |
| `l4ff6` | — | l | — | boot.c:29460 |
| `l501e` | L501e | l | L501e (CODE 7+0x501e) — position the list-dialog scroll at row `n` | boot.c:44322 |
| `l50fe` | L50fe | l | L50fe (CODE 7 + 0x50fe) — paint the top-down area map | boot.c:2532 |
| `l5124` | JT[399] | l | JT[399] is the engine's "fill / zero buffer" service in this context | boot.c:48250 |
| `l5126` | L5126 | l | L5126 (CODE 11 + 0x5126) — the deep dungeon status-header panel jt240 draws above the first-person view | boot.c:13276 |
| `l5274` | L5274 | l | L5274 (CODE 19 + 0x5274) — the theoretical max-HP ceiling for the character's current class levels: per class, (die size -22993 + jt22 per-die CON bonus) x effective level below name level -23007, the | boot.c:34674 |
| `l52b8` | L52b8 | l | L52b8 (CODE 7 + 0x52b8) — step the area-map scroll origin | boot.c:2423 |
| `l52f2` | L52f2 | l | L52f2 (CODE 11 + 0x52f2) — draw one automap cell: map the cell's stored (x,y) to the screen (jt296); if it lands in the map window, draw its column number via jt1089 (colour alternates per map row) | boot.c:13053 |
| `l5304` | L5304 | l | L5304 (CODE 6 local) — load "item.dat" (the 255 x 18-byte item record table) through the GLIB loader, jt87 as the callback. | boot.c:48858 |
| `l534a` | L534a | l | L534a (CODE 6+0x534a) — redraw the active GLIB view (handle g_a5_-24320): in deep mode (jt1200()==3) at raw 8000-space via L3804, else at cell space via L3880; frame 1 (when the count `d` is non-zero) | boot.c:46520 |
| `l5368` | L5368 | l | L5368 (CODE 7 + 0x5368) — one-axis scroll-window solver for the overland map | boot.c:12006 |
| `l53a6` | L53a6 | l | L53a6 (CODE 11 + 0x53a6) — the automap screen's title + area-list panel (L63c0's wilderness-arm header) | boot.c:13099 |
| `l541a` | L541a | l | L541a (CODE 6 + 0x541a) — load a GLIB backdrop/sprite resource of `type` ("PIC", "SPRIT", ...) and id into the slot struct `buf` (b[0]=frame count, b[1]=static-last flag, b[2..]=group handle) | boot.c:46684 |
| `l5484` | L5484 | l | L5484 (CODE 7 + 0x5484) — classify map cell (row,col) edge `dir` for the area map: 0 = nothing (no edge present, L5e52==0), else map the wall-style code (L5bfa, 0..15) to 2 for the "open/secret" style | boot.c:2469 |
| `l54ac` | L54ac | l | L54ac (CODE 18 + 0x54ac) — rebuild the memorized-spell list after a level change: walk the 141 memorized-spell bytes at rec[198..338], counting kept spells per class/level against the slot limits at r | boot.c:34897 |
| `l54f2` | L54f2 | l | L54f2 (CODE 7 + 0x54f2) — draw one area-map cell at screen (sy,sx) | boot.c:2491 |
| `l555a` | L555a | l | CODE 14 local L555a — fire a ray: jt497(kind) sprite setup, then the animated jt501 trajectory line from the shooter to the target (anim 1, dwell 30). | boot.c:38194 |
| `l5586` | — | l | — | boot.c:3109 |
| `l55d0` | L55d0 | l | CODE 14 local L55d0 — pick the next ray target and walk the shot line: jt546 picks into the sub-record target slot (+12); on a pick, jt506 walks the line from the shooter to the target with *dist as t | boot.c:38214 |
| `l5676` | L5676 | l | L5676 (CODE 20 + 0x5676) — the STAIRS / teleport / level-transition event handler (l709e cases 5/11/34) | boot.c:31522 |
| `l5700` | L5700 | l | L5700 — slot-1 mode tear-down | boot.c:3906 |
| `l5746` | L5746 | l | L5746 (CODE 18 + 0x5746) — the level-drain core ("gets drained"): per drained level, pick the victim's best class level (ties broken toward the class with the LOWER jt26 XP threshold), bank the max-at | boot.c:35273 |
| `l5752` | L5752 | l | L5752 (CODE 7 + 0x5752) — draw the party marker on the area map | boot.c:2513 |
| `l579e` | L579e | l | L579e (CODE 6 + 0x579e) — load the "bigpic" play-screen backdrop for `id`, cached against g_a5_-24256 | boot.c:46584 |
| `l57f2` | L57f2 | l | L57f2 (CODE 7 + 0x57f2) — first-person dungeon-view perspective SHELL | boot.c:2684 |
| `l5822` | L5822 | l | L5822 (CODE 6+0x5822) — full-screen backdrop refresh: (re)load the cached bigpic, then blit + commit it at cell (1,1). | boot.c:46605 |
| `l5864` | L5864 | l | L5864 — slot-2 mode tear-down | boot.c:3927 |
| `l5876` | L5876 | l | L5876 (CODE 6 + 0x5876) — forward to the song player (JT[985]). | boot.c:17921 |
| `l5888` | CODE 6 + 0x4d98 | l | static void l4d98(void); CODE 6 + 0x4d98 — lifted near EOF (the game-session init; needs the GLIB tier) | boot.c:1892 |
| `l58c4` | L58c4 | l | L58c4 (CODE 7 + 0x58c4) — overlay the cell's perspective backdrop image | boot.c:2634 |
| `l59c2` | L59c2 | l | CODE 16 locals of the spell-list dialog, PROBE stubs pending their own lifts: L59c2 builds the spell list for context `code` (returns the count byte), L4faa runs the pick loop over it. | boot.c:39853 |
| `l59ca` | L59ca | l | L59ca: a one-line wrapper — JT[981](6656). | core.c:105 |
| `l59d6` | L59d6 | l | L59d6 (CODE 6 local) — audio bring-up: open "music.slb" (jt986), then check free memory (jt459(-1)) | boot.c:48728 |
| `l5ac0` | CODE 6 + 0x5888 | l | static void l5888(short a) { PROBE("l5888"); } CODE 6 + 0x5888 | boot.c:1893 |
| `l5ac2` | L5ac2 | l | L5ac2 (CODE 6+0x5ac2) — page-up while paused: toggle the latch via JT[1140](!JT[1154]). | boot.c:18501 |
| `l5ad8` | L5ad8 | l | L5ad8 (CODE 6+0x5ad8) — page-down while paused: toggle g_a5_-17443 and feed it to the scroll dispatcher JT[983]. | boot.c:18506 |
| `l5b42` | — | l | — | boot.c:10470 |
| `l5baa` | CODE 7 + 0x5baa | l | l5baa (CODE 7 + 0x5baa) — is map cell (row, col) inside the loaded design's GEO map? The map is column-major with stride = height: the row index spans [0, ds[3]) and the column index spans [0, ds[2]) | boot.c:10136 |
| `l5bde` | — | l | — | boot.c:3123 |
| `l5bfa` | L5bfa | l | L5bfa (CODE 7 + 0x5bfa) — the wall-style code at cell (row,col) edge `dir` | boot.c:2444 |
| `l5d8c` | — | l | — | boot.c:16206 |
| `l5d92` | — | l | — | boot.c:25174 |
| `l5e0e` | L5e0e | l | L5e0e (CODE 14 + 0x5e0e) — recompute every active creature's on-screen cell (g_a5_27059[i]/g_a5_26991[i]) as its world position (g_a5_27472 record i, 6 bytes: x@0, y@2) minus the current map-scroll or | boot.c:25434 |
| `l5e52` | CODE 7 + 0x5e52 | l | l5e52 (CODE 7 + 0x5e52) — read a cell edge's MOVEMENT-TYPE code (the LOW nibble), clamping the cell to the map rather than failing: the frustum walker (JT[199]) uses it to test whether a view ray can | boot.c:10425 |
| `l5e9a` | L5e9a | l | L5e9a (CODE 14 + 0x5e9a) — draw the party's facing fan plus the creature on the party's own cell | boot.c:25762 |
| `l5f04` | L5f04 | l | L5f04 (CODE 8+0x5f04) — release/retarget the current "STR@n" monster-art resource (the -10370 slot; jt488 "%3s@%1d" name build + jt393 compare) | boot.c:1387 |
| `l5f3a` | L5f3a | l | L5f3a (CODE 6+0x5f3a) — stash the pause "cancel" key in g_a5_-13084. | boot.c:18497 |
| `l5f4e` | L5f4e | l | L5f4e (CODE 6 + 0x5f4e) — zero N bytes at buf | boot.c:3831 |
| `l5f66` | CODE 6 + 0x5ac0 | l | static void l5ac0(void) { PROBE("l5ac0"); } CODE 6 + 0x5ac0 | boot.c:1894 |
| `l5f84` | L5f84 | l | L5f84 (CODE 6+0x5f84) — the pause key reader: poll a key (jt1133), handle Esc/`(27/96 -> cancel via L5f3a) and the page-up/down keys (338/339), then drain any further pending events (jt1118) | boot.c:18517 |
| `l5fc0` | L5fc0 | l | L5fc0 (CODE 7 + 0x5fc0) — the party cell's BackdropZone selector (0..3): bounds-test the cell (L5baa), then read the 6-byte cell record's +5 byte (ds[290 + (col*H + row)*6 + 5]) and mask the low two b | boot.c:2591 |
| `l5fcc` | — | l | — | boot.c:3126 |
| `l6020` | — | l | — | boot.c:3117 |
| `l6028` | L6028 | l | L6028 (CODE 10 + 0x6028) — load MONSTnnn.DAT for monster slot `num` into the design header (g_a5_-28006 + 101) | boot.c:1294 |
| `l6048` | L6048 | l | L6048 / JT[66] (CODE 6+0x6048) — thin wrapper over L604e. | boot.c:18556 |
| `l604e` | L604e | l | L604e (CODE 6+0x604e) — pump one event: if an event is pending (jt1118) run the pause key reader (L5f84), then refresh the IKBD via jt1125(7). | boot.c:18547 |
| `l6090` | L6090 | l | L6090 (CODE 14 + 0x6090) — redraw a single world cell (px,py) and the creature on it: clip (l744e), redraw the terrain cell (l635e, party- relative), then the occupant sprite (l62ec -> g_a5_25676 -> j | boot.c:25805 |
| `l6096` | L6096 | l | L6096 (CODE 6 + 0x6096) — release one 62-byte list node back to its pool: jt471(*pnode, 62, &g_a5_-21508) | boot.c:23376 |
| `l60b0` | L60b0 | l | L60b0 (CODE 8+0x60b0) — format the spell-list header text for (kind, mask) into `out` | boot.c:41143 |
| `l60b4` | L60b4 | l | L60b4 (CODE 6+0x60b4) — decimal string of a byte into the shared -13083 scratch buffer; returns its address. | boot.c:5488 |
| `l6114` | CODE 16+0x6114 | l | CODE 16+0x6114 (local, ~660B) — the per-target effect-message applier loop (sets the -25266 damage word, walks the -23512 target slots applying the effect + message) | boot.c:30890 |
| `l6148` | L6148 | l | L6148 (CODE 7 + 0x6148) — load the current level's 3D art | boot.c:2380 |
| `l6204` | L6204 | l | L6204 (CODE 4 + 0x6204) — live mouse poll | boot.c:7158 |
| `l6256` | L6256 | l | L6256 (CODE 11 + 0x6256) — register the dungeon-walk input sources | boot.c:12275 |
| `l62e0` | L62e0 | l | L62e0 (CODE 8+0x62e0) — bind/load one monster-art entry into the -10370 slot (re-runs JT[370] for the kind flags; 255 id wildcard for mid-band kinds) | boot.c:1400 |
| `l62ec` | — | l | — | boot.c:25155 |
| `l62fa` | L62fa | l | L62fa (CODE 4 + 0x62fa) — query "where is the mouse?" state | boot.c:15876 |
| `l6330` | L6330 | l | L6330 (CODE 4+0x6330) — stage the converted cursor into the 68-byte Mac Cursor record at `dst` (A5 -892): 32B data from src+4, 32B mask from src+260, the hotspot bytes src[1]/src[0] into the words at | boot.c:17609 |
| `l635e` | L635e | l | L635e (CODE 14 + 0x635e) — redraw the cells a creature (or the party) occupies | boot.c:25592 |
| `l63c0` | — | l | — | boot.c:12320 |
| `l6432` | L6432 | l | L6432 (CODE 8+0x6432) — locate/load one monster-art record from the -10370 table | boot.c:44107 |
| `l6436` | — | l | — | boot.c:3129 |
| `l6520` | L6520 | l | L6520 (CODE 14 + 0x6520) — is a (dx,dy) within the 0..6 viewport window? | boot.c:25141 |
| `l6520_c8` | L6520 | l | L6520 (CODE 8+0x6520 — NOT the lifted CODE 14 l6520, hence the _c8 suffix) — kind byte -> art class: 1 for kinds < 6, 2 for 6..10, 3/4/5/6 for 11/12/13/14, else 0. | boot.c:1367 |
| `l6538` | L6538 | l | L6538 (CODE 4 + 0x6538) — cursor reset / refresh | boot.c:15916 |
| `l6554` | L6554 | l | L6554 (CODE 14 + 0x6554) — is the creature `entity` within the 7x7 map viewport? Resolve its zone (l6bbe), walk the 6 step offsets of its facing (g_a5_27472[zone].dir, via l5d92) and test each landing | boot.c:25456 |
| `l661c` | — | l | — | boot.c:3134 |
| `l6652` | L6652 | l | L6652 (CODE 14 + 0x6652) — scroll the map view so (tx,ty) stays within a `margin` cell window of the centre (mode 255 forces a recentre); returns 0 if no scroll was needed | boot.c:25725 |
| `l66cc` | — | l | static void l3bee(short v); encounter-queue insert — defined after its deps | boot.c:3133 |
| `l66e6` | CODE 7 + 0x2062 | l | static void jt174(void); CODE 7 + 0x2062 (lifted below) | boot.c:5207 |
| `l66e8` | L66e8 | l | L66e8 (CODE 4 + 0x66e8, CODE-local) — event-record post-process gate | boot.c:15939 |
| `l670c` | L670c | l | L670c (CODE 6+0x670c) — stand up the empty viewport: clear, frame box, and blit the four border cells, then run the view-prep tail (jt174). | boot.c:46504 |
| `l673e` | L673e | l | L673e (CODE 20 + 0x673e) — encounter OUTCOME dispatch | boot.c:31431 |
| `l67ca` | — | l | — | boot.c:5423 |
| `l67e4` | — | l | — | boot.c:12116 |
| `l6804` | L6804 | l | L6804 (CODE 4 + 0x6804) — "are we the front window?" probe | boot.c:16350 |
| `l68ae` | L68ae | l | L68ae (CODE 6 + 0x68ae) — marker-overlay toggle | boot.c:46661 |
| `l68f8` | CODE 6 + 0x66e6 | l | static void l66e6(short n); CODE 6 + 0x66e6 — lifted below | boot.c:2109 |
| `l690e` | L690e | l | L690e (CODE 4 + 0x690e) — mouseDown arm | boot.c:17014 |
| `l694e` | L694e | l | L694e (CODE 20 + 0x694e) — the event-VALID gate / condition evaluator | boot.c:2970 |
| `l694e_class_match` | — | other | l694e class-group match (case 16 inner switch on ev[2]): does class byte `cls` belong to the requested FRUA class group `which` (0..6)? | boot.c:2949 |
| `l698a` | CODE 16 + 0x698a | l | CODE 16 + 0x698a — seed a combat_walk's endpoints from (sign-extended) byte coords and run the walker init (jt509). | boot.c:30217 |
| `l69d2` | CODE 16 + 0x69d2 | l | CODE 16 + 0x69d2 — clamp a combat-map coord pair to 0..49 x 0..24 (signed byte compares; negative folds to 0). | boot.c:30229 |
| `l6a1e` | CODE 16 + 0x6a1e | l | CODE 16 + 0x6a1e — run a combat_walk ray to its end, inserting each cell's occupant (l62ec) into the NUL-terminated dedup array `arr`: an existing match stops the scan, else the first empty slot takes | boot.c:30247 |
| `l6ada` | CODE 6 + 0x5f66 | l | static void l5f66(void) { PROBE("l5f66"); } CODE 6 + 0x5f66 | boot.c:1895 |
| `l6b26` | L6b26 | l | L6b26 (CODE 4 + 0x6b26) — inContent click body | boot.c:16968 |
| `l6bbe` | L6bbe | l | L6bbe (CODE 14 + 0x6bbe) — linear search for `key` in the id-key array g_a5_25676; returns the 1-based index of the match (or the count past the last when not found) | boot.c:19976 |
| `l6cba` | L6cba | l | L6cba (CODE 4 + 0x6cba) — mouseUp arm | boot.c:16925 |
| `l6d1e` | L6d1e | l | L6d1e (CODE 13 + 0x6d1e) — sign of a word: -1 / 0 / +1. | boot.c:25891 |
| `l6d4a` | L6d4a | l | L6d4a (CODE 13, local) — sort the -19170 target list ascending by path cost (entry byte 1); cost ties prefer the lower direction code unless that trades an even (cardinal) code for an odd (diagonal). | boot.c:24288 |
| `l6dd0` | L6dd0 | l | L6dd0 (CODE 4 + 0x6dd0) — keyDown / autoKey arm | boot.c:17146 |
| `l6e50` | L6e50 | l | L6e50 (CODE 8 + 0x6e50) — g_a5_-10374 = clamp(arg, 0, 40) | boot.c:677 |
| `l6e58` | — | l | — | boot.c:44786 |
| `l6ea2` | L6ea2 | l | L6ea2 (CODE 7 + 0x6ea2) — bind the perspective backdrop art for backdrop `id`: select the backdrop load kind (jt113(50)) and bind "back1"<id> into the g_a5_-22222 slot through the FC library binder L3 | boot.c:2607 |
| `l6eba` | L6eba | l | L6eba (CODE 13 + 0x6eba) — initialise a line descriptor `r` from its endpoints: current = start, \|dx\|/\|dy\| (jt388), step signs (l6d1e), error and step count cleared. | boot.c:25918 |
| `l6eea` | — | l | — | boot.c:2331 |
| `l6f68` | L6f68 | l | L6f68 (CODE 13 + 0x6f68) — advance the line descriptor `r` one Bresenham step along its major axis (with a minor-axis step on error overflow), set the step-direction byte r[23] from the 3x3 g_a5_27956 | boot.c:25939 |
| `l7026` | L7026 | l | L7026 (CODE 16+0x7026) — the saving-throw-modified effect announce ("is held" etc with modifier d) | boot.c:44147 |
| `l7090` | L7090 | l | L7090 (CODE 4 + 0x7090) — updateEvt arm | boot.c:16817 |
| `l709e` | L709e | l | L709e (CODE 20 + 0x709e) — the dungeon EVENT DISPATCHER | boot.c:3148 |
| `l70e0` | L70e0 | l | L70e0 (CODE 4 + 0x70e0) — activateEvt arm | boot.c:16753 |
| `l71ac` | L71ac | l | L71ac (CODE 4 + 0x71ac) — osEvt suspend/resume arm | boot.c:16858 |
| `l7204` | L7204 | l | L7204 (CODE 4 + 0x7204) — diskEvt / high-level event arm | boot.c:17254 |
| `l720a` | L720a | l | L720a (CODE 7 + 0x720a) — thin wrapper: 0 if the GEO parse succeeded, -1 (error) otherwise. | boot.c:1024 |
| `l7222` | L7222 | l | L7222 / JT[369] (CODE 8 + 0x7222) — design-header post-load step: L6e50(g_a5_-18828); // active level/page, clamped 0..40 *(short *)g_a5_-12300 = 0; // reset the design-state cursor g_a5_-18828 is a b | boot.c:691 |
| `l7226` | L7226 | l | L7226 (CODE 7 + 0x7226) — parse a GEO container into the design-state buffer (g_a5_-12300) | boot.c:970 |
| `l725c` | L725c | l | L725c (CODE 4 + 0x725c) — Mac event-pump dispatcher | boot.c:16305 |
| `l731e` | L731e | l | L731e (CODE 4 + 0x731e) — shared event filter (level-2 skeleton) | boot.c:15971 |
| `l7406` | L7406 | l | L7406 (CODE 7 + 0x7406) — find `fourcc` via L7470, verify it is exactly `size` bytes, BlockMove it to `dst`, and step *walker past it (plus any odd-byte pad) | boot.c:949 |
| `l744e` | L744e | l | L744e (CODE 14 + 0x744e) — set the map-view clip rectangle: the native 24..248 box in render mode 3, else the doubled-space 8004..8088 box. | boot.c:25420 |
| `l7470` | L7470 | l | L7470 (CODE 7 + 0x7470) — IFF chunk walker | boot.c:894 |
| `l747a` | L747a | l | L747a (CODE 4 + 0x747a) — schedule menu-bar paint job | boot.c:16412 |
| `l7488` | L7488 | l | L7488 (CODE 14 + 0x7488) — resolve the *displayed* feature of map cell (cx,cy): normally the static map feature (live map g_a5_25318, cell+9), but for multi-cell object cells (27/28/29) it walks the o | boot.c:25497 |
| `l7490` | L7490 | l | L7490 (CODE 8+0x7490) — load area-map icon `n` into the -10366 art slot | boot.c:40815 |
| `l74ae` | L74ae | l | L74ae (CODE 4 + 0x74ae) — refresh after menu-bar window close | boot.c:16388 |
| `l7638` | L7638 | l | L7638 (CODE 14 + 0x7638) — 8-way compass octant from (x1,y1) toward (x2,y2): 0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW (indexes the g_a5_27862 / g_a5_27853 dx/dy tables) | boot.c:25635 |
| `l77a0` | JT[857] | l | JT[857] / L77a0 (CODE 18 + 0x77a0, 14 sites) — dispatch an effect-removal hook by item/effect type | boot.c:23846 |
| `l7894` | L7894 | l | CODE 14 locals for jt555 — leaf PROBE stubs pending their own lifts: L7894 = the facing direction from one combatant to another (0..7), L14bc = the melee attack round (the big strike resolver), L2b24 | boot.c:37017 |
| `l78fa` | L78fa | l | L78fa (CODE 14 + 0x78fa) — draw one map cell | boot.c:25540 |
| `l79d4` | L79d4 | l | L79d4 (CODE 4 + 0x79d4) — start idle-time accounting | boot.c:16141 |
| `l79ec` | L79ec | l | L79ec (CODE 4 + 0x79ec) — end idle-time accounting | boot.c:16157 |
| `l7a0e` | JT[1074] | l | JT[1074] (CODE 5+0x7c90) — print one paginated message line: spill to a new page first when the -3148 line counter reaches 56 (L7a0e, leaf stub), then L7ab4 with the "%* %r" recursive format (width, t | boot.c:40567 |
| `l7a24` | L7a24 | l | L7a24 (CODE 5+0x7a24) — the per-character page gate: nonzero while output may continue on the current page | boot.c:40478 |
| `l7ab4` | L7ab4 | l | L7ab4 = JT[1076] (CODE 5+0x7ab4) — the message-window pagination printer, full lift over the l7a24/jt433 leaf stubs | boot.c:40506 |
| `l7de0` | — | l | — | boot.c:16584 |
| `l7eb8` | L7eb8 | l | L7eb8 (CODE 5 local) — flip the sign bit of `n` sample bytes (signed <-> excess-128 8-bit PCM). | boot.c:48692 |
| `l_cch_read` | — | other | — | boot.c:28151 |
| `lc` | — | other | Lower-case one byte (ASCII), for the case-insensitive .DSN suffix test. | files.c:57 |
| `link_control` | — | other | linked-list management | controls.c:58 |
| `load_backdrop` | — | other | load_backdrop — load BACK.CTL backdrop `n` (1-based): the 88x88 8bpp floor/ceiling/sky image (sub-GLIB item 1) into g_back_img, and its own 32-colour palette (item 0) into the screen clut at BACK_PAL_ | boot.c:9413 |
| `load_color_wallset` | — | other | load_color_wallset — manual/initial path: load the current (g_cw_file, `set`) into slot 0 and leave slots 1-2 empty, so every face uses it. | boot.c:9381 |
| `load_cw_full` | — | other | load_cw_full — load all 48 pieces of wall-set (`file`,`set`) into the resident store (bodies + dims + signed bearings) | boot.c:10227 |
| `load_frua_cursors` | — | other | Optional colour mouse pointer | main.c:321 |
| `load_frua_palette` | — | other | Non-static: the engine (jt315) re-installs this on menu redraw, since the dungeon play loop (port_play_demo) overwrites clut 0..15 and doesn't restore them — the menu would otherwise paint with the du | main.c:270 |
| `load_frua_rsrc` | — | other | Open frua.rsc through the File Manager and hand the bytes to the Resource Manager shim | main.c:219 |
| `load_menu_ui` | — | other | — | boot.c:18781 |
| `load_monsters` | — | other | static short g_mdb_n = -1; -1 = not yet scanned | boot.c:14017 |
| `load_roster` | JT[589] | other | Scan the design folder for CHAR*.CHR via the FS shim's directory enumeration (the capability JT[589] needs), reading each into the pool, then relink the party from each record's CHAR_INPARTY flag | boot.c:15200 |
| `load_wall_groups` | — | other | load_wall_groups — auto path: load the level's three wall groups (Wall1-3 = ds[4..6]) into slots 0-2 so each map face can use its own set. | boot.c:9392 |
| `mac_path_to_c` | L494e | other | Translate a Mac Pascal filename into a C string suitable for GEMDOS | files.c:86 |
| `main` | — | other | — | main.c:365 |
| `make_null` | — | other | — | events.c:205 |
| `map_demo_palette` | — | other | install the map demo CLUT: 224..239 wall grey ramp, 240..243 door colours, 248..251 floor shades, 255 black | boot.c:8609 |
| `map_px` | — | other | paint one pixel with clipping against the surface | boot.c:8587 |
| `master_init` | CODE 5 + 0x4 | other | master_init — CODE 5 + 0x4 (jump-table entry 1079) | master.c:112 |
| `master_shutdown` | CODE 5 + 0x62 | other | master_shutdown — CODE 5 + 0x62 (jump-table entry 1081) | master.c:131 |
| `menu_button_bevel` | — | other | A MENU button face, matching the Mac main menu sampled from frua_mac_main_menu.png: a clut-23 face with a 1px bevel | boot.c:19388 |
| `menu_button_hit` | — | other | — | boot.c:18091 |
| `menu_button_press_draw` | — | other | — | boot.c:18109 |
| `menu_button_track` | — | other | Track a press on DLItem `rec` until the mouse button releases — the faithful l1676 cmd=3 loop (highlight while held, toggle as the cursor drags on/off) | boot.c:18177 |
| `menu_draw_plates` | — | other | — | boot.c:19558 |
| `menu_push_item` | — | other | Append a single item (raw text bytes) to the menu, growing the slab. | menus.c:108 |
| `menu_todo` | — | other | — | boot.c:19777 |
| `menubar_active` | — | other | — | menus.c:442 |
| `menubar_height` | — | other | — | menus.c:437 |
| `mouse_edge_to_event` | — | other | Last-seen mouse-button state | events.c:175 |
| `next_edit_item` | — | other | — | dialogs.c:93 |
| `node_pool_init` | — | other | — | boot.c:15160 |
| `paint_ditl` | — | other | Repaint every DITL item inside `bounds` on the screen port. | dialogs.c:243 |
| `paint_ditl_for_dialog` | — | other | Variant for DrawDialog — knows the dialog so it can show the focus caret and the default-item ring. | dialogs.c:264 |
| `paint_item` | — | other | short focus_item; editField; 0 if none | dialogs.c:172 |
| `party_passable` | JT[202] | other | party_passable — may the party leave cell (x,y) heading `f`? Per JT[202] the blocking edge is t[(f&6)>>1]; passable iff that edge's movement nibble is 0 (Free movement). | boot.c:8901 |
| `party_step` | — | other | party_step — apply a movement command to the live party globals (g_a5_-12288 x / -12287 y / -12286 facing): 0 turn left, 1 turn right, 2 forward, 3 back | boot.c:8920 |
| `pick_clk` | — | other | — | sound_falcon.c:50 |
| `picker_button_track` | — | other | Track a press on a picker command button until the mouse releases — the same on/off-drag behaviour as menu_button_track, returning 1 on release over the button | boot.c:19495 |
| `picker_cmd_button` | — | other | A dialog command button (Select / Cancel): a menu_button_bevel with a CENTRED label — the same chrome as the menu buttons | boot.c:19431 |
| `plat_input_init` | — | shim | — | input.c:151 |
| `plat_input_shutdown` | — | shim | — | input.c:163 |
| `plat_kb_poll` | — | shim | — | input.c:50 |
| `plat_kb_shift` | — | shim | — | input.c:71 |
| `plat_mouse_btn` | — | shim | — | input.c:175 |
| `plat_mouse_pos` | — | shim | — | input.c:169 |
| `plat_sound_init` | — | shim | — | sound_falcon.c:71 |
| `plat_sound_play_mono8` | — | shim | — | sound_falcon.c:95 |
| `plat_sound_playing` | — | shim | — | sound_falcon.c:130 |
| `plat_sound_shutdown` | — | shim | — | sound_falcon.c:81 |
| `plat_sound_stop` | — | shim | — | sound_falcon.c:124 |
| `plat_ticks` | — | shim | — | input.c:40 |
| `port_always_load` | — | port | ROUTED (RM #127, 2026-06-14): load ALWAYS.CTL through the faithful FC pool group 0 (jt997 -> jt1014 -> jt464 register + jt987 read-into-pool) instead of the old resident static-buffer + l37aa shortcut | boot.c:11329 |
| `port_begin_adventure` | — | port | port_begin_adventure — the real "Begin Adventuring" entry (Training Hall case 9 / l1142) | boot.c:47773 |
| `port_blit_demo` | — | port | port_blit_demo — exercise the bit-packed blit foundation: load a real 32x32 1bpp DUNGCOM tile, OR-blit it into the page at a run of sub-word x offsets (shift 0..15) via bp_blit_or, then convert the pa | boot.c:13752 |
| `port_draw_play_frame` | — | port | static void jt1193(void); clip-to-full-screen, defined below | boot.c:11449 |
| `port_frame_load` | — | port | ROUTED (RM #127, 2026-06-14): load FRAME.CTL through the faithful FC pool group 1 (jt997 -> jt1014 -> jt464 + jt987) instead of the resident fbuf[] shortcut | boot.c:11306 |
| `port_hud_text_clut` | — | port | port_hud_text_clut — re-install the UI text colours into the live (dungeon) clut so jt94/jt1089 HUD text (fgColor = colour & 0x0f) is readable | boot.c:18881 |
| `port_l6234_verify` | L6234 | port | port_l6234_verify — geometry-verification harness for the faithful first-person render (jt221 -> L6234) | boot.c:13483 |
| `port_load_savgame` | — | port | port_load_savgame — load a real BasiliskII-saved game (SAVE/SavGam<X>.csv, 10284 bytes) into the port party | boot.c:15242 |
| `port_menu_bar` | — | port | #define MENU_BAR_ROWS 14 draw 14 of the 16 rows -> exact 14px tiling | boot.c:11380 |
| `port_menu_load` | — | port | MENU.CTL = jt468 group 24 — the menu chrome GLIB | boot.c:11343 |
| `port_play_demo` | L0bbc | port | port_play_demo — the play loop core as an interactive dungeon walk on the automap | boot.c:14552 |
| `port_play_message` | — | port | port_play_message — a two-line notice during play (black screen + the hud_text glyph blitter) | boot.c:14080 |
| `port_render_geo_contact` | — | port | — | boot.c:14998 |
| `port_render_geo_map` | — | port | — | boot.c:8632 |
| `port_render_geo_tiles` | — | port | port_render_geo_tiles — draw the loaded GEO map with REAL tiles | boot.c:8801 |
| `port_render_topview` | L37aa | port | port_render_topview — load the real TOPVIEW.TLB tile library (Disk1, a GLIB of 16x16 1bpp top-down map tiles) and rasterize every glyph into a grid via blit_glyph_1bpp + the validated L37aa/L2856 extr | boot.c:8744 |
| `port_rest` | — | port | port_rest — camp to recover | boot.c:14293 |
| `port_run_encounter` | — | port | port_run_encounter — announce, then Fight (auto-resolve) or Run. | boot.c:14099 |
| `port_show_intro` | — | port | — | boot.c:19125 |
| `port_sprite_demo` | — | port | port_sprite_demo — render FRUA's COLOUR art (mode-1 4bpp/8bpp tiles from the "C" files) through clut 129: rows of character paperdolls (CBODY), combat sprites (COMSPR), and creature pictures (CPIC) | boot.c:14938 |
| `port_test_seed_design` | — | port | static short jt573(short doSave); CODE 17 review screen (below) | boot.c:15414 |
| `port_ui_group_base` | — | port | jt468 groups 0/1/24 -> the port's resident UI GLIBs (see [[glib-resource-groups]]). | boot.c:11430 |
| `port_view_demo` | — | port | port_view_demo — drive jt199, the first-person frustum walker, over the real loaded design's GEO map | boot.c:13405 |
| `port_wall_demo` | JT[200] | port | port_wall_demo — drive jt200 (JT[200]), the per-slot wall-tile selector, against the real DUNGCOM wall set | boot.c:13831 |
| `pstr_copy` | — | other | Copy a Pascal string into dst, clamping to 255 data bytes. | menus.c:83 |
| `pstr_len` | — | other | Pascal-string length (the first byte). | menus.c:77 |
| `pulldown_hit` | — | other | Identify which item (1-based) of menu `i`'s pull-down `pt` falls over, or 0 if outside. | menus.c:356 |
| `pulldown_rect` | — | other | Compute the pull-down rect for the menu at bar slot `i`. | menus.c:259 |
| `qd_attach_screen` | — | shim | — | quickdraw.c:306 |
| `qd_cursor_refresh` | — | shim | qd_cursor_refresh — called from the event pump on every idle spin | quickdraw.c:1322 |
| `qd_dump_palette` | — | shim | Debug accessor: copy the live shim CLUT out as 256 packed RGB triples | quickdraw.c:1050 |
| `qd_effective_clip` | — | shim | The port's effective clip — portRect intersected with the visRgn and clipRgn bounding boxes (both rectangular in the shim) | quickdraw.c:366 |
| `qd_fill_rect` | — | shim | Shared body of EraseRect / PaintRect — fill `r` (in the current port's local coordinates) with `color`, clipped to the port's effective clip | quickdraw.c:488 |
| `qd_hline` | — | shim | Horizontal line helper — used by PaintOval's per-row fill. | quickdraw.c:547 |
| `qd_init_port_defaults` | — | shim | Initialise the per-port drawing defaults that the Mac sets in OpenPort (the shim has no OpenPort; qd_attach_screen and win_new both call this) | quickdraw.c:233 |
| `qd_install_color_pointer` | — | shim | — | quickdraw.c:1218 |
| `qd_nearest_color` | — | shim | Find the cached-CLUT index closest to `color` by sum-of-squared differences in 8-bit-per-channel space | quickdraw.c:1066 |
| `qd_oval_axes` | — | shim | Oval primitives — the oval is inscribed in `r`, touching the four edges | quickdraw.c:718 |
| `qd_pen_hline` | — | shim | — | quickdraw.c:577 |
| `qd_pen_plot` | — | shim | Pen-aware primitives — sweep the pnSize.h x pnSize.v pen along the shape's path, so a 3x3 pen draws a 3-pixel-thick line / outline | quickdraw.c:563 |
| `qd_pen_vline` | — | shim | — | quickdraw.c:592 |
| `qd_pixmap_fill` | — | shim | Fill a rectangle of the current port's pixmap, applying `mode` between `fg` and the destination pixel, optionally gated by a one-bit pattern | quickdraw.c:406 |
| `qd_plot` | — | shim | Plot one pixel at (x, y) into the current port's pixmap, silently discarding writes that fall outside the precomputed clip rect | quickdraw.c:526 |
| `qd_present` | — | shim | static void qd_rebake_color_pointer(void); re-resolve cursor to live CLUT | quickdraw.c:282 |
| `qd_present_rect` | — | shim | — | quickdraw.c:298 |
| `qd_rebake_color_pointer` | — | shim | (Re)resolve the cursor's RGB palette to the LIVE CLUT and translate the raw source indices into screen indices | quickdraw.c:1196 |
| `qd_screen_pixels` | — | shim | Direct access to the attached screen back buffer, for engine code that paints raw 8-bit cells (e.g | quickdraw.c:342 |
| `qd_screen_port` | — | shim | Has qd_attach_screen been called yet? Until it has, g_screen_port is zeroed and not usable as a port; the Window Manager checks before targeting it for frame drawing. | quickdraw.c:262 |
| `qd_set_palette` | — | shim | The shim's cached CLUT, in 8-bit-per-channel form (one slot for each of the 256 indices) | quickdraw.c:1026 |
| `qd_set_present` | — | shim | Present hook — see quickdraw.h. | quickdraw.c:270 |
| `qd_set_present_rect` | — | shim | Dirty-rect present hook — see quickdraw.h. | quickdraw.c:293 |
| `qd_sword_cursor` | — | shim | — | quickdraw.c:1167 |
| `queue_peek` | — | other | — | events.c:106 |
| `queue_pop` | — | other | — | events.c:122 |
| `queue_push` | — | other | the posted-event queue | events.c:65 |
| `queue_remove` | — | other | Remove the i-th queue entry (0 = oldest) and compact the slots after it | events.c:88 |
| `rd_be16` | — | other | Big-endian readers for the `snd ` resource. | sound.c:31 |
| `rd_be32` | — | other | — | sound.c:36 |
| `read_hz200` | — | other | — | input.c:35 |
| `rect_to_global` | — | other | Map control-rect (window-local) to the screen-port global rect for painting | controls.c:320 |
| `render_3d_faithful` | — | other | — | boot.c:10911 |
| `render_3d_raycast` | — | other | render_3d_raycast — the faithful-frustum view: the wider 3-cell-wide field jt199 walks (left/center/right columns), versus render_3d_view's single corridor | boot.c:9717 |
| `render_3d_view` | — | other | — | boot.c:9581 |
| `res_find` | — | other | Binary-search the entry table for (type, id); the table is sorted. | resources.c:75 |
| `res_load` | — | other | Load entry `idx` into a Handle, caching it; returns the Handle or NULL. | resources.c:96 |
| `resource_open` | — | other | — | resources.c:44 |
| `restore_under` | — | other | Blit a previously saved rectangle back to the screen port. | menus.c:416 |
| `rm_audit` | — | other | rm_audit — read-only dump of the FC group-pool state, to gauge whether the faithful resource path can carry the wall load before routing l6eea onto it | boot.c:10866 |
| `save_roster` | — | other | Write each pool character to its own CHARnnnn.CHR file; delete the files for the now-unused higher slots so a removed character doesn't linger. | boot.c:15172 |
| `save_under` | — | other | Snapshot the rectangle from the screen port into `out`. | menus.c:382 |
| `scroll_is_vertical` | — | other | A vertical scroll bar is taller than it is wide. | controls.c:344 |
| `scroll_rects` | — | other | Arrow / page-zone / thumb rects in global coords | controls.c:355 |
| `scroll_value_from_pt` | — | other | Map a current mouse position (in global coords) to a scroll-bar value, based on the thumb's position inside the track. | controls.c:791 |
| `show_controls_demo` | — | other | Stand-alone Mac Control Manager demo — opens a fresh window, drops a push button + a checkbox + two radio buttons + an OK button into it via NewControl, then runs a small event loop that routes mouseD | main.c:62 |
| `show_dialog` | — | other | Pop up DLOG `id`, run the modal loop, return the dismissing item (1-based) or 0 if the dialog couldn't be loaded | main.c:42 |
| `show_name_prompt` | — | other | Synthetic "enter name" modal — proves the Dialog Manager's editText path end to end without depending on a FRUA-shipped DITL | main.c:157 |
| `snd_parse_and_play` | — | other | Parse a Mac `snd ` format-1 resource and play its sampled-sound header through the platform backend | sound.c:69 |
| `sprite_row` | — | other | sprite_row — load a colour "C"-file tile library and blit a run of its tiles across one screen row, using the file's OWN sub-palette (item 1, four RGB triples) matched to clut 129 | boot.c:14887 |
| `str_c2p` | — | other | C-string -> Pascal, for the File Manager Create/FSOpen save-slot calls. | boot.c:27715 |
| `submenu_decorate` | — | other | the menu-stack pattern: a sub-menu is a C function that runs its own menu_run loop and returns to the caller's dispatch | boot.c:19767 |
| `te_ensure_text` | — | other | — | textedit.c:43 |
| `te_text` | — | other | #define TE_CARET_BLINK 30 ticks between caret state flips | textedit.c:35 |
| `title_width` | — | other | Approximate pixel width of the 8x8 fallback string. | menus.c:102 |
| `to_upper` | — | other | Uppercase an ASCII letter; pass other bytes through. | menus.c:59 |
| `toolbox_init` | JT[1144] | other | toolbox_init — the standard Mac Toolbox startup sequence | toolbox.c:70 |
| `track_scrollbar` | — | other | — | controls.c:827 |
| `ua_arena_alloc` | L531e | other | ua_arena_alloc — L531e | core.c:83 |
| `ua_backdrop_to_back` | — | other | ua_backdrop_to_back — map a FRUA level-header backdrop id (the values stored in ds[8..11]) to a BACK.CTL backdrop index (1-based) | boot.c:9493 |
| `ua_main` | — | other | static void port_show_intro(void); title / credits sequence | boot.c:1922 |
| `ui_glib_blit` | — | other | — | boot.c:18919 |
| `uninstall_supervisor` | — | other | — | input.c:140 |
| `unlink_control` | — | other | — | controls.c:64 |
| `unpackbits` | — | other | Decode a PackBits-RLE run into dst (cap bytes) | boot.c:18721 |
| `update_to_event` | — | other | — | events.c:189 |
| `vbl_flip_handler` | — | other | Called from vbl_trampoline at every vertical blank (supervisor) | display_videl.c:77 |
| `vbl_install_super` | — | other | Supexec'd: add / remove the trampoline in the OS VBL queue (protected low memory: _vblqueue @ 0x456, _nvbls @ 0x454). | display_videl.c:92 |
| `vbl_remove_super` | — | other | — | display_videl.c:106 |
| `videl_flip` | — | other | Flip: point the VIDEL base at the just-blitted back buffer | display_videl.c:300 |
| `videl_init` | — | other | — | display_videl.c:116 |
| `videl_lut_blit` | — | other | 8->16 LUT blit: chunky rows [y0,y1), columns [x0,x1) -> the 16bpp buffer `dst` | display_videl.c:275 |
| `videl_present` | — | other | — | display_videl.c:305 |
| `videl_present_rect` | — | other | The LUT blit is fast, so a partial present buys little and is wrong under triple-buffering (the three pages diverge); just do a full present. | display_videl.c:330 |
| `videl_set_palette` | — | other | Rebuild the 8->RGB565 LUT from the engine's palette | display_videl.c:339 |
| `videl_shutdown` | — | other | — | display_videl.c:247 |
| `videl_surface` | — | other | — | display_videl.c:266 |
| `void` | — | other | — | boot.c:19629 |
| `walk_ditl` | — | other | — | dialogs.c:120 |
| `wall_slot_for_edge` | — | other | wall_slot_for_edge — given a map edge byte, return the wall-set slot to draw that face with, or -1 for no wall | boot.c:9549 |
| `wallset_for_id` | L6eea | other | wallset_for_id — map a FRUA wall-set id (level header Wall1-3, ds[4..6]) to a wall library + set | boot.c:9535 |
| `win_close_box` | — | other | Close-box rect, in screen coordinates — shared by win_draw_frame and the hit-testing entries (FindWindow / TrackGoAway) so the box never drifts between the painted pixels and the hit area. | windows.c:87 |
| `win_drag_outline` | — | other | Tracking helper for DragWindow: paint or erase the drag outline on the screen port | windows.c:736 |
| `win_draw_frame` | — | other | Paint the window's frame, title bar, title text, and (optional) close box into the screen port | windows.c:160 |
| `win_focus_changed` | — | other | Post activate / deactivate events when the visible front transitions from `old_front` to `new_front` | windows.c:485 |
| `win_link` | — | other | Insert `w` into the window list relative to `behind` (see NewWindow). | windows.c:45 |
| `win_new` | — | other | Shared body of NewWindow / NewCWindow: allocate or adopt the storage, set the window up, and link it into the list | windows.c:306 |
| `win_reset_visrgn` | — | other | Set the window's visRgn to its full content — the rectangular-region stand-in for "the window is entirely visible". | windows.c:67 |
| `win_set_regions` | — | other | Set strucRgn (the whole window including frame and title bar) and contRgn (the content area only) in global screen coordinates from the content bounds `bounds` the caller passed to NewWindow. | windows.c:105 |
| `win_set_title` | — | other | Copy the title's Pascal-string bytes into a relocatable block on the heap and stash it in w->titleHandle, replacing any previous title | windows.c:127 |
| `win_unlink` | — | other | Detach `w` from the window list if it is currently in it. | windows.c:34 |
| `wind_be16` | — | other | Big-endian 16-/32-bit fields of a resource body. | windows.c:380 |
| `wind_be32` | — | other | — | windows.c:385 |
| `xlate_modifiers` | — | other | — | events.c:33 |
