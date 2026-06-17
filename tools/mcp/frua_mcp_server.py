#!/usr/bin/env python3
"""FRUA reference MCP server — stdio, dependency-free JSON-RPC 2.0.

A Model Context Protocol server that gives a coding assistant authoritative
reference while lifting the Macintosh Resource Manager / Memory Manager and
FRUA's FC (file-cache) GLIB-group subsystem. Two bodies of knowledge in one
server:

  toolbox.*  Authored Mac Toolbox reference (Resource Manager, Memory Manager,
             resource-fork on-disk format, and FRUA's FC group cache) under
             tools/mcp/reference/toolbox/*.md. Committed; expand freely.

  rfork.* / fc.* / jt.* / sym.* / disasm.*
             Live queries over OUR artifacts: the application resource fork
             (data/work/UnlimitedAdventures.rfork via tools/macrsrc.py), the
             FC group map, the A5 jump table + docs/function-reference.md, the
             lifted C in src/engine/, and the objdump listings under
             data/work/disasm/. The data/ inputs are git-ignored (copyrighted
             FRUA assets); this server only READS them at runtime.

Transport: newline-delimited JSON-RPC 2.0 over stdin/stdout (the MCP stdio
transport). No third-party packages required — runs on a stock Python 3.

Register in .mcp.json:
  { "mcpServers": { "frua-reference": {
      "command": "python3",
      "args": ["tools/mcp/frua_mcp_server.py"] } } }
"""

import sys
import os
import io
import json
import re
import glob

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))          # tools/mcp -> repo root
REF_TOOLBOX = os.path.join(HERE, "reference", "toolbox")
REF_TRACES = os.path.join(HERE, "reference", "traces")
DATA = os.path.join(REPO, "data", "work")
DISASM = os.path.join(DATA, "disasm")
RFORK = os.path.join(DATA, "UnlimitedAdventures.rfork")
JUMPTABLE = os.path.join(DISASM, "jumptable.txt")
DOCS = os.path.join(REPO, "docs")
ENGINE = os.path.join(REPO, "src", "engine")
FUNCREF = os.path.join(DOCS, "function-reference.md")

sys.path.insert(0, os.path.join(REPO, "tools"))
try:
    from macrsrc import ResourceFork
except Exception:                                      # pragma: no cover
    ResourceFork = None

SERVER_NAME = "frua-reference"
SERVER_VERSION = "0.1.0"


# --------------------------------------------------------------------------- #
# Small helpers
# --------------------------------------------------------------------------- #
def _read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def _missing_data(what, path):
    return (f"[{what} unavailable] expected `{os.path.relpath(path, REPO)}`.\n"
            "The FRUA assets under data/ are git-ignored and built locally — "
            "see docs/mac-release.md (rfork) and run `python3 tools/dis68k.py "
            "data/work/UnlimitedAdventures.rfork` (disassembly). This server "
            "only reads them; it cannot regenerate them.")


def _hexdump(data, base=0, limit=512):
    out = []
    n = min(len(data), limit)
    for off in range(0, n, 16):
        chunk = data[off:off + 16]
        hexs = " ".join(f"{b:02x}" for b in chunk)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        out.append(f"  {base + off:06x}  {hexs:<47}  {text}")
    if len(data) > limit:
        out.append(f"  ... ({len(data) - limit} more bytes; raise `bytes`)")
    return "\n".join(out)


# --------------------------------------------------------------------------- #
# Reference corpus (toolbox.*)
# --------------------------------------------------------------------------- #
_SECTION_RE = re.compile(r"^(#{1,3})\s+(.*)$")


def _corpus_files(refdir=REF_TOOLBOX):
    if not os.path.isdir(refdir):
        return []
    return sorted(glob.glob(os.path.join(refdir, "*.md")))


def _split_sections(path):
    """Yield (anchor, heading, body) for each ## / ### section of a doc."""
    text = _read(path)
    stem = os.path.splitext(os.path.basename(path))[0]
    cur_head = stem
    cur_anchor = stem
    buf = []
    for line in text.splitlines():
        m = _SECTION_RE.match(line)
        if m and len(m.group(1)) >= 2:
            if buf:
                yield (cur_anchor, cur_head, "\n".join(buf).strip())
                buf = []
            cur_head = m.group(2).strip()
            cur_anchor = f"{stem}#{cur_head.lower().replace(' ', '-')}"
        else:
            buf.append(line)
    if buf:
        yield (cur_anchor, cur_head, "\n".join(buf).strip())


# Generic reference-corpus ops, shared by toolbox.* (Mac docs) and trace.*
# (captured real-Mac runtime traces). Each is a dir of `##`-sectioned markdown.
def _ref_list(refdir, title, get_name, search_name):
    files = _corpus_files(refdir)
    if not files:
        return f"[no corpus] expected markdown under {refdir}"
    out = [title + "\n"]
    for path in files:
        stem = os.path.splitext(os.path.basename(path))[0]
        heads = [h for _, h, _ in _split_sections(path)]
        out.append(f"- {stem}: {heads[0] if heads else stem}")
        for s in [h for h in heads[1:] if h]:
            out.append(f"    · {s}")
    out.append(f"\nUse {get_name}(topic=<stem>) for full text, "
               f"{search_name}(query=...) to search.")
    return "\n".join(out)


def _ref_get(refdir, args):
    topic = (args.get("topic") or "").strip()
    if not topic:
        return "error: `topic` required (a corpus stem)"
    stem = re.sub(r"\.md$", "", os.path.basename(topic))
    path = os.path.join(refdir, stem + ".md")
    if not os.path.isfile(path):
        avail = ", ".join(os.path.splitext(os.path.basename(p))[0]
                          for p in _corpus_files(refdir))
        return f"error: no topic '{stem}'. Available: {avail}"
    return _read(path)


def _ref_search(refdir, args, default_max=6):
    query = (args.get("query") or "").strip()
    if not query:
        return "error: `query` required"
    maxn = int(args.get("max", default_max))
    terms = [t for t in re.split(r"\W+", query.lower()) if t]
    if not terms:
        return "error: no searchable terms in query"
    hits = []
    for path in _corpus_files(refdir):
        for anchor, head, body in _split_sections(path):
            hay = (head + "\n" + body).lower()
            score = 0
            for t in terms:
                c = hay.count(t)
                if c:
                    score += c
                    if t in head.lower():
                        score += 5          # heading match weighs more
            if score:
                hits.append((score, anchor, head, body))
    if not hits:
        return f"no matches for '{query}'."
    hits.sort(key=lambda h: -h[0])
    out = [f"Top {min(maxn, len(hits))} matches for '{query}':\n"]
    for score, anchor, head, body in hits[:maxn]:
        snippet = body if len(body) <= 800 else body[:800] + "\n  ...(truncated)"
        out.append(f"### {anchor}  (score {score})\n{snippet}\n")
    return "\n".join(out)


def tool_toolbox_list(_args):
    return _ref_list(REF_TOOLBOX, "Mac Toolbox / FRUA reference topics:",
                     "toolbox_get", "toolbox_search")


def tool_toolbox_get(args):
    return _ref_get(REF_TOOLBOX, args)


def tool_toolbox_search(args):
    return _ref_search(REF_TOOLBOX, args)


def tool_trace_list(_args):
    return _ref_list(REF_TRACES,
                     "Captured real-Mac (BasiliskII) runtime traces:",
                     "trace_get", "trace_search")


def tool_trace_get(args):
    return _ref_get(REF_TRACES, args)


def tool_trace_search(args):
    return _ref_search(REF_TRACES, args)


# --------------------------------------------------------------------------- #
# Resource fork (rfork.*)
# --------------------------------------------------------------------------- #
_RF_CACHE = {}


def _load_rfork():
    if not os.path.isfile(RFORK):
        return None
    if ResourceFork is None:
        return None
    if "rf" not in _RF_CACHE:
        _RF_CACHE["rf"] = ResourceFork.from_file(RFORK)
    return _RF_CACHE["rf"]


def tool_rfork_list(args):
    rf = _load_rfork()
    if rf is None:
        return _missing_data("resource fork", RFORK)
    rtype = (args.get("type") or "").strip()
    if not rtype:
        by = {}
        for r in rf.resources:
            t = by.setdefault(r.type, [0, 0])
            t[0] += 1
            t[1] += len(r.data)
        out = ["Resource fork summary (type: count, bytes):\n"]
        for t in sorted(by):
            c, sz = by[t]
            out.append(f"  {t!r:8} {c:4d}  {sz:>8d}")
        out.append("\nPass type=CODE (etc.) for per-resource ids/sizes/names.")
        return "\n".join(out)
    rtype = (rtype + "    ")[:4]
    res = rf.of_type(rtype)
    if not res:
        return f"no resources of type {rtype!r}."
    out = [f"{rtype!r} resources (id, size, name):\n"]
    for r in res:
        out.append(f"  {r.id:6d}  {len(r.data):>8d}  {r.name}")
    return "\n".join(out)


def tool_rfork_get(args):
    rf = _load_rfork()
    if rf is None:
        return _missing_data("resource fork", RFORK)
    rtype = (args.get("type") or "").strip()
    if not rtype or "id" not in args:
        return "error: `type` (4-char) and `id` (int) required"
    rtype = (rtype + "    ")[:4]
    try:
        r = rf.get(rtype, int(args["id"]))
    except KeyError:
        return f"no resource {rtype!r} id {args['id']}"
    limit = int(args.get("bytes", 512))
    head = (f"{rtype!r} id {r.id}  name={r.name!r}  attrs=0x{r.attrs:02x}  "
            f"size={len(r.data)} bytes")
    return head + "\n" + _hexdump(r.data, limit=limit)


# --------------------------------------------------------------------------- #
# FC group cache (fc.*)
# --------------------------------------------------------------------------- #
# The port's GLIB-group -> source-file map (the FC file cache, jt468). Groups
# 0/1 are resident; the rest are purgeable, loaded on demand into the FAR pool.
# Mirrors docs/function-reference.md + docs/glib-resource-groups + the wall
# memories. Keep in sync with the engine's jt468 / l37aa routing.
FC_GROUP_MAP = [
    (0,  "ALWAYS.CTL", "resident", "buttons / markers / cursors (jt452 shapes)"),
    (1,  "FRAME.CTL",  "resident", "frame bevels + the play-screen chrome"),
    (24, "MENU.CTL",   "purgeable", "main-menu command-bar plate glyphs"),
    (-1, "8X8DB.CTL / 8X8DC.CTL", "purgeable",
         "dungeon wall sets (GLIB-of-GLIBs; GEO HDR Wall1/2/3 select the set)"),
    (-1, "<design>.GLB / *.CTL / *.TLB", "purgeable",
         "per-design art libraries, loaded by bare filename via the FC pool"),
]


def tool_fc_groups(_args):
    out = ["FRUA FC (file-cache) GLIB groups — jt468(group) -> base:\n"]
    out.append("  group  file                      residency  contents")
    for g, f, res, desc in FC_GROUP_MAP:
        gid = "  *" if g < 0 else f"{g:3d}"
        out.append(f"  {gid}    {f:<25} {res:<10} {desc}")
    out.append("")
    out.append("Mechanics:")
    out.append("  jt468(group)  -> resolve a loaded GLIB base for `group`.")
    out.append("  l37aa(base,i) -> verify 'GLIB' magic, bounds-check item i,")
    out.append("                   return base + offset[i] (the item record).")
    out.append("  Resident groups (0,1) stay mapped; others are purgeable and")
    out.append("  loaded on demand into the FAR pool (an LRU cache keyed by the")
    out.append("  group tag). This is FRUA's app-level cache built ON TOP of the")
    out.append("  Mac Resource Manager's purgeable handles — see")
    out.append("  toolbox_get(topic=fc-group-cache) and toolbox_get(")
    out.append("  topic=resource-manager).")
    # Surface any live doc references too.
    refs = _grep_files([FUNCREF, os.path.join(DOCS, "decompilation.md")],
                       r"jt468|file cache|FC group", max_hits=8)
    if refs:
        out.append("\nProject doc references:")
        out.append(refs)
    return "\n".join(out)


# --------------------------------------------------------------------------- #
# Jump table + lifted symbols (jt.* / sym.* / disasm.*)
# --------------------------------------------------------------------------- #
def _grep_files(paths, pattern, max_hits=40, flags=re.IGNORECASE):
    rx = re.compile(pattern, flags)
    out = []
    for path in paths:
        if not os.path.isfile(path):
            continue
        rel = os.path.relpath(path, REPO)
        try:
            for n, line in enumerate(_read(path).splitlines(), 1):
                if rx.search(line):
                    out.append(f"  {rel}:{n}: {line.rstrip()}")
                    if len(out) >= max_hits:
                        out.append("  ...(truncated; narrow the pattern)")
                        return "\n".join(out)
        except Exception:
            continue
    return "\n".join(out)


def tool_jt_lookup(args):
    name = (args.get("name") or "").strip()
    if not name:
        return "error: `name` required (e.g. jt468 or 468)"
    m = re.search(r"(\d+)", name)
    if not m:
        return "error: could not parse a JT index from the name"
    idx = int(m.group(1))
    out = [f"JT[{idx}] (jt{idx}):\n"]
    # 1. address from the jump table dump
    if os.path.isfile(JUMPTABLE):
        rx = re.compile(rf"^\s*JT\[\s*{idx}\]\s")
        for line in _read(JUMPTABLE).splitlines():
            if rx.search(line):
                out.append("  jumptable: " + line.strip())
                break
    else:
        out.append("  " + _missing_data("jump table", JUMPTABLE).splitlines()[0])
    # 2. curated notes from the function reference
    notes = _grep_files([FUNCREF], rf"jt{idx}\b", max_hits=8)
    if notes:
        out.append("\n  function-reference.md:")
        out.append(notes)
    # 3. lifted C: definition + caller count in the engine
    defs = _grep_files(_engine_files(),
                       rf"\b(static\b.*\bjt{idx}\s*\(|jt{idx}\s*\([^)]*\)\s*$)",
                       max_hits=6)
    if defs:
        out.append("\n  lifted C (defs/sites):")
        out.append(defs)
    if len(out) == 1:
        out.append("  (no records found — try sym_search or disasm_grep)")
    return "\n".join(out)


def _engine_files():
    return sorted(glob.glob(os.path.join(ENGINE, "*.c"))) + \
        sorted(glob.glob(os.path.join(REPO, "compat", "*.c"))) + \
        sorted(glob.glob(os.path.join(REPO, "compat", "*.h")))


def tool_sym_search(args):
    query = (args.get("query") or "").strip()
    if not query:
        return "error: `query` required"
    maxn = int(args.get("max", 30))
    hits = _grep_files(_engine_files(), re.escape(query), max_hits=maxn)
    return hits or f"no engine/compat lines match '{query}'."


def tool_disasm_grep(args):
    pattern = (args.get("pattern") or "").strip()
    if not pattern:
        return "error: `pattern` (regex) required"
    if not os.path.isdir(DISASM):
        return _missing_data("disassembly", DISASM)
    code = (args.get("code") or "").strip()
    maxn = int(args.get("max", 40))
    if code:
        m = re.search(r"(\d+)", code)
        files = [os.path.join(DISASM, f"CODE_{int(m.group(1)):02d}.s")] if m else []
    else:
        files = sorted(glob.glob(os.path.join(DISASM, "CODE_*.s")))
    hits = _grep_files(files, pattern, max_hits=maxn, flags=0)
    return hits or f"no disassembly lines match /{pattern}/."


# --------------------------------------------------------------------------- #
# Tool registry
# --------------------------------------------------------------------------- #
def _t(name, desc, props, required=None):
    return {
        "name": name,
        "description": desc,
        "inputSchema": {
            "type": "object",
            "properties": props,
            "required": required or [],
        },
    }


TOOLS = [
    _t("toolbox_list",
       "List the authored Mac-Toolbox/FRUA reference topics and their sections "
       "(Resource Manager, Memory Manager, resource-fork format, FC group "
       "cache).", {}),
    _t("toolbox_get",
       "Return the full text of one reference topic.",
       {"topic": {"type": "string",
                  "description": "corpus stem, e.g. resource-manager"}},
       ["topic"]),
    _t("toolbox_search",
       "Full-text search the reference corpus; returns ranked sections. Use "
       "before lifting Resource Manager / purgeable-handle / FC-cache code.",
       {"query": {"type": "string"},
        "max": {"type": "integer", "description": "max sections (default 6)"}},
       ["query"]),
    _t("rfork_list",
       "List the application resource fork. No `type` = summary by type; a "
       "4-char `type` (e.g. CODE) = per-resource id/size/name.",
       {"type": {"type": "string", "description": "4-char ResType, optional"}}),
    _t("rfork_get",
       "Hexdump one resource (type,id) from the application resource fork.",
       {"type": {"type": "string"}, "id": {"type": "integer"},
        "bytes": {"type": "integer", "description": "max bytes (default 512)"}},
       ["type", "id"]),
    _t("fc_groups",
       "Show the FRUA FC (file-cache) GLIB-group -> source-file map and the "
       "jt468/l37aa lookup mechanics (residency, purgeable FAR-pool loading).",
       {}),
    _t("jt_lookup",
       "Resolve a JT entry: A5 jump-table address, function-reference notes, "
       "and the lifted-C definition/sites. Arg `name` like jt468 or 468.",
       {"name": {"type": "string"}}, ["name"]),
    _t("sym_search",
       "Grep the lifted C (src/engine + compat) for a symbol or phrase.",
       {"query": {"type": "string"},
        "max": {"type": "integer", "description": "max hits (default 30)"}},
       ["query"]),
    _t("disasm_grep",
       "Search the objdump listings under data/work/disasm. Optional `code` "
       "(e.g. 5) restricts to CODE_05.s.",
       {"pattern": {"type": "string", "description": "regex"},
        "code": {"type": "string"},
        "max": {"type": "integer"}},
       ["pattern"]),
    _t("trace_list",
       "List the captured real-Mac (BasiliskII) runtime traces — ground truth "
       "for validating the port's render/behaviour (e.g. the char-gen JT[1089] "
       "draw sequence: coords, colours, label order).", {}),
    _t("trace_get",
       "Return the full text of one captured trace.",
       {"topic": {"type": "string",
                  "description": "trace stem, e.g. chargen-render-jt1089"}},
       ["topic"]),
    _t("trace_search",
       "Full-text search the captured Mac traces; returns ranked sections.",
       {"query": {"type": "string"},
        "max": {"type": "integer", "description": "max sections (default 6)"}},
       ["query"]),
]

DISPATCH = {
    "toolbox_list": tool_toolbox_list,
    "toolbox_get": tool_toolbox_get,
    "toolbox_search": tool_toolbox_search,
    "rfork_list": tool_rfork_list,
    "rfork_get": tool_rfork_get,
    "fc_groups": tool_fc_groups,
    "jt_lookup": tool_jt_lookup,
    "sym_search": tool_sym_search,
    "disasm_grep": tool_disasm_grep,
    "trace_list": tool_trace_list,
    "trace_get": tool_trace_get,
    "trace_search": tool_trace_search,
}


# --------------------------------------------------------------------------- #
# JSON-RPC 2.0 over stdio
# --------------------------------------------------------------------------- #
def _send(obj, out):
    out.write(json.dumps(obj) + "\n")
    out.flush()


def _result(rid, result, out):
    _send({"jsonrpc": "2.0", "id": rid, "result": result}, out)


def _error(rid, code, message, out):
    _send({"jsonrpc": "2.0", "id": rid, "error":
           {"code": code, "message": message}}, out)


def handle(req, out):
    """Process one decoded JSON-RPC request/notification. Returns nothing."""
    method = req.get("method")
    rid = req.get("id")
    params = req.get("params") or {}

    if method == "initialize":
        proto = params.get("protocolVersion", "2024-11-05")
        _result(rid, {
            "protocolVersion": proto,
            "capabilities": {"tools": {"listChanged": False}},
            "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
        }, out)
    elif method in ("notifications/initialized", "initialized", "notifications/cancelled"):
        pass                                      # notifications: no reply
    elif method == "ping":
        _result(rid, {}, out)
    elif method == "tools/list":
        _result(rid, {"tools": TOOLS}, out)
    elif method == "tools/call":
        name = params.get("name")
        args = params.get("arguments") or {}
        fn = DISPATCH.get(name)
        if fn is None:
            _result(rid, {"content": [{"type": "text",
                     "text": f"error: unknown tool '{name}'"}],
                     "isError": True}, out)
            return
        try:
            text = fn(args)
        except Exception as exc:                  # surface as a tool error
            text = f"error: {type(exc).__name__}: {exc}"
            _result(rid, {"content": [{"type": "text", "text": text}],
                     "isError": True}, out)
            return
        _result(rid, {"content": [{"type": "text", "text": text}]}, out)
    elif rid is not None:
        _error(rid, -32601, f"method not found: {method}", out)


def main(stdin=None, stdout=None):
    stdin = stdin or sys.stdin
    stdout = stdout or sys.stdout
    for line in stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(req, list):                 # batch
            for one in req:
                handle(one, stdout)
        else:
            handle(req, stdout)


if __name__ == "__main__":
    main()
