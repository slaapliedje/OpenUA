# Decompilation sources & methodology — where to look, what to extract

Synthesis of a 2026-06-21 research sweep (4 parallel web-research passes) into the existing
reverse-engineering prior-art for the SSI Gold Box engine, plus a direct inspection of this repo's
DOS binaries. Supersedes scattered notes; pair with `docs/findings.md`.

> **Clean-room posture (unchanged):** we **study** these references and use the tools as **test
> oracles**, but we do **not** copy GPL or unlicensed source into the MIT/TS engine. Only
> **Gold Box Explorer** is permissively (MIT) licensed. COAB, Dungeon Craft, DaxDump/EclDump,
> TLButil2 are study-only.

---

## ★ The Rosetta Stone: `simeonpilgrim/coab`

**https://github.com/simeonpilgrim/coab** — a ~feature-complete C#/C re-implementation of **Curse of
the Azure Bonds** (1989), **decompiled directly from the DOS executable**, with original
`seg600:XXXX` / `sub_XXXXX` addresses preserved as comments. CoAB is the Gold Box generation
**immediately before** the Krynn games and shares the same Borland/overlay engine lineage, so its
structure transfers (values must be re-derived from the Krynn binaries — "ECL opcode drift").

No LICENSE file → **study-only**, but it is the single most valuable reference we have. Key files:

| File | Contents we need |
| ---- | ---------------- |
| `engine/ovr003.cs` | **The ECL VM.** `SetupCommandTable()` registers every opcode (value, arg-count, name, handler + original `sub_` address). Dispatch: `command = ecl_ptr[offset + 0x8000]`. ECL block size `0x1E00`, 16-bit addressed (`index & 0xFFFF`). |
| `engine/ovr008.cs` | ECL **string compression/decompression** + VM init. Control bytes `0x80` = load compressed string, `0x81` = load string from memloc. |
| `engine/ovr018.cs` | **The AD&D-1e tables, as SSI baked them:** `thac0_table[class,level]` (`seg600:3E3A`), `exp_table[class,level]` (`seg600:4293`), `con_hp_adj[]`, `classMasks = {2,2,8,0x10,0x20,1,4,4}`, `hp_calc_table[]`. |
| `engine/ovr024.cs` | `RollSavingThrow`: d20, nat-1 always fails / nat-20 always succeeds, else `roll + bonus + field_186 >= player.saveVerse[type]`. |
| `engine/ovr026.cs` | `reclac_saving_throws(player)` → fills the 5-entry `saveVerse[]` (the per-class/level save matrix). |
| `Classes/ItemData.cs` | **16-byte item record** (CoAB): `+0 item_slot, +1 handsCount, +2 diceCountLarge, +3 diceSizeLarge, +4 bonusLarge, +5 numberAttacks, +9 diceCountNormal, +A diceSizeNormal, +B bonusNormal, +C range, +D classFlags, +E ItemDataFlags, +F field_F`. |
| `Classes/Player.cs` | Character/monster (.CCH) struct: `thac0 @0x73, race @0x74, _class @0x75`, `field_186` (save bonus), `saveVerse[]`, `hitBonus`. Monsters share this struct. |
| `Classes/{GeoBlock,SpellList,Spells,Combat,DataIO}.cs` | GEO maps, spell records, combat sort, DAX read primitives. |
| `Data/` | The actual CoAB `.DAX` files — a **labelled corpus** to validate decoders. |
| `coab_new.idc` / `coad_db.idc` / `.lst` | IDA database/scripts: symbol names + struct layouts + EXE data-table addresses. |

### ✅ It independently confirms this session's monster work
`seg043.cs` shows the engine **"displays THAC0 as `0x3C - hitBonus`"** — `0x3C = 60`. That is exactly
the `60 − byte` inversion we reverse-engineered for CoK monster **THAC0 (byte 89)** and **AC (byte
275)**. Independent corroboration that the decode is correct. Likewise `diceCount/diceSize/bonus`
item damage fields mirror our monster damage triple (269 d 271 + 270).

### 🔑 Lead for the CoK item base-stats table
CoAB item records **do** store damage as `diceCount/diceSize/bonus` (small & large) + `classFlags` +
slot. CoK's records are 63 bytes (not 16) and we found base damage is **type-keyed** there — but the
COAB field set is the template to look for, both in the CoK 63-byte record's unmapped region **and**
in the in-EXE base-item table. **Action: clone coab, read `ItemData.cs` + the `ovr018`/`ovr024`/
`ovr026` tables in full, and diff against our correlation findings.**

---

## ECL opcode table (CoAB, from `ovr003.cs`) — diff target for Krynn-Gen1

```
0x00 EXIT          0x11 PRINT          0x22 PARTY SURPRISE  0x33 PRINT RETURN
0x01 GOTO          0x12 PRINTCLEAR     0x23 SURPRISE        0x34 ECL CLOCK
0x02 GOSUB         0x13 RETURN         0x24 COMBAT          0x35 SAVE TABLE
0x03 COMPARE       0x14 COMPARE AND    0x25 ON GOTO         0x36 ADD NPC
0x04 ADD           0x15 VERTICAL MENU  0x26 ON GOSUB        0x37 LOAD PIECES
0x05 SUBTRACT      0x16 IF =           0x27 TREASURE        0x38 PROGRAM
0x06 DIVIDE        0x17 IF <>          0x28 ROB             0x39 WHO
0x07 MULTIPLY      0x18 IF <           0x29 ENCOUNTER MENU  0x3A DELAY
0x08 RANDOM        0x19 IF >           0x2A GETTABLE        0x3B SPELL
0x09 SAVE          0x1A IF <=          0x2B HORIZONTAL MENU 0x3C PROTECTION
0x0A LOAD CHARACTER0x1B IF >=          0x2C PARLAY          0x3D CLEAR BOX
0x0B LOAD MONSTER  0x1C CLEARMONSTERS  0x2D CALL            0x3E DUMP
0x0C SETUP MONSTER 0x1D PARTYSTRENGTH  0x2E DAMAGE          0x3F FIND SPECIAL
0x0D APPROACH      0x1E CHECKPARTY     0x2F AND             0x40 DESTROY ITEMS
0x0E PICTURE       0x1F (2 args)       0x30 OR
0x0F INPUT NUMBER  0x20 NEWECL         0x31 SPRITE OFF
0x10 INPUT STRING  0x21 LOAD FILES     0x32 FIND ITEM
```
**Correction to an earlier note:** 0x2C = **PARLAY**, not "ENCOUNTER" (combat = 0x24 / 0x29). The
Krynn games (newer) likely append opcodes past 0x40 and may reshuffle 0x3x — diff our disassembler's
arg-counts and semantics against this table to quantify drift.

---

## Toolchain — established from THIS repo's binaries (not inference)

`strings` on the actual files settles the decompilation approach:

| Binary | Signature | Verdict |
| ------ | --------- | ------- |
| CoK/DoK `START.EXE` | `Portions Copyright (c) 1983,91 Borland`, `Overlay error…`, `Please insert overlay disk.` | **Borland C/C++, real-mode MZ, VROOMM overlays** |
| CoK/DoK `GAME.OVR` | magic `FBOV`, `WARNING: Insufficient Memory` | **Borland FBOV/VROOMM overlay container** |
| `DQK.EXE` | `eov0001:`…`eov0009:`, `Overlay Manager Internal Reload Stack…`, `Cannot find overlay file` | **Borland overlaid runtime; NO separate `.OVR`** — overlays served from the `.TLB`/`.GLB` HLIB containers at runtime |

Consequences: all three are **Borland real-mode** (no DOS extender/DPMI). CoK/DoK overlays are a
self-describing `FBOV` container; DQK's overlay code lives inside the HLIB chunks we already decode.
Calling convention = Borland `__cdecl` default / `__pascal` (`-p`) for engine code (watch `RET n`).

### FBOV overlay header (`GAME.OVR`)
```
char[4] "FBOV"   uint32 ovrsize   uint32 exeinfo(→segtable)   int32 segnum
segtable @exeinfo, 8 bytes/entry: uint16 seg, maxoff, flags, minoff
```
Parse it like our DAX/HLIB readers and carve each overlay → disassemble as `x86:LE:16:Real Mode`.
(CoK `ovrsize` = 0x00041adf, DoK = 0x0003da2d.) Or open in **IDA Free 5.0**, which auto-segments FBOV.

### Static-analysis tooling
- **Ghidra** + **GhidraDosToolbox** (https://github.com/Gravelbones/GhidraDosToolbox) — DOS loader,
  per-segment CS assumptions, PSP/IVT, `dos_vs6_16` types. Language ID `x86:LE:16:Real Mode:default`.
  Trust CFG, distrust its segmented-pointer C output (known bugs at high offsets).
- **IDA Free 5.0** — last free IDA that disassembles MZ **and** auto-creates FBOV overlay segments
  (mirror: `https://www.scummvm.org/frs/extras/IDA/idafree50.exe`).
- Fallbacks: radare2, Semblance, TatraDAS, Reko.
- **Spice86** (https://github.com/OpenRakis/Spice86) — execute the real binary, overlay recovered
  C-structs onto live memory via `--StructureFile`, generate C# overrides — a **diff oracle** to
  prove our TS damage/save calc matches the original.

### Finding the base-item / THAC0 / save tables (highest-yield path)
1. **Carve** code/data: CoK/DoK = `START.EXE` + each FBOV overlay from `GAME.OVR`; DQK = `DQK.EXE` +
   HLIB code chunks. (Reuse our container code.)
2. **Static value-correlation scan** (our existing strength, applied to EXE bytes): a fixed-stride
   struct array whose damage-dice/AC fields match known item values → stride = `sizeof(struct)`; a
   monotonic descending byte ramp → THAC0; a small matrix of 3–20-range bytes with monotonic rows →
   saving throws.
3. **Disassemble around candidates** (IDA/Ghidra) to confirm the indexing math (`[index*stride +
   base]`) and field meanings.
4. **Runtime-prove** in **DOSBox-X** (`MEMFIND`/`MEMS` to locate a known stat, `BPPM`/`BPM`
   watchpoint, `MEMDUMPBIN seg off n` to dump the table) or **dosdebug** (GDB read-watchpoints) —
   catch the code that reads the table during an attack, map `seg:off` back to the file offset.

---

## Format tools & docs (cross-checks / oracles)

- **Gold Box Explorer** — https://github.com/bsimser/Gold-Box-Explorer (MIT; fork
  `simeonpilgrim/goldboxexplorer`). The **only permissively-licensed** parser (DAX incl.
  compression, ECL decrypt, monster/item/GEO, FRUA TLB). Clone `GoldBoxExplorer.Lib/` to read its
  DAX TOC/RLE + ECL routines — legally adaptable.
- **DaxDump / EclDump** (Simeon) — https://gbc.zorbus.net/ — our existing byte-identical DAX/ECL
  oracle.
- **TLButil2** (Itamar / Dan Autery) — https://gbc.zorbus.net/ — the only third-party **`.TLB`/HLIB**
  extractor; reference for our HLIB **repacker** (roadmap step 4, our weakest-tooled format).
- **Gold Box Companion** (Joonas, zorbus.net) — closed tool, but ships **`formats.zip`** (save +
  effect + item docs) and **per-class/per-game THAC0/XP/HD/spell-progression text tables for all 12
  games incl. CoK/DoK/DQK** — the cleanest human-readable cross-check for the in-EXE tables. Built-in
  **ECL-monitor** (live opcode/flag trace) and **DAX repacker** (jhirvonen's near-byte-perfect RLE
  encoder — useful for our writer). **Download `formats.zip` + the level tables directly.**
- **Local `hackdocs_extracted/`** already has: `TLBFORM.TXT` (full HLIB/TLB structure — `"HLIB"`,
  size, ptr count, palette magic @0xB, `"TILE"`, pointer array), `SCRIPT.TXT` (FRUA event-type
  enum), `CCHFORM.TXT`, `ITEM*.TXT`, `SAVGAM.TXT`, `GEO*.TXT`, `MONST*.TXT`, `SPEL*.TXT`,
  `SLOT*.TXT`. **Note:** local `OPCODES.TXT` is a generic x86 CPU reference, **NOT** an ECL table —
  the ECL semantics live in COAB `ovr003.cs` and GBC's ECL-monitor.

## Community / people
- **Forums** https://forums.goldbox.games/ — DAX format (topic 1073: 9-byte TOC records, RLE
  bottom-right fill, GEO RLE quirk), DAX **repacking** (topic 3148: full RLE encode, jhirvonen),
  ECL contents (topic 1241). **Wiki** https://wiki.goldbox.games/ is per-game/play-oriented, light
  on formats. **Simeon Pilgrim's blog** https://simeonpilgrim.com/blog/curse-of-the-azure-bonds.
- People: **Simeon Pilgrim** (COAB, DaxDump/EclDump, GB Explorer), **Joonas/zorbus** (GBC,
  ECL-monitor), **bsimser** (GB Explorer MIT), **jhirvonen** (DAX repacker), **manikus / Ishad Nha**
  (DAX TOC), **Itamar / Dan Autery** (TLButil2), **grannypron** (Dungeon Craft).
  (Project policy: **no third-party outreach** — clean-room, reference only.)

## FRUA-clone lineage (mechanics reference, GPL — behaviour only, never copy)
- **Dungeon Craft / UAF** — https://github.com/grannypron/uaf (GPL-2.0, C++, semi-active, has an
  experimental **WebGL browser build** = prior art for our secondary browser goal). Implements Gold
  Box combat/spell/movement + the FRUA event model from scratch.

## Cross-platform art (best-of goal)
- Confirmed: **Amiga best for CoK & DoK**, **DOS/Mac VGA best for DQK**. Amiga ports by **Westwood**
  added **content** that has *no DOS equivalent* — custom combat sprites, character icons,
  **animations** (Amiga is the only animated platform). So "best-of art" is not just a palette
  upscale; some Amiga assets must be lifted wholesale. CoK also shipped Apple II + C64 + PC-98
  (16-colour, ignore); DoK adds PC-98; **DQK has a Mac 256-colour (chunky) build** = clean fallback
  source if any DOS asset is damaged. DQK by **MicroMagic** (explains the HLIB/VGA generation jump).
- **Conversion prior-art (Amiga DAA → HLIB TILE, roadmap step 5):** port **libamivideo**
  (https://github.com/svanderburg/libamivideo) — planar→chunky + EHB/HAM + **4-bit→8-bit palette
  scaling**. Use nibble-replicate `c8 = (c4<<4)|c4`; for HLIB's **6-bit DAC** use `c6 =
  round(c4*63/15)`. Add an **IFF/ILBM `.LBM`** reader (spec: https://wiki.amigaos.net/wiki/ILBM_IFF,
  reference decoder https://github.com/svanderburg/libilbm) for Amiga title/standalone screens —
  `FORM/BMHD/CMAP/BODY`, PackBits, interleaved-by-row planes — a solved format, distinct from the
  still-unsolved `.DAA` sub-frame variants.

## Gaps (no public prior-art — genuinely our own work)
- **Amiga `.DAA`** format: no third-party tool/doc exists anywhere; our `daa_decode.py` is the only
  decoder. The unsolved 6-byte `SPRIT*/PIC*/HEAD*` sub-frame inner encoding stays novel.
- **Krynn-specific ECL opcode table:** COAB covers CoAB; no published CoK/DoK/DQK opcode diff exists.
- **HLIB *writing*/repacking:** TLButil2 only reads; no open HLIB repacker exists.

---

## Recommended next actions (priority order)
1. **Clone `simeonpilgrim/coab`** (study-only) and transcribe `ovr018` (THAC0/XP/con-HP), `ovr024`+
   `ovr026` (saving throws), `ItemData.cs`, `Player.cs` offsets — cross-check against our correlation
   findings; this is the fastest route to the PC THAC0/save tables (M2.S3b) and the base-item table.
2. **Diff the ECL opcode table** above against our Krynn-Gen1 disassembler to map opcode drift.
3. **Read the Journal PDF PC tables via pdfplumber char-extraction** (recovers digits 2–8 that
   pdftotext drops) as a second, original-source cross-check for #1.
4. Clone **Gold Box Explorer** (MIT) for a legally-adaptable DAX/ECL reference.
5. Static-scan `GAME.OVR`/`DQK.EXE` for the base-item and to-hit/save tables (FBOV carve + value
   correlation), runtime-prove in DOSBox-X.
