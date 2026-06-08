# JT call-frequency audit

A porting-priority tool (idea: lift the most-shared functions first, then the
stand-ins fall away and the rest composes). Counts how often each `JT[N]` entry
is *called* across the whole Macintosh decompilation, cross-referenced with the
port's lift status.

## Regenerate

```sh
# call frequency across all CODE segments (from dis68k's (JT[N]) annotations)
grep -rhoE "\(JT\[[0-9]+\]\)" data/work/disasm/CODE_*.s \
  | grep -oE "[0-9]+" | sort -n | uniq -c | sort -rn > /tmp/jt_freq.txt
```

Then cross-reference `/tmp/jt_freq.txt` against the `jtN` definitions in
`src/engine/boot.c` (a one-line body that is only `PROBE(...)` + `(void)` casts
+ `return 0` is a true stub). 1205 distinct JT entries are called; ~63 of the
port's `jtN` are still one-line stubs.

## The shared foundation (top-called — verify these stay FULLY lifted)

These are the load-bearing primitives; most are already lifted, which is why
recent UI/HUD work composed cleanly. If any regresses to a stub, everything
above it breaks.

| JT | calls | what |
|----|-------|------|
| jt3 | 307 | THINK C inline `switch` dispatch |
| jt384 | 287 | string copy |
| jt1200 | 187 | display-mode query (deep gate) |
| jt488 / jt394 | 156 | sprintf-style format |
| jt94 | 155 | text draw |
| jt406 | 153 | BlockMove / memmove |
| jt1161 | 147 | PaintRect (fill) |
| jt1089 | 143 | formatted text draw |
| jt399 | 126 | memset/fill |
| jt1135 | 83 | 8000-space → screen coord scale |
| jt452 | 81 | DLItem stream builder |
| jt468 / jt1001 | 64/69 | GLIB group lookup / glyph blit |
| jt117 / jt112 / jt108 | 56/43/30 | present / paint-mode / commit |

## High-leverage TRUE stubs to lift (most-called, real work pending)

The actionable queue — sorted by call count. Lifting these removes the most
stand-in pressure.

| JT | calls | what (CODE addr) |
|----|-------|------------------|
| jt96 | 43 | rect/paint wrapper over jt1135/jt1161/jt1089 |
| jt23 | 37 | play-frame stand-up (Inv/redraw) |
| jt1084 | 34 | setter (buf, val) |
| jt938 | 27 | status / clock line (CODE 12+0x562) — HUD clock |
| jt358 | 27 | counter (CODE 8+0x6e4a) |
| jt1193 | 24 | (CODE 7) view-prep tail |
| jt876 | 22 | popup action handler (CODE 18+0x1666) |
| jt1177 | 22 | row-blit draw primitive (HAL-deferred) |
| jt273 | 22 | deep-mode flag (CODE 22+0x4900) |

Play/HUD-relevant subset (current critical path): **jt23, jt938, jt273, jt96**.

## Genuine no-ops / constants — faithful AS stubs, do NOT "lift"

Verified against the Mac body; leave them.

| JT | calls | Mac body |
|----|-------|----------|
| jt1170 | 24 | empty (`linkw/unlk/rts`) |
| jt1198 | 30 | returns 1 always (glyph row-step constant) |
| jt1163 | 36 | returns 0 |
| jt949 | 2 | empty (`rts`) |

## Caveat

`(JT[N])` counts are *static call sites* in the disasm, not runtime hotness —
but for "what's most depended-on across the code" they're the right proxy. The
list above counts single-line stubs only; a few heavily-called multi-line
functions (jt1134, jt878, jt1061, jt936, jt868, jt935…) are partial lifts worth
spot-checking individually.
