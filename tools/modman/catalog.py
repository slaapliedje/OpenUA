"""modman.catalog — the list of modules the manager can fetch, and fetching one.

A catalog is plain text, so it can be written by hand, kept in a repository and
read by a 68000 one day as easily as by this program:

    # a line starting with # is a comment; blank lines are ignored
    [curse]
    title   = The Curse of the Fire Dragon
    author  = A. Designer
    url     = https://example.org/modules/curse.zip
    size    = 1605181
    sha256  = 9f2c...
    kind    = pc
    about   = One line shown under the title.

One [section] per module (the name is any short id). Only url is required;
size and sha256 are checked when given; kind is pc or mac.

The catalog does not carry the modules: each entry points at where its author
or an archive already publishes it. Downloads go to a cache folder and are
checked (size, checksum, and that the file really is an archive) before they
are offered for install.
"""
import hashlib
import os
import urllib.request

from . import core

USER_AGENT = "OpenUA-modman/1"
FIELDS = ("title", "author", "url", "size", "sha256", "kind", "about")


class Entry:
    def __init__(self, ident, fields):
        self.ident = ident
        self.title = fields.get("title", ident)
        self.author = fields.get("author", "")
        self.url = fields.get("url", "")
        self.size = int(fields["size"]) if fields.get("size", "").isdigit() else None
        self.sha256 = fields.get("sha256", "").lower() or None
        self.kind = fields.get("kind", "")
        self.about = fields.get("about", "")

    @property
    def filename(self):
        tail = self.url.rstrip("/").rsplit("/", 1)[-1] or self.ident + ".zip"
        return core.design_name(self.ident) + "_" + tail


def parse(text):
    """[Entry] from catalog text. Raises ModError on a malformed line."""
    entries, ident, fields = [], None, {}
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            if ident is not None:
                entries.append(Entry(ident, fields))
            ident, fields = line[1:-1].strip(), {}
            continue
        key, eq, value = line.partition("=")
        if not eq or ident is None:
            raise core.ModError("catalog line %d: expected 'key = value' inside a [section]" % n)
        key = key.strip().lower()
        if key in FIELDS:
            fields[key] = value.strip()
    if ident is not None:
        entries.append(Entry(ident, fields))
    for e in entries:
        if not e.url:
            raise core.ModError("catalog entry [%s] has no url" % e.ident)
    return entries


def load(where):
    """A catalog from a file path or an http(s) URL."""
    if where.startswith(("http://", "https://")):
        req = urllib.request.Request(where, headers={"User-Agent": USER_AGENT})
        with urllib.request.urlopen(req, timeout=30) as r:
            return parse(r.read().decode("utf-8", "replace"))
    with open(where, encoding="utf-8") as fh:
        return parse(fh.read())


def verify(entry, data):
    """ModError unless `data` is what the entry promised and an archive."""
    if entry.size is not None and len(data) != entry.size:
        raise core.ModError("%s: got %d bytes, the catalog says %d"
                            % (entry.title, len(data), entry.size))
    if entry.sha256 and hashlib.sha256(data).hexdigest() != entry.sha256:
        raise core.ModError("%s: the download does not match its checksum" % entry.title)
    core.open_archive(data, entry.title)


def fetch(entry, cache, progress=None, opener=None):
    """The entry's archive, from `cache` when already there and good, else
    downloaded into it. Returns the cached file's path."""
    os.makedirs(cache, exist_ok=True)
    path = os.path.join(cache, entry.filename)
    if os.path.exists(path):
        with open(path, "rb") as fh:
            data = fh.read()
        try:
            verify(entry, data)
            return path
        except core.ModError:
            os.remove(path)
    req = urllib.request.Request(entry.url, headers={"User-Agent": USER_AGENT})
    open_url = opener or urllib.request.urlopen
    chunks, got = [], 0
    with open_url(req, timeout=60) as r:
        total = int(r.headers.get("Content-Length") or 0) or entry.size or 0
        while True:
            b = r.read(65536)
            if not b:
                break
            chunks.append(b)
            got += len(b)
            if progress:
                progress(got, total)
    data = b"".join(chunks)
    verify(entry, data)
    tmp = path + ".part"
    with open(tmp, "wb") as fh:
        fh.write(data)
    os.replace(tmp, path)
    return path
