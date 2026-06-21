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
  - Working today: **View** (case 0 → jt904), **Save** (6 → jt585 + jt159
    confirm), **Load** (5 → jt942, with the unsaved-game confirm), **Exit** (7).
- **`l473e`** (CODE 20 + 0x473e) — the rest/Encamp trigger. Resolves the party
  cell (jt197), primes rec[44]=100 when the cell allows a full heal, opens
  `jt957`, recomposes on success. Reached from the Encamp command (res==4,
  `boot.c` play loop) and the inn event.

## Remaining (slices 2..N) — the five deep camp actions, all PROBE-stubbed

| case | fn | size | what it is (to confirm by reading) |
|-----:|----|------|-------------------------------------|
| 1 | `l1850` | ~115 ln | camp action 1 (Magic/memorize?) — jt399 + menu |
| 2 | `l09ea` | ~65 ln  | camp action 2 — calls L0006 |
| 3 | `l1e44` | ~92 ln  | camp action 3 — itself a 5-option sub-menu (L1abc, jt917, L1cb8, view jt573, L1c2a) |
| 4 | `l2d7e` | ~146 ln | camp action 4 (Alter/party order?) — big locals, calls L23dc |
| — | `l038a` | ~14 ln  | per-member recompute: walks -27928 calling `L026e_c21` → `jt638` (CODE-16 effects) |

`l038a` is the cheapest but pulls in `L026e_c21` (CODE 21 0x026e → `jt638`,
CODE 16). The actual heal/time-advance ("rest") lives inside one of the action
sub-menus — identify it (the one calling `jt914` calendar-advance or HP
restore) and lift that first in slice 2.

## Map of the camp globals

- `-27990` current mode (2 = camp), `-27989` prev mode, `-27987` flag
- `-18486` "in overlay" (suppress frame draw), `-18485` Save/Load gate
- `-24140` jt160 menu-state carry, `-24139` quick-keys mode
- `-22307` row counter (build loop), `-24126` 40-byte menu buffer
- `rec[44]` (rec = -28006) heal allowance (0 / 100); set by `l473e`
- `-27982` "keep adventuring" / reload flag (Save/Load/Exit set it)
- `-13952`/`-13660` jt160 menu title/block; `-14104` camp title; `-14288` save name
