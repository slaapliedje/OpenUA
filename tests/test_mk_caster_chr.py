"""Tests for tools/mk_caster_chr.py. All synthetic — no copyrighted data.

This tool has now produced THREE bugs that each looked exactly like an engine
fault, and every one was a field the engine derives or gates on:

  1. AC 60 / Move 1     — wrote the DERIVED bytes 384/385/396 instead of the
                          BASE bytes 127/136/179 that l4842 copies from.
  2. CAST screen empty  — never wrote rec[382], so l05c4 refused every magic
                          command BEFORE jt597 built a list.
  3. XP 1.35 billion    — wrote the long at +68 big-endian when the .cch file
                          is little-endian there (l0ce0_c15 swaps it).

Each cost an emulator round-trip to diagnose, and each is one byte. These tests
are cheap; that debugging was not.
"""
import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from mk_caster_chr import (                                        # noqa: E402
    CHAR_ALIVE, CHAR_AC, CHAR_BASE_AC, CHAR_BASE_MOVE, CHAR_BASE_THAC0,
    CHAR_CAPACITY, CHAR_CONSCIOUS, CHAR_GRIMOIRE, CHAR_INV_HEAD,
    CHAR_ITEM_COUNT, CHAR_MEMORIZED, CHAR_MONEY, CHAR_XP, DEFAULT_STATS,
    MAGE_SPELLS_BY_LEVEL, MAGE_L1_SPELLS, RECORD_LEN, SCROLL_MAX_SPELLS,
    SCROLL_TYPE_MAGE, build, build_scroll, default_grimoire, max_spell_level,
)


def mk(**kw):
    kw.setdefault("name", "MERLIN")
    kw.setdefault("stats", DEFAULT_STATS)
    kw.setdefault("spells", MAGE_L1_SPELLS)
    kw.setdefault("mage_level", 5)
    kw.setdefault("hp", 49)
    return build(**kw)


# --- the three regressions -------------------------------------------------

def test_xp_is_little_endian_on_disk():
    """l0ce0_c15 byte-swaps the long at +68, so the FILE must be little-endian.

    Big-endian here made a level-5 mage read back with 0x50C30000 experience.
    The stock characters all carry little-endian 50000.
    """
    rec = mk(mage_level=5)
    assert struct.unpack_from("<I", rec, CHAR_XP)[0] == 50000
    assert struct.unpack_from(">I", rec, CHAR_XP)[0] != 50000
    assert struct.unpack_from("<H", rec, CHAR_MONEY)[0] == 100


def test_base_combat_fields_are_written_not_just_derived():
    """l4842 copies 127->384, 179->385, 136->396 on every load, so the BASE
    bytes are the authoritative ones. Writing only the derived bytes is what
    displayed AC 60 / Move 1."""
    rec = mk()
    assert rec[CHAR_BASE_AC] == 50            # displayed AC 10
    assert rec[CHAR_BASE_THAC0] == 40
    assert rec[CHAR_BASE_MOVE] == 12
    assert rec[CHAR_AC] == rec[CHAR_BASE_AC]  # derived seeded to match


def test_magic_gate_bytes_are_set():
    """l05c4 returns BEFORE jt597 when rec[382]==0 or rec[94]==1, which makes a
    working spell picker look broken."""
    rec = mk()
    assert rec[CHAR_ALIVE] == 1
    assert rec[CHAR_CONSCIOUS] == 0


# --- the capacity grid belongs to the engine -------------------------------

def test_capacity_grid_is_left_to_jt908():
    """rec[355..381] is zeroed and rebuilt by jt908 on every jt910 (the Add
    path). All four stock characters ship it all-zero. Writing it here would be
    silently discarded and would mislead the next reader."""
    rec = mk()
    assert rec[CHAR_CAPACITY:CHAR_ALIVE] == bytes(CHAR_ALIVE - CHAR_CAPACITY)


# --- the grimoire ----------------------------------------------------------

def test_grimoire_bit_order_is_lsb_first():
    """mask[bit] = 1 << bit, from the real -18893 table; cross-checked against
    the 0x00ff run checksum in src/engine/a4_map.c."""
    rec = mk(grimoire=(1,))
    assert rec[CHAR_GRIMOIRE] == 0x01
    rec = mk(grimoire=(8,))
    assert rec[CHAR_GRIMOIRE] == 0x80
    rec = mk(grimoire=(9,))
    assert rec[CHAR_GRIMOIRE] == 0x00
    assert rec[CHAR_GRIMOIRE + 1] == 0x01


def test_grimoire_round_trips_every_valid_spell_id():
    for spell in range(1, 127):
        rec = mk(grimoire=(spell,))
        byte = rec[CHAR_GRIMOIRE + ((spell - 1) >> 3)]
        assert byte == 1 << ((spell - 1) & 7), spell
        assert sum(rec[CHAR_GRIMOIRE:CHAR_CAPACITY]).bit_count() >= 1


def test_grimoire_default_tracks_the_mage_progression():
    """A mage gets spell level (N+1)//2: L1->1st, L3->2nd, L5->3rd."""
    assert max_spell_level(1) == 1
    assert max_spell_level(5) == 3
    assert max_spell_level(20) == 9          # capped
    l5 = default_grimoire(5)
    assert set(MAGE_SPELLS_BY_LEVEL[1]) <= set(l5)
    assert set(MAGE_SPELLS_BY_LEVEL[3]) <= set(l5)
    assert not set(MAGE_SPELLS_BY_LEVEL[4]) & set(l5)


def test_default_grimoire_holds_back_the_scroll_spells():
    """THE INVARIANT SCRIBE DEPENDS ON. l0df2 rejects any pick the grimoire
    already has ('already knows that spell'), so a scroll whose spells are all
    known is unscribable and the screen looks broken again."""
    held = [20, 21]
    g = default_grimoire(5, exclude=held)
    assert not set(held) & set(g)
    assert 19 in g                           # its neighbours stay


# --- scrolls ---------------------------------------------------------------

def test_scroll_is_a_type_jt638_accepts():
    """jt638 keeps only item types 39, 40 and 73; anything else never reaches
    l5726 and the scroll is invisible to SCRIBE."""
    assert SCROLL_TYPE_MAGE in (39, 40, 73)
    it = build_scroll([20])
    assert it[0] == SCROLL_TYPE_MAGE
    assert len(it) == 18


def test_scroll_read_bits_are_clear():
    """l5726 SKIPS a scroll when node[51] (template [11]) has any low-3 bit
    set, unless the reader has record-flag 16 or a cleric level. Real scrolls
    carry 0x04 there; the harness authors them pre-read on purpose."""
    assert build_scroll([20])[11] & 0x07 == 0


def test_scroll_spell_ids_land_where_l5726_reads_them():
    """l5726 reads it[53+s] for s in 1..4, i.e. node[54..57] = template[14..17]."""
    it = build_scroll([20, 21, 19, 18])
    assert list(it[14:18]) == [20, 21, 19, 18]


def test_scroll_rejects_a_fifth_spell():
    with pytest.raises(ValueError):
        build_scroll([1, 2, 3, 4, 5])


def test_inventory_head_must_be_non_zero_or_jt577_skips_the_loop():
    """jt577 reads rec[8] only as a flag: it saves the value, zeroes the slot,
    then loops `while (old_inv != 0)`. A zero head means the 18-byte item
    bytes on disk are never read, however correct they are."""
    rec = mk(scroll_spells=[20])
    assert struct.unpack_from(">I", rec, CHAR_INV_HEAD)[0] != 0
    assert rec[CHAR_ITEM_COUNT] == 1


def test_file_length_matches_the_reader_arithmetic():
    """jt577 reads 398 bytes then 18 per item — the stock files are exactly
    398 + 18*count long."""
    assert len(mk()) == RECORD_LEN
    assert len(mk(scroll_spells=[20])) == RECORD_LEN + 18


def test_no_inventory_by_default():
    """An empty inventory is load-bearing for the hunk-16 A/B: it keeps the
    'Items' verb off the character sheet's command bar."""
    rec = mk()
    assert len(rec) == RECORD_LEN
    assert struct.unpack_from(">I", rec, CHAR_INV_HEAD)[0] == 0
    assert rec[CHAR_ITEM_COUNT] == 0


def test_memorized_spells_land_in_the_141_slots():
    rec = mk(spells=(9, 10, 11))
    assert list(rec[CHAR_MEMORIZED:CHAR_MEMORIZED + 3]) == [9, 10, 11]
