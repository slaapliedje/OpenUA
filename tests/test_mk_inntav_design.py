"""Tests for tools/mk_inntav_design.py. All synthetic — no copyrighted data.

Two properties here are worth pinning because getting either wrong produces a
screen that looks BROKEN rather than a test that fails:

  1. STRG ids are 1-BASED (jt232 reads index id-1). Writing the 0-based index
     resolves to the wrong slot -- often an empty one -- and then jt232 leaves
     its buffer empty, l0b20 prints nothing, and the tavern's "TALK" option
     appears to do nothing at all. That cost a boot on the first run of this
     design.
  2. The tavern's rumor ids are LITTLE-ENDIAN pairs in ev[8..15]. The engine
     assembles them by hand as `(ev[9+2k] << 8) | ev[8+2k]`, so a big-endian
     write turns id 2 into 512 -- again a silent empty string.

The observable flag->menu mapping is pinned too, since that is what the live
verification actually keyed on: ev[7] bit5 -> DRINK, any rumor -> TALK,
bit4 -> FIGHT, always -> LEAVE.
"""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from mk_inntav_design import (                                     # noqa: E402
    ENTRY_COL, ENTRY_ROW, EV_INN, EV_MENU, EV_TAVERN, INN_CELL, MENU_CELL,
    ON_ENTRY, ROOM, STRINGS, STR_MENU, STR_RUMORS, TAVERN_CELL, TYPE_INN,
    TYPE_MENU, TYPE_TAVERN, _sid, inntav_design,
)


def area(**kw):
    return inntav_design(**kw).areas[5]


# --- the 1-based STRG id trap ---------------------------------------------

def test_every_authored_id_resolves_to_the_intended_string():
    """THE REGRESSION. strg_read() is 0-based, ids are 1-based: id N is
    strings[N-1]. Every id this tool writes into an event must land on the
    text it was meant to name."""
    strings = area().strg_read()
    for i, sid in enumerate(STR_RUMORS):
        assert strings[sid - 1].upper().startswith("RUMOR "), (sid, strings[sid - 1])
    assert "BOARD" in strings[STR_MENU - 1].upper()


def test_no_authored_id_is_zero_or_points_at_an_empty_slot():
    """id 0 means 'no string' to the engine, and an empty slot prints nothing
    -- both indistinguishable from a broken handler on screen."""
    strings = area().strg_read()
    for sid in tuple(STR_RUMORS) + (STR_MENU,):
        assert sid != 0
        assert strings[sid - 1] != ""


def test_sid_raises_rather_than_returning_a_bogus_id():
    with pytest.raises(ValueError):
        _sid("a string that is not in the table")


def test_there_are_four_rumors():
    """l4f9a reads exactly four design rumors from ev[8..15]."""
    assert len(STR_RUMORS) == 4


# --- endianness ------------------------------------------------------------

def test_tavern_rumor_ids_are_little_endian_pairs():
    """The engine does `(ev[9+2k] << 8) | ev[8+2k]` -- low byte FIRST."""
    ev = area().event(EV_TAVERN)
    for k, sid in enumerate(STR_RUMORS):
        assert ev[8 + 2 * k] == sid & 0xff          # low byte first
        assert ev[9 + 2 * k] == (sid >> 8) & 0xff
        assert (ev[9 + 2 * k] << 8) | ev[8 + 2 * k] == sid


def test_menu_text_id_is_little_endian():
    """l5bde reads the short at ev+4 off a 68k and jt1180-swaps it, so the file
    holds it low byte first."""
    ev = area().event(EV_MENU)
    assert ev[4] == STR_MENU & 0xff
    assert ev[5] == (STR_MENU >> 8) & 0xff


def test_menu_passes_a_rumor_id_to_the_tavern_subevent():
    """l5bde copies ev[10]/ev[11] into subrec[8]/subrec[9], which is the
    tavern's first rumor slot."""
    ev = area().event(EV_MENU)
    assert (ev[11] << 8) | ev[10] == STR_RUMORS[0]


# --- flags -> the menu rows actually observed ----------------------------

def test_tavern_flags_produce_drink_talk_fight_leave():
    """Observed live as exactly `DRINK TALK FIGHT LEAVE`."""
    ev = area().event(EV_TAVERN)
    assert ev[7] & 0x20, "bit5 -> DRINK"
    assert ev[7] & 0x10, "bit4 -> FIGHT"
    assert any(ev[8:16]), "a rumor -> TALK"


def test_tavern_rumors_are_round_robin_not_random():
    """bit3 selects RANDOM rumor choice. Leaving it clear makes successive
    runs comparable, which is what let the cursor wrap be verified by a
    byte-identical 5th frame."""
    assert not (area().event(EV_TAVERN)[7] & 0x08)


def test_tavern_takes_the_generic_drink_and_no_fight_chain():
    """ev[16..17] == 0 -> the generic 'orders a local drink.'; ev[18] == 0 ->
    'Everyone runs away...'. Both were the observed messages."""
    ev = area().event(EV_TAVERN)
    assert ev[16] == 0 and ev[17] == 0
    assert ev[18] == 0


def test_menu_enables_only_inn_and_tavern():
    """Observed live as exactly `INN PUB LEAVE`."""
    ev = area().event(EV_MENU)
    assert ev[7] == 0x08 | 0x10


def test_inn_text_is_empty_so_the_default_message_prints():
    """l398a prints "The party enters a local Inn." when the event text is
    empty (or the caller is type 22), then runs l473e."""
    ev = area().event(EV_INN)
    assert (ev[5] << 8) | ev[4] == 0


# --- types and placement --------------------------------------------------

def test_event_types_are_the_ones_the_handlers_dispatch_on():
    g = area()
    assert g.event_type(EV_TAVERN) == TYPE_TAVERN == 7
    assert g.event_type(EV_INN) == TYPE_INN == 29
    assert g.event_type(EV_MENU) == TYPE_MENU == 22


def test_each_event_has_a_cell_pointing_at_it():
    """cell special = event index + 1. An event with no cell can never fire --
    which is the state three of HEIRS' four taverns are in."""
    g = area()
    for idx, (col, row) in ((EV_TAVERN, TAVERN_CELL), (EV_INN, INN_CELL),
                            (EV_MENU, MENU_CELL)):
        assert g.cell_special(col, row) == idx + 1


@pytest.mark.parametrize("which", sorted(ON_ENTRY))
def test_on_entry_puts_the_chosen_event_under_the_party(which):
    """The whole point: the handler runs with no keys pressed, so a wrong
    facing cannot silently walk the party away from the cell."""
    g = area(on_entry=which)
    assert g.cell_special(ENTRY_COL, ENTRY_ROW) == ON_ENTRY[which] + 1
    assert g.entry_point(0) == (ENTRY_COL, ENTRY_ROW, 0)


def test_room_is_big_enough_that_no_neighbour_is_off_map():
    for col, row in (TAVERN_CELL, INN_CELL, MENU_CELL):
        assert 0 <= col < ROOM and 0 <= row < ROOM


def test_strings_table_has_no_empty_entries():
    """Every slot is addressable as id index+1; an empty one is a trap for a
    future edit that appends a string and reuses an id."""
    assert all(s for s in STRINGS)
