# atari-compendium-mcp

A local **MCP (Model Context Protocol) stdio server** that exposes Scott
Sanders' *Atari Compendium* — the reference for Atari ST/TT/Falcon programming
(XBIOS, GEMDOS, BIOS, VDI, AES, Line-A, hardware registers) — as searchable
tools, so Claude Code sessions can look things up while doing Atari development
on OpenUA.

## Copyright

The *Atari Compendium* PDF and its extracted text are **copyrighted**. Only the
server **code** in this directory is committed — the same policy the repo
applies to `data/`. The PDF, the venv, and the extracted text cache are all
git-ignored and must never be committed.

## What it does

On first run (or with `--reindex`) the server extracts the PDF to per-page text
with `pdftotext -layout`, splitting on the form-feed (`\f`) page breaks poppler
emits, and writes a cache **outside the repo** at
`~/.cache/atari-compendium/pages.json` (plus a derived `toc.json`). Subsequent
runs load the cache, so startup is fast and there is **no network at runtime**.

The PDF path is read from `ATARI_COMPENDIUM_PDF` (default:
`/home/jfergus/Downloads/Atari/Atari Compendium - Scott Sanders.pdf`). The cache
directory can be overridden with `ATARI_COMPENDIUM_CACHE`.

The current PDF is **842 pages**.

## Tools

- **`search(query, max_results=5)`** — ranks pages with a hand-rolled BM25
  scorer (case-insensitive, tokenized on word boundaries; no external index
  dependency). Returns, per hit: page number, score, and a ~400-char snippet
  centered on the densest cluster of query-term matches. Good queries:
  `Blitmode`, `vro_cpyfm`, `GEMDOS Fopen`, `Setscreen`, `Line-A`,
  `VDI vsl_color`, `XBIOS Vsync`.
- **`get_page(page, context=0)`** — full text of a 1-based page; with
  `context > 0`, also that many pages on either side.
- **`table_of_contents()`** — the PDF has no embedded bookmarks, so this returns
  a lightweight section list derived from ALL-CAPS heading lines (running
  page-headers are filtered out), clearly labelled as derived/approximate. If a
  future PDF carries bookmarks and `mutool` is installed, those are used
  instead.

## Setup

The server uses the official `mcp` Python SDK (FastMCP) in a local venv:

```sh
cd tools/atari-compendium-mcp
python3 -m venv .venv
.venv/bin/pip install mcp
```

Build the text cache and print the page count:

```sh
.venv/bin/python server.py --reindex
```

Run the built-in query self-test (no MCP client needed):

```sh
.venv/bin/python server.py --selftest
```

Exercise it through the actual MCP stdio protocol:

```sh
.venv/bin/python test_client.py
```

## Register with Claude Code

MCP servers load at **session start**, so registering does not affect the
current session — start a new one afterward. Register at **user scope** (this is
a personal tool pointing at a personal PDF path; do **not** commit a project
`.mcp.json` with a hardcoded personal path):

```sh
claude mcp add atari-compendium -s user -- \
  /home/jfergus/dev/OpenUA/tools/atari-compendium-mcp/.venv/bin/python \
  /home/jfergus/dev/OpenUA/tools/atari-compendium-mcp/server.py
```

If the PDF ever moves, pass its location via the environment:

```sh
claude mcp add atari-compendium -s user \
  -e ATARI_COMPENDIUM_PDF=/new/path/to/AtariCompendium.pdf -- \
  /home/jfergus/dev/OpenUA/tools/atari-compendium-mcp/.venv/bin/python \
  /home/jfergus/dev/OpenUA/tools/atari-compendium-mcp/server.py
```

Verify and use:

```sh
claude mcp list
# then, in a new session, the tools appear as:
#   mcp__atari-compendium__search
#   mcp__atari-compendium__get_page
#   mcp__atari-compendium__table_of_contents
```

## Files

- `server.py` — the MCP stdio server (extraction, BM25 index, tools, CLI).
- `test_client.py` — MCP stdio client smoke test.
- `README.md` — this file.
- `.gitignore` — keeps the venv, PDF, and extracted text out of git.
