"""Test configuration: put tools/ on the import path.

The host tools are plain scripts under tools/, not an installed package, so
the suite imports them directly. fixtures.py builds synthetic binaries, so no
real (copyrighted) game data is needed to run the tests.
"""
import os
import sys

_TOOLS = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "tools"))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)
