#!/usr/bin/env python3
"""stage_dos — build a complete OpenUA game folder from the DOS release alone.

Point it at your GOG/Steam FRUA install (the folder holding CKIT.EXE and
DISK1/DISK2/DISK3) and at the game folder you want to play from (the one
holding frua.prg / frua), and it performs the whole ADR-0017 pipeline that
`make gamedata-dos` runs for developers:

  1. copy the DISK1..3 data files flat + every *.DSN design folder
  2. convert all DOS art to the engine's .ctl twins   (art_convert)
  3. convert the root data banks HLIB -> GLIB          (glb2glib)
  4. synthesize MUSIC.SLB from the DOS XMI soundtrack, root AND
     per-design (xmi2slb)
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


def pick_ckit(dos):
    """(ckit path or None, backup dir or None, message).

    rsrc_from_dos reads CKIT.EXE's strings at fixed offsets, so it takes only
    the v1.2 build it was mapped against. A mod manager that patched the game
    (seen on a Steam install, 2026-11) leaves the originals in MM_BACKUP: the
    v1.2 CKIT.EXE there, and the DISK1..3 files it replaced. Then the
    originals are used, and the caller lays the backed-up DISK files over the
    modded ones, since OpenUA is tested against SSI's data."""
    import json
    with open(rsrc_from_dos.DEFAULT_MAP) as fh:
        doc = json.load(fh)
    top = find_file(dos, "CKIT.EXE")
    if top is None:
        return None, None, "no CKIT.EXE"
    ok, msg = rsrc_from_dos.verify_exe(open(top, "rb").read(), doc)
    if ok:
        return top, None, msg
    bak = find_dir(dos, "MM_BACKUP")
    bck = find_file(bak, "CKIT.EXE") if bak else None
    if bck:
        ok2, _ = rsrc_from_dos.verify_exe(open(bck, "rb").read(), doc)
        if ok2:
            return bck, bak, ("CKIT.EXE was changed by a mod manager; using "
                              "the v1.2 originals it kept in MM_BACKUP")
    return None, None, msg


def design_dirs(dos):
    """Folders that may hold *.DSN design folders, searched in order.

    GOG keeps the designs beside CKIT.EXE. Steam keeps the player's working
    designs (HEIRS, TUTORIAL and any installed modules) in
    <edition>/DESIGNS/UA and untouched copies of HEIRS and TUTORIAL in
    <edition>/Default files/Saves/UA, two levels above GAME/UA. A design
    found earlier wins, so the player's copy beats the default."""
    out = [dos]
    edition = os.path.dirname(os.path.dirname(dos))
    des = find_dir(edition, "DESIGNS") if os.path.isdir(edition) else None
    if des and find_dir(des, "UA"):
        out.append(find_dir(des, "UA"))
    dflt = find_dir(edition, "Default files") if os.path.isdir(edition) else None
    if dflt:
        saves = find_dir(dflt, "Saves")
        if saves and find_dir(saves, "UA"):
            out.append(find_dir(saves, "UA"))
    return out


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
    if find_file(dos, "CKIT.EXE") is None:
        print("stage_dos: no CKIT.EXE in %s — point me at the DOS FRUA "
              "install folder" % dos, file=sys.stderr)
        return 1
    ckit, backup, why = pick_ckit(dos)
    if ckit is None:
        print("stage_dos: %s. OpenUA needs the English v1.2 DOS release "
              "(GOG/Steam); nothing was staged." % why, file=sys.stderr)
        return 1
    if backup:
        print("stage_dos: " + why)
    os.makedirs(dest, exist_ok=True)

    # 1) the shared data files + the design folders
    total = 0
    for d in ("DISK1", "DISK2", "DISK3"):
        sd = find_dir(dos, d)
        if sd:
            total += copy_into(sd, dest)
    if backup:
        # the mod manager's backups of the DISK files it replaced
        for d in ("DISK1", "DISK2", "DISK3"):
            sd = find_dir(backup, d)
            if sd:
                copy_into(sd, dest)
    print("stage_dos: copied %d data files" % total)
    taken = set()
    for where in design_dirs(dos):
        for e in sorted(os.listdir(where)):
            if not e.upper().endswith(".DSN") or e.upper() in taken:
                continue
            sd = os.path.join(where, e)
            if not os.path.isdir(sd):
                continue
            taken.add(e.upper())
            dd = os.path.join(dest, e)
            os.makedirs(dd, exist_ok=True)
            copy_into(sd, dd)
            save = find_dir(sd, "SAVE")
            if save:
                copy_into(save, dd)
            print("stage_dos: design %s" % e)

    # 2) art -> .ctl twins (colour only; the mono synthesis is a separate,
    #    slower pass the colour engine never reads)
    # a set: on a case-blind file system (Windows) *.DSN and *.dsn match
    # the same folders
    tlbs = sorted(set(glob.glob(os.path.join(dest, "*.[Tt][Ll][Bb]"))
                      + glob.glob(os.path.join(dest, "*.[Dd][Ss][Nn]",
                                               "*.[Tt][Ll][Bb]"))))
    if tlbs:
        print("stage_dos: converting %d art libraries..." % len(tlbs))
        # in batches: a Steam install with its fan modules has thousands,
        # and Windows caps a command line at 32767 characters
        for k in range(0, len(tlbs), 100):
            subprocess.run([sys.executable,
                            os.path.join(HERE, "art_convert.py"),
                            "--no-mono"] + tlbs[k:k + 100],
                           stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)

    # 3) root data banks HLIB -> GLIB (skips anything already GLIB)
    glb2glib.main(sorted(glob.glob(os.path.join(dest, "*.[Gg][Ll][Bb]"))))

    # 4+5) music + sampled sfx from the DOS soundtrack sources.
    # --skip-designs so a module's own soundtrack cannot become the base
    # game's bank; each design then gets its OWN bank below.
    xmi2slb.main(["--skip-designs", dos, os.path.join(dest, "MUSIC.SLB")])
    voc2glb.main([dos, os.path.join(dest, "SOUNDS.GLB")])

    # 4b) PER-DESIGN music. A DOS fan module keeps its .XMI inside its .DSN
    # (curse does exactly this), and the engine now resolves the bank
    # design-first, root-fallback like every other overridable file
    # (ADR-0011). Without this a module played the BASE GAME's music, which
    # looks like working audio and is the wrong soundtrack.
    for dsn in sorted(glob.glob(os.path.join(dest, "*.[Dd][Ss][Nn]"))):
        if not os.path.isdir(dsn):
            continue
        paths, _pref = xmi2slb.find_xmi_set(dsn)
        if paths:
            # one module's music must not stop the whole staging
            try:
                xmi2slb.main([dsn, os.path.join(dsn, "MUSIC.SLB")])
            except Exception as exc:
                print("stage_dos: %s: its music did not convert (%s); it "
                      "plays the base game's" % (os.path.basename(dsn), exc))

    # 6) the engine resource archive, from CKIT.EXE alone
    # (its failure used to go unnoticed: a folder without frua.rsc and
    # "DONE", which the engine starts as a black screen)
    if rsrc_from_dos.main([ckit, "-o", os.path.join(dest, "frua.rsc")]) != 0:
        print("stage_dos: frua.rsc could not be built", file=sys.stderr)
        return 1

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
