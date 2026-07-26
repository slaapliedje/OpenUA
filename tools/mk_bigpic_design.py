#!/usr/bin/env python3
"""Build a module whose entry cell raises a BIGPIC prompt — the context the
Mac 1.2 Items-browser hunks (19-22) need.

Those four hunks all turn on `-22281`, the flag `jt182` passes to `l23b4`,
where it gates a per-iteration animation block. 1.0 suppressed it only around
the trade/give confirm inside `jt893`'s case 4; 1.2 hoists the suppression to
the whole browser. To see that, `-22281` has to be 1 when `jt893` is ENTERED,
and no shipped path through HEIRS gets there headlessly:

  * `l442e` sets it when an event's picture id is >= 240 — a bigpic backdrop
    (boot.c ~45211). Any event type with a picture field will do.
  * The tactical-combat setup CLEARS it (~46687, ~49124), so it cannot come
    from a fight.
  * `l085e` clears it on every STEP (~45676), so the browser must be opened
    without moving off the entry cell.

Two variants, because the arm matters as much as the flag:

  message (default)
      A plain bigpic message event. Reaches the browser, but outside a vault
      `l11a8` offers arm 4 (trade/give) where the visible "Drop" button lives —
      and arm 4 is the ONE arm 1.0 already suppressed, so ON and OFF agree.
      Useful as the negative control.

  --shop
      A shop event (type 8) with the same bigpic. `jt183` puts the play mode at
      1, which is what makes `l11a8` offer arms 7 (Sell) and 8 (Id) as well.
      Both call `jt159`, neither was suppressed by 1.0 — this is the variant
      that diverges.

    python3 tools/mk_bigpic_design.py data/work/gamedata --current --shop

Then: beginplay -> (Return to dismiss a message) -> v (View) -> i (Items) ->
click an item row -> click Sell. See docs/deterministic-ab.md.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dsn import Design, _walled_room, _hook          # noqa: E402
from geo import EVENT_SIZE                            # noqa: E402

BIGPIC_ID = 240        # >= 240 is what l442e reads as "bigpic backdrop"


def bigpic_design(name="BIGPROMPT", shop=False, picture=BIGPIC_ID, sprite=False):
    a5 = _walled_room(entry=(3, 3), facing=0)
    a5.strg_write(["", "A great painted mural fills the wall."])

    if shop:
        a5.set_shop(0, shop_type=0, picture=picture, stock=(1, 2, 3))
    else:
        a5.set_message(0, [1], picture=picture)

    if sprite:
        # ev[7] bit 7 routes a sub-240 picture id down l442e's SPRITE arm,
        # which puts it in the -22312 slot. Counter-intuitively that is the
        # slot l08ce feeds to l541a as **"PIC"** (the -22313 slot goes to
        # "SPRIT"), and only the PIC arm sets -24321 — the flag l23b4's
        # animation block needs. That is what hunk 7 turns on.
        ev = bytearray(a5.encr[0:EVENT_SIZE])
        ev[7] |= 0x80
        a5.encr[0:EVENT_SIZE] = bytes(ev)

    _hook(a5, 3, 3, special=1)                  # entry cell -> event 0

    d = Design(name, title="Bigpic prompt test")
    d.xp = 15000
    d.start_area = 5                            # >= 5 -> dungeon mode
    d.start_entry = 1
    d.add_area(5, a5)
    return d


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    base = argv[0]
    shop = "--shop" in argv
    name = "SHOPPIC" if shop else "BIGPROMPT"
    picture = BIGPIC_ID
    if "--picture" in argv:
        picture = int(argv[argv.index("--picture") + 1])

    d = bigpic_design(name, shop=shop, picture=picture,
                      sprite=("--sprite" in argv))
    folder = d.write(base, make_current=("--current" in argv))
    ev = d.areas[5].event(0)
    print("wrote", folder)
    print("  event 0: type=%d ev[6]=%d ev[7]=0x%02x (%s)"
          % (ev[0], ev[6], ev[7], "shop" if shop else "message"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
