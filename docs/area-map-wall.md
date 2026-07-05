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

## Remaining (leaf-first order)

1. **jt233** (CODE 11+0x27e, ~266) — calls jt358/318/361/135/356, JT[1] switch.
2. **jt242** (CODE 11+0x589a, ~101) — JT[3] switch over local painters L5a06/L5b0e/L5ee2/L6136/L61c6.
3. **jt239** (CODE 11+0x4846, ~128) — the play-view layer rebuild (L429c/L6256/L63c0/jt235/jt218…), on the walk path.
4. **jt243** (CODE 11+0xb26, ~800) — the big one; JT[3] dispatch + many locals (L4144/L16ae/L23de/jt321/jt305/jt346…).
5. **CODE 22 L0004** — the 21-arm area-command dispatcher (calls jt233/239/242/243/244 + jt281/282/286). Lifting this WIRES the whole cluster → first screenshot-validatable payoff. Find its JT entry (near jt316/318/321) and its reachability from the play loop first.

Each CODE 11 entry pulls in a handful of CODE-11-local painters
(L5a06, L5b0e, …) — size + alias-check them before each lift (the
l0004/l2aaa collisions above show CODE 11 offsets alias other
segments; match on (CODE, offset), suffix `_c11`).
