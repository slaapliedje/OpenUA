# Built-in item name-word list

An item's **name is not text in `ITEM.DAT`.** Each 18-byte template
([item-table](item-table.md)) stores three **name-word indices** at bytes
`[1]`, `[2]`, `[3]`; the engine composes the display name from them. This
document maps that mechanism. `tools/itemnames.py` implements it.

## The name-word table (A5 -13628)

The words live in a single shared `char*` array based at **A5 `-13628`**. It is
initialised data: the DATA resource seeds the pointer slots and the **DREL**
relocation table marks each as **A4/STRS-relative**, so after the THINK C A5
setup every slot points into the application's **`STRS`** string pool. The
table is **136 entries** (index `0` is the empty word); indices `1..135` are
the AD&D item words — `Battle Axe`, `Long Sword`, `Plate`, `Mail`, `Ring`,
`of`, `Displacement`, `+1` … `Cute Yellow Canary`.

`jt28` (CODE 6 + 0x0dc6) reads them as:

```c
const char *name = *(const char **)(g_a5_buf(-13628) + idx * 4);
```

**These words are engine data, shared across every design** (they are in the
application, not a per-design file) — and copyrighted. `tools/itemnames.py`
therefore never hardcodes them; it extracts the table from the user's own
`UnlimitedAdventures.rfork` / `frua.rsc`, exactly as `tools/items.py` needs the
user's own `ITEM.DAT`. Never commit an extracted list — `data/` is git-ignored.

## How a name is composed

A live item node's bytes `[41..43]` are `jt187`'s copy of template `[1..3]`.
`jt28` emits the parts **high-to-low — `[3]`, then `[2]`, then `[1]`** — joining
with spaces and skipping index-0 (empty) parts:

| template | words | name |
|---|---|---|
| `[0,0,18]`   | ·, ·, Long Sword          | **Long Sword** |
| `[0,47,35]`  | ·, Mail, Banded           | **Banded Mail** |
| `[58,90,44]` | Displacement, of, Cloak   | **Cloak of Displacement** |
| `[0,111,1]`  | ·, +1, Battle Axe         | **Battle Axe +1** |
| `[78,90,72]` | Ogre Power, of, Gauntlets | **Gauntlets of Ogre Power** |

So the **primary noun sits in `[3]`**, and `[2]`/`[1]` are trailing modifier
words. For the 254 base-game items, template `[3]` equals the item **`type`**
(`[0]`) — the same numbering space names the primary word and selects the item
category. A custom item can decouple them (a weapon-behaviour `type` with a
different `[3]` name word).

Verified: composing all 254 base `ITEM.DAT` records against the extracted table
reproduces every FRUA item name (`Staff Sling`, `Javelin of Lightning`,
`Ring of Wizardry`, `Wand of Fireballs`, `Potion of Extra Healing`, …).

## Runtime decoration NOT reproduced

`base_name()` returns only the composed name-word string — the stable part
derived from the template. On top of it `jt28` layers, at runtime:

- a **quantity** prefix for stacked items (`3 Arrows`);
- an identified-magic **`* `** mark (when a party member has identified it and
  the item carries a magic bonus);
- a **usability** tag (` Yes.` / ` No. `);
- **plural** `s` on the noun (type/slot-dependent);
- a verbose **stat line** (`L3fd6` → `jt94`).

These depend on party/inventory state, not the template, so the tool leaves
them out.

## Reading

```python
from itemnames import ItemNamer
from items import ItemTable

namer = ItemNamer.from_rfork("data/work/UnlimitedAdventures.rfork")
t = ItemTable.parse(open("ITEM.DAT", "rb").read())
namer.base_name(t[28])      # "Long Sword"
namer.base_name(t[48])      # "Cloak of Displacement"
```

Or from the command line:

```sh
python3 tools/itemnames.py UnlimitedAdventures.rfork ITEM.DAT
```

prints the 136-word table and the named contents of `ITEM.DAT`.
