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
