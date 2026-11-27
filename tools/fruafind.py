#!/usr/bin/env python3
"""fruafind — find the DOS Unlimited Adventures a player already owns.

The GOG and Steam re-releases ("Forgotten Realms: The Archives - Collection
Two") carry the DOS FRUA; its install folder is the one holding CKIT.EXE and
DISK1..3 (Steam: .../games/Unlimited Adventures -ENG/GAME/UA). This looks in
the usual places on Windows, macOS and Linux:

  - every Steam library (the default ones and those listed in
    steamapps/libraryfolders.vdf; on Windows also the SteamPath registry
    value), under steamapps/common
  - GOG's default folders (GOG Games, GOG Galaxy/Games)
  - any folder given with --search

An install counts only if its CKIT.EXE is the v1.2 build that
rsrc_from_dos.py can turn into frua.rsc (size and SHA-256); anything else —
the German edition, an older DOS build — is reported with the reason, not
picked.

    python3 fruafind.py                 # list what it finds
    python3 fruafind.py --search D:/Games

Imported by mkdatadisks.py, which uses the first good one when it is not
told where FRUA is.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# How deep under a search root to look for CKIT.EXE. Steam's layout puts it
# five levels below steamapps/common: <collection>/games/<edition>/GAME/UA.
_DEPTH = 6
_NAME_HINT = re.compile(r'forgotten|unlimited|frua|realms', re.I)


def _steam_roots():
    home = os.path.expanduser('~')
    roots = [
        os.path.join(home, '.local', 'share', 'Steam'),
        os.path.join(home, '.steam', 'steam'),
        os.path.join(home, '.var', 'app', 'com.valvesoftware.Steam',
                     '.local', 'share', 'Steam'),
        os.path.join(home, 'Library', 'Application Support', 'Steam'),
    ]
    for env in ('ProgramFiles(x86)', 'ProgramFiles'):
        if os.environ.get(env):
            roots.append(os.path.join(os.environ[env], 'Steam'))
    if sys.platform == 'win32':
        try:
            import winreg
            for hive, key, val in (
                    (winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam', 'SteamPath'),
                    (winreg.HKEY_LOCAL_MACHINE,
                     r'SOFTWARE\WOW6432Node\Valve\Steam', 'InstallPath')):
                try:
                    with winreg.OpenKey(hive, key) as k:
                        roots.append(winreg.QueryValueEx(k, val)[0])
                except OSError:
                    pass
        except ImportError:
            pass
    # every library a Steam install knows about
    libs = []
    for r in roots:
        vdf = os.path.join(r, 'steamapps', 'libraryfolders.vdf')
        try:
            text = open(vdf, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        libs += [p.replace('\\\\', '\\') for p in
                 re.findall(r'"path"\s+"([^"]+)"', text)]
    out = []
    for r in roots + libs:
        common = os.path.join(r, 'steamapps', 'common')
        if os.path.isdir(common) and common not in out:
            out.append(common)
    return out


def _gog_roots():
    home = os.path.expanduser('~')
    roots = [os.path.join(home, 'GOG Games'), '/Applications']
    for drive in ('C:', 'D:', 'E:'):
        roots += [drive + '\\GOG Games',
                  drive + '\\Program Files (x86)\\GOG Galaxy\\Games',
                  drive + '\\Program Files\\GOG Galaxy\\Games']
    return [r for r in roots if os.path.isdir(r)]


def _ckit_dirs(root, depth=_DEPTH, hint=True):
    """Folders under root holding CKIT.EXE, at most `depth` levels down.
    With hint, only descend into top-level entries whose name looks like
    FRUA or the collection, so a whole Steam library is not walked."""
    found = []

    def walk(d, left, top):
        try:
            ents = os.listdir(d)
        except OSError:
            return
        if any(e.upper() == 'CKIT.EXE' for e in ents):
            found.append(d)
            return
        if left == 0:
            return
        for e in sorted(ents):
            p = os.path.join(d, e)
            if not os.path.isdir(p) or os.path.islink(p):
                continue
            if top and hint and not _NAME_HINT.search(e):
                continue
            walk(p, left - 1, False)

    walk(root, depth, True)
    return found


def check(folder):
    """(ok, message) for one candidate install folder. Uses stage_dos's
    own choice, so a folder passes exactly when stage_dos can stage it
    (including a mod-managed one whose originals are in MM_BACKUP)."""
    import stage_dos
    ents = {e.upper(): e for e in os.listdir(folder)}
    if 'CKIT.EXE' not in ents:
        return False, 'no CKIT.EXE'
    if not any(d in ents for d in ('DISK1', 'DISK2', 'DISK3')):
        return False, 'no DISK1..3 folders'
    ckit, _backup, msg = stage_dos.pick_ckit(folder)
    return ckit is not None, msg


def find(extra=()):
    """[(folder, ok, message)], good ones first, each folder once."""
    seen, out = set(), []
    searches = [(r, True) for r in _steam_roots() + _gog_roots()]
    searches += [(r, False) for r in extra]
    for root, hint in searches:
        for d in _ckit_dirs(root, hint=hint):
            key = os.path.normcase(os.path.realpath(d))
            if key in seen:
                continue
            seen.add(key)
            ok, msg = check(d)
            out.append((d, ok, msg))
    out.sort(key=lambda t: not t[1])
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--search', action='append', default=[],
                    help='also look under this folder (repeatable)')
    args = ap.parse_args(argv)
    res = find(args.search)
    if not res:
        print('No Unlimited Adventures install found. Point the tools at the '
              'folder holding CKIT.EXE and DISK1..3, or use --search.')
        return 1
    for d, ok, msg in res:
        print('%s  %s\n      %s' % ('OK ' if ok else 'NO ', d, msg))
    return 0 if any(ok for _, ok, _ in res) else 1


if __name__ == '__main__':
    sys.exit(main())
