# Krynn Gen1 ECL Opcode Table — Research Findings

**Target:** DOS Champions of Krynn (CoK) ECL bytecode dialect  
**Derived empirically** from `daxdump_extracted/EclDump.exe` (.NET 2.0 IL disassembly),  
ECL blocks in `Champions of Krynn/ECL1.DAX`, `ECL2.DAX`, `ECL3.DAX`, and  
`hackdocs_extracted/GEOEVENT.TXT`.  
**Author:** Greybox reverse-engineering research (automated session)  
**Date:** 2026-06-21

---

## Verdict

**PARTIAL — sufficient to implement a disassembler for the opcode layer.**

The complete opcode table (65 opcodes, 0x00–0x40) is confirmed from the IL of
`EclDump.exe` (`SetupCommandTable()` method), which is the COAB project's Gold Box
ECL dumper. The argument-encoding scheme is understood well enough to walk bytecode
forward and label every instruction. What remains uncertain:

- The semantic meaning of EclOpp type-codes 0x09 and 0x7F (they appear frequently in
  CoK blocks but are not documented in the EclDump source strings).
- The `ecl_initial_entry` header field does NOT point to an instruction opcode — it
  points to a data word embedded in the tail of the block. Do not use it as an
  execution entry point (see §3 and §4).
- Opcodes 0x1F ("notsure 0x1f") and 0x22 ("PARTY SURPRISE") have no handlers
  registered in EclDump, meaning the COAB author was also uncertain.

---

## 1. Source Materials

| Material | Path | Role |
|---|---|---|
| EclDump.exe IL | `daxdump_extracted/EclDump.il` | **Primary** — definitive opcode table |
| ECL1/2/3.DAX | `Champions of Krynn/ECL[1-3].DAX` | Raw bytecode to verify against |
| OPCODES.TXT | `hackdocs_extracted/OPCODES.TXT` | **Irrelevant for ECL** — this is x86 Intel machine code, not Gold Box ECL |
| SCRIPT.TXT | `hackdocs_extracted/SCRIPT.TXT` | Documents SCRIPT.GLB HLIB form structure (UA editor), not ECL bytecode |
| GEOEVENT.TXT | `hackdocs_extracted/GEOEVENT.TXT` | GEO event record layout; shows ECL→map linkage |

> **CRITICAL NOTE:** `OPCODES.TXT` in hackdocs documents the Intel 8086 instruction
> set for patching `CKIT.EXE`. It has nothing to do with Gold Box ECL opcodes. The
> real ECL opcode table is only recoverable from `EclDump.exe`.

---

## 2. Opcode Table (Krynn Gen1 / CoK dialect)

Source: `SetupCommandTable()` in `daxdump_extracted/EclDump.il`, lines 1548–2312.  
Pattern: `ldc.i4 <opcode>; ldc.i4 <wordSet>; ldstr "<NAME>"; ldftn CMD_*; newobj CmdItem`.

`wordSet` = number of EclOpp argument-sets that follow the 1-byte opcode.  
Each EclOpp argument-set is 2 or 3 bytes (see §3).

| Opcode | wordSet | Mnemonic | Notes |
|--------|---------|----------|-------|
| 0x00 | 0 | EXIT | End of subroutine / halt |
| 0x01 | 1 | GOTO | Unconditional jump |
| 0x02 | 1 | GOSUB | Call subroutine |
| 0x03 | 2 | COMPARE | Compare two values; result consumed by IF_* |
| 0x04 | 3 | ADD | Arithmetic add |
| 0x05 | 3 | SUBTRACT | |
| 0x06 | 3 | DIVIDE | |
| 0x07 | 3 | MULTIPLY | |
| 0x08 | 2 | RANDOM | Random number |
| 0x09 | 2 | SAVE | Store to variable |
| 0x0A | 1 | LOAD CHARACTER | Load party member data |
| 0x0B | 3 | LOAD MONSTER | Load monster into encounter slot |
| 0x0C | 3 | SETUP MONSTER | Configure monster stats |
| 0x0D | 0 | APPROACH | Trigger approach sequence |
| 0x0E | 1 | PICTURE | Display picture/portrait |
| 0x0F | 2 | INPUT NUMBER | Player numeric input |
| 0x10 | 2 | INPUT STRING | Player text input |
| 0x11 | 1 | PRINT | Print text (no clear) |
| 0x12 | 1 | PRINTCLEAR | Print text and clear box (same handler as PRINT) |
| 0x13 | 0 | RETURN | Return from GOSUB |
| 0x14 | 4 | COMPARE AND | Compound comparison (4 args) |
| 0x15 | 0 | VERTICAL MENU | Show vertical choice menu |
| 0x16 | 0 | IF = | Conditional: equal |
| 0x17 | 0 | IF <> | Conditional: not equal |
| 0x18 | 0 | IF < | Conditional: less than |
| 0x19 | 0 | IF > | Conditional: greater than |
| 0x1A | 0 | IF <= | Conditional: less or equal |
| 0x1B | 0 | IF >= | Conditional: greater or equal |
| 0x1C | 0 | CLEARMONSTERS | Clear monster slots |
| 0x1D | 1 | PARTYSTRENGTH | Check/compute party strength |
| 0x1E | 6 | CHECKPARTY | Query party member attributes (6 args) |
| 0x1F | 2 | *(unknown)* | EclDump labels "notsure 0x1f"; no handler |
| 0x20 | 1 | NEWECL | Load new ECL module (chain to block by ID) |
| 0x21 | 3 | LOAD FILES | Load game data files |
| 0x22 | 2 | PARTY SURPRISE | EclDump has no handler registered; uncertain |
| 0x23 | 4 | SURPRISE | Surprise encounter setup |
| 0x24 | 0 | COMBAT | Start combat |
| 0x25 | 2 (+count) | ON GOTO | Computed/indexed jump — see §ON_GOTO below |
| 0x26 | 2 (+count) | ON GOSUB | Computed/indexed call (same dispatch as ON GOTO) |
| 0x27 | 8 | TREASURE | Define treasure (8 args) |
| 0x28 | 3 | ROB | Rob party |
| 0x29 | 14 | ENCOUNTER MENU | Full encounter dialog (14 args — largest) |
| 0x2A | 3 | GETTABLE | Look up table value |
| 0x2B | 0 | HORIZONTAL MENU | Show horizontal choice menu |
| 0x2C | 6 | PARLAY | Parlay/conversation sequence |
| 0x2D | 1 | CALL | Call external routine |
| 0x2E | 5 | DAMAGE | Apply damage to party |
| 0x2F | 3 | AND | Bitwise AND |
| 0x30 | 3 | OR | Bitwise OR |
| 0x31 | 0 | SPRITE OFF | Hide map sprite |
| 0x32 | 1 | FIND ITEM | Search for item in inventory |
| 0x33 | 0 | PRINT RETURN | Print with return/newline |
| 0x34 | 1 | ECL CLOCK | Timer / clock operation |
| 0x35 | 3 | SAVE TABLE | Save to data table |
| 0x36 | 1 | ADD NPC | Add NPC to party |
| 0x37 | 3 | LOAD PIECES | Load sprite/piece data |
| 0x38 | 1 | PROGRAM | Run program/macro |
| 0x39 | 1 | WHO | Who query (character selection) |
| 0x3A | 0 | DELAY | Time delay |
| 0x3B | 3 | SPELL | Cast spell |
| 0x3C | 1 | PROTECTION | Apply protection effect |
| 0x3D | 0 | CLEAR BOX | Clear text box |
| 0x3E | 0 | DUMP | Debug dump |
| 0x3F | 1 | FIND SPECIAL | Find special item/location |
| 0x40 | 1 | DESTROY ITEMS | Remove items from inventory |

**Confidence:** HIGH for all entries (sourced directly from EclDump.exe IL).  
Gaps: 0x1F (no handler, unknown semantics), 0x22 (no handler).

---

## 3. Instruction-Framing Spec

### 3.1 Block container

ECL blocks live in DAX container files (`ECL1.DAX`, `ECL2.DAX`, `ECL3.DAX`).  
Standard DAX format: `{uint16 toc_bytes, N×9-byte TOC entries, compressed data}`.  
Each TOC entry: `{uint8 block_id, uint32 data_offset, uint16 raw_size, uint16 comp_size}`.  
RLE decompression: signed-byte run-length (see `docs/dax-format.md`).

ECL1 block IDs: 16, 17, 18, 32, 33, 34, 36 (area 1)  
ECL2 block IDs: 48, 49, 50, 57, 64, 66, 67, 68 (area 2)  
ECL3 block IDs: 80, 81, 82, 96, 97, 98, 99 (area 3)

### 3.2 Block layout (decompressed) — CORRECTED (EclDump ground truth)

> **Correction (2026-06-21):** an earlier revision of this doc described a **20-byte /
> five-pointer** header and claimed `ecl_initial_entry` was not a code address. That was
> wrong and shifted every jump target by 2. The verified layout (cross-checked against
> `daxdump_extracted/ECL1_018.txt` and confirmed identical across blocks 16/17/32/48/80) is
> a **22-byte header: a 2-byte magic word + FIVE 4-byte far pointers**, and the fifth
> pointer `ecl_initial_entry` IS the real code entry (always `0x8014`).

```
offset   size   field
0        2      magic word = 0x1388 (game-global VM dispatch constant)
2        20     five 4-byte far pointers: {uint16 segment 0x0101, uint16 vaddr}
22+      ...    bytecode body (entry = byte 22 = virtual 0x8014)
```

The five far pointers (vaddr word of each; segment is always `0x0101`):

```
ptr[0]  vm_run_1          → this block's main VM-run sub (varies per block; e.g. 0x8085)
ptr[1]  SearchLocation    → entry of "search this location" sub in this block
ptr[2]  PreCampCheck      → entry of "pre-camp check" sub in this block
ptr[3]  CampInterrupted   → entry of "camp interrupted" sub in this block
ptr[4]  ecl_initial_entry → CODE ENTRY, always 0x8014 (= byte 22, right after the header)
```

### 3.3 Virtual addressing

The game loads each ECL block at segment `0x0101`. Byte 0 (the magic word) maps to virtual
base `0x7FFE`, so that the entry at byte 22 lands on the canonical `0x8014`.  
Thus: `virtual_address = 0x7FFE + raw_byte_index` and `raw_byte_index = virtual_addr - 0x7FFE`.

**Correct execution entry point:** byte index 22 (virtual `0x8014`), immediately after the
22-byte header — and equal to the `ecl_initial_entry` pointer in every block observed.

The hook entries `vm_run_1`, `SearchLocation`, `PreCampCheck`, and `CampInterrupted` are
valid code addresses for their respective subroutines (e.g. in block 018 `vm_run_1 = 0x8085`
is `GOTO 0x8014`). If a block does not implement a hook, it typically places a single
`EXIT (0x00)` byte at that address (block 018: `PreCampCheck`/`CampInterrupted` → EXIT).

### 3.5 `SearchLocation` + `ON_GOTO` dispatch — the per-step event path  CONFIRMED (M2.S3)

`SearchLocation` (far pointer 1) is the routine the engine runs **every time the party enters a
cell**. It dispatches on the current cell's **wall/roof** value — the GEO plane-2 backdrop byte,
held in the game-data variable `mapWallRoof` at absolute address **`0x7f79`** — through an
`ON_GOTO` jump table. Verified in ECL1 block 34 (`SearchLocation = 0x81DE`):

```
0x81EF  AND   word_6F2 = mapWallRoof(0x7f79) & 63          ; low 6 bits = event index
0x81F8  ON_GOTO mapWallRoof of [0x8287, 0x83B4, 0x8454, 0x81DD, 0x81DD, 0x8546, …]  ; 35 targets
```

`ON_GOTO` / `ON_GOSUB` operand layout (matches `wordSet 2 + countAt 1`): `a[0]` = selector,
`a[1]` = target count, `a[2..2+count)` = jump targets. **The selector is 0-based** — value `v`
branches to `a[2 + v]`; a value outside `[0, count)` takes no branch and falls through. Proof of
0-based: open floor has backdrop `0` (212 / 256 cells in GEO 34), and target 0 = `0x8287` = the
wandering-monster check (RANDOM 1..99 + compares) — exactly what should run on a normal floor
step. Sentinel target `0x81DD` (one byte before `SearchLocation`) = "no event" for that index.

Engine wiring: `world/dungeonEvents.ts primeSearchLocation(block, backdrop, opts)` positions an
`EclVm` at `searchLocation − 0x7FFE` with `mem[0x7f79] = backdrop & 0x3f`; the host runs it per
step and resumes on COMBAT/menu pauses. `MapCell.floor` carries the backdrop. (`ON_GOTO` dispatch
is implemented in `ecl/vm.ts`; the VM no longer halts there.)

### 3.4 EclOpp argument encoding

Each instruction is: `1 byte opcode + wordSet × EclOpp-set`.  
Each EclOpp argument-set is **2 or 3 bytes**:

```
byte 0  code       : type indicator (determines size)
byte 1  high_init  : initial high byte or value
byte 2  val        : (ONLY present if code ∈ {1, 2, 3})
```

**3-byte form** (code in {1, 2, 3}):  
Read `code`, `high_init`, then one more byte `val`.  
`SetHigh(val)` replaces `high_init` with `val`.  
Result word = `(val << 8) | code`... but the actual decoded value appears to be
`(high_init << 8) | val` based on observed virtual addresses (e.g., `01 14 80` decodes
to address 0x8014 when code=0x01, high=0x14, val=0x80, yielding address=0x8014).

Observed code values and their apparent meanings:
- `0x00` — Literal immediate: value = `high_init` byte. E.g., `00 44` = literal 68.
- `0x01` — Virtual address / direct value: `(val << 8) | high_init` (little-endian address).
             E.g., `01 14 80` = virtual address `0x8014`.
- `0x02` — Another variable reference form (semantics: see EclDump `CMD_*` handlers).
- `0x03` — Another variable reference form.
- `0x09` — Flag/predicate byte (frequent in COMPARE_AND; exact type unclear).
- `0x7F` — Variable-type operand (appears in SAVE instructions referencing game variables).
- `0x80` — String table reference: `high_init` = string table index.
- `0x81` — Inline string copied from memory.

**2-byte form** (all other codes): `code` + `high_init`, no third byte.

### 3.5 Decode-loop pseudocode

```
function walk_ecl_block(raw_bytes):
    pos = 22                    # skip 22-byte header (magic + 5 far pointers)
    while pos < len(raw_bytes):
        opcode = raw_bytes[pos]; pos += 1
        if opcode not in OPCODE_TABLE:
            error("unknown opcode 0x{opcode:02x} at byte {pos-1}")
        n_args = OPCODE_TABLE[opcode].wordSet
        args = []
        for i in range(n_args):
            code = raw_bytes[pos]; pos += 1
            high = raw_bytes[pos]; pos += 1
            if code in {1, 2, 3}:
                val = raw_bytes[pos]; pos += 1
                args.append(EclOpp(code, high, val))
            else:
                args.append(EclOpp(code, high, None))
        emit(opcode, args)
        if opcode == 0x00:      # EXIT ends the current subroutine
            return
```

---

## 4. Worked Example — ECL1 Block 018

Block 018 is the smallest ECL block in CoK (144 bytes decompressed). It is a
"zone transition" block that checks certain conditions and chains to other ECL
modules.

### 4.1 Header (22 bytes — magic + five far pointers)

```
Bytes 0x00-0x15 (22-byte header):
  88 13        → magic:             0x1388       (game-global VM dispatch constant)
  01 01 85 80  → vm_run_1:          0x8085:0101  → 0x8085 is GOTO 0x8014
  01 01 89 80  → SearchLocation:    0x8089:0101  → 0x8089 is GOTO 0x8014
  01 01 83 80  → PreCampCheck:      0x8083:0101  → byte[133] = EXIT
  01 01 84 80  → CampInterrupted:   0x8084:0101  → byte[134] = EXIT
  01 01 14 80  → ecl_initial_entry: 0x8014:0101  → byte[22] = CODE ENTRY (SAVE)
```

### 4.2 Disassembly (from byte 22, virtual 0x8014) — verified vs EclDump

```
; Main subroutine — starts at 0x8014 (byte 22 = first byte after the 22-byte header).
; virtual = 0x7FFE + raw_byte_index. Cross-checked against daxdump_extracted/ECL1_018.txt.
8014h  09 00 00 01 e6 4b               SAVE         #0,  addr:0x4be6   ; inDungeon = 0
801ah  09 00 01 01 38 4c               SAVE         #1,  addr:0x4c38   ; word_270 = 1
8020h  09 00 12 01 f2 4b               SAVE         #18, addr:0x4bf2   ; LastEclBlockId = 18
8026h  03 01 04 4c 00 01               COMPARE      addr:0x4c04 (word_208), #1
802ch  16                              IF_EQ
802dh  01 01 40 80                     GOTO         0x8040
8031h  09 00 01 01 04 4c               SAVE         #1,  addr:0x4c04   ; word_208 = 1
8037h  09 00 7f 01 a8 7e               SAVE         #127, var:0x7ea8   ; training_class_mask
803dh  38 00 00                        PROGRAM      #0                 ; StartGameMenu
8040h  09 00 00 01 38 4c               SAVE         #0,  addr:0x4c38   ; word_270 = 0
8046h  09 00 01 01 e6 4b               SAVE         #1,  addr:0x4be6   ; inDungeon = 1
804ch  03 01 b1 4c 00 00               COMPARE      addr:0x4cb1 (word_362), #0
8052h  16                              IF_EQ
8053h  01 01 60 80                     GOTO         0x8060
8057h  09 00 05 01 12 7f               SAVE         #5,  var:0x7f12    ; game_area = 5
805dh  20 00 50                        NEWECL       #80        ; chain to ECL3 block 80
8060h  03 01 a6 4c 00 01               COMPARE      addr:0x4ca6 (word_34C), #1
8066h  16                              IF_EQ
8067h  01 01 74 80                     GOTO         0x8074
806bh  09 00 06 01 12 7f               SAVE         #6,  var:0x7f12    ; game_area = 6
8071h  20 00 61                        NEWECL       #97        ; chain to ECL3 block 97
8074h  09 00 00 01 a6 4c               SAVE         #0,  addr:0x4ca6   ; word_34C = 0
807ah  09 00 04 01 12 7f               SAVE         #4,  var:0x7f12    ; game_area = 4
8080h  20 00 44                        NEWECL       #68        ; chain to ECL2 block 68
8083h  00                              EXIT  (PreCampCheck hook)
8084h  00                              EXIT  (CampInterrupted hook)
8085h  01 01 14 80                     GOTO 0x8014  (vm_run_1 hook)
8089h  01 01 14 80                     GOTO 0x8014  (SearchLocation hook)
```

**Reading this block:** an area dispatcher. It records `inDungeon`/`LastEclBlockId`, runs
`StartGameMenu` on first entry (`word_208`), then routes by area flag: `word_362` (0x4cb1)
set → chain to ECL 80 (area 5); else `word_34C` (0x4ca6) set → chain to ECL 97 (area 6);
else default → chain to ECL 68 (area 4). With all flags 0 the default fires (area 6 → ECL
97), because `0==word_362` short-circuits the area-5 branch and `1==word_34C` is false.

### 4.3 Note on `ecl_initial_entry` (corrected)

`ecl_initial_entry = 0x8014` (the fifth far pointer) IS the real code entry — byte 22, the
first `SAVE`. The earlier claim that this slot pointed at a NEWECL argument byte was an
artifact of a 20-byte/5-pointer mis-framing that dropped the magic word and shifted every
read by one pointer (2 bytes); it has been retired. Across blocks 16/17/32/48/80 the slot is
always `0x8014`.

---

## 5. GEO → ECL Linkage

From `hackdocs_extracted/GEOEVENT.TXT`:

GEO data files (`GEO*.DAX`) contain event records at a fixed offset.  
Each event record is **20 bytes**:

```
byte  0   event_type        (0-38; see SCRIPT.TXT for type names)
byte  1   flags
byte  2   condition_byte
byte  3   chain_to_event    (event index to chain to; 0xFF = none)
bytes 4+  type-specific fields (combat params, text ID, ECL block ID, etc.)
```

Event types that trigger ECL:
- **Type 1 (Combat):** contains a combat group ID; the post-combat ECL block ID is
  in the event data.
- **Type 2 (Text Statement):** text ID; may chain ECL for follow-up.
- **Type 11 (Transfer Module):** contains ECL block ID directly.
- **Type 16 (Utilities):** ECL script block ID.
- Various others that reference ECL blocks for custom logic.

The ECL block IDs referenced from GEO events match the block IDs in ECL1/2/3.DAX
(sparse IDs 16-36, 48-68, 80-99). The game dispatches to the correct ECL block
when the player triggers the event on the map.

---

## 6. For the Implementer

### 6.1 Proposed `EclOp` data shape

```typescript
interface EclOpp {
  code: number;       // type indicator byte (0x00, 0x01-0x03, 0x7F, 0x80, 0x81, ...)
  high: number;       // second byte (initial; may be replaced by val for code in {1,2,3})
  val?: number;       // third byte, only present if code in {1, 2, 3}
  // derived:
  word(): number;     // decoded 16-bit value: see notes
  isAddr(): boolean;  // true if code == 0x01 (virtual address argument)
  isImm(): boolean;   // true if code == 0x00 (literal byte in high)
  isStrRef(): boolean;// true if code == 0x80
}

interface EclInstruction {
  offset: number;     // raw byte offset in block
  vaddr: number;      // virtual address (0x7FFE + offset; body entry at byte 22 = 0x8014)
  opcode: number;     // 0x00-0x40
  mnemonic: string;   // from OPCODE_TABLE
  args: EclOpp[];     // length == OPCODE_TABLE[opcode].wordSet
  size: number;       // total bytes: 1 + sum(2 or 3 per arg)
}

interface EclBlock {
  blockId: number;
  rawSize: number;
  vmRun1: number;        // always 0x1388
  searchLocation: number; // virtual addr of SearchLocation sub
  preCampCheck: number;  // virtual addr of PreCampCheck sub
  campInterrupted: number; // virtual addr of CampInterrupted sub
  eclInitialEntry: number; // NOT a code addr; tail NEWECL arg addr
  instructions: EclInstruction[];
}
```

### 6.2 Decode loop (reference implementation)

```python
OPCODE_TABLE = {
    0x00: (0, 'EXIT'),     0x01: (1, 'GOTO'),      0x02: (1, 'GOSUB'),
    0x03: (2, 'COMPARE'),  0x04: (3, 'ADD'),        0x05: (3, 'SUBTRACT'),
    0x06: (3, 'DIVIDE'),   0x07: (3, 'MULTIPLY'),   0x08: (2, 'RANDOM'),
    0x09: (2, 'SAVE'),     0x0A: (1, 'LOAD_CHAR'),  0x0B: (3, 'LOAD_MONSTER'),
    0x0C: (3, 'SETUP_MONSTER'), 0x0D: (0, 'APPROACH'), 0x0E: (1, 'PICTURE'),
    0x0F: (2, 'INPUT_NUM'), 0x10: (2, 'INPUT_STR'), 0x11: (1, 'PRINT'),
    0x12: (1, 'PRINTCLEAR'), 0x13: (0, 'RETURN'),   0x14: (4, 'COMPARE_AND'),
    0x15: (0, 'VERT_MENU'), 0x16: (0, 'IF_EQ'),     0x17: (0, 'IF_NE'),
    0x18: (0, 'IF_LT'),    0x19: (0, 'IF_GT'),      0x1A: (0, 'IF_LE'),
    0x1B: (0, 'IF_GE'),    0x1C: (0, 'CLEARMONSTERS'), 0x1D: (1, 'PARTYSTRENGTH'),
    0x1E: (6, 'CHECKPARTY'), 0x1F: (2, 'UNKNOWN_1F'), 0x20: (1, 'NEWECL'),
    0x21: (3, 'LOAD_FILES'), 0x22: (2, 'PARTY_SURPRISE'), 0x23: (4, 'SURPRISE'),
    0x24: (0, 'COMBAT'),   0x25: (0, 'ON_GOTO'),    0x26: (0, 'ON_GOSUB'),
    0x27: (8, 'TREASURE'), 0x28: (3, 'ROB'),         0x29: (14, 'ENCOUNTER_MENU'),
    0x2A: (3, 'GETTABLE'), 0x2B: (0, 'HORIZ_MENU'), 0x2C: (6, 'PARLAY'),
    0x2D: (1, 'CALL'),     0x2E: (5, 'DAMAGE'),      0x2F: (3, 'AND'),
    0x30: (3, 'OR'),       0x31: (0, 'SPRITE_OFF'),  0x32: (1, 'FIND_ITEM'),
    0x33: (0, 'PRINT_RETURN'), 0x34: (1, 'ECL_CLOCK'), 0x35: (3, 'SAVE_TABLE'),
    0x36: (2, 'ADD_NPC'),  0x37: (3, 'LOAD_PIECES'), 0x38: (1, 'PROGRAM'),
    0x39: (1, 'WHO'),      0x3A: (0, 'DELAY'),        0x3B: (3, 'SPELL'),
    0x3C: (1, 'PROTECTION'), 0x3D: (0, 'CLEAR_BOX'), 0x3E: (0, 'DUMP'),
    0x3F: (1, 'FIND_SPECIAL'), 0x40: (1, 'DESTROY_ITEMS'),
}

def disassemble_block(raw: bytes) -> list:
    """Disassemble a decompressed ECL block. raw is the full block including the 22-byte
    header. NOTE: this skeleton omits the count-driven opcodes (ON_GOTO/ON_GOSUB/menus) and
    the inline 0x80 6-bit packed strings — see packages/engine/src/ecl/disassemble.ts for
    the authoritative reader that handles both."""
    instructions = []
    pos = 22                          # skip 22-byte header (magic + 5 far pointers)
    while pos < len(raw):
        start = pos
        opcode = raw[pos]; pos += 1
        if opcode not in OPCODE_TABLE:
            raise ValueError(f"unknown opcode 0x{opcode:02x} at byte {start}")
        n_args, mnemonic = OPCODE_TABLE[opcode]
        args = []
        for _ in range(n_args):
            code = raw[pos]; pos += 1
            high = raw[pos]; pos += 1
            if code in (1, 2, 3):
                val = raw[pos]; pos += 1
                args.append({'code': code, 'high': high, 'val': val})
            else:
                args.append({'code': code, 'high': high})
        instructions.append({
            'offset': start,
            'vaddr': 0x7FFE + start,
            'opcode': opcode,
            'mnemonic': mnemonic,
            'args': args,
            'size': pos - start,
        })
        if opcode == 0x00:            # EXIT: one subroutine ends here
            break
    return instructions
```

### 6.3 Unresolved items

| Item | Status | How to resolve |
|------|--------|----------------|
| Opcode 0x1F | wordSet=2, no handler | Observe in real game trace; check `DQK.EXE` for clue |
| Opcode 0x22 | wordSet=2, no handler registered | Same as above |
| EclOpp code=0x09 | Appears in COMPARE_AND args; type unclear | Systematically scan all blocks; frequency analysis |
| EclOpp code=0x7F | Appears in SAVE args | Check EclDump string heap: may be `<stru_1B2CA.word_{0}>` style game variable |
| `ecl_initial_entry` field purpose | NOT an execution entry | Run EclDump with verbose tracing to observe what the game engine reads |
| Exact `word()` assembly for code∈{1,2,3} | Partially clear: `(val<<8)\|high` gives the virtual address correctly for 0x01 args | Verify with code=2,3 args in larger blocks |

### 6.4 Dialect note: OPCODES.TXT

`hackdocs_extracted/OPCODES.TXT` is a reference for the Intel 8086 instruction set
used when hand-patching `CKIT.EXE` (the Gold Box Construction Kit engine).  
**It is not an ECL opcode reference.** The COAB project's EclDump.exe is the only
known authoritative source for the ECL opcode table in the Krynn games.

---

## Summary (≤300 words)

**Verdict: PARTIAL.** The CoK ECL dialect is understood well enough to implement a
complete forward-walking disassembler. All 65 opcodes (0x00–0x40) are confirmed from
the IL decompilation of `EclDump.exe` (`SetupCommandTable()`), which is the COAB
project's own disassembler for Gold Box ECL.

**Confirmed count:** 65 opcodes. 63 with handlers; 2 gaps (0x1F "notsure", 0x22 no handler).

**Instruction framing:** 1-byte opcode + N × EclOpp argument-sets. Each argument-set
is 2 bytes (`code` + `high`) or 3 bytes if `code ∈ {1,2,3}` (extra `val` byte via
`SetHigh()`). Count-driven opcodes (ON_GOTO/ON_GOSUB/menus) carry `count` extra operands
after their fixed ones, and `code 0x80` operands are followed inline by `high` bytes of
6-bit packed text. The block starts with a **22-byte header (a 0x1388 magic word + 5 far
pointers)**; execution starts at byte 22 (virtual `0x8014`).

**Entry point (resolved):** The `ecl_initial_entry` header field (fifth far pointer) IS the
real code entry — always `0x8014` = byte 22, immediately after the header. An earlier
revision of this doc mis-framed the header as 20 bytes / 5 pointers (dropping the magic
word), which made every read land one pointer early and shifted all jump targets by 2; that
is fixed. EclOpp type-codes 0x09 and 0x7F appear frequently in CoK blocks but are not
documented in EclDump's string heap — their exact semantics require a game-trace pass or
examination of the original 8086 VM interpreter in `START.EXE`.

The worked example (block 018, 144 bytes) disassembles fully with no unknown opcodes and
produces coherent code matching EclDump byte-for-byte: an area-transition script that records
state, runs StartGameMenu on first entry, and chains to blocks 80, 97, or 68 by area flag via
`NEWECL`. All encountered opcodes (SAVE, COMPARE, IF_EQ, GOTO, PROGRAM, NEWECL, EXIT) are in
the confirmed table.
