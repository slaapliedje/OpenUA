#!/usr/bin/env python3
"""mkdatadisks — make OpenUA game-data floppy images from YOUR copy of FRUA.

    python3 mkdatadisks.py                      # find FRUA, every machine
    python3 mkdatadisks.py "D:/GOG Games/..."   # a DOS install or a game folder
    python3 mkdatadisks.py --for amiga --out disks

The source may be:
  - nothing: the GOG/Steam install is found (fruafind.py) and staged;
  - a DOS FRUA install (the folder holding CKIT.EXE and DISK1..3): staged
    with stage_dos.py into a temporary folder first;
  - a staged game folder (frua.rsc beside the .TLB art), used as it is.

Families (--for, comma-separated or "all"):
  atari     Atari 1.44 MB disks (Falcon, TT, ST with an HD drive)
  atari720  Atari 720 KB disks (any ST)
  gotek     ONE Atari image for a FlashFloppy Gotek (255 cyls, 75 rpm)
  amiga     Amiga 880 KB disks (AGA and ECS)

*** THE IMAGES CONTAIN SSI'S COPYRIGHTED GAME DATA. They are for the owner
of the game only: never share or upload them. ***

Disk 1 of each set carries the installer (INSTDISK.TTP/.PRG on the Atari,
instdisk on the Amiga) when it sits beside this script or in a source build,
plus DISK.LST on every disk (instdisk's manifest) and NDISKS on disk 1 (the
count, for the Amiga engine disk's Install script).

Options as in the old mkdatadisks.sh, which now runs this:
  --art tlb|ctl|both   ship SSI's .TLB art (default; the engine converts on
                       first use), our converted .ctl, or both
  --perband auto|off|N reduce the art to N colours per 8-row band for the
                       bitplane machines (auto: 12 Atari, 24 Amiga)
  --design NAME.DSN    designs to include (repeatable; default HEIRS.DSN)
  --module NAME        a fan-module set: volumes NAME-Data-N, and on the
                       Amiga an Install icon that copies the design into an
                       existing OpenUA drawer

Needs only Python 3: the images are written by diskimg.py.
"""
import argparse
import fnmatch
import glob
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import diskimg  # noqa: E402

FAMILIES = ('atari', 'atari720', 'gotek', 'amiga')

# Installer files for data disk 1: (name on disk, release-zip name, repo build)
INSTALLERS = {
    'atari': [('INSTDISK.TTP', 'INSTDISK.TTP', 'instdisk.ttp'),
              ('INSTDISK.PRG', 'INSTDISK.PRG', 'instdisk.prg')],
    'amiga': [('instdisk', 'instdisk', 'instdisk_amiga')],
}
INSTALLERS['atari720'] = INSTALLERS['gotek'] = INSTALLERS['atari']

PERBAND_AUTO = {'amiga': 24, 'atari': 12, 'atari720': 12, 'gotek': 12}

# The engine is not data; neither are logs, the DOS executables or the
# player's characters.
ROOT_SKIP = ('*.EXE', 'DBG.LOG', 'frua.prg', 'frua', '*.CHR', '*.CCH')


def say(msg):
    print('datadisks: ' + msg)


# --------------------------------------------------------------------------
# finding and staging the data
# --------------------------------------------------------------------------

def _has(folder, name):
    try:
        return any(e.upper() == name.upper() for e in os.listdir(folder))
    except OSError:
        return False


def resolve_source(src, work):
    """A staged game folder for src (staging it if it is a DOS install)."""
    if src is None:
        import fruafind
        found = [d for d, ok, _ in fruafind.find() if ok]
        if not found:
            sys.exit('mkdatadisks: no Unlimited Adventures install found. '
                     'Give the folder holding CKIT.EXE and DISK1..3 '
                     '(run fruafind.py to see what was looked at).')
        src = found[0]
        say('found FRUA at ' + src)
    src = os.path.abspath(src)
    if _has(src, 'frua.rsc'):
        return src
    if _has(src, 'CKIT.EXE'):
        import stage_dos
        dest = os.path.join(work, 'staged')
        say('staging the DOS install (stage_dos.py) ...')
        if stage_dos.main([src, dest]) != 0:
            sys.exit('mkdatadisks: staging failed (see above)')
        return dest
    sys.exit('mkdatadisks: %s is neither a game folder (frua.rsc) nor a DOS '
             'FRUA install (CKIT.EXE)' % src)


def file_list(src, designs, art):
    """Paths (relative, '/'-separated) that go onto the disks."""
    out = []
    for e in sorted(os.listdir(src)):
        p = os.path.join(src, e)
        if not os.path.isfile(p) or os.path.islink(p):
            continue
        if any(fnmatch.fnmatch(e.upper(), pat.upper()) for pat in ROOT_SKIP):
            continue
        out.append(e)
    for d in designs:
        dp = os.path.join(src, d)
        if not os.path.isdir(dp):
            sys.exit('mkdatadisks: no such design: %s' % dp)
        # Saves are the player's, except the one SSI shipped: Save A.
        for root, _dirs, files in os.walk(dp):
            for f in sorted(files):
                low = f.lower()
                if low.startswith('savgam') or low.startswith('vault') \
                        or low.endswith('.cch'):
                    continue
                out.append(os.path.relpath(os.path.join(root, f), src)
                           .replace(os.sep, '/'))
        for keep in ('SAVE/SAVGAMA.CSV', 'SAVE/VAULTA.DAT'):
            if os.path.isfile(os.path.join(dp, *keep.split('/'))):
                out.append(d + '/' + keep)
    if art == 'tlb':
        out = [f for f in out if not f.lower().endswith('.ctl')]
    elif art == 'ctl':
        out = [f for f in out if not f.lower().endswith('.tlb')]
    return sorted(out)


def perband_copy(src, files, budget, work):
    """Copy the listed files and reduce their art to `budget` colours per
    band. Returns the new source folder; the original is never touched."""
    dest = os.path.join(work, 'perband-%d' % budget)
    if os.path.isdir(dest):
        return dest
    for f in files:
        d = os.path.join(dest, *f.split('/'))
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copy2(os.path.join(src, *f.split('/')), d)
    say('per-band art conversion, budget %d colours ...' % budget)
    subprocess.run([sys.executable, os.path.join(HERE, 'perband.py'), dest,
                    '--out', dest, '--budget', str(budget)],
                   stdout=subprocess.DEVNULL)
    return dest


# --------------------------------------------------------------------------
# packing
# --------------------------------------------------------------------------

def _geometry(family):
    """(unit bytes, capacity units, root entries or 0 for unlimited)."""
    if family == 'amiga':
        return 512, diskimg.AdfImage.capacity() - 16, 0
    unit, n, root = diskimg.FatImage(family).capacity()
    return unit, n - 7, root          # a little slack, as the bash had


def pack(src, files, family, extra_bytes):
    """Bin-pack files into disks: [[path, ...], ...]. Counts allocation units
    (FFS: + file header + an extension block per 72), and on FAT the root
    directory's entries as well (720K's 112 bind before its capacity)."""
    unit, cap, rootmax = _geometry(family)

    def blocks(nbytes):
        data = (nbytes + unit - 1) // unit
        if family == 'amiga':
            return data + 1 + data // 72
        return max(data, 1)

    def budget(i):
        b = cap - blocks(4096)                    # DISK.LST + NDISKS + slack
        return b - (blocks(extra_bytes) if i == 0 else 0)

    def entry_budget(i):
        if rootmax == 0:
            return 10 ** 9
        return rootmax - 3 - (len(INSTALLERS.get(family, [])) if i == 0 else 0)

    def size(f):
        return os.path.getsize(os.path.join(src, *f.split('/')))

    disks, used, ents = [], [], []
    for f in sorted(files, key=lambda f: -size(f)):
        n = blocks(size(f))
        top = f.split('/')[0] if '/' in f else None
        n += 1 if top else 0                       # the folder's own block
        for i in range(len(disks)):
            e = 0 if top and top in ents[i][1] else 1
            if used[i] + n <= budget(i) and ents[i][0] + e <= entry_budget(i):
                disks[i].append(f)
                used[i] += n
                ents[i][0] += e
                if top:
                    ents[i][1].add(top)
                break
        else:
            disks.append([f])
            used.append(n)
            ents.append([1, {top} if top else set()])
    return [sorted(d) for d in disks]


# --------------------------------------------------------------------------
# installers and the module Install icon
# --------------------------------------------------------------------------

def find_installers(family, extra_dir):
    found = []
    for disk_name, zip_name, repo_name in INSTALLERS[family]:
        for cand in ([os.path.join(extra_dir, zip_name),
                      os.path.join(extra_dir, repo_name)] if extra_dir else []) + [
                os.path.join(HERE, zip_name),
                os.path.join(HERE, os.pardir, repo_name)]:
            if os.path.isfile(cand):
                found.append((disk_name, cand))
                break
    return found


def module_install_files(module, volbase, ndisks):
    """{name: bytes} for an Amiga fan-module set's disk 1."""
    launcher = """; %(m)s - fan-module install launcher (run by IconX). No .KEY line: a
; bare .KEY is rejected by the 3.1 script runner ("Illegal Key directive").
; CD off the floppy first, or the shell demands the volume back at the end.
ECHO ""
ECHO "  %(m)s - a fan-made module for OpenUA"
ECHO ""
ECHO "  This installs the design INTO YOUR EXISTING OpenUA DRAWER."
ECHO "  It does NOT install OpenUA itself. You will be asked where"
ECHO "  that drawer is (the one holding frua); the default is DH0:OpenUA."
ECHO "  If you pick a folder that does not exist, it is created there,"
ECHO "  and the engine will NOT find the module until you move it."
ECHO ""
ECHO "  %(n)d disks, labelled %(v)s-1 .. Feed them in order."
ECHO "  Then start OpenUA and choose SELECT A DESIGN."
ECHO ""
CD RAM:
IF EXISTS SYS:System/Installer
  SYS:System/Installer SCRIPT %(v)s-1:Install.script APPNAME "%(m)s" MINUSER NOVICE DEFUSER AVERAGE
ELSE
  IF EXISTS SYS:Utilities/Installer
    SYS:Utilities/Installer SCRIPT %(v)s-1:Install.script APPNAME "%(m)s" MINUSER NOVICE DEFUSER AVERAGE
  ELSE
    ECHO "No AmigaOS Installer on this system (Workbench 3.1 never shipped one)."
    ECHO "Using the console installer instead. At its prompt, type the path of"
    ECHO "your OpenUA drawer (the one holding frua), e.g. DH0:OpenUA"
    ECHO ""
    %(v)s-1:instdisk
    ECHO ""
    ECHO "Press RETURN to close this window."
    ASK "" >NIL:
  ENDIF
ENDIF
""" % dict(m=module, v=volbase, n=ndisks)
    script = """; %(m)s - OpenUA fan-module install script for the AmigaOS Installer (2.0+)
; Copies the design INTO an existing OpenUA drawer; installs nothing else.
(set uadir
  (askdir (prompt "Where is your OpenUA drawer? (the one holding frua)")
          (help "%(m)s is copied INTO the drawer you pick. Pick the drawer that already holds the OpenUA engine (frua). If you pick a folder that does not exist it is created, but the engine will not find the module there.")
          (default "DH0:OpenUA")))
(if (not (exists (tackon uadir "frua")))
  (message (cat "There is no frua in " uadir ". The module is copied there anyway, but OpenUA will only see it once it is beside frua.")))
(message "Installing %(m)s into " uadir)
(set i 1)
(while (<= i %(n)d)
  (
    (askdisk (prompt (cat "Insert %(m)s disk " i " of %(n)d"))
             (help "The disks are labelled %(v)s-1 .. %(v)s-%(n)d.")
             (dest (cat "%(v)s-" i)))
    (copyfiles (source (cat "%(v)s-" i ":")) (dest uadir) (pattern "~(DISK.LST|NDISKS|instdisk|Install|Install.info|Install.script)"))
    (set i (+ i 1))
  )
)
(askdisk (prompt "Put %(v)s-1 back in the drive.") (help "The install window needs its script disk back to close cleanly.") (dest "%(v)s-1"))
(complete 100)
(exit (cat "%(m)s is installed in " uadir ". Start OpenUA and pick it with SELECT A DESIGN."))
""" % dict(m=module, v=volbase, n=ndisks)
    import make_amiga_icon
    with tempfile.TemporaryDirectory() as td:
        info = os.path.join(td, 'Install.info')
        make_amiga_icon.main(['--type', 'project', '--default-tool', 'IconX',
                              '--tooltype',
                              'WINDOW=CON:0/20/640/180/%s install/CLOSE' % module,
                              '-o', info])
        icon = open(info, 'rb').read()
    return {'Install': launcher.encode('latin-1'),
            'Install.script': script.encode('latin-1'),
            'Install.info': icon}


# --------------------------------------------------------------------------
# one family
# --------------------------------------------------------------------------

def make_family(family, src, files, out, art, perband, module, inst_dir, work):
    if perband == 'auto':
        budget = PERBAND_AUTO[family]
    elif perband in ('off', '0'):
        budget = 0
    else:
        budget = int(perband)
    fsrc = perband_copy(src, files, budget, work) if budget else src

    volbase = (module + '-Data') if module else 'OpenUA-Data'
    title = (module + ' module') if module else 'OpenUA game data'
    installers = find_installers(family, inst_dir)
    extra = {}
    for disk_name, path in installers:
        extra[disk_name] = open(path, 'rb').read()
    if module and family == 'amiga':
        # sized now, filled once the disk count is known
        extra.update(module_install_files(module, volbase, 9))
    names = [n for n, _, _ in INSTALLERS[family]]
    missing = [n for n in names if n not in extra]
    if missing:
        say('%s: no %s beside this script or in a build; data disk 1 goes '
            'without it' % (family, '/'.join(missing)))

    disks = pack(fsrc, files, family,
                 sum(len(b) for b in extra.values()) + 512 * len(extra))
    n = len(disks)
    if module and family == 'amiga':
        extra.update(module_install_files(module, volbase, n))
    ext = 'adf' if family == 'amiga' else 'st'
    os.makedirs(out, exist_ok=True)
    # a set that shrank must not leave its old last disks behind
    for old in glob.glob(os.path.join(out, 'openua-data-%s-disk*.%s'
                                      % (family, ext))):
        os.remove(old)
    paths = []
    for i, d in enumerate(disks, 1):
        img = diskimg.AdfImage() if family == 'amiga' else diskimg.FatImage(family)
        for f in d:
            img.add_file(f, open(os.path.join(fsrc, *f.split('/')), 'rb').read())
        lst = '%d %d %s\n' % (i, n, title) + ''.join(f + '\n' for f in d)
        img.add_file('DISK.LST', lst.encode('latin-1'))
        if i == 1:
            img.add_file('NDISKS', str(n).encode())
            for name, data in extra.items():
                img.add_file(name, data)
        label = '%s-%d' % (volbase, i) if family == 'amiga' else 'OPENUADAT'
        path = os.path.join(out, 'openua-data-%s-disk%d.%s' % (family, i, ext))
        try:
            img.save(path, label)
        except diskimg.DiskFull as exc:
            sys.exit('mkdatadisks: packing overfilled %s disk %d (%s) — a '
                     'bug in the packer; please report it' % (family, i, exc))
        say('%s  %d files' % (os.path.basename(path), len(d)))
        paths.append(path)
    write_readme(out, family, n, names)
    return paths


def write_readme(out, family, n, inst_names):
    if len(inst_names) > 1:
        desc = ' (%s for the desktop, %s for the console)' % (inst_names[1],
                                                             inst_names[0])
    else:
        desc = ': ' + inst_names[0]
    text = """OpenUA game-data disks (%(f)s) - %(n)d disks

*** THESE CONTAIN COPYRIGHTED GAME DATA. Do not redistribute them. ***

Disk 1 carries the installer%(d)s.
Run it, give it a destination, and feed the disks in when asked:

    Atari:  C:\\OPENUA
    Amiga:  DH0:OpenUA

When the data is in, it asks for the OpenUA ENGINE disk and installs the
engine into the same directory (joining the two halves on the Amiga ECS set
itself). Run the game from that directory - it looks for frua.rsc relative
to where it runs.

Amiga Workbench users can instead double-click the Install icon on the
engine disk: it uses the OS Installer and asks for these disks by name.
""" % dict(f=family, n=n, d=desc)
    with open(os.path.join(out, 'README-DATA-%s.txt' % family), 'w') as fh:
        fh.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description='Make OpenUA game-data floppy images from your own FRUA.',
        epilog='The images contain copyrighted game data: never share them.')
    ap.add_argument('source', nargs='?',
                    help='DOS FRUA install or staged game folder '
                         '(default: find it)')
    ap.add_argument('--out', default='openua-data-disks',
                    help='output folder (default: openua-data-disks)')
    ap.add_argument('--for', dest='families', default='all',
                    help='all, or any of %s, comma-separated' % ','.join(FAMILIES))
    ap.add_argument('--design', action='append', default=[],
                    help='design to include (repeatable; default HEIRS.DSN)')
    ap.add_argument('--art', choices=('tlb', 'ctl', 'both'), default='tlb')
    ap.add_argument('--perband', default='auto',
                    help="auto (default), off, or a colour count")
    ap.add_argument('--module', default='',
                    help='make a fan-module set named MODULE')
    ap.add_argument('--installers', default='',
                    help='folder holding INSTDISK.TTP/.PRG and/or instdisk')
    args = ap.parse_args(argv)

    fams = FAMILIES if args.families == 'all' else tuple(
        f.strip() for f in args.families.split(',') if f.strip())
    for f in fams:
        if f not in FAMILIES:
            ap.error('unknown family %r (use %s or all)' % (f, ', '.join(FAMILIES)))
    if args.perband not in ('auto', 'off') and not args.perband.isdigit():
        ap.error("--perband takes auto, off or a colour count")
    designs = args.design or ['HEIRS.DSN']

    print('*** The disk images contain SSI\'s copyrighted game data: they are '
          'for your own use only. ***')
    work = tempfile.mkdtemp(prefix='openua-disks-')
    try:
        src = resolve_source(args.source, work)
        files = file_list(src, designs, args.art)
        total = sum(os.path.getsize(os.path.join(src, *f.split('/'))) for f in files)
        say('%d files, %d bytes (art %s)' % (len(files), total, args.art))
        made = []
        for fam in fams:
            made += make_family(fam, src, files, args.out, args.art,
                                args.perband, args.module, args.installers, work)
        say('done: %d images in %s' % (len(made), os.path.abspath(args.out)))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
