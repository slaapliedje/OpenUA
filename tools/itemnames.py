#!/usr/bin/env python3
"""Resolve FRUA item names — the engine's built-in item name-word list.

An item's *name* is NOT stored as text in `ITEM.DAT`. Each 18-byte item
template carries three **name-word indices** at bytes `[1]`, `[2]`, `[3]`
(mirrored into a live item node's `[41]`, `[42]`, `[43]` by `jt187`). The
engine's name builder `jt28` composes the display name from those indices
into a shared **name-word table** — an A5 `char*` array based at **A5 -13628**
whose entries point into the application's **STRS** string pool.

    template[3] -> primary name word   (usually == item type; e.g. "Long Sword")
    template[2] -> a modifier word     (e.g. "Mail", "of")
    template[1] -> a modifier word     (e.g. a "+1" / material / gem word)

`jt28` emits the parts **high-to-low** — part 3, then part 2, then part 1 —
so template `[0,47,18]` (words "", "Mail", "Long Sword") is *not* how a base
weapon reads; a real "Long Sword" is `[0,0,18]`, "Banded Mail" is `[0,47,35]`
("Banded" + "Mail"), "Cloak of Displacement" is `[58,90,44]`
("Cloak" + "of" + "Displacement"). Word index 0 is the empty word (skipped).

**The name words are engine data, shared across every design** — they live in
the FRUA application resource fork (STRS), not in a per-design file. They are
also copyrighted, so this tool NEVER hardcodes them: it *extracts* the table
from the user's own `UnlimitedAdventures.rfork` / `frua.rsc` at runtime, the
same way `tools/items.py` needs the user's own `ITEM.DAT`. `data/` is
git-ignored — never commit an extracted word list.

What this tool does NOT reproduce (runtime-only decoration `jt28` adds on top
of the base name): the quantity prefix ("3 Arrows"), the identified-magic "* "
mark, the " Yes."/" No. " usability tag, plural "s", and the verbose stat line.
`base_name()` returns the composed name-word string — the stable part of the
name that comes purely from the template.
"""
import struct
import sys

# Structure facts (not copyrighted content): where the name-word char* table
# lives in the A5 world, and that entry 0 is the empty word.
NAME_WORDS_A5_OFFSET = -13628   # base of the char* name-word table
NAME_WORD_STRIDE     = 4        # 4-byte pointer per entry


class ItemNameError(ValueError):
    pass


def extract_name_words(rfork_path):
    """Extract the item name-word list from a FRUA resource fork.

    Reads the DATA/ZERO/DREL/STRS resources, replays the THINK C A5-world
    setup (expand DATA, apply DREL), then walks the `char*` array at
    A5 -13628 — as long as each slot is an A4/STRS-relative pointer — and
    resolves every entry to its STRS string.

    Returns a list of `str` indexed by name-word index (entry 0 == "").
    """
    # Imported lazily so `import itemnames` costs nothing until extraction.
    import os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from macrsrc import ResourceFork
    import datapool as dp

    rf = ResourceFork.from_file(rfork_path)
    try:
        data = rf.get("DATA", 0).data
        zero = rf.get("ZERO", 0).data
        drel = rf.get("DREL", 0).data
        strs = rf.get("STRS", 0).data
    except KeyError as e:
        raise ItemNameError(
            "%s is missing a DATA/ZERO/DREL/STRS resource (%s) — is this the "
            "FRUA application resource fork?" % (rfork_path, e))

    image = dp.expand_data(data, zero)          # A5-below image; A5 is just past
    a5_len = len(image)                         # the last byte
    a4_slots = {e.a5_offset for e in dp.parse_drel(drel).entries
                if e.base == dp.RELOC_BASE_A4}

    def strs_string(offset):
        end = strs.find(b"\x00", offset)
        if end < 0:
            end = len(strs)
        return strs[offset:end].decode("mac_roman", "replace")

    words = []
    i = 0
    while True:
        off = NAME_WORDS_A5_OFFSET + i * NAME_WORD_STRIDE
        # The table runs while slots stay A4/STRS pointers; the first
        # non-A4 slot after entry 0 is the end of the array.
        if off not in a4_slots:
            if i > 0:
                break
            words.append("")           # entry 0 is the empty word
            i += 1
            continue
        pos = a5_len + off
        strs_off = struct.unpack_from(">i", image, pos)[0]
        words.append(strs_string(strs_off) if 0 <= strs_off < len(strs) else "")
        i += 1
    return words


class ItemNamer:
    """Compose item names from a name-word list.

    `words` is a list indexed by name-word index (as `extract_name_words`
    returns). Construct one from a resource fork with `from_rfork`, or pass a
    word list directly (tests inject a synthetic one — the real words are
    copyrighted engine data).
    """

    def __init__(self, words):
        self.words = list(words)

    @classmethod
    def from_rfork(cls, rfork_path):
        return cls(extract_name_words(rfork_path))

    def word(self, idx):
        """The name word at `idx`, or "" if out of range / the empty word."""
        return self.words[idx] if 0 <= idx < len(self.words) else ""

    def base_name(self, item):
        """The composed base name for an item template.

        `item` is an `items.Item` (or any object with a `.raw` bytes) or a raw
        18-byte record. Emits name parts high-to-low ([3], [2], [1]) exactly as
        `jt28` does, skipping empty (index 0) parts. Returns the stable
        template-derived name without the runtime quantity/magic/plural
        decoration.
        """
        raw = getattr(item, "raw", item)
        if len(raw) < 4:
            raise ItemNameError("item record too short to name")
        parts = []
        for i in (3, 2, 1):            # template[3], [2], [1] — high to low
            idx = raw[i]
            if idx:
                w = self.word(idx)
                if w:
                    parts.append(w)
        return " ".join(parts)


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    namer = ItemNamer.from_rfork(argv[0])
    print("name-word table: %d words (from %s)" % (len(namer.words), argv[0]))
    for i, w in enumerate(namer.words):
        if w:
            print("  [%3d] %s" % (i, w))
    if len(argv) > 1:
        import os
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from items import ItemTable
        t = ItemTable.parse(open(argv[1], "rb").read())
        print("\n%s:" % argv[1])
        for iid in t.used_ids():
            print("  id %3d: %s" % (iid, namer.base_name(t[iid])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
