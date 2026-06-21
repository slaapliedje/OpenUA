# Mac blit trace — HEIRS dungeon entry + opening "caravan" event

Captured from BasiliskII (real Mac binary, instrumented) by the user,
2026-06-21: from the Training-Hall menu through **Begin Adventuring** into the
dungeon, landing on the opening event cell, and **accepting the 100 platinum +
ring**. (The konsole scrollback truncated the *head*; this is the tail, which
fortunately contains the whole entry + event sequence.)

This is ground truth for the **event-pictures** subsystem's "intro caravan =
black frame" bug ([[event-pictures-wall]]).

## HEADLINE: the HEIRS opening "caravan" IS the shop/merchant event

Two captures (a head and a tail; the head starts at the "Loading...Please Wait"
play-screen build and runs through the menu → **Begin Adventuring** → entry →
the event). The decisive tell is at the end of the entry sequence: the command
bar paints **`View / Take / Pool / Share / Exit`**, then **`Money / Items /
Exit`**, then **`Select / Exit`** — these are the **`jt183` merchant-screen
commands** ([[shop-merchant-wall]]). So the opening caravan is the **case-8
SHOP/MERCHANT event** (`l5586`), which shows a picture + greeting text + the
merchant screen and pays out the **100 platinum + ring**.

This **resolves** the shop wall doc's open question ("is the HEIRS shop cell
real / reachable?") — yes, it's the very first event on entry — and reframes the
event-pictures Bug A: the "picture" is the **merchant illustration** drawn by
`l5586`→`l442e`, not a standalone intro bigpic.

## What's still truncated: the picture blit itself

The actual `l442e` picture load+draw (`JT43`/`JT44`/`L579e` or the `l541a` PIC
blit + its `jt993`/`jt1069`/`jt1066` palette commit) happened in the lost head,
*before* the "Loading...Please Wait" line. Both captures still lack it. But we
now have the full surrounding order, so the picture-palette ordering can be
pinned from the PORT side (reproduce + observe) rather than another capture.

What the captures DO reliably show: the full **menu → Begin Adventuring → HUD →
3D view → event text box → Return → reward → merchant screen** sequence, the
entry frustum (25 slots), and the UI text-colour band.

## The decoded entry sequence (after "Begin Adventuring" is pressed)

"Begin Adventuring" highlights (`JT1089 #176/#177`, colour 135→131), then:

1. **HUD roster** — `JT1089 #178-239`, `v=8008..8036`, `h=8068/8132/8148`
   (8000-based dungeon space). 6 party rows × 3 columns (name + 2 stat cols).
2. **3D view** — `JT200 #0-24` = **exactly 25 wall slots** (matches the known
   good frustum, [[dungeon-3d-view-faithful]]). Re-blit passes repeat the same
   25 (#25-49, #50-74, #75-99, #100-124, #125-149) — several full repaints.
3. **Event text box** — `JT1089 #284-498`: a dense grid of `%c` at
   `v=8068,8072,8076,8080,8084,8088` (6 rows) × `h=8004..8152` step 4 (~37 cols)
   = a **6-line × ~37-char fixed-width text box** in the bottom content window
   (screen y≈136-176, the area BELOW the 3D view). Drawn one `JT1089 %c` per
   character. A colour-131 band (#368-403, #502-578) marks one highlighted block.
4. **Prompt** — `#499 v=189 h=8032 "%s"` + `#500/#501 v=189 h=112 "Return"`
   (the event's Return prompt, screen-space row 189 = the command bar).
5. **Reward text** — `#582 v=8020 h=8004 "%s"`, `#583 v=8028`, `#584 v=8040`
   = the "100 platinum / ring" lines, drawn on the LEFT of the content window.
6. After dismiss → `JT200 #100-149` (the dungeon repaints, view still present).

## What the tail nails down (post-picture)

- **Compose order (after the picture):** HUD roster → 3D view (walls) → event
  text box (content window) → Return prompt → reward text → dungeon repaint.
- **UI text colours are in the UPPER CLUT band:** headers 140, sub-headers 139,
  body 135, highlight 131, buttons 143/139, status 112. All ≥112 — nowhere near
  0..31. This MATTERS for the picture bug: an event picture that installs a
  palette over 32..255 lands right on top of these text indices, so the *order*
  of "install picture palette" vs "draw UI text" is what decides whether text
  survives. The head trace (with the picture) is what pins that order.
- **The frustum emits 25 slots** at the entry cell — consistent with the port's
  faithful jt199/jt200 path.

## Still needed: the HEAD (with the picture)

To chart the picture present path we need the `JT43`/`JT44`/`L579e` + the picture
GLIB blit + its `jt993`/`jt1069`/`jt1066` palette commit, and crucially **whether
the Mac draws the picture's pixels BEFORE committing its palette, and whether the
picture replaces or sits within the play screen.** Re-capture piped to a file so
the buffer limit is gone, e.g.:

```sh
BasiliskII 2>&1 | tee /tmp/heirs-entry-full.log     # then start->caravan->accept
```

(or redirect the instrumented binary's stdout to a file). Then we have the full
sequence from program start through the caravan picture.

## Open question for the port's black-frame bug

The port already composes the play frame (`jt23()` at boot.c:3545, dungeon
mode 4) BEFORE firing the landing-cell event (`l709e(special)` at boot.c:3563).
So the black frame is most likely a **present/timing or pool** issue, OR the
picture-palette ordering the head trace will reveal. Reproduce the port entry in
Hatari and observe WHICH is black (whole screen / 3D view / frame / text) to
disambiguate. Per the project CLUT rule, confirm before changing the palette path.
