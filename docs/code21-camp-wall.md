# CODE-21 Encampment (camp / rest) subsystem — worklist

The "Encamp" command (g_walk_cmd 4 in `l63c0`, and the inn event) opens the
camp menu. Chain: **Encamp/inn → `l473e` → `jt957`** (the camp-menu dispatcher).

## Done (slice 1, 2026-06-21)

- **`jt957`** (CODE 21 + 0x2f9a) — the camp-menu dispatcher. Faithful structural
  lift: enters camp mode (mode 2; prev saved in -27989), draws the frame/title
  (jt103/jt94) when not in an overlay (-18486), loops a 9-row command menu
  (jt155 rows 0..8, gated: 2/4 drop at rec[44]>=100; 5/6 vs 7 by -18485),
  prompts via jt160, supports the quick-keys auto-select (-24139 → jt934/jt936),
  dispatches JT[3] 0..7, and on exit restores the prev mode + repaints
  (jt84/jt23 for overland mode 3, else jt78/jt937/jt938).
  - Working today: **View** (case 0 → jt904), **Rest** (2, slice 2 below),
    **Save** (6 → jt585 + jt159 confirm), **Load** (5 → jt942, with the
    unsaved-game confirm), **Exit** (7).
- **`l473e`** (CODE 20 + 0x473e) — the rest/Encamp trigger. Resolves the party
  cell (jt197), primes rec[44]=100 when the cell allows a full heal, opens
  `jt957`, recomposes on success. Reached from the Encamp command (res==4,
  `boot.c` play loop) and the inn event.

## Done (slice 2, 2026-06-21) — the REST action works end to end

- **`l09ea`** (CODE 21 + 0x09ea, camp case 2) — finds the longest per-member
  rest time (`l0006_c21`), splits it into the day/hour/ten-min/min display
  fields (-23206/-23208/-23210/-23212), then runs the **already-lifted** rest
  engine **`jt915`(1)** which advances the clock and heals/memorizes
  (interruptible "Stop Resting?"). So Rest now actually passes time + heals.
- **`l0006_c21`** (CODE 21 + 0x0006) — per-member rest minutes = type*60 +
  (Σ pending spell levels)*15 (type 4, or 6 with any level-3+ spell). Named
  `_c21` to avoid the lXXXX collision with the existing `l0006`.

`L09ea` is also called by the Magic camp action (`l1850`, case 1, case 4=Rest).

## Done (slice 3, 2026-06-21) — the MAGIC menu dispatcher

- **`l1850`** (CODE 21 + 0x1850, camp case 1) — the Magic menu. Faithful
  structural lift: 6-row menu (jt160), row 4 (Rest) dropped at rec[44]>=100,
  quick-keys auto-select, JT[3] dispatch 0..4. **Rest (case 4 → l09ea) and Exit
  (row 5) work**; the four spell screens are PROBE-stubbed (slices 5+):
  `l06d6` (0), `l0bc6` (1, ~190 ln), `l0df2` (2, ~458 ln), `l1374` (3, ~366 ln)
  — memorize / view / cast / scribe. Each is a large screen of its own.

## Remaining (slices 4..N) — still PROBE-stubbed

| case | fn | size | what it is (to confirm by reading) |
|-----:|----|------|-------------------------------------|
| 3 (camp) | `l1e44` | ~92 ln  | a 5-option sub-menu (L1abc, jt917, L1cb8, view jt573, L1c2a) |
| 4 (camp) | `l2d7e` | ~146 ln | Alter/party order? — big locals, jt406, calls L23dc/L2310/L2106/L1fcc/… |
| 0..3 (magic) | `l06d6`/`l0bc6`/`l0df2`/`l1374` | 190/458/366 ln | the spell-management screens (memorize/view/cast/scribe) |
| — | `l038a` | ~14 ln  | per-member recompute: walks -27928 calling `L026e_c21` → `jt638` |

The Magic spell screens (`l0df2` ~458 ln etc.) are the largest remaining piece;
`l038a` is the cheapest but pulls in `L026e_c21` (CODE 21 0x026e → `jt638`).

## Map of the camp globals

- `-27990` current mode (2 = camp), `-27989` prev mode, `-27987` flag
- `-18486` "in overlay" (suppress frame draw), `-18485` Save/Load gate
- `-24140` jt160 menu-state carry, `-24139` quick-keys mode
- `-22307` row counter (build loop), `-24126` 40-byte menu buffer
- `rec[44]` (rec = -28006) heal allowance (0 / 100); set by `l473e`
- `-27982` "keep adventuring" / reload flag (Save/Load/Exit set it)
- `-13952`/`-13660` jt160 menu title/block; `-14104` camp title; `-14288` save name
