#!/usr/bin/env python3
"""Can the Mac `STRS` string pool be sourced from the DOS `CKIT.EXE`?

WHY: the port's one hard Mac dependency is `frua.rsc` (see
docs/redistributable-binary.md and GAMEDATA.md).  Design data is already
byte-identical between releases and art already converts (tools/art_convert.py),
so a player who owns only the DOS release is blocked on exactly two things: the
A5 world (DATA/ZERO/DREL) and the string pool (STRS).  This probe answers the
second, and it is the cheaper of the two to falsify — if the two releases'
string pools could not be reconciled, DOS-independence would be dead regardless
of any A5 work, so it is worth settling first.

METHOD.  Split the Mac `STRS` resource on NUL into (pool_offset, bytes)
entries, then look for each entry inside the DOS executable.  A match counts
as RECOVERED only when it appears as a whole NUL-delimited string in DOS too —
the loose "is this byte sequence present anywhere" test scores several points
higher but admits false positives, where a short Mac string is really a
fragment of a longer DOS one ('Pod' inside 'Podium').

An entry found at several DOS offsets is not ambiguous for our purposes: every
occurrence is the identical byte sequence, so any one of them serves as the
extraction offset.

RESULT (Mac 1.0 fork vs the GOG DOS release; see docs/dos-strings-probe.md):
2068/2145 recovered (96.4%), 40 substring-only, 37 absent — and every absent
string is Mac-platform-specific (`:DISK4:ALWAYS.CTL`, the `.ctl` art-name
templates, MacPaint/PICT import, the Mac memory-partition warning, Toolbox
StandardFile prompts, the Mac sound engine's diagnostics).  No creative game
text is missing.

The emitted map holds offsets and lengths only — never the text itself — so it
is distributable; the words come from the user's own CKIT.EXE at install time,
exactly as art_convert.py takes their own art.
"""
import argparse
import json
import sys


def parse_strs(blob):
    """Split a STRS pool into [(pool_offset, bytes)] for non-empty entries."""
    out, off = [], 0
    for part in blob.split(b"\x00"):
        if part:
            out.append((off, part))
        off += len(part) + 1
    return out


def whole_hits(needle, hay, cap=8):
    """Offsets where `needle` sits in `hay` as a whole NUL-delimited string."""
    out, i, n = [], hay.find(needle), len(needle)
    while i >= 0 and len(out) < cap:
        if (i == 0 or hay[i - 1] == 0) and \
           (i + n >= len(hay) or hay[i + n] == 0):
            out.append(i)
        i = hay.find(needle, i + 1)
    return out


def probe(strs, exe):
    """Classify every STRS entry against the DOS executable.

    Returns (recovered, substring_only, absent) where recovered is a list of
    (pool_offset, length, [dos_offsets]) and the other two are
    [(pool_offset, bytes)].
    """
    recovered, substring_only, absent = [], [], []
    for off, s in parse_strs(strs):
        hits = whole_hits(s, exe)
        if hits:
            recovered.append((off, len(s), hits))
        elif exe.find(s) >= 0:
            substring_only.append((off, s))
        else:
            absent.append((off, s))
    return recovered, substring_only, absent


def build_map(recovered):
    """The distributable extraction table: pool offset -> (dos offset, len).

    Contains no text — only positions — so it carries no copyrighted bytes.
    """
    return {str(off): [hits[0], length] for off, length, hits in recovered}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("strs", help="Mac STRS resource, raw (see --from-rfork)")
    ap.add_argument("exe", help="DOS CKIT.EXE")
    ap.add_argument("--from-rfork", action="store_true",
                    help="treat `strs` as a Mac resource fork and pull STRS 0")
    ap.add_argument("--emit-map", metavar="JSON",
                    help="write the pool-offset -> (dos-offset, len) table")
    ap.add_argument("--show", type=int, default=12,
                    help="how many absent/substring samples to print")
    args = ap.parse_args(argv)

    if args.from_rfork:
        sys.path.insert(0, __file__.rsplit("/", 1)[0])
        from macrsrc import ResourceFork
        rf = ResourceFork.from_file(args.strs)
        strs = {(r.type, r.id): r.data for r in rf.resources}[("STRS", 0)]
    else:
        strs = open(args.strs, "rb").read()
    exe = open(args.exe, "rb").read()

    recovered, substring_only, absent = probe(strs, exe)
    n = len(recovered) + len(substring_only) + len(absent)
    if not n:
        print("no STRS entries found", file=sys.stderr)
        return 1

    print(f"Mac STRS entries : {n}")
    print(f"DOS executable   : {len(exe)} bytes")
    print(f"  recovered      : {len(recovered):5d}  ({100*len(recovered)/n:5.1f}%)")
    print(f"  substring only : {len(substring_only):5d}  ({100*len(substring_only)/n:5.1f}%)")
    print(f"  absent         : {len(absent):5d}  ({100*len(absent)/n:5.1f}%)")

    multi = sum(1 for _, _, h in recovered if len(h) > 1)
    print(f"\n  {multi} recovered entries occur more than once (harmless —"
          f" identical bytes)")

    if args.show:
        print("\n--- substring-only (a longer DOS string contains these) ---")
        for off, s in substring_only[:args.show]:
            print(f"  @{off:6d}  {s[:56]!r}")
        print("\n--- absent ---")
        for off, s in absent[:args.show]:
            print(f"  @{off:6d}  {s[:56]!r}")

    if args.emit_map:
        table = build_map(recovered)
        with open(args.emit_map, "w") as fh:
            json.dump(table, fh, indent=1, sort_keys=True)
        print(f"\nwrote {len(table)} entries -> {args.emit_map}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
