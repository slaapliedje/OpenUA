#!/usr/bin/env python3
"""Atari Compendium MCP stdio server.

Exposes Scott Sanders' *Atari Compendium* (the reference for Atari
ST/TT/Falcon programming — XBIOS, GEMDOS, BIOS, VDI, AES, Line-A,
hardware registers) as searchable MCP tools so Claude Code sessions can
look things up while doing Atari development on the OpenUA project.

The PDF and its extracted text are COPYRIGHTED and are never committed —
only this server code lives in the repo. The extracted per-page text is
cached OUTSIDE the repo (default ~/.cache/atari-compendium/pages.json).

Tools:
  - search(query, max_results=5) : keyword-ranked page hits with snippets.
  - get_page(page, context=0)    : full text of a page (+/- context pages).
  - table_of_contents()          : PDF bookmarks, or a derived heading list.

Env:
  ATARI_COMPENDIUM_PDF   path to the source PDF (default below).
  ATARI_COMPENDIUM_CACHE path to the cache dir (default ~/.cache/atari-compendium).

Run standalone to (re)build the cache:
  python server.py --reindex     rebuild the text cache, print page count, exit.
  python server.py --selftest    build cache if needed, run a few queries, exit.
  python server.py               serve MCP over stdio (builds cache if missing).
"""

import json
import math
import os
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_PDF = "/home/jfergus/Downloads/Atari/Atari Compendium - Scott Sanders.pdf"


def pdf_path() -> Path:
    return Path(os.environ.get("ATARI_COMPENDIUM_PDF", DEFAULT_PDF)).expanduser()


def cache_dir() -> Path:
    d = os.environ.get("ATARI_COMPENDIUM_CACHE")
    if d:
        return Path(d).expanduser()
    return Path.home() / ".cache" / "atari-compendium"


def pages_cache_file() -> Path:
    return cache_dir() / "pages.json"


def toc_cache_file() -> Path:
    return cache_dir() / "toc.json"


# --------------------------------------------------------------------------
# Extraction / cache
# --------------------------------------------------------------------------

_TOKEN_RE = re.compile(r"[A-Za-z0-9_]+")


def tokenize(text: str):
    return [t.lower() for t in _TOKEN_RE.findall(text)]


def extract_pages(pdf: Path):
    """Extract the whole PDF once with `pdftotext -layout`, splitting on the
    form-feed (\\f) page breaks pdftotext emits between pages. Returns a list
    of {"page": int (1-based), "text": str}."""
    if not pdf.exists():
        raise FileNotFoundError(f"PDF not found: {pdf}")
    # Emit to stdout so we get the whole document in one pass with the form
    # feeds intact; -layout preserves the columnar layout of the reference.
    proc = subprocess.run(
        ["pdftotext", "-layout", str(pdf), "-"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    raw = proc.stdout.decode("utf-8", errors="replace")
    # pdftotext separates pages with \f (form feed). A trailing \f produces a
    # final empty element which we keep so page numbers stay aligned, then trim.
    chunks = raw.split("\f")
    if chunks and chunks[-1].strip() == "":
        chunks = chunks[:-1]
    return [{"page": i + 1, "text": chunk} for i, chunk in enumerate(chunks)]


def extract_toc(pdf: Path):
    """Return the PDF bookmarks as a list of {level, title, page} if present,
    else an empty list. Uses `pdfinfo -listenc`... actually pdfinfo doesn't
    dump bookmarks; we parse `pdftotext`'s sibling isn't available, so we try
    the poppler `pdfinfo` bookmark listing via -meta is unavailable too.
    We shell out to `pdfinfo` and, separately, attempt bookmarks through
    `pdftk`/`mutool` if present; otherwise return []."""
    # Try mutool (poppler has no CLI bookmark dump; mutool does).
    for tool, args in (
        ("mutool", ["mutool", "show", str(pdf), "outline"]),
    ):
        exe = _which(tool)
        if not exe:
            continue
        try:
            out = subprocess.run(
                args, check=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL
            ).stdout.decode("utf-8", errors="replace")
        except Exception:
            continue
        toc = _parse_mutool_outline(out)
        if toc:
            return toc
    return []


def _which(name: str):
    from shutil import which
    return which(name)


def _parse_mutool_outline(out: str):
    toc = []
    for line in out.splitlines():
        if not line.strip():
            continue
        # mutool outline lines look like: "+ Title\t#page,..." with indentation
        indent = len(line) - len(line.lstrip("\t +|"))
        m = re.search(r"#(?:page=)?(\d+)", line)
        title = line.strip().lstrip("+|- ").strip()
        # strip a trailing "\t#..." destination from the title
        title = re.split(r"\t", title)[0].strip()
        if title:
            toc.append({"level": indent, "title": title, "page": int(m.group(1)) if m else None})
    return toc


# Heading heuristic for deriving a TOC when no bookmarks exist. The Compendium
# uses ALL-CAPS section banners and function-name headers.
_HEADING_RE = re.compile(r"^\s*([A-Z][A-Z0-9 ,/&()\-]{4,60})\s*$")


def _norm_title(title):
    # Collapse the spaced-out-caps artifacts pdftotext produces from tracked
    # display type (e.g. "T HE AT ARI COMPENDIUM" -> "THE ATARI COMPENDIUM").
    return re.sub(r"\s+", " ", title).strip()


def derive_headings(pages):
    """Lightweight section list derived from ALL-CAPS heading-like lines.

    Running headers/footers (the same banner on many pages) are dropped so the
    result reads more like a section list than page furniture."""
    # First pass: collect candidate (title, page) and count title frequency.
    raw = []
    freq = {}
    for p in pages:
        page_seen = set()
        for line in p["text"].splitlines():
            m = _HEADING_RE.match(line)
            if not m:
                continue
            title = _norm_title(m.group(1))
            letters = sum(c.isalpha() for c in title)
            if letters < 4:
                continue
            if title in page_seen:
                continue
            page_seen.add(title)
            raw.append((title, p["page"]))
            freq[title] = freq.get(title, 0) + 1
    # A heading appearing on many pages is a running header, not a section.
    RUNNING_HEADER_THRESHOLD = 8
    headings = []
    for title, page in raw:
        if freq[title] > RUNNING_HEADER_THRESHOLD:
            continue
        headings.append({"level": 0, "title": title, "page": page})
    return headings


def build_cache(pdf: Path, verbose: bool = False):
    cache_dir().mkdir(parents=True, exist_ok=True)
    if verbose:
        print(f"Extracting {pdf} ...", file=sys.stderr)
    pages = extract_pages(pdf)
    with open(pages_cache_file(), "w", encoding="utf-8") as f:
        json.dump(pages, f)
    toc = extract_toc(pdf)
    if not toc:
        toc = derive_headings(pages)
        toc_source = "derived-headings"
    else:
        toc_source = "pdf-bookmarks"
    with open(toc_cache_file(), "w", encoding="utf-8") as f:
        json.dump({"source": toc_source, "entries": toc}, f)
    if verbose:
        print(f"Cached {len(pages)} pages -> {pages_cache_file()}", file=sys.stderr)
        print(f"TOC ({toc_source}): {len(toc)} entries", file=sys.stderr)
    return pages, {"source": toc_source, "entries": toc}


def load_cache(reindex: bool = False):
    """Return (pages, toc). Builds the cache if missing or if reindex=True."""
    pf = pages_cache_file()
    if reindex or not pf.exists():
        return build_cache(pdf_path(), verbose=True)
    with open(pf, encoding="utf-8") as f:
        pages = json.load(f)
    tf = toc_cache_file()
    if tf.exists():
        with open(tf, encoding="utf-8") as f:
            toc = json.load(f)
    else:
        toc = {"source": "none", "entries": []}
    return pages, toc


# --------------------------------------------------------------------------
# Search index (hand-rolled BM25)
# --------------------------------------------------------------------------

class Index:
    """A tiny BM25 index over the cached page text. No external deps."""

    K1 = 1.5
    B = 0.75

    def __init__(self, pages):
        self.pages = pages
        self.doc_tokens = []          # per-page token lists
        self.doc_freq = []            # per-page {term: count}
        self.df = {}                  # term -> #pages containing it
        self.doc_len = []
        for p in pages:
            toks = tokenize(p["text"])
            self.doc_tokens.append(toks)
            self.doc_len.append(len(toks))
            tf = {}
            for t in toks:
                tf[t] = tf.get(t, 0) + 1
            self.doc_freq.append(tf)
            for t in tf:
                self.df[t] = self.df.get(t, 0) + 1
        self.N = len(pages)
        self.avgdl = (sum(self.doc_len) / self.N) if self.N else 0.0

    def _idf(self, term):
        n = self.df.get(term, 0)
        if n == 0:
            return 0.0
        return math.log(1 + (self.N - n + 0.5) / (n + 0.5))

    def search(self, query, max_results=5):
        q_terms = tokenize(query)
        if not q_terms:
            return []
        idfs = {t: self._idf(t) for t in set(q_terms)}
        scored = []
        for i in range(self.N):
            tf = self.doc_freq[i]
            dl = self.doc_len[i]
            score = 0.0
            for t in q_terms:
                f = tf.get(t, 0)
                if f == 0:
                    continue
                idf = idfs[t]
                denom = f + self.K1 * (1 - self.B + self.B * dl / (self.avgdl or 1))
                score += idf * (f * (self.K1 + 1)) / denom
            if score > 0:
                scored.append((score, i))
        scored.sort(key=lambda x: (-x[0], x[1]))
        results = []
        for score, i in scored[:max_results]:
            page = self.pages[i]
            snippet = make_snippet(page["text"], q_terms)
            results.append({
                "page": page["page"],
                "score": round(score, 3),
                "snippet": snippet,
            })
        return results


def make_snippet(text, q_terms, width=400):
    """Return a ~width-char snippet centered on the densest cluster of query
    term matches in the page text."""
    low = text.lower()
    positions = []
    for t in set(q_terms):
        start = 0
        while True:
            idx = low.find(t, start)
            if idx == -1:
                break
            positions.append(idx)
            start = idx + len(t)
    if not positions:
        s = " ".join(text.split())
        return s[:width]
    positions.sort()
    # Find the window of `width` chars containing the most match positions.
    best_center = positions[0]
    best_count = 0
    for p in positions:
        lo, hi = p, p + width
        c = sum(1 for q in positions if lo <= q <= hi)
        if c > best_count:
            best_count = c
            best_center = p
    start = max(0, best_center - width // 2)
    end = min(len(text), start + width)
    snippet = text[start:end]
    snippet = " ".join(snippet.split())  # collapse -layout whitespace
    prefix = "..." if start > 0 else ""
    suffix = "..." if end < len(text) else ""
    return f"{prefix}{snippet}{suffix}"


# --------------------------------------------------------------------------
# Server wiring
# --------------------------------------------------------------------------

_STATE = {"pages": None, "toc": None, "index": None}


def ensure_loaded(reindex: bool = False):
    if _STATE["index"] is None or reindex:
        pages, toc = load_cache(reindex=reindex)
        _STATE["pages"] = pages
        _STATE["toc"] = toc
        _STATE["index"] = Index(pages)
    return _STATE


def do_search(query: str, max_results: int = 5):
    st = ensure_loaded()
    return st["index"].search(query, max_results=max_results)


def do_get_page(page: int, context: int = 0):
    st = ensure_loaded()
    pages = st["pages"]
    n = len(pages)
    if page < 1 or page > n:
        return {"error": f"page {page} out of range 1..{n}"}
    lo = max(1, page - context)
    hi = min(n, page + context)
    out = []
    for pnum in range(lo, hi + 1):
        out.append({"page": pnum, "text": pages[pnum - 1]["text"]})
    return {"page": page, "context": context, "pages": out, "total_pages": n}


def do_toc():
    st = ensure_loaded()
    toc = st["toc"]
    entries = toc.get("entries", [])
    source = toc.get("source", "none")
    note = None
    if source == "derived-headings":
        note = ("No embedded PDF bookmarks; this list is derived from ALL-CAPS "
                "heading lines in the extracted text and is approximate.")
    elif not entries:
        note = "No embedded TOC and no headings could be derived."
    return {"source": source, "note": note, "count": len(entries), "entries": entries}


def build_mcp():
    from mcp.server.fastmcp import FastMCP

    mcp = FastMCP("atari-compendium")

    @mcp.tool()
    def search(query: str, max_results: int = 5) -> str:
        """Search the Atari Compendium for a keyword/phrase (XBIOS/GEMDOS/BIOS/
        VDI/AES/Line-A calls, hardware registers, etc). Returns ranked page
        hits, each with the page number, a relevance score, and a ~400-char
        snippet centered on the best match. Good queries: function names like
        'vro_cpyfm', call names like 'Setscreen', register names like
        'Blitmode'."""
        results = do_search(query, max_results=max_results)
        if not results:
            return f"No matches for {query!r}."
        lines = [f"{len(results)} result(s) for {query!r}:\n"]
        for r in results:
            lines.append(f"[page {r['page']}] score={r['score']}\n{r['snippet']}\n")
        return "\n".join(lines)

    @mcp.tool()
    def get_page(page: int, context: int = 0) -> str:
        """Return the full extracted text of a Compendium page (1-based). If
        context > 0, also include that many pages on either side."""
        res = do_get_page(page, context=context)
        if "error" in res:
            return res["error"]
        parts = []
        for p in res["pages"]:
            parts.append(f"===== PAGE {p['page']} / {res['total_pages']} =====\n{p['text']}")
        return "\n\n".join(parts)

    @mcp.tool()
    def table_of_contents() -> str:
        """Return the Compendium's table of contents. Uses embedded PDF
        bookmarks if present, otherwise a heading list derived from the text."""
        res = do_toc()
        lines = [f"TOC source: {res['source']} ({res['count']} entries)"]
        if res.get("note"):
            lines.append(res["note"])
        lines.append("")
        for e in res["entries"]:
            indent = "  " * int(e.get("level", 0) or 0)
            pg = e.get("page")
            pg_s = f"  (p.{pg})" if pg else ""
            lines.append(f"{indent}{e['title']}{pg_s}")
        return "\n".join(lines)

    return mcp


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def _selftest():
    st = ensure_loaded()
    print(f"Pages cached: {len(st['pages'])}")
    for q in ["Blitmode", "vro_cpyfm", "GEMDOS Fopen", "Setscreen",
              "Line-A", "VDI vsl_color", "XBIOS Vsync"]:
        hits = do_search(q, max_results=3)
        top = hits[0] if hits else None
        if top:
            print(f"\n== search({q!r}) -> {len(hits)} hits; "
                  f"top page {top['page']} score {top['score']}")
            print("   " + top["snippet"][:200])
        else:
            print(f"\n== search({q!r}) -> NO HITS")


def main(argv):
    if "--reindex" in argv:
        pages, toc = build_cache(pdf_path(), verbose=True)
        print(f"Page count: {len(pages)}")
        print(f"TOC: {toc['source']} ({len(toc['entries'])} entries)")
        return
    if "--selftest" in argv:
        _selftest()
        return
    # Default: serve MCP over stdio (build cache if missing).
    ensure_loaded()
    mcp = build_mcp()
    mcp.run()


if __name__ == "__main__":
    main(sys.argv[1:])
