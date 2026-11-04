#!/usr/bin/env python3
"""Turns a picture into the DDS the PS5 home screen wants behind a tile (pic0.dds / pic1.dds).

Usage: png_to_bc7_dds.py <in.png|jpg> <out.dds> [width height]   (default 3840 2160)

What the console reads is a single BC7_UNORM DDS (DX10 header, no mip levels), 3840 x 2160 for the full-screen
background; a plain pic0.png beside it is not used (the ps5-homebrew-ui kit's validate-assets.sh checks the same
format, its sample files are exactly this). The picture is scaled to cover the size and centred; it is opaque.

BC7 is written in mode 6 only (one colour pair per 4x4 block, 8-bit effective endpoints, 16 steps between them).
That is not the best BC7 there is, but for a dark photographic background it is visually clean, and it needs no
encoder library: numpy and Pillow are enough. The block's endpoints are the extremes along the block's main colour
axis; every pixel then takes the nearest of the sixteen colours between them. Needs Pillow and numpy.
"""
import struct
import sys

import numpy as np
from PIL import Image

# The 4-bit interpolation weights BC7 uses between the two endpoints (out of 64).
W4 = np.array([0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64], dtype=np.int32)


def cover(im, w, h):
    im = im.convert("RGB")
    s = max(w / im.width, h / im.height)
    nw, nh = max(w, round(im.width * s)), max(h, round(im.height * s))
    im = im.resize((nw, nh), Image.LANCZOS)
    left, top = (nw - w) // 2, (nh - h) // 2
    return im.crop((left, top, left + w, top + h))


def encode(rgb):
    """rgb: (h, w, 3) uint8, both sizes a multiple of 4. Returns the BC7 blocks as bytes (row after row of blocks)."""
    h, w, _ = rgb.shape
    bx, by = w // 4, h // 4
    # (blocks, 16 pixels, 3): pixel 0 is the block's top left, row by row
    blocks = rgb.reshape(by, 4, bx, 4, 3).transpose(0, 2, 1, 3, 4).reshape(by * bx, 16, 3)
    n = blocks.shape[0]
    out = np.zeros((n, 2), dtype=np.uint64)
    step = 40000
    for a in range(0, n, step):
        out[a:a + step] = encode_blocks(blocks[a:a + step].astype(np.float64))
    return out.astype("<u8").tobytes()


def principal_axis(px):
    mean = px.mean(axis=1, keepdims=True)
    c = px - mean
    cov = np.einsum("bpi,bpj->bij", c, c)
    cov += np.eye(3)[None] * 1e-6                       # flat blocks: any axis will do
    _, vecs = np.linalg.eigh(cov)
    return vecs[:, :, 2], mean[:, 0, :]                 # the eigenvector of the largest eigenvalue


def quantise(e):
    """8-bit effective endpoint with the shared P-bit forced to 1 (so alpha 255 stays 255): 2 * c7 + 1."""
    c7 = np.clip(np.rint((e - 1.0) / 2.0), 0, 127).astype(np.int64)
    return c7, c7 * 2 + 1


def encode_blocks(px):
    n = px.shape[0]
    axis, mean = principal_axis(px)
    t = np.einsum("bpi,bi->bp", px - mean[:, None, :], axis)
    lo = px[np.arange(n), t.argmin(axis=1)]
    hi = px[np.arange(n), t.argmax(axis=1)]
    c0, e0 = quantise(lo)
    c1, e1 = quantise(hi)
    # all sixteen colours between the endpoints, as the decoder makes them
    cand = ((64 - W4)[None, :, None] * e0[:, None, :] + W4[None, :, None] * e1[:, None, :] + 32) >> 6
    cand = cand.astype(np.float64)                       # (n, 16, 3)
    d = ((px[:, :, None, :] - cand[:, None, :, :]) ** 2).sum(axis=3)   # (n, pixel, step)
    idx = d.argmin(axis=2).astype(np.int64)              # (n, 16)
    # the first pixel's index may only use 3 bits: swap the endpoints when it is 8 or more
    swap = idx[:, 0] >= 8
    idx = np.where(swap[:, None], 15 - idx, idx)
    c0s = np.where(swap[:, None], c1, c0)
    c1s = np.where(swap[:, None], c0, c1)
    lo64 = np.full(n, 1 << 6, dtype=np.uint64)           # mode 6: six zero bits, then a one
    off = 7
    for ch in range(3):                                  # R0 R1 G0 G1 B0 B1, seven bits each
        lo64 |= c0s[:, ch].astype(np.uint64) << np.uint64(off)
        off += 7
        lo64 |= c1s[:, ch].astype(np.uint64) << np.uint64(off)
        off += 7
    lo64 |= np.uint64(127) << np.uint64(49)              # A0 = 127
    lo64 |= np.uint64(127) << np.uint64(56)              # A1 = 127
    lo64 |= np.uint64(1) << np.uint64(63)                # P0 = 1  (255 for alpha)
    hi64 = np.ones(n, dtype=np.uint64)                   # P1 = 1, bit 64
    hi64 |= idx[:, 0].astype(np.uint64) << np.uint64(1)  # index 0: 3 bits at 65
    for i in range(1, 16):
        hi64 |= idx[:, i].astype(np.uint64) << np.uint64(4 * i)
    return np.stack([lo64, hi64], axis=1)


def dds_header(w, h):
    size = w * h                                         # BC7: one byte per pixel
    head = b"DDS " + struct.pack("<7I", 124, 0xA1007, h, w, size, 1, 1)
    head += b"\0" * 44                                   # reserved
    head += struct.pack("<2I4s5I", 32, 4, b"DX10", 0, 0, 0, 0, 0)       # pixel format: FourCC DX10
    head += struct.pack("<5I", 0x1000, 0, 0, 0, 0)       # caps: texture
    head += struct.pack("<5I", 98, 3, 0, 1, 1)           # DXGI_FORMAT_BC7_UNORM, 2D, 1 array slice, alpha straight
    return head


def main():
    if len(sys.argv) not in (3, 5):
        sys.exit(__doc__)
    w, h = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) == 5 else (3840, 2160)
    if w % 4 or h % 4:
        sys.exit("width and height must be multiples of 4")
    rgb = np.asarray(cover(Image.open(sys.argv[1]), w, h), dtype=np.uint8)
    data = encode(rgb)
    with open(sys.argv[2], "wb") as f:
        f.write(dds_header(w, h))
        f.write(data)
    print("%s: %dx%d BC7, %d bytes" % (sys.argv[2], w, h, 148 + len(data)))


if __name__ == "__main__":
    main()
