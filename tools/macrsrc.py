#!/usr/bin/env python3
"""Reader for classic Mac resource forks. Shared by the tools/ scripts.

A resource fork is: a 16-byte header, the resource data, then the resource
map (type list -> reference lists -> optional name list). See Inside
Macintosh: More Macintosh Toolbox, "Resource Manager".
"""
import struct
from dataclasses import dataclass


@dataclass
class Resource:
    type: str
    id: int
    name: str
    attrs: int
    data: bytes


class ResourceFork:
    def __init__(self, blob):
        self.blob = blob
        data_off, map_off, _data_len, _map_len = struct.unpack_from(">IIII", blob, 0)
        type_list_off = struct.unpack_from(">H", blob, map_off + 24)[0]
        name_list_off = struct.unpack_from(">H", blob, map_off + 26)[0]
        type_list = map_off + type_list_off
        name_list = map_off + name_list_off

        self.resources = []
        n_types = struct.unpack_from(">H", blob, type_list)[0] + 1
        for t in range(n_types):
            base = type_list + 2 + t * 8
            rtype = blob[base:base + 4].decode("mac-roman", "replace")
            count = struct.unpack_from(">H", blob, base + 4)[0] + 1
            ref_list = type_list + struct.unpack_from(">H", blob, base + 6)[0]
            for r in range(count):
                ref = ref_list + r * 12
                rid = struct.unpack_from(">h", blob, ref)[0]
                name_off = struct.unpack_from(">H", blob, ref + 2)[0]
                attrs = blob[ref + 4]
                data_ptr = struct.unpack_from(">I", blob, ref + 4)[0] & 0x00FFFFFF
                size = struct.unpack_from(">I", blob, data_off + data_ptr)[0]
                start = data_off + data_ptr + 4
                rdata = blob[start:start + size]
                name = ""
                if name_off != 0xFFFF:
                    ln = blob[name_list + name_off]
                    name = blob[name_list + name_off + 1:
                                name_list + name_off + 1 + ln].decode(
                                    "mac-roman", "replace")
                self.resources.append(Resource(rtype, rid, name, attrs, rdata))

    @classmethod
    def from_file(cls, path):
        with open(path, "rb") as f:
            return cls(f.read())

    def of_type(self, rtype):
        return sorted((r for r in self.resources if r.type == rtype),
                      key=lambda r: r.id)

    def get(self, rtype, rid):
        for r in self.resources:
            if r.type == rtype and r.id == rid:
                return r
        raise KeyError(f"{rtype} {rid}")
