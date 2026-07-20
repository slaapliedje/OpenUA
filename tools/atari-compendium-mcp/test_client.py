#!/usr/bin/env python3
"""Smoke test for the Atari Compendium MCP server.

Spawns `server.py` over stdio using the MCP SDK client, lists the tools,
and calls search()/get_page(), asserting non-empty, plausible results.

Run:  .venv/bin/python test_client.py
"""

import asyncio
import os
import sys

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


def _text(result):
    """Flatten a CallToolResult's content blocks to a single string."""
    parts = []
    for c in result.content:
        parts.append(getattr(c, "text", "") or "")
    return "\n".join(parts)


async def main():
    here = os.path.dirname(os.path.abspath(__file__))
    server_py = os.path.join(here, "server.py")
    params = StdioServerParameters(command=sys.executable, args=[server_py], env=os.environ.copy())

    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()

            tools = await session.list_tools()
            names = sorted(t.name for t in tools.tools)
            print("Tools:", names)
            assert set(["search", "get_page", "table_of_contents"]) <= set(names), names

            # --- search("Blitmode") ---
            r = await session.call_tool("search", {"query": "Blitmode"})
            txt = _text(r)
            print("\n--- search('Blitmode') ---\n" + txt[:600])
            assert "page" in txt.lower()
            low = txt.lower()
            assert "blitter" in low or "64" in txt, "Blitmode hit should mention blitter / XBIOS 64"

            # --- search("vro_cpyfm") ---
            r = await session.call_tool("search", {"query": "vro_cpyfm", "max_results": 3})
            txt = _text(r)
            print("\n--- search('vro_cpyfm') ---\n" + txt[:600])
            assert "vro_cpyfm" in txt.lower()
            assert "MFDB" in txt or "blit" in txt.lower()

            # --- get_page (use the top Blitmode page, 207) ---
            r = await session.call_tool("get_page", {"page": 207})
            txt = _text(r)
            print("\n--- get_page(207) [first 400 chars] ---\n" + txt[:400])
            assert "PAGE 207" in txt
            assert len(txt) > 200

            # --- table_of_contents ---
            r = await session.call_tool("table_of_contents", {})
            txt = _text(r)
            print("\n--- table_of_contents() [first 400 chars] ---\n" + txt[:400])
            assert "TOC source" in txt

            print("\nALL ASSERTIONS PASSED")


if __name__ == "__main__":
    asyncio.run(main())
