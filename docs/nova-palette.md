# Nova / ATW800/2 — the palette path

**Status: SOLVED on hardware (2026-08-12). The card owns its palette through the
FPGA LUT; VDI pens cannot express it.**

## The symptom

On the Nova the main-menu hotkey (accelerator) letters were BLACK. Everything
else looked right: chrome, art, sprites, and the *pressed* button's cyan
accelerator. Visiting the char-gen sprite page appeared to "fix" it.

Two maintainer observations cracked it, and both overturned a working theory:

1. **"The pressed button has the right colours; only unpressed ones are black."**
   `menu_button_press_draw` uses FIXED indices — unpressed = clut **15** (white),
   pressed = clut **11** (cyan). So exactly one slot was broken, not the palette
   generally.
2. **"The sprite page turns them CYAN, not white."** So it never unlocked
   anything — that page simply draws the labels in the pressed style. The
   "initialisation" theory was wrong.

Note `dim` was ruled out too: a dimmed item draws `BODY = 0` as well, so the
whole label would be black — the bodies stayed grey.

## Root cause

`nova_hw_inverse` is the standard **16-colour** Atari VDI pen→register table, in
which hardware register 15 is reached by **pen 1**. But pen 1 is VDI's *black*,
and on a **256-colour** device black is index `ncolors-1` = **255**. So every
UI-white write (14 per session, always `0xFFFFFF`) landed on slot 255 — leaving
the letter wrong *and* clobbering a live art colour. Slot 11 → pen 14 works,
which is why only unpressed buttons were affected.

Engine side was blameless, verified on the ST backend: the UI palette installs
LAST (after both `clut 129` installs) with the correct cyan and white.

## The fix

Write the card's **hardware LUT** directly — index == slot, so all 256 entries
are reachable and the pen table's unreachable slots are gone.

```
Logbase()      = 0xFEA00000            card video base (ADDR jumper position)
LUT            = base + 0x1FF000       256 x 16-bit, read/write
entry format   = RGB565, big-endian word
```

- **Always derive the address from `Logbase()`.** The manual's absolute addresses
  (Mega ST `0xDFF000`, Mega STE `0xBFF000`, TT `0xFEDFF000`) assume VidMem at
  `0x(FE)C00000`; the ADDR jumper can move it to `0x(FE)A00000`, taking the LUT
  with it. Verified by write/read-back/restore, with a silent fallback.
- **Never probe a second candidate blindly.** Reading the 4MB offset
  (`base + 0x3FF000`) on a 2MB card gives 2 bombs and a crash to the desktop —
  learned the hard way. 4MB cards are opt-in via `novalut=4mb`.
- The format was **decoded from a read-back** against known colours (slot 7 =
  `0xAD55` grey, 254 = `0xFFE0` yellow), not assumed from the manual's 15bpp
  PIXEL layout — which is a different encoding.

## …and why one write is not enough

The LUT must be **re-asserted**, because the VDI keeps re-emitting its own colour
table over ours (the manual: "CLUT shadow … needs to be kept up-to-date by the
VDI"), and we can no longer mirror slot 15 to it — pen 1 addresses slot 255.

Measured on the card, slot 15 was found holding, in order:

```
0xFFF9 (255,255,205)   near-white
0xFFEF (255,255,123)
0xCE99 (205,210,205)   grey
0xFFF7 (255,255,189)
0xFABF (255, 85,255)   the VDI magenta default
0x0000 (  0,  0,  0)   black   <- what the player saw
```

So the palette is asserted:

1. **After** a batch's `vs_color` calls, never inline. Writing `LUT[15]` at `i=15`
   and then calling `vs_color` for 16..32 let the VDI revert us microseconds
   later — self-inflicted, in the same loop. This is what made the letters flash
   white and then go black.
2. **Once per full present**, so a re-emit between palette installs is corrected
   within a frame. 256 word writes against a 512,000-byte present is free.
3. Over **every slot ever set**, not just the current range, so a re-emit
   triggered by an unrelated range cannot leave an older slot wrong.

`vs_color` is still called for `idx != 15` so VDI's own table stays roughly in
step for any AES/VDI redraw. Slot 15 no longer writes pen 1 at all, which also
ended the slot-255 corruption.

A bounded (8-line) `lut: slot15 CLOBBERED` check stays in the shipping build: it
is what proved the mechanism, and it will prove a regression just as cheaply.

## video.cfg keys

| key | effect |
|---|---|
| `novalut=off` | disable the direct LUT path (back to `vs_color` only) |
| `novalut=4mb` | 4MB card — LUT at `base + 0x3FF000` |
| `novawhite=<pen>` | override the slot-15 pen (only used when the LUT is unbound) |
| `nova=force` | run the Nova probe even when `_VDO` says Falcon (emulators) |
