"""OpenUA Module Manager.

    python3 tools/modman                         the window
    python3 tools/modman inspect ARCHIVE|FOLDER  what installing it would do
    python3 tools/modman install ARCHIVE|FOLDER --to OPENUA_DIR [--name NAME] [--replace]
    python3 tools/modman list OPENUA_DIR         designs installed there
    python3 tools/modman remove NAME.DSN --from OPENUA_DIR
    python3 tools/modman catalog FILE|URL        the modules a catalog offers
    python3 tools/modman fetch FILE|URL ID --to OPENUA_DIR [--replace]

OPENUA_DIR is the folder holding frua.prg (Atari) or frua (Amiga): on an SD or
CF card, a hard-disk image mounted on the desktop, or an emulator's folder.
"""
import os
import sys

if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    __package__ = "modman"

from modman import catalog, core      # noqa: E402


def arg(args, flag, default=None):
    if flag in args:
        i = args.index(flag)
        if i + 1 < len(args):
            return args[i + 1]
    return default


def show_plan(p):
    print("%s -> %s (%d files)" % (p.title or "(untitled)", p.folder, len(p.files)))
    for n in p.notes:
        print("  note:", n)
    for path, why in p.skipped:
        print("  left out: %s (%s)" % (path, why))


def main(argv):
    if not argv:
        from modman import gui
        gui.main()
        return 0
    cmd, args = argv[0], argv[1:]
    if cmd in ("-h", "--help", "help"):
        print(__doc__)
        return 0
    if cmd == "inspect" and args:
        show_plan(core.plan(core.Source.from_path(args[0]), arg(args, "--name")))
        return 0
    if cmd == "install" and args and arg(args, "--to"):
        p = core.plan(core.Source.from_path(args[0]), arg(args, "--name"))
        show_plan(p)
        print("installed:", core.Target(arg(args, "--to")).install(p, replace="--replace" in args))
        return 0
    if cmd == "list" and args:
        t = core.Target(args[0])
        why = t.check()
        if why:
            raise core.ModError(why)
        for folder, title in t.designs():
            print("%-13s %s" % (folder, title))
        return 0
    if cmd == "remove" and args and arg(args, "--from"):
        core.Target(arg(args, "--from")).remove(args[0])
        print("removed", args[0])
        return 0
    if cmd == "catalog" and args:
        for e in catalog.load(args[0]):
            print("%-12s %s%s" % (e.ident, e.title, "  (" + e.author + ")" if e.author else ""))
        return 0
    if cmd == "fetch" and len(args) >= 2 and arg(args, "--to"):
        want = [e for e in catalog.load(args[0]) if e.ident == args[1]]
        if not want:
            raise core.ModError("no [%s] in that catalog" % args[1])
        from modman.gui import CACHE
        path = catalog.fetch(want[0], CACHE)
        p = core.plan(core.Source.from_path(path))
        show_plan(p)
        print("installed:", core.Target(arg(args, "--to")).install(p, replace="--replace" in args))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except core.ModError as e:
        sys.exit("modman: %s" % e)
