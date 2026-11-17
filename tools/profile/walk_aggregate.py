#!/usr/bin/env python3
"""Aggregate one window of a tools/profile/walk_profile.sh run.

    walk_aggregate.py <run.log> <WALKDUMP|IDLEDUMP> <walk.prof|idle.prof> <frua.prg> [rows]

Hatari's `profile save` writes the per-address disassembly to the console (so
it is in run.log, after the mark the script echoes) and the caller graph and
the PROGRAM_TEXT range to the .prof file. Exclusive cycles are summed per
function from the first; calls and inclusive time come from the second.

Unlike st_aggregate.py this needs no load-address fallback: the range is read
from the profile itself, so it is right on every machine. Two cautions, both
measured: Hatari's INCLUSIVE figures are wrong across tail calls (a function
that jumps into another is charged twice or not at all) — cross-check against
exclusive sums; and libc routines appear under several internal labels
(memcpy as copy16_d / less256_d / ...), which have to be added up by hand.
"""
import bisect, re, subprocess, sys

log, mark, prof, binp = sys.argv[1:5]
rows = int(sys.argv[5]) if len(sys.argv) > 5 else 32

hdr = open(prof, errors='replace').read()
m = re.search(r'PROGRAM_TEXT:\s*0x([0-9a-f]+)-0x([0-9a-f]+)', hdr)
base, top = int(m.group(1), 16), int(m.group(2), 16)
cps = int(re.search(r'Cycles/second:\s*(\d+)', hdr).group(1))

syms = []
for line in subprocess.run(['m68k-atari-mint-nm', '-n', binp], capture_output=True, text=True).stdout.splitlines():
    f = line.split()
    if len(f) != 3 or f[1] not in 'Tt':
        continue
    n = f[2]
    if n.startswith('.L') or n.endswith('.o') or n in ('etext', '_etext'):
        continue
    syms.append((int(f[0], 16), n))
syms.sort()
addrs = [s[0] for s in syms]

def fn(a):
    if a < base or a >= top:
        return None
    i = bisect.bisect_right(addrs, a - base) - 1
    return syms[i][1] if i >= 0 else '?'

text = open(log, errors='replace').read()
i = text.index(mark + '> profile save')
j = text.find('\n> ', i + 10)
seg = text[i:j if j > 0 else None]
excl, instr, tos, tosins = {}, {}, 0, 0
rx = re.compile(r'^([0-9a-f]{8}) .* [\d.]+% \((\d+), (\d+), (\d+), (\d+)\)$', re.M)
for mm in rx.finditer(seg):
    a, ins, cyc = int(mm.group(1), 16), int(mm.group(2)), int(mm.group(3))
    f = fn(a)
    if f is None:
        tos += cyc; tosins += ins
    else:
        excl[f] = excl.get(f, 0) + cyc
        instr[f] = instr.get(f, 0) + ins
total = sum(excl.values())

# inclusive, from the callers section
incl, calls = {}, {}
for line in hdr.splitlines():
    mm = re.match(r'^0x([0-9a-f]+): (.*), (\S+)$', line)
    if not mm:
        continue
    name = mm.group(3)
    for c in mm.group(2).split(', '):
        p = c.split()
        # caller = calls types incl excl
        if len(p) >= 5 and 's' in p[3]:
            calls[name] = calls.get(name, 0) + int(p[2])
            incl[name] = incl.get(name, 0) + int(p[4].split('/')[2])

print('window: program %.2f s (%d cycles), outside program (TOS etc) %.2f s; %d Hz clock'
      % (total / cps, total, tos / cps, cps))
print('\n%-30s %13s %6s %7s | %10s %7s %6s' % ('function (EXCLUSIVE rank)', 'cycles', 'share', 'sec', 'calls', 'incl s', 'incl%'))
for k, v in sorted(excl.items(), key=lambda x: -x[1])[:rows]:
    print('%-30s %13d %5.1f%% %7.2f | %10s %7s %6s' % (
        k, v, 100.0 * v / total, v / cps, calls.get(k, ''),
        ('%.2f' % (incl[k] / cps)) if k in incl else '',
        ('%.1f' % (100.0 * incl[k] / (total + tos))) if k in incl else ''))
print('\n%-30s %10s %8s %6s   (INCLUSIVE rank; %% of whole window incl. TOS)' % ('function', 'calls', 'incl s', 'incl%'))
for k, v in sorted(incl.items(), key=lambda x: -x[1])[:rows + 28]:
    print('%-30s %10d %8.2f %5.1f%%' % (k, calls[k], v / cps, 100.0 * v / (total + tos)))
