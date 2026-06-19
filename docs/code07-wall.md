# CODE 7 wall — the DLItem widget library + dialog managers

**Status: foundationally DONE.** CODE 7 is the shared UI toolkit — the list
dialog, prompt bars, button/widget methods, and the dialog-item (DLItem)
system every screen builds on. Update the status columns in the same commit as
each lift; regenerate counts with `python3 tools/seg_audit.py 7` and
`python3 tools/jt_progress.py` (the auto-alias classifier).

**Snapshot (auto-alias corrected):** 97 called JT entries, **71 done / 26
pending** (all 1-call leaves). The load-bearing core is lifted:

- `jt169` — the LIST DIALOG (CODE 7+0x3600) — design-select, Delete browser,
  save/load pickers all run on it. **LIFTED.**
- `jt178` prompt/option bar · `jt137`/`jt151` command-button widgets ·
  `jt179`/`jt155` slot-index table · `jt165` list lookup · `jt176` frame paint ·
  `jt182` slot picker · `jt181` "press a key" · `jt159` Y/N confirm · `jt147`
  list free · `jt158`/`jt166`/`jt452` DLItem install + 50 more. **LIFTED.**

## The 26 pending — per-FEATURE widget leaves, NOT a CODE 7 gap

Every pending entry is called **once, from a specific subsystem** (the caller
segment is in brackets). They live in CODE 7 because it's the widget library,
but each belongs to the feature that calls it — **lift them WITH that feature,
not as a standalone CODE 7 task.** Grouped by owner:

### List-dialog internals (the only truly CODE-7-native pending)
| JT | addr | what (disasm) | status |
|----|------|---------------|--------|
| jt138 / jt139 / jt222 | 0x1914 / 0x199c / 0x09ea | list SCROLL/paging widgets (jt50/jt51 page up-down + jt1118/jt1133 key) — only exercised when a list exceeds the visible rows | stub / — |
| jt140 | 0x1e58 | roster-list DLItem (jt1139 + jt937 HUD roster draw) | stub |
| jt143 / jt144 / jt225 | 0x3564 / 0x35d4 / 0x0b8e | DLItem enable/dispatch leaves (jt444 / jt3) | stub / — |

### Shop / merchant dialogs  [CODE 19]
| JT | addr | what | status |
|----|------|------|--------|
| jt189 | 0x43a4 (190ln) | BUY/SELL haggle ("I'll give you %ld", "Will you sell") | — |
| jt190 | 0x4644 (142ln) | shop charge / identify ("Fool! You must…", "For 20 platinum") | — |

### Inventory  [CODE 12 / 9]
| JT | addr | what | status |
|----|------|------|--------|
| jt186 | 0x3aba | pack overflow ("Too many Bundles!", "OverLoaded"; jt903/jt887/jt889) | stub |
| jt230 | 0x0110 | inventory widget leaf | — |

### Combat / encounter  [CODE 13 / 20 / 14]
| JT | addr | what | status |
|----|------|------|--------|
| jt154 | 0x169e | "Press [key] to continue" combat prompt (jt447/jt452 buttons) | stub |
| jt183 / jt185 | 0x3e68 (219ln) / 0x417a (157ln) | encounter/combat status widgets (jt926/jt936/jt937/jt583) | — |
| jt206 | 0x5d1c | combat widget (96ln) | — |
| jt187 | 0x4910 | encounter list alloc (jt479/jt477) | stub |
| jt172 | 0x2cf6 | combat-field widget (jt142/jt452) | — |

### Art / picture, editor, Hall  [CODE 8 / 10 / 2 / 12 / 6]
| JT | addr | owner | what | status |
|----|------|-------|------|--------|
| jt203 | 0x6148 | C8 | picture DLItem (jt1004/jt468/jt459) | — |
| jt220 | 0x6ea2 | C8 | background blit ("back1", jt110/jt113/jt115) | stub |
| jt208 | 0x71f4 | C10 | picture widget leaf | — |
| jt227 / jt228 | 0x0040 / 0x0088 | C2 | design-editor widgets | — |
| jt146 / jt170 / jt177 | 0x1586 / 0x15ae / 0x15bc | C12 | Training Hall widget leaves | stub |
| jt136 | 0x11a6 | C6 | fill leaf (jt399) | — |

## Verdict

CODE 7 is **not a foundational gap** — the dialog/widget toolkit the whole UI
needs is in place (proven: the menus, design picker, Hall, char-gen, save/load
all run on `jt169`/`jt178`/the button widgets). The 26 pending are downstream
leaves that come for free when their owning feature is lifted:
- **Shop** (jt189/jt190), **inventory** (jt186/jt230) — biggest concrete UI
  features still missing.
- The rest are combat/encounter/editor widgets (lift with those subsystems).
- Only the list **scroll/paging** (jt138/jt139/jt222) is arguably standalone —
  needed if a pick-list ever exceeds the visible rows (the current designs/
  roster fit, so it isn't exercised).
