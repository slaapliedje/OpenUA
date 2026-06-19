# Port roadmap — the whole architecture + what's next

Synthesised from the corrected `tools/jt_progress.py` audit (alias-safe, 2026-06-19)
and the per-segment wall docs (`code07/13/14/16/22-wall.md`). Overall: **821 done
/ 138 stub / 246 missing** of 1205 called JT entries — but raw counts mislead, so
this groups every segment by ARCHITECTURAL LAYER and says what it means.

## 1. FOUNDATION — the libraries everything sits on  ✅ in place

| CODE | role | done/pend | verdict |
|-----:|------|----------:|---------|
| 1 | boot / A5 init / entry | 8 / 2 | done |
| 3 | Mac Toolbox shim (QuickDraw/Dialog/Event/Menu) | 90 / 26 | done; pending = trap-glue + per-feature widgets |
| 4 | display low-level (QuickDraw/blit math) | 54 / 63 | **SUPERSEDED by the VIDEL HAL**; pending is demand-driven, not gaps |
| 5 | core runtime lib (format/error, called by ALL) | 78 / 51 | pending demand-driven |
| 6 | file-group + GLIB art + Resource Manager | 116 / 10 | done (#127) |
| 7 | DLItem widget toolkit (jt169 list dialog, buttons) | 69 / 28 | done; pending = per-feature widget leaves (code07-wall) |
| 8 | input/menu/file-prefix lib | 29 / 18 | foundation met; pending demand-driven |

**Proof it's complete:** the entire pre-game UI runs on it. The big pending counts
(CODE 4 = 63, CODE 5 = 51) are display/runtime paths the working code never calls
— **demand-driven, not foundation gaps.** Don't grind these; lift on demand.

## 2. FRONT DOOR — boot → ready to play  ✅ done

| CODE | role | done/pend | verdict |
|-----:|------|----------:|---------|
| 22 | main menu + **design-select picker** (jt314/l494e) | 43 / 8 | picker done + verified; the 8 pending are the design EDITOR |
| 17 | character generation | 17 / 3 | done (#135); 3 leaf stubs |
| 12 | Training Hall + roster (the menu half) | (part) | done (#141) |
| 15 | save/load **serializer** | 11 / 8 | party round-trip done (#141); slot pickers pending |

Title → credits → main menu → Select-a-Design → char-gen → Training Hall →
Save/Load all work end-to-end.

## 3. IN-GAME — the frontier  🚧 (all gated — see §5)

### Combat cluster
| CODE | role | done/pend | note |
|-----:|------|----------:|------|
| 18 | combat EFFECTS engine (poison/cure/damage payloads) | **165 / 6** | **almost DONE** — the hard payloads are lifted |
| 13 | combat MAIN LOOP + per-turn tree | 20/2 JT + **45 lXXXX locals** | **the keystone** (l076e spine, code13-wall) |
| 14 | combat-FIELD render (actor sprites, HP, targeting) | 25 / 19 | gated on 13 (code14-wall) |
| 16 | combat HANDLER tier (82 effect handlers) | 33 / 82 | gated on 13 (code16-wall) |

### Town / camp / dungeon-interaction cluster
| CODE | role | done/pend | note |
|-----:|------|----------:|------|
| 19 | char sheet + **REST/CAMP** (memorize, heal, "Stop Resting?") | 24 / 11 | the camp screen |
| 21 | **spell memorization** + scroll scribing | 1 / 8 | spell prep |
| 9 | inventory + spellbook viewer | 2 / 3 | |
| 10 | event-picture / portrait display (PIC/SPRIT/CPIC) | 4 / 8 | partly #125 |
| 20 | encounter / combat narration ("A battle begins…") | 10 / 4 | the l709e event TEXT |
| — | **SHOP/merchant** (jt893 dispatcher + jt189/190 + jt923) | missing | [[shop-subsystem-scope]] |

## 4. EDITOR — the authoring tool  ⏸ deferred (not play)

| CODE | role | done/pend |
|-----:|------|----------:|
| 2 | event / zone / map-step editing ("Step Event", "Rest in Zone") | 5 / 9 |
| 11 | 3D-MAP (GEO) editing + save ("Save3DMap") | 6 / 6 |
| 22 | editor record panels (jt281/282/286) | (part) |

## 5. THE GATE — why the whole frontier is one dependency

Every §3 feature is downstream of the **dungeon being interactively playable**:

```
design loaded  →  party walks the dungeon (#124: l63c0 / jt240 walk loop)
               →  steps on a map cell
               →  l709e dispatches the cell's EVENT  (jt947 = l709e, lifted)
               →  combat / shop / vault / special-text / stairs
```

Until the walk + event dispatch run interactively, combat, shops, rest, spells,
and event pictures are all **breadth-first, untestable** lifts. The single
highest-leverage unlock is the play loop, not any one feature.

## 6. What to work on next (priority order)

1. **Make the dungeon interactively playable** — finish the walk loop (#124,
   `l63c0`/`jt240`) and wire `l709e` event dispatch to fire on cell entry. This
   is the keystone: it makes EVERYTHING in §3 testable. Chart it next.
2. **The combat SPINE** (CODE 13: `l4f22`→`l0434`→`l076e`→`l102a`→`l0116`,
   code13-wall §1). Surprising leverage: **CODE 18 (the effects engine) is
   already 165/6**, so combat is "engine done, orchestration + field-render
   missing." The spine unblocks the 82 CODE 16 handlers + CODE 14 field render.
3. **Non-combat dungeon features**, each testable once #1 lands — pick by
   visibility: rest/camp (CODE 19), spell memorization (CODE 21), shop
   (CODE 19/7), inventory (CODE 9), event pictures (CODE 10).
4. **Editor** (CODE 2/11/22) — a separate authoring track, defer until play is solid.

**Bottom line:** the foundation and front door are done. The port can boot,
build a party, pick a design, save/load. The next real work is the **play loop**
(#1) — it's the one thing standing between "ready to adventure" and actually
adventuring, and it ungates the entire in-game half of the program.
