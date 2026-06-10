# JT[400] — the THINK C "%r" format VM

`JT[400]` (CODE 3 + 0x3fb8, ~490 instr) is **not** a plain printf. It is THINK
C's `%r` recursive-format facility: a little stack/accumulator VM driven by a
template string, emitting characters through a caller-supplied callback. The
port currently stands this in with C `vsprintf` (jt394/jt488/jt94/jt1089) — this
doc maps the faithful engine so it can be lifted incrementally to replace that
stand-in. Decoded 2026-06-10.

## Signature / ABI

```
JT[400](char *fmt,          /* fp@(8)  — template, consumed byte-by-byte   */
        void *args,         /* fp@(12) — arg cursor (-20 internal copy)    */
        void (*emit)(short),/* fp@(16) — output sink, called per character */
        void *handlers)     /* fp@(20) — custom-conversion set, 0 = none   */
```

Locals: `acc` = fp@(-26) (the word accumulator), `width` = fp@(-22),
`zeroflag` = fp@(-1) (`%0N`), `argp` = fp@(-20) (arg cursor, advances 2 per
word arg / 4 per `%s` long).

## Main loop (L45c0 / L3fcc)

```
for each char c in fmt (until NUL):
    if c != '%':  emit(c)                       /* literal */
    else:
        acc = *(short*)argp                     /* load current arg word   */
        parse [0][digits] -> zeroflag, width    /* %0, field width         */
        c = next fmt char                        /* the conversion char     */
        if handlers && lookup(c) (L3e62): call handler(acc, width)
        else: dispatch c via the 31-op JT[1] table (below)
```

## Conversion / operator table (JT[1] @ 0x409c, 31 cases)

**Value-format arms** (emit the accumulator, advance argp):

| char | code | base / behavior | argp |
|------|------|-----------------|------|
| `d` | 100 | signed decimal: emit '-' + negate if acc<0, then L3e94(base 10) | +2 |
| `u` | 117 | unsigned decimal, L3e94(base 10) | +2 |
| `x`/`X` | 120/88 | hex, L3e94(base 16) | +2 |
| `o` | 111 | octal, L3e94(base 8) | +2 |
| `l` | 108 | long variant (0x4296) — reads a long arg | +4 |
| `s` | 115 | string: read `char*` (long) from arg, emit + space-pad to width | +4 |
| `c` | 99 | char: pad, emit acc low byte (0x41c8) | +2 |
| `w` | 119 | word: emit acc high byte then low byte (0x421a) | +2 |
| `r` | 114 | **recursive format** (0x4344) — re-enter the VM on a sub-template | — |
| `g` | 103 | (0x44c0) | |

**Arithmetic arms** (operate on `acc` = fp@-26 with operand `width` = fp@-24,
no emit; these make the template a tiny calculator):

| char | code | op |
|------|------|----|
| `-` | 45 | acc -= operand |
| `^` | 94 | acc ^= operand |
| `/` | 47 | acc = (signed) acc / operand |
| `#` | 35 | acc = acc / operand, take remainder (mod) |
| `|` | 124 | acc \|= operand |
| `~` | 126 | acc = ~acc |
| `\` | 92 | acc = (unsigned) acc / operand |
| `=` | 61 | acc = width (load immediate) |
| `&` `*` `+` `>` `<` `(` `)` `[` `]` `;` `:` | 38 42 43 62 60 40 41 91 93 59 58 | bracket/scope + remaining ops (arms at 0x44d4..0x448a) — TBD |
| `z` | 122 | acc = 0 |
| NUL | 0 | back up argp, treat as literal '%' |

## Leaf helpers

- **L3e94** (CODE 3+0x3e94, 56 instr) — **LIFTED** (`l3e94`, this session): the
  recursive unsigned number emitter for d/u/x/X/o. value/base recursion, MSD
  first, pad at the base, digits 0-9A-F via the emit callback.
- **L3e62** (CODE 3+0x3e62, 21 instr) — custom-handler lookup: scan the
  `handlers` set for conversion char `c`, return its descriptor (or 0). Needed
  for `%r` and any game-registered conversions (jt966-969, the "vka" set).
- **L3e94 callers** pass (acc, width, zeroflag, base, emit).

## Lift plan (phased)

1. **Leaf** — `l3e94` ✓ (done). 
2. **Core loop + common arms** — `jt400()` parse loop + emit sink + the
   value arms d/u/x/X/o/s/c/w/l (all funnel through l3e94 or direct emit). This
   is the bulk of real-world formatting.
3. **Arithmetic arms** — the one-line ops (- ^ / # | ~ \ = z) on acc/operand.
4. **L3e62 + custom handlers** — wire the `handlers` set; lift jt966-969 and the
   `%r` recursion (it re-enters jt400 on a sub-template). Unlocks `%r`.
5. **Wire-up** — point jt1084's "Error: %r" (L0306) at the faithful jt400, and
   optionally migrate jt94/jt1089/jt394 off C vsprintf onto it.

## Why this matters / why it was deferred

The port already formats via C `vsprintf`, so day-to-day text works — but that
shortcut can't do the custom conversions (`%r` recursive, `%w`, the arithmetic
ops) the Mac templates use, and it is a stand-in the project's faithfulness goal
wants replaced. `jt1084` (the "Error: %r" modal) is the first concrete consumer
that needs the real engine (specifically `%r` via step 4).
