#!/usr/bin/env python3
"""Did the planar tile cache's remap agreement survive the RE-BANDS?

#144 wants to cache wall tiles in PLANAR form, keyed on (slot, idx). That key is
only sound if a cached tile stays valid wherever it lands, and a cached tile has
the index->slot remap baked in — a remap that is PER BAND (band = y*nbands/h).
Tiles straddle band boundaries constantly (20-row bands, viewport rows 24..111),
so the question is whether the remap DIFFERS across the bands a tile spans.

The engine answers it per blit under FRUA_TILEPROF (+FRUA_R3DPROF for the dump),
logging `r3d: remap SAME` / `r3d: remap DIFFER`. This script answers the harder
half: whether that held ACROSS RE-BANDS.

★ WHY AN END-OF-RUN TOTAL IS NOT ENOUGH. A re-band rebuilds every band palette
from new content, so it is the one event that could invalidate a cached tile. A
final "DIFFER = 0" proves nothing if every re-band happened AFTER the last tile
was blitted — the comparison would simply never have been tested against one. So
walk the log in ORDER, pair each remap dump with the number of re-bands already
behind it, and report the degenerate cases as INCONCLUSIVE rather than as a pass.

Needs a build with all three: FRUA_TILEPROF + FRUA_R3DPROF (the tile/remap
counters and their dump) and FRUA_STPROF (the `b4audit: reband` markers). Miss
FRUA_STPROF and the re-band count reads zero, which looks like a clean result and
is really a blind one — that is exactly how the first attempt at this went.

  make CPU68K=68000 EXTRA_CFLAGS='-DFRUA_TILEPROF -DFRUA_R3DPROF -DFRUA_STPROF'
  tools/profile/st_profile.sh 140000 175000 400
  tools/profile/analyse_bands.py [DBG.LOG]

Exit status: 0 closed, 1 inconclusive, 2 broken.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DEFAULT = os.path.join(os.environ.get('GEMDOS_DIR',
                                      os.path.join(REPO, 'data/work/gamedata')),
                       'DBG.LOG')


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    try:
        fh = open(path, errors='replace')
    except OSError as e:
        print('cannot read %s: %s' % (path, e))
        return 1

    rebands = 0
    straddle = None
    rows = []                       # (rebands_before, straddle_cum, differ)
    with fh:
        for line in fh:
            if 'b4audit: reband' in line:
                rebands += 1
            m = re.search(r'r3d: STRADDLE cum=\s*(\d+)', line)
            if m:
                straddle = int(m.group(1))
            m = re.search(r'r3d: remap DIFFER=\s*(\d+)', line)
            if m:
                rows.append((rebands, straddle, int(m.group(1))))

    if not rows:
        print('no remap dumps in %s' % path)
        print('build with -DFRUA_TILEPROF -DFRUA_R3DPROF')
        return 1

    print('%14s %13s %7s' % ('rebands_before', 'straddle_cum', 'DIFFER'))
    show = rows if len(rows) <= 6 else rows[:3] + [None] + rows[-3:]
    for r in show:
        if r is None:
            print('%14s' % '...')
        else:
            print('%14d %13s %7d' % (r[0], r[1], r[2]))

    after = [r for r in rows if r[0] > 0]
    bad = [r for r in rows if r[2] > 0]
    print()
    print('total remap dumps           : %d' % len(rows))
    print('re-bands in the run         : %d' % rebands)
    print('dumps taken AFTER a re-band : %d' % len(after))
    if after:
        print('  straddles measured after  : %s' % after[-1][1])
        print('  max DIFFER after a reband : %d' % max(r[2] for r in after))
    print('dumps with DIFFER > 0       : %d' % len(bad))
    print()

    if rebands == 0:
        print('INCONCLUSIVE: no re-band happened, so agreement was never tested')
        print('              against one. Is FRUA_STPROF in the build?')
        return 1
    if not after:
        print('INCONCLUSIVE: every re-band came after the last tile blit.')
        return 1
    if bad:
        print('BROKEN: the remap disagrees after a re-band, so a cached tile is')
        print('        not valid across bands -> key must be (slot, idx, band).')
        return 2
    print('CLOSED: agreement holds across re-bands -> key stays (slot, idx).')
    return 0


sys.exit(main())
