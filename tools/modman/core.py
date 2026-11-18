"""modman.core — what a module archive holds, and putting it into an OpenUA folder.

A FRUA module ("design") is a folder of data files the engine finds by its
`.DSN` suffix next to the engine (docs/fan-module-hacks.md). Fan archives come
three ways, all seen in the wild:

    loose       GAME001.DAT, GEO005.DAT, ... at the archive root
    folder      Curse.dsn/GAME001.DAT ...
    nested      ua/Game40.dsn/GAME001.DAT ...

and they carry things the engine never reads: DOS tools (RESIZEPC.EXE), HTML
manuals in subfolders, readmes. An "archive" that is really a saved web error
page is common too (half of one test download set was).

What goes on the card is held to the strictest of our file systems, the Atari's
GEMDOS: every name 8.3 and upper case. That is safe everywhere else (AmigaDOS
and the Unix port both match names without regard to case). Mac modules name
their art the long way ("8x8db3001.ctl"); those become the 8.3 spelling the
engine also probes (ADR-0013: the first four letters, the digit, the id).

Base-game art a module replaces (a "hack") stays INSIDE the design folder, as
the engine expects (ADR-0011, design first then root): installing a module
never overwrites a file outside its own folder.
"""
import io
import os
import re
import shutil
import zipfile

DESIGN_FILE = "GAME001.DAT"         # every design has one; it holds the title
TITLE_OFF, TITLE_LEN = 0, 31        # tools/dsn.py _OFF_TITLE, 31 bytes
ENGINES = ("FRUA.PRG", "FRUA", "FRUA.TTP")
SKIP_EXT = (".EXE", ".COM", ".BAT", ".PIF", ".LNK", ".SCR", ".DLL", ".TMP")
NAME83 = re.compile(r"^[A-Z0-9_$~!#%&'(){}@^`-]{1,8}(\.[A-Z0-9_$~!#%&'(){}@^`-]{1,3})?$")
MAC_ART = re.compile(r"^([A-Z0-9]{4})[A-Z]*([0-9])([0-9]{3})$")


class ModError(Exception):
    pass


# ---------------------------------------------------------------- archives

def open_archive(data, name="archive"):
    """A zipfile.ZipFile over `data`, or ModError saying what it really is."""
    head = data[:64].lstrip().lower()
    if head.startswith((b"<!doctype", b"<html", b"<?xml", b"<head")):
        raise ModError("%s is a web page, not an archive (the download "
                       "failed or the link has moved)" % name)
    if data[:4] == b"SIT!" or data[:8] == b"StuffIt ":
        raise ModError("%s is a StuffIt archive; unpack it first (e.g. with "
                       "'unar') and add the folder" % name)
    if len(data) > 7 and data[2:5] in (b"-lh", b"-lz"):
        raise ModError("%s is an LHA archive; unpack it first and add the "
                       "folder" % name)
    try:
        return zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile:
        raise ModError("%s is not a zip archive" % name)


# ---------------------------------------------------------------- explode
#
# Zip method 6, "imploding", from PKZIP 1.x — the packer of the early 1990s,
# and so of early FRUA modules (two of the first thirteen tried). Python's
# zipfile cannot read it; this does, after PKWARE's APPNOTE (section "Imploding
# - Method 6") and checked against each member's CRC by the caller.

class _Bits:
    def __init__(self, data):
        self.data, self.pos, self.acc, self.n = data, 0, 0, 0

    def get(self, count):
        while self.n < count:
            b = self.data[self.pos] if self.pos < len(self.data) else 0
            self.pos += 1
            self.acc |= b << self.n
            self.n += 8
        v = self.acc & ((1 << count) - 1)
        self.acc >>= count
        self.n -= count
        return v


def _sf_tree(data, pos, count):
    """A Shannon-Fano tree: {(bit length, code read LSB first): value}, and the
    position after it."""
    nbytes = data[pos] + 1
    lens = []
    for b in data[pos + 1:pos + 1 + nbytes]:
        lens += [(b & 15) + 1] * ((b >> 4) + 1)
    if len(lens) != count:
        raise ModError("damaged imploded data (a code table of %d, not %d)"
                       % (len(lens), count))
    order = sorted(range(count), key=lambda v: (lens[v], v))
    code = inc = last = 0
    table = {}
    for i in range(count - 1, -1, -1):
        v = order[i]
        code = (code + inc) & 0xFFFF
        if lens[v] != last:
            last = lens[v]
            inc = 1 << (16 - last)
        rev = int("{:016b}".format(code)[::-1], 2)       # MSB first -> LSB first
        table[(lens[v], rev & ((1 << lens[v]) - 1))] = v
    return table, pos + 1 + nbytes


def _sf_read(bits, table):
    v = n = 0
    while n < 16:
        v |= bits.get(1) << n
        n += 1
        hit = table.get((n, v))
        if hit is not None:
            return hit
    raise ModError("damaged imploded data (no such code)")


def explode(data, size, flags):
    """Unpack `size` bytes of method-6 data. `flags` is the member's general
    purpose bit flag: bit 1 an 8K window (else 4K), bit 2 a literal tree."""
    big, literals = bool(flags & 2), bool(flags & 4)
    pos = 0
    lit = None
    if literals:
        lit, pos = _sf_tree(data, pos, 256)
    length_t, pos = _sf_tree(data, pos, 64)
    dist_t, pos = _sf_tree(data, pos, 64)
    bits = _Bits(data[pos:])
    out = bytearray()
    low = 7 if big else 6
    minimum = 3 if literals else 2
    while len(out) < size:
        if bits.get(1):
            out.append(_sf_read(bits, lit) if literals else bits.get(8))
            continue
        dist = bits.get(low) | (_sf_read(bits, dist_t) << low)
        n = _sf_read(bits, length_t)
        if n == 63:
            n += bits.get(8)
        n += minimum
        start = len(out) - dist - 1
        for k in range(n):
            j = start + k
            out.append(out[j] if j >= 0 else 0)
    return bytes(out[:size])


def _read_member(z, raw, info):
    if info.compress_type != 6:
        return z.read(info)
    import struct
    import zlib
    h = info.header_offset
    if raw[h:h + 4] != b"PK\x03\x04":
        raise ModError("damaged zip (%s)" % info.filename)
    nlen, xlen = struct.unpack_from("<HH", raw, h + 26)
    start = h + 30 + nlen + xlen
    out = explode(raw[start:start + info.compress_size], info.file_size, info.flag_bits)
    if zlib.crc32(out) & 0xFFFFFFFF != info.CRC:
        raise ModError("%s did not unpack correctly (checksum)" % info.filename)
    return out


class Source:
    """A module's files, from a zip or from a folder already unpacked:
    a list of (relative path with '/', bytes loader)."""

    def __init__(self, name, entries):
        self.name = name
        self.entries = entries      # [(path, callable -> bytes)]

    @classmethod
    def from_zip(cls, data, name):
        z = open_archive(data, name)
        entries = []
        for info in z.infolist():
            if info.is_dir():
                continue
            if info.compress_type not in (0, 6, 8, 12, 14):
                raise ModError("%s uses a zip packing method (%d) this cannot "
                               "read; unpack it with another tool and add the "
                               "folder" % (name, info.compress_type))
            path = info.filename.replace("\\", "/")
            entries.append((path, (lambda i=info: _read_member(z, data, i))))
        return cls(name, entries)

    @classmethod
    def from_path(cls, path):
        if os.path.isdir(path):
            entries = []
            for root, _dirs, files in os.walk(path):
                for f in files:
                    full = os.path.join(root, f)
                    rel = os.path.relpath(full, path).replace(os.sep, "/")
                    entries.append((rel, (lambda p=full: open(p, "rb").read())))
            return cls(os.path.basename(os.path.normpath(path)), entries)
        with open(path, "rb") as fh:
            data = fh.read()
        return cls.from_zip(data, os.path.basename(path))


# ---------------------------------------------------------------- planning

def dos83(name):
    """A file name as it goes on the card: upper case 8.3, or None when it
    cannot be made one without guessing."""
    up = name.upper()
    base, dot, ext = up.rpartition(".")
    if not dot:
        base, ext = up, ""
    if NAME83.match(up):
        return up
    m = MAC_ART.match(base)
    if m and ext in ("CTL", "TLB"):
        return "%s%s%s.%s" % (m.group(1), m.group(2), m.group(3), ext)
    return None


def design_name(text):
    """A design folder name (without .DSN) from a folder or archive name."""
    stem = re.sub(r"\.(DSN|ZIP)$", "", text.upper())
    stem = re.sub(r"[^A-Z0-9_]", "", stem)[:8]
    return stem or "MODULE"


def read_title(game001):
    raw = game001[TITLE_OFF:TITLE_OFF + TITLE_LEN].split(b"\0")[0]
    return raw.decode("mac-roman", "replace").strip()


class Plan:
    """What installing a source would do. `files` maps the destination path
    inside the design folder ("GAME001.DAT", "SAVE/SAVGAMA.CSV") to the source
    entry; `skipped` says what is left out and why."""

    def __init__(self, source, design, title, files, skipped, notes):
        self.source = source
        self.design = design        # "CURSE" -> CURSE.DSN
        self.title = title
        self.files = files          # {dest: loader}
        self.skipped = skipped      # [(path, reason)]
        self.notes = notes          # [str]

    @property
    def folder(self):
        return self.design + ".DSN"


def plan(source, design=None):
    """Find the design inside `source` and decide every file's fate."""
    roots = {}
    for path, loader in source.entries:
        parts = path.split("/")
        if parts[-1].upper() == DESIGN_FILE:
            roots["/".join(parts[:-1])] = loader
    if not roots:
        raise ModError("%s holds no design (no %s anywhere)" % (source.name, DESIGN_FILE))
    if len(roots) > 1:
        raise ModError("%s holds %d designs (%s); add them one at a time"
                       % (source.name, len(roots), ", ".join(sorted(roots))))
    root, game001 = next(iter(roots.items()))
    prefix = root + "/" if root else ""
    if design is None:
        last = root.split("/")[-1] if root else ""
        design = design_name(last if last.upper().endswith(".DSN") else
                             os.path.splitext(source.name)[0])
    files, skipped, notes = {}, [], []
    for path, loader in source.entries:
        if not path.startswith(prefix):
            skipped.append((path, "outside the design folder"))
            continue
        rel = path[len(prefix):].split("/")
        if rel[-1] == "" or rel[-1].startswith("."):
            continue
        if len(rel) == 2 and rel[0].upper() == "SAVE":
            sub = "SAVE/"
        elif len(rel) == 1:
            sub = ""
        else:
            skipped.append((path, "in a subfolder the engine does not read"))
            continue
        name = rel[-1]
        if name.upper().endswith(SKIP_EXT):
            skipped.append((path, "a DOS/Windows program or temporary file"))
            continue
        dest = dos83(name)
        if dest is None:
            skipped.append((path, "no 8.3 name for it"))
            continue
        dest = sub + dest
        if dest in files:
            skipped.append((path, "same 8.3 name as another file (%s)" % dest))
            continue
        files[dest] = loader
    title = read_title(game001())
    if any(d.endswith(".TLB") for d in files):
        notes.append("PC (DOS) art: the engine converts it the first time it is shown")
    if any(d.endswith(".CTL") for d in files):
        notes.append("Mac art")
    return Plan(source, design, title, files, skipped, notes)


# ---------------------------------------------------------------- targets

class Target:
    """An OpenUA folder: the engine and the base game, where designs go."""

    def __init__(self, path):
        self.path = path

    def check(self):
        """None when this looks like an OpenUA folder, else why not."""
        if not os.path.isdir(self.path):
            return "%s is not a folder" % self.path
        have = {n.upper() for n in os.listdir(self.path)}
        if not any(e in have for e in ENGINES):
            return "no OpenUA engine (frua.prg / frua) in %s" % self.path
        return None

    def designs(self):
        """[(folder, title)] for every design installed."""
        out = []
        for n in sorted(os.listdir(self.path)):
            full = os.path.join(self.path, n)
            if not (os.path.isdir(full) and n.upper().endswith(".DSN")):
                continue
            title = ""
            for f in os.listdir(full):
                if f.upper() == DESIGN_FILE:
                    with open(os.path.join(full, f), "rb") as fh:
                        title = read_title(fh.read(64))
            out.append((n, title))
        return out

    def find(self, folder):
        for n in os.listdir(self.path):
            if n.upper() == folder.upper():
                return os.path.join(self.path, n)
        return None

    def install(self, p, replace=False, progress=None):
        """Write plan `p` into this folder. Returns the design's path."""
        why = self.check()
        if why:
            raise ModError(why)
        old = self.find(p.folder)
        if old and not replace:
            raise ModError("%s is already installed" % p.folder)
        dest = os.path.join(self.path, p.folder)
        tmp = dest + ".NEW"
        if os.path.exists(tmp):
            shutil.rmtree(tmp)
        os.makedirs(os.path.join(tmp, "SAVE"))
        try:
            for i, (rel, loader) in enumerate(sorted(p.files.items())):
                out = os.path.join(tmp, *rel.split("/"))
                with open(out, "wb") as fh:
                    fh.write(loader())
                if progress:
                    progress(i + 1, len(p.files), rel)
        except Exception:
            shutil.rmtree(tmp, ignore_errors=True)
            raise
        if old:
            keep = os.path.join(old, "SAVE")
            if os.path.isdir(keep):         # the player's saves survive a reinstall,
                for f in os.listdir(keep):  # over any the module ships
                    src = os.path.join(keep, f)
                    if os.path.isfile(src):
                        shutil.copy2(src, os.path.join(tmp, "SAVE", f.upper()))
            shutil.rmtree(old)
        os.rename(tmp, dest)
        return dest

    def remove(self, folder):
        path = self.find(folder)
        if path is None:
            raise ModError("%s is not installed" % folder)
        shutil.rmtree(path)
