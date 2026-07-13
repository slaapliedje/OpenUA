#!/usr/bin/env python3
"""Disassemble the CODE segments of a classic Mac application resource fork.

Extracts every CODE resource, parses the CODE 0 jump table, and drives
m68k-atari-mint-objdump over each segment. The raw objdump output is then
annotated: A-line Toolbox/OS traps are named, intra-segment branch targets
get labels, calls through the A5 jump table are resolved to their target
(segment, routine), and CREL-relocated absolute operands are resolved to
A5-world or string-pool references.

Per the project's copyright stance the output is written under data/ (which
is git-ignored) -- it is the original program in another notation. The
hand-lifted C in src/engine/ is the committed work.

Usage:
    dis68k.py <resource-fork> [--out DIR] [--cpu m68k:68000]

Output (default data/work/disasm/):
    CODE_NN.bin      raw extracted segment
    CODE_NN.s        annotated disassembly listing
    DATA.bin DREL.bin   the A5-world image and its relocations, for later
    jumptable.txt    the full 1208-entry A5 jump table
"""
import os
import re
import sys
import struct
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from macrsrc import ResourceFork
from mactraps import trap_name

OBJDUMP = "m68k-atari-mint-objdump"
NEAR_HEADER = 4          # every CODE segment's code begins 4 bytes in

BRANCH = re.compile(r"^(b|db|fb)[a-z]*$")          # bra/bsr/bcc/dbf/...
LINE = re.compile(r"^\s*([0-9a-f]+):\t([0-9a-f ]+?)\s*\t(.*)$")
PC_REL = re.compile(r"%pc@\(0x([0-9a-f]+)\)")
A5_DISP = re.compile(r"%a5@\((-?\d+)\)")
BARE_HEX = re.compile(r"\b0x([0-9a-f]+)\b")


def parse_jump_table(code0):
    """Return (jt_a5_offset, [(segment, entry_address), ...]).

    A jump-table routine offset is measured from the start of a segment's
    *code* -- past the 4-byte segment header -- so the resource-relative
    entry address is the stored offset plus NEAR_HEADER.
    """
    _above, _below, jt_len, jt_off = struct.unpack_from(">IIII", code0, 0)
    entries = []
    for k in range(jt_len // 8):
        ro, _push, seg, _trap = struct.unpack_from(">HHHH", code0, 16 + 8 * k)
        entries.append((seg, ro + NEAR_HEADER))
    return jt_off, entries


def parse_crel(data):
    """Parse a CREL/DREL relocation resource into {offset: is_a4}.

    The resource is a flat array of big-endian 16-bit words. Bit 0 of each
    word is the base flag (0 = the A5 world, 1 = the A4 string pool); the
    word with bit 0 cleared is an even byte offset into the segment, where a
    32-bit value is fixed up by adding the chosen base at load time.
    """
    relocs = {}
    for (word,) in struct.iter_unpack(">H", data[:len(data) & ~1]):
        relocs[word & 0xFFFE] = bool(word & 1)
    return relocs


def read_cstr(buf, off, limit=48):
    """A printable, length-capped C string from buf at off, or None."""
    if buf is None or not 0 <= off < len(buf):
        return None
    end = buf.find(b"\0", off, off + limit)
    raw = buf[off:end if end >= 0 else off + limit]
    return "".join(c if c.isprintable() else "." for c in
                   raw.decode("mac-roman", "replace"))


def objdump(path, start, cpu):
    """Disassemble a raw binary from `start`; yield (addr, bytes, mnem, ops)."""
    out = subprocess.run(
        [OBJDUMP, "-D", "-b", "binary", "-m", cpu,
         f"--start-address={start}", path],
        capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        m = LINE.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        raw = m.group(2).replace(" ", "")
        insn = m.group(3).strip()
        mnem, _, ops = insn.partition(" ")
        yield addr, raw, mnem, ops.strip()


def annotate(addr, raw, mnem, ops, seg_len, jt_off, jt, labels):
    """Return (annotated-operands, comment) for one instruction."""
    comment = ""

    # A-line trap: first opcode word in the $A000-$AFFF range.
    word = int(raw[:4], 16) if len(raw) >= 4 else 0
    if 0xA000 <= word <= 0xAFFF:
        comment = f"trap {trap_name(word)}"

    # Call/branch through the A5 jump table. A JT entry is 8 bytes; a call
    # targets entry+2 -- the MOVE.W #seg the loader leaves in place -- so the
    # displacement is jt_off + 2 + 8*index.
    m = A5_DISP.search(ops)
    if m:
        disp = int(m.group(1))
        rel = disp - jt_off - 2
        if disp < 0:
            comment = f"A5 global {disp}"
        elif rel >= 0 and rel % 8 == 0 and rel // 8 < len(jt):
            tseg, tro = jt[rel // 8]
            comment = f"-> CODE {tseg}+{tro:#x}  (JT[{rel // 8}])"

    # Intra-segment branch/call target -> label reference.
    target = None
    pc = PC_REL.search(ops)
    if pc:
        target = int(pc.group(1), 16)
    elif BRANCH.match(mnem):
        b = BARE_HEX.search(ops)
        if b:
            target = int(b.group(1), 16)
    if target is not None and NEAR_HEADER <= target < seg_len:
        labels.add(target)
        ops = ops.replace(f"0x{target:x}", f"L{target:04x}")
    return ops, comment


def decode_jt3_table(data, table_at):
    """Decode a THINK C JT[3] inline switch table at byte offset `table_at`.

    Returns (table_end, min_case, max_case, default_target, [case_targets])
    or None if the bytes don't shape up as a plausible table. The PC-rel
    offset math matches tools/jt3_extract.py — see that module's docstring
    for the layout: short min, max, default_off, case0_off, ..., caseN_off.
    """
    if table_at < 0 or table_at + 6 > len(data):
        return None
    min_c = int.from_bytes(data[table_at:table_at + 2], "big", signed=True)
    max_c = int.from_bytes(data[table_at + 2:table_at + 4], "big",
                           signed=True)
    if max_c < min_c or (max_c - min_c) > 1024:
        return None
    n_cases = max_c - min_c + 1
    cases_at = table_at + 6
    table_end = cases_at + 2 * n_cases
    if table_end > len(data):
        return None
    default_off = int.from_bytes(data[table_at + 4:table_at + 6], "big",
                                 signed=True)
    default_target = (table_at + 4) + default_off
    case_targets = []
    for k in range(n_cases):
        slot = cases_at + 2 * k
        off = int.from_bytes(data[slot:slot + 2], "big", signed=True)
        case_targets.append(slot + off)
    return (table_end, min_c, max_c, default_target, case_targets)


def is_jt3_jsr(mnem, ops, jt_off):
    """A `jsr JT[3]` site — the THINK C switch dispatcher. Its inline case
    table follows the instruction. A JT call's displacement is
    jt_off + 2 + 8*index (see annotate)."""
    if mnem != "jsr":
        return False
    m = A5_DISP.search(ops)
    return bool(m) and int(m.group(1)) == jt_off + 2 + 8 * 3


def resync_stream(data, jt_off, dis):
    """Disassemble with a RESTART after every JT[3] inline switch table.

    `dis(start)` yields (addr, raw_hex, mnem, ops) rows from byte offset
    `start` (objdump in production; a fake in tests).

    One linear objdump pass decodes each table's bytes as garbage
    instructions, and a garbage "instruction" can straddle the table's
    end and eat the first bytes of the REAL code after it — the stream
    never resyncs. CODE 3's jt433 is the canonical victim: the form-feed
    arm's `4eba fe8e` (`jsr L4854` = PrClosePage) sat exactly at its
    table's end and was listed as a stray `.short 0xfe8e`, which cost a
    mis-lift (jt433's form-feed case called L4806 with no page close).
    So: at each decoded table, drop the tail and re-disassemble from
    table_end. Table bytes never appear as rows, and the scan continues
    over the fresh tail, so tables hidden behind an earlier straddle are
    found too.

    Returns (rows, {table_addr: decode_jt3_table(...) tuple}).
    """
    insns = list(dis(NEAR_HEADER))
    tables = {}
    i = 0
    while i < len(insns):
        addr, raw, mnem, ops = insns[i]
        if is_jt3_jsr(mnem, ops, jt_off):
            table_at = addr + len(raw) // 2
            decoded = decode_jt3_table(data, table_at)
            if decoded is not None:
                tables[table_at] = decoded
                del insns[i + 1:]
                if decoded[0] < len(data):          # table_end
                    insns.extend(dis(decoded[0]))
        i += 1
    return insns, tables


def reloc_note(crel, strs, data, addr, insn_len):
    """Annotation for any CREL relocations inside one instruction's bytes."""
    notes = []
    for off in range(addr, addr + insn_len):
        if off not in crel or off + 4 > len(data):
            continue
        imm = int.from_bytes(data[off:off + 4], "big")
        if crel[off]:                       # A4 -- string pool
            s = read_cstr(strs, imm)
            notes.append(f"STRS+{imm:#x}" + (f' "{s}"' if s else ""))
        else:                               # A5 -- globals / jump table
            notes.append(f"A5+{imm:#x}")
    return "reloc " + ", ".join(notes) if notes else ""


def disassemble_segment(res, jt_off, jt, jt_owner, out_dir, cpu, crel, strs):
    sid, data = res.id, res.data
    bin_path = os.path.join(out_dir, f"CODE_{sid:02d}.bin")
    with open(bin_path, "wb") as f:
        f.write(data)

    # JT[3] inline switch tables are decoded (not disassembled) and the
    # stream restarts at each table's end — see resync_stream. Their
    # case/default targets become LXXXX labels so the arm code reads
    # cleanly.
    insns, jt3_tables = resync_stream(
        data, jt_off, lambda start: objdump(bin_path, start, cpu))
    labels = set()
    for _table_at, (_end, _min, _max, default_target,
                    case_targets) in jt3_tables.items():
        labels.add(default_target)
        for t in case_targets:
            labels.add(t)

    # routine offset -> global jump-table index, for this segment's exports.
    entries = {ro: gidx for gidx, ro in jt_owner.get(sid, [])}
    rows, traps, jt_calls = [], 0, 0
    for addr, raw, mnem, ops in insns:
        ops, comment = annotate(addr, raw, mnem, ops, len(data),
                                jt_off, jt, labels)
        rnote = reloc_note(crel, strs, data, addr, len(raw) // 2)
        if rnote:
            comment = f"{comment}  {rnote}" if comment else rnote
        if comment.startswith("trap "):
            traps += 1
        if comment.startswith("-> CODE"):
            jt_calls += 1
        rows.append((addr, raw, mnem, ops, comment))

    s_path = os.path.join(out_dir, f"CODE_{sid:02d}.s")
    with open(s_path, "w") as f:
        f.write(f"; CODE segment {sid} -- {len(data)} bytes, "
                f"{len(entries)} jump-table entry points, "
                f"{len(crel)} CREL relocations\n")
        f.write(f"; segment header (raw): {data[:4].hex()}; "
                f"code begins at +{NEAR_HEADER:#x}\n")
        f.write("; addresses are resource-relative; "
                "entry_jtN cross-references jumptable.txt\n\n")
        for addr, raw, mnem, ops, comment in rows:
            if addr in entries:
                f.write(f"\nentry_jt{entries[addr]}:"
                        f"  ; CODE {sid} jump-table export\n")
            if addr in labels:
                f.write(f"L{addr:04x}:\n")
            text = f"{mnem} {ops}".strip()
            line = f"  {addr:04x}:  {raw:<20}  {text}"
            if comment:
                line = f"{line:<58}; {comment}"
            f.write(line + "\n")
            # The table bytes never appear as rows (resync_stream skips
            # them) — surface the decode after its `jsr JT[3]`.
            table_at = addr + len(raw) // 2
            if table_at in jt3_tables:
                table_end, min_c, max_c, default_target, case_targets \
                    = jt3_tables[table_at]
                f.write(f"; JT[3] inline table @ 0x{table_at:04x}  "
                        f"min={min_c} max={max_c}  "
                        f"({table_end - table_at} bytes)\n")
                f.write(f";   default -> L{default_target:04x}\n")
                for k, t in enumerate(case_targets):
                    f.write(f";   case {min_c + k:>3} -> L{t:04x}\n")
    return sid, len(data), len(insns), traps, jt_calls, len(crel)


def main(argv):
    if not argv:
        print(__doc__)
        sys.exit(2)
    fork = argv[0]
    out_dir = "data/work/disasm"
    cpu = "m68k:68000"
    if "--out" in argv:
        out_dir = argv[argv.index("--out") + 1]
    if "--cpu" in argv:
        cpu = argv[argv.index("--cpu") + 1]
    os.makedirs(out_dir, exist_ok=True)

    rf = ResourceFork.from_file(fork)
    jt_off, jt = parse_jump_table(rf.get("CODE", 0).data)
    try:
        strs = rf.get("STRS", 0).data        # the A4 string pool
    except KeyError:
        strs = None

    # Which jump-table entries each segment exports (its routine entry points).
    jt_owner = {}
    for idx, (seg, ro) in enumerate(jt):
        jt_owner.setdefault(seg, []).append((idx, ro))

    with open(os.path.join(out_dir, "jumptable.txt"), "w") as f:
        f.write(f"A5 jump table: {len(jt)} entries, base A5+{jt_off:#x}\n\n")
        for idx, (seg, ro) in enumerate(jt):
            f.write(f"  JT[{idx:4}]  A5+{jt_off + 8 * idx:#06x}  "
                    f"CODE {seg:2}+{ro:#06x}\n")

    # The A5-world image and its relocations, kept for later reconstruction.
    for rtype in ("DATA", "DREL"):
        for r in rf.of_type(rtype):
            with open(os.path.join(out_dir, f"{rtype}.bin"), "wb") as f:
                f.write(r.data)

    print(f"{'seg':>4} {'bytes':>8} {'insns':>8} {'traps':>7} "
          f"{'JT calls':>9} {'relocs':>7}")
    for res in rf.of_type("CODE"):
        if res.id == 0:
            continue
        try:
            crel = parse_crel(rf.get("CREL", res.id).data)
        except KeyError:
            crel = {}
        sid, n, ins, tr, jc, rl = disassemble_segment(
            res, jt_off, jt, jt_owner, out_dir, cpu, crel, strs)
        print(f"{sid:>4} {n:>8} {ins:>8} {tr:>7} {jc:>9} {rl:>7}")
    print(f"\nlistings in {out_dir}/CODE_NN.s")


if __name__ == "__main__":
    main(sys.argv[1:])
