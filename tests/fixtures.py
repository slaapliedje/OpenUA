"""Synthetic binary fixtures for the tool tests.

Each builder produces a minimal but format-correct blob, so the suite needs
no real game data (which is copyrighted and git-ignored anyway).
"""
import struct


def build_resource_fork(resources):
    """Build a minimal classic Mac resource fork.

    resources: list of (type:str, id:int, name:str, data:bytes).
    """
    # Resource data section: a 4-byte length then the bytes, per resource.
    data = b""
    data_ptr = []
    for _type, _id, _name, blob in resources:
        data_ptr.append(len(data))
        data += struct.pack(">I", len(blob)) + blob

    # Resource indices grouped by type, in first-seen order.
    by_type = {}
    for idx, (rtype, _id, _name, _blob) in enumerate(resources):
        by_type.setdefault(rtype, []).append(idx)

    # Name list: a Pascal string per named resource.
    names = b""
    name_off = []
    for _type, _id, name, _blob in resources:
        if name:
            name_off.append(len(names))
            enc = name.encode("mac-roman")
            names += bytes([len(enc)]) + enc
        else:
            name_off.append(0xFFFF)

    # Type list followed by the per-type reference lists.
    ref_base = 2 + 8 * len(by_type)        # first ref list, from type-list start
    type_list = struct.pack(">H", len(by_type) - 1)
    ref_lists = b""
    for rtype, idxs in by_type.items():
        type_list += (rtype.encode("mac-roman").ljust(4)[:4]
                      + struct.pack(">HH", len(idxs) - 1,
                                    ref_base + len(ref_lists)))
        for idx in idxs:
            _t, rid, _n, _b = resources[idx]
            ref_lists += struct.pack(">hHII", rid, name_off[idx],
                                     data_ptr[idx], 0)
    type_list += ref_lists

    # Resource map: 28-byte header, then the type list, then the names.
    type_list_off, _ = 28, None
    name_list_off = type_list_off + len(type_list)
    rmap = (b"\0" * 16                     # reserved copy of the fork header
            + b"\0\0\0\0"                  # next-map handle
            + b"\0\0"                      # file reference number
            + b"\0\0"                      # map attributes
            + struct.pack(">HH", type_list_off, name_list_off)
            + type_list + names)

    data_off, map_off = 16, 16 + len(data)
    header = struct.pack(">IIII", data_off, map_off, len(data), len(rmap))
    return header + data + rmap


def build_splt_segment(index, count, total, payload):
    """Build one DiskDoubler SPLT split-archive segment (94-byte header)."""
    head = bytearray(0x5E)
    head[0:4] = b"SPLT"
    struct.pack_into(">H", head, 0x04, count)
    struct.pack_into(">I", head, 0x08, total)
    struct.pack_into(">H", head, 0x2A, index)
    struct.pack_into(">I", head, 0x2C, len(payload))
    return bytes(head) + payload


def build_appledouble(forks):
    """Build an AppleDouble file. forks: dict {entry_id: bytes}."""
    out = struct.pack(">II", 0x00051607, 0x00020000) + b"\0" * 16
    out += struct.pack(">H", len(forks))
    base = len(out) + 12 * len(forks)
    descriptors, body = b"", b""
    for eid, blob in forks.items():
        descriptors += struct.pack(">III", eid, base + len(body), len(blob))
        body += blob
    return out + descriptors + body


def build_code0(jt_off, entries):
    """Build a CODE 0 jump-table resource. entries: [(seg, routine_offset)]."""
    blob = struct.pack(">IIII", 0x1000, 0x8000, len(entries) * 8, jt_off)
    for seg, ro in entries:
        blob += struct.pack(">HHHH", ro, 0x3F3C, seg, 0xA9F0)
    return blob
