#!/usr/bin/env python3
"""Aggregate a Hatari `profile cycles` dump to FUNCTIONS.

★ Hatari's own `profile symbols` is not enough: it lists only addresses that
sit exactly ON a symbol, i.e. function entry points, and the cycles live in the
bodies. A 25-symbol request came back with ONE row. So take the flat per-address
ranking and map each address to its enclosing symbol here.

★ AND FILTER THE COMPILER-LOCAL LABELS. `nm` emits .L / .LBB / .LBE labels and
object-file markers; left in, they swallow the ranking and every hot address
maps to a meaningless name (`.LBB429  29.6%`). The same trap bit the #96
multiply histogram.

The load base comes from the profile's own PROGRAM_TEXT line when present,
else the ST default TPA text start.

Usage: st_aggregate.py <hatari-run.log> [binary]
"""
import bisect
import re
import subprocess
import sys

LOG = sys.argv[1]
BIN = sys.argv[2] if len(sys.argv) > 2 else 'frua.prg'


def load_symbols(path):
    out = subprocess.run(['m68k-atari-mint-nm', '-n', path],
                         capture_output=True, text=True).stdout
    syms = []
    for line in out.splitlines():
        f = line.split()
        if len(f) != 3 or f[1] not in ('T', 't'):
            continue
        name = f[2]
        if name.startswith('.L') or name.endswith('.o') or name in ('etext', '_etext'):
            continue
        syms.append((int(f[0], 16), name))
    syms.sort()
    return syms


def main():
    text = open(LOG, errors='replace').read()
    m = re.search(r'PROGRAM_TEXT:\s*0x([0-9a-f]+)-0x([0-9a-f]+)', text)
    base = int(m.group(1), 16) if m else 0x018872
    # ★ AND THE END. Without it every TOS ROM address (0xe0xxxx) lands past the
    # last program symbol and is silently attributed to `__etext`, which read as
    # 12-20% of "our" cycles. TOS is real work but it is not ours; count it
    # separately so the program shares are shares OF THE PROGRAM.
    top = int(m.group(2), 16) if m else 0x1199e6

    syms = load_symbols(BIN)
    if not syms:
        sys.exit('no symbols from %s — build without stripping' % BIN)
    addrs = [s[0] for s in syms]

    agg, total, other = {}, 0, 0
    for line in text.splitlines():
        m = re.match(r'^0x([0-9a-f]+)\s+[\d.]+%\s+(\d+)', line)
        if not m:
            continue
        a, cyc = int(m.group(1), 16), int(m.group(2))
        if a < base or a > top:
            other += cyc                  # TOS ROM / cartridge / outside .text
            continue
        i = bisect.bisect_right(addrs, a - base) - 1
        name = syms[i][1] if i >= 0 else '?'
        agg[name] = agg.get(name, 0) + cyc
        total += cyc

    if not total:
        sys.exit('no profile rows found — did the close breakpoint fire?')
    print('program cycles in the ranked addresses: %d (%.1f s at 8.02 MHz)'
          % (total, total / 8021247.0))
    print('outside the program (TOS ROM etc): %d (%.1f%% of ranked)\n'
          % (other, 100.0 * other / (total + other) if total + other else 0))
    print('%-34s %14s %7s' % ('function', 'cycles', 'share'))
    for k, v in sorted(agg.items(), key=lambda x: -x[1])[:20]:
        print('%-34s %14d %6.1f%%' % (k, v, 100.0 * v / total))

    # ★ SAY WHICH SCREEN THIS IS, because the ranking looks equally plausible
    # either way. st_profile.sh once booted straight to the main menu and pressed
    # arrow keys at it; the resulting profile was published as "the play loop".
    # The 3D walk renderer is the discriminator: in the dungeon it and the
    # viewport blitters carry real cycles, at the menu they are absent.
    # nm prefixes these with '_'; match on the bare names too.
    names = ('render_3d', 'qd_planar_bridge', 'dc_plane_bridge', 'st_vp_composite')
    walk = sum(v for k, v in agg.items() if k.lstrip('_').startswith(names))
    share = 100.0 * walk / total
    print('\n3D/viewport work: %.1f%% of program cycles' % share)
    if share < 1.0:
        print('*** WARNING: this profile has essentially NO 3D work in it. It is')
        print('*** almost certainly a MENU profile, not the play loop. Do not')
        print('*** quote it as play-loop shares.')


main()
