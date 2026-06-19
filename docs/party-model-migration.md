# Party data-model migration: cg_pool stand-in → faithful -27928 list

Goal (user-directed 2026-06-19): eliminate the port's character-management
stand-ins so the Training Hall runs the faithful Mac code. The blocker is the
**party data model** — the two models are incompatible and must be switched
atomically.

## The two models

**Port (stand-in, today):**
- `cg_pool[16][512]` — a fixed array holding ALL characters (party + benched).
- `CHAR_INPARTY` (rec[210]) flag — 1 = in the party, 0 = benched.
- `cg_party_relink()` rebuilds `-27928` from the `CHAR_INPARTY==1` pool slots.
- So `-27928` is *derived*; the flag is the source of truth.
- Helpers: cg_collect_party / cg_collect_addable / cg_add_character /
  cg_remove_from_party / cg_view_sheet / cg_delete_character.

**Faithful (Mac):**
- `-27928` is the *primary* party list — a directly-managed singly-linked list
  (`.next` at record offset +0). It IS the source of truth.
- Each member record carries `rec[189]` = its party SLOT (0-6); the count lives
  in the game record `-28006[32]`.
- The *benched* / saved characters are **not in memory** — they are `CHAR*.CHR`
  files on disk, enumerated on demand by `jt589` (via l01be / jt990).
- Party ops: `jt590(entry)` appends a record to `-27928` + assigns the first
  free slot + bumps `-28006[32]`; the symmetric unlink removes it.

The conflict: `cg_party_relink` (derives `-27928`) and `jt590` (manages
`-27928`) cannot both be live. The switch is atomic.

## Target model (faithful, on the port's storage)

Keep `cg_pool[16][512]` purely as the **node backing** for party members (the
Mac NewPtr's them; we allocate from the fixed pool). After the migration:
- A `cg_pool` slot is "in use" **iff it is linked in `-27928`** (no flag).
- `-27928` is managed directly: `jt590` appends, the unlink removes.
- `rec[189]` = party slot; `-28006[32]` = party size.
- Saved/benched characters live only as `CHAR*.CHR` files (jt589 lists them).
- Allocation (`cg_node_alloc`): return a `cg_pool` slot not currently in
  `-27928`.

## Existing partial state (discovered 2026-06-19)

The port is ALREADY half-migrated: `l12a0` (boot.c ~27300, the View-label case
6) carries a PARTIAL faithful Add — `jt477` allocs a 398-byte slot, `jt587`
loads the picked `.CHR`, and on an EMPTY roster `jt590(fresh_slot)` appends it —
right next to the `cg_pool`/`CHAR_INPARTY` stand-in path it uses otherwise. So
the migration is an untangle-and-finish, not a from-scratch build. (jt590 was a
stub until 2026-06-19; lifting it activated only that empty-roster branch.)

## Phases (each must keep the build + FRUA_HALL working)

0. **jt590 lifted** (DONE 2026-06-19) — the append primitive (above).

1. **Primitives.** Lift `jt590` (append + slot assign + count), a party
   **unlink** primitive, and `cg_node_alloc` (free-slot finder that walks
   `-27928`). Leaf functions; not yet wired. (jt590 mapped: CODE 15+0x1b74.)
1b. **Primitives DONE** (53c9391): cg_node_alloc (free cg_pool slot not in
   -27928) + cg_party_unlink (remove from -27928). With jt590 the append/
   remove/alloc set is complete.

2. **The atomic flip (Phase 2) — all-or-nothing, deeper than first scoped.**
   `port_test_seed_design` builds `-27928` by `jt590`-appending the party
   records; `cg_party_relink` is retired (no-op'd, then deleted). BUT every
   `cg_party_relink` (12) and `CHAR_INPARTY` (24) site flips together, AND the
   benched-character concept moves OUT of `cg_pool` and INTO `CHAR*.CHR` files:
   - `cg_collect_addable` must read the `CHAR*.CHR` files NOT currently in
     `-27928` (today it scans `cg_pool[CHAR_INPARTY==0]`).
   - `cg_pool` then holds ONLY party members; benched/saved = files only.
   - This is irreversible-ish and FRUA_HALL does NOT exercise save/load, so a
     subtle break could slip past the harness into the real game. Execute with
     fresh attention; pin the .CHR read + the 12 relink/24 flag sites in one
     careful pass, then FRUA_HALL + a save/load round-trip.
3. **Create.** `jt574` already benches (saves the `.CHR`, no party change) —
   just stop touching `cg_pool`/`CHAR_INPARTY`; the saved file is the record.
4. **Faithful Add (L12a0).** Browse `CHAR*.CHR` (jt589) → jt169 list pick →
   load the file into a `cg_node_alloc` slot → `jt590` append, with the
   composition checks ("too many rangers in party"). Replaces cg_add_character.
5. **Faithful Remove + Delete.** Remove = unlink the selected `-27928` member +
   free its slot. Delete (L15e2) = jt589 list → "Delete %s forever?" → unlink
   the `.CHR` file. Replaces cg_remove_from_party / cg_delete_character.
6. **Faithful View (jt904).** The sheet + jt182 action bar; replaces
   cg_view_sheet. (Read-only wrt the party — lowest risk, can slot in anytime.)
7. **Remove the stand-ins.** Delete CHAR_INPARTY, cg_collect_party,
   cg_collect_addable, and the cg_* screen functions once nothing calls them.

## Touch points (grep footprint, 2026-06-19)

- `cg_party_relink` — 12 call sites.
- `CHAR_INPARTY` — 24 references.
- Plus cg_collect_party / cg_collect_addable / the cg_* screens, the seed,
  jt574, save_roster, the roster (l02dc walks `-27928` already), the play HUD.

## Risks / testing

- The roster (l02dc), the HUD, and save/load all read `-27928` — they keep
  working as long as `-27928` stays a valid `.next@+0` list of 512-byte
  records (it does). The risk is in the WRITE paths (seed/Create/Add/Remove).
- Test each phase in FRUA_HALL (jumps to l07dc → jt918): party renders, Add
  finds saved chars, Remove benches, selection (L0848) still tracks.
