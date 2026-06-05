"""Decode the inline JT[1] table that follows every `jsr JT[1]` in
the FRUA Mac build.

JT[1] is THINK C's *sparse* switch runtime — used when the case
labels are not a contiguous range (so a dense JT[3] jump table would
waste space). Every such C `switch` compiles to:

    jsr   JT[1]
    .short count             ; number of case entries (N)
    .short off0  .short val0  ; case 0: value val0, PC-rel offset off0
    .short off1  .short val1  ; case 1
    ...
    .short off(N-1) .short val(N-1)
    .short off_default       ; trailing default offset (no value word)

JT[1]'s body (CODE 1 + 0x130):

    moveal sp@+, a0          ; a0 = inline table (return PC)
    movew  a0@+, d1          ; d1 = count
  L: movew  a0@+, d2          ; d2 = this entry's offset
    cmpw   a0@+, d0          ; compare this entry's value with the selector
    dbeq   d1, L             ; loop until equal or d1 exhausted
    tstw   d2
    beqs   .                 ; (no-default trap)
    jmp    a0@(-4, d2:w)     ; jump to the matched / default arm

Because `dbeq` runs N+1 iterations, the (N+1)-th entry's offset slot
is the default arm; its "value" word is whatever code byte follows
(never a real match). At the jmp, a0 has advanced past the matched
pair, so for case k (matched on iteration k):

    a0      = table_at + 2 + 4*(k+1)
    target  = a0 - 4 + offset_k = table_at + 2 + 4*k + offset_k

and the default (iteration N) resolves to
`table_at + 2 + 4*N + off_default`. Offsets are signed (a case can
jump backwards), so sign-extend the 16-bit slot.

This tool reads a CODE segment .bin (as produced by
`tools/dis68k.py`) plus the byte offset of the `jsr JT[1]` (or the
table directly) and emits the resolved targets plus a C `switch`
skeleton ready to drop into the lift.

Usage:
    jt1_extract.py CODE_02.bin --jsr-at 0x42b2     # table is JSR+4
    jt1_extract.py CODE_02.bin --table-at 0x42b6
"""

import argparse
import struct
import sys
from dataclasses import dataclass
from typing import List, Sequence, Tuple


@dataclass(frozen=True)
class Jt1Table:
    """A decoded JT[1] sparse-switch inline table.

    `address` is the byte offset of the table's first word (the
    `count` short) inside the CODE segment's .bin. `cases` is a list
    of (value, target) pairs in table order; `default_target` is the
    arm taken when no value matches.
    """
    address:        int
    count:          int
    cases:          Sequence[Tuple[int, int]]
    default_target: int

    @property
    def table_size(self) -> int:
        """Total bytes the table occupies: count + N pairs + default."""
        return 2 + 4 * self.count + 2


def _s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def parse_table(blob: bytes, table_at: int) -> Jt1Table:
    """Decode a JT[1] inline table.

    `blob` is the raw CODE segment bytes; `table_at` is the byte
    offset of the table's first word (the `count` short). Raises
    ValueError if the table runs past the end of the blob or the
    count is implausible.
    """
    if table_at < 0 or table_at + 2 > len(blob):
        raise ValueError(
            f"table at 0x{table_at:x} doesn't fit in {len(blob)}-byte blob")

    count = struct.unpack_from(">H", blob, table_at)[0]
    if count > 1024:
        raise ValueError(
            f"implausible case count {count} at 0x{table_at:x}; "
            f"wrong table address?")

    end = table_at + 2 + 4 * count + 2
    if end > len(blob):
        raise ValueError(
            f"table at 0x{table_at:x} runs past end of {len(blob)}-byte blob")

    cases: List[Tuple[int, int]] = []
    for k in range(count):
        off = _s16(struct.unpack_from(">H", blob, table_at + 2 + 4 * k)[0])
        val = struct.unpack_from(">h", blob, table_at + 4 + 4 * k)[0]
        target = table_at + 2 + 4 * k + off
        cases.append((val, target))

    default_off = _s16(
        struct.unpack_from(">H", blob, table_at + 2 + 4 * count)[0])
    default_target = table_at + 2 + 4 * count + default_off

    return Jt1Table(
        address        = table_at,
        count          = count,
        cases          = cases,
        default_target = default_target,
    )


def emit_c_switch(table: Jt1Table, indent: str = "\t") -> str:
    """Render a C `switch` skeleton for `table`."""
    lines = [
        f"/* JT[1] sparse table at 0x{table.address:x} "
        f"({table.count} cases). */",
        "switch (value) {",
    ]
    for value, target in table.cases:
        lines.append(f"case {value}:  /* arm at 0x{target:x} */")
        lines.append(f"{indent}break;")
    lines.append(f"default: /* arm at 0x{table.default_target:x} */")
    lines.append(f"{indent}break;")
    lines.append("}")
    return "\n".join(indent + line for line in lines)


def _parse_address(text: str) -> int:
    return int(text.strip(), 16)


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Decode a JT[1] sparse-switch inline table.")
    parser.add_argument("code_bin",
                        help="CODE segment .bin (e.g. data/work/disasm/CODE_02.bin)")
    addr = parser.add_mutually_exclusive_group(required=True)
    addr.add_argument("--table-at", type=_parse_address,
                      help="hex byte offset of the table's first word (count)")
    addr.add_argument("--jsr-at", type=_parse_address,
                      help="hex byte offset of the `jsr JT[1]` "
                           "(4-byte instruction; table is JSR+4)")
    parser.add_argument("--indent", default="\t",
                        help="indentation for the emitted C switch")
    args = parser.parse_args(argv)

    with open(args.code_bin, "rb") as f:
        blob = f.read()

    table_at = (args.table_at if args.table_at is not None
                else args.jsr_at + 4)
    table = parse_table(blob, table_at)

    print(f"JT[1] table at 0x{table.address:x}:")
    print(f"  count   = {table.count}")
    for value, target in table.cases:
        print(f"  case {value:>4} = 0x{target:x}")
    print(f"  default = 0x{table.default_target:x}")
    print(f"  table_size = {table.table_size} bytes")
    print()
    print(emit_c_switch(table, args.indent))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
