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
        void *handlers,     /* fp@(20) — custom-conversion set, 0 = none   */
        void *extra)        /* fp@(24) — forwarded through %r recursion     */
```

(5 params, not 4 — the recursion at 0x4378 pushes fp@(24) and `lea sp@(20),sp`
pops 5 longs. `extra` is opaque to jt400; it is only forwarded into the `%r`
sub-call, never dereferenced.)

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
2. **Core loop + common arms** — `jt400()` parse loop + emit sink + value arms
   d/u/x/X/o/s/c/w/l ✓ **(done)**. Also lifted `l3f20` (the long analog of
   l3e94, used by `%l`; the Mac's JT[5]/JT[6] unsigned-long div/mod map to C `/`
   `%` on `unsigned long`).
3. **Arithmetic + group arms** ✓ **(done — landed with phase 2)**. The
   arithmetic arms (`+ - * / # \ & | ^ ~ > < =`) branch back to the width-parse
   (L45ae→L3fe0) rather than the main loop, so they're interleaved with the
   value arms and can't be split cleanly — lifted together. Also did the group
   control arms: `%(..%)` repeat (counter at fp@-28, body start at fp@-10),
   `%[..%]` indexed select (saved cursor at fp@-32, `%;`/`%:` separators,
   inner scan table @ 0x444c), `%g` random-access arg, `%z`/default char-repeat
   (the L45a2 loop — default arm = emit the unknown char `operand` times), and
   `%r` recursion (reads TWO longs: sub-fmt + sub-args, re-enters jt400). Note:
   jt400 takes **5** params, not 4 — `extra` at fp@(24) is forwarded through
   `%r` (never dereferenced by jt400 itself).
4. **L3e62 + custom handlers** — the ONLY remaining deferral. Wire the
   `handlers` set: lift L3e62 (descriptor lookup) + the descriptor CALL at
   L4056 (`*(fp + idx*4 + 20)(acc, width)`), jt966-969 (the "vka" handler set).
   The jt400 lift already has the `if (handlers) { /* TODO phase 4 */ }` hook in
   place; it currently falls through to standard dispatch (no port caller passes
   a handler set yet, so this is dormant).
5. **Wire-up** — point jt1084's "Error: %r" (L0306) at the faithful jt400, and
   optionally migrate jt94/jt1089/jt394 off C vsprintf onto it. `jt400` is
   currently `__attribute__((unused))` (dead-stripped) until wired.

## Why this matters / why it was deferred

The port already formats via C `vsprintf`, so day-to-day text works — but that
shortcut can't do the custom conversions (`%r` recursive, `%w`, the arithmetic
ops) the Mac templates use, and it is a stand-in the project's faithfulness goal
wants replaced. `jt1084` (the "Error: %r" modal) is the first concrete consumer
that needs the real engine (specifically `%r` via step 4).
