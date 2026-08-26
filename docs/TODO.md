# TODO

Working notes on what's next. Ratified architecture decisions live in
`docs/decisions.md`; this file is the rolling task list.

## Status snapshot (2026-08-03)

> ⚠️ **The 2026-06-06 snapshot that used to sit here was EIGHT WEEKS STALE and
> contradicted reality** — it described the play-entry chain as "in progress"
> and combat as port-local scaffolding, both long since landed. It was removed
> rather than patched. Everything below the next heading is older working notes:
> treat it as history, verify before acting on it.

**Do not maintain a status snapshot in this file.** Three live sources already
carry it and are kept current:

- `docs/milestone.md` — the high-level burn-down. START HERE.
- `docs/subsystem-status.md` — per-subsystem register + the `*-wall.md` index.
- `python3 tools/stub_audit.py --stubs` / `--arms`, `tools/jt_progress.py` —
  the authoritative numbers. As of 2026-07-30: 1201 done / 1 stub / 4 missing
  of 1206 JT entries; **0 live gaps, 0 deferred arms**. A zero means no
  reachable PROBE stub body, NOT feature-complete.

The game **plays on real hardware** (Falcon 030@50MHz, VGA) and ships engine +
data media for Falcon/TT, Mega ST (plus a Gotek image that needs no unzip),
Amiga AGA and Amiga ECS. Five hardware reports have been closed; the running
list of what the real machine found — and what it is still owed — is
**`docs/milestone.md` §2a and §3.C**.

### Genuinely open

00. ~~**Does the port exit cleanly to TOS?**~~ **CLOSED 2026-08-04** — reported
   fixed on the reporting Falcon. It took three rounds and the useful part is
   what each one established:
   - the teardown DOES run. QUIT goes `jt415` -> `ExitToShell`, so `ua_main`
     never returns and none of `main()`'s markers are written; a `DBG.LOG` that
     stops at `menu: modal up` after a clean quit is NORMAL. `ExitToShell` now
     logs both sides.
   - `videl_init: old mode = NNN` was logged BEFORE `dbg_log_screen_owned()`,
     i.e. to the console the screen takeover then paints over — so the one
     number the diagnosis needed could never have been in the file. It is now.
   - an emulator could not answer it: Hatari never draws a desktop before an
     auto-started program, so the desktop it shows afterwards is not evidence
     (already paid for once — see `falcon-vga-hardware`).
   The restore goes through `VsetScreen(SCR_MODECODE)` (`VsetMode` does not
   reinitialise the VDI — Compendium p.289/290) plus the ST-compat palette, and
   `video.cfg`'s `exit=full|vdi|palfirst|mode` remains as an escape hatch for a
   machine the default does not suit.

0a. **Save-file parity with DOS: three deltas left.** The path and spelling now
   match (`<design>.DSN\SAVE\SAVGAM<c>.CSV`, 2026-08-03), verified against a
   headless DOS run. Not copied, in rising order of effort:
   - ~~`VAULT<c>.DAT`~~ **NOT A GAP — it never was.** The format is fully
     lifted (`jt74` read / `jt75` write) and the port has been writing the file
     all along, just into the flat folder; it now goes to
     `<design>.DSN\SAVE\` with the slot. Spec in `docs/vault-format.md`.
     Mac pads to 200 item records (3616 B) and DOS to 50 (916 B), but the
     reader is count-driven so the pad is never read and **DOS vaults load
     unchanged**.
   - DOS writes **10 285** bytes to our 10 284. One byte, never chased — and
     it does not obstruct anything: a DOS slot file loads (verified
     2026-08-03, all six characters back with AC/HP).
   - ~~saved CHARACTERS~~ **DONE 2026-08-03.** They live at
     `<design>.DSN\SAVE\<NAME>.CCH` now, named through the faithful `jt130`
     8.3 rule. Verified both ways: a DOS-authored character appears in Add A
     Character with the right AC/HP, and DOS still reads one our engine
     rewrote. Existing installs migrate off `CHAR*.CHR` on first boot.
     Follow-up worth knowing about: `jt584` (the Mac's own per-character save,
     with its "Update %s?" collision prompt) is lifted but not wired to the
     Hall — the port's `save_roster` writes the whole pool instead, and
     resolves a name collision silently by slot rather than by asking.

0. ~~**`geo.py`'s STRG encoder is not byte-faithful to SSI**~~ **CLOSED
   2026-08-05.** 635 of 635 real SSI-authored areas now re-encode byte-for-byte.
   Three things this entry got wrong are worth keeping, because each one aimed
   the work at the wrong place:
   - **"Every 6-bit character code comes out exactly ONE LESS than SSI's" was
     FALSE.** Re-packing all 1 270 used strings in HEIRS reproduces SSI's bytes
     exactly — the alphabet and the 4-codes-per-3-bytes packing were always
     right. The body bytes were never the problem; the LENGTH INDEX was. Read
     the actual diff before naming a cause.
   - **"A fidelity gap, not a functional break" was FALSE.** The third header
     word is the string pool's USED-REGION LENGTH: `l4e8a` (add) opens its gap
     with `jt406(data+slot+size, data+slot, hdr[2] - slot)` and `l501e`
     (delete) closes it with `hdr[2] - (off+size)`. Writing 0 there makes both
     move lengths NEGATIVE, so adding or deleting a string in the in-game
     editor on a module we generated would move the wrong span over the packed
     data. Every module `mk_*_design.py` ever wrote had 0 in that field.
   - **The proposed oracle — "re-encode every SSI area and require byte
     equality" — is UNSATISFIABLE by any pure encoder.** SSI's allocation is
     editing history, not a function of the string: GEO005 slot 4 and slot 67
     are both 38 characters and carry 29 and 30 bytes, in the same file. The
     body tail is likewise deleted-string residue (1 740 nonzero bytes in
     GEO001). Fidelity comes from PRESERVING provenance — `strg_read()` records
     each slot's bytes and the tail, `strg_write()` reuses them for anything it
     did not change — so editing one string perturbs that string and the
     offsets after it, not all 400 slots.
   The other real fix: SSI packs each string PLUS a NUL terminator, which the
   engine's own allocator confirms (`len = jt423(text) + 1`,
   `size = (len*3+3)/4`). Six tests over real areas plus two data-free ones;
   all five mutations caught, and a regenerated KOBOLD.DSN was booted in Hatari
   to confirm the engine still reads what we write.



1. ~~Mono ST/STE (BWMODE) is BROKEN at runtime.~~ **NOT A CODE REGRESSION —
   RESOLVED 2026-08-02.** It boots to the menu in ~5 s at `5297636a` once the
   data tree has art it can use. In mono the engine selects the **`.tlb`** art
   set, and a Mac B&W `.TLB` is a DIFFERENT FORMAT from a DOS HLIB `.TLB`
   despite the shared extension; `jt398` rejects the HLIB one by design and
   `jt987` turns that into an infinite disk-swap retry. `data/work/gamedata` is
   DOS-derived, so mono had nothing to load. Recipe for a mono data tree is in
   the run-falcon-port skill.
   **Both follow-ups are now DONE** (2026-08-02): `tests/test_mono_boot.py`
   builds and boots mono and asserts the menu marker (slow-marked, skips when
   the Mac release is not unpacked; mutation-checked), and `jt987`'s retry is
   bounded (3 rounds, 15 s each) and names the missing resource on the first
   miss — the DOS-only tree now stops with `LBLoad: Bad Lib: 'always.TLB'`
   instead of hanging.

   **Remaining, tracked:** mono requires the MAC release. `MONO_FAMILIES` in
   `tools/art_convert.py` synthesises 17 of 23 libraries from colour art but
   not the six chrome ones — **ALWAYS, FRAME, GEN, MENU, TITLE, TOPVIEW** — and
   ALWAYS is the first the boot wants. Adding them is tractable, not
   speculative: the Mac base game ships BOTH halves of every pair, so the
   per-family scale and mode are measurable exactly as the other ten were
   (ALWAYS 5368/1816, FRAME 34320/23672, GEN 26818/7748, MENU 11064/1458,
   TITLE 171744/51736, TOPVIEW 1552/1392). Until then the caveat is documented
   in `HARDWARE.md`.

2. ~~The compass after an AREA toggle is fixed-by-observation only.~~
   **CLOSED 2026-08-01 (`84511949`)** — root-caused: `l67ca` read the 8-entry
   direction table with an unmasked facing, and `l1908` normalises facing to
   1..8, so NORTH (8) indexed past the letters and drew no face. Not an
   AREA-map bug; the map only forced the redraw that exposed it.
3. ~~TT draw-time planar writer (ADR-0016) deliberately not done.~~
   **DONE 2026-08-01 (`0227b5aa`)** — the TT registers a draw-time target and
   87% of the rows the present still handled need no conversion. `FRUA_PLANAR`
   is now the default on every target. The "~6% left to win" was a share of the
   original figure, not of the work the present actually still did. See
   "#160 THE TT WRITER HALF" in `docs/planar-plan.md`.
4. ~~**The play loop is still unmeasured** for planar conversion cost~~
   **MEASURED 2026-08-06.** A walk action costs ~1.17 s on an 8 MHz STE and
   ~1.13 s on a 7 MHz Amiga ECS — the same, which contradicts the expectation
   that ECS would be clearly worse. Present is 51% / 65% of a redraw, so the
   present is the lever, not the renderer; two redraws are issued per action on
   both. The step profiler had to be fixed first (it was timing its own
   logger). Full table + traps in the #90 section of `docs/planar-plan.md`.
5. **1.0.0 is RESERVED for real hardware** and deliberately not cut yet; the
   user has asked to stay on 0.9.x while game-breaking bugs remain.
6. ~~Amiga input does not commit — the save/load round-trip is stuck on
   Falcon/TT/ST.~~ **CLOSED 2026-08-02 — IT WAS THE HARNESS, NOT THE PORT.**
   `xdotool windowactivate` drives EWMH `_NET_ACTIVE_WINDOW`, which a bare
   Xvfb (no window manager) does not support; the amiga driver chained it into
   ONE invocation as `windowactivate --sync $w key p`, so the failure aborted
   the chain and **the keystroke was never sent** — silently, from the caller's
   side. It also killed `start`'s mouse-capture click, which is why the
   emulated pointer looked positioned (driver-side bookkeeping) but never
   clicked. `windowfocus` needs no WM and fixes both. A second harness bug sat
   behind it: flatpak's x11 socket sharing **rewrites `DISPLAY` inside the
   sandbox**, so `--env=DISPLAY=:99` was ignored and the window kept opening on
   the user's desktop; only the launching shell's `DISPLAY` counts.
   With those fixed, **AGA and ECS both complete the round-trip** and the
   save/load matrix is 5 of 5 (see `docs/save-load-wall.md`). Beware the
   latency trap this hid behind: on the 7 MHz ECS build a committed click can
   take ~30 s to paint, which reads exactly like a dropped click and invites
   crediting whatever you tried second.

7. ~~**Sticky-square text prints BEFORE the step's view redraw**~~ **FIXED
   (10bad796)** — #90's deferred render skipped l1908's Mac-order jt312 and
   the replacement re-render ran only after the dispatch, but GAP-1 fires
   the square's event INSIDE the dispatch: text drew before any view
   render. Fix: flush the deferred render (jt312 presents) right before an
   event dispatch or sticky-box clear; ordinary steps keep the single
   render. Verified with an authored STICKY.DSN and on HEIRS's real Weary
   Wanderer square (text prints with the view already moved). Await the
   user's hardware confirm on the ATW. (original report: Mega STe
   ATW800/2 field report, 2026-08-25, 0.9.17-diag2). Old behaviour: step in,
   text starts printing, stalls 1-2 s mid-print, finishes (that stall is
   likely gone with the jt1066 cycle-range fix — the wipe/re-install churn is
   dead). New behaviour: the text lands at the bottom of the screen before
   the 3D view draws the move ('The Weary Wanderer.' after the caravan
   event). Present-granularity ordering on a slow-present card: the text
   rows likely go out via a small early present (the jt1134 idle concession?)
   while the view redraw arrives in the big frame commit. Check what order
   the Mac/DOS shows (DOS oracle, fine sampling), then either hold presents
   across the step+text compose or reorder.

8. **Big-pic teardown is visible on slow presents** — closing a picture
   event shows the pic "unloading" (draining out of the view square) before
   the view redraws (same field report). Same disease the titles had: a
   multi-step clear+recompose presented piecemeal over ~0.5 s VME writes.
   Candidate fix: qd_present_hold around the event-close teardown+restore so
   it lands as ONE present (blackout is wrong here — it is a content change,
   not a palette one).

9. ~~**Movement-arrow cursor is not confined to the 3D view**~~ **CORE
   FIXED (f8a8e308)** — the pad was the faithful Mac 136x160 screen-origin
   rects; now walk_pad_regions_install() retiles the four regions to the
   88x88 viewport hole (24,24)-(112,112); re-sweep lands every boundary
   exactly, click-to-move regression-checked. REMAINING (cosmetic): DOS
   shows the SWORD over the roster where we always show the shield, and
   the cursor ART size vs the Mac is unchecked.
   (history) **DOS ORACLE VERIFIED 2026-08-25 (screenshots in the session log):** in
   DOSBox the arrows exist STRICTLY inside the 3D view — one pixel out and
   the cursor is the SWORD over the roster, the SHIELD over the text pane.
   **Port measured the same day (Falcon/Hatari, xdotool mousemove probes):**
   arrows are right IN the view (forward arrow matches), but the zones
   overhang the frame — turn-right shows over the roster border, the
   U-turn below the frame — before eventually flipping to shield. Second
   delta: our out-of-view cursor is always the shield; DOS uses the sword
   over the roster.
   Code map for the fix: the four walk regions are jt164's JT[452] install
   (boot.c ~69073, tags 22/11/21/23, faithful Mac rects in 8000-space);
   hover shape comes from l2d3e's Phase-3 live hit-test (~28557) running
   jt378 cmd 2 -> jt1139 (8000-space origin via jt1135, scale 2, grid
   bounds rec[22]/rec[24]). NOTE: a first-pass decode of those rects
   PREDICTS the wrong flip points (the text-pane probe should have been
   inside the bottom band but showed shield) — do an EMPIRICAL sweep first
   (probe grid + log the hovered tag) before trusting any arithmetic; then
   either fix the transform or clamp the region hit-test to the live
   viewport rect. Cursor art size vs Mac still unchecked (we ship the DOS
   colourful set; Mac is 16x16 B&W).

10. **Title song vs credits timing** (same report, LOW): the tune ends right
   before the credits screen appears. Probably FAITHFUL: the Mac song is
   ~57 s and five screens x 1050-tick holds is ~90 s, so the music runs out
   around screen 4 on the original too. Verify against DOS/Mini vMac before
   touching anything; the user explicitly ranks this minor.

11. **Menu accelerators beep on the Mega STe** (field report 2026-08-26):
   [P]lay / [L]oad / [B]egin each WORK but beep every time — "don't recall
   it doing that before". Check the DOS oracle (drive p/l/<slot>/b in
   DOSBox with a pulse audio capture: does DOS beep on accelerators?),
   then find which path beeps — jt1080 fired by an unclaimed-key fall-
   through AFTER the accelerator was consumed is the likely shape (l2d3e
   Phase-5 "ph5 unclaimed -> jt1080"). NB the beeping heard during the
   2026-08-26 session was ALSO partly my own headless Hatari leaking SDL
   audio to the desktop — mute with SDL_AUDIODRIVER=dummy; that does NOT
   explain the on-hardware report.

## Play-screen HUD polish (future work)

Observed in the faithful play-screen render (jt948 → jt953); cosmetic, tackle
after the play-loop logic (#100) lands. Full notes in the
`play-screen-hud-polish` memory. Native 320x200 vs Mac 640x400 is the
recurring suspect.

1. **3D view reads as a placeholder, not "live."** Likely the test pipeline —
   jt948's `l0bbc` loads a different level/position than the verify's seeded
   level 5. Verify it tracks the live party cell + real walls before treating
   it as a render bug.
2. **Stray frame dividers mid-screen (scale bug).** A small divider under the
   compass (offset right + down) and a vertical line through the compass —
   `port_draw_play_frame` → `ui_glib_blit` places FRAME.CTL pieces by their
   metric bearings; if those are 640x400 units they land at ~2x on native.
   Fix: halve the piece bearings/placement for native (ui_glib_blit is
   absolute + bearings, no jt1135 scaling — the bearings are the lever).
3. **3D-view frame gaps are GRAY, the Mac HUD is BLACK.**
   `port_draw_play_frame` fills clut 21 (stone) then blits pieces; the gaps
   show gray. Check the Mac: black (clut 0) underlay, or seamless tiling.
4. **Command-bar text is BLACK + not on buttons.** Should match the menu:
   accelerator letter white (clut 15), rest light grey (clut 7), on raised
   bevel plates. Part of finishing jt164/l23b4 (#100-B).

## Boot UI / menus

- DONE: main menu renders (`jt315`, CODE 22 + 0x4d8a). Builds the ten
  DLItem buttons (jt447/jt452), paints (l2c60 -> jt382 DrawString), draws
  the "Current Game Design:" banner (jt94), runs the dialog event loop
  (jt453). Full screen of UI matching the Mac; Quit-confirm dialog works.
- DONE: per-selection dispatch. jt315 loops + dispatches on the selected
  DLItem index (g_mainmenu order). "Play the Game" (0) -> port_play_demo
  (load level, place party, render jt312, walk WASD, back on 'q'); "Quit
  From Game" (9) -> return 0. Verified in Hatari (-DDEMO_LEVEL=2): boot ->
  menu -> 'P' -> the 3D dungeon corridor. Selection works via the item
  hotkey (jt382 hit-test + l1676 commit -> l2d3e returns the index).
- DONE: faithful Play path to the Training Hall. jt315 Play -> return 1 ->
  ua_main l07dc -> jt918 -> l0aae renders the party-management menu (Add/
  Remove/Modify/Train/View Character, Human Change Class, Create/Delete,
  Load/Save, Begin Adventuring, Exit From Play). l0aae was lifted earlier
  but painted via the jt449 stub + never presented; fixed (clear + l2c60 +
  qd_present, like jt315). "Begin Adventuring" (case 9 / l1142) bridges to
  port_play_demo -> the 3D dungeon (faithful jt585/CODE15-19 chain is still
  stubbed). Full chain verified: menu -> P -> Training Hall -> B -> dungeon.
- PARTIAL: the roster/Adventure menu renders. "Add Character" -> jt904 ->
  jt182 (Add/Modify/Delete roster popup) now displays on a clean backdrop
  (jt155 value list + l206e/l23b4/l25b6 over the l2d3e pump were lifted;
  added the clear+present prime). CHARACTER CREATION + A POPULATED PARTY are
  a LARGE subsystem, NOT done:
  * Create/Add character -> CODE 17 char-gen (~10k lines of asm, barely
    lifted): JT[557] create, JT[574] train, JT[556], JT[560], etc. — all
    PROBE stubs. This is the multi-session piece.
  * Party data g_a5_-5806 is NULL until a real roster block exists; the
    character record format (base+76 party slots, base+198.. roster flags,
    base[94]/[147]/[382] fields read by jt904) needs RE to seed a test party.
  * DONE (i): seeded a test party. Decoded the roster data model from l02dc
    — linked list off g_a5_-27928 (next@+0), name@+96, HP@+385, AC@+395.
    port_test_seed_design seeds 3 characters (Bramble/Korin Vale/Sable);
    lifted jt25 (row name paint) so all entries show; moved the backdrop
    clear to jt918's loop top so the menu no longer wipes the roster.
    Training Hall now lists the party. REMAINING POLISH: lift jt32/jt34
    (AC/HP number paint, still stubs) and fix the "Name"/"AC HP" header
    column positions (jt94 col mapping). The party is a static stand-in —
    replace with real created/loaded characters once CODE 17 lands.
  * STARTED (ii): CODE 17 char-gen. Lifted L35f8 (the PICK RACE/ALIGNMENT/
    GENDER/CLASS headers via jt1089) + L3666 (char-gen screen init skeleton:
    dims, draw headers, seed wizard step g_a5_-7018) + jt574 (case-0 entry
    shows the screen). The big remainder: the ability-score roll (L34f0 over
    the race/class tables at g_a5_-30450) + the jt568 per-step pick state
    machine (race/class/gender/alignment selection + the created record).
    BLOCKED on visual confirm by the present issue below.

## Main-menu chrome — WRONG ASSET; needs a methodical rework

Feedback (correct): the current menu backdrop uses GEN.CTL, a high-contrast
marble image — the WRONG asset (same over-application pattern as DUNGCOM).
The real FRUA menu uses dedicated UI assets. Confirmed by dumping them:
- MENU.CTL = 3-item GLIB. item 0 is the FULL 256-colour UI PALETTE (EGA-style:
  14 = gold, 15 = white, 16..31 = grey stone). item 1/2 are 320x16 raw-8bpp
  tiles (flags 0xc0). So the menu's palette + backdrop live here, NOT clut 129
  (the game palette, which gives the wrong cyan text).
- FRAME.CTL/TLB = ~29 items: the raised 3D frame/bevel graphics that box each
  menu command (the "bars"). Each item composites a frame border — they are
  ART, not the hand-drawn rectangles I tried.
- TITLE.CTL/TLB = the title-screen art ("UNLIMITED ADVENTURES / VERSION 1.0 /
  APRIL 27, 1993" block) — currently missing entirely.

Pitfalls hit when I tried MENU.CTL quickly (so do it carefully):
- item 1 tiled vertically shows horizontal STRIPES (it has internal line
  structure — likely a separator/edge piece, not a seamless fill; item 2 or a
  solid grey may be the field).
- Installing MENU.CTL's 256-palette turned the menu text MAGENTA — the label
  fgColor index maps to magenta there. The labels must be drawn in white (15)
  / gold (14) explicitly; the UI text colour is its own thing, not inherited.

Plan (methodical, per asset): (1) install MENU.CTL palette + set the menu
text fgColor to white/gold; (2) pick the right MENU bg field (item 2 / solid
grey) so it's calm + uniform; (3) composite FRAME.TLB bevels per command;
(4) draw the TITLE block. Each is a focused step with a Hatari check.
The GEN backdrop (below) is a stopgap and should be replaced by the above.

DONE (commit 7edb43e): steps 1+2. load_menu_ui() installs MENU.CTL item0
(256-entry UI palette) and tiles item1 (320x16 stone course) as the
backdrop. jt315 draws the heading cyan + design/title gold; jt382 forces
button labels white (clut 15). Verified in Hatari — calm grey stone bg,
cyan/gold/white text per the reference. GEN backdrop now unused.
DONE (commit 94b1f9e): the TITLE block. jt315 now draws all five banner
lines the asm does (CODE 22 + 0x506e): "Unlimited Adventures" (row 3, col
11 cyan) + version/build line (row 4, col 7) + "Current Game Design:" +
design name + module title. Sampling the reference proved there is NO
gold — cyan headings (col 11), light-grey values (col 7, 187,187,187) —
so the earlier gold (col 14) change was reverted to the faithful col 7.
DONE (commit 0e99658): bevel boxes — draw_menu_bevels() draws a raised
box (light top+left, dark bottom+right) per command from g_mainmenu via
jt1135, before l2c60. Outline-only (stone shows through); a darker field
fill is an optional refinement.
DONE (commit decd60f): hotkey-letter highlight — jt382 draws the label
body in light grey (clut 7) and the accelerator letter (rec[29]) in white
(clut 15). Matches the P/S/C/D/G/E/A/M/Q highlights in the reference.
DONE (commit fa33ca7): switched the Falcon to 320x200x256 (mode 0x003
RGB / 0x113 VGA) so the menu fills the screen 1:1 with the Mac instead of
the top half of a 320x400 buffer.
DONE (commit 77f7ba4): dark stone surround + raised lighter plates.
fill_stone_dark() darkens the stone tile for the recessed backdrop;
draw_plate() = flat clut-8 fill + bevel; jt315 draws a title plate and
draw_menu_plates() draws one plate per command. Replaced the procedural
outline-boxes that overlaid everything. Matches the reference layout.

FRAME.TLB analysis (for when the faithful art is lifted): it's a flat
30-item GLIB of frame pieces — wide edge strips (top1 480x12, top4
480x24, top6 480x16), tall side strips (top2/3 16x276), 16x17 corner
tiles (top10-15), a 280x264 panel MASK (top5, 1bpp — solid interior +
dithered stone border), and 96x56 tiles (top22-25). Encodings: flags
0x90=1bpp (verified, top5 renders clean), 0x91/0x92 are NOT plain
2/4bpp-chunky (top4 as 4bpp = noise) — needs the real decode. The blit
funnel l309c/jt1001 is a PROBE stub, so faithful FRAME = reverse the
encoding + the Mac's piece-placement. Multi-step; deferred.

DONE (commit f1f4041): switched the menu chrome to the FRAME.CTL color
assets. The .CTL files hold the 8bpp COLOR data (flags 0xc0 raw / 0xc2
PackBits); the .TLB files are the low-bpp (0x90/91/92) masks. So:
- backdrop = FRAME.CTL item 4 (320x16 PackBits dark warm stone tile),
  tiled across the whole screen (shows in the gaps + perimeter);
- UI palette = MENU.CTL item 0 (256 base, cyan/white/grey text) + FRAME.CTL
  item 0 (16-colour warm-grey band) installed over clut 16..31; plate
  face = clut 23 (91,83,79), bevel = clut 16 light / 31 dark;
- bevel state via draw_plate(... recessed): active = raised, title +
  Delete/Unlock + the 2 empty spacer boxes = recessed (g_mainmenu.recessed;
  wire to jt158/rec[28] real enable state when the sub-menus lift).

DONE (commit 236ed7d): font kerning. DrawChar's Mac-FONT path now applies
the per-glyph left-side bearing (mac_font_offset = OW high byte + kernMax)
so glyphs sit correctly within their advance cell. FONT -27001 is fixed
8px advance with positioned glyphs (e.g. 'I' width 3 offset 3); before,
all glyphs left-aligned and spacing looked uneven. This is in the shared
compat text path, so it fixes kerning for EVERY menu/screen in the game.

Main-menu chrome is now a faithful match to data/frua_mac_menu.png
(backdrop, warm palette, raised/recessed plates, kerned text, hotkey
highlights). The frame/plate/text system is reusable for all the game's
other menus, which share this format.

OPTIONAL refinements: (a) faithful FRAME.TLB mask+corner compositing (the
.CTL color tiles cover the look now; the .TLB masks + l309c/jt1001 blit
remain unlifted); (b) disabled-item TEXT dimming — comes with button
wiring.

## Initial-screen texture (GEN backdrop) — stopgap (wrong asset)

The main menu now renders the GEN.CTL marbled-stone backdrop. GEN.CTL = a
2-item GLIB: item 0 = a 16-colour RGB palette band (installed at clut 16),
item 1 = a 320x90 PackBits-RLE image (flags 0xc2; decodes to exactly 28800
bytes) whose pixels are clut indices 16..31 + 0. load_gen_bg() decodes it
once + installs the band each menu redraw (after load_frua_palette, which
would otherwise clobber clut 16..31); jt315 paints it across the top rows.
The earlier "magenta noise" was reading the compressed bytes as raw pixels.
JT[110] (the named loader) is still unlifted — load_gen_bg opens GEN.CTL
directly. (Old notes below kept for the format reference.)

### (historical) initial GEN investigation

The Mac main menu's textured background is drawn by JT[81] (CODE 6 + 0x6a10):
it loads the "gen" tile library and blits backdrop tiles (idx 1,2,3, +4 in
deep mode) at (8000,8000) via JT[1001]/L309c, then disposes the handle.
Two blockers found:
- JT[110] (CODE 6 + 0x33ac), the NAMED-GLIB loader JT[81] uses to load
  "gen", is NOT lifted. (It's reusable — loads any "<name>.ctl/.tlb" — worth
  lifting on its own.)
- GEN.CTL has a NON-STANDARD tile format, unlike the 8X8 wall sets. Outer
  GLIB = 2 entries: item 0 looks like an RGB palette band (flags 0xc8),
  item 1 a colour image (flags 0xc2, ybear -110, metric[6]=0x28). Decoding
  item 1 as width=8*metric[6]=320 x h=90 gives a 28 800-byte body that does
  NOT fit the 26 818-byte file, so that width is wrong; a direct-blit
  attempt produced magenta/colour NOISE (wrong width/stride + the
  transparency key not skipped). GEN's width/stride/transparency need
  proper RE.
A direct load-and-tile attempt (mirroring the wall-set colour path) was
reverted — the menu keeps its flat clut-32 backdrop for now. NEXT: lift
JT[110], decode GEN's real tile format (probably stride=metric[6] not
8*metric[6], with a magenta-key transparency band), and blit per JT[81]'s
coords. (Also gated by the present-buffer issue below in some contexts.)

## Display present / buffer plumbing

RESOLVED (the round-trip black screen). It was NOT a present/buffer bug:
instrumenting videl_present showed it ran correctly (front alternating, the
chunky surface held the gray fill, chunky[0]=8) — but clut[8] had become a
near-black DUNGEON shade. port_play_demo overwrites clut 0..15 (corridor
shading) and switches to deep mode (g_a5_-2347=0, jt1135 scale 3) and never
restores either, so the menu painted with the dungeon palette + deep-scaled
(shifted/clipped) coords. FIX: jt315 and jt918 now restore g_a5_-2347 = 1 +
load_frua_palette() (clut 129, made non-static) on every menu redraw.
Verified: Play -> dungeon -> q -> menu redraws fully (was black).
- RESOLVED: char-gen draw bug. jt574 -> L3666 (PICK race/class/gender/
  alignment) drew black only because I first probed it at jt918 ENTRY, before
  the loop sets up the clear/present + palette/mode state. Reached properly
  (Train -> case 0 -> l0f1a -> jt574, inside jt918's loop), it renders. Fix:
  port_test_seed_design enables Train (g_a5_-14440=1); jt574 sets g_a5_-2347=0
  (deep jt1135 x3 scale) so the PICK headers lay out spaced + legible (the
  Training Hall's x2 scale packed them together). Verified. The full wizard
  (stat roll, race/class lists, selection state machine) is the deferred
  CODE 17 lift; the screen now renders as the first slice.
- NEXT (boot UI):
  - The faithful Begin Adventuring (jt585 -> CODE 15/19 -> the real play
    loop) so the port_play_demo bridge can come off. Exit From Play
    (case 8 / l10ca) -> back to main menu.
  - ROUND-TRIP GLITCH: returning from the dungeon ('q') redraws the menu
    BLACK — after port_play_demo's double-buffered VIDEL mode, even jt315's
    gray-fill + qd_present doesn't reach the visible buffer (the dungeon
    present path and qd_present target different buffers/state). Double
    qd_present didn't fix it; needs a display-HAL present/buffer reset on
    menu re-entry (platform/display_videl.c). The forward path
    (menu -> Play) is unaffected.
  - The faithful Play path (jt315 -> return 1 -> l07dc -> jt918 party
    setup / Training Hall) and Select/Create/Delete design + Unlock Editor
    land in CODE 8/2/12 entries still PROBE-stubbed.
  - Version banner: find the real source for the two top lines
    (g_a5_-13948/-13944 hold a "%s%03d.dat" template in this build).
  - jt131(6) screen-clear is a stub — jt315 paints its own backdrop +
    primes qd_present as a workaround; lift the real clear when convenient.
  - The play-loop body l07dc + jt918 (new-game / Training Hall) and their
    CODE 12/17/18 case bodies are the next big stubbed area.

## 3D dungeon view — FAITHFUL PATH RESOLVED (2026-06-06)

The live renderer is **`render_3d_faithful`** — the 1:1 Mac slot-assembly
view (`jt199` frustum walk → `l5b42` transform → `jt200` tile select →
`jt114`/`l309c_tile` 8bpp blit). It is wired into BOTH live render sites
(`jt221` initial draw and `jt312` per-step movement redraw, commit 9f7ab27),
gated on deep mode (`jt1200()==3`). `render_3d_view` (texture trapezoids)
and `render_3d_raycast` (3-column frustum) remain selectable fallbacks
behind `FRUA_CORRIDOR` / `FRUA_RAYCAST`.

Done:
- Per-group wall sets — Wall1-3 (`8X8DB`/`8X8DC` `.CTL`), each its own CLUT
  band at clut 32/64/96 (`load_wall_groups`); level-change handle reload
  (`l6148`).
- Per-cell floor/ceiling/sky backdrop from `BACK.CTL` (`cell_backdrop_id`).
- FRAME.CTL set-9 chrome integration: the 88×88 native view seats in the
  hole at (24,24); `g_cwf_ox/oy = (20,12)` = the Mac deep-view clip origin
  (4,12) native slid into the hole.
- Double-buffered VIDEL present (c2p into the hidden buffer, vsync flip).
- Movement: arrow/mouse → `jt297` → `L1908` (turn/step); automap render
  cluster + party marker (`jt448`).

The two bugs that had blocked this for many sessions, both now fixed:
1. **View axis/scale** — `l5b42`'s deep transform is `((v-8012)<<2)+8`
   (×4+8, doubled-space); native 320×200 is a clean uniform halve to
   `<<1 +4` (the view is 88×88, not 176×176 — see the screen-size note).
   The earlier "side walls off-screen" was the static-DATA red herring
   (layout globals are byte-truncated, so render-time values are small
   0–9, captured live: `5 4 6 4 2 7 2 0 9 5 4 3 3 3 1 1 1 0 0 4`).
2. **jt200 per-layer step on the WRONG AXIS** (the real fix, commit
   0f62432) — `L59d4` 5a28-5a52 steps `fp@(10)` (the 8016-anchored
   VERTICAL coord, = jt200's `top`); the lift stepped `left` (horizontal),
   inverting the depth stack (far walls rode to the screen top, side-wall
   tops didn't meet the facing wall, ceiling read as a black void). Now
   steps `top` (deep +16 halved to +8). User-confirmed "perspective is
   perfect."

`jt199` + `l5b42` were verified faithful line-by-line against `CODE_07.s`
(the full L6234 band walk: near ×4 + mid ×2 + far ×2, origins, advdir/bdir,
soff0, soffsteps ±2/±3/±7, aMaxDepth gates 99/2/99/1, sub layers) and
`jt200` against `/tmp/jt200_capture.log` (100 calls, all 24 (code,sub)→idx
tuples match).

Remaining 3D polish (non-blocking):
- The `-27886` Wall3 post-process (`JT[468]/1004/459/406/115`), deferred.
- Faithful per-step re-render arms `L64f2..L666c` in `l63c0`.
- `render_3d_faithful` seeds `g_cwf_ox/oy` once so the `FRUA_L6234_VERIFY`
  walk loop can nudge the view live (`[` `]` / `,` `.`) for any fine-tuning.

### (historical) the blocked-pixel-path investigation

The notes below trace how the faithful pixel path was diagnosed over many
sessions. Kept for the RE record; superseded by the RESOLVED summary above.

The active renderer WAS `render_3d_view` — a perspective-trapezoid
*reconstruction*, not the Mac engine's real view. The goal was to replace it
with the faithful frustum walker so the port is 1:1 with the original:

- `jt199` (CODE 7 +0x6234) is lifted — `jt199_side` / `jt199_front` walk
  the four ray passes and call `l5b42` to place each visible wall slot.
- `l5b42` / `jt200` / `jt200_layer` place + blit a pre-rendered slot
  tile 1:1 (no scale loop), at the screen positions held in the read-only
  DATA layout globals `g_a5_-12202..-12240`.

FINDING (2026-06-01, byte-exact re-trace of CODE 7): the faithful PIXEL
pipeline is **blocked** by layout-global state we can't reconstruct.
Confirmed against the asm:

- `jt199` (L6234) deep clip rect is `((v-8012)<<2)+8` off the (8012,8016)
  anchor → the on-screen viewport is *small* deltas (screen =
  `delta_lowbyte<<4 + 8`, so the layout-global low bytes must be ~0-12).
- The side-wall pass (L63a2 case 2) passes `l5b42(8012, 8016,
  ydelta=g_a5_-12222+soff, xdelta=g_a5_-12202, …)` — the lift is faithful.
- `l5b42` (L5b42) reads each delta's **low byte** (signed). `-12222=516`
  → low byte 4 → on-screen; but `-12202=175` → low byte −81 → screen
  coord ≈ −1272, **off-screen**, on either axis (the X/Y-swap reading
  doesn't rescue it).
- `-12202` is **read-only across all 23 CODE segments** (no view-init
  writes it), so 175 is its permanent value. There is no recoverable
  state that maps the side walls on-screen.

UPDATE 2026-06-01 — UNBLOCKED by mon capture. Ran FRUA under the
mon-enabled BasiliskII (`docs/mac-emulator.md`) and dumped the live
globals: `CurrentA5` confirmed `0x01F74AC0`, and the layout table
`g_a5_-12240..-12198` (16-bit words) is **5 4 6 4 2 7 2 0 / 9 5 4 3 3 3 1 1
/ 1 0 0 4 0 0** — tiny values, NOT the 175/516/250 the static DATA image
held (a launch-time init overwrites them). The side xdelta `-12202` is
**4**, so `l5b42`'s deep transform `((8016+4·4)−8012)·4+8 = 88` lands
on-screen. So the static-DATA off-screen result was the red herring; the
faithful pixel path IS reconstructible with the real coords.
DONE: seeded the captured values in `boot_a5_seed_defaults` and confirmed
identical inside the live 3D view (second mon capture). Wired the faithful
colour path — `render_3d_faithful` (behind `FRUA_FAITHFUL`) loads the
active set's 48 pieces (`load_cw_full`) + palette band, and `jt199`'s walk
→ `l5b42` (real coords) → `jt200_layer` → `cw_blit_piece` blits the
pre-sized colour pieces 1:1 on screen. Fixed a row/col transpose (pass
`row=partyY, col=partyX` so `l5b42`'s `cell=col*h+row` matches the map).

WIP / next iterations:
- The walk fires real slots (7 in a corridor vs 0 before). Two transposes
  fixed: cell indexing (pass row=partyY,col=partyX) and the screen axes
  (l5b42's `top` is X, `left` is Y — `soff` spreads on `top`). Slots now
  spread horizontally.
- DISASSEMBLY FINDINGS (CODE 7 L6234, re-verified):
  * `l5b42` adds `ydelta+soff` to arg1 (`top` = Mac Rect Y) and `xdelta` to
    arg2 (`left` = X). So Mac convention `top=Y, left=X` — the original lift
    naming was right (an X/Y swap experiment was reverted).
  * `jt199` is MULTI-SCAN: the JT[3] selector is constant 2, but case 2
    (L63a2) is a leftward lateral scan that FALLS THROUGH to a rightward
    scan (L6556, `yadj=-1` vs the left scan's `+1`), and presumably the
    front passes after. The lift's 4-call decomposition is roughly right.
  * Each scan varies only `soff` (on the Y/top axis) while `xdelta` is
    constant (-12202/-12220 = 4 -> X = 4*16+24 = 88). So a scan lays its
    pieces along a near-vertical line at X~88; the *perspective* must come
    from the PRE-SIZED pieces (jt200's `sub`/idx picking smaller far tiles),
    EOB-style — not from the anchor moving in 2D.
- GROUND TRUTH CAPTURED (2026-06-01, mon BasiliskII + a non-intrusive
  instruction-loop hook at jt200 entry 0x01E5B2D4 and its 4 blit jsr sites;
  log saved /tmp/jt200_capture.log, 100 jt200 calls of a real HEIRS 3D
  frame). This OVERTURNS two earlier wrong conclusions and pinpoints the
  real bug:

  * **jt200 (L59d4) is 100% correct.** All 24 distinct (code,sub)->idx
    tuples match the lift exactly, e.g. code=9,sub=0 -> peel to code=4/grp1,
    code-- =3, far idx=sub+1=2, near idx=code*9+sub+2=30 — matches
    `far idx=2 near idx=30`. code=1,sub=6 -> idx 8; code=5,sub=3 -> idx 42;
    code=2,sub=1 -> idx 13. Every line verified.

  * **The bug is jt199: my lift is INCOMPLETE, not faithful.** I wrongly
    concluded the JT[3] view selector is constant 2. It is NOT — the
    `moveq #2` (L636a) only seeds it; an OUTER LOOP (tail L6e4a) iterates
    the selector 2 -> 1 -> 0, dispatching three scan BANDS:
      - case 2 (L63a2): side scans (sub 0 front-face / 9 side-face, both
        constant) + near front scans L66f2 (sub=1) / L67e2 (sub=2).
      - case 1 (L68be): mid band, nested depth loop, sub=3 (glob
        -12234/-12214) and sub=4 (-12232/-12212).
      - case 0 (L6bc2): far band, sub=5 and sub=6.
    So the real walk emits sub = 0,1,2,3,4,5,6 (+9 side) — a depth RAMP.
    My jt199_front froze sub at 1/2, so jt200 never saw sub>2 and never
    produced the near/big idx (30/31/32/33/42, and side idx 6/7/8). THAT
    is why only small far tiles drew. Earlier "synthesis is the root cause"
    and "needs no emulator" were both wrong; the capture was decisive.

- DONE: jt199 RE-LIFTED faithfully (boot.c). Added `jt199_band` (the
  case-1/0 facing+side scan) and rewrote `jt199` with the selector
  2->1->0 outer loop + all three JT[3] bands, transcribed line-by-line
  from the asm:
  * case 2 (sel=2, origin +2 fwd): the existing jt199_side x2 + jt199_front
    x2 (sub 0/9 side, 1/2 front) — unchanged, already faithful.
  * case 1 (sel=1, +1 fwd): jt199_band x2 — start orow+2*left (facing->sub3
    / left->sub4, soff -6 step +3) and orow+2*right (facing->sub3 depth<2 /
    right->sub5, soff +6 step -3), depth 0..2, globs -12234/-12214,
    -12232/-12212, -12230/-12210.
  * case 0 (sel=0, party cell): jt199_band x2 — start orow+left
    (facing->sub6 / left->sub7, soff -7 step +7) and orow+right
    (facing->sub6 depth<1 / right->sub8, soff +7 step -7), depth 0..1,
    globs -12228/-12208, -12226/-12206, -12224/-12204.
  Origin recedes one cell (back dir) per selector pass. Builds clean;
  asm-faithful + jt200 already verified against /tmp/jt200_capture.log.
  Visual confirmation deferred — the FRUA_MAP_DEMO entry (port_play_demo)
  is no longer on the boot path (the port now boots the real Training Hall
  UI), so rendering the 3D view needs menu navigation or a demo re-wire.
- DONE: demo re-wired. `port_play_demo` was buried behind FRUA_ENGINE_PROBE
  + FRUA_MAP_DEMO in jt361's test soup; added a clean independent hook in
  ua_main after jt361(1) under FRUA_3D_DEMO. `make EXTRA_CFLAGS="-DFRUA_3D_DEMO
  -DFRUA_FAITHFUL -DDEMO_LEVEL=2"` boots straight into the 3D view. CONFIRMED
  the jt199 re-lift: renders a receding stone corridor with side-wall wedges
  at multiple depths (the band/sub ramp firing) — the frozen-sub lift could
  not produce this.
- NEXT: the .tlb-vs-.ctl + synthesis question (deep mode loads .tlb w/
  placeholder synthesis via JT[111]; non-deep loads .ctl, all sizes
  present). The demo render shows the right STRUCTURE but wrong palette
  (blue speckle) + aspect — render_3d_faithful loads .ctl while deep mode
  wants .tlb-with-synthesis. Resolve so it blits the right tiles for the
  now-correct (code,sub) sequence. Also: keyboard walk (WASD) under
  --fast-forward didn't visibly step — check plat_kb_poll input plumbing.
- Meanwhile render_3d_raycast (visibility-faithful, on-screen, looks right)
  is the working demo renderer; the pixel-exact jt199 path is in progress.
- Strip the `g_cwf_blits` debug logging once the layout is correct.

### Pivot: faithful WALK + our texture renderer

The faithful part that IS sound: `jt199`'s frustum-walk logic + `l5e52`
wall probes (which walls are visible at each depth/side). Plan: drive the
colour render from that faithful visibility, but place slots with an
on-screen coordinate model (the viewport geometry render_3d_view already
uses) instead of the un-reconstructible `l5b42` pixel coords. That is "the
raycaster mixed with the textures" — authentic visibility, working visuals.

- [x] Port `jt199`'s frustum visibility — `render_3d_raycast` walks the
      wider 3-column (left/center/right) field, back-to-front, gating side
      columns on a clear line of sight (the corridor wall toward them being
      open), so it matches the old corridor view on straight halls and opens
      up at junctions/rooms. Now the default; `FRUA_CORRIDOR` selects the old
      `render_3d_view`. Renders with the per-edge wall + facet system at
      viewport-derived positions.
- [ ] Keep `l5b42`/`jt200` lifts in place (faithful, documented) for if/when
      the runtime layout state is ever recovered.

**Unblock route (runtime capture):** a BasiliskII Mac emulator is set up.
Running the real FRUA there and dumping the A5 slot-layout globals
(`g_a5_-12240..-12196`) at the moment `jt199` renders would give the real
on-screen coords and make the pixel-1:1 `l5b42` path renderable. (Needs the
game's copy-protection answers from the manual; not a porting blocker.)

## Future additions

Out of scope for the 1:1 port — revisit once the faithful engine is
solid:

- **Smooth-transition movement engine.** The original (and the faithful
  port) is instant grid-step: 90° turns and cell-to-cell jumps. A later
  optional mode could interpolate the view between cells/facings (slide
  forward, rotate turns) for a smoother feel — a deviation from the
  original, so gated behind a setting, built on top of the faithful
  renderer rather than replacing it.
- The trapezoid `render_3d_view` may stay as a fast/low-spec fallback.

## Connecting all the menus

See docs/menu-wiring-plan.md — phased plan to wire every menu on the shared
chrome/runner. Phase 0 = factor the chrome into a reusable menu_run() driven
by the live DLItem group (raised/recessed from rec[28]); then wire jt315's
faithful JT[3] dispatch (CODE22+0x5112) and lift each sub-menu. Deferred
polish: backdrop tile has a baked-in white 3D bevel line (FRAME.CTL item4 is
a framing piece, not a clean field — /tmp/frame_bar.png); bars/sizing.
