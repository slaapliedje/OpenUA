#!/usr/bin/env python3
"""stage_dos — build a complete OpenUA game folder from the DOS release alone.

Point it at your GOG/Steam FRUA install (the folder holding CKIT.EXE and
DISK1/DISK2/DISK3) and at the game folder you want to play from (the one
holding frua.prg / frua), and it performs the whole ADR-0017 pipeline that
`make gamedata-dos` runs for developers:

  1. copy the DISK1..3 data files flat + every *.DSN design folder
  2. convert all DOS art to the engine's .ctl twins   (art_convert)
  3. convert the root data banks HLIB -> GLIB          (glb2glib)
  4. synthesize MUSIC.SLB from the DOS XMI soundtrack  (xmi2slb)
  5. rebuild SOUNDS.GLB (DIG8) from SFXDQ.VOC          (voc2glb)
  6. build frua.rsc from CKIT.EXE                      (rsrc_from_dos)
  7. extract the colour mouse pointers to frua.cur     (hlib_extract)
  8. write start.dat naming the design to boot

No Mac files are needed. Existing files in the game folder are overwritten
but nothing is deleted, so save games and rolled characters survive a re-run.

usage: stage_dos.py <dos_install_dir> <game_folder> [--design NAME.DSN]
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import glb2glib          # noqa: E402
import rsrc_from_dos     # noqa: E402
import voc2glb           # noqa: E402
import xmi2slb           # noqa: E402


def find_dir(base, name):
    """Case-insensitive subdirectory lookup (GOG installs vary)."""
    for e in os.listdir(base):
        if e.upper() == name.upper() and os.path.isdir(os.path.join(base, e)):
            return os.path.join(base, e)
    return None


def find_file(base, name):
    for e in os.listdir(base):
        if e.upper() == name.upper() and os.path.isfile(os.path.join(base, e)):
            return os.path.join(base, e)
    return None


def copy_into(src_dir, dst_dir):
    n = 0
    for f in sorted(os.listdir(src_dir)):
        p = os.path.join(src_dir, f)
        if os.path.isfile(p):
            shutil.copy2(p, os.path.join(dst_dir, f))
            n += 1
    return n


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Build a complete OpenUA game folder from the DOS "
                    "(GOG/Steam) FRUA release — no Mac files needed.")
    ap.add_argument("dos", help="the DOS FRUA install (holds CKIT.EXE, DISK1..3)")
    ap.add_argument("dest", help="the game folder to stage into")
    ap.add_argument("--design", default="HEIRS.DSN",
                    help="design to boot at startup (default: HEIRS.DSN)")
    args = ap.parse_args(argv)

    dos, dest = os.path.abspath(args.dos), os.path.abspath(args.dest)
    ckit = find_file(dos, "CKIT.EXE")
    if ckit is None:
        print("stage_dos: no CKIT.EXE in %s — point me at the DOS FRUA "
              "install folder" % dos, file=sys.stderr)
        return 1
    os.makedirs(dest, exist_ok=True)

    # 1) the shared data files + the design folders
    total = 0
    for d in ("DISK1", "DISK2", "DISK3"):
        sd = find_dir(dos, d)
        if sd:
            total += copy_into(sd, dest)
    print("stage_dos: copied %d data files" % total)
    for e in sorted(os.listdir(dos)):
        if not e.upper().endswith(".DSN"):
            continue
        sd = os.path.join(dos, e)
        if not os.path.isdir(sd):
            continue
        dd = os.path.join(dest, e)
        os.makedirs(dd, exist_ok=True)
        copy_into(sd, dd)
        save = find_dir(sd, "SAVE")
        if save:
            copy_into(save, dd)
        print("stage_dos: design %s" % e)

    # 2) art -> .ctl twins (colour only; the mono synthesis is a separate,
    #    slower pass the colour engine never reads)
    tlbs = sorted(glob.glob(os.path.join(dest, "*.[Tt][Ll][Bb]"))
                  + glob.glob(os.path.join(dest, "*.DSN", "*.[Tt][Ll][Bb]"))
                  + glob.glob(os.path.join(dest, "*.dsn", "*.[Tt][Ll][Bb]")))
    if tlbs:
        print("stage_dos: converting %d art libraries..." % len(tlbs))
        subprocess.run([sys.executable, os.path.join(HERE, "art_convert.py"),
                        "--no-mono"] + tlbs,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # 3) root data banks HLIB -> GLIB (skips anything already GLIB)
    glb2glib.main(sorted(glob.glob(os.path.join(dest, "*.[Gg][Ll][Bb]"))))

    # 4+5) music + sampled sfx from the DOS soundtrack sources
    xmi2slb.main([dos, os.path.join(dest, "MUSIC.SLB")])
    voc2glb.main([dos, os.path.join(dest, "SOUNDS.GLB")])

    # 6) the engine resource archive, from CKIT.EXE alone
    rsrc_from_dos.main([ckit, "-o", os.path.join(dest, "frua.rsc")])

    # 7) colour mouse pointers (optional nicety; mono fallback if it fails)
    always = find_file(dest, "ALWAYS.TLB")
    if always:
        subprocess.run([sys.executable, os.path.join(HERE, "hlib_extract.py"),
                        always, "--emit", os.path.join(dest, "frua.cur")],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # 8) the boot marker: 34 bytes of design name + the "valid" flag byte
    with open(os.path.join(dest, "start.dat"), "wb") as f:
        f.write(args.design.encode("ascii")[:34].ljust(34, b"\x00") + b"\x01")

    print("stage_dos: DONE — %s is a complete game folder (design %s)"
          % (dest, args.design))
    print("stage_dos: copy the engine binary from the release zip next to "
          "the data and run it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
