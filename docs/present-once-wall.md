# #144 — Present once per logical screen (off-screen compose) — SCOPE

Status: scoped 2026-06-28. **Premise CORRECTED same day** — see the box below.
Owner: display HAL + engine present sites.
Companion: [docs/dialog-render-input-audit.md](dialog-render-input-audit.md) §2b/§5
(the roster arrow-nav garbage that motivated this), and #143 (cursor smear).

> **CORRECTION (2026-06-28, after starting Phase 1).** Two findings changed the
> scope:
> 1. **The present MODEL is already correct.** `videl_present_rect` collapses to
>    `videl_present()` (a *full* chunky blit), and the triple-buffer rotation is
>    provably tear-free + VBL-coalesced (the displayed buffer is always the
>    latest complete frame). So buffers do NOT diverge — the "HAL copy-commit"
>    (old Option A) solves a non-problem. The dungeon's 2× present is vestigial.
> 2. **The roster garbage was NOT a present bug at all — it was an unclean
>    crash** (shape-7 page-switch item calling an invalid callback; see
>    audit §2b). Fixed directly.
>
> So #144's *real* remaining value is modest **present-once cleanup**, not a HAL
> rework: (a) DONE — removed l0aae's redundant present (jt453 is the faithful
> single commit); (b) DONE — the "vestigial dungeon double-present
> (boot.c:14941)" lived in `draw_party_panel`, which was DEAD CODE (no callers;
> superseded by the faithful HUD jt937/jt938, #113) — deleted the whole function.
> (Its `for(;;) qd_present()` siblings in `port_l6234_verify` are `#ifdef`-gated
> debug holds, not production.) (c) OPTIONAL — engine re-faithfulization via
> jt1153/jt1146 (Option B) for structural fidelity. The HAL copy-commit (Option
> A) is **shelved** — the model is sound as-is. With (a)+(b) done, #144's
> actionable scope is effectively closed; only the optional (c) pass remains.

## 1. Symptom inventory (what "present-too-often" looks like)

- **Roster arrow-nav garbage band** (the headline): on a Hall selection change,
  ~4 cyan-bordered RGB-static boxes appear over the top roster rows. PROVEN: the
  chunky is 100% clean and all three 16bpp buffers are clean at present time, so
  it is a display-layer artifact on the nav's back-to-back presents — not a lift
  bug. (Mechanism not fully pinned; this is the primary verification target.)
- **Dungeon HUD double-present hack**: boot.c:14941 `qd_present()` with the
  comment *"both flip buffers (jt312 rect-presents the viewport)"* — HUD text is
  drawn once but must be pushed into multiple flip buffers or it flickers as the
  display rotates buffers.
- **First Hall flash** (FIXED this session, 976c9c9): the bare stone backdrop was
  presented before l02dc/l0aae layered the roster + menu on top.
- General: every incremental compose step that calls `qd_present()` flips the
  screen, so a multi-step screen can briefly show half-composed states.

## 2. The faithful Mac model (CONFIRMED from disasm)

The Mac runs a **two-page double-buffer with a COPY commit**, not a flip:

- `-2570[]` — a 2-entry table of 108-byte page records; each holds a GWorld/
  pixmap pointer at offset +2.
- `-2354` — the current **draw (off) page** index (0/1).
- `-2347` — the jt1135 scale flag (selects the copy path in jt1146).
- **`jt1153` (JT[1153], CODE 4+0x5d34)** — page-select bookkeeping: sets the draw
  page index/`-3076` draw base. Called ~16× (every screen's draw prelude;
  e.g. l2d3e Phase 1 does `jt1153(1); l2c60(0); read-event`).
- **`jt1146` (JT[1146], CODE 4+0x5c82)** — the **commit**: computes off-page =
  `-2570[-2354]` and on-page = `-2570[1 - -2354]`, then `jsr JT[406]` to **copy
  off → on** (L050a supplies the byte count). The on-screen page therefore always
  holds the *complete* composed frame after the copy. Called ~12× — **once per
  logical screen / loop iteration** (CODE 5/6/8/11/...).

Engine pattern per screen: `jt1153` (pick off page) → draw everything to the off
page (QuickDraw, l02dc, l0aae, jt76, …) → **`jt1146` (copy off→on, ONE commit)**.
No intermediate flips. That is "present once per logical screen."

## 3. The port's divergence

- **One** chunky surface (`g_screen_port` / `qd_screen_pixels`) = the off page.
- `videl_present()` (platform/display_videl.c): LUT-blits the full chunky into a
  **triple-buffer** slot `g_screen[g_draw]`, publishes it, and the VBL handler
  **flips** the VIDEL base to it (#130 — added for tear-free, low-lag output).
- `jt1146` was collapsed to `qd_present()` (boot.c:18682); `jt1153` to page
  bookkeeping that is a no-op for the single chunky (-3076 = constant port base).
- **72 `qd_present()` call sites** in boot.c, but only **2 `jt1146()`** calls.
  The faithful "draw → single commit" structure was replaced by ad-hoc presents
  sprinkled through each lift.

Net: instead of one COPY to a stable on-page, the port FLIPS among 3 buffers and
presents many times per screen. Buffers diverge (a one-shot draw lands in one
slot; the rotation shows another), which is exactly the dungeon HUD hack and the
root class behind the nav-time artifacts.

## 4. Root cause

Triple-buffer **flip-rotation** + **partial (rect) presents** + **many presents
per screen** ⇒ the displayed buffer is not guaranteed to be the latest fully
composed frame. The Mac never has this because `jt1146` COPIES the whole off page
to a single on page.

## 5. Fix options

**A. HAL copy-commit (faithful to jt1146) — RECOMMENDED first.**
Make `videl_present()` always produce a complete on-screen frame, idempotently:
keep the chunky as the single off page; on present, LUT-blit the **full** chunky
to the displayed page (or to the back page then flip, keeping both pages a
complete copy of the chunky). Result: every `qd_present()` is an idempotent full
commit (= jt1146 semantics), so the 72 ad-hoc presents become harmless and the
dungeon double-present hack retires. Must preserve #130's tear-free/low-lag
property (blit the off/back page, flip at vblank — never tear the scanned page).
- Sub-decision: 2-page copy-flip (blit chunky→back, flip, blit chunky→new back so
  both stay complete = 2 blits/present) **vs** keep 3 buffers but guarantee each
  present writes a complete buffer and rect-presents update the displayed page in
  place during vblank. Pick during implementation; measure against #130.

**B. Engine present-once (structurally faithful) — follow-up.**
Restore the real `jt1153`/`jt1146` page model in the engine: route screen commits
through `jt1146`, delete the intermediate `qd_present()` calls per screen. Bigger
(per-screen audit of the 72 sites) but matches the Mac structure exactly. Do this
incrementally AFTER A makes it safe (A means a missed/extra present can't corrupt
the display, so B can proceed screen-by-screen without regressions).

**C. (rejected as the primary fix)** Patch each symptom screen individually —
churns the same code repeatedly and never addresses the class.

## 6. Phased plan

0. **Bench/verify harness ready** — headless arrow injection (`FRUA_NO_CONOUT=1`)
   + `dbg_file_num` already landed (37e05bc). Repro: roster nav garbage; dungeon
   HUD; menu. Capture before/after screenshots for each.
1. **Phase 1 — HAL copy-commit (Option A).** Rework `videl_present` /
   `videl_present_rect` so the displayed frame is always the complete chunky,
   tear-free. Verify: (a) roster arrow-nav band gone, (b) dungeon HUD no longer
   needs the 2× present (remove boot.c:14941 hack + confirm), (c) no input-lag
   regression vs #130 (eyeball the walk loop), (d) menu/char-gen still clean.
2. **Phase 2 — retire the workarounds.** Remove the dungeon double-present and any
   "present twice to fill buffers" comments; drop the redundant `jt453` present on
   the non-modal Hall menu path.
3. **Phase 3 — engine faithfulness (Option B), incremental.** Reinstate
   `jt1153`/`jt1146` as the commit per screen; convert the ad-hoc `qd_present()`
   clusters to a single per-screen `jt1146`. One screen per commit (Hall, menu,
   char-gen, dungeon, modals/jt453, shop). Each verified before the next.

## 7. Files

- `platform/display_videl.c` — `videl_present`, `videl_present_rect`,
  `videl_lut_blit`, the `g_screen[]`/`g_draw`/`g_next`/`g_disp` triple-buffer, the
  VBL handler. (Phase 1/2.)
- `platform/display_vdi.c` / TT-shifter backend (if present) — mirror the change.
- `compat/quickdraw.c` — `qd_present` / `qd_present_rect` hooks (no change
  expected; they just call the HAL hook).
- `src/engine/boot.c` — `jt1146` (18682), `jt1153` (18601), the 72 `qd_present`
  sites, jt312/dungeon present (14941), l0aae (21543), jt453 (19741). (Phase 2/3.)

## 8. Risks / watch-items

- **#130 input lag / tearing** — the triple-buffer exists for a reason; the
  copy-commit must stay vblank-synchronised. Re-measure walk-loop responsiveness.
- **Rect presents** — jt312's viewport-only present is a real perf win; don't
  force full presents in the walk loop without checking frame time.
- **The roster garbage mechanism is not fully pinned** — Option A is the most
  likely fix, but if the band persists after Phase 1 it points at the VBL cursor
  save/restore across flips (#143 territory), needing a separate focused pass.
- **Cursor** — the VBL cursor composites onto the displayed page; any page-model
  change must keep `vbl_cursor_update`'s save-under coherent (see #143).
