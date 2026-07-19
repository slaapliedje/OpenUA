# Item template fields (`ITEM.DAT`, per-type semantics)

Each 18-byte `ITEM.DAT` record ([item-table](item-table.md)) is copied by
`jt187` into a live 62-byte item node at `node[40..57]` (so `template[k]` =
`node[40+k]`). This document maps the static template bytes to their engine
meaning, confirmed against the readers in `src/engine/boot.c`. `tools/items.py`
exposes them as named `Item` properties.

## The two 16-bit words — `[4..5]` and `[6..7]`

These are the only multi-byte fields, and they are stored **little-endian** on
disk (FRUA's DOS heritage; the Mac engine byte-swaps them on load in `jt578`
via `jt1180`). The rest of the record is single bytes.

| bytes | field | evidence |
|---|---|---|
| `[4..5]` | **weight** — tenths of a pound (Plate Mail 450 = 45 lb, Dagger 10 = 1 lb, Arrow 2 = 0.2 lb) | `l0b3e` reads `item+44` as the encumbrance value (`>399` slows movement); `L…` names the local `weight` at `boot.c:91244` |
| `[6..7]` | **value** — appraised worth in gp (base gear 1–80; +1 ≈ 500; +5 ≈ 10000) | the sell/appraise path feeds `item[46]` to the pricer `jt932` (`boot.c:92627`); summed across a merged stack |

## Single-byte fields — `[8..17]`

| byte | field | evidence |
|---|---|---|
| `[8]` | **to-hit / damage bonus**, signed (`+1..+5`; `253` = −3 cursed) | `l0be0`/combat read `(signed char)item[48]`; `jt28` magic-name test |
| `[9]` | **AC / save bonus**, signed | `l0be0` folds `item[49]` into the AC accumulator and `player[195]` |
| `[10]` | **usability / identify flag** (gates worn effects; `jt28` " Yes."/" No. ") | `item[50]` in `l0be0`, `l2d78`, `jt28` |
| `[11]` | **known-name-parts mask** — which name words show before the item is identified; `jt187` clears its low 3 bits on the give-unflagged path | `item[51]` in `jt28`; `jt187` |
| `[12]` | *(unused in the base table; a runtime magic marker `item[52]` is set on identify)* | `jt28` reads `it[52]`, but it is 0 in every base template |
| `[13]` | **stack / ammo count** (Arrow 20, Dart 10, Javelin 5) | `item[53]` — `jt28` quantity prefix; stack merge/split `l32c4` |
| `[14]` | **charges** (wands, consumables) | `item[54]`; Wands carry 20 |
| `[15]` | **spell / effect id** — passed to the item-effect core `l77a0` | `l2d78` case 0: `l77a0((short)item[55], …)` |
| `[16]` | **hook** — bit 7 (`0x80`) = worn/passive effect present; low 7 bits = **hook kind** dispatched through `JT[1] @0x2da2` (0 = generic effect, 1 = wizardry ring, 3/5 = recompute effects, 9 = strip effect) | `l2d78`: `kind = item[56] & 0x7f`; `hook = item[56] & 0x80` |
| `[17]` | *(unused; 0 in the base table)* | — |

### Per-type overload of `[14..16]`

The `[14..16]` triple is **type-dependent**:

- **Wands / consumables** — `[14]` = charges, `[15]` = the spell/effect id.
- **Scrolls** (types 38/39/40) — `[14]`, `[15]`, `[16]` are up to **three spell
  ids** (a "3 Spells" scroll: e.g. `94, 118, 119`), not a charge+hook.
- **Worn magic** (rings, cloaks, bracers, girdles) — `[16]` bit 7 marks a
  passive effect and its low bits select the equip hook; `[15]` names the power.

## Where weapon range and class restrictions live (NOT the template)

Weapon reach and the usable-by class mask are **not** in `ITEM.DAT`. They come
from the per-**type** behaviour table at **A5 `-27944`** (16-byte stride, keyed
by `item[40]` = the item type). `l0be0` reads `rec[0]` (kind: weapon / shield /
worn-AC / …) and `rec[6]` (codec: bit 7 = "contributes AC", low 7 = magnitude)
from it; `l0b3e` reads `rec[0] == 2` for mounts. An earlier draft of
`tools/items.py` mislabelled template `[12]`/`[13]` as range/class — corrected
here: `[12]` is unused and `[13]` is the stack count.

## Reading

```python
from items import ItemTable
t = ItemTable.parse(open("ITEM.DAT", "rb").read())
it = t[40]
it.weight     # 450  (45.0 lb Plate Mail)
it.value      # 80   (gp)
it.hit_bonus  # signed to-hit/damage bonus
it.ac_bonus   # signed AC/save bonus
it.count      # stack / ammo count
it.charges, it.effect_id, it.hook, it.worn_effect
```

Verified: decoding all 254 base `ITEM.DAT` records reproduces the expected
weights (tenths-lb), enchantment signs (Cursed = −3), ammo counts, wand charges,
and scroll spell triples.
