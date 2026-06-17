# FRUA reference MCP server

A small [Model Context Protocol](https://modelcontextprotocol.io) server that
gives a coding assistant authoritative reference while lifting the Macintosh
Resource Manager / Memory Manager and FRUA's FC (file-cache) GLIB-group
subsystem. It bundles two things in one stdio server:

1. **Authored Mac Toolbox reference** (`toolbox.*`) — `reference/toolbox/*.md`:
   the Resource Manager, Memory Manager (purgeable handles), the resource-fork
   on-disk format, and FRUA's FC group cache. Committed; expand freely.
2. **Live queries over our artifacts** (`rfork.* / fc.* / jt.* / sym.* /
   disasm.*`) — the application resource fork, the FC group map, the A5 jump
   table + `docs/function-reference.md`, the lifted C in `src/engine/`, and the
   objdump listings.

The `data/` inputs (resource fork, disassembly) are **git-ignored** copyrighted
FRUA assets. The server only *reads* them at runtime; nothing under `data/` is
committed, and the `toolbox.*` / `fc.*` tools work with no game data present.

## Design

- **Dependency-free.** Pure-stdlib Python; implements newline-delimited
  JSON-RPC 2.0 over stdin/stdout (the MCP stdio transport). No `pip install`.
- **Path-independent.** Resolves the repo root from its own location, so it
  works whatever the launch cwd is.
- **Graceful when data is absent.** The asset-backed tools report a clear
  "unavailable" message instead of crashing (so CI, which has no `data/`,
  still loads the server).

## Tools

| Tool            | What it does                                                |
|-----------------|-------------------------------------------------------------|
| `toolbox_list`  | List reference topics + their sections.                     |
| `toolbox_get`   | Full text of one topic (`resource-manager`, `memory-manager`, `resource-fork-format`, `fc-group-cache`). |
| `toolbox_search`| Ranked full-text search across the corpus.                  |
| `rfork_list`    | Resource-fork summary by type, or per-resource for a type.  |
| `rfork_get`     | Hexdump one resource `(type,id)`.                           |
| `fc_groups`     | FC GLIB-group → file map + `jt468`/`l37aa` mechanics.       |
| `jt_lookup`     | JT entry → A5 address + function-reference notes + lifted C.|
| `sym_search`    | Grep the lifted C (`src/engine` + `compat`).               |
| `disasm_grep`   | Search the objdump listings under `data/work/disasm`.       |

## Use

Registered for this project in `../../.mcp.json`:

```json
{ "mcpServers": { "frua-reference": {
    "command": "python3",
    "args": ["tools/mcp/frua_mcp_server.py"] } } }
```

Claude Code picks it up automatically in this repo (approve it on first run, or
`claude mcp list` to check). To drive it by hand:

```sh
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05"}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"toolbox_search","arguments":{"query":"purgeable reload"}}}' \
  | python3 tools/mcp/frua_mcp_server.py
```

## Tests

`tests/test_frua_mcp.py` (run via `make test`) drives the handler directly with
an injected stream — no subprocess, no game data required. The one rfork-backed
test self-skips when `data/work/UnlimitedAdventures.rfork` is absent.

## Extending

- **More reference:** drop another `reference/toolbox/<topic>.md`. Use `##` /
  `###` headings — `toolbox_search` indexes per section and weights heading
  matches. No code change needed.
- **More data tools:** add a `tool_*` function + a `TOOLS` entry + a `DISPATCH`
  key in `frua_mcp_server.py`. Keep asset reads behind the `_missing_data`
  guard so the server still loads without `data/`.
