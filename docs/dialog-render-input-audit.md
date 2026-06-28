# Dialog / render / input stack audit (2026-06-27)

Goal: find the *code-level* gaps (true stubs, stand-ins, deferred commits)
behind the in-game-dialog breakage the user keeps hitting — so we fix at the
source instead of poking the running game. Verified against current `boot.c`
and the Mac disasm; not point-in-time guesswork.

Three observed symptoms, each traced to one root:

| Symptom (user) | Root cause | Class | Fix owner |
|---|---|---|---|
| text on **stone** instead of light-grey box (prompts, "Already using…", Trade prompt) | UI palette band not installed in-game | **CLUT** (not a stub) | #147 |
| can't **type a number** in Trade (jt891); roster keys do nothing | event-pump, runtime | **runtime** (not a stub) | new |
| arrowing the party list does a **huge redraw/recolour**; can't mouse-highlight | CLUT re-install + present-once + missing keystroke filter | **mixed** | #147 + #144 + jt138/jt139 |

---

## 1. CLUT — the #1 visual bug (NOT a missing leaf; a deferred commit)

The prompt-plate renderer itself is already correct: `jt1089` PaintRects the
text cell in the bg index when `(bg==15||fg==0)&&bg!=8` (commit 06cd0b5, the
`(bg<<4)|fg` colour-pair decode). The box *is* being drawn — in the **wrong
colour**, because the light-grey box index (7) isn't installed when in-game
dialogs paint.

Two places gate the UI palette band on `g_menu_state == 1`:

- `port_hud_text_clut` (boot.c:20164) — `if (g_menu_state != 1) return;` then
  installs `g_menu_pal` slots **1 and 6..15** (1 name / 7 grey-box / 11 cyan /
  12 red / 15 white). In the dungeon / inventory / trade / shop contexts
  `g_menu_state != 1`, so this is a no-op and indices 1/6..15 keep whatever
  **clut 129** (the 256-entry wall palette) left there → stone.
- `L4d98` (boot.c:64190) — the faithful resident-palette commit is
  `(void)0;` PORT-DEFERRED. The Mac runs `jt1068 + jt993(jt468(0),0) +
  jt1066 + jt1000(-17482)` (colour modes) / `jt996(0,0,0)` (B&W). **All four
  callees are lifted and ready** (jt1068 b59128, jt993, jt1066, jt1000 b63912);
  the *call* is deferred because running it globally recolours the ranges the
  port renderers own (corrupted Hall backdrop, dungeon frame breakage).

**Code-level fix (#147), two options:**
- (faithful) Re-enable the L4d98 resident commit, but scope it so the GLIB
  colour-range subsystem owns the 0..31 UI band and never overwrites the wall
  bands (32+) — i.e. `jt1000(-17482)` seeds the `-4188` text-palette template
  and `jt1006` remaps fills through it (`g_a5_1312` gate). This is the real
  pipeline and also fixes `jt1161` fill remap.
- (targeted bridge) Install the UI band (`g_menu_pal` slots 1, 6..15) at every
  in-game dialog entry, not only when `g_menu_state==1`. Cheapest path to a
  light-grey box now; superseded by the faithful version later.

Keystone leaf: **`jt1000` (boot.c:63912)** `jt406(&-4188, src, 16)` — sets the
text-palette template; **currently never called**. `jt1006` (b58311) already
reads `-4188`. Wiring `jt1000(-17482)` is the smallest faithful step.

---

## 2. Dialog keyboard (jt891 "can't type") — RUNTIME, not a leaf

`jt891` (boot.c:65015) is faithful: `jt1134()` flush → `key = jt60()` →
digit test `jt389` (b54231, body matches Mac `cmpib #48/#57`, correct) →
shifted-symbol remap. **No stub in its path.** The key never arrives, so the
bug is in the event pump:

    jt60 → jt1133 (spin while jt1118()==0) → jt1118 → l731e(3) → l725c →
    WaitNextEvent → kb_to_event → plat_kb_poll (BIOS Bconstat/Bconin) → l6dd0
    (sets -818 key / -820 pending)

Suspect: `jt1134()` at the top of the loop pumps `l725c(0x8140)` every
iteration; 0x8140 excludes keyDown (0x08) so it shouldn't *return* a key, but
if the pump drains/consumes the BIOS key before `jt60` runs, the key is lost.
**Needs instrumentation (log plat_kb_poll vs l6dd0 vs jt60 return), not a
lift.** The dungeon reads keys on a *separate* direct path (port_play_demo
~b15048) which is why movement works and dialogs don't.

---

## 3. True leaf stubs ON the dialog/list path (these DO need lifting)

| fn | boot.c | Mac sz | role | note |
|---|---|---|---|---|
| **jt138** | 52524 | 0x86 (C7) | list-item keystroke filter: loop `jt1118`/`jt1133`, arrows 338/339→`jt50`/`jt51` scroll, translate via JT[1133] | installed as DLItem method (b52712); scroll targets `l5ac2`/`l5ad8` are real |
| **jt139** | 52525 | 0x6e (C7) | same, plus stores the chosen key in `-12914` (ESC/`` ` ``) | the "Enter returns selection" variant |
| **jt140** | 21122 | 0xe4 (C7) | list column/row measure: divides text width by 4, walks the `-27928` party list via `jt1139` | list-layout helper |
| **l2062** | 30440 | 0x10 (C7=JT[174]) | sets dirty flags `-12911`/`-12912`=1 (commit-deferred-paint) | trivial 4-instr lift |

`jt50`/`jt51` (the page-scroll the filters call) are already real
(`l5ac2`/`l5ad8`, b19782/19786). So lifting jt138/jt139 closes keyboard nav for
list dialogs (roster, picker) without further deps.

---

## 4. Other true stubs (functionality gaps, NOT dialog-critical)

Real Mac bodies, currently `PROBE`-only. Listed largest-first; lift when their
subsystem comes up, not for the dialog work:

- **jt520** 0x3c0 (C14, →jt52) · **jt560** 0x29e (C17 char-gen — *harmless*,
  real path is l618c now) · **jt541** 0x224 (C14, record init: THAC0/AC via
  jt868) · **jt930** 0x156 (C12, leave-area cleanup) · jt1109 0x5e (C4) ·
  jt985 0x4e (C5) · jt1142 0x32 (C4) · jt68 0x26 (C6, yield/pump) · jt45 0x2e
  (C6) · jt1081 0x24 (C5).
- Tiny leaves (≤0x14): jt146, jt736, jt1152, jt1121, jt1137, jt512, jt859,
  jt920, jt956, jt1130.
- CODE-local `lXXXX` leaves (mostly editor / combat tail, off the dialog path):
  L4144 L10a0 L1176 L40b4 L1c92 L1cd2 L4f2c L4ff6 L15bc L157c l2aaa l2f24
  l329c l347a l0572 l0418 l035c l07e6 l0cb8 l0d86 l0e3e L0062 L038a L1e44
  L2d7e L06d6 L0bc6 L0df2 L1374.

Note: many functions still carry a `PROBE("…")` marker but have a **faithful
body** (e.g. jt389/jt408/jt420 ctype, jt527, l5ac2/l5ad8). They are NOT stubs —
the marker is just left in. Don't re-lift those; the 53 above are the genuine
empty/return-0 stubs.

---

## Recommended order

1. **#147 CLUT** — wire `jt1000(-17482)` + the targeted UI-band install, fixes
   every "stone box" at once (prompts, messages, Trade, roster recolour). Biggest
   visible win, no new reverse-engineering (all callees lifted).
2. **jt138/jt139 + l2062** — small, self-contained list-dialog keystroke
   filters + the dirty-flag leaf; restores keyboard nav in list dialogs.
3. **jt891 keyboard** — runtime instrumentation pass (separate from lifting).
4. **#144 present-once** — stops the full-screen redraw on every list arrow.
