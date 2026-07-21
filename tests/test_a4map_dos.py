"""The DOS scalar map must reconstruct the Mac A5 bytes it claims to cover.

The shipped map (g_a5_dos_scalars) is positions + checksums only; the VALUES
come from the user's own CKIT.EXE at install time. That is only sound if the
DOS bytes at each recorded offset really are the Mac bytes — these tests
verify that end to end, including the zero-gap coalescing that re-fuses
tables scalar_runs() shreds at interior zeros (#75: the -27848 terrain
descriptor table lost 40 of its 70 entries to that shredding, and a no-replay
combat map grew wall chunks).

Skips when the copyrighted inputs (Mac resource fork, DOS CKIT.EXE) are not
staged under the git-ignored data/.
"""
import os
import sys

import pytest

RFORK = "data/work/UnlimitedAdventures.rfork"
DOSEXE = "data/dos-frua/CKIT.EXE"

pytestmark = pytest.mark.skipif(
    not (os.path.exists(RFORK) and os.path.exists(DOSEXE)),
    reason="Mac resource fork / DOS CKIT.EXE not staged under data/",
)


@pytest.fixture(scope="module")
def world():
    sys.path.insert(0, "tools")
    from macrsrc import ResourceFork
    import datapool
    import a4map
    rf = ResourceFork.from_file(RFORK)
    res = {(r.type, r.id): r.data for r in rf.resources}
    img = datapool.expand_data(res[("DATA", 0)], res[("ZERO", 0)])
    drel = res[("DREL", 0)]
    dos = open(DOSEXE, "rb").read()
    runs = a4map.scalar_runs(img, drel)
    loc, res_runs = a4map.split_by_dos(
        a4map.coalesce_runs_by_dos(runs, dos), dos)
    return img, dos, loc, res_runs


def test_dos_bytes_equal_mac_bytes(world):
    """Every mapped run: CKIT.EXE at `off` holds the run verbatim, and every
    NON-ZERO byte matches the Mac image at its A5 position.

    Zero bytes are not compared against the image: a merged run's interior
    gaps may sit on relocation-covered Mac bytes (pointer slots), which the
    runtime never touches — dos_scalars_load skips zero source bytes, so the
    A4 map still fills them.
    """
    img, dos, loc, _ = world
    assert loc, "no DOS-locatable runs at all?"
    for slot, b, off in loc:
        assert dos[off:off + len(b)] == b
        base = len(img) + slot
        for k, v in enumerate(b):
            if v:
                assert img[base + k] == v, \
                    f"run at A5{slot}: byte +{k} disagrees with the Mac image"


def test_terrain_table_is_one_run(world):
    """#75 regression: the -27848 terrain descriptor table (70 entries x 4B)
    must be fully covered — its interior zeros shred it into sub-min_run
    fragments unless the coalescer re-fuses it."""
    img, dos, loc, _ = world
    for a5 in range(-27848, -27568):
        assert any(slot <= a5 < slot + len(b) for slot, b, _ in loc), \
            f"terrain table byte A5{a5} not covered by the DOS map"


def test_partial_split_recovers_welded_tables(world):
    """Partial-run splitting regressions: a maximal non-zero Mac run can weld
    unrelated tables together, and DOS lays the parts elsewhere — whole-run
    matching missed these. The stat-limits table (A5-30890, 26B, welded to
    the race/class matrix) and the A5-8673 curve (97B, the un-derivable half
    of the deferred pair) must be fully DOS-covered; the A5-804 pitch table
    is in NO file of the DOS install and must stay residue (it still comes
    from the Mac DATA replay)."""
    _, _, loc, res_runs = world

    def coverage(slot, n):
        return sum(1 for s in range(slot, slot + n)
                   if any(rs <= s < rs + len(b) for rs, b, _ in loc))

    assert coverage(-30890, 26) == 26, "stat min/max table not recovered"
    assert coverage(-8673, 97) == 97, "A5-8673 curve not recovered"
    assert coverage(-804, 24) == 0, \
        "pitch table suddenly DOS-covered — a different DOS build?"


def test_runs_fit_runtime_buffer(world):
    """No run may exceed dos_scalars.c's DOS_RUN_MAX (1024): the loader
    refuses the whole file on an oversized run."""
    _, _, loc, _ = world
    assert max(len(b) for _, b, _ in loc) <= 1024


def test_coalescing_only_helps(world):
    """The residue after coalescing is a subset of the residue without it —
    re-fusing may only move bytes from residue to the shipped map."""
    img, dos, loc, res_runs = world
    sys.path.insert(0, "tools")
    import a4map
    _, plain_res = a4map.split_by_dos(a4map.scalar_runs(img, _drel()), dos)
    res_bytes = sum(len(b) for _, b in res_runs)
    plain_res_bytes = sum(len(b) for _, b in plain_res)
    assert res_bytes <= plain_res_bytes


def _drel():
    sys.path.insert(0, "tools")
    from macrsrc import ResourceFork
    rf = ResourceFork.from_file(RFORK)
    return {(r.type, r.id): r.data for r in rf.resources}[("DREL", 0)]
