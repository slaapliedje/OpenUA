"""Smoke tests for the FRUA reference MCP server (tools/mcp/frua_mcp_server.py).

Drives the server's JSON-RPC handler directly with an injected output stream —
no subprocess, no MCP client package. The toolbox.* / fc.* tools work from the
committed reference corpus + docs, so these pass with no game data present; the
data-backed tools (rfork.*, jt.*) are asserted only to degrade gracefully when
data/ is absent (they read live assets, never committed).
"""
import io
import json
import os
import sys

import pytest

_MCP = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "tools", "mcp"))
if _MCP not in sys.path:
    sys.path.insert(0, _MCP)

import frua_mcp_server as srv


def call(method, params=None, rid=1):
    """Run one request through the handler, return the decoded result dict."""
    buf = io.StringIO()
    srv.handle({"jsonrpc": "2.0", "id": rid, "method": method,
                "params": params or {}}, buf)
    lines = [l for l in buf.getvalue().splitlines() if l.strip()]
    assert len(lines) == 1, f"expected 1 reply, got {len(lines)}"
    return json.loads(lines[0])


def tool(name, args=None):
    """Call a tool, return its text content."""
    res = call("tools/call", {"name": name, "arguments": args or {}})
    return res["result"]["content"][0]["text"]


def test_initialize_reports_server_info():
    res = call("initialize", {"protocolVersion": "2024-11-05"})
    info = res["result"]["serverInfo"]
    assert info["name"] == "frua-reference"
    assert res["result"]["protocolVersion"] == "2024-11-05"


def test_initialized_notification_is_silent():
    buf = io.StringIO()
    srv.handle({"jsonrpc": "2.0", "method": "notifications/initialized"}, buf)
    assert buf.getvalue() == ""          # notifications get no reply


def test_tools_list_has_all_tools():
    res = call("tools/list")
    names = {t["name"] for t in res["result"]["tools"]}
    assert {"toolbox_list", "toolbox_get", "toolbox_search", "rfork_list",
            "rfork_get", "fc_groups", "jt_lookup", "sym_search",
            "disasm_grep"} <= names
    # every tool advertises an object inputSchema
    for t in res["result"]["tools"]:
        assert t["inputSchema"]["type"] == "object"


def test_unknown_method_is_method_not_found():
    res = call("does/not/exist")
    assert res["error"]["code"] == -32601


def test_toolbox_list_includes_core_topics():
    txt = tool("toolbox_list")
    for stem in ("resource-manager", "memory-manager",
                 "resource-fork-format", "fc-group-cache"):
        assert stem in txt


def test_toolbox_get_resource_manager():
    txt = tool("toolbox_get", {"topic": "resource-manager"})
    assert "GetResource" in txt and "LoadResource" in txt
    assert "ReleaseResource" in txt


def test_toolbox_get_unknown_topic_lists_available():
    txt = tool("toolbox_get", {"topic": "nope"})
    assert "no topic" in txt and "resource-manager" in txt


def test_toolbox_search_ranks_purge_semantics():
    txt = tool("toolbox_search", {"query": "purgeable handle reload", "max": 3})
    assert "memory-manager" in txt
    # heading-weighted: the purge state machine should surface
    assert "purge" in txt.lower()


def test_fc_groups_shows_map_and_mechanics():
    txt = tool("fc_groups")
    assert "jt468" in txt
    assert "ALWAYS.CTL" in txt and "FRAME.CTL" in txt
    assert "purgeable" in txt


def test_unknown_tool_is_error_content():
    res = call("tools/call", {"name": "bogus", "arguments": {}})
    assert res["result"]["isError"] is True
    assert "unknown tool" in res["result"]["content"][0]["text"]


def test_trace_tools_registered():
    res = call("tools/list")
    names = {t["name"] for t in res["result"]["tools"]}
    assert {"trace_list", "trace_get", "trace_search"} <= names


def test_trace_list_includes_chargen_capture():
    txt = tool("trace_list")
    assert "chargen-render-jt1089" in txt


def test_trace_get_and_search_chargen():
    full = tool("trace_get", {"topic": "chargen-render-jt1089"})
    assert "PICK RACE" in full and "ready    action" in full
    # the colour-128 disabled-text ground truth is searchable
    hits = tool("trace_search", {"query": "colour 128 disabled", "max": 2})
    assert "chargen-render-jt1089" in hits


@pytest.mark.skipif(not os.path.isfile(srv.RFORK),
                    reason="game data (rfork) not present")
def test_rfork_list_reads_code_segments():
    txt = tool("rfork_list", {"type": "CODE"})
    assert "id" in txt.lower()
    # FRUA ships 23 CODE segments
    assert txt.count("\n") >= 20


def test_rfork_list_degrades_without_data(monkeypatch):
    monkeypatch.setattr(srv, "RFORK", "/nonexistent/UA.rfork")
    srv._RF_CACHE.clear()
    txt = tool("rfork_list")
    assert "unavailable" in txt
    srv._RF_CACHE.clear()
