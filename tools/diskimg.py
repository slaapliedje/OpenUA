#!/usr/bin/env python3
"""diskimg — write Atari FAT12 (.st) and Amiga FFS (.adf) floppy images in
plain Python, so the data-disk tool runs on Windows, macOS and Linux with no
mtools or amitools.

    img = FatImage('atari')            # 'atari' 1.44 MB, 'atari720', 'gotek'
    img = AdfImage()                   # 880 KB DD, FFS (DOS\\1)
    img.add_file('HEIRS.DSN/GEO001.DAT', data)   # folders are made as needed
    img.save('disk1.st', label='OpenUA-Data-1')

Folders nest to any depth. Names must already be legal for the target: 8.3
for FAT (stored upper-case, as TOS expects), at most 30 characters and no
':' or '/' for Amiga. A disk that runs out of room raises DiskFull; nothing
is written.

The FAT12 boot sectors carry the values mformat writes for the same
geometry (measured, see GEOMETRIES), which is what every Atari data disk so
far was made with. The FFS layout is the AmigaDOS one: boot block, root at
block 880, one bitmap block, file headers with their data-block tables in
reverse order, extension blocks past 72 data blocks, and the DOS\\1 hash.
tests/test_diskimg.py reads both back with mtools and amitools when they are
installed.
"""
import struct
import time

__all__ = ['FatImage', 'AdfImage', 'DiskFull', 'GEOMETRIES']


class DiskFull(Exception):
    pass


# Measured from mformat (-f 1440, -f 720, -t 255 -h 2 -n 36) on 2026-11-26.
#   spc: sectors per cluster; root: root-directory entries; spf: sectors per
#   FAT; media: media byte. The gotek geometry is FlashFloppy's 75 rpm image
#   (tools/mkhwdist.sh writes the matching IMG.CFG stanza).
GEOMETRIES = {
    'atari':    dict(tracks=80,  heads=2, spt=18, spc=1, root=224, spf=9, media=0xF0),
    'atari720': dict(tracks=80,  heads=2, spt=9,  spc=2, root=112, spf=3, media=0xF9),
    'gotek':    dict(tracks=255, heads=2, spt=36, spc=8, root=512, spf=7, media=0xF0),
}

# Fixed time stamp: the same data makes the same image.
_STAMP = time.struct_time((2026, 1, 1, 12, 0, 0, 3, 1, 0))


class _Node:
    def __init__(self, name):
        self.name = name
        self.children = {}        # upper-case name -> _Node (dirs) or bytes
        self.order = []           # names as given, insertion order

    def add(self, parts, data):
        head = parts[0]
        key = head.upper()
        if len(parts) == 1:
            if key in self.children:
                raise ValueError('duplicate file: %s' % head)
            self.children[key] = data
            self.order.append(head)
            return
        sub = self.children.get(key)
        if sub is None:
            sub = _Node(head)
            self.children[key] = sub
            self.order.append(head)
        elif not isinstance(sub, _Node):
            raise ValueError('%s is a file, not a folder' % head)
        sub.add(parts[1:], data)

    def items(self):
        for name in sorted(self.order, key=str.upper):
            yield name, self.children[name.upper()]


class _Image:
    def __init__(self):
        self.root = _Node('')

    def add_file(self, path, data):
        parts = [p for p in path.replace('\\', '/').split('/') if p]
        if not parts:
            raise ValueError('empty path')
        for p in parts:
            self.check_name(p)
        self.root.add(parts, bytes(data))

    def save(self, path, label):
        img = self.build(label)
        with open(path, 'wb') as fh:
            fh.write(img)
        return img


# --------------------------------------------------------------------------
# FAT12
# --------------------------------------------------------------------------

def _fat_name(name):
    """'frua.rsc' -> b'FRUA    RSC'. Raises on anything not 8.3."""
    up = name.upper()
    if up in ('.', '..'):
        return up.encode().ljust(11)
    base, dot, ext = up.partition('.')
    bad = set('"*+,/:;<=>?[\\]| ')
    if (not base or len(base) > 8 or len(ext) > 3 or '.' in ext
            or any(c in bad or ord(c) < 33 or ord(c) > 126 for c in base + ext)):
        raise ValueError('not an 8.3 name: %r' % name)
    return base.encode().ljust(8) + ext.encode().ljust(3)


def _fat_datetime():
    t = _STAMP
    date = ((t.tm_year - 1980) << 9) | (t.tm_mon << 5) | t.tm_mday
    tod = (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2)
    return tod, date


class FatImage(_Image):
    def __init__(self, geometry='atari'):
        super().__init__()
        self.g = dict(GEOMETRIES[geometry])
        self.geometry = geometry

    @staticmethod
    def check_name(name):
        _fat_name(name)

    # layout
    def _layout(self):
        g = self.g
        total = g['tracks'] * g['heads'] * g['spt']
        root_secs = g['root'] * 32 // 512
        first_data = 1 + 2 * g['spf'] + root_secs
        nclusters = (total - first_data) // g['spc']
        return total, root_secs, first_data, nclusters

    def capacity(self):
        """(cluster bytes, clusters, root entries) — for the packer."""
        _, _, _, n = self._layout()
        return self.g['spc'] * 512, n, self.g['root']

    def build(self, label):
        """The real FAT12 builder: one pass that allocates and writes each folder
        with its children, so a subfolder's entries know their clusters."""
        g = self.g
        total, root_secs, first_data, nclusters = self._layout()
        csize = g['spc'] * 512
        img = bytearray(total * 512)
        fat = [0] * (nclusters + 2)
        fat[0] = 0xF00 | g['media']
        fat[1] = 0xFFF
        nxt = [2]

        def alloc(nbytes):
            count = max(1, (nbytes + csize - 1) // csize)
            if nxt[0] + count > nclusters + 2:
                raise DiskFull('FAT12 %s: out of clusters' % self.geometry)
            first = nxt[0]
            for i in range(count):
                c = first + i
                fat[c] = c + 1 if i < count - 1 else 0xFFF
            nxt[0] += count
            return first

        def put(first, data):
            off = (first_data + (first - 2) * g['spc']) * 512
            img[off:off + len(data)] = data

        tod, date = _fat_datetime()

        def entry(name11, attr, cluster, size):
            return (name11 + bytes([attr]) + bytes(10)
                    + struct.pack('<HHHI', tod, date, cluster, size))

        def entries(node, own, parent):
            """Directory entries for node; allocates and fills its children."""
            ents = []
            if own:
                ents.append(entry(b'.          ', 0x10, own, 0))
                ents.append(entry(b'..         ', 0x10, parent, 0))
            for name, child in node.items():
                if isinstance(child, _Node):
                    first = alloc((2 + len(child.order)) * 32)
                    sub = entries(child, first, own)
                    put(first, b''.join(sub))
                    ents.append(entry(_fat_name(name), 0x10, first, 0))
                else:
                    first = 0
                    if child:
                        first = alloc(len(child))
                        put(first, child)
                    ents.append(entry(_fat_name(name), 0x20, first, len(child)))
            return ents

        root_ents = [entry(label.upper().encode()[:11].ljust(11), 0x08, 0, 0)]
        root_ents += entries(self.root, 0, 0)
        if len(root_ents) > g['root']:
            raise DiskFull('FAT12 %s: %d root entries, %d fit'
                           % (self.geometry, len(root_ents), g['root']))
        roff = (1 + 2 * g['spf']) * 512
        img[roff:roff + 32 * len(root_ents)] = b''.join(root_ents)

        fb = bytearray(g['spf'] * 512)
        for i in range(len(fat)):
            v = fat[i]
            o = i * 3 // 2
            if i % 2 == 0:
                fb[o] = v & 0xFF
                fb[o + 1] = (fb[o + 1] & 0xF0) | ((v >> 8) & 0x0F)
            else:
                fb[o] = (fb[o] & 0x0F) | ((v & 0x0F) << 4)
                fb[o + 1] = (v >> 4) & 0xFF
        for k in range(2):
            o = (1 + k * g['spf']) * 512
            img[o:o + len(fb)] = fb
        img[0:512] = self._boot(total, label)
        self.used_clusters = nxt[0] - 2
        return bytes(img)

    def _boot(self, total, label):
        g = self.g
        b = bytearray(512)
        b[0:3] = b'\xeb\x3c\x90'
        b[3:11] = b'OPENUA  '
        struct.pack_into('<HBHBHHBHHHII', b, 11, 512, g['spc'], 1, 2,
                         g['root'], total, g['media'], g['spf'], g['spt'],
                         g['heads'], 0, 0)
        b[36] = 0                       # drive
        b[38] = 0x29                    # extended BPB
        b[39:43] = b'OUA1'              # serial
        b[43:54] = label.upper().encode()[:11].ljust(11)
        b[54:62] = b'FAT12   '
        b[510:512] = b'\x55\xaa'
        # TOS runs a boot sector whose big-endian word sum is 0x1234; make
        # sure this one never is.
        s = sum(struct.unpack('>256H', bytes(b))) & 0xFFFF
        if s == 0x1234:
            b[62] ^= 1
        return bytes(b)




# --------------------------------------------------------------------------
# Amiga FFS (DOS\1), 880 KB DD
# --------------------------------------------------------------------------

ADF_BLOCKS = 1760
ADF_ROOT = 880
_HT = 72                     # hash-table / data-block-table slots per block
T_HEADER, T_LIST = 2, 16
ST_ROOT, ST_USERDIR, ST_FILE = 1, 2, -3


def _amiga_hash(name):
    h = len(name)
    for c in name.encode('latin-1'):
        if 97 <= c <= 122:
            c -= 32
        h = (h * 13 + c) & 0x7FF
    return h % _HT


def _amiga_date():
    days = (time.mktime(_STAMP) - time.mktime(
        time.struct_time((1978, 1, 1, 0, 0, 0, 6, 1, 0)))) // 86400
    return int(days), _STAMP.tm_hour * 60 + _STAMP.tm_min, 0


class AdfImage(_Image):
    @staticmethod
    def check_name(name):
        if not name or len(name) > 30 or ':' in name or '/' in name:
            raise ValueError('not an Amiga file name: %r' % name)
        name.encode('latin-1')

    @staticmethod
    def capacity():
        """Free blocks on an empty disk (all but boot, root and bitmap)."""
        return ADF_BLOCKS - 2 - 2

    def build(self, label):
        self.check_name(label)
        blocks = [bytearray(512) for _ in range(ADF_BLOCKS)]
        used = [False] * ADF_BLOCKS
        for b in (0, 1, ADF_ROOT, ADF_ROOT + 1):
            used[b] = True
        order = list(range(ADF_ROOT + 2, ADF_BLOCKS)) + list(range(2, ADF_ROOT))
        cursor = [0]
        meta = set()              # header and list blocks: checksummed

        def alloc():
            while cursor[0] < len(order):
                b = order[cursor[0]]
                cursor[0] += 1
                if not used[b]:
                    used[b] = True
                    return b
            raise DiskFull('ADF %s: out of blocks' % label)

        days, mins, ticks = _amiga_date()

        def L(blk, off, val):
            struct.pack_into('>i' if val < 0 else '>I', blocks[blk], off, val)

        def name_at(blk, off, name):
            n = name.encode('latin-1')
            blocks[blk][off] = len(n)
            blocks[blk][off + 1:off + 1 + len(n)] = n

        def link(dir_blk, child_blk, child_name):
            """Put child into dir's hash table (chained at the head)."""
            slot = 24 + 4 * _amiga_hash(child_name)
            prev = struct.unpack_from('>I', blocks[dir_blk], slot)[0]
            L(child_blk, 496, prev)
            L(dir_blk, slot, child_blk)

        def write_file(parent, name, data):
            hdr = alloc()
            meta.add(hdr)
            nblocks = (len(data) + 511) // 512
            dblocks = []
            for i in range(nblocks):
                b = alloc()
                blocks[b][:len(data[i * 512:(i + 1) * 512])] = data[i * 512:(i + 1) * 512]
                dblocks.append(b)
            L(hdr, 0, T_HEADER)
            L(hdr, 4, hdr)
            first = dblocks[:_HT]
            L(hdr, 8, len(first))
            L(hdr, 16, first[0] if first else 0)
            for i, b in enumerate(first):
                L(hdr, 24 + 4 * (_HT - 1 - i), b)
            L(hdr, 324, len(data))
            L(hdr, 420, days); L(hdr, 424, mins); L(hdr, 428, ticks)
            name_at(hdr, 432, name)
            L(hdr, 500, parent)
            L(hdr, 508, ST_FILE)
            # extension blocks, 72 data blocks each
            prev = hdr
            rest = dblocks[_HT:]
            while rest:
                chunk, rest = rest[:_HT], rest[_HT:]
                ext = alloc()
                meta.add(ext)
                L(ext, 0, T_LIST)
                L(ext, 4, ext)
                L(ext, 8, len(chunk))
                for i, b in enumerate(chunk):
                    L(ext, 24 + 4 * (_HT - 1 - i), b)
                L(ext, 500, hdr)
                L(ext, 508, ST_FILE)
                L(prev, 504, ext)
                prev = ext
            link(parent, hdr, name)

        def write_dir(parent, name, node):
            blk = alloc()
            meta.add(blk)
            L(blk, 0, T_HEADER)
            L(blk, 4, blk)
            L(blk, 420, days); L(blk, 424, mins); L(blk, 428, ticks)
            name_at(blk, 432, name)
            L(blk, 500, parent)
            L(blk, 508, ST_USERDIR)
            fill(blk, node)
            link(parent, blk, name)

        def fill(dir_blk, node):
            for name, child in node.items():
                if isinstance(child, _Node):
                    write_dir(dir_blk, name, child)
                else:
                    write_file(dir_blk, name, child)

        # root
        r = ADF_ROOT
        L(r, 0, T_HEADER)
        L(r, 12, _HT)
        L(r, 312, -1)                  # bitmap valid
        L(r, 316, ADF_ROOT + 1)        # bitmap block
        for off in (420, 472, 484):    # root, volume, creation dates
            L(r, off, days); L(r, off + 4, mins); L(r, off + 8, ticks)
        name_at(r, 432, label)
        L(r, 508, ST_ROOT)
        fill(r, self.root)

        # checksums: word 5 of every header/list block, so the longs sum to 0
        for b in meta | {ADF_ROOT}:
            self._checksum(blocks[b], 20)
        # bitmap: bit set = free; bit k of long n is block 2 + 32n + k
        bm = blocks[ADF_ROOT + 1]
        for n in range((ADF_BLOCKS - 2 + 31) // 32):
            v = 0
            for k in range(32):
                blk = 2 + 32 * n + k
                if blk < ADF_BLOCKS and not used[blk]:
                    v |= 1 << k
            struct.pack_into('>I', bm, 4 + 4 * n, v)
        self._checksum(bm, 0)

        blocks[0][0:4] = b'DOS\x01'
        struct.pack_into('>I', blocks[0], 8, ADF_ROOT)
        # boot-block checksum (both blocks, add with carry, inverted). Only
        # a bootable disk needs it, but Workbench tools report its absence.
        s = 0
        for v in struct.unpack('>256I', bytes(blocks[0]) + bytes(blocks[1])):
            s += v
            if s > 0xFFFFFFFF:
                s = (s & 0xFFFFFFFF) + 1
        struct.pack_into('>I', blocks[0], 4, (~s) & 0xFFFFFFFF)
        self.used_blocks = sum(used) - 4
        return b''.join(bytes(b) for b in blocks)

    @staticmethod
    def _checksum(blk, off):
        struct.pack_into('>I', blk, off, 0)
        s = sum(struct.unpack('>128I', bytes(blk))) & 0xFFFFFFFF
        struct.pack_into('>I', blk, off, (-s) & 0xFFFFFFFF)
