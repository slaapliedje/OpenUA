"""Decode the inline JT[3] table that follows every `jsr JT[3]` in
the FRUA Mac build.

JT[3] is THINK C's switch runtime. Every C `switch` statement
compiles to:

    jsr   JT[3]
    .short min               ; lowest case label
    .short max               ; highest case label
    .short default_offset    ; PC-rel offset to the default arm
    .short case0_offset      ; PC-rel offset to case `min`
    .short case1_offset      ; PC-rel offset to case `min+1`
    ...
    .short caseN_offset      ; PC-rel offset to case `max`

JT[3] pops the return PC, indexes the table, and JMPs to the
chosen arm. Each call site has a unique table — there's no shared
dispatch logic to lift.

This tool reads a CODE segment .bin file (as produced by
`tools/dis68k.py`) plus the byte offset of the table's first
word, and emits the resolved targets along with a C switch
skeleton ready to drop into the lift.

Each `case_offset` is PC-relative to the slot that holds it: arm
N's destination is `(table_address + 6 + 2*N) + case_offset[N]`.
The default offset is PC-relative to its own slot too —
`(table_address + 4) + default_offset`.

Usage:
    jt3_extract.py CODE_12.bin --table-at 0x12ea
    jt3_extract.py CODE_12.bin --jsr-at 0x12e6      # table is JSR+4

`jsr %a5@(disp16)` is a 4-byte instruction; the inline table
starts immediately after.
"""

import argparse
import os
import struct
import sys
from dataclasses import dataclass
from typing import List, Sequence


@dataclass(frozen=True)
class Jt3Table:
    """A decoded JT[3] inline table.

    `address` is the byte offset of the first table word inside the
    CODE segment's .bin. `case_targets[i]` is the resolved address
    for case (min + i); `default_target` is the resolved address for
    out-of-range inputs.
    """
    address:        int
    min_case:       int
    max_case:       int
    default_target: int
    case_targets:   Sequence[int]

    @property
    def n_cases(self) -> int:
        return self.max_case - self.min_case + 1

    @property
    def table_size(self) -> int:
        """Total bytes the table occupies (min/max/default + cases)."""
        return 2 * (3 + self.n_cases)


def parse_table(blob: bytes, table_at: int) -> Jt3Table:
    """Decode a JT[3] inline table.

    `blob` is the raw CODE segment bytes; `table_at` is the byte
    offset of the table's first word (the `min` short). Raises
    ValueError if the table runs past the end of the blob or
    contains contradictory bounds.
    """
    if table_at < 0 or table_at + 6 > len(blob):
        raise ValueError(
            f"table at 0x{table_at:x} doesn't fit in {len(blob)}-byte blob")

    min_case, max_case, default_off = struct.unpack_from(
        ">3h", blob, table_at)
    if max_case < min_case:
        raise ValueError(
            f"bogus table at 0x{table_at:x}: max ({max_case}) < min ({min_case})")

    n_cases = max_case - min_case + 1
    if n_cases > 1024:
        raise ValueError(
            f"implausible case count {n_cases} at 0x{table_at:x}; "
            f"wrong table address?")

    cases_at = table_at + 6
    if cases_at + 2 * n_cases > len(blob):
        raise ValueError(
            f"table at 0x{table_at:x} runs past end of {len(blob)}-byte blob")

    default_target = (table_at + 4) + default_off
    case_offsets   = struct.unpack_from(f">{n_cases}h", blob, cases_at)
    case_targets   = [cases_at + 2 * i + off
                      for i, off in enumerate(case_offsets)]

    return Jt3Table(
        address        = table_at,
        min_case       = min_case,
        max_case       = max_case,
        default_target = default_target,
        case_targets   = case_targets,
    )


def emit_c_switch(table: Jt3Table, indent: str = "\t") -> str:
    """Render a C `switch` skeleton for `table`.

    The skeleton is meant to be edited into a lift — each case body
    is a placeholder comment pointing at the asm address.
    """
    lines = [
        f"/* JT[3] table at 0x{table.address:x} "
        f"(min={table.min_case}, max={table.max_case}). */",
        "switch (value) {",
    ]
    for i, target in enumerate(table.case_targets):
        case_label = table.min_case + i
        lines.append(f"case {case_label}:  "
                     f"/* arm at 0x{target:x} */")
        lines.append(f"{indent}break;")
    lines.append(f"default: /* arm at 0x{table.default_target:x} */")
    lines.append(f"{indent}break;")
    lines.append("}")
    return "\n".join(indent + line for line in lines)


def _parse_address(text: str) -> int:
    text = text.strip()
    base = 16 if text.startswith(("0x", "0X")) else 16
    # Always hex — these are CODE offsets.
    return int(text, base)


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Decode a JT[3] inline switch table.")
    parser.add_argument("code_bin",
                        help="CODE segment .bin (e.g. data/work/disasm/CODE_12.bin)")
    addr = parser.add_mutually_exclusive_group(required=True)
    addr.add_argument("--table-at", type=_parse_address,
                      help="hex byte offset of the table's first word")
    addr.add_argument("--jsr-at", type=_parse_address,
                      help="hex byte offset of the `jsr JT[3]` "
                           "(4-byte instruction; table is JSR+4)")
    parser.add_argument("--indent", default="\t",
                        help="indentation for the emitted C switch")
    args = parser.parse_args(argv)

    with open(args.code_bin, "rb") as f:
        blob = f.read()

    table_at = (args.table_at if args.table_at is not None
                else args.jsr_at + 4)
    table = parse_table(blob, table_at)

    print(f"JT[3] table at 0x{table.address:x}:")
    print(f"  min     = {table.min_case}")
    print(f"  max     = {table.max_case}")
    print(f"  default = 0x{table.default_target:x}")
    for i, target in enumerate(table.case_targets):
        print(f"  case {table.min_case + i:>3} = 0x{target:x}")
    print(f"  table_size = {table.table_size} bytes")
    print()
    print(emit_c_switch(table, args.indent))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
