#!/usr/bin/env python3
# compare_alpha.py — where do two PNGs' alpha masks differ?
# 两张 PNG 的 alpha 掩码差异在哪些像素？
#
# The palette-extraction gate rejects an input whose silhouette does not match
# the template; this tool shows exactly which pixels are off.
# 配色提取闸门会拒绝剪影与模板不符的输入；本工具列出具体差在哪些像素。
#
# Usage / 用法:
#   python3 -I tools/compare_alpha.py user.png template.png

import struct
import sys
import zlib


def read_png(path):
    """Read a PNG file and return (width, height, bpp, rows)."""
    with open(path, 'rb') as f:
        data = f.read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', f'not a PNG: {path}'
    pos = 8
    idat = b''
    w = h = 0
    ct = 6
    while pos < len(data):
        length = struct.unpack('>I', data[pos:pos+4])[0]
        tag = data[pos+4:pos+8]
        chunk = data[pos+8:pos+8+length]
        if tag == b'IHDR':
            w, h, bd, ct = struct.unpack('>IIBB', chunk[:10])
        elif tag == b'IDAT':
            idat += chunk
        pos += 12 + length
    raw = zlib.decompress(idat)
    bpp = 4 if ct == 6 else 3
    rows = []
    stride = w * bpp + 1
    for y in range(h):
        row_data = raw[y*stride:(y+1)*stride]
        ft = row_data[0]
        row = bytearray(row_data[1:])
        if ft == 1:
            for i in range(bpp, len(row)):
                row[i] = (row[i] + row[i-bpp]) & 0xFF
        elif ft == 2:
            if y > 0:
                prev = rows[y-1]
                for i in range(len(row)):
                    row[i] = (row[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(len(row)):
                left = row[i-bpp] if i >= bpp else 0
                up = rows[y-1][i] if y > 0 else 0
                row[i] = (row[i] + (left + up) // 2) & 0xFF
        elif ft == 4:
            for i in range(len(row)):
                a = row[i-bpp] if i >= bpp else 0
                b = rows[y-1][i] if y > 0 else 0
                c = rows[y-1][i-bpp] if (y > 0 and i >= bpp) else 0
                p = a + b - c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                row[i] = (row[i] + pr) & 0xFF
        rows.append(row)
    return w, h, bpp, rows


def mask(rows, bpp, w, h):
    """Opaque (alpha != 0) mask; RGB-only images are opaque everywhere.
    非透明（alpha != 0）掩码；纯 RGB 图视为全不透明。"""
    out = []
    for y in range(h):
        row = rows[y]
        if bpp == 4:
            out.append([row[x*4+3] != 0 for x in range(w)])
        else:
            out.append([True] * w)
    return out


def main():
    if len(sys.argv) != 3:
        print(__doc__ or 'usage: compare_alpha.py a.png b.png')
        sys.exit(2)
    wa, ha, ba, ra = read_png(sys.argv[1])
    wb, hb, bb, rb = read_png(sys.argv[2])
    if (wa, ha) != (wb, hb):
        print(f'size mismatch: {wa}x{ha} vs {wb}x{hb}')
        sys.exit(1)
    ma = mask(ra, ba, wa, ha)
    mb = mask(rb, bb, wb, hb)
    diffs = [(x, y) for y in range(ha) for x in range(wa) if ma[y][x] != mb[y][x]]
    print(f'{len(diffs)} differing alpha pixel(s) / alpha 不同的像素数: {len(diffs)}')
    for (x, y) in diffs[:40]:
        print(f'  ({x},{y}): a={"in " if ma[y][x] else "out"} b={"in " if mb[y][x] else "out"}')
    if len(diffs) > 40:
        print(f'  ... and {len(diffs) - 40} more / 另有 {len(diffs) - 40} 处')


if __name__ == '__main__':
    main()
