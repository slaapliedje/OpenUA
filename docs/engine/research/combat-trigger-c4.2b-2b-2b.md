# DQK (Gen2) In-World Combat Trigger — Research Findings (C4.2b-2b-2b)

**Assigned task:** Reverse the DQK ECL combat-trigger path — from the per-step main loop
(vmRun1) and SearchLocation down to the actual `LOAD_MONSTER` / `COMBAT` opcodes. Produce
grounded findings with a concrete worked example and engine wiring proposal.

**Sources:** `DISK3/ECL.GLB` (DQK), `packages/engine/src/ecl/opcodes.ts`,
`packages/engine/src/ecl/vm.ts`, `packages/engine/src/ecl/disassemble.ts`,
`apps/web/src/ui/dqkExplore.ts`, hackdocs `OPCODES.TXT`. All ECL bytes decoded live via
offline probes (now deleted). No engine source was modified.

---

## 1. The two combat-launch opcodes (Gen2 = Gen1 unchanged)

| Opcode | Mnemonic | Operands | Function |
|---|---|---|---|
| `0x0b` | `LOAD_MONSTER` | 3 EclOpp: `(monsterId, count, picBlockId)` | Appends one group to the VM's pending encounter roster |
| `0x24` | `COMBAT` | 0 | Pops the pending roster and fires combat; returns `status:'combat'` to the host |
| `0x1c` | `CLEARMONSTERS` | 0 | Empties the pending roster without fighting |
| `0x1d` | `PARTYSTRENGTH` | 1 EclOpp | **Strength gate, not a roster-filler** — computes a party-strength scalar and sets the cmp register vs the operand threshold for the following `IF_*` (corrected; see §4) |
| `0x23` | `SURPRISE` | 4 EclOpp | Initiates surprise-check before combat (not always present) |

Source: `packages/engine/src/ecl/opcodes.ts` entries 0x0b, 0x1c, 0x1d, 0x23, 0x24. The
`vm.ts` COMBAT handler (case 0x24) pushes `{ type: 'combat', monsters: this.encounter }` and
returns `status: 'combat'`. The Gen2 opcode table is byte-for-byte identical to Gen1.

EclOpp operand encoding: code `0x00` = immediate `#N`; codes `0x01`/`0x02`/`0x03` = memory
address `[addr]` (u16 LE at bytes 1-2). Multiple `LOAD_MONSTER` calls before a single `COMBAT`
build a multi-group encounter.

---

## 2. The per-step dispatch: vmRun1

### Header format (Gen2, 20 bytes, no magic, vbase 0x8000)

```
offset 0x00: uint16 lo  \  vmRun1 far pointer
offset 0x02: uint16 hi  /
offset 0x04: uint16 lo  \  searchLocation far pointer
offset 0x06: uint16 hi  /
offset 0x08: uint16 lo  \  preCampCheck far pointer
offset 0x0a: uint16 hi  /
offset 0x0c: uint16 lo  \  campInterrupted far pointer
offset 0x0e: uint16 hi  /
offset 0x10: uint16 lo  \  eclInitialEntry far pointer
offset 0x12: uint16 hi  /
```

Effective offset of a slot = stored vaddr − 0x8000.

### Standard vmRun1 dispatch pattern (area 18 example — "Hizden hamlet")

```
vmRun1 @ 0x833e:
  833eh  SAVE               #256, c99:0          ; reset step flag
  8344h  COMPARE            [0x0008], #6150      ; time-of-day check 1
  834bh  SAVE               #257, c99:0
  8351h  COMPARE            [0x0008], #6417      ; time-of-day check 2
  8358h  SAVE               #257, c99:0
  835eh  SAVE               [0x002d], [0x00cc]   ; save raw cell word
  8365h  AND                #383, c2d:0, [0x00bf] ; mask low 9 bits → cell type bits
  836eh  COMPARE_AND        [0x00bf], #257, c11:0, #5634  ; cell==257 AND facing==5634?
  837ah  GOTO               [0x8505]             ; yes → transition handler
  837eh  AND                #376, c2d:0, [0x00c0] ; alternative masking
  8387h  COMPARE            [0x00c0], #5672
  838eh  GOTO               [0x85ab]             ; cell-type branch
  8392h  COMPARE_AND        [0x00bf], #312, c11:0, #5633
  839eh  GOTO               [0x83a3]
  83a2h  EXIT
```

**The dispatch pattern in plain terms:**

1. `SAVE [0x002d], [0x00cc]` — preserve the packed GEO cell word.
2. `AND [0x002d], #MASK, [0x00bf]` — extract cell descriptor bits into a temp register (`[0x00bf]`).
3. `COMPARE_AND [0x00bf], #CELLBITS, [0x0011], #FACING` — test cell bits AND facing simultaneously.
4. Conditional `GOTO` to the fight or event subroutine.

Key memory addresses confirmed by disassembly:
- `[0x002d]` — packed GEO cell descriptor word (wall bits + backdrop + flags).
- `[0x0011]` — current facing direction (0=N, 1=E, 2=S, 3=W).
- `[0x00bf]` — scratch temp (dual-used; also party X in some contexts).
- `[0x0097]` — encounter cooldown counter (see §3).
- `[0x0023]` — wandering-encounter selector variable (area 5 ON_GOTO dispatch).

### searchLocation dispatch pattern (area 23 — "Dargaard Keep")

```
searchLoc @ 0x8323:
  8323h  AND                [0x002d], #383, c9b:0   ; mask cell bits
  832ch  COMPARE            [0x009b], #5632          ; == 0? (empty cell)
  8333h  GOTO               [0x83a4]                 ; yes → exit path
  8337h  AND                #320, c9b:0, [0x00bf]   ; test wall-type sub-field
  8340h  COMPARE            [0x00bf], #5888
  8347h  GOTO               [0x9f27]                 ; specific wall type → event
  834bh  AND                #288, c9b:0, [0x00bf]
  8354h  COMPARE            [0x00bf], #5888
  835bh  GOTO               [0x9e54]
  835fh  SUBTRACT           #257, c9b:0, [0x00bf]   ; normalize cell-type index
  8368h  ON_GOTO            [0x00bf], #274, ...      ; jump table: 274 entries
                            ... [0] = c8a:134, [1] = [0x8e97], ..., [16] = [0x9c40], ...
```

The `ON_GOTO` dispatches on the normalized cell index. Entry `[16]` maps to `sub_9c40` which
contains the `PARTYSTRENGTH + COMBAT` sequence.

---

## 3. Encounter cooldown (addr 0x0097)

Several areas open vmRun1 / searchLocation with:

```
COMPARE  [0x0097], #0
IF_NE
SUBTRACT #1 → SAVE #255, [0x0003]
EXIT
```

When `[0x0097]` is non-zero, all event processing is skipped. This prevents immediate re-trigger
on the same cell after combat. The engine must preserve `[0x0097]` in `areaMemory` and decrement
it on each step.

Confirmed in areas 30, 34, and several others. The value `#255` written to `[0x0003]` after
decrement appears to signal "step consumed" to the host.

---

## 4. The two paths to `COMBAT`

### Path A: `LOAD_MONSTER` → `COMBAT` (explicit roster, scripted encounter)

```
; Area 66 (Zhaman / Palace district), searchLoc subroutine @ 0x8398:
  8398h  LOAD_MONSTER       #24, c4:0, [0x000b]   ; monster-id=24, count=[0x000b], pic=[reg]
  83a1h  COMBAT                                   ; fire with queued roster
  83a2h  EXIT
```

Also confirmed in area 2 (debug arena — `LOAD_MONSTER [0x0099], [0x009a], [0x0099]` × 4 →
`COMBAT`) and area 5 (overland wandering encounter `ON_GOTO` → per-encounter `LOAD_MONSTER`
stubs). Arguments are fully runtime: `monsterId`, `count`, and `picBlockId` can all be
immediate (`#N`) or memory references (`[addr]`), enabling the engine to look them up from
the GEO or a rolled random table.

Multiple `LOAD_MONSTER` calls before a single `COMBAT` create a multi-group encounter
(confirmed in areas 43 and 66 which queue 3–4 groups).

### Path B: `PARTYSTRENGTH` → `COMBAT` (auto-scaled roster, wandering encounter)

```
; Area 23 sub_9c40 (cell index 16 in searchLoc ON_GOTO):
  9cceh  PARTYSTRENGTH      c12:128   ; engine selects + queues monsters scaled to party
  9cd1h  IF_GE                         ; conditional on some party-strength threshold
  9cd2h  COMBAT                        ; fire auto-queued encounter
```

Also confirmed in area 18 sub_9130 (`PARTYSTRENGTH c12:128` → `COMBAT`).

> **⚠ CORRECTED (C4.2b-2b-2c-ii, 2026-06-22).** The "engine auto-populates the roster, scaling to
> party level" reading above is **NOT borne out by the disassembly** and is retained only to show the
> earlier hypothesis. Re-probing the real ECL (area 23 etc.) shows `0x1d` is *consistently followed by*
> an `IF_GE`/`IF_EQ` (e.g. `PARTYSTRENGTH c18:128 ; IF_GE`), exactly like `COMPARE`. So `PARTYSTRENGTH`
> is a **party-strength *gate*, not a roster-filler**: it computes a party-strength scalar, compares it
> to the operand, and leaves the result in the comparison register for the following `IF_*`. This also
> matches `ecl-krynn-gen1-opcodes.md` ("Check/compute party strength"). The roster for a Path-B fight
> still comes from a `LOAD_MONSTER` (or the engine's own wandering table) — **not** from `0x1d`.
> The single operand (`c12:128`/`c18:128`) is the **threshold**; the party-strength *formula*, the
> operand's *units*, and the gate *direction* (does a strong party fight a scaled encounter, or skip it?)
> are all **unrecoverable from disk** (undocumented; not in hackdocs `OPCODES.TXT`).
>
> **Implementation (`packages/engine/src/ecl/vm.ts`):** `case 0x1d` sets
> `cmp = {left: hostPartyStrength, right: rvalue(operand)}` and emits a `{type:'partyStrength', strength,
> threshold}` effect (previously a no-op → `unhandled`, which left the following `IF_*` reading a stale
> cmp). The host supplies the strength metric via `EclVmOptions.partyStrength`, **default 0** — which
> conservatively *fails* `IF_GE threshold` gates (deterministic, no spurious combat). Note the linear
> byte-scan that surfaced these sequences is itself unreliable (§7); the `IF_*`-follows pattern is the
> trustworthy signal, cross-checked against the gen1-opcodes table.

### Which path fires from vmRun1 vs searchLocation?

From the probe data:

| Entry point | Fight path | Confirmed areas |
|---|---|---|
| `searchLocation` | `ON_GOTO` → `LOAD_MONSTER` → `COMBAT` (Path A) | 66, 23, 18 |
| `searchLocation` | `ON_GOTO` → `PARTYSTRENGTH` → `COMBAT` (Path B) | 23, 18 |
| `vmRun1` | `COMPARE_AND` → `GOTO` → subroutine (both paths) | 18, 5, 30 |
| `vmRun1` (area 5) | `ON_GOTO [0x0023]` → 8 wandering-encounter stubs (each `LOAD_MONSTER`→`COMBAT`) | 5 |
| `vmRun1` (area 66) | `GOSUB [0x9f6f]` → cooldown check → `EXIT` (thin wrapper) | 66 |

**Conclusion:** Both vmRun1 and searchLocation can independently reach a `COMBAT` opcode.
A full per-step execution must call both. Area 66's vmRun1 is almost empty (just a shared
cooldown GOSUB); area 18's vmRun1 is the primary fight trigger for the hamlet.

---

## 5. Concrete worked example — Area 66 (Zhaman district), searchLocation path

**Area 66** = DISK3/ECL.GLB member 46 (size 8142 bytes, vbase 0x8000).
Entry points: vmRun1 @ 0x8592, searchLoc @ 0x8629, eclInit @ 0x8014.

Step trace for "party moves into a combat cell":

```
1.  searchLoc @ 0x8629 executes
2.  GOSUB [0x9f6f]           — shared cooldown check (runs [0x0097] decrement logic)
3.  SAVE #256, c97:0         — reset cooldown
4.  COMPARE [0x00a0], #6399  — check encounter-cycle counter
5.  ADD #257, ca0:0, [0x00a0] — increment it
6.  COMPARE [0x00a9], #6145  — check wandering-encounter ready flag
7.  IF_EQ / GOTO             — not time yet → skip fight setup
8.  RANDOM #261, cbf:0       — roll for encounter
9.  ...
10. AND #383, c2d:0, [0x009b] — mask cell bits
11. ON_GOTO [0x009b], #271, ... [0x88e7], [0x890d], ... [0x8398], ...
    (one of the jump-table entries dispatches to sub_8398)
12. sub_8398:
      LOAD_MONSTER #24, c4:0, [0x000b]  ; monster #24, count from [0x000b], pic from reg
      COMBAT                             ; fire encounter
      EXIT
```

Monster `#24` in MONCHA.GLB = **(UNKNOWN — the monsterId-to-name mapping requires
correlating LOAD_MONSTER arg `#24` with the MONCHA id table; not resolved in this pass).**

---

## 6. Concrete worked example — Area 5 (overland), vmRun1 path

vmRun1 @ 0x8429:

```
  8429h  COMPARE  [0x009c], #5633     ; check "first step on area" flag
  8430h  SAVE     #256, c9c:0
  8436h  IF_EQ / GOTO [0xa76a]        ; first step → encounter init sub
  843bh  ON_GOTO  [0x0023], #264, ...  ; wandering encounter dispatch (8 main slots):
         [0] = ... [0xa5dd] [0xa5f3] [0xa607] [0xa61d] [0xa631] [0xa647] [0xa65b]
         each pointing to a stub like:
           a9a3h: LOAD_MONSTER [0x00c0], [0x00bf], c13:42
                  COMBAT
           (coords loaded into monster args from party position registers [0xc0]/[0xbf])
```

`[0x0023]` is the area's random encounter ticker (incremented each step or rolled on
encounter check). When it reaches the target index, the corresponding stub fires
`LOAD_MONSTER` + `COMBAT` with monster id and count taken from runtime memory registers.

---

## 7. Raw byte scan vs walk-reachable discrepancy

Most areas show high counts of raw `0x24` bytes that are **not reachable from entry points** by
a static walker. Example: area 34 has 65 raw `0x24` bytes but 0 confirmed by walk. This is
explained by **0x24 appearing as data bytes inside 6-bit-encoded strings** (the Gold Box string
encoding places arbitrary byte values in the packed text payload). The raw byte scan is therefore
unreliable; only the disassembler-based walk is trustworthy.

Areas with walk-confirmed COMBAT: **5, 18, 22, 23, 30, 35, 50, 66**. Many others (14, 15, 16,
35, 43, 46, 48, 62, 64, 65, 66) have walk-confirmed `LOAD_MONSTER` opcodes but the COMBAT that
consumes them is deeper in a subroutine chain not fully traced in this pass.

---

## 8. Open questions / unknowns

| Item | Status |
|---|---|
| Exact semantics of `PARTYSTRENGTH` operand (`c12:128`) | UNKNOWN — opcode undocumented in hackdocs; engine selects monsters from some table indexed by the operand **(inferred)** |
| Meaning of `[0x0023]` in area 5 ON_GOTO | UNKNOWN — wandering encounter selector, likely incremented per step by the engine runtime **(inferred)** |
| Exact meaning of `[0x002d]` bits after masking | UNKNOWN — GEO cell descriptor word; mask values `#383` (0x17F) and `#376` (0x178) extract sub-fields **(inferred)** |
| What GEO data produces which cell-type index for ON_GOTO dispatch | UNKNOWN — requires cross-referencing GEO map format with cell descriptor bit layout |
| Whether `SURPRISE` (0x23) must be handled before `COMBAT` or if COMBAT can fire without it | UNKNOWN — `SURPRISE` seen in area 34 searchLoc but not in the confirmed combat chains above; likely optional |
| Monster `#24` in LOAD_MONSTER for area 66 | UNKNOWN — MONCHA id table not correlated here |
| vmRun1 and searchLocation interaction: can both fire a COMBAT in the same step? | UNKNOWN — engine should probably fire vmRun1 first, then searchLocation, and stop after the first COMBAT effect **(inferred from Gen1 pattern)** |
| `PARTYSTRENGTH IF_GE COMBAT` — what happens when IF_GE is false (party too strong) | UNKNOWN — presumably falls through to EXIT without combat |

---

## 9. Implications for the engine

### What the current engine (`dqkExplore.ts`) is missing

Current `runStepEvent` calls only `fireSearchLocation`. The vmRun1 handler is **never called
per step**. This means:

1. Area-exit triggers (e.g., area 18's hamlet departure prompt) do not fire.
2. vmRun1-only combat triggers (area 18, area 5 wandering encounters) never launch.
3. The encounter cooldown (`[0x0097]`) is not maintained across steps.

### Proposed wiring (architecture-level; no code changes here)

```
function runStepEvent(areaId, x, y, facing, cellDescWord, areaMemory):
  // 1. Seed shared memory
  areaMemory[0x002d] = cellDescWord   // raw GEO cell word for vmRun1
  areaMemory[0x0011] = facing         // 0=N,1=E,2=S,3=W
  areaMemory[0x0034] = x
  areaMemory[0x0035] = y
  areaMemory[0x001b] = areaId

  // 2. Run vmRun1 via EclVm
  vmResult = runEclUntilStop(block, 'vmRun1', areaMemory)
  if vmResult.status === 'combat':
    return launchDqkCombat(vmResult.roster, ...)  // stop; don't run searchLoc

  // 3. Run searchLocation
  slResult = runEclUntilStop(block, 'searchLocation', areaMemory)
  if slResult.status === 'combat':
    return launchDqkCombat(slResult.roster, ...)

  // 4. Surface any text events
  return { text: [...vmResult.effects, ...slResult.effects].filter(e=>e.type==='print') }
```

**Critical implementation note:** `areaMemory[0x0097]` (encounter cooldown) must be
persisted in the area state between steps, not reset on each call. The ECL itself
decrements it; the host must store/restore it from `areaMemory`.

### `PARTYSTRENGTH` handling

**DONE (C4.2b-2b-2c-ii) — and the suggestion below was wrong.** `PARTYSTRENGTH` (0x1d) was a no-op;
it is now implemented in `vm.ts` as a **comparison-register setter** (`cmp = {left: hostPartyStrength,
right: operand}`), because the disassembly shows it is always followed by `IF_*` — it is a strength
*gate*, **not** a roster-filler. The earlier "populate `this.encounter[]` before `COMBAT`" suggestion
was based on the refuted Path-B reading; the roster comes from `LOAD_MONSTER`, not `0x1d`. See the §4
correction box for full detail.

### `SURPRISE` handling

The engine's `SURPRISE` (0x23) handler path should be verified. If a fight sub calls
`SURPRISE` before `COMBAT`, the VM returns `status: 'surprise'` (or similar) and the host
must resolve the surprise check before resuming the VM into `COMBAT`. The combat flow in
`launchDqkCombat` should accept a `surprised: boolean` parameter.

---

## 10. File paths

- ECL source: `The Dark Queen of Krynn/DISK3/ECL.GLB`
- ECL loader: `packages/engine/src/loaders/eclGlb.ts`
- ECL disassembler: `packages/engine/src/ecl/disassemble.ts`
- ECL VM: `packages/engine/src/ecl/vm.ts`
- Opcode table: `packages/engine/src/ecl/opcodes.ts`
- Explore wiring: `apps/web/src/ui/dqkExplore.ts`
- Gen2 header doc: `docs/engine/research/ecl-gen2-dqk.md`
- SearchLocation prior research: `docs/engine/research/dqk-search-location.md`
- Bestiary / combat wiring: `docs/engine/research/moncha-dqk.md`
