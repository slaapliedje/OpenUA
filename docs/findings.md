# Consolidated Findings — Krynn Trilogy (verified)

This is the **authoritative synthesis** of the three investigation passes. Where it
disagrees with the individual docs, this file wins (it is backed by direct byte-level
verification). Source docs: `amiga-inventory.md`, `amiga-format-research.md` (note: its
"DQK still uses DAX" inference is **WRONG** — see Containers below), `dos-inventory.md`.

## 1. "Best graphics" verdict — both project claims hold

| Game | Best platform | Why | Confidence |
| ---- | ------------- | --- | ---------- |
| Champions of Krynn (CoK, 1990) | **Amiga** | DOS is **EGA 16-color only**; Amiga is 32-of-4096 with reworked detail + extra combat sprites. | High |
| Death Knights of Krynn (DoK, 1991) | **Amiga** | "Last Gold Box without VGA" — DOS **EGA 16-color only**; Amiga 32-color. | High |
| The Dark Queen of Krynn (DQK, 1992) | **DOS VGA (256-color)** (Mac = equal peer) | DOS/Mac = 256-color VGA; Amiga DQK still 32-color (weakest). | High |

Evidence (DOS side, from binaries/configs): CoK & DoK `START.EXE` setup offers only
CGA/EGA/Tandy, no VGA strings; `KRYNN.CFG` stores `E`. DQK `INSTALL.EXE` offers
`"VGA (256 colors)" / "EGA (16 colors)" / "Tandy 16 Color"` and both EXEs carry
`"Installed for VGA only!"` enforcement strings. → **CoK/DoK never had a 256-color DOS
release**; the only way to show their art in 256 colors under DOS is a VGA-capable engine,
i.e. the **DQK engine**. This is the technical justification for the whole "rebuild on DQK"
plan. The lossy direction (DOS EGA-16 → richer) is correctly avoided by sourcing CoK/DoK
art from the **Amiga**.

## 2. The three container formats (VERIFIED by magic bytes)

| Platform / game | Ext | First bytes | Format |
| --------------- | --- | ----------- | ------ |
| DOS CoK / DoK | `.DAX` | `24 00 72 00 …` (LE word header) | **DAX** — headerless: `uint16 entry_count`, `uint16 table_offset`, then TOC, then signed-byte-RLE block data. No palette in file (fixed EGA-16). |
| Amiga CoK / DoK | `.DAA` | `00 77 c9 00 …` | **DAA** — structurally DAX-adjacent but distinct (Amiga = big-endian 68000). **NOT an alien format** — looks like a DAX cousin. Exact header/endianness still needs a dedicated reversing pass. This is the biggest *unknown* but a *tractable* one. |
| DOS DQK | `.TLB` / `.GLB` | `48 4c 49 42` = **`HLIB`** | **HLIB "DataLib"** — a tagged container: `HLIB` header then typed sub-chunks: **`TILE`** (graphics frames, **embedded 6-bit-DAC VGA palette**), **`DIG4`** (digitized sound), **`DATA`** (ECL scripts / GEO maps / generic). `.GLB` is the master-library variant (e.g. `ECL.GLB`, `GEO.GLB`, `SOUNDS.GLB`). |

**Correction recorded:** DQK does **not** use DAX. It uses the HLIB/DataLib container
generation (the same lineage later used by FRUA / Unlimited Adventures). This matters: the
rebuild target format is **HLIB**, and any repacker we build writes HLIB `TILE`/`DATA`
chunks, not DAX.

## 3. What was extracted (Amiga, into git-ignored `amiga_extracted/`)

- **CoK** — clean `ChampionsOfKrynn_Nof3.Adf` set, 3 disks, standard OFS, **109 files**.
  Art candidates: `PIC1/2.DAA`, `SPRIT1/2.DAA`, `BIGPIC1/2.DAA`, plus directly-viewable IFF
  `scrn*.lbm` title screens.
- **DoK** — `dkk01/02.adz` (Skid Row, decompressed), 2 disks, **137 files**.
  Art candidates: `PIC2.DAA` (282 KB), `CPIC1.DAA`, `BACK1.DAA`, `SPRIT1.DAA`, IFF `Title*.LBM`.
- **DQK (Amiga)** — partial; **Disk 3 segfaults `unadf` (unrecoverable)**. *Low priority* —
  we want **DOS** DQK art, not Amiga DQK, so this gap does not block the main goal.

## 4. Prior art / tools (use as format reference, don't reinvent)

- **Gold Box Explorer** (bsimser) — batch DAX/TLB → PNG export (extract-only).
- **Simeon Pilgrim's COAB reimplementation / fork** — de-facto DAX + **ECL** spec; the best
  source for the ECL bytecode VM (port its logic for any interpreter we write).
- **Gold Box Companion** — the only tool that *writes* DAX (but EGA icons/fonts/items only).
- **forums.goldbox.games** topics 1073 / 3148 / 1241 — the real community format docs.
- Local: `hackdocs_extracted/` (FRM_DESC, OPCODES, SCRIPT, GEO*, etc.), `daxdump_extracted/`
  (DaxDump.exe, EclDump.exe), `tools/dax_inspect.py`.
- **Gap:** no public tool targets **Amiga `.DAA` graphics**, and no public DAX/HLIB
  *repacker* for backdrops/sprites exists — both must be built. Format is known/knowable, so
  tractable.

## 5. Top technical risks (for the main rebuild goal)

1. **Amiga `.DAA` format** is undocumented — must be reversed from the extracted files
   before any Amiga art can be decoded. (Mitigated: it looks DAX-like.)
2. **No HLIB repacker exists** — to put art into the DQK engine we must write `TILE`/`DATA`
   chunks ourselves. (Format known from reading DQK's own files.)
3. **ECL opcode drift** between the CoK/DoK engine and the DQK engine — porting CoK/DoK
   scripts/encounters onto DQK may need opcode translation or engine patches.
4. **"Plays like the original"** is more than art: CoK/DoK maps (GEO), monsters (MON),
   items (ITEM), and ECL logic must be carried onto the DQK engine while preserving original
   mechanics/level ranges/story — the genuinely hard part of the main goal.
