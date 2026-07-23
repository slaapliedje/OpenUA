#!/usr/bin/env python3
"""Build frua.rsc from the DOS release — no Mac files needed (ADR-0017).

The engine's one hard runtime dependency on the Macintosh release is
`frua.rsc`, and the only resource in it a DOS-sourced install genuinely
needs is the `STRS` string pool: the reconstructed A5 world's 1016 pointer
relocations point into it, and the lifted code compares against its strings
at fixed offsets. This tool rebuilds that pool byte-exactly from the user's
own DOS `CKIT.EXE`:

  - 2108 of 2145 entries are read straight out of CKIT.EXE via a committed
    positions-only table (installer/strs_map_dos12.json — offsets and
    lengths, never text);
  - the remaining 37 are Mac-platform infrastructure (filename templates,
    memory diagnostics, StandardFile prompts) with no counterpart in any DOS
    file; they are port-authored below (ADR-0017 decision 6) and are
    functional identifiers, so they must be stated verbatim.

The result is a minimal FRSC archive holding just `STRS`. It deliberately
contains no DATA/ZERO/DREL — their absence is what routes the engine onto
the reconstructed-A5 path (authored scalars + relocation tables + DOS scalar
runs) at boot.

The map is derived against ONE exact executable (the GOG/Steam v1.2
CKIT.EXE; both ship the identical file). A repacked or different build
shifts every offset, so the tool verifies the executable's size and SHA-256
first and refuses loudly on a mismatch rather than emitting garbage.

Usage:
    rsrc_from_dos.py <CKIT.EXE> [-o frua.rsc] [--map JSON]
"""
import argparse
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsrcpack

def _default_map():
    """The STRS positions table: ../installer/ in the repo layout, or flat
    next to this script in a release-zip layout."""
    here = os.path.dirname(os.path.abspath(__file__))
    for p in (os.path.join(here, os.pardir, "installer", "strs_map_dos12.json"),
              os.path.join(here, "strs_map_dos12.json")):
        if os.path.isfile(p):
            return p
    return os.path.join(here, "strs_map_dos12.json")


DEFAULT_MAP = _default_map()

# The 37 Mac-only STRS entries, port-authored (ADR-0017 decision 6). Each is
# platform plumbing, not game content: art/control filename templates, the
# Mac memory-partition diagnostics, StandardFile prompts, the sound engine's
# messages, and the version banner. Offsets are pool positions; the bytes
# must match the original pool exactly because the engine builds filenames
# from the templates and compares against the identifiers.
AUTHORED = {
    66:    b":DISK4:ALWAYS.CTL",
    1652:  b"%s.ctl",
    1668:  b"%s%d%03d.ctl",
    1696:  b"%s.ctl",
    1810:  b"topview",
    1884:  b"music",
    5430:  b"Quit Game? ",
    7084:  b"Version 1.0       April 27,1993",
    11762: b"MacPaint File:",
    11786: b"PICT File:",
    11816: b"MENU",
    11838: b"PIC%c1%03d.ctl",
    11868: b"SPRI0%03d.ctl",
    11932: b"CPIC1%03d.ctl",
    11960: b"BIGP0%03d.ctl",
    12032: b"CTL",
    12318: b"Insufficent Memory",          # sic — the original's spelling
    12338: b"Try increasing the amount of memory",
    12374: b"allocated for the program.",
    16834: b"Insufficient FAR Memory!",
    17360: b"system",
    17368: b"pref.dat",
    17378: b"desktop",
    17386: b"File to save",
    17406: b"Yes\n",
    17412: b"No\n",
    17416: b"File to open",
    17430: b"Moebius",
    17438: b"Out of Memory!",
    27968: b"Insufficient memory in MLoad",
    27998: b"slb",
    28002: b"Insufficient memory in MLoad\n",
    28032: b"Song out of range (%d/%d)",
    28062: b"Please Insert %s",
    28270: b"CTL",
    28954: b"Insufficient memory in DNPInit",
    28986: b"Insufficient memory in DNPInit",
}


class _Res:
    def __init__(self, rtype, rid, data):
        self.type, self.id, self.attrs, self.data = rtype, rid, 0, data


def build_strs(exe, doc):
    """Reconstruct the STRS pool from the DOS executable + the map."""
    pool = bytearray(doc["pool_size"])
    for pool_off, (dos_off, length) in doc["entries"].items():
        pool_off = int(pool_off)
        pool[pool_off:pool_off + length] = exe[dos_off:dos_off + length]
    for off, s in AUTHORED.items():
        pool[off:off + len(s)] = s
    return bytes(pool)


def build_rsc(exe, doc):
    return rsrcpack.build_archive([_Res("STRS", 0, build_strs(exe, doc))])


def verify_exe(exe, doc):
    """(ok, message) — refuse a DOS build the map was not derived against."""
    if len(exe) != doc["ckit_size"]:
        return False, (f"CKIT.EXE is {len(exe)} bytes, expected "
                       f"{doc['ckit_size']} — a different DOS build")
    digest = hashlib.sha256(exe).hexdigest()
    if digest != doc["ckit_sha256"]:
        return False, ("CKIT.EXE checksum mismatch — a different DOS build; "
                       "refusing rather than extracting garbage")
    return True, "CKIT.EXE verified (GOG/Steam v1.2)"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("exe", help="the DOS release's CKIT.EXE")
    ap.add_argument("-o", "--out", default="frua.rsc",
                    help="output archive (default frua.rsc)")
    ap.add_argument("--map", default=DEFAULT_MAP,
                    help="positions map JSON (default the committed one)")
    args = ap.parse_args(argv)

    with open(args.map) as fh:
        doc = json.load(fh)
    exe = open(args.exe, "rb").read()

    ok, msg = verify_exe(exe, doc)
    print(msg, file=sys.stderr)
    if not ok:
        return 1

    blob = build_rsc(exe, doc)
    with open(args.out, "wb") as fh:
        fh.write(blob)
    print(f"wrote {args.out}: {len(blob)} bytes "
          f"(STRS pool {doc['pool_size']} bytes, "
          f"{len(doc['entries'])} extracted + {len(AUTHORED)} authored "
          f"entries)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
