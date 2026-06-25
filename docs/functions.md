# Functions — reference index

One entry point to the function reference material. **Grep these before writing a
new function or guessing a name** — the recurring trap is re-deriving (or
re-implementing) something that already exists.

| Doc | What it is | Regenerate |
|-----|------------|------------|
| [`jt-aliases.md`](jt-aliases.md) | **The jt ↔ `lXXXX` alias map.** Every `jtN` → its `CODE seg+0xOFF` → its `lOFF` alias, plus the name + **line** it's defined under in `boot.c` and its lift status. Use it whenever you need to know "what's the local name for this JT?" or "is this defined above my call site?" | `python3 tools/jt_aliases.py` |
| [`function-index.md`](function-index.md) | Auto-generated catalog of **every** C function in the engine + shim/HAL, with origin (`CODE N+0xXXXX` / `JT[N]`), kind (`jt`/`l`/`port`/`shim`), a one-line purpose, and `file:line`. | `python3 tools/function_index.py` |
| [`function-reference.md`](function-reference.md) | **Curated** deep-dives: subsystem ABIs, calling conventions, and the traps worth remembering. Hand-written; add a line when you trace something non-obvious. | (manual) |

## The two things that keep biting us (and where the answer lives)

1. **jt ↔ alias translation** — "is `jt857` some `lXXXX`?" → yes, `l77a0`.
   Look it up in `jt-aliases.md` (the *alias* and *defined as* columns).
2. **Forward declarations** — a faithful lift calls `jtX`/`lY` that is defined
   *later* in `boot.c`, so the call needs a `static … jtX(…);` first. The
   *line* column in `jt-aliases.md` tells you each function's definition line:
   if you're calling it from above that line, forward-declare it.

## Naming recap (see also CLAUDE.md → Decompilation workflow → Naming)

- `jtNNN` — a jump-table export, lifted from `jumptable.txt`. Lower-case.
- `lXXXX` — a CODE-local helper at hex offset `0xXXXX`. A `jtN` and its `lOFF`
  are the **same function** at the same address; the codebase defines it under
  whichever name was natural at lift time (the `jt-aliases.md` *defined as*
  column is the source of truth).
- `lXXXX_cNN` — disambiguates an `lXXXX` that collides across CODE segments
  (e.g. `l0006` exists in both CODE 13 and CODE 18); `_cNN` = the CODE segment.
- `g_a5_N` — an A5-world global at offset `-N` (typed macros in `boot.c`).
