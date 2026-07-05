# CODE 11/22 area-map chain — worklist (#151)

The last MISSING cluster that touches the **runtime** (not the design
editor): the CODE 11 area/geo functions and the CODE 22 area-command
dispatcher that drives them. Picked over CODE 8 monster-art (dark, no
live parent), CODE 2 recorder (editor, ADR-0008), and CODE 10 viewers
(editor-adjacent) because CODE 22 is the LIVE dungeon-view subsystem
(jt312 render, jt280 compass, jt311 overland move, jt287 keyboard walk
proc — all lifted, reachable per #124), so its gaps are on the walk
path and screenshot-validatable once wired.

## Corrected scope — the "8 missing area-map entries" are NOT one chain

Scouting the cluster fragmented it three ways (recorded so the next
session doesn't re-mis-scope it):

| entry | reality |
|---|---|
| **jt195** | = **l4db4** (CODE 7), already lifted — false-MISSING. The design **string-table** region setup (-12304/-12314/-12318), not automap. Aliased in `jt_progress.py`. |
| **jt281 / jt282 / jt286** | = **l329c / l2f24 / l2aaa** (CODE 22), already present as PROBE **stubs** — the design-list ENTRY PAINTERS (kinds 0..3, the jt322 Keys/Items/Quests family). **Editor** territory (ADR-0008 defers tools); mouse-gated. Left as stubs. |
| **jt233 / jt239 / jt242 / jt243 / jt244** | CODE 11 — the genuine **runtime geo/area** functions (jt239 calls the live L63c0 walk loop). This is the real chain. |

All ~40 JT deps these call are already LIFTED (jt273/325/317/358/364/
201/198/351/316/1180 + jt195=l4db4).

## Done

- **jt244** (CODE 11+0x4, area-load transition) + its three locals
  **l011c_c11 / l0148_c11 / l068a_c11**, and the **jt195→l4db4** alias.
  jt244 loads the area block (jt325), then on success either re-seats
  the party at the entry cell + rebinds art (jt358/jt364, flag set) or
  re-inits the string table + cell arrays (l4db4 + l011c + l0148, flag
  clear). `unused` for now — its caller is the CODE 22 L0004 dispatcher
  (below), still unlifted, so the visible payoff is deferred. Build
  clean, boot unregressed.

- **jt233** (CODE 11+0x27e, cell wall/state encoder) — self-contained
  (zero unlifted locals; all JT deps lifted). Four switches: cmd<10
  sets a fresh cell (JT[3] on the type), cmd 10/11 edit (JT[3] on cmd →
  JT[1]/JT[3] on the type) packing the type nibble + the +16 direction
  field into *rec. All tables via jt1_extract/jt3_extract. `unused`.

- **l4d24_c11** (CODE 11+0x4d24) — the automap **region flood-fill**
  (recursive 4-dir, toroidal, marks connected cells while the shared
  edge's wall code <= threshold). dep l05ca=JT[293]. `unused`.
- **l49dc_c11** (CODE 11+0x49dc) — the automap **area info-panel
  painter**: 3 flood passes at tightening thresholds {13,5,1} (tightest
  class wins) -> tally cells into hist[0..2] -> paint the panel
  (jt1161 backdrop + jt1089 lines from the -11120/-11136/-11128/-11132
  string table, singular/plural suffixes). Drives l4d24_c11. `unused`.

- **jt239** (CODE 11+0x4846) — the runtime area-ENTER: layers up
  (l476e/l6256/jt108/jt112/l429c/jt148/jt449), seats the party
  (area[46..] -> -12288), maps it (jt218), repoints -12200 and paints
  the info panel (l49dc_c11), runs the L63c0 walk loop with jt235 as cb
  (result stamps rec[3]), writes the party position back, tears down.
  **The area-enter path is complete** (jt239 + its l4d24/l49dc locals).

## Remaining (leaf-first order)

1. **jt242** (CODE 11+0x589a, ~101, cell-edit committer) — JT[3] over 5
   unlifted painters: L5a06(77), L6136(44), L61c6(44) are small leaves;
   L5ee2(183), L5b0e(200) are larger. (`l61c6` in the port is a *different*
   segment — suffix `_c11`.) Lift the painters first, then jt242.
3. **jt243** (CODE 11+0xb26, ~800) — the big one; JT[3] dispatch + many locals.
4. **CODE 22 L0004** — the 21-arm area-command dispatcher (calls jt233/239/242/243/244 + jt281/282/286). Lifting this WIRES the whole cluster → first screenshot-validatable payoff. Find its JT entry (near jt316/318/321) and its reachability from the play loop first.

Each CODE 11 entry pulls in a handful of CODE-11-local painters
(L5a06, L5b0e, …) — size + alias-check them before each lift (the
l0004/l2aaa collisions above show CODE 11 offsets alias other
segments; match on (CODE, offset), suffix `_c11`).
