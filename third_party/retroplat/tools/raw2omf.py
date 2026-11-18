#!/usr/bin/env python3
"""raw2omf -- wrap a flat binary in an Apple IIGS OMF v2 segment.

WHY THIS EXISTS
    Calypsi's 65816 linker emits S-record/S19/S28/S37/intel-hex/pgz/prg/raw
    and no OMF.  GS/OS loads OMF.  That single gap is the whole reason the
    IIGS is not a buildable target, even though the compiler itself handles
    a full C codebase without complaint.  This closes it for the simplest
    and most useful case: one fully-linked, absolute segment.

WHAT IT DOES NOT DO, said plainly
    * No relocation.  Emits LCONST + END only, which is valid exactly when
      the code is already linked to the address in --org and makes no
      cross-segment references.  A relocatable or multi-segment build needs
      RELOC/INTERSEG records and a real linker pass; this is not that.
    * No ProDOS metadata.  An OMF file must ALSO be given filetype $B3
      (S16) and auxtype $0000 by whatever writes it to the disk image.
      That lives outside the file, so it lives outside this tool.
    * NOT VERIFIED ON HARDWARE OR IN AN EMULATOR.  Written from the OMF
      spec; the --selftest below checks the bytes against an independent
      re-parser, which catches a malformed file but cannot tell you GS/OS
      is happy.  Treat "selftest passes" as "well-formed", not "works".

Layout is OMF v2.0 (VERSION=2), which is what GS/OS wants.
"""

import argparse
import struct
import sys

# --- OMF v2.0 segment header ------------------------------------------
# Fixed portion is $2C bytes, then LOADNAME (always 10), then SEGNAME.
HDR_FIXED = 0x2C
LOADNAME_LEN = 10

OP_END = 0x00
OP_LCONST = 0xF2

KIND_CODE = 0x0000          # low byte: 0 = code segment

# LCONST carries a 4-byte length, so a single record could hold 4GB. Real
# loaders are happier with modest records, and chunking costs nothing.
LCONST_CHUNK = 0xFF00


def build_omf(payload, org=0, entry=0, segname="MAIN", loadname="",
              kind=KIND_CODE, segnum=1, resspc=0):
    """Return the bytes of a single-segment OMF v2 file."""
    seg = segname.upper().encode("ascii")
    load = loadname.upper().encode("ascii")[:LOADNAME_LEN]
    load = load + b" " * (LOADNAME_LEN - len(load))

    # LABLEN 0 means names are variable length, each preceded by a byte
    # count. LOADNAME is the one exception: always LOADNAME_LEN bytes.
    dispname = HDR_FIXED
    dispdata = HDR_FIXED + LOADNAME_LEN + 1 + len(seg)

    body = bytearray()
    off = 0
    while off < len(payload):
        chunk = payload[off:off + LCONST_CHUNK]
        body.append(OP_LCONST)
        body += struct.pack("<I", len(chunk))
        body += chunk
        off += len(chunk)
    body.append(OP_END)

    bytecnt = dispdata + len(body)
    length = len(payload) + resspc

    h = bytearray(HDR_FIXED)
    struct.pack_into("<I", h, 0x00, bytecnt)     # BYTECNT
    struct.pack_into("<I", h, 0x04, resspc)      # RESSPC
    struct.pack_into("<I", h, 0x08, length)      # LENGTH
    h[0x0C] = 0                                  # undefined in v2 (v1 KIND)
    h[0x0D] = 0                                  # LABLEN 0 = variable names
    h[0x0E] = 4                                  # NUMLEN must be 4
    h[0x0F] = 2                                  # VERSION = 2
    struct.pack_into("<I", h, 0x10, 0x00010000)  # BANKSIZE: one bank
    struct.pack_into("<H", h, 0x14, kind)        # KIND
    struct.pack_into("<I", h, 0x18, org)         # ORG (0 = relocatable)
    struct.pack_into("<I", h, 0x1C, 0)           # ALIGN
    h[0x20] = 0                                  # NUMSEX: little-endian
    struct.pack_into("<H", h, 0x22, segnum)      # SEGNUM
    struct.pack_into("<I", h, 0x24, entry)       # ENTRY
    struct.pack_into("<H", h, 0x28, dispname)    # DISPNAME
    struct.pack_into("<H", h, 0x2A, dispdata)    # DISPDATA

    return bytes(h) + load + bytes([len(seg)]) + seg + bytes(body)


# --- independent re-parser --------------------------------------------
# Deliberately reads the fields back out of the raw bytes rather than
# reusing anything above, so a mistake in the writer cannot hide behind
# the same mistake in the checker.

def parse_omf(data):
    if len(data) < HDR_FIXED:
        raise ValueError("shorter than an OMF header")
    bytecnt, resspc, length = struct.unpack_from("<III", data, 0)
    lablen = data[0x0D]
    numlen = data[0x0E]
    version = data[0x0F]
    kind = struct.unpack_from("<H", data, 0x14)[0]
    org = struct.unpack_from("<I", data, 0x18)[0]
    numsex = data[0x20]
    segnum = struct.unpack_from("<H", data, 0x22)[0]
    entry = struct.unpack_from("<I", data, 0x24)[0]
    dispname = struct.unpack_from("<H", data, 0x28)[0]
    dispdata = struct.unpack_from("<H", data, 0x2A)[0]

    if version != 2:
        raise ValueError("VERSION is %d, expected 2" % version)
    if numlen != 4:
        raise ValueError("NUMLEN is %d, must be 4" % numlen)
    if numsex != 0:
        raise ValueError("NUMSEX is %d, expected 0 (little-endian)" % numsex)
    if bytecnt != len(data):
        raise ValueError("BYTECNT %d != file size %d" % (bytecnt, len(data)))
    if dispname != HDR_FIXED:
        raise ValueError("DISPNAME %d != %d" % (dispname, HDR_FIXED))

    nlen = data[dispname + LOADNAME_LEN]
    segname = data[dispname + LOADNAME_LEN + 1:
                   dispname + LOADNAME_LEN + 1 + nlen].decode("ascii")
    if dispdata != dispname + LOADNAME_LEN + 1 + nlen:
        raise ValueError("DISPDATA %d does not follow SEGNAME" % dispdata)

    payload = bytearray()
    i = dispdata
    saw_end = False
    while i < len(data):
        op = data[i]; i += 1
        if op == OP_END:
            saw_end = True
            break
        elif op == OP_LCONST:
            n = struct.unpack_from("<I", data, i)[0]; i += 4
            payload += data[i:i + n]; i += n
        elif 0x01 <= op <= 0xDF:          # short LCONST: opcode IS the count
            payload += data[i:i + op]; i += op
        else:
            raise ValueError("unhandled record opcode 0x%02X at %d" % (op, i - 1))
    if not saw_end:
        raise ValueError("no END record")
    if i != len(data):
        raise ValueError("%d trailing bytes after END" % (len(data) - i))
    if length != len(payload) + resspc:
        raise ValueError("LENGTH %d != payload %d + RESSPC %d"
                         % (length, len(payload), resspc))

    return dict(payload=bytes(payload), org=org, entry=entry, kind=kind,
                segnum=segnum, segname=segname, length=length,
                resspc=resspc, lablen=lablen)


def selftest():
    import os
    cases = [
        (b"", 0x000000, "EMPTY"),
        (b"\x00", 0x002000, "ONEBYTE"),
        (bytes(range(256)) * 4, 0x010000, "MAIN"),
        (os.urandom(LCONST_CHUNK * 2 + 7), 0x038000, "BIG"),   # forces chunking
    ]
    for payload, org, name in cases:
        blob = build_omf(payload, org=org, entry=0, segname=name)
        got = parse_omf(blob)
        assert got["payload"] == payload, "%s: payload round-trip failed" % name
        assert got["org"] == org, "%s: ORG %d != %d" % (name, got["org"], org)
        assert got["segname"] == name, "%s: SEGNAME %r" % (name, got["segname"])
        print("  ok  %-8s payload=%-7d omf=%-7d org=$%06X"
              % (name, len(payload), len(blob), org))
    # a corrupted file must be REJECTED, or the checker proves nothing
    bad = bytearray(build_omf(b"hello", org=0x2000))
    bad[0x0F] = 1                      # VERSION 1
    try:
        parse_omf(bytes(bad)); raise AssertionError("bad VERSION not rejected")
    except ValueError:
        pass
    bad = bytearray(build_omf(b"hello", org=0x2000))
    bad[0] = (bad[0] + 1) & 0xFF       # corrupt BYTECNT
    try:
        parse_omf(bytes(bad)); raise AssertionError("bad BYTECNT not rejected")
    except ValueError:
        pass
    print("  ok  malformed files are rejected (VERSION, BYTECNT)")
    print("selftest passed -- well-formed per the spec, NOT verified on GS/OS")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", nargs="?", help="flat binary (Calypsi --output-format raw)")
    ap.add_argument("-o", "--output", help="OMF file to write")
    ap.add_argument("--org", type=lambda s: int(s, 0), default=0,
                    help="absolute load address the binary was linked for")
    ap.add_argument("--entry", type=lambda s: int(s, 0), default=0,
                    help="entry point, as an offset within the segment")
    ap.add_argument("--segname", default="MAIN")
    ap.add_argument("--loadname", default="")
    ap.add_argument("--segnum", type=int, default=1)
    ap.add_argument("--dump", action="store_true", help="parse an OMF and report")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    if a.selftest:
        selftest(); return 0
    if not a.input:
        ap.error("an input file is required")
    data = open(a.input, "rb").read()

    if a.dump:
        info = parse_omf(data)
        for k in ("segname", "segnum", "org", "entry", "kind", "length", "resspc"):
            v = info[k]
            print("  %-8s %s" % (k, ("$%06X" % v) if k in ("org", "entry") else v))
        print("  %-8s %d bytes" % ("payload", len(info["payload"])))
        return 0

    if not a.output:
        ap.error("-o/--output is required")
    blob = build_omf(data, org=a.org, entry=a.entry,
                     segname=a.segname, loadname=a.loadname, segnum=a.segnum)
    # never write a file we cannot read back
    back = parse_omf(blob)
    if back["payload"] != data:
        raise SystemExit("internal error: round-trip mismatch, refusing to write")
    open(a.output, "wb").write(blob)
    print("%s -> %s  (%d bytes payload, %d bytes OMF, org=$%06X)"
          % (a.input, a.output, len(data), len(blob), a.org))
    print("remember: set the file's ProDOS filetype to $B3 (S16), auxtype $0000")
    return 0


if __name__ == "__main__":
    sys.exit(main())
