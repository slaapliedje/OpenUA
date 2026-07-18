#!/usr/bin/env python3
"""Generate an AmigaOS Workbench icon (.info / old-style DiskObject).

The .info binary is the classic OS1.x/2.x/3.x DiskObject any Workbench reads
(magic 0xE310) — a header, one planar image, and optional DefaultTool /
ToolTypes strings. We emit a WBTOOL icon carrying a STACK tooltype and a
matching do_StackSize so a double-clicked tool is given a generous stack.

Used to build uainst.info for the Amiga release (a tool with no icon cannot
be launched from Workbench at all). Self-contained: no copyrighted Commodore
imagery — the default glyph is drawn here.

Format references decoded byte-for-byte against a stock WB3.2 Tools.info:
  DiskObject is 78 bytes; do_StackSize is the last LONG. Strings are stored
  as a LONG length (including the NUL) followed by the bytes. ToolTypes is a
  LONG ((n+1)*4) then n such strings. Multi-byte fields are big-endian.
"""
import argparse
import struct
import sys

# --- DiskObject constants ---------------------------------------------------
DO_MAGIC = 0xE310
DO_VERSION = 1
WB_DISK, WB_DRAWER, WB_TOOL, WB_PROJECT = 1, 2, 3, 4
NO_ICON_POSITION = 0x80000000

# Gadget flags / activation (icon.library conventions)
GFLG_GADGIMAGE = 0x0004          # GadgetRender is an Image
GFLG_GADGHCOMP = 0x0000          # highlight by colour-complement (one image)
GACT_RELVERIFY = 0x0001
GACT_IMMEDIATE = 0x0002
GTYP_BOOLGADGET = 0x0001

# Default WB palette indices used by the built-in glyph.
C_BG, C_BLACK, C_WHITE, C_BLUE = 0, 1, 2, 3


def default_glyph(w=30, h=24):
    """Draw a simple 'install into a tray' glyph: a boxed down-arrow over a
    baseline. Returns a list of h rows, each a list of w colour indices."""
    px = [[C_BG] * w for _ in range(h)]
    # outer border
    for x in range(w):
        px[0][x] = px[h - 1][x] = C_BLACK
    for y in range(h):
        px[y][0] = px[y][w - 1] = C_BLACK
    cx = w // 2
    # arrow shaft
    for y in range(4, h - 9):
        px[y][cx - 1] = px[y][cx] = C_BLUE
    # arrow head (a downward triangle)
    for i, y in enumerate(range(h - 10, h - 6)):
        half = 4 - i
        for x in range(cx - half, cx + half):
            if 0 < x < w - 1:
                px[y][x] = C_BLUE
    # tray / baseline the arrow points into
    for x in range(4, w - 4):
        px[h - 4][x] = C_BLACK
    return px


def parse_glyph(text):
    """Parse an ASCII glyph: '.'=bg '#'=black '*'=white '+'=blue. Rows padded
    to the widest line. Blank input -> None (use the default)."""
    rows = [ln.rstrip("\n") for ln in text.splitlines() if ln.strip("\r")]
    if not rows:
        return None
    w = max(len(r) for r in rows)
    m = {".": C_BG, "#": C_BLACK, "*": C_WHITE, "+": C_BLUE, " ": C_BG}
    out = []
    for r in rows:
        out.append([m.get(c, C_BG) for c in r.ljust(w, ".")])
    return out


def pack_image(px, depth=2):
    """Serialise a colour-index bitmap as an Amiga planar Image (struct + data).
    Rows are word-aligned; planes are stored low-plane first."""
    h = len(px)
    w = len(px[0])
    words_per_row = (w + 15) // 16
    body = bytearray()
    for plane in range(depth):
        for row in px:
            for word_i in range(words_per_row):
                word = 0
                for bit in range(16):
                    x = word_i * 16 + bit
                    if x < w and ((row[x] >> plane) & 1):
                        word |= 1 << (15 - bit)
                body += struct.pack(">H", word)
    plane_pick = (1 << depth) - 1
    # struct Image: LeftEdge, TopEdge, Width, Height, Depth, ImageData(!=0),
    # PlanePick, PlaneOnOff, NextImage
    hdr = struct.pack(">hhHHHI BB I", 0, 0, w, h, depth, 1,
                      plane_pick, 0, 0)
    return hdr + bytes(body)


def pack_string(s):
    b = s.encode("latin-1") + b"\x00"
    return struct.pack(">I", len(b)) + b


def build_icon(icon_type=WB_TOOL, stack=200000, tooltypes=None,
               default_tool="", glyph=None):
    if glyph is None:
        glyph = default_glyph()
    tooltypes = tooltypes or []
    img = pack_image(glyph)
    w = len(glyph[0])
    h = len(glyph)

    out = bytearray()
    out += struct.pack(">HH", DO_MAGIC, DO_VERSION)
    # embedded struct Gadget (44 bytes)
    out += struct.pack(
        ">I hhhh HHH I I I i I H I",
        0,                                  # ga_Next
        0, 0, w, h,                         # LeftEdge, TopEdge, Width, Height
        GFLG_GADGIMAGE | GFLG_GADGHCOMP,    # ga_Flags
        GACT_RELVERIFY | GACT_IMMEDIATE,    # ga_Activation
        GTYP_BOOLGADGET,                    # ga_GadgetType
        1,                                  # ga_GadgetRender (image present)
        0,                                  # ga_SelectRender (none)
        0,                                  # ga_GadgetText
        0,                                  # ga_MutualExclude
        0,                                  # ga_SpecialInfo
        0,                                  # ga_GadgetID
        1,                                  # ga_UserData (icon revision 1)
    )
    out += struct.pack(">BB", icon_type, 0)             # do_Type, pad
    out += struct.pack(">I", 1 if default_tool else 0)  # do_DefaultTool
    out += struct.pack(">I", 1 if tooltypes else 0)     # do_ToolTypes
    out += struct.pack(">II", NO_ICON_POSITION, NO_ICON_POSITION)  # CurrentX/Y
    out += struct.pack(">I", 0)                         # do_DrawerData
    out += struct.pack(">I", 0)                         # do_ToolWindow
    out += struct.pack(">I", stack)                     # do_StackSize

    out += img                                          # ga_GadgetRender image
    if default_tool:
        out += pack_string(default_tool)
    if tooltypes:
        out += struct.pack(">I", (len(tooltypes) + 1) * 4)
        for tt in tooltypes:
            out += pack_string(tt)
    return bytes(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Generate an AmigaOS .info icon.")
    ap.add_argument("-o", "--out", required=True, help="output .info path")
    ap.add_argument("--type", choices=["tool", "project", "drawer", "disk"],
                    default="tool")
    ap.add_argument("--stack", type=int, default=200000,
                    help="do_StackSize (Workbench launch stack)")
    ap.add_argument("--tooltype", action="append", default=[],
                    help="a TOOLTYPE=value string (repeatable)")
    ap.add_argument("--default-tool", default="",
                    help="DefaultTool (projects only)")
    ap.add_argument("--glyph", help="ASCII glyph file (.=bg #=black *=white +=blue)")
    args = ap.parse_args(argv)

    tmap = {"tool": WB_TOOL, "project": WB_PROJECT,
            "drawer": WB_DRAWER, "disk": WB_DISK}
    glyph = None
    if args.glyph:
        with open(args.glyph) as f:
            glyph = parse_glyph(f.read())

    data = build_icon(tmap[args.type], args.stack, args.tooltype,
                      args.default_tool, glyph)
    with open(args.out, "wb") as f:
        f.write(data)
    print(f"wrote {args.out} ({len(data)} bytes, "
          f"stack={args.stack}, tooltypes={args.tooltype})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
