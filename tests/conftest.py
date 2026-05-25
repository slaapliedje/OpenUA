"""Test configuration: put tools/ on the import path.

The host tools are plain scripts under tools/, not an installed package, so
the suite imports them directly. fixtures.py builds synthetic binaries, so no
real (copyrighted) game data is needed to run the tests.
"""
import os
import sys

import pytest

_TOOLS = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "tools"))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)


def pytest_configure(config):
    """Register custom markers so unknown-marker warnings stay quiet."""
    config.addinivalue_line(
        "markers",
        "slow: long-running tests (Hatari boots, network, etc.)")


def pytest_collection_modifyitems(config, items):
    """Skip slow tests unless the user opts in via -m slow."""
    if config.getoption("-m") and "slow" in config.getoption("-m"):
        return
    skip_slow = pytest.mark.skip(reason="slow test — pass `-m slow` to run")
    for item in items:
        if "slow" in item.keywords:
            item.add_marker(skip_slow)
