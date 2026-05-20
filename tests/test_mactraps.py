"""Tests for the A-line trap table (tools/mactraps.py)."""
import mactraps


def test_canonical_toolbox_trap():
    kind, key, _flags = mactraps.canonical(0xA9F0)
    assert kind == "Toolbox"
    assert key == 0xA9F0


def test_canonical_os_trap_strips_flag_bits():
    # _NewPtr is OS trap 0x1E; 0xA11E carries an extra flag bit.
    kind, key, _flags = mactraps.canonical(0xA11E)
    assert kind == "OS"
    assert key == 0xA01E


def test_trap_name_known():
    assert mactraps.trap_name(0xA9F0) == "_LoadSeg"
    assert mactraps.trap_name(0xA022) == "_NewHandle"
    assert mactraps.trap_name(0xA11E) == "_NewPtr"


def test_trap_name_unknown_is_still_flagged():
    name = mactraps.trap_name(0xA8FF)
    assert "trap" in name
    assert "a8ff" in name.lower()
