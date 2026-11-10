#!/usr/bin/env python3
# extract_view_templates.py — extract the side and back pixel maps from the
# reference sheet.
# 从参考图提取侧视与背视的像素图。
#
# The reference sheet is the ABSOLUTE TEMPLATE. Its idle frames are the
# standard standing pose for each view, so they are what a rig must be built
# from. This script lifts the side (cell 0,2) and back (cell 0,4) poses out of
# the sheet and writes them in the same format as chibi_standard_pixels.txt,
# so the whole toolchain reads one shape of file.
# 参考图是【绝对模板】。它的待机帧就是各视图的标准站姿，也正是骨架要对齐的东西。
# 本脚本把侧视（格 0,2）与背视（格 0,4）的站姿提出来，按
# chibi_standard_pixels.txt 的格式写出，这样整条工具链读的是同一种文件。
#
# Why the idle frame and not a walk frame / 为什么取待机帧而非走路帧:
#   A walk frame has limbs mid-swing: a rotated leg is not a leg you can rig
#   from. The idle frame is the standing pose, which is exactly the neutral
#   pose every transform is measured from.
#   走路帧的肢体摆到一半：一条转过角度的腿不是能用来装骨架的腿。待机帧是站姿，
#   正是所有变换的基准姿势。
#
# Usage / 用法:
#   python3 -I tools/extract_view_templates.py [reference.png]

import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---------------------------------------------------------------------------
# Palette / 调色板 — exact RGB from the reference sheet / 参考图精确 RGB
#
# Keep in step with assets/templates/chibi_standard.json's palette. The two
# are compared in main() so a drift is loud rather than silent.
# 与 assets/templates/chibi_standard.json 的 palette 保持一致。main() 里会比对，
# 不一致就大声报错，不会悄悄走样。
# ---------------------------------------------------------------------------

PALETTE = {
    (46, 34, 47): 'outline',
    (247, 150, 23): 'hair_mid',
    (251, 107, 29): 'hair_dark',
    (249, 194, 43): 'hair_highlight',
    (253, 203, 176): 'skin',
    (252, 167, 144): 'blush',
    (230, 144, 78): 'tan',
    (199, 220, 208): 'mint',
    (255, 255, 255): 'white',
    (232, 59, 59): 'red',
    (205, 104, 61): 'pants',
    (155, 171, 178): 'grey',
    (127, 112, 138): 'grey_dark',
    (22, 90, 76): 'teal',
}

CELL = 32
# Content band every view lives in: x9..x22, y2..y31 inside 32x32.
# 三视图共用的内容带：32x32 内的 x9..x22, y2..y31。
BAND_X0, BAND_Y0, BAND_W, BAND_H = 9, 2, 14, 30


# ---------------------------------------------------------------------------
# PNG reader — stdlib only / 仅用标准库
# ---------------------------------------------------------------------------

def read_png(path):
    """(width, height, bpp, rows) for an 8-bit PNG."""
    with open(path, 'rb') as f:
        data = f.read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', f'not a PNG: {path}'
    pos, idat, w, h, ct = 8, b'', 0, 0, 6
    while pos < len(data):
        length = struct.unpack('>I', data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        if tag == b'IHDR':
            w, h, bd, ct = struct.unpack('>IIBB', chunk[:10])
            assert bd == 8, f'only 8-bit PNGs supported, got bit depth {bd}'
        elif tag == b'IDAT':
            idat += chunk
        pos += 12 + length
    raw = zlib.decompress(idat)
    bpp = 4 if ct == 6 else 3
    rows, stride = [], w * bpp + 1
    for y in range(h):
        row_data = raw[y * stride:(y + 1) * stride]
        ft, row = row_data[0], bytearray(row_data[1:])
        if ft == 1:
            for i in range(bpp, len(row)):
                row[i] = (row[i] + row[i - bpp]) & 0xFF
        elif ft == 2:
            if y > 0:
                prev = rows[y - 1]
                for i in range(len(row)):
                    row[i] = (row[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(len(row)):
                left = row[i - bpp] if i >= bpp else 0
                up = rows[y - 1][i] if y > 0 else 0
                row[i] = (row[i] + (left + up) // 2) & 0xFF
        elif ft == 4:
            for i in range(len(row)):
                a = row[i - bpp] if i >= bpp else 0
                b = rows[y - 1][i] if y > 0 else 0
                c = rows[y - 1][i - bpp] if (y > 0 and i >= bpp) else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                row[i] = (row[i] + pr) & 0xFF
        rows.append(row)
    return w, h, bpp, rows


def pixel_at(rows, bpp, cx, cy, lx, ly):
    """(r, g, b, a) at local (lx, ly) inside grid cell (cx, cy)."""
    x = cx * CELL + lx
    y = cy * CELL + ly
    o = x * bpp
    r = rows[y]
    return (r[o], r[o + 1], r[o + 2], r[o + 3] if bpp == 4 else 255)


# ---------------------------------------------------------------------------
# Extraction / 提取
# ---------------------------------------------------------------------------

def extract_cell(rows, bpp, cx, cy):
    """A 32x32 cell as a grid of palette characters ('.' = transparent).
    Returns (chars[32][32], unknown) where unknown lists off-palette pixels."""
    grid = [['.'] * CELL for _ in range(CELL)]
    unknown = []
    for ly in range(CELL):
        for lx in range(CELL):
            r, g, b, a = pixel_at(rows, bpp, cx, cy, lx, ly)
            if a == 0:
                continue
            name = PALETTE.get((r, g, b))
            if name is None:
                unknown.append((lx, ly, r, g, b))
                continue
            grid[ly][lx] = CHAR_FOR[name]
    return grid, unknown


def content_extent(grid):
    """(x0, x1, y0, y1) inclusive bounds of the opaque content."""
    xs = [x for y in range(CELL) for x in range(CELL) if grid[y][x] != '.']
    ys = [y for y in range(CELL) for x in range(CELL) if grid[y][x] != '.']
    return min(xs), max(xs), min(ys), max(ys)


CHAR_FOR = {
    'outline': '@', 'hair_mid': 'h', 'hair_dark': 'H', 'hair_highlight': 'y',
    'skin': 's', 'blush': 'b', 'tan': 't', 'mint': 'm', 'white': 'w',
    'red': 'r', 'pants': 'p', 'grey': 'g', 'grey_dark': 'G', 'teal': 'T',
}


def write_pixels(path, title_cn, title_en, subtitle_cn, subtitle_en, band_rows):
    """Write one pixels.txt in the shared 30x14 format (x9..x22, y2..y31).
    按共用的 30x14 格式（x9..x22, y2..y31）写出一个 pixels.txt。"""
    lines = [
        f'# {os.path.basename(path)} — pixel-exact character template ({title_en})',
        f'# 逐像素角色模板（{title_cn}）',
        '#',
        f'# {subtitle_en}',
        f'# {subtitle_cn}',
        f'# {BAND_H} rows x {BAND_W} cols, placed at x{BAND_X0}..x{BAND_X0 + BAND_W - 1}, '
        f'y{BAND_Y0}..y{BAND_Y0 + BAND_H - 1} in 32x32.',
        f'# {BAND_H} 行 x {BAND_W} 列，放在 32x32 的 x{BAND_X0}..x{BAND_X0 + BAND_W - 1}, '
        f'y{BAND_Y0}..y{BAND_Y0 + BAND_H - 1}。',
        '#',
        '# Palette / 调色板:',
        '#   @ = outline(46,34,47)       h = hair_mid(247,150,23)    H = hair_dark(251,107,29)',
        '#   y = hair_highlight(249,194,43)  s = skin(253,203,176)   b = blush(252,167,144)',
        '#   t = tan(230,144,78)         m = mint(199,220,208)       w = white(255,255,255)',
        '#   r = red(232,59,59)          p = pants(205,104,61)       g = grey(155,171,178)',
        '#   G = grey_dark(127,112,138)  T = teal(22,90,76)          . = transparent',
        '#',
        '# Part ownership lives in chibi_standard.json (views.<view>.part_map),',
        '# in absolute image coords — not duplicated here.',
        '# 部件归属在 chibi_standard.json（views.<view>.part_map）里，用绝对图像坐标',
        '# —— 此处不重复。',
    ]
    for y, row in enumerate(band_rows):
        lines.append(row)
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


# ---------------------------------------------------------------------------
# Reporting / 报告
# ---------------------------------------------------------------------------

def report(name, grid):
    """Print the structure a part_map has to describe."""
    x0, x1, y0, y1 = content_extent(grid)
    print(f'--- {name} ---')
    print(f'  content extent: x{x0}..x{x1} ({x1 - x0 + 1} wide), '
          f'y{y0}..y{y1} ({y1 - y0 + 1} tall)')
    # Per-row extent inside the body band, to show where arms and torso sit.
    print('  per-row extent in the body band (y13..y20):')
    for y in range(13, 21):
        xs = [x for x in range(CELL) if grid[y][x] != '.']
        if not xs:
            continue
        print(f'    y{y}: x{min(xs)}..x{max(xs)}  {"".join(grid[y][min(xs):max(xs) + 1])}')
    print('  per-row extent in the leg band (y21..y31):')
    for y in range(21, 32):
        xs = [x for x in range(CELL) if grid[y][x] != '.']
        if not xs:
            continue
        print(f'    y{y}: x{min(xs)}..x{max(xs)}  {"".join(grid[y][min(xs):max(xs) + 1])}')


def main():
    ref = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'assets', 'reference_sheet.png')
    if not os.path.exists(ref):
        ref = os.path.join(ROOT, os.path.basename(ref))
    if not os.path.exists(ref):
        print(f'ERROR: reference sheet not found: {ref}')
        print('Pass its path, or copy it to assets/reference_sheet.png')
        return 1

    # The palette in this file must match chibi_standard.json exactly.
    # 本文件的调色板必须与 chibi_standard.json 完全一致。
    import json
    with open(os.path.join(ROOT, 'assets', 'templates', 'chibi_standard.json')) as f:
        spec = json.load(f)
    json_palette = {tuple(v) for k, v in spec['palette'].items() if k != 'gold'}
    if set(PALETTE) != json_palette:
        only_here = set(PALETTE) - json_palette
        only_json = json_palette - set(PALETTE)
        print('ERROR: palette drift between this script and chibi_standard.json')
        if only_here:
            print(f'  only here: {sorted(only_here)}')
        if only_json:
            print(f'  only in json: {sorted(only_json)}')
        return 1

    w, h, bpp, rows = read_png(ref)
    print(f'Reference: {ref} ({w}x{h}, {bpp}bpp)')

    # (label, grid row in the 6x6 sheet, output file, part map key)
    # 6x6 网格里的 (标签, 行号, 输出文件, part_map 键)
    views = [
        ('side', 2, 'chibi_standard_side_pixels.txt', 'side',
         'Extracted from reference sheet cell (0,2) — side idle frame 0 (standard standing pose).',
         '取自参考图 (0,2) 格 —— 侧视待机第 0 帧（标准站姿）。'),
        ('back', 4, 'chibi_standard_back_pixels.txt', 'back',
         'Extracted from reference sheet cell (0,4) — back idle frame 0 (standard standing pose).',
         '取自参考图 (0,4) 格 —— 背视待机第 0 帧（标准站姿）。'),
    ]

    out_dir = os.path.join(ROOT, 'assets', 'templates')
    for label, cy, fname, _key, sub_en, sub_cn in views:
        grid, unknown = extract_cell(rows, bpp, 0, cy)
        if unknown:
            print(f'ERROR: {label} has {len(unknown)} off-palette opaque pixels:')
            for lx, ly, r, g, b in unknown[:10]:
                print(f'    ({lx},{ly}) rgb({r},{g},{b})')
            return 1

        x0, x1, y0, y1 = content_extent(grid)
        assert y0 == BAND_Y0 and y1 == BAND_Y0 + BAND_H - 1, \
            f'{label}: content y{y0}..y{y1} does not fill the shared band'
        assert x0 >= BAND_X0 and x1 <= BAND_X0 + BAND_W - 1, \
            f'{label}: content x{x0}..x{x1} escapes the shared band'

        # Slice the shared band out of the full 32x32 cell.
        # 从整幅 32x32 里切出共用的内容带。
        band = [''.join(row[BAND_X0:BAND_X0 + BAND_W])
                for row in grid[BAND_Y0:BAND_Y0 + BAND_H]]
        path = os.path.join(out_dir, fname)
        write_pixels(path, label, label, sub_cn, sub_en, band)
        print(f'wrote {os.path.relpath(path, ROOT)}  '
              f'({BAND_W}x{BAND_H}, content x{x0}..x{x1})')

        report(label, grid)

    print()
    print('Now record each view\'s part_map in chibi_standard.json (views.<view>.part_map).')
    print('接下来把各视图的 part_map 写进 chibi_standard.json（views.<view>.part_map）。')
    return 0


if __name__ == '__main__':
    sys.exit(main())
