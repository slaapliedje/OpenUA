"""Editor round-trip test — the GEO file is the contract between the in-engine
GEO map editor (jt243, which saves via l0878) and the offline tools/geo.py.

WHY THIS IS A HOST-SIDE TEST, NOT A LIVE GUI DRIVE
--------------------------------------------------
The in-engine editor's edit gestures are mouse-driven and its Save is behind a
Mac press-drag pulldown (FILE -> Save 3D Map). Driving that headlessly is not
reliably automatable: synthetic Alt+letter menu accelerators RACE the modifier
release (compat/events.c:169 — "synthetic input releases the modifier before the
engine polls the key"), a held mouse button under Hatari's SDL grab deadlocks the
X server, and the SDL window is recreated on every video-mode change (so click
coordinates and screenshot sizes shift). A test built on that would be flaky.

So we assert the *invariant the GUI round-trip rests on* directly: the editor
saves and loads the exact 12962-byte FORM/AMOD/HDR/MAP/ENCR/STRG container that
`geo.py` builds and parses. Two levels:

  1. synthetic edit -> save -> reload persistence (always runs; no data needed);
  2. byte-exact parse->build against REAL engine/editor-authored GEO files
     (opt-in: runs only when data/work/gamedata is present; copyrighted data is
     git-ignored, so this is skipped in CI).

See docs/geo-editor.md and the run-falcon-port skill's input notes.
"""
import glob
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from geo import Geo, GEO_SIZE


# --- 1. synthetic edit -> save -> reload (models the editor's edit gestures) --

def test_editor_edit_save_reload_persists():
    """Apply a sequence of edits equivalent to editor tool actions, 'save'
    (build the container) and 'reload' (parse it), and assert every edit
    survived — the round-trip the in-engine editor performs on FILE -> Save
    then re-open."""
    g = Geo.blank(15, 38)                 # OVERLAND-01-sized area (WD 15 HT 38)
    g.set_entry_point(0, x=2, y=3, facing=1)

    # "PLACE / BLOCK" tool: drop walls on a few cells (edge 0 = north wall).
    walls = {(1, 1): (0x10, 0, 0, 0),
             (5, 9): (0x10, 0x10, 0, 0),
             (14, 37): (0, 0, 0x10, 0x10)}   # far corner (bounds check)
    for (c, r), w in walls.items():
        g.set_cell(col=c, row=r, walls=w)

    # attach an event to a cell and author it (a Message), like the event editor
    g.set_event_header(7, type=2, chain=0)
    g.set_message(7, text_ids=[1, 2], picture=3)

    # SAVE (build) -> the on-disk image; RELOAD (parse) a fresh object from it
    image = g.build()
    assert len(image) == GEO_SIZE
    g2 = Geo.parse(image)

    # every edit persisted
    assert g2.width == 15 and g2.height == 38
    assert g2.entry_point(0) == (2, 3, 1)          # (x, y, facing)
    for (c, r), w in walls.items():
        assert tuple(g2.cell(c, r)[0:4]) == w, (c, r)
    assert g2.event_type(7) == 2
    assert g2.message(7)["lines"] == [1, 2]

    # a second save is byte-identical (idempotent — no editor-state drift)
    assert g2.build() == image


def test_editor_reload_is_byte_stable_over_two_cycles():
    """parse -> build -> parse -> build is a fixed point (what re-opening and
    re-saving an unchanged area in the editor must do)."""
    g = Geo.blank(11, 24)
    g.set_cell(col=3, row=4, walls=(0x20, 0, 0x30, 0))
    a = g.build()
    b = Geo.parse(a).build()
    c = Geo.parse(b).build()
    assert a == b == c


# --- 2. byte-exact vs REAL engine/editor output (opt-in; needs the game data) --

def _real_geo_files():
    root = os.path.join(os.path.dirname(__file__), "..", "data", "work",
                        "gamedata")
    if not os.path.isdir(root):
        return []
    return sorted(glob.glob(os.path.join(root, "**", "GEO*.DAT"),
                            recursive=True))


REAL = _real_geo_files()


@pytest.mark.skipif(not REAL,
                    reason="no data/work/gamedata (copyrighted GEO files absent)")
def test_geo_py_roundtrips_real_editor_output_byte_for_byte():
    """geo.py must parse->build every real engine/editor-authored GEO file
    byte-for-byte — proof that the offline tooling reads and writes EXACTLY the
    container the in-engine editor saves. Runs only where the game data is
    staged; the copyrighted files are never committed."""
    checked = 0
    for path in REAL:
        data = open(path, "rb").read()
        if len(data) != GEO_SIZE:        # not a standard area container
            continue
        rebuilt = Geo.parse(data).build()
        assert rebuilt == data, "geo.py did not round-trip %s byte-for-byte" % path
        checked += 1
    assert checked > 0
