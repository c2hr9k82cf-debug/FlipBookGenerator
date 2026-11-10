#!/usr/bin/env python3
# make_test_character.py — author the 32x32 test character, pixel by pixel.
# 逐像素绘制 32x32 测试角色。
#
# Why this exists / 为什么有这个文件:
#   The test character is the input every animation result is judged against, so
#   its design is not incidental. It was authored here rather than drawn in an
#   editor so that (a) every pixel is deliberate and reviewable, and (b) the six
#   part layers are known exactly — which makes the reconstruction invariant
#   exact by construction instead of by luck.
#   测试角色是评判一切动画结果的输入，所以它的设计不是随手的。在这里逐像素
#   绘制而非用编辑器画，是为了 (a) 每个像素都有据可查，(b) 六个部件图层完全
#   已知 —— 重建不变量因此是【构造上】精确，而不是碰巧。
#
# Design rules followed / 遵循的设计规则:
#   * Content is 14 wide x 30 tall inside 32x32, placed at x9..x22, y2..y31.
#     Proportions are pixel-exact from the reference sheet (09d465...png):
#     head 11 rows (y2-y12), body 8 rows (y13-y20), legs+boots 11 rows (y21-y31).
#     内容 14 宽 x 30 高，放在 32x32 的 x9..x22, y2..y31。
#     比例逐像素对齐参考图：头 11 行、身体 8 行、腿加靴 11 行。
#   * Palette is the reference's exact 15-color set — no substitutions.
#     调色板就是参考图的精确 15 色 —— 不做任何替换。
#   * Face is 2px wide at eye level (x15-x16); the rest of the head is hair.
#     眼睛处脸宽仅 2px（x15-x16），其余全是头发。
#
# Usage / 用法:
#   python3 -I tools/make_test_character.py            # write into the repo
#   python3 -I tools/make_test_character.py /tmp/peek  # write somewhere to look

import os
import sys
import struct
import zlib

# ---------------------------------------------------------------------------
# Single source of truth / 唯一来源
#
# The pixel map lives in assets/templates/chibi_standard_pixels.txt and the
# palette + part ownership in assets/templates/chibi_standard.json. Nothing
# about the character is duplicated here: reskinning means editing those two
# files, and every tool that renders the template (this one,
# make_review_sheet.py, the view extractors) reads the same pair.
# 像素图在 assets/templates/chibi_standard_pixels.txt，调色板与部件归属在
# assets/templates/chibi_standard.json。角色数据在此处零重复：换皮就是改那两个
# 文件，所有渲染模版的工具（本工具、make_review_sheet.py、视图提取器）
# 读的是同一对文件。
# ---------------------------------------------------------------------------

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_template():
    """(art rows, char -> RGB-or-None, spec) from the two template files.
    从那两个模版文件得到（像素行列表，字符 -> RGB 或 None，spec）。"""
    import json
    with open(os.path.join(ROOT, 'assets', 'templates', 'chibi_standard.json')) as f:
        spec = json.load(f)
    palette = {k: tuple(v) for k, v in spec['palette'].items()}
    # palette_chars maps a character to a palette ROLE name ('.' -> None for
    # transparent). palette holds plain RGB; nothing on this path is BGR.
    # palette_chars 把字符映射到调色板【角色名】（'.' -> None 表示透明）。
    # palette 存的是纯 RGB，这条路径上没有 BGR。
    char_map = spec['palette_chars']
    rgb = {ch: (palette[role] if role else None) for ch, role in char_map.items()}

    rows = []
    with open(os.path.join(ROOT, 'assets', 'templates', 'chibi_standard_pixels.txt')) as f:
        for line in f:
            line = line.rstrip('\n')
            if not line or line.lstrip().startswith('#'):
                continue
            rows.append(line)
    return rows, rgb, spec


ART, MAP, SPEC = load_template()

# ---------------------------------------------------------------------------
# Part ownership / 部件归属
#
# Every opaque pixel belongs to exactly ONE part, which is what makes the
# reconstruction invariant hold by construction. The split itself lives in
# the template's part_map (absolute image coords) — not here — so a reskin or
# a new view can redefine it without touching this script.
# 每个不透明像素只属于【一个】部件 —— 重建不变量因此构造性成立。划分本身在
# 模版的 part_map（绝对图像坐标）里，不在这里 —— 换皮或新增视图都能重定义它，
# 不必改本脚本。
#
#   1 head       y2..y12   whole width / 整宽
#   2 body       y13..y20  centre columns / 中间列
#   3 arm_left   y13..y20  x9..x11
#   4 arm_right  y13..y20  x20..x22
#   5 leg_left   y21..y31  x9..x15
#   6 leg_right  y21..y31  x16..x21
# ---------------------------------------------------------------------------

PART_IDS = {  # name -> id, must match src/utils.cpp / 必须与 src/utils.cpp 一致
    'head': 1, 'body': 2, 'arm_left': 3,
    'arm_right': 4, 'leg_left': 5, 'leg_right': 6,
}

ORIGIN_X, ORIGIN_Y = 9, 2   # 30 rows in 32x32, y2..y31 / 30 行，y2..y31
                            # top margin 2px, bottom margin 0px / 上 2px，下 0px


def part_of(row, col):
    """Which part owns this ART pixel?  Returns a part name or None.
    Ownership comes from the template's part_map, in absolute image coords.
    这个像素属于哪个部件？归属来自模版的 part_map，用绝对图像坐标。"""
    pm = SPEC['part_map']
    y = row + ORIGIN_Y
    x = col + ORIGIN_X
    if y in pm['head_rows']:
        return 'head'
    if y in pm['body_rows']:
        if x in pm['arm_cols']:
            return 'arm_left' if x <= pm['leg_split_col'] else 'arm_right'
        return 'body'
    if y in pm['leg_rows']:
        return 'leg_left' if x <= pm['leg_split_col'] else 'leg_right'
    return None


# ---------------------------------------------------------------------------
# PNG writer — stdlib only, so this file has no dependencies.
# PNG 写出 —— 只用标准库，本文件零依赖。
# ---------------------------------------------------------------------------

def write_png(path, rgba_rows):
    h = len(rgba_rows)
    w = len(rgba_rows[0])
    raw = b''.join(b'\x00' + bytes(px for p in row for px in p) for row in rgba_rows)

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data
                + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)  # 8-bit RGBA
    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr)
           + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(png)


def build():
    """Return (frame rows, {part: (bbox, sprite rows)})."""
    assert len(ART) == 30, f'ART must be 30 rows, got {len(ART)}'
    for i, r in enumerate(ART):
        assert len(r) == 14, f'ART[{i}] is {len(r)} cols, expected 14: {r!r}'

    frame = [[(0, 0, 0, 0) for _ in range(32)] for _ in range(32)]
    owned = {}   # part -> list of (fx, fy, rgba)

    for row, line in enumerate(ART):
        for col, ch in enumerate(line):
            rgba = MAP[ch]
            if rgba is None:
                continue
            assert ch in MAP, f'unknown art character {ch!r} at {row},{col}'
            fx = ORIGIN_X + col
            fy = ORIGIN_Y + row
            frame[fy][fx] = rgba + (255,)
            name = part_of(row, col)
            assert name, f'opaque pixel at {row},{col} belongs to no part'
            owned.setdefault(name, []).append((fx, fy, rgba + (255,)))

    parts = {}
    for name, pxs in owned.items():
        xs = [p[0] for p in pxs]
        ys = [p[1] for p in pxs]
        bx, by = min(xs), min(ys)
        bw, bh = max(xs) - bx + 1, max(ys) - by + 1
        sprite = [[(0, 0, 0, 0) for _ in range(bw)] for _ in range(bh)]
        for fx, fy, rgba in pxs:
            sprite[fy - by][fx - bx] = rgba
        parts[name] = ((bx, by, bw, bh), sprite)

    return frame, parts


def reconstruct(frame, parts):
    out = [[(0, 0, 0, 0) for _ in range(32)] for _ in range(32)]
    for (bx, by, bw, bh), sprite in parts.values():
        for y in range(bh):
            for x in range(bw):
                if sprite[y][x][3] != 0:
                    out[by + y][bx + x] = sprite[y][x]
    return out


def main():
    out_root = sys.argv[1] if len(sys.argv) > 1 else None
    frame, parts = build()

    # The reconstruction gate, checked before anything is written.
    # 重建闸门 —— 在写出任何东西之前先验。
    recon = reconstruct(frame, parts)
    diff = sum(1 for y in range(32) for x in range(32) if recon[y][x] != frame[y][x])
    if diff:
        print(f'RECONSTRUCTION FAIL — {diff} pixels differ')
        return 1
    print(f'reconstruction: PASS (1024/1024 pixels identical)')

    if out_root:
        assets = out_root
        bundle = os.path.join(out_root, 'bundle')
    else:
        here = os.path.dirname(os.path.abspath(__file__))
        assets = os.path.join(os.path.dirname(here), 'assets')
        bundle = os.path.join(os.path.dirname(here), 'tests', 'fixtures', 'character_32')
    os.makedirs(assets, exist_ok=True)
    os.makedirs(bundle, exist_ok=True)

    write_png(os.path.join(assets, 'character_32.png'), frame)

    names = ['head', 'body', 'arm_left', 'arm_right', 'leg_left', 'leg_right']
    with open(os.path.join(bundle, 'anchors.txt'), 'w') as f:
        f.write('# partId x y   — one rotation pivot per part, original-image coords\n'
                '# 每部件一个旋转锚点，原图坐标\n')
        for name in names:
            (bx, by, bw, bh), _ = parts[name]
            pid = PART_IDS[name]
            # defaultPivot() from src/utils.cpp, computed here so the file
            # documents the intent rather than being magic.
            # 这里算的就是 src/utils.cpp 的 defaultPivot()，写清楚好过凭空出现。
            cx = bx + (bw - 1) // 2
            if pid == 1:      x, y = cx, by + bh - 1     # head: neck / 头：颈
            elif pid in (3,): x, y = bx + bw - 1, by     # arm_left: shoulder / 左臂：肩
            elif pid in (4,): x, y = bx, by              # arm_right: shoulder / 右臂：肩
            else:             x, y = cx, by              # body/legs: top joint / 躯干腿：顶端关节
            f.write(f'{pid} {x} {y}\n')

    with open(os.path.join(bundle, 'image_path.txt'), 'w') as f:
        f.write('assets/character_32.png\n')

    for name in names:
        (bx, by, bw, bh), sprite = parts[name]
        write_png(os.path.join(bundle, name + '.png'), sprite)
        with open(os.path.join(bundle, name + '_bbox.txt'), 'w') as f:
            f.write(f'{bx} {by} {bw} {bh}\n')

    print(f'wrote {os.path.join(assets, "character_32.png")}')
    print(f'wrote bundle -> {bundle}')
    for name in names:
        (bx, by, bw, bh), sprite = parts[name]
        op = sum(1 for r in sprite for p in r if p[3])
        print(f'  {name:<10} bbox {bx} {by} {bw} {bh}   {op} opaque px')
    return 0


if __name__ == '__main__':
    sys.exit(main())
