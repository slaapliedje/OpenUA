"""src/engine/ecl.c — the C interpreter core, built and run on the host.

Two kinds of check:

  * FRAMING, differentially. tools/ecl.py is the reference (its own framing was
    proven on the three Krynn games' files). Every instruction the Python walk
    reaches must decode to the same size, opcode and operand count in C — on
    scripts assembled here always, and on the real games' scripts when they are
    present under data/ or a Steam install.

  * SEMANTICS, on scripts assembled here: the rules that are easy to get wrong
    (the destination is the third operand, SUBTRACT is second-minus-first,
    COMPARE_AND clears the ordering flags, AND/OR set the flags, IF_* guards
    exactly one instruction, ON_GOTO is zero-based).

The host binary is built on the fly with the host compiler.
"""
import glob
import os
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))

import ecl                                                    # noqa: E402
from ecl import GEN1, GEN1_DOK, GEN2, Block                   # noqa: E402
from test_ecl import block, imm, mem, word, text, vaddr       # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "src", "engine", "ecl.c"),
       os.path.join(ROOT, "tests", "ecl_host.c")]


@pytest.fixture(scope="module")
def cbin(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("ecl_c") / "ecl_host")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-o", exe] + SRC, check=True)
    return exe


def c_decode(cbin, tmp_path, raw, dialect, offsets):
    p = tmp_path / "blk.bin"
    p.write_bytes(raw)
    r = subprocess.run([cbin, "decode", dialect.name, str(p)],
                       input="\n".join(str(o) for o in offsets) + "\n",
                       capture_output=True, text=True, check=True)
    return [tuple(int(x) for x in line.split()) for line in r.stdout.splitlines()]


def c_run(cbin, tmp_path, code, dialect=GEN2, entry=4, budget=10000, pokes=(),
          entries=None):
    p = tmp_path / "blk.bin"
    p.write_bytes(block(code, dialect, entries))
    r = subprocess.run([cbin, "run", dialect.name, str(p), str(entry), str(budget)]
                       + ["%d=%d" % kv for kv in pokes],
                       capture_output=True, text=True, check=True)
    host, mem_, status, pauses = [], {}, None, []
    for line in r.stdout.splitlines():
        f = line.split()
        if f[0] == "HOST":
            texts = line.split(" | ")[1:]
            nums = line.split(" | ")[0].split()
            host.append((int(nums[1]), [int(x) for x in nums[3:]], texts))
        elif f[0] == "MEM":
            mem_[int(f[1])] = int(f[2])
        elif f[0] == "PAUSE":
            pauses.append(int(f[1]))
        elif f[0] == "STATUS":
            status = dict(kv.split("=") for kv in f[2:]) if len(f) > 2 else {}
            status["status"] = f[1]
    return host, mem_, status, pauses


EXITED, CHAIN, WAIT, COMBAT, FAULT, BUDGET = "0", "1", "2", "3", "4", "5"


# ---- framing: C against the Python reference ------------------------------

def assert_same_framing(cbin, tmp_path, blk):
    seen, halts = blk.walk()
    offs = sorted(seen)
    got = c_decode(cbin, tmp_path, blk.raw, blk.dialect, offs + halts)
    for off, row in zip(offs, got):
        ins = seen[off]
        assert row == (off, ins.size, ins.opcode, len(ins.args)), (
            "block %s offset %d (%s)" % (blk.block_id, off, ins.mnemonic))
    for off, row in zip(halts, got[len(offs):]):
        assert row[1] == 0, "C decoded what Python could not, at %d" % off
    return len(offs)


def test_every_opcode_frames_the_same_in_c_and_python(cbin, tmp_path):
    """One instruction of every opcode in every dialect, operands padded out
    with a mix of sizes, each followed by an EXIT the decode must land on."""
    for d in (GEN1, GEN1_DOK, GEN2):
        for op, (_, fixed, count_at) in sorted(d.opcodes.items()):
            body = bytes([op])
            for i in range(fixed):
                body += imm(2) if i == count_at else (mem(0x1234), imm(9),
                                                      word(0x4321))[i % 3]
            if count_at is not None:
                body += text("ONE") + mem(0x8040)
            raw = block(body + b"\x00", d)
            (row,) = c_decode(cbin, tmp_path, raw, d, [d.header_len])
            ref = ecl.decode_at(raw, d.header_len, d)
            assert row == (d.header_len, ref.size, op, len(ref.args)), (d.name, hex(op))
            assert raw[d.header_len + row[1]] == 0x00


def test_c_rejects_what_python_rejects(cbin, tmp_path):
    raw = block(bytes([0x7B, 0x42, 0x03]) + imm(1), GEN2)   # not opcodes; truncated
    rows = c_decode(cbin, tmp_path, raw, GEN2, [20, 21, 22, 99])
    assert [r[1] for r in rows] == [0, 0, 0, 0]


def _real_script_files():
    steam = os.path.expanduser("~/.local/share/Steam/steamapps/common")
    pats = [os.path.join(ROOT, "data", "**", "ECL.GLB"),
            os.path.join(ROOT, "data", "**", "ECL[123].DAX"),
            os.path.join(steam, "Dungeons & Dragons Krynn Series", "**", "ECL.GLB"),
            os.path.join(steam, "Dungeons & Dragons Krynn Series", "**", "ECL[123].DAX")]
    out = []
    for p in pats:
        out += glob.glob(p, recursive=True)
    return sorted(set(out))


def test_real_scripts_frame_the_same_in_c_and_python(cbin, tmp_path):
    """The whole of every Krynn script the machine has, instruction by
    instruction. Skipped where there is no game data (CI)."""
    total = 0
    for path in _real_script_files():
        with open(path, "rb") as f:
            data = f.read()
        if data[:4] == b"GLIB":
            continue                    # the ST Curse library: not a Krynn dialect
        try:
            blocks = ecl.load_blocks(data)
        except ecl.EclError:
            continue
        for blk in blocks:
            total += assert_same_framing(cbin, tmp_path, blk)
    if total == 0:
        pytest.skip("no Krynn script files on this machine")
    assert total > 1000


# ---- semantics -------------------------------------------------------------

def test_save_and_arithmetic_destination_is_the_last_operand(cbin, tmp_path):
    code = (bytes([0x09]) + imm(7) + mem(0x10)                # [0x10] = 7
            + bytes([0x04]) + imm(5) + mem(0x10) + mem(0x11)  # [0x11] = 5 + 7
            + bytes([0x05]) + imm(1) + mem(0x11) + mem(0x12)  # [0x12] = 12 - 1
            + bytes([0x07]) + imm(3) + mem(0x12) + mem(0x13)  # [0x13] = 3 * 11
            + bytes([0x06]) + mem(0x13) + imm(5) + mem(0x14)  # [0x14] = 33 / 5
            + b"\x00")
    _, m, st, _ = c_run(cbin, tmp_path, code)
    assert st["status"] == EXITED
    assert (m[0x10], m[0x11], m[0x12], m[0x13], m[0x14]) == (7, 12, 11, 33, 6)
    assert m[0x29] == 3                                       # Gen2's remainder cell


def test_divide_remainder_lands_where_the_dialect_says(cbin, tmp_path):
    code = bytes([0x06]) + imm(17) + imm(5) + mem(0x14) + b"\x00"
    _, m, _, _ = c_run(cbin, tmp_path, code, dialect=GEN1)
    assert m[0x14] == 3 and m[0x7F3F] == 2


def test_divide_by_zero_faults(cbin, tmp_path):
    code = bytes([0x06]) + imm(17) + imm(0) + mem(0x14) + b"\x00"
    _, m, st, _ = c_run(cbin, tmp_path, code)
    assert st["status"] == FAULT and st["fault"] == "20" and 0x14 not in m


def test_subtract_wraps_to_sixteen_bits(cbin, tmp_path):
    code = bytes([0x05]) + imm(1) + imm(0) + mem(0x10) + b"\x00"   # 0 - 1
    _, m, _, _ = c_run(cbin, tmp_path, code)
    assert m[0x10] == 0xFFFF


def branch(cond_code, if_op, dialect=GEN2):
    """<cond> ; IF ; SAVE #1,[0x50] ; SAVE #1,[0x51] ; EXIT — [0x50] is set only
    when the IF passes, [0x51] always."""
    return (cond_code + bytes([if_op])
            + bytes([0x09]) + imm(1) + mem(0x50)
            + bytes([0x09]) + imm(1) + mem(0x51) + b"\x00")


@pytest.mark.parametrize("left,right,passing", [
    (3, 3, {0x16, 0x1a, 0x1b}),          # ==  <=  >=
    (2, 3, {0x17, 0x18, 0x1a}),          # !=  <   <=
    (4, 3, {0x17, 0x19, 0x1b}),          # !=  >   >=
])
def test_compare_sets_all_six_flags(cbin, tmp_path, left, right, passing):
    for if_op in range(0x16, 0x1c):
        cond = bytes([0x03]) + imm(left) + imm(right)
        _, m, _, _ = c_run(cbin, tmp_path, branch(cond, if_op))
        assert (0x50 in m) == (if_op in passing), hex(if_op)
        assert m[0x51] == 1                  # the IF guarded only one instruction


def test_compare_and_sets_only_equal_and_not_equal(cbin, tmp_path):
    both = bytes([0x14]) + imm(1) + imm(1) + imm(2) + imm(2)
    one = bytes([0x14]) + imm(1) + imm(1) + imm(2) + imm(3)
    assert 0x50 in c_run(cbin, tmp_path, branch(both, 0x16))[1]
    assert 0x50 not in c_run(cbin, tmp_path, branch(both, 0x17))[1]
    assert 0x50 in c_run(cbin, tmp_path, branch(one, 0x17))[1]
    # the trap: after COMPARE_AND, <= and >= are FALSE even when both pairs match
    for if_op in (0x18, 0x19, 0x1a, 0x1b):
        assert 0x50 not in c_run(cbin, tmp_path, branch(both, if_op))[1], hex(if_op)


def test_and_sets_the_flags_against_zero(cbin, tmp_path):
    hit = bytes([0x2f]) + imm(0x04) + imm(0x0c) + mem(0x20)        # 4 & 12 = 4
    miss = bytes([0x2f]) + imm(0x04) + imm(0x03) + mem(0x20)       # 4 & 3  = 0
    assert 0x50 in c_run(cbin, tmp_path, branch(hit, 0x17))[1]     # IF_NE: bit set
    assert 0x50 not in c_run(cbin, tmp_path, branch(miss, 0x17))[1]
    assert c_run(cbin, tmp_path, branch(hit, 0x17))[1][0x20] == 4


def test_a_skipped_instruction_with_inline_text_is_stepped_over_whole(cbin, tmp_path):
    code = (bytes([0x03]) + imm(1) + imm(2) + bytes([0x16])        # false
            + bytes([0x12]) + text("NOT SHOWN, AND LONG ENOUGH TO MATTER")
            + bytes([0x09]) + imm(1) + mem(0x51) + b"\x00")
    host, m, st, _ = c_run(cbin, tmp_path, code)
    assert st["status"] == EXITED and host == [] and m[0x51] == 1


def test_gosub_and_return(cbin, tmp_path):
    # GOSUB sub ; SAVE #2,[0x61] ; EXIT ; sub: SAVE #1,[0x60] ; RETURN
    main = bytes([0x02]) + mem(vaddr(GEN2, 4 + 6 + 1)) + bytes([0x09]) + imm(2) + mem(0x61) + b"\x00"
    sub = bytes([0x09]) + imm(1) + mem(0x60) + bytes([0x13])
    _, m, st, _ = c_run(cbin, tmp_path, main + sub)
    assert st["status"] == EXITED and (m[0x60], m[0x61]) == (1, 2)


def test_return_with_an_empty_stack_exits(cbin, tmp_path):
    _, _, st, _ = c_run(cbin, tmp_path, bytes([0x13]))
    assert st["status"] == EXITED


def test_runaway_recursion_faults_instead_of_overflowing(cbin, tmp_path):
    code = bytes([0x02]) + mem(vaddr(GEN2, 0))                    # GOSUB itself
    _, _, st, _ = c_run(cbin, tmp_path, code)
    assert st["status"] == FAULT


def test_an_endless_loop_spends_its_budget(cbin, tmp_path):
    code = bytes([0x01]) + mem(vaddr(GEN2, 0))                    # GOTO itself
    _, _, st, _ = c_run(cbin, tmp_path, code, budget=500)
    assert st["status"] == BUDGET and st["steps"] == "500"


@pytest.mark.parametrize("selector,expect", [(0, 0x70), (1, 0x71), (2, None), (9, None)])
def test_on_goto_is_zero_based_and_falls_through_out_of_range(cbin, tmp_path,
                                                             selector, expect):
    # ON_GOTO [0x40], #2, t0, t1 ; SAVE #1,[0x72] ; EXIT
    # t0: SAVE #1,[0x70] ; EXIT     t1: SAVE #1,[0x71] ; EXIT
    head = bytes([0x25]) + mem(0x40) + imm(2)
    fall = bytes([0x09]) + imm(1) + mem(0x72) + b"\x00"
    t0 = len(head) + 6 + len(fall)
    arm = lambda a: bytes([0x09]) + imm(1) + mem(a) + b"\x00"     # noqa: E731
    code = (head + mem(vaddr(GEN2, t0)) + mem(vaddr(GEN2, t0 + 7)) + fall
            + arm(0x70) + arm(0x71))
    _, m, st, _ = c_run(cbin, tmp_path, code, pokes=[(0x40, selector)])
    assert st["status"] == EXITED
    hit = [a for a in (0x70, 0x71, 0x72) if a in m]
    assert hit == ([expect] if expect else [0x72])


def test_on_gosub_returns_to_the_instruction_after_the_table(cbin, tmp_path):
    head = bytes([0x26]) + mem(0x40) + imm(1)
    fall = bytes([0x09]) + imm(1) + mem(0x72) + b"\x00"
    sub = bytes([0x09]) + imm(1) + mem(0x70) + bytes([0x13])
    code = head + mem(vaddr(GEN2, len(head) + 3 + len(fall))) + fall + sub
    _, m, _, _ = c_run(cbin, tmp_path, code, pokes=[(0x40, 0)])
    assert m[0x70] == 1 and m[0x72] == 1


def test_random_stays_in_range_and_zero_range_is_safe(cbin, tmp_path):
    code = b"".join(bytes([0x08]) + imm(6) + mem(0x80 + i) for i in range(40))
    code += bytes([0x08]) + imm(0) + mem(0xF0) + b"\x00"
    _, m, st, _ = c_run(cbin, tmp_path, code)
    assert st["status"] == EXITED
    rolls = [m.get(0x80 + i, 0) for i in range(40)]
    assert max(rolls) <= 5 and len(set(rolls)) > 2


def test_save_table_and_gettable_index_script_memory(cbin, tmp_path):
    code = (bytes([0x09]) + imm(3) + mem(0x40)                          # index = 3
            + bytes([0x35]) + imm(77) + mem(0x0200) + mem(0x40)         # [0x200+3] = 77
            + bytes([0x2a]) + mem(0x0200) + mem(0x40) + mem(0x50)       # [0x50] = table[3]
            + bytes([0x2a]) + mem(0x0200) + imm(4) + mem(0x51)          # unset -> 0
            + b"\x00")
    host, m, st, _ = c_run(cbin, tmp_path, code)
    assert st["status"] == EXITED and host == [] and st["unhandled"] == "0"
    assert m[0x0203] == 77 and m[0x50] == 77 and 0x51 not in m


def test_gettable_reads_constant_data_out_of_the_block(cbin, tmp_path):
    # GETTABLE table, #2, [0x50] ; EXIT ; table: 11 22 33 44
    head = bytes([0x2a]) + mem(vaddr(GEN2, 10)) + imm(2) + mem(0x50) + b"\x00"
    assert len(head) == 10
    code = head + bytes([11, 22, 33, 44])
    _, m, st, _ = c_run(cbin, tmp_path, code)
    assert st["status"] == EXITED and m[0x50] == 33


def test_newecl_reports_the_block_to_chain_to(cbin, tmp_path):
    code = bytes([0x09]) + imm(34) + mem(0x10) + bytes([0x20]) + mem(0x10)
    _, m, st, _ = c_run(cbin, tmp_path, code)
    assert st["status"] == CHAIN and st["chain"] == "34" and m[0x10] == 34


def test_host_gets_everything_else_with_values_and_text(cbin, tmp_path):
    code = (bytes([0x09]) + imm(4) + mem(0x1b)
            + bytes([0x21]) + mem(0x1b) + imm(4)                  # LOAD_FILES [0x1b], #4
            + bytes([0x12]) + text("YOUR VOYAGE BEGINS PEACEFULLY.")
            + bytes([0x0e]) + imm(12) + b"\x00")
    host, _, st, _ = c_run(cbin, tmp_path, code)
    assert st["status"] == EXITED and st["unhandled"] == "0"
    assert host == [(0x21, [4, 4], []),
                    (0x12, [text("YOUR VOYAGE BEGINS PEACEFULLY.")[1]],
                     ["YOUR VOYAGE BEGINS PEACEFULLY."]),
                    (0x0e, [12], [])]


def test_saving_text_reaches_the_host_and_stores_its_marker(cbin, tmp_path):
    code = bytes([0x09]) + text("RAISTLIN") + bytes([0x81, 0x0e, 0x02]) + b"\x00"
    host, m, _, _ = c_run(cbin, tmp_path, code)
    assert host == [(0x09, [text("RAISTLIN")[1], 0x020e], ["RAISTLIN"])]
    assert m[0x020e] == text("RAISTLIN")[1]


def test_a_pause_resumes_after_the_instruction(cbin, tmp_path):
    code = (bytes([0x24])                                         # COMBAT
            + bytes([0x09]) + imm(1) + mem(0x90) + b"\x00")
    _, m, st, pauses = c_run(cbin, tmp_path, code)
    assert pauses == [int(COMBAT)] and st["status"] == EXITED and m[0x90] == 1


def test_the_host_can_end_the_script_from_an_instruction(cbin, tmp_path):
    # The Dark Queen's 0x23 clears the text box and exits; what follows it in
    # the block is another cell's code and must not run.
    code = (bytes([0x23])
            + bytes([0x09]) + imm(1) + mem(0x90) + b"\x00")
    host, m, st, _ = c_run(cbin, tmp_path, code)
    assert st["status"] == EXITED and [h[0] for h in host] == [0x23]
    assert m.get(0x90, 0) == 0


def test_a_host_answer_steers_the_next_if(cbin, tmp_path):
    # 0x22 asks yes/no and leaves the answer in the flags: yes is ==.
    code = (bytes([0x22, 0x17, 0x00])                             # YES_NO; IF_NE; EXIT
            + bytes([0x09]) + imm(7) + mem(0x90) + b"\x00")
    _, m, st, _ = c_run(cbin, tmp_path, code)
    assert st["status"] == EXITED and m[0x90] == 7


def test_a_jump_into_another_block_is_skipped_not_followed(cbin, tmp_path):
    code = bytes([0x01]) + mem(0x0042) + bytes([0x09]) + imm(1) + mem(0x90) + b"\x00"
    _, m, st, _ = c_run(cbin, tmp_path, code)
    assert st["status"] == EXITED and st["unhandled"] == "1" and m[0x90] == 1


def test_a_byte_that_is_no_opcode_faults_with_its_address(cbin, tmp_path):
    code = bytes([0x3d, 0x7B])                                    # CLEAR_BOX ; ???
    _, _, st, _ = c_run(cbin, tmp_path, code)
    assert st["status"] == FAULT and st["fault"] == "21"


def test_each_entry_point_starts_where_the_header_says(cbin, tmp_path):
    code = (bytes([0x09]) + imm(1) + mem(0xA0) + b"\x00"          # entry 4 (offset 0)
            + bytes([0x09]) + imm(1) + mem(0xA1) + b"\x00")       # entry 0 (offset 7)
    _, m, _, _ = c_run(cbin, tmp_path, code, entry=0, entries=[7, 0, 0, 0, 0])
    assert list(m) == [0xA1]
    _, m, _, _ = c_run(cbin, tmp_path, code, entry=4, entries=[7, 0, 0, 0, 0])
    assert list(m) == [0xA0]


def test_gen1_block_needs_its_magic_word(cbin, tmp_path):
    p = tmp_path / "blk.bin"
    p.write_bytes(block(b"\x00", GEN2) + b"\x00\x00")
    r = subprocess.run([cbin, "run", "gen1", str(p), "4", "10"],
                       capture_output=True, text=True, check=True)
    assert "load-failed" in r.stdout
