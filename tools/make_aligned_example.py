#!/usr/bin/env python3
# make_aligned_example.py — synthesise strictly template-aligned example views.
# 合成严格模板对齐的示例三视图。
#
# assets/character_alt*.png is the DELIBERATE negative fixture: its hair corners
# stick out 8px past the template, so extraction rejects it (that is its job).
# This script builds the usable counterpart: the alt reskin colours, but on the
# template's own pixels — pixel-exact by construction, so extraction accepts it.
# assets/character_alt*.png 是**故意**的负向夹具：头发角比模板突出 8 像素，提取会
# 拒绝它（它的职责所在）。本脚本造出能用的对应物：alt 换皮配色，但像素形状就是
# 模板本身 —— 构造上逐像素对齐，提取必然接受。
#
# Output / 输出: assets/example_{front,side,back}.png
# Usage / 用法:
#   python3 -I tools/make_aligned_example.py

import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHIPS = os.path.join(ROOT, 'assets/templates/motion_chips/chips')
PALETTE_MAP = os.path.join(ROOT, 'assets/templates/motion_chips/palette_map.txt')


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


def write_png(path, w, h, rows):
    """Write RGBA rows as a PNG (filter 0). / 把 RGBA 行写成 PNG（滤波 0）。"""
    def chunk(tag, body):
        c = struct.pack('>I', len(body)) + tag + body
        return c + struct.pack('>I', zlib.crc32(tag + body) & 0xFFFFFFFF)
    raw = b''.join(b'\x00' + bytes(r) for r in rows)
    out = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 9))
           + chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(out)


def load_palette_map(path):
    """Parse '<sB,sG,sR,sA> <dB,dG,dR,dA>' lines into RGBA keys.
    解析每行一对 BGRA 八元组，转成 RGBA 键（PNG 字节序）。"""
    def bgra_to_rgba(t):
        return (t[2], t[1], t[0], t[3])
    m = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            a, b = line.split()
            src = tuple(int(v) for v in a.split(','))
            dst = tuple(int(v) for v in b.split(','))
            m[bgra_to_rgba(src)] = bgra_to_rgba(dst)
    return m


def reskin(rows, w, h, bpp, palette):
    """Replace each pixel's BGRA via the palette; alpha-0 stays transparent.
    按调色板替换每像素 BGRA；alpha 0 保持透明。"""
    out = []
    unknown = set()
    for y in range(h):
        row = bytearray()
        for x in range(w):
            px = tuple(rows[y][x*bpp:x*bpp+4])
            if px[3] == 0:
                row += b'\x00\x00\x00\x00'
                continue
            if px in palette:
                row += bytes(palette[px])
            else:
                unknown.add(px)
                row += bytes(px)
        out.append(row)
    return out, unknown


def main():
    palette = load_palette_map(PALETTE_MAP)
    print(f'palette entries / 映射条目: {len(palette)}')
    for view in ('front', 'side', 'back'):
        src = os.path.join(CHIPS, f'{view}_idle_0.png')
        w, h, bpp, rows = read_png(src)
        out_rows, unknown = reskin(rows, w, h, bpp, palette)
        if unknown:
            print(f'{view}: {len(unknown)} unmapped colour(s) kept as-is / '
                  f'未映射颜色原样保留:')
            for px in sorted(unknown):
                print('   ', px)
        dst = os.path.join(ROOT, 'assets', f'example_{view}.png')
        write_png(dst, w, h, out_rows)
        print(f'wrote / 已写 {dst} ({w}x{h})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
