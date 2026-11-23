"""Hostile NTFS images for the regression test of the driver's checks (audit 23.11.2026): made from a clean image of mkntfs (2 MiB, 512-byte
clusters), each with one lie a stick can tell. python3 hostile.py <clean.img> <out dir>

  attrlen.img  an attribute of $MFT's own record claims a length near 4 GB (next_attr wrapped around: endless loop while mounting)
  nameoff.img  the root folder's $INDEX_ROOT names its name 0xFF00 bytes past the attribute (read past a 1 KB buffer)
  bitmap.img   the boot sector claims 2^44 sectors, $Bitmap a matching size (the free-space count ran for years over zeros)
  dag.img      the root folder's index: 6 blocks, every entry of one points at the next (millions of visits, no cycle)
  div0.img     2^23 sectors per cluster: the cluster size wrapped to 0 in 32 bits (division by zero while mounting)
  short.img    not hostile: the image is 8 KB shorter than its volume, as behind a USB bridge that reports a few sectors less (must
               still be read, never written: clusters past the end would be handed out)
"""
import os
import struct
import sys

src, out = sys.argv[1], sys.argv[2]
clean = open(src, "rb").read()
u16 = lambda b, o: struct.unpack_from("<H", b, o)[0]
u32 = lambda b, o: struct.unpack_from("<I", b, o)[0]
u64 = lambda b, o: struct.unpack_from("<Q", b, o)[0]
bps, spc = u16(clean, 0x0B), clean[0x0D]
cluster = bps * spc
mft_lcn = u64(clean, 0x30)
cpr = struct.unpack_from("<b", clean, 0x40)[0]
rec_size = (1 << -cpr) if cpr < 0 else cpr * cluster


def unfix(buf):
    ofs, cnt = u16(buf, 4), u16(buf, 6)
    for i in range(1, cnt):
        buf[i * 512 - 2:i * 512] = buf[ofs + i * 2:ofs + i * 2 + 2]


def refix(buf):
    ofs, cnt = u16(buf, 4), u16(buf, 6)
    usn = buf[ofs:ofs + 2]
    for i in range(1, cnt):
        buf[ofs + i * 2:ofs + i * 2 + 2] = buf[i * 512 - 2:i * 512]
        buf[i * 512 - 2:i * 512] = usn


def record(img, ino):
    off = mft_lcn * cluster + ino * rec_size
    rec = bytearray(img[off:off + rec_size])
    unfix(rec)
    return off, rec


def attrs(rec):
    p = u16(rec, 0x14)
    while p + 8 <= rec_size:
        t, l = u32(rec, p), u32(rec, p + 4)
        if t == 0xFFFFFFFF or l == 0:
            return
        yield p, t
        p += l


def put(img, off, rec):
    refix(rec)
    img[off:off + rec_size] = rec


def save(img, name):
    open(os.path.join(out, name), "wb").write(img)


# attrlen: $MFT's $BITMAP (0xB0) claims a length near 4 GB that wraps around to the record's first attribute (the walk went round
# and round)
img = bytearray(clean)
off, rec = record(img, 0)
for p, t in attrs(rec):
    if t == 0xB0:
        struct.pack_into("<I", rec, p + 4, (1 << 32) - (p - u16(rec, 0x14)))
        break
put(img, off, rec)
save(img, "attrlen.img")

# nameoff: the root's $INDEX_ROOT name offset far outside
img = bytearray(clean)
off, rec = record(img, 5)
for p, t in attrs(rec):
    if t == 0x90:
        struct.pack_into("<H", rec, p + 10, 0xFF00)
        break
put(img, off, rec)
save(img, "nameoff.img")

# bitmap: 2^44 sectors and a $Bitmap to match, nothing of it initialised
img = bytearray(clean)
sectors = 1 << 44
struct.pack_into("<Q", img, 0x28, sectors)
bm_bytes = (sectors // spc + 7) // 8
off, rec = record(img, 6)
for p, t in attrs(rec):
    if t == 0x80 and rec[p + 8]:
        struct.pack_into("<QQQ", rec, p + 40, bm_bytes, bm_bytes, 0)
        break
put(img, off, rec)
save(img, "bitmap.img")

# dag: a chain of D index blocks, every entry of block d points at block d + 1
D = 6
img = bytearray(clean)
off, rec = record(img, 5)
ir = ia = None
for p, t in attrs(rec):
    if t == 0x90:
        ir = p
    if t == 0xA0:
        ia = p
blk = u32(rec, ir + u16(rec, ir + 20) + 8)
mp = u16(rec, ia + 32)
r = ia + mp
lb, ob = rec[r] & 15, rec[r] >> 4
lcn0 = int.from_bytes(rec[r + 1 + lb:r + 1 + lb + ob], "little", signed=True)
cpb = blk // cluster
need = D * cpb
total = len(img) // cluster
L = next(s for s in range(total // 2, total - need - 64) if not any(img[s * cluster:(s + need) * cluster]))
assert L < 0x8000 and need < 256 and u32(rec, ia + 4) - mp >= 5
rec[r:r + 5] = bytes([0x21, need]) + struct.pack("<H", L) + b"\0"
huge = 0xFFFFFFF0 * blk
struct.pack_into("<QQQ", rec, ia + 40, huge, huge, huge)
put(img, off, rec)
tmpl = bytearray(img[lcn0 * cluster:lcn0 * cluster + blk])
unfix(tmpl)
h = 0x18
eo, alloc = u32(tmpl, h), u32(tmpl, h + 8)
first = h + eo
kl = u16(tmpl, first + 10)
key, ref = bytes(tmpl[first + 16:first + 16 + kl]), bytes(tmpl[first:first + 8])


def entry(ref, key, flags, vcn=None):
    body = (16 + len(key) + 7) & ~7
    e = bytearray(body + (8 if vcn is not None else 0))
    e[0:8] = ref
    struct.pack_into("<HHH", e, 8, len(e), len(key), flags)
    e[16:16 + len(key)] = key
    if vcn is not None:
        struct.pack_into("<Q", e, len(e) - 8, vcn)
    return bytes(e)


room = alloc - eo
for d in range(D):
    b = bytearray(tmpl)
    struct.pack_into("<Q", b, 0x10, d * cpb)
    body = bytearray()
    if d < D - 1:
        e, last = entry(b"\0" * 8, b"", 1, (d + 1) * cpb), entry(b"\0" * 8, b"", 3, (d + 1) * cpb)
    else:
        e, last = entry(ref, key, 0), entry(b"\0" * 8, b"", 2)
    while len(body) + len(e) + len(last) <= room - 8:
        body += e
    body += last
    b[first:first + len(body)] = body
    struct.pack_into("<I", b, h + 4, eo + len(body))
    struct.pack_into("<I", b, h + 12, 1 if d < D - 1 else 0)
    refix(b)
    o = (L + d * cpb) * cluster
    img[o:o + blk] = b
save(img, "dag.img")

# div0: a shift of 23 for the sectors per cluster (512 * 2^23 is 2^32: 0 in 32 bits)
img = bytearray(clean)
img[0x0D] = 0xE9
save(img, "div0.img")

# short: the drive ends 8 KB before its volume
img = bytearray(clean[:len(clean) - 8192])
save(img, "short.img")
print("hostile images: attrlen nameoff bitmap dag div0 short")
