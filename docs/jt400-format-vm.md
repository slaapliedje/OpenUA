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
5. **Wire-up** ✓ **(core done)** — `l0306` (CODE 5+0x306) now runs the faithful
   jt400 with the on-screen emit sink + the "vka" handler set. The chain:

   ```
   jt1084("Error: %r", ...) -> l0306(fmt, &args)
       -> jt400(fmt, &args, jt966, "vka", jt967, jt968, jt969)
            jt966  emit sink   -> QuickDraw DrawChar at the A5 pen (-4898/-4896)
            jt967  "%v"        -> jt1135 move pen Y
            jt968  "%k"        -> jt1135 move pen X
            jt969  "%a"        -> l024c set color/style
   ```

   **LIVE.** `l036a` (JT[1084], CODE 5+0x36a) — the engine's "Error: …" modal,
   called from ~15 sites across the GLIB loader (LBLoad/LBISize/_LBConvert/
   FCSetup/...) and the repaint path — now renders its text through l0306 ->
   jt400 instead of vsnprintf+jt1089. So the live error path flows through the
   faithful VM. Same trigger conditions as before (only HOW the text draws
   changed), so no new modal behaviour. The box/input deps were already lifted
   (jt1161/jt1153/jt1147/jt1116/jt1205/jt1167, l0088/l00a8 real event pump;
   only l0062, the rare 'q'-abort teardown, is still a PROBE stub).

   C-vararg bridge: l036a's callers pass C-promoted varargs, which can't be
   word-streamed through jt400's "%r". So l036a flattens the message with
   vsnprintf first and feeds it to l0306 as a `%s` arg block — jt400's %s
   consumes a 4-byte char* (ABI-safe). The "Error: " literals + the message now
   emit through jt966/DrawChar.

   Pen geometry (jt966): g_a5_4896 = pen X (the advancing axis), g_a5_4898 =
   pen Y — the FAITHFUL convention (L0264/jt1084 pass (top,left); JT[1136]'s
   bounds put the wide axis in fp@10). This is the opposite slot order from
   jt1089's self-consistent-but-swapped path; the two must not be mixed. jt1089
   stays the live text path for C-vararg callers. The on-screen pixel position
   is VISUAL-UNVERIFIED (no error modal exercised in Hatari yet); baseline
   +g_hud_dy mirrors jt1089's DrawChar correction.

ABI correction (handlers): JT[400] is **variadic** after the emit sink —
`jt400(fmt, args, emit, handler_chars, h1, h2, ...)`. `handler_chars` (fp@20) is
the set-name string ("vka"); the handler fns follow as varargs, handler #idx at
fp@(20 + idx*4). `L3e62(handler_chars, c)` returns the 1-based index. The handler
is called as `h(acc, width)` and does NOT advance the arg cursor.

## Why this matters / why it was deferred

The port already formats via C `vsprintf`, so day-to-day text works — but that
shortcut can't do the custom conversions (`%r` recursive, `%w`, the arithmetic
ops) the Mac templates use, and it is a stand-in the project's faithfulness goal
wants replaced. `jt1084` (the "Error: %r" modal) is the first concrete consumer
that needs the real engine (specifically `%r` via step 4).
