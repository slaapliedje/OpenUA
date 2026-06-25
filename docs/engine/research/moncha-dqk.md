# DQK (Gen2) bestiary — `MONCHA.GLB` decode + combat wiring (C4.2a)

How The Dark Queen of Krynn stores its monsters, and how a DQK fight now runs on the shared
interactive combat screen with faithful decoded stats.

## Container

`MONCHA.GLB` (**MON**sters + **CHA**racters) is a flat HLIB **DATA** library — the same Gen2
container family as `GEO.GLB` / `ECL.GLB` (`magic=1`):

- **member[0]** = id table: `uint16 count`, then `count × {uint16 areaId, uint16 memberIndex}`.
- **members[1..N]** = individual creature records, each **compressed** (≈90–128 raw bytes).

## The MONCHA run-length variant

The records use a signed-byte RLE that is the same *family* as the DAX/DAA scheme but with the
boundary **one higher** and the repeat count **one larger** (documented in hackdocs `MONSTDAT.TXT`):

```
read control byte c:
  c <= 0x80 (<=128) : copy the next (c + 1) bytes literally
  c >  0x80 (>=129) : repeat the next single byte (257 - c) times
```

The `+1` on both the boundary and the repeat count is decisive: a record's all-`0x0a` ability run
and its zero padding only decompress to the verified fixed-offset layout under `257-c`, never the
DAX `256-c`. Engine: `monchaRLE` (`util/rle.ts`), kept separate from `signedByteRLE` so no DAX/DAA
golden moves.

## Decompressed record layout (type-8 monster) — VERIFIED

Unlike the Gen1 .CCH-family record (name length at offset 0, name at 1), the DQK record has a
**fixed-width 16-byte name field at offset 24** and a record-type tag at offset 2. Offsets were
located by correlating the cleanly-decoded records against canonical AD&D values:

| offset | field | notes |
|---|---|---|
| 2 | record type | `0x08` = monster (`0x03` = character, interleaved) |
| 4..5 | XP award | u16 LE |
| 24..39 | name | ASCII, NUL-padded, 16-byte field |
| 40..51 | 6 ability scores | (cur,max) byte pairs; monsters default to 10s |
| 55 | THAC0 | stored inverted: to-hit = `60 - byte` |
| 59..63 | 5 saving throws | paralyze/poison/death, petrify, rod, breath, spell |
| 65 | hit-dice count | |
| 101 / 103 / 105 | damage dice / die size / bonus | `count d sides + bonus` |
| 107 | armour class | stored inverted: AC = `60 - byte` |

**Canon cross-checks (exact):** `BLACK PUDDING` → AC 6, HD 10, THAC0 10, 3d8, XP 3000;
`BORING BEETLE` → AC 0, HD 5, 5d4; `SHAMBLING MOUND` → AC 0, HD 10, XP 7000; `ETTIN` → AC 3, HD 10,
2d8, XP 2580. HP is not stored (the original rolls it from HD), so the loader reports the average
roll `round(HD × 4.5)` as a usable hit-point pool.

Engine: `decodeMonchaGlb` / `decodeDqkMonsterRecord` / `sniffDqkMonster` (`loaders/moncha.ts`),
returning records as the shared `MonsterRecord` shape so the existing combat model consumes them
directly.

## Coverage — full **98/98 names** (C4.2b sub-item 1), ~handful with full stats

The decode now has **two tiers**, because the record is variable-length and self-describing:

1. **Full bestiary roster (98/98 named).** Every creature member yields a **name + record-type
   category** via {@link scanMonsterName} — an *offset-independent* scan (the name is the longest
   uppercase-ASCII run of ≥3 chars / ≥2 letters at/after the binary header), because the name does
   **not** sit at a fixed offset: measured name offsets range **21 … 300+** across the library. This
   recovers the whole DQK bestiary — BLACK PUDDING, the chromatic DRAGONs + HEADs, the draconians
   (AURAK/BOZAK/SIVAK), the Thenol troops, and the named bosses (CAPTAIN DAENOR, GRUNSCHKA, ELGYNORA,
   DAWNSHINE). Names are SSI's in-data abbreviations ("ENORMUS SPIDER", "GREATR OTYUGH", "PURPL
   WORM") — **not** decode errors.
2. **Validated full-stat records (the fixed-offset-aligned subset).** Only members whose decompressed
   record happens to align to the fixed stat offsets (`sniffDqkMonster`) yield a complete
   `MonsterRecord` (THAC0/HD/damage/AC/saves). Those are the records the combat path fights with
   faithful numbers; the rest carry name + category and `hasStats=false`.

`decodeMonchaGlb` returns `roster` (all 98, `MonchaEntry[]`) + `monsters` (validated stats) + the
counts `named` / `decoded` / `total`, so the UI reports both truthfully
("`98` of `98` creatures named (`N` with full validated combat stats)").

## Combat wiring

`apps/web/src/ui/dqkExplore.ts`:

- `fireSearchLocation` now returns `{ text, roster }` — it still folds the cell's event description
  into the per-cell line, but also **captures the first queued combat roster** instead of swallowing
  fights into a `⚔` marker.
- `launchDqkCombat` builds the party (created roster, else a DQK demo four) + faithful enemy
  `Combatant`s (`dqkMonsterFor` resolves an encounter `monsterId` through the bestiary; a scripted
  fight that resolves nothing still faces one real decoded monster), then hands off to the shared
  `launchCombat` overlay. On dismiss, `onEnd` returns control to the dungeon with the outcome on the
  event line — the same tactical screen, treasure flow, and HP persistence CoK/DoK fights use.

## C4.2b investigation — RESOLVED for names (full roster); stat framing still open

A focused reversing pass (opcode-by-opcode RLE traces of clean vs short members) established:

- **`monchaRLE` is correct and faithful.** Every boundary/repeat variant was tested against the
  golden: `c ≤ 128` copy `c+1` / `c ≥ 129` repeat `257−c` reproduces BLACK PUDDING byte-exact; the DAX
  `256−c` clips it. The "desync" is **not** an RLE-scheme bug.
- **Double-RLE is ruled out.** Running `monchaRLE` a second time over a member's output produces
  garbage (the first output byte is a data byte, not an opcode) — there is no second compression layer.
- **The records are genuinely variable-length and self-describing.** Member byte-lengths span
  ~88 … 530 after decompression, and the *name offset itself varies* (21 … 300+). The "short" members
  (≈90–120 B) are not truncated/desynced — they are complete, compact records; the "long" ones simply
  carry more trailing structure. The high bytes (`fe`/`f9`/`f5` …) that survive in short records are
  **genuine field data**, not leaked opcodes (they fall inside legitimate literal-copy runs).
- **So fixed offsets (55/65/101/107) only align by coincidence** for members whose pre-name header +
  name length happen to place the stat block there (≈the 13–14-char-name records). `sniffDqkMonster`
  keeps exactly those, which is why the full-stat tier stays a handful.

**Shipped (sub-item 1):** offset-independent {@link scanMonsterName} → **names + categories for all
98 members** (`roster`), keeping exact stats for the validated subset (`monsters`).

## The stat block is RLE-compressed **inline** (C4.2b-2a — RESOLVED for 20 type-8 monsters)

The follow-up reversing pass cracked why the "short" records didn't decode: **the monster record's
stat block is itself `monchaRLE`-compressed, inline.** Opcode-by-opcode traces showed the "clean"
records (BLACK PUDDING, ETTIN) had their stat block expanded *as part of* the member-level pass, while
the "short" records carry it as a literal run at the member level — so after one pass it is still
compressed. The surviving high bytes (`f5 0a fe 00 …`) are exactly that inner RLE: `f5 0a` → `0a×12`
(the ability defaults), `fe 00` → `00×3` (the pad before THAC0).

**Reconstruction.** `reconstructDqkRecord` re-expands it: splice the unchanged header+name with a
second `monchaRLE` pass over the post-name tail. This reproduces the canonical layout the clean records
already have — proven by re-deriving BLACK PUDDING/ETTIN byte-for-byte and by **SHAMBLING MOUND** now
decoding to its canon AC 0 / HD 10 / 2d8.

**Ability-anchored stats.** The record is variable-length, so the stat fields are read at fixed deltas
from the **ability block** (`findAbilityBlock`: the first `[1,25]×12` run followed by `00 00 00`), not
from record start: THAC0 +15, saves +19, HD +25, damage +61/+63/+65, AC +67 (the same deltas the clean
records show at ability offset 40). Full-stat **type-8 decode 5 → 20**, every one validated and
canon-cross-checked — IRON GOLEM AC 3 / HD 18 / 4d10 / XP 14550, RED DRAGON AC −1, PURPL WORM 2d12,
AMPHI DRAGON AC −3. XP sits at the *absolute* offset 4 (before the reconstructed block) and matches
canon exactly, independently confirming the framing.

## Two record framings — the non-type-8 split (C4.2b-2b-1 — RESOLVED)

The "non-type-8" members are **not** a single alien layout. A focused pass (probe of all 78 non-type-8
records' ability blocks vs canon) showed `MONCHA.GLB` mixes two combat framings that share the same
container, RLE scheme and name scan but differ at the stat block:

- **Monster-framed** — type 8 **and** many type 0/3/12 records (Sahuagin, Spectre, Wraith, Fire Giant,
  Giant Troll, Skeleton Warrior, Enchanted Baaz…). The ability block is the **monster default**: every
  score is `0x0a` (10), except undead, which carry a few low mental `0x03`s; the real THAC0/HD/damage/AC
  fields follow at the same ability-anchored deltas the type-8 monsters use. These decode to canon —
  **Sahuagin AC 5 / 1d2, Spectre AC 2 / 1d8, Wraith AC 4 / 1d6, Fire Giant AC 3 / 5d6, Skeleton Warrior
  AC 2 / 1d12+3**. Extending the validated tier to them lifts full-stat decode **20 → 27**.
- **Character/NPC-framed** — Black Ogre, Dark Wizard, Thenol Wizard, and the named bosses (Selia,
  Tasslehoff, Captain Daenor, Sharman, Gnome Tinkerer…). The ability block is a **rolled character
  spread** (scores 7…19), and the monster-damage/AC fields after it are **placeholders** (AC 10 default,
  1d2) because a character's combat derives from class + level + equipment stored elsewhere in the record.

**The discriminator is exact on the golden library:** a monster-default block has **≥4 of its 6 current
ability scores equal to 10 and none above 10**; a rolled character spread always contains a score > 10.
`isMonsterDefaultAbility` gates the non-type-8 full-stat path on this, so monster-framed creatures gain
canon stats while character/NPC records honestly stay **name + category** (now categorized `npc`). A
validated record's `MonsterRecord.category` is `monster` regardless of its raw type byte (Sahuagin is
type 0 but combat-framed as a monster). Roster category breakdown: ~44 monster / ~15 npc / ~38 unknown
(the no-ability-block records — Vampire, Mummy, Gas Spore, the long caster/boss records — which we can
name + type but not yet frame). Engine: `isMonsterDefaultAbility` / `classifyCategory` (`loaders/moncha.ts`).

**Still open (C4.2b-2b-2):** the full **character/NPC stat model** (deriving combat numbers from
class + level + equipment for the rolled-ability records — a distinct, larger decode), the handful of
type-8 records whose tail re-expands short (UMBERHULK/HYDRA — AC/damage land past the re-expanded end),
and the combat **trigger** — `SearchLocation` seeds position only and queues no direct
`LOAD_MONSTER`/COMBAT, so fights launch from chained blocks / the `vmRun1` per-step main loop (facing
`0x11` + position dispatch, deferred from C4.1). The `.LST` file MONSTDAT.TXT says "adequately describes"
the pre-record data is not in our hackdocs, so this stays pure byte-level RE.

## Combat trigger — deferred to C4.2b

A position-only sweep of every area × cell through the Gen2 `SearchLocation` handler queued **no**
direct `COMBAT`. DQK's per-cell handlers surface descriptions/ambient events; the *fights* are
launched elsewhere — chained blocks and/or the `vmRun1` per-step main loop (which branches on
facing `0x11` + a position expression, the dispatch deferred from C4.1). C4.2a delivers the combat
**screen + faithful monsters + return-to-dungeon**; C4.2b reverses the trigger so the fights fire
from the world itself rather than the scripted seam.
