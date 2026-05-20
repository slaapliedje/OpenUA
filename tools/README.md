# tools/

Host-side tooling for the FRUA port. These run on the build host, not the
Atari.

## rsrcpack

Extracts the Mac resource fork and packs it into the flat archive the
Resource Manager shim reads at runtime (ADR-0007).

Planned input formats: MacBinary (`.bin`), AppleDouble (`._name`), and a raw
resource fork. Extracting a fork from an HFS disk image is a separate step.

```
rsrcpack <input> -o frua.rsrc
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
