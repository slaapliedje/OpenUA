# Save / Load subsystem — findings + worklist

Goal: a faithful round-trip of a full FRUA save — the adventuring **party** *and*
the **design-state** block — through the real CODE-15 serializer, with the A–J
slot pickers and the load-confirm modal, retiring the `port_load_savgame`
stand-in.

> ⚠️ **THIS HEADER WAS STALE (corrected 2026-08-02).** It said "party-only
> round-trip already works (#141); the design-state block is the main gap",
> and `docs/gap-analysis.md` row C repeated it — while the TABLE BELOW already
> recorded `jt585` as **LIFTED + Hatari-verified (A-J pick -> 10284B save)**
> and `jt580` as **LIFTED (party block + design-state pad)**. The stale header
> cost a wrong "save/load is the top remaining gap" call in an audit.
> **Re-verified live on the Falcon:** ENCAMP -> SAVE -> the A-J picker renders
> and slot J wrote `HEIRS.DSN/SavGamJ.csv`, 10 284 bytes, 21% non-zero and 99%
> byte-identical in shape to an independently-made slot B save. Save/load
> WORKS. What remained after that was boot auto-load and confirmation on the
> non-Falcon ports — both closed below, on 2026-08-02.
>
> ✅ **ALL FIVE PORTS CONFIRMED (2026-08-02).** Load-then-play verified on
> Falcon 030 (TOS 4.04), TT030 (EmuTOS 1.3.0), ST 8 MHz (TOS 2.06, ST-Low),
> Amiga AGA and Amiga ECS/68000: an empty Hall loads slot B and all six
> characters come back with identical AC/HP, then BEGIN ADVENTURING enters the
> authored dungeon. The **write** side was re-confirmed on ECS too — camp →
> SAVE → slot C produced a 10 284-byte `SavGamC.csv` differing from the
> fixture in 46 bytes (position, facing, clock, per-character state).
>
> ✅ **THE SAVE PATH NOW MATCHES SSI'S (2026-08-03).** Saves move from
> `<design>.DSN\SavGam<c>.csv` to `<design>.DSN\SAVE\SAVGAM<c>.CSV` — the
> location and spelling DOS FRUA 1.2 actually uses. Not inferred: the DOS
> release was driven headlessly (`tools/dosdrive.sh`) through Training Hall →
> SAVE CURRENT GAME → slot F, and it wrote
>
>     HEIRS.DSN\SAVE\SAVGAMF.CSV   10 285 bytes
>     HEIRS.DSN\SAVE\VAULTF.DAT       916 bytes
>
> ★ **A pair of DOS-written `SAVGAMA.CSV` / `VAULTA.DAT` sitting at a design
> ROOT in the staged gamedata is what made the flat layout look confirmed.**
> They had been moved there by hand. Watching the program write a file beats
> reading where a file happens to be, and this cost a wrong answer once.
>
> Verified live on the Falcon: Hall → SAVE → slot F wrote
> `HEIRS.DSN/SAVE/SAVGAMF.CSV` (the `SAVE` folder created on demand — GEMDOS
> Fcreate will not make it and neither will AmigaDOS Open), the load picker
> then enumerated **only** F, loading it restored the party, and
> `autoload.dat` resumed straight into the caravan event from the new path.
>
> ✅ **AND DOS SAVES LOAD AS THEY ARE (2026-08-03).** A DOS-written pair —
> `SAVGAMA.CSV` 10 285 bytes + `VAULTA.DAT` 916 bytes — dropped into
> `HEIRS.DSN\SAVE\` loaded through PLAY → LOAD SAVED GAME → A with all six
> characters intact (BARBARUS, LADY ILLIS, MALTIER, NIVLOC, CLARANA,
> STRANILLA), and saving to B then wrote both `SAVGAMB.CSV` and `VaultB.DAT`
> beside them. The one-byte length difference does not obstruct it: the
> deserializer is field-driven, not length-driven.
>
> ★ **`VAULT<c>.DAT` WAS NEVER AN UNKNOWN FORMAT.** It is lifted as `jt74` /
> `jt75`, and the port had been writing it all along — into the flat gamedata
> folder, which is why nobody noticed. It now follows the slot into
> `<design>.DSN\SAVE\`. Spec: `docs/vault-format.md`. The Mac pads the file
> to 200 item records (3616 B) and DOS to 50 (916 B), but the reader takes its
> count from the header and never touches the pad.
>
> ✅ **SAVED CHARACTERS TOO (2026-08-03).** They move from slot-numbered
> `CHAR0000.CHR` in the flat folder to `<design>.DSN\SAVE\<NAME>.CCH` — the
> name put through the faithful `jt130` (strip the 17 hostile characters at A5
> -31268, truncate to 8, uppercase), which is exactly DOS's 8.3 rule: LADY
> ILLIS → `LADYILLI.CCH`, STRANILLA → `STRANILL.CCH`. The list still shows the
> FULL name, because that comes from rec[96] inside the file.
>
> **The two files are the same format.** A DOS `BARBARUS.CCH` and our
> `CHAR0000.CHR` are both 506 bytes with the name at offset 96 — so this was
> only ever a naming and location difference.
>
> Proven in BOTH directions: a DOS-authored `BARBARUS.CCH` dropped into
> `HEIRS.DSN\SAVE\` appears in Add A Character and joins the party at the
> AC/HP the DOS release shows for it (-3 / 62); and after our engine had
> rewritten that file, **DOS FRUA still lists it**.
>
> Existing installs migrate automatically: when the SAVE folder holds no
> `.CCH`, the loader falls back to the flat `CHAR*.CHR` and immediately writes
> the roster out under the new names. The old files are left in place rather
> than deleted — a failed migration that also destroyed the source would be
> unrecoverable.
>
> ★ **There is deliberately NO sweep of the SAVE folder.** Under the old scheme
> `save_roster` could delete every `CHAR*.CHR` it did not write, because it
> owned that namespace. It does not own this one: characters authored by DOS or
> the Mac live in the same folder, and "delete what we did not write" would
> destroy them. Each pool slot now remembers the file it owns and drops only
> that one on a rename or a delete.
>
> **Still divergent:** the 10 285th byte of the slot file, and nothing else.
>
> Old saves at the design root become invisible. That was the maintainer's
> call: *"I can always move the files there if I want."*

> The drive that makes this portable is **keyboard-only** — `p` → `l` → slot
> letter → `b`, no coordinates — so one script runs on every machine. Clicks
> position the pointer on the slot pickers but do not commit there.
>
> ✅ **BOOT AUTO-LOAD SHIPPED (2026-08-02) — and the Mac has none.** Before
> building it I checked three ways: the whole binary holds exactly **three
> `SavGam` string references and all three are in CODE 15's INTERACTIVE
> save/load** (`jt582`'s picker, `jt585`'s save), so no boot path names a slot;
> the port's own boot auto-load (`port_load_savgame`) was retired ON PURPOSE in
> 2026-07 under direction (B) of `docs/play-entry-wall.md`; and `start.dat`'s
> second field — which `jt128`'s comment calls "the resume flag" — is really
> `-18476`, the packed area (mode, kind) nibble pair that `jt275` writes and
> `l4810` reads. The comment guessed. So the entry that sat here as "the one
> save/load piece still missing" was a leftover from before direction (B), not
> a faithfulness gap.
>
> What ships is therefore a **port convenience, opt-in, off by default**: one
> byte — the slot letter — in `autoload.dat` beside the game data. It resolves
> against the CURRENT design (so it follows `start.dat`), fires at most once
> per run, and reuses `l143e` + `jt582`'s position-restore tail verbatim, which
> is why it resumes at the SAVED cell. `ua_main` skips the main menu for the
> first pass only when the slot actually opens, so a dud configuration leaves
> the faithful boot untouched. Verified zero-key into the dungeon on the Falcon
> and on Amiga AGA; a nonexistent slot falls back to the menu and logs
> `autoload: no such slot`.
>
> ✅ **CAMP SAVE vs CAMP LOAD — SETTLED AGAINST DOS FRUA 1.2 (2026-08-03).**
> Reported from real hardware: camp SAVE -> "Exit Play? YES" dropped back into
> the dungeon, and only camp LOAD got you out. The SAVE half was a real bug
> (see the l4cda note in boot.c). The LOAD half is NOT: driven end to end in
> SSI's own DOS build — roll a fighter, Begin Adventuring, ENCAMP -> SAVE ->
> slot A -> "EXIT PLAY?" -> NO, then ENCAMP -> LOAD -> slot A — **DOS lands in
> the TRAINING HALL with the loaded party**, exactly as the port does. So
> "Load should drop you into the dungeon" is not what the original does, and
> nothing needs changing there. From the Hall it is one BEGIN ADVENTURING.
>
> Two side observations from the same run, both worth acting on eventually:
>
>   - **DOS keeps saves in `<design>.DSN\SAVE\`** — `SAVGAMA.CSV` and the
>     character `.CCH` files all land in that subdirectory. The port flattens
>     them into the design folder (`savgam_path`), so **saves do not
>     interchange between DOS FRUA and the port in either direction**. The Mac
>     built the same `<design>:SAVE:` path; flattening was the port's choice.
>   - **DOS writes `VAULTA.DAT` beside `SAVGAMA.CSV`** — a PER-SLOT vault file
>     the port does not write at all. Relevant to the vault event (l3a32).
>   - **DOS's save is 10,285 bytes; ours is 10,284** — one byte SHORT. The
>     10 284 figure quoted all through these notes came from the Mac side.
>     Dropping a port-written `SAVGAMB.CSV` into DOS's `SAVE\` folder and
>     opening the camp picker lists only slot A: **DOS will not enumerate our
>     save.** Supplying a `VAULTB.DAT` alongside does not change that, so the
>     vault file is not the gate — it is the save itself. 1360 of the bytes
>     differ, which is mostly party content, but the LENGTH is the cheap
>     suspect and the first thing to check if cross-loading is ever wanted.
>
> The "Game not saved, load anyway?" confirm did NOT appear on that camp LOAD,
> because the save had just happened — which matches the port's `-27946` gate
> exactly. A small but real parity signal.
>
> ★ **Two traps it walked into, both worth knowing.** (1) Skipping `jt315`
> also skips `load_menu_ui`, and then EVERY scrap of HUD text is invisible —
> roster, coords, clock, button labels — because `port_hud_text_clut` bails on
> `g_menu_state != 1` and the dungeon's clut 129 leaves the text indices
> grey-on-grey. Chrome and the 3D view render perfectly, so it reads as a font
> bug. `frua_areatest_entry` already had the same call for the same reason.
> (2) With autoload armed the harness marker **`menu: modal up` never fires** —
> both menus are skipped. Wait for `autoload: resumed` instead, or the driver's
> `start` will sit there until it times out.

Layer: the serializer core is **CODE 15** (jt577–jt587). Char-gen review/finalize
(jt570–jt573, CODE 17) is a *separate* concern and **not** part of save/load.

## What renders / runs today (traced, confirmed)

Two live entry points converge on the same CODE-15 core:
- **Camp menu (faithful):** `jt957` (camp dispatcher, boot.c:54544) case 6 →
  `jt585()` save (boot.c:54636) + `jt159` confirm; case 5 → design-reload load.
- **Training-Hall menu (stand-in):** `l1142` (boot.c:50848) →
  `port_save_game()` / `port_load_game()` (boot.c:28892/28900) — these **bypass
  the A–J picker and hardcode slot A**.

### Status table

| Fn | jumptable | Status | boot.c / asm | Role |
|----|-----------|--------|--------------|------|
| `jt585` | CODE15+0x1a24 | **LIFTED + Hatari-verified** (A–J pick → 10284B save) | 29955 | "Save Which Game" A–J picker; stamps position into player rec; `l00e0(fn,jt580)` |
| `jt582` | CODE15+0x153e | **LIFTED + Hatari-verified** (A–J pick → party restore) | 30091 | "Load Which Game" picker; flat-dir glob (jt990/jt991) → `l143e`→jt579 → restore position |
| `jt580` | CODE15+0x182c | **LIFTED** (party block + design-state pad, asm L19ca) | 28806 | Write player rec + position/state + count + per-member + pad-to-10284 |
| `jt579` | CODE15+0x124c | **LIFTED** | 28793 | Read mirror of jt580; rebuilds party `-27928` |
| `jt578` | CODE15+0x0934 | **LIFTED + LIVE** (the saved-character roster writer) | 28936 | Write one 398-byte record + inventory items + container sub-items + spells |
| `jt577` | CODE15+0x03fe | **LIFTED + LIVE** (the saved-character roster reader; round-trip self-tests PASS) | 29118 | Read mirror of jt578; **rebuilds** both chains from pool nodes; container capacity guard (jt903, 120-slot cap) |
| `jt584` | CODE15 (chargen) | **LIFTED** | 29198 | Save one character to a named `.cch` ("Update %s?" via jt159) |
| `l00e0` | CODE15+0x00e0 | **LIFTED** (port-adapted) | 28845 | Write opener: FSDelete+Create('FRUA','SAVE')+FSOpen → serializer cb |
| `l143e` | CODE15+0x143e | **LIFTED** (port-adapted) | 30044 | Read opener: FSOpen → jt579 |
| `l0ce0_c15` | CODE15+0x0ce0 | **LIFTED** | 28914 | Endian-swap the record's multi-byte fields native↔little-endian |
| `jt990`/`jt991` | CODE5+0x1b76/0x1cb6 | **LIFTED** (GEMDOS Fsfirst/Fsnext) | 23092/23112 | SAVE-dir enumeration for the picker |
| `jt159` | CODE7+0x16ea | **LIFTED** | 28594 | Yes/No confirm modal (via jt182) |
| `l005a` | CODE15+0x005a | **LIFTED** (always true) | ~28685 | Mac "insert save disk" — HD always present |
| `jt412` | CODE3+0x3888 | **LIFTED** | 42265 | GetFPos/tell — used by the design-state pad math (asm only today) |
| `jt581` | CODE15+0x1c76 | **LIFTED** | 23174 | `jt147(&head);jt147(&tail)` list-splice (Mac jt579 uses it) |
| `jt587` | CODE15+0x08e8 | **STUB — keep (port reimplemented)** | 23176 | Mac: jt399(dest,398,0) + (if -22733==1) `l08ba_c15(name,dest)` + jt21 + jt910 — a file-based "reset+reload a party slot from its .cch". The port replaced this with the in-memory jt590/cg_pool party splice (boot.c:27872), so the lone caller's `jt587(fresh_slot, matched)` is **vestigial** — faithfully lifting it would zero `matched` + open a record as a filename. Do NOT lift; the jt477/jt165/jt587 trio is dead next to jt590 |
| `l08ba_c15` | CODE15+0x08ba | **LIFTED** | 29412 | Load a `.cch` named `name` into `rec` (jt384 + l_cch_read; replaces Mac L0006). jt587's case-1 loader |
| `port_save_game`/`port_load_game` | — | **STAND-IN driver** | 28892/28900 | Bypass picker; drive jt580/jt579 over fixed slot A |
| `port_load_savgame` | — | **STAND-IN** | 15810 | Non-faithful BasiliskII `SAVGAMA.CSV` party scanner (bring-up roster) |

> CORRECTION to prior notes: **jt570/jt571/jt572/jt573 are char-gen action
> procs/finalizers (CODE 17), NOT save/load.** jt573 = char review (Save/Cancel
> → jt593); jt572 = race/class finalize commit. The actual *character* `.cch`
> save is `jt584`. The game-save serializer core is jt577–jt580 in CODE 15.

## Data model — what's in a save

**On-disk `SavGam<A..J>.csv` = 10284 bytes** (type/creator `'SAVE'/'FRUA'`; the
FSOpen shim flattens the Mac `<design>/SAVE/` HFS path to the staged dir,
boot.c:28837):

1. **player record** — 1024 B (from `g_a5_-28006 +1`)
2. **position + state block** — `-12288` row/col/facing (5 B), `-27989` (1 B)+pad,
   `-27990` (1 B)+pad, 2 reserved shorts (4 B)
3. **party count** — 2 B little-endian, ≤8 (via jt1180)
4. **N × per-member `.cch` block** (jt578)
5. **design-state pad** — asm jt580 L19ca (CODE_15.s:2093) does `jt412`(tell);
   if pos < 10284 writes `(10284 − pos)` bytes from **`g_a5_-27920`** — the
   item-record **template table** (`jt387(4590)`, boot.c:1210; 255 × 18-byte
   item records, boot.c:51770). **This is the TODO at boot.c:28783** — the port
   writes no pad, so its files are short of 10284.

**Per-character `.cch` record (jt578/jt577) = 398 bytes** (asm `#398`/0x18e):
- swapped fields: word@82, words@76/78/80, longs@68/72, words@84/86 (l0ce0_c15).
- inventory list: head `rec+8`, `.next` at item`+0`; 18 B from item`+40`, words
  `+44/+46` swapped; container (`item[40]==73/'I'`) → `item[53]` sub-items at `+58`.
- spell list: head `rec+4`, 10 B/node, word `+2` swapped, `.next` at `+6`.

> Open: asm jt579 (L124c) reads player+position+count+members only — it does
> **not** read the design-state pad. So the read side of `-27920` lives in the
> **design-reload** path (camp Load sets `-27982=1` to trigger it), not in jt579.
> Must trace where `-27920` is repopulated on load to close the round-trip.

## What works vs stub/missing/stand-in

- **Works (Hatari-verified, #141):** faithful party round-trip — player rec +
  position + state + count + per-member 398-byte records with inventory & spells;
  endian handling; dir scan; file shim; jt159 confirm. Reachable from both menus.
- **Stub/missing:** the **design-state ~10KB block** (jt580 TODO); jt581/jt587;
  boot auto-load; design-select-on-load; jt159 "abandon game?" gate on camp Load
  (boot.c:50949 skips it). The A–J pickers are lifted but bypassed (slot A only).
- **Stand-in:** `port_load_savgame` reads a BasiliskII `SAVGAMA.CSV` and
  heuristically scans 398-byte records (printable name@96, abilities@112 ∈ 3..19,
  maxHP@82), dedups by name, restores **party only** — the bring-up roster source.

## Blockers + open questions

1. **Design-state block (the ~10KB) is the main blocker.** Source confirmed =
   `g_a5_-27920` template table, padded to 10284 in jt580 (asm L19ca). The
   **read** side isn't jt579 — find where the design-reload re-ingests it.
2. **A–J picker live-exercise** needs a populated player handle `g_a5_-28006`;
   jt585 is NULL-guarded and not boot-reachable (gated on `g_a5_-14429`, @29950).
3. **jt581 splice vs cg_pool rebuild:** Mac jt579 splices loaded members via
   jt581/jt147; the port rebuilds via cg_pool/jt590 — confirm member ordering
   matches for a faithful round-trip.
4. ~~**jt577 untested**~~ — **DONE.** jt577/jt578 are LIVE (the saved-character
   roster) and Hatari-verified end to end; both `.cch` round-trip self-tests PASS,
   including the pool-allocating inventory rebuild. See the 2026-07-12 section.
5. Shim flattens `<design>/SAVE/` — slot files aren't design-namespaced; confirm
   no cross-design collision.

## Plan (smallest-first, each independently testable)

1. ~~**jt580 design-state write tail**~~ — **DONE** (asm L19ca: `jt412`(tell) +
   `jt410(-27920, 10284-pos)`). Saved slot is now byte-length faithful (10284,
   Hatari-verified). `-27920` over-allocated to 10284 so the pad read stays
   in-bounds (Mac over-reads into adjacent design-state heap; port's heap isn't
   laid out the same — pad content is non-meaningful, the load path ignores it).
2. ~~**jt577 round-trip test**~~ — **DONE** (boot self-test, `#ifdef
   FRUA_ENGINE_PROBE`): the empty-record test plus a new one-inventory-item
   round-trip (jt578→jt577) — exercises jt577's pool-allocating inventory loop +
   the +44/+46 swap (the empty test skipped both). PASS Hatari-verified. (Spell
   list still un-exercised — a trivial follow-up: rec+4 head, 10 B/node.)
   GOTCHA captured: the `-21508` item pool is null until l4cc0 (design-load), so
   the boot self-test inits it first.
3. ~~**jt579 design-state read**~~ — **INVESTIGATED → no pad read exists.** asm
   jt579 (L124c, lines 1486–1727) reads player+position+count+members then does
   post-load setup (jt56 portraits, jt214/jt951/jt952 play-mode dispatch) — it
   does **not** read the 10284-byte design-state pad, and jt582→l143e→jt579 so
   the picker doesn't either. The pad is **write-only** (fixed slot length). The
   design-state *progress* restores via the `-27982` level/design **reload from
   the `.DSN` files**, a separate subsystem — NOT the save-slot pad. So the
   party+position round-trip is already complete; there is no pad read-side to
   wire. (Design-progress persistence = a separate, larger future card: trace the
   Mac `-27982` reload + whether play mutates the `.DSN`/GEO files.)
4. **jt581/jt587** — lift the two CODE-15 stubs so the port can use the Mac's
   member-splice instead of the cg_pool rebuild.
5. ~~**A–J slot picker wiring**~~ — **DONE 2026-06-21, Hatari-verified
   end-to-end.** The Training-Hall Save (l1142) drives the faithful `jt585`
   **"SAVE WHICH GAME A B C D E F G H I J"** picker; pressing **A** saves a
   byte-faithful **10284-byte SavGamA.csv** (6 HEIRS members, all names present,
   count word `0600`) and returns to the Hall.

   ROOT CAUSE (corrects the prior "rewrite jt585's loop to the DLItem-selection
   model" hypothesis — that was **wrong**). The Mac jt585 (CODE15 0x1a82–0x1ace)
   uses `jt182()`'s return **directly** as the 0-based slot index, and the port's
   jt585 already matched it — EXCEPT the loop-break test was **inverted**. Mac
   `0x1a94 cmpiw #10; bcss` exits the loop on a *valid* pick `slot < 10`; the port
   wrote `if (slot_idx >= 10) break;`, so a valid pick (0–9) never broke — the
   modal just re-armed and waited for more input. Fix: `if (slot_idx < 10) break;`
   + drop the scaffold `iter_guard` (the Mac loops unbounded; jt182/l23b4 block
   each pass). The whole resolution chain was already correct and is proven by a
   live trace: `jt179(9)` seeds `-24126[i*2]=i`; `l2184("A B …J")` fills the
   accelerator codes (`-24126` = {0:'A'…9:'J'}); l23b4 blocks, returns the matched
   item; **l25b6 mode = 9 → the fallback** reads the matched label char ('A'=65)
   and returns slot **0**. (The earlier "l25b6 maps to a control code by mode" was
   a *static inference* about modes 1/4/…; the save picker runs in mode 9 → the
   accelerator fallback, which returns the slot index.) The picker prompt slots
   `g_a5_-13776`="A B C D E F G H I J" / `-13904`="save which game: " are
   DATA-pool-seeded and correct at boot.

   SECONDARY FIX (newly exposed by the now-working save path): `port_load_savgame`
   (the bring-up HEIRS loader) `memcpy`s each 398-byte record raw but does **not**
   reconstruct the inventory/spell node lists, leaving stale Mac heap pointers in
   the list-head fields (spell @4, inventory @8). jt578's faithful list walk
   followed member 2's garbage head → bus error mid-save. Fixed by nulling
   `dst+4`/`dst+8` after the memcpy (empty lists = the honest representation of
   what the stand-in loads; also protects jt903 / the inventory screens — likely
   the same root cause as the [[inventory-subsystem-next]] "rec[12] stale-ptr bus
   error"). The faithful jt579/jt577 load path already zeroes the slot, so it was
   never affected.
6. ~~**jt582 LOAD picker + jt159 load-confirm**~~ — **DONE 2026-06-22,
   Hatari-verified.** Training-Hall **Load Saved Game** now runs the faithful
   chain: jt159 **"Game not saved. Load anyway?"** confirm (when an unsaved game
   exists) → clear party → **jt582 "LOAD WHICH GAME A…J" picker** → pick A →
   l143e→jt579 reads the 10284B slot → the 6 HEIRS members restore. Three fixes:
   (a) **menu-case routing** — the JT[3] table (CODE12 0xefc, decoded) maps case
   8 = L10ca (the LOAD handler: jt159 + jt582) and case 11 = L120c (Exit); the
   port's `k_jt918_menu_items` had "Load"/"Exit" at the wrong slots (8↔11), so
   "Load Saved Game" reached L120c's slot-A stand-in. Swapped the two rows (the
   `phrase` y-id travels with each label, so positions stay) — mirror of the
   earlier 9/10 Save/Begin swap. (b) **flat-dir enumeration** — jt582 built the
   Mac `<design>:SAVE` HFS path; the port stages saves flat, so it now globs
   `SavGam*.csv` via case-insensitive Fsfirst (jt407 made case-insensitive,
   slot-letter upper-cased). (c) **L120c made faithful** (Mac 0x120c: gated
   jt159(-14284) + jt585) instead of the port_load_game load stand-in.
   GOTCHA: L10ca clears the party *before* jt582 (faithful), so a Load with **no
   slot present** leaves an empty roster — only press Load when a save exists.

7. ~~**Faithful design-reload (lift the full L143e)**~~ — **DONE 2026-06-22
   (e160820), Hatari-verified.** The full Mac L143e post-load dungeon restore is
   lifted (jt579 + jt56/l03d2 portraits + jt198(h[19]) GEO reload + jt952 dungeon
   mode + jt85 palette), plus the 4 deps jt951/jt952/l03d2/jt85. Training-Hall
   Save A → Load A runs it with no crash; Begin Adventuring resumes the dungeon
   at the saved cell **10,8** with the member portrait painted.

   **BOOT AUTO-LOAD via L143e — ATTEMPTED + REVERTED 2026-06-22 (does NOT work at
   the seed point).** Routed `port_test_seed_design`'s roster seed through a
   `boot_autoload_savegame()` = GEO-guard + `l143e("SavGamA.csv")`. Two findings
   from Hatari: (1) the roster seed fires at the FIRST (boot-time)
   port_test_seed_design, BEFORE l4cc0 — so -28006 / the -21508/-21152 pools are
   null (fixed by calling idempotent l4cc0() first). (2) **The real blocker:**
   with l4cc0 up, l143e ran but **corrupted the display to garbage + a SysBeep** —
   l143e is an IN-GAME-context function (jt198 GEO load + jt952 dungeon-mode + jt56
   portrait composites + jt85 wall palette) that needs the play environment, which
   isn't set up at the boot/menu seed point. DEEPER COUPLING: the boot path renders
   off the **synthetic `g_area_state` design header** that port_load_savgame
   maintains (it loads party + position-overlay only, never the real header);
   loading the REAL save header (via l143e OR jt579) at boot replaces it, and the
   port's render can't yet handle the real header. So **retiring port_load_savgame
   at boot is blocked on the real-design-load / port_test_seed_design retirement
   (task #100)** — NOT just the L143e lift. Reverted to protect the working HEIRS
   demo; port_load_savgame stays as the boot roster source. The faithful in-game
   Load (l10ca → jt582 → L143e) is the real, working use of L143e. NEXT REAL STEP
   for boot retirement: make the play-entry render off the loaded real header
   (part of #100), then a deferred-to-play-entry l143e (not boot-seed) auto-load.

   Original recipe (now implemented) kept below for reference.

   The "design-reload" the boot auto-load needs is the
   **post-load dungeon restore inside the Mac `L143e`** (CODE15 0x143e), which the
   port had as a bare `FSOpen+jt579` STUB. The faithful Mac L143e
   (disasm 0x143e–0x153c) does:
   ```
   -6924 = h[58]; -22218 = -6923; saved_lv = h[19];   // h = g_a5_-28006
   L0006 -> jt579(file)                                 // header->-28006, pos, party
   for (m = -27932; m; m = m->next)                     // reload portraits
       if (m[147] < 128) l03d2(1);                      //   jt56("CBODYS", m[188], m[189])
       else jt56("CPIC", m[181], m[189]);
   -27932 = -27928;
   h = -28006;
   if (-18485) { h[19]=saved_lv; h[49]=0; h[133]=0; }   // overland
   else        jt198(h[19]);                            // DUNGEON: reload the GEO for the saved level!
   if (h[19] <= 4) { jt951(); jt214(); }                // overland play setup
   else            jt952();                             // dungeon play setup
   if (-6924 != h[58]) jt85(h[58]);                     // wall group / palette refresh
   ```
   **The level is design-header byte 19** (`h[19]`); `jt198(h[19])` reloads the
   GEO; `jt952` sets dungeon mode. This is the whole "reload the design/level from
   the save" chain — it replaces port_load_savgame's hand-rolled level/GEO/resume
   tail (boot.c:16061) entirely.

   **4 missing deps — all small, sub-deps all present:**
   - `jt951` (CODE20 0xb88, 26B): `h[34]=0; h[36]=1; -27990=3` (overland mode).
   - `jt952` (CODE20 0xba2, 26B): `h[34]=1; h[36]=0; -27990=4` (dungeon mode).
   - `l03d2` (CODE15 0x3d2, 44B): `jt56("CBODYS", (-27932)[188], (-27932)[189])`.
   - `jt85`  (CODE6  0x6ada, 108B): wall group/palette manager — cache in `-13040`;
     on change loads "frame" group (jt997/jt468/jt993, all lifted). Deps jt1200/
     jt1163/jt468/jt993/jt997 all exist.
   Use `g_a5_byte(-6924/-6923/-27986)` / `g_a5_word(-13040)` directly (no macros).

   **Integration is faithful (verified by reading l10ca):** the Training-Hall Load
   (l10ca → jt582 → L143e) stashes the loaded play-mode (`-27989 = -27990; -27990
   = 0`) for the subsequent Begin-Adventuring, so L143e SHOULD do the full
   jt198/jt952 dungeon setup mid-load. TEST HARNESS: the now-working Training-Hall
   Load — after the lift, Load slot A should reload the GEO + set dungeon mode;
   verify Begin Adventuring resumes at the saved level/cell, and the Save→Load
   round-trip still works. RISK: jt198 (GEO load) fires mid-Training-Hall; the Mac
   does it unguarded — HEIRS save targets level 5 (GEO005 ships) so it's safe, but
   a foreign save level would jt69-fatal (port_load_savgame guards this; decide
   whether to keep a GEO-exists guard in the port L143e).

   **THEN retire port_load_savgame:** with L143e doing the faithful resume, the
   boot auto-load becomes `l143e("SavGamA.csv")` (loads party + reloads the
   dungeon). port_load_savgame's heuristic scan + manual resume tail both go.
   COUPLING NOTE: jt952/jt951 are also the faithful play-mode setup the port's
   l07dc/l63c0 entry reimplements — full convergence is part of task #100.

8. ~~**boot auto-load**~~ — **DONE 2026-08-02** as the opt-in `autoload.dat`
   port option (see the header). Note the 2026-06 note above is still accurate
   about WHY it cannot run at the boot seed: it runs at play-entry, in `l07dc`,
   which is the earliest point where `-28006` is live and `l143e` has the
   in-game context it assumes. Still open here: the camp Load "abandon game?"
   gate (boot.c:50949) + design-select-on-load. The case-sensitivity of the *shipped*
   `SAVGAMA.CSV` vs the port's `SavGamA.csv` is now handled in the picker, but a
   cross-design save namespace (the shim flattens `<design>/SAVE/`) is still open.

Related: [[inventory-subsystem-wall]] (the 398 B record's item serialization is
the same format), [[party-model-migration]] (the `-27928` party list jt579
rebuilds).

## The cold-disk / not-found dialog — jt987's retry loop can be ANSWERED (2026-07-12)

A load that could not find its file used to spin forever in `jt987`'s retry loop
with nothing on screen. That is the single mechanism behind every "frozen but
alive" screen a missing file has produced — the ADD-CHARACTER wedge among them.
It is now a dialog you can answer. **Four** separate defects, and the loudest one
(`l157c` being a stub) was not the reason it was invisible:

1. **`l157c` (CODE 5 + 0x157c) was a PROBE stub.** Full lift now. `a` picks the
   message and doubles as show/hide: 0 = dismiss (repaint over the box, restore
   the saved page at -4658); 1 = "Please Insert %s" (a kind of 'S' is remapped
   through the save-disk letter at -4670 FIRST — the Mac writes it back over its
   own argument — then ' ' -> "the Disk", 'S' -> "SAVE Disk", else "Disk %c"; a
   kind of 0 means the file is missing: "'%s' not found"); 2 = "Disk %s error"
   ("write" when `b` is set, else "read").

2. **★ main() registered EMPTY THUNKS as the dialog handlers.** `l157c` calls the
   hook at -4680 (installed via JT[989]) in preference to painting itself — and
   the port registered `jt10_handler` / `jt11_handler`, two `{ }` stubs, while
   the REAL `jt10` (CODE 6 + 0x0538) and `jt11` (CODE 6 + 0x04c0) sat fully
   lifted and `__attribute__((unused))` a few thousand lines away. So `l157c`
   dutifully called a handler that did nothing. THIS is why nothing drew. Both
   thunks deleted; the real handlers are registered, exactly as the Mac does.

3. **`jt987`'s wait loop lost its BODY.** The Mac's `while (!L0088() && !JT[1121]())`
   calls **L0f9c** every iteration — which the port already has, as **jt980**
   (the lXXXX = jtN alias trap again): it services the sound driver's next voice,
   so the music keeps playing while the dialog waits. The port spun on an empty
   body, silencing it.

4. **`jt394` had no "%r".** THINK C's recursive conversion: the next arg is a
   format string and the one after it a POINTER to that format's own argument
   block. `L0ab6` (the box-label painter) is its only user and always passes
   `"%r"` alone. m68k's `va_list` IS a byte pointer into such a block, so the
   inner call is a plain `vsprintf` over it. This is also what fills the `%c` in
   "Disk %c" — at PAINT time, with the drive letter. Not a Mac bug after all.

Plus one port concession, the same one `jt1134` carries: `l157c`'s paint ends in
`qd_present()`. On the Mac the framebuffer IS the screen; the Falcon HAL
double-buffers and `jt987`'s wait loop never reaches a present, so the box was
drawn and never shown.

**Verified live** (temp harness: `jt987(0, "NOSUCH.XXX", 0, NULL)` right after the
menu comes up): the box paints **'NOSUCH.XXX' NOT FOUND**; lowercase `c` (99) is
not a verb so it retries and re-shows — faithful; uppercase **`C`** (67) cancels
and `jt987` returns 0. The Mac compares against uppercase `'Q'`/`'C'`/`'?'` and
`jt1133` does not up-case, so uppercase is correct, not a port quirk.

## l005a — the save-medium precondition (2026-07-12)

`l005a` (CODE 15 + 0x5a) is the gate `jt589` / `jt585` / the roster builders sit
behind, and it was a stub returning 1. Full lift now, Mac shape kept verbatim:

    for (;;) {
        if (scan opens) break;              /* L00b2 — medium answered   */
        jt179(1);                           /* L0082 — prompt and retry  */
        if (jt182("Please insert save disk.", "Ok Exit", 0, 0) & 0xff == 1)
            return 0;                       /*         Exit -> give up   */
    }
    while (jt991(&isdir)) ;                 /* L00cc — DRAIN the scan    */
    return 1;

Draining is load-bearing: jt990/jt991 carry their cursor in globals, so a
half-consumed scan bleeds into L01be's.

**PORT (ADR-0003) — and the trap in it.** The Mac scans the design's SAVE FOLDER
(jt431 over the design name at -31336, then "SAVE"), and an EMPTY folder still
counts as REACHABLE; it is L01be's own scan that decides whether any characters
exist (jt589 posts "No characters to load." over an empty list). The Atari has no
per-design SAVE folder — saved characters are flat CHAR*.CHR on the C: mount — so
the medium IS that mount and the test is a scan of it. **The pattern must NOT be
"CHAR*.CHR":** the port's jt990 reports "a file matched", not "the scan opened",
so a roster with no characters yet would look like an absent disk and prompt
forever. `"*.*"` is the reachability question the port can actually ask.

The stub's answer (1) was right on a hard disk, but it skipped the prompt
entirely — with the data unmounted the engine sailed past the precondition
instead of saying so.

**LIVE** (temp: point the scan at a pattern that cannot match): "PLEASE INSERT
SAVE DISK." with OK / EXIT renders; **OK retries and re-prompts**, **EXIT returns
0** and the engine recovers without hanging. With the medium present the roster
builders are untouched — the Add-Character picker still lists all six.

## The saved-character roster is now a real .cch stream — the "garbled roster" crash (2026-07-12)

**SOLVED, root-caused, Hatari-verified.** The intermittent "garbled roster panel"
(the visual-garbage class the user has always correctly read as *"that's a crash
to the desktop"*) was a **bus error in JT[878]**, and the cause was the port's
saved-character file format.

### The bug

`save_roster()` wrote each pool slot as a **raw 512-byte blit** of the record.
That also persists the record's four POINTER fields — `rec+0` (party next),
`rec+4` (spell head), `rec+8` (inventory head), `rec+64` (sub-record) — which are
**live heap addresses**. `load_roster()` read them straight back, so a fresh
process inherited pointers into a **dead heap**:

```
CHAR0001.CHR   +4 = $004FB0F8     <- spell-list head
               +8 = $004FFBC4     <- inventory head
```

JT[878] walks `rec+4`. Its NULL guards are all faithful and correct — but a
garbage node's `.next` held **`-1`**, which passes `tst.l` and then dies on
`cmp.b (a0),d1`:

```
Bus error reading at address $ffffffff PC=$46442     (SR 308 = USER mode = us)
0004643A  movea.l ($0004,a2),a0    ; p = rec->spell_head
0004643E  tst.l   a0
00046440  beq.b   #$0E             ; NULL check passes...
00046442  cmp.b   (a0),d1          ; <-- a0 = $ffffffff
00046448  movea.l ($0006,a0),a0    ; p = p->next
```

The engine installs an `$_exception_handler`, so it **caught the fault and limped
on** — which is why the symptom was half-drawn garbage rather than a clean bomb.

Worse than the crash: the raw format **cannot represent the item nodes at all**
(it only ever stored the 398-byte record). So equipment silently vanished across
a save, and every inventory-derived stat — AC via jt21 — was being computed by
walking **whatever happened to live at those stale addresses**. An observed
"AC -3" was garbage that merely failed to fault.

### The fix — use the serializer that was already sitting there

`save_roster` / `load_roster` now go through the **faithful CODE-15 pair**:

- **write:** `g_a5_-6902 = slot; l00e0(fn, jt578)` — JT[578] writes the 398-byte
  record, then each inventory item's 18 data bytes (containers followed by their
  sub-items), then the spell list.
- **read:** `l_cch_read(fn, slot)` — points `-6902` at the slot and runs
  **JT[577]**, which reads the record and then **rebuilds** both chains out of
  fresh pool nodes (`-21508` items / `-21152` spells), NULing `rec+0` and `rec+64`
  on the way out. Every pointer field in a loaded record is then either a live
  node or zero — never an address inherited from the writer's process.

Both `jt577` and `l_cch_read` existed, round-tripped, and were parked as
`__attribute__((unused))` — the known debt noted at boot.c ("jt577/jt578 exist and
round-trip, but save_roster still writes raw slots"). This just wires them up.

**How JT[577] rebuilds (worth knowing — it is not a pointer read):**
- *inventory*: the on-disk `rec+8` is used **only as a "had items?" flag**; the
  rebuild is driven by the item count `rec[193]`.
- *spells*: each 10-byte node is written **including its `+6` next pointer**, and
  JT[577] reads that on-disk pointer purely as a **"another node follows"
  boolean** (`cont_more = *(long*)(node+6)`), then zeroes it.

### Ordering hazard (fixed in the same commit)

`load_roster` runs from `port_test_seed_design()`, which the boot calls from
`boot_a5_seed_defaults()` — **before** ua_main reaches `l4cc0()`, which is what
allocates the `-21508` / `-21152` buckets. Without them `jt477` returns a NULL
node and JT[577] would read the file into low memory. `l4cc0` is idempotent and
already documents this "probe harness enters without the full boot" case, so it
is pulled forward ahead of the roster load.

### Verified

- `cch round-trip self-test: PASS` (field@82 survives the byte-swap) and
  **`cch inventory round-trip: PASS`** (item type@40 + swapped val@46 survive —
  this is the pool-allocating rebuild loop, not just the flat record).
- `CHAR*.CHR` are now **398 bytes** (the .cch record) where the raw dump was
  always exactly 512.
- Original repro (2-member party → Begin Adventuring), `--trace cpu_exception`:
  **`Bus error … PC=$46442` gone; zero faults in engine address space.** The only
  bus errors left are TOS's own two ROM hardware probes (`PC=$e0184e`,
  `PC=$e02cde`), which are present even on a bare menu boot.
- Reboot → all four pool characters deserialize from `.cch` and list correctly.

### Note for anyone with old files

The raw-512 and .cch formats share the same `CHAR*.CHR` name. **Old raw files are
not readable** as .cch (their garbage `rec+8` makes JT[577] try to read items out
of the record padding). Delete `data/work/gamedata/CHAR*.CHR` once; the port
re-seeds the pool and rewrites them in the new format.

### Method note

`--trace cpu_exception` floods the log with **millions** of TOS ROM lines — HBL /
VBL / MFP interrupts, which Hatari's tracer also calls "exceptions". Filter to
faults in **engine address space** (PC < `$e00000`) or on the `SR` S-bit (TOS runs
supervisor `SR 2xxx`; our app is user mode, `SR 0xxx`). Then map the PC to a
function: the relocation offset is `runtime - link` for any known symbol
(here `_jt936`: `$40AA6 - $21A52 = $1F054`), so `$46442 - $1F054 = $273EE` →
`nm -n frua.prg` → **`_jt878`**.

### Follow-up: the equip-by-kind slots rec[12..60] (same commit family)

Wiring JT[577] exposed a second derived-pointer field. `rec[12..60]` are the **13
equip-by-kind slots** — pointers INTO the inventory chain, one per item kind (plus
two ring slots, ammo, off-hand). JT[578] writes them out with the record (the Mac
does too), so they arrive from a load as **addresses belonging to the writer's
process** — the documented `jt28(rec[12])` bus-error trap.

The Mac neutralises them immediately: **jt579 calls JT[21] right after JT[577]**,
and JT[21] step 1 clears those slots and **refiles** each worn item from the
rebuilt `rec+8` chain by its type-record kind.

**`load_roster` cannot call JT[21].** It runs from the BOOT seed
(`main.c` → `boot_a5_seed_defaults` → `port_test_seed_design`), which is *before*
`jt361` reaches `L4d98` and fills the item-template table (`-27944`) that JT[21]
files by. Measured against an empty table, JT[21] mis-files and wrecks the derived
stats:

| field | correct | JT[21] with empty item table |
|---|---|---|
| Armor Class | **-3** | 8 |
| THAC0 | **14** | 15 |
| Damage | **1D8+4** | 0D0+1 |
| Equipment | LONG SWORD +1 + PLATE MAIL +1 | sword only (armour dropped) |

So the split is: **`load_roster` ZEROES the slots** (the only correct value for a
derived pointer read off disk — exactly what JT[21] step 1 leaves), and the
**refile is deferred** to the first `port_test_seed_design` call that arrives with
a design up. `l07dc` reaches it on the way to the Training Hall, which is the
earliest point a character sheet can be opened.

**★ A trap worth remembering:** before the zeroing, the sheet rendered the gear
*correctly* from the stale on-disk pointers — because JT[577] rebuilds the chain
into the **same deterministic pool slots** (`base + idx*62`) the writer used, so
the dead addresses happened to alias the right nodes. It would have broken the
moment the character set or load order changed. **A stale pointer that "works" is
still a stale pointer.**

**Verified (HEIRS party, load save A → reboot → View Character):** all six members
deserialize; BARBARUS shows **LONG SWORD +1 / PLATE MAIL +1, AC -3, THAC0 14,
DAMAGE 1D8+4, ENCUMBRANCE 880** (the encumbrance proves the whole rec+8 chain is
walked, not just the equipped pair); zero faults in engine address space. The six
.cch files are 480–526 bytes — *different sizes per character*, where the raw dump
was always exactly 512.

## "Load Saved Game does nothing" with an empty SAVE folder is FAITHFUL

Asked 2026-08-05, and settled two ways.

**The Mac's own asm returns silently.** `jt582` builds a present-slots bitmap by
enumerating the design's SAVE folder, compacts it into an index, and then:

    if (idx[0]==0xFF) return;      ; nothing to load

No alert, no message, straight back to the Training Hall — which is what this
port does.

**DOS 1.2 agrees, observed.** Driven headlessly with no `SAVGAM*.CSV` in
`HEIRS.DSN\SAVE\`: pressing `L` at the Hall produces a screenshot
**byte-identical** to the one before the keypress. (This is also what once made
me conclude the DOS `L` key was dead and switch to mouse driving — see the
header of `tools/dosdrive.sh`. A silent control is not a dead control.)

★ **Do not "fix" this into a notice.** SSI clearly knew how: the CHARACTER
roster builder `jt589` DOES post "No characters to load." / "...to delete."
through `jt42` when its list comes back empty. The save-game picker has no such
call. The asymmetry is theirs, and adding one here would be a divergence.

**And the case is reachable on a fresh install** — designs do NOT ship a save.
Neither the DOS install disks nor an installed `HEIRS.DSN` contain any
`SAVGAM*.CSV` or `VAULT*.DAT`; the pair sitting in this repo's staged
`data/work/gamedata/HEIRS.DSN/` is an artefact of our own July DOS session, not
something SSI shipped. So every design starts in this state until the player
saves once.
