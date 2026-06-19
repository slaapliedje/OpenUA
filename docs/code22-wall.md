# CODE 22 wall — the FRONT DOOR (main menu + design-select) + the design editor

**The running worklist for CODE 22 — everything "before the game".** Update the
status columns in the same commit as each lift — this file is the duplicate-work
check: consult it (and `git grep -n 'static.*\bjtNNN( / \blXXXX('
src/engine/boot.c`, plus the dual-name check vs `data/work/disasm/jumptable.txt`)
BEFORE lifting anything here. Regenerate counts with `python3 tools/seg_audit.py 22`.

Status legend: **LIFTED** real body / `stub` PROBE placeholder / `—`/MISSING no
symbol yet. Canonical classifier = `tools/jt_progress.py` → `jt-lift-progress.md`.

## What CODE 22 is — TWO things

1. **The front door / play-entry UI**: title → **main menu** (`jt315` @ CODE
   22+0x4d8a) → **"Select a Design"** picker → the design-data load. This is the
   only CODE 22 the *player* sees, and it's **mostly DONE** — the one real gap is
   the design picker (`jt314`).
2. **The design EDITOR** (the FRUA authoring tool — create/edit adventures): the
   `jt281`/`282`/`286`/`292` record panels + `jt290`/`jt327` click/dispatch
   (DONE, task #121) + the `l1240`/`lee6`/`l347a` list/format tools. This is
   **NOT on the play path** — defer it unless we're building the editor.

**Snapshot (`seg_audit.py 22`):** 67 functions, 49 lifted (**73%**), 18 remaining
— 11 pending JT (all 1-call MISSING) + ~9 lXXXX locals.

## DONE (the menu spine + loaders + editor dispatch)
- `jt315` main menu dispatcher (CODE 22+0x4d8a) — LIFTED. The port's `menu_run`
  mirrors its build + chrome. **NOTE** (see `faithful-main-menu-code22` memory):
  there is NO traceable Mac path that draws the per-command bars, so the port's
  `menu_draw_plates` deliberately diverges — **do NOT "lift CODE 22 verbatim"**
  for the menu plates.
- `jt313` menu build · `jt127` design-data loader (GEO/STRG/MONST/GAME*.DAT,
  closes each file) · `jt290`/`jt327` editor click-tool + record-edit dispatch
  (task #121) · + ~40 more lifted (jt271–321 range).

## Cluster 1 — the PLAY-PATH gap (DO THESE FIRST)

The "Select a Design" → load flow. Small and self-contained; this is the only
CODE 22 the play path actually needs.

| piece | addr | size | what (disasm) | status |
|---|---|---:|---|---|
| **`jt314`** (= L494e) | 0x494e | 179ln | **the DESIGN-SELECT picker** — enumerates `*.DSN` (jt990/jt991 dir scan), builds the list (`"DSN"`/`"%s %s %s %s:"` via jt384/jt406), allocates entries (jt477), shows the chooser (→ jt169 list-dialog), loads each design header (jt133/jt147). **The #1 before-the-game target.** | — |
| `l4bea` | 0x4bea | 100ln | the **SAVE-folder enumeration** (`"SAVE"`, jt990/jt991 dir scan + jt431 path + jt416 delete + jt427) — the load-game / save-slot file lister; pairs with the jt585/jt582 picker work | stub |

Wiring: `jt315` (main menu) case "Select a Design" → `jt314` picker → `jt127`
loads the chosen design's GEO/STRG/MONST/GAME data into the A5 buffers the
Training Hall then runs on. The port currently seeds the design via
`port_test_seed_design` (CURRENT.TXT marker) — `jt314` replaces that with the
real chooser.

## Cluster 2 — the design EDITOR (authoring tool — DEFER unless building it)

The screens that create/edit an adventure. Not reached on the play path.

| piece | addr | size | what (disasm) | status |
|---|---|---:|---|---|
| `jt286` | 0x2aaa | 366ln | editor record panel (`"%s %s:"`, jt1089/jt1161 text, jt1200 colour, jt357) | stub |
| `jt282` | 0x2f24 | 283ln | editor record view (`"%s %d"`, jt1173/jt1193 + jt117 present) | stub |
| `l347a` | 0x347a | 243ln | editor format/list panel (`"%16s"/"%*s"`, jt1089/jt394/jt348/jt366) | stub |
| `jt281` | 0x329c | 147ln | editor field row (`"%*s"/"%16s"`, jt1089/jt406/jt488) | — |
| `l1240` | 0x1240 | 209ln | editor list/grid tool (jt218/jt213) — one of the jt290 editor locals | stub |
| `lee6` | 0x0ee6 | 267ln | editor list/grid tool (jt213/jt218) — jt290 editor local | stub |
| `jt292` | 0x14d8 | 162ln | editor list helper (jt218/jt213) | — |

## Cluster 3 — small leaves + jt3 sub-dispatchers (lift with their parent)

| piece | addr | size | role | status |
|---|---|---:|---|---|
| `jt309` | 0x0808 | 35ln | leaf (jt1113) | — |
| `l2806` | 0x2806 | 96ln | jt3 inline-switch sub-dispatch (jt1161) | — |
| `l16e0` `l173a` | 0x16e0 / 0x173a | 19/20ln | jt3 inline-switch dispatch leaves | — |
| `jt271` `jt291` `jt300` `jt301` `jt302` `jt310` | 0x4b0 … | 9–16ln | tiny leaves (getters / no-ops) | — |
| `l1bee` | 0x1bee | 16ln | leaf | — |

## Worklist discipline

1. **Play path = `jt314` only.** Lift the design picker (`*.DSN` enumerate →
   jt169 list dialog → jt127 load), wire it into `jt315`'s "Select a Design"
   arm, and the front door is faithful end-to-end (retiring the
   `port_test_seed_design` CURRENT.TXT stand-in). `l4bea` (SAVE enum) pairs with
   the save/load slot-picker work (jt585/jt582).
2. The editor (Cluster 2) is a separate, deferrable workstream — only touch it
   when building the authoring tool.
3. Pick a row. `git grep -n 'static.*\bjtNNN(' src/engine/boot.c` (duplicate
   check) + dual-name check vs `jumptable.txt` (jt314 ≡ L494e is the canonical
   example of the form collision here).
4. Lift, gate (`make`, codegen grep `muls.l|bfextu|bfins`, `make -s test`),
   update this file's status column, commit both together; re-run
   `python3 tools/jt_progress.py`.
