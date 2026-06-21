# jt94 / jt1089 UI-text colour model — analysis (2026-06-20)

## Symptom

The dungeon "Press [Return] to continue." prompt (and the compass face) render
**black**. Reported as part of the caravan #129 issue.

## Root cause (fully traced)

The Mac UI text colour is a **full 8-bit index** into a **256-entry** UI CLUT:

    jt94(page, row, col, colour, fmt)   ->   color word = (colour << 4) | col
    jt1089 sets the QuickDraw fgColor to that whole byte (0..255).

The high nibble (`colour`/"style") selects a CLUT *band*; the low nibble (`col`)
is the base colour. The full 256-entry palette is genuinely loaded — `load_menu_ui`
reads MENU.CTL item 0 (256 RGB triples) into `g_menu_pal[0..255]`, `g_menu_pe = 256`,
and installs all 256 (verified live). Sampled entries:

| clut | RGB | meaning |
|-----:|-----|---------|
| 11  | (103,255,255) cyan  | base text col 11 |
| 112 | (155,207,155) sage  | **the prompt** (style 7, col 0) |
| 128 | (103,155,51) olive  | style 8, col 0 |

So the prompt's intended colour is **clut[112] = light sage**, not black.

## Why the port shows black

The port collapsed the model to **16 colours**:
- `jt94` (boot.c step 3) FORCES `style = 8` whenever the colour arg is non-zero
  (the Mac forces 8 only when the arg is **0**; a non-zero arg is kept). The
  port also has this `if` inverted vs the Mac (L3fd6 @ 0x3ffa).
- `jt1089` masks the index to the low nibble: `fgColor = color & 0x0f`.

For the prompt `jt94(7,24,0,7)`: port -> style forced to 8 -> color `(8<<4)|0 = 128`
-> `& 0x0f = 0` -> **clut[0] = black**. Body text survives because it uses col 11
(low nibble), which the port keeps in clut[0..15].

## The faithful fix — and why it is NOT two lines

The fix is: `jt94` keep the colour high-nibble (don't force 8) + `jt1089` index
with the full byte (`& 0xff`). For normal text (colour arg 0, value < 16) this is
identical to today. **But it regresses callers that were written against the
16-colour truncation.** Caught live: `jt137` (the menu command-button DLItem
method) passes `colour = (rec[28]&1) ? 503 : 240`, relying on `503 & 0x0f = 7`
to get grey — the full byte makes it `clut[247]` (green/magenta). The whole menu
button row went green/magenta; reverted immediately.

So the port has **two contradictory jt1089 colour conventions**:
- FAITHFUL callers pass real indices: literals **135 (x13), 139 (x5), 240 (x1)**
  (style-8/15 bands) — these WANT the full byte.
- TRUNCATION-ERA callers pass values whose low nibble is the intent: `jt137`'s
  `503`/`240`. Plus ~15 variable-colour callsites (`col`/`pal`/`cw`/`colour`/
  `g_a5_word(-7000)`: char-gen "PICK RACE/CLASS", roster, editor) that each need
  per-caller classification.

## The real task (proposed: a dedicated #129-text-colour effort)

1. Make `jt94` faithful (keep colour nibble for non-zero) + `jt1089` full byte.
2. Audit every `jt1089` caller (~20). For each passing >= 16: is the intent the
   full index (keep) or the truncated low nibble (rewrite to the low value, e.g.
   `jt137` 503 -> 7, 240 -> 0)? The literal 135/139/240 callers are already
   faithful; the variable ones need reading.
3. Per-screen regression pass: menu, roster, char sheet, char-gen pick screens,
   editor panels, dungeon HUD, then the caravan prompt.
4. CLOBBER follow-up: clut[112..255] (the UI style bands) sit in the 32..255
   range that event pictures + wall sets overwrite. After step 1-3 fix the
   prompt everywhere static, the in-game (dungeon/caravan) prompt still needs the
   UI style bands preserved/reinstalled after a picture — extend the existing
   `g_clut_clobbered` recovery (which today only re-lays walls 32/64/96 +
   backdrop 145) to the UI text bands.

Status: reverted to the known-good 16-colour path (no regression shipped). This
doc records the analysis so the coordinated fix can be done deliberately.

## REFINED MODEL (2026-06-20, after reading the Mac glyph path + live palette)

The Mac colour path is `jt1089` (L0334) -> `L024c` (stores color word -4894 +
its high byte -4892) -> `jt966`/`JT[1136]` glyph blitter. **JT[1136] draws the
glyph with the colour word as the PIXEL VALUE** — i.e. `color = (style<<4)|col`
is a direct CLUT index (function-reference.md: "mode 0 = 1bpp mono, OR fgColor").
The port replaced JT[1136] with QuickDraw DrawString + `fgColor = color & 0x0f`.

Key consequence — the truncation is NOT broadly wrong:
- **Style 8 (128..143) appears to MIRROR the base 16 colours** (clut[128+c] ~
  clut[c]). The port's `& 0x0f` yields clut[c] = the same result, so style-8
  text (almost all UI text — menus, sheets, jt137 buttons via 131/135/139/143)
  is already correct. That's why the menu looks right today.
- **Style 7 (112..127) is a NON-mirror ramp** — clut[112] != clut[0]. The
  prompt (jt94 ... ,7) is the one casualty: truncation -> clut[0] = black.

So a global `& 0xff` is the WRONG fix: it would send the style-8 callers to
clut[131/135/139/143] = MENU.CTL *image* colours (magenta/red/green, verified
live) instead of the mirrored base. The menu regression confirmed this.

### BLOCKER — need Mac ground truth

The port loads MENU.CTL item 0 (a 256-entry IMAGE palette) into clut[0..255].
Its upper bands are image colours, NOT the Mac's UI-text ramps. To fix style-7
text (the prompt) faithfully we need the Mac's real `clut[112..127]` (and to
confirm 128..143 mirrors 0..15). Best obtained from **BasiliskII mon** (read the
live Mac CLUT) — the project's strongest ground truth. Alternatively, lift the
jt94 row-24 bottom arm fully to see whether the prompt's colour even comes from
`(style<<4)|col` or from the DLItem path.

Until then: leave the 16-colour truncation (correct for the dominant style-8
case). Do NOT flip jt1089 globally. The prompt-black is cosmetic and isolated.
