"""Parse a THINK C DATA + DREL resource pair.

The THINK C runtime loads the `DATA` resource into the negative-A5
half of the A5 world (the data + bss segment), then walks the `DREL`
resource applying pointer relocations: each 16-bit signed entry in
DREL is an A5-relative offset to a long in DATA that holds a value
"reference to <something>"; the runtime adds the A5 base address (and
relocations into STRS / CODE segments resolve through associated
CREL entries) to produce a working absolute pointer at run time.

For the Atari port we replay the same flow without a hardware A5
register: the lift allocates a single byte buffer for the A5-below
region (sized by the max negative offset DREL touches), copies DATA
into the appropriate slice, then applies relocations by either:

  - adding the A5 base address (for "self-relative" relocations that
    point within the A5 world), or
  - leaving them for CREL to patch with absolute STRS / CODE
    addresses that the loader knows once the resources are mapped.

This module is a pure parser — it produces a structured description
of DATA and DREL without trying to apply them. Subsequent commits
wire the application into the engine bring-up sequence.
"""

import struct
from dataclasses import dataclass
from typing import Iterable, Sequence


@dataclass(frozen=True)
class DataPool:
    """The parsed DATA resource contents.

    `bytes` is the raw initialised data; positions are A5-relative
    offsets (i.e. negative integers ending at A5 = -1 just past the
    last byte). `a5_below_size` is the inferred size of the A5-below
    region — it's the larger of `len(bytes)` and the deepest DREL
    offset's magnitude, so the engine can size its A5 buffer in one
    shot.
    """
    bytes: bytes
    a5_below_size: int


RELOC_BASE_A5 = "A5"      # negative-A5 globals
RELOC_BASE_A4 = "A4"      # the STRS string pool


@dataclass(frozen=True)
class RelocEntry:
    """A single DREL relocation entry.

    The DREL resource encodes each entry as a 16-bit word: bit 0 is
    the base selector (0 = A5, 1 = A4 / STRS pool) and bits 15..1 are
    an even signed offset from that base. The slot at base + offset is
    a long whose initial value gets the base address added at load —
    i.e. it stores an A5- or A4-relative offset that the load step
    converts to an absolute pointer.
    """
    a5_offset: int          # signed byte offset (always even)
    base: str               # RELOC_BASE_A5 or RELOC_BASE_A4


@dataclass(frozen=True)
class RelocTable:
    """The parsed DREL resource — a list of `RelocEntry` in file order."""
    entries: Sequence[RelocEntry]

    def offsets(self) -> Iterable[int]:
        return (e.a5_offset for e in self.entries)

    def a5_min(self) -> int:
        """Most-negative A5 offset across the A5-base entries, or 0
        when no A5-base entries exist."""
        a5s = [e.a5_offset for e in self.entries if e.base == RELOC_BASE_A5]
        return min(a5s) if a5s else 0


def parse_data(data_bytes: bytes, drel_min_a5_offset: int = 0) -> DataPool:
    """Wrap raw DATA bytes in a DataPool, inferring the A5-below size.

    If `drel_min_a5_offset` is supplied (e.g. from the DREL parse), the
    A5-below size grows to whichever is larger — `len(data_bytes)` or
    `-drel_min_a5_offset` — so the application buffer covers every
    address DREL references.
    """
    below = max(len(data_bytes), -drel_min_a5_offset)
    return DataPool(bytes=data_bytes, a5_below_size=below)


def _split_reloc_word(word: int) -> RelocEntry:
    """Decode one DREL word into (base, offset)."""
    base = RELOC_BASE_A4 if (word & 1) else RELOC_BASE_A5
    # Mask off the base-select bit, then re-interpret as signed 16.
    raw_offset = word & ~1 & 0xFFFF
    if raw_offset & 0x8000:
        raw_offset -= 0x10000
    return RelocEntry(a5_offset=raw_offset, base=base)


def parse_drel(drel_bytes: bytes) -> RelocTable:
    """Parse the DREL resource: a flat array of big-endian shorts."""
    if len(drel_bytes) % 2 != 0:
        raise ValueError(
            "DREL resource size %d is not a multiple of 2" % len(drel_bytes)
        )
    n = len(drel_bytes) // 2
    raw_shorts = struct.unpack(f">{n}H", drel_bytes)
    return RelocTable(entries=tuple(_split_reloc_word(w) for w in raw_shorts))


def data_offset_for(a5_offset: int, pool: DataPool) -> int:
    """Convert an A5-relative offset into a 0-based index into DataPool.bytes.

    Returns -1 if the offset falls outside the initialised data range
    (i.e. it's in the BSS portion of the A5-below region — patched at
    runtime but starts as zeros).
    """
    if a5_offset > 0:
        return -1
    # A5 sits just past the last byte of DATA. byte index = DATA size + offset.
    idx = len(pool.bytes) + a5_offset
    if 0 <= idx <= len(pool.bytes) - 4:
        return idx
    return -1


def read_initial_long(a5_offset: int, pool: DataPool) -> int | None:
    """Read the 4-byte initial value at the given A5 offset.

    Returns None when the offset is in BSS (initial value = 0) or
    outside the data range.
    """
    idx = data_offset_for(a5_offset, pool)
    if idx < 0:
        return None
    return struct.unpack_from(">I", pool.bytes, idx)[0]
