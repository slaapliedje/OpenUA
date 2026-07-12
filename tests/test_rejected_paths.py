"""Guard the paths the user has REJECTED, so a stale comment can't resurrect them.

This project's expensive failure mode is not bad code — it is a confident,
wrong COMMENT that sends the next session hunting. The stub-claim audit
(test_stub_audit.py) already guards one class of that. This guards another:
a rejected architectural direction that keeps coming back because the code
comment describing it asserted the wrong thing.

THE SHARED-PALETTE DUNGEON CLUT. `g_dungeon_bigpic_overlay` gates L58c4's
perspective-backdrop overlay, whose L3f3c(32,255) installs one palette across
CLUT 32..255. Its comment used to call that "the faithful path" and the port's
per-set band model "fabricated". That is wrong:

  * every 8X8DB texture set's item-0 header declares start=32, count=37 — each
    set carries its OWN palette wanting 32..68, and a level mixes THREE sets
    whose palettes differ over the same byte range. They cannot share one flat
    palette. That is why g_cw_base bands them (32/69/106).
  * the port already draws the perspective backdrop (render_3d_faithful blits
    BACK.CTL's image directly; night skies index 144..175, the band
    load_backdrop lays). The overlay adds nothing and would wipe the wall bands,
    the backdrop band and the menu palette.

The user has corrected this repeatedly. The old comment still resurrected it
twice in a single session (2026-07-12). So: the gate stays 0, and no comment may
describe the shared-palette rewrite as pending/faithful work.
"""
import os
import re

import pytest

BOOT = os.path.join(os.path.dirname(__file__), '..', 'src', 'engine', 'boot.c')


def _boot():
    with open(BOOT, encoding='utf-8', errors='replace') as fh:
        return fh.read()


@pytest.mark.skipif(not os.path.exists(BOOT), reason='boot.c not present')
def test_bigpic_overlay_gate_stays_off():
    """g_dungeon_bigpic_overlay must remain 0 — flipping it wipes the CLUT."""
    src = _boot()
    m = re.search(r'static int\s+g_dungeon_bigpic_overlay\s*=\s*(\d+)\s*;', src)
    assert m, 'g_dungeon_bigpic_overlay is gone — did someone enable the ' \
              'rejected shared-palette path? See this file\'s docstring.'
    assert m.group(1) == '0', (
        'g_dungeon_bigpic_overlay was flipped to %s. That enables L58c4\'s '
        'L3f3c(32,255) shared-palette install, which wipes the per-set wall '
        'bands, the backdrop band and the menu palette. The per-set band model '
        'is CORRECT and the backdrop already renders. Rejected path.'
        % m.group(1))


@pytest.mark.skipif(not os.path.exists(BOOT), reason='boot.c not present')
def test_no_comment_calls_the_shared_palette_rewrite_pending_work():
    """No comment may sell the shared-palette rewrite as the faithful/pending fix.

    The exact wording that cost two detours: "Until the shared-palette CLUT
    model lands ... Flip to 1 once the dungeon uses the faithful shared
    palette." Ban the shape, not just that sentence.
    """
    src = _boot()
    bad = [
        r'faithful\s+shared\s+palette',
        r'shared-palette\s+CLUT\s+model\s+lands',
        r'[Uu]ntil\s+the\s+shared-palette',
        r'fabricated\s+per-band',
        r'[Ff]lip\s+to\s+1\s+once',
    ]
    hits = []
    for pat in bad:
        for m in re.finditer(pat, src):
            line = src.count('\n', 0, m.start()) + 1
            hits.append('  boot.c:%d  %r' % (line, m.group(0)))
    assert not hits, (
        'A comment is describing the REJECTED shared-palette dungeon CLUT as '
        'faithful/pending work:\n%s\n\nThe per-set band model is correct (see '
        'jt114) and the backdrop already renders. Do not resurrect this.'
        % '\n'.join(hits))
