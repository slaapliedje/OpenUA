# tools/

Host-side tooling for the FRUA port. These run on the build host, not the
Atari.

## Python tooling

Most tools are pure standard library; `hfs_extract.py` needs `machfs`, and
`dis68k.py` shells out to `m68k-atari-mint-objdump`.

| Tool             | Needs           | Purpose                                |
|------------------|-----------------|----------------------------------------|
| `hfs_extract.py` | venv (machfs)   | List / extract a classic HFS volume.   |
| `dd_unsplit.py`  | --              | Reassemble a DiskDoubler split archive.|
| `appledouble.py` | --              | Extract a fork from AppleSingle/Double.|
| `rsrc_list.py`   | --              | List a Mac resource fork's contents.   |
| `dis68k.py`      | objdump         | Disassemble & annotate the CODE segments. |
| `rsrcpack.py`    | --              | Pack a resource fork into the FRSC archive. |

`macrsrc.py` (resource-fork reader) and `mactraps.py` (A-line trap names) are
shared modules, imported by the tools above rather than run directly.

Set up the virtualenv once — `machfs` for `hfs_extract.py`, `pytest` for the
test suite (`make test`):

```sh
python3 -m venv tools/.venv
tools/.venv/bin/pip install machfs pytest
```

Run venv tools with `tools/.venv/bin/python3`; the rest with plain `python3`.
`tools/.venv/` is git-ignored. The suite under `tests/` builds synthetic
fixtures, so it needs no real game data; run it with `make test`.

`docs/mac-release.md` covers how the unpacking tools chain together;
`docs/decompilation.md` covers `dis68k.py` and the disassembly model.

## rsrcpack

`rsrcpack.py` packs a Mac resource fork into the flat FRSC archive the
Resource Manager shim (`compat/resources.c`) reads at runtime (ADR-0007).
`appledouble.py` unwraps the fork from an AppleDouble file upstream.

```
rsrcpack.py <resource-fork> -o frua.rsc
```

## FRSC archive format

Version 1. All multi-byte fields are **big-endian** (matches 68k and the
original Mac resource data, so resource bodies are copied verbatim).

```
Header (16 bytes)
  +0   char[4]   magic  "FRSC"
  +4   uint16    format version (1)
  +6   uint16    entry count N
  +8   uint32    offset to the entry table
  +12  uint32    reserved (0)

Entry table  —  N x 16 bytes, sorted by (type, id) for binary search
  +0   char[4]   resource type   (e.g. 'DLOG')
  +4   int16     resource id
  +6   uint16    attributes      (resource flags from the fork)
  +8   uint32    data offset     (from start of file)
  +12  uint32    data length

Resource data
  Raw resource bodies, concatenated in entry-table order.
```

Resource *names* (Mac resources may be named as well as numbered) are not
stored in version 1. If a named lookup turns out to be needed, append a name
table and bump the version.
