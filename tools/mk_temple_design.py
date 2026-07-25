#!/usr/bin/env python3
"""Build TEMPLE.DSN — a test module for the Mac 1.2 endian fix (hunks 27/28).

1.2 byte-swaps the 4-byte money field at `ev[8]` before handing it to `jt933`
(CODE 4+0x22aa = the port's `jt1199`). ENCR records are little-endian, so a raw
long read off `ev+8` on a 68k comes back reversed.

Every one of the 192 type-9 temple events in the fan modules on hand stores
`01 00 00 00` there — little-endian 1, which 1.0 reads as 16,777,216. That is a
real value but a poor probe: 1 and its byte-swap are both "wrong-looking"
numbers and easy to confuse. This authors a distinctive one instead, so the two
readings are unmistakable:

    bytes 8..11 = 0x64 0x00 0x00 0x00
      little-endian (1.2, correct) ->            100
      big-endian   (1.0, the bug)  ->  1,677,721,600

    python3 tools/mk_temple_design.py data/work/gamedata --current
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dsn import Design, _walled_room, _hook          # noqa: E402
from geo import EVENT_SIZE                            # noqa: E402

PROBE = 100          # a value whose byte-swap is unmistakable


def temple_design(name="TEMPLE", amount=PROBE):
    a5 = _walled_room(entry=(3, 3), facing=0)
    a5.strg_write(["", "Temple endian probe chamber.",
                   "The priest regards you expectantly."])

    a5.set_temple(0, intro_text=2, wish_text=2, healing=True)

    # Bytes 8..11 are not in docs/geo-format.md's type-9 map — they are the
    # 4-byte money field hunks 25-28 concern. Write it LITTLE-ENDIAN, which is
    # how every shipped design stores it.
    ev = bytearray(a5.encr[0:EVENT_SIZE])
    ev[8]  = amount & 0xff
    ev[9]  = (amount >> 8) & 0xff
    ev[10] = (amount >> 16) & 0xff
    ev[11] = (amount >> 24) & 0xff
    a5.encr[0:EVENT_SIZE] = bytes(ev)

    _hook(a5, 3, 3, special=1)                  # entry cell -> event 0

    d = Design(name, title="Temple endian probe")
    d.xp = 15000
    d.start_area = 5                            # >= 5 -> dungeon mode
    d.start_entry = 1
    d.add_area(5, a5)
    return d


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    d = temple_design()
    folder = d.write(argv[0], make_current=("--current" in argv))
    ev = d.areas[5].event(0)
    le = ev[8] | (ev[9] << 8) | (ev[10] << 16) | (ev[11] << 24)
    be = (ev[8] << 24) | (ev[9] << 16) | (ev[10] << 8) | ev[11]
    print("wrote", folder)
    print("  event 0: type=%d bytes[8:12]=%s" % (d.areas[5].event_type(0),
                                                 bytes(ev[8:12]).hex(' ')))
    print("  little-endian (1.2, correct) = %d" % le)
    print("  big-endian    (1.0, the bug) = %d" % be)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
