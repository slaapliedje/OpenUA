# CoK item record format — `ITEM{n}.DAX`

Champions of Krynn stores items in `ITEM1.DAX` / `ITEM2.DAX` / `ITEM3.DAX`. Each DAX block packs
**63-byte item records** back-to-back (every block length is a multiple of 63: 63, 126, 252, 567,
819 = 63 × 1/2/4/9/13). `ITEM1.DAX` holds 39 records: stock gear (Sling, Short Bow, Long Sword,
Mace, Quarter Staff, Dart, Shield, Leather/Ring/Chain Mail, Bracers), consumables (Potion of
Healing, various scrolls, Wand of Magic Missiles), and quest items (Houpak, named potions/rings).

Unlike FRUA's `item.dat` (18-byte records with VOCAB-coded 3-word names), CoK stores a
length-prefixed **plain-ASCII name** followed by a stat block.

## Verified fields (`loaders/item.ts`) — HIGH confidence

| Offset | Size | Field | Evidence |
| ------ | ---- | ----- | -------- |
| 0 | 1 | name length | matches the readable name in every record |
| 1.. | var | ascii name, NUL-padded | "Long Sword", "Potion of Healing", "Wand of Magic Missiles" |
| 42 | 1 | base-type id (catalogue index) | stock gear runs 5 (vial), 6 (staff sling), 7 (sling), 8 (arrow), 9 (short bow), 10 (long sword), 11 (quarter staff), 12 (mace), 13 (dart), 14 (shield), 15 (leather); **0 = special/unique** (Ring Mail, Houpak, named magic items, most potions/scrolls/wands/rings) |

## Verified — `loaders/item.ts`

| Offset | Field | Evidence |
| ------ | ----- | -------- |
| 57 | stack / quantity count (0 for non-stackables) | "20 Arrow" = 20, "4 Dart" = 4, "10 Arrow" = 10, "1 Vial" = 1; **every** non-countable item = 0 |

## Key structural finding — base combat stats are *type-keyed*, not per-record

Correlating the stock-gear rows (Long Sword, Mace, Shield, Leather/Ring/Chain Mail, Sling, Bow,
Dart, Quarter Staff) against their known D&D stats shows **no per-record byte carries weapon damage
or armour AC**. Two facts establish this:

- The bytes that *do* vary with item identity (44, 46, 49) don't track damage dice ({1d8, 1d6, 1d3,
  1d4, 1d6} for sword/mace/dart/sling/staff) or armour AC ({8, 7, 5} for leather/ring/chain) in any
  consistent encoding — they read as icon/secondary-table indices.
- Base equipment is fully identified by the **base-type id at offset 42** (5 = vial, 6 = staff
  sling, 7 = sling, 8 = arrow, 9 = short bow, 10 = long sword, 11 = quarter staff, 12 = mace, 13 =
  dart, 14 = shield, 15 = leather), and identical base types share identical stat bytes regardless
  of name.

So CoK (like the rest of the Gold Box line) keeps a **base-item table in the engine executable**,
indexed by the byte-42 id, that supplies base damage / AC / weight / to-hit class. The 63-byte DAX
record stores only the item's *identity and deltas*: name, base-type id, stack count, and a
magic/effect descriptor.

**Next item step:** extract the base-item table from `START.EXE` (keyed by ids 5–15), or — since the
stock-gear set is canonical D&D — encode those base stats directly as ruleset/base-item data. This
is what feeds PC attack damage in the combat loop (M2.S5); it does **not** require the Journal.

## Located but not pinned — MEDIUM/LOW confidence (per-record magic/effect block)

The block at offsets **47–53** and **58–62** carries an item's magic/effect descriptor:

- **51** = 0 for plain stock gear, ≥ 1 for consumables/magic items — a "special/charged" flag.
- **47–49** populate only for items with an active effect (scrolls, potions, wands) — effect-type /
  power / target descriptor.
- **58** looks like a charge count (Wand of Magic Missiles 13, Flail 15, Wand 9, Bracers 4).
- **59–62** hold **spell ids** for scrolls — a "3 Spells" scroll fills three of them, a "2 Spells"
  scroll two — paralleling FRUA `ITEM.TXT` bytes 14–17.

Magic bonus (+1/+2…) and gold value still need known per-item values to assign; the decoder keeps
the full 63-byte `raw` record so these land without re-extraction. (Offset 55 looked like a price —
Long Sword 45, Shield 100, Leather 150 — but a `Chain Mail` variant breaks the ordering, so it is
left unassigned.)
