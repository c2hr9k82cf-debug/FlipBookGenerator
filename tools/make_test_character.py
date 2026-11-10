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
# Palette / 配色 — exact RGB from reference sheet / 参考图精确 RGB
# ---------------------------------------------------------------------------

OUT   = (46, 34, 47)       # outline / 描边
HAIR1 = (247, 150, 23)     # hair mid / 头发中调
HAIR2 = (251, 107, 29)     # hair dark / 头发暗部
HAIR3 = (249, 194, 43)     # hair highlight / 头发高光
SKIN  = (253, 203, 176)    # skin / 皮肤
SKIN2 = (252, 167, 144)    # blush / 腮红
TAN   = (230, 144, 78)     # tan / 棕色
MINT  = (199, 220, 208)    # mint / 薄荷绿
WHITE = (255, 255, 255)    # white / 白色
RED   = (232, 59, 59)      # red / 红色
PANT  = (205, 104, 61)     # pants / 裤子
GREY  = (155, 171, 178)    # grey / 灰色
GREY2 = (127, 112, 138)    # grey dark / 深灰
TEAL  = (22, 90, 76)       # teal / 青色

MAP = {
    '.': None,
    '@': OUT,
    'h': HAIR1, 'H': HAIR2, 'y': HAIR3,
    's': SKIN,  'b': SKIN2, 't': TAN,
    'm': MINT,  'w': WHITE, 'r': RED,
    'p': PANT,  'g': GREY,  'G': GREY2,
    'T': TEAL,
}

# ---------------------------------------------------------------------------
# The art / 画面 — pixel-exact from reference cell (0,0)
# 14 columns wide (x9..x22), 30 rows tall (y2..y31).
# 每行 14 列（x9..x22），共 30 行（y2..y31）。逐像素取自参考图 (0,0) 格。
# ---------------------------------------------------------------------------

ART = [
    '....@@@@@@....',   # y2   crown / 头发顶
    '...@yhyyhy@...',   # y3   highlight streak / 高光条
    '..@yhhhhhhy@..',   # y4   mid fills / 中调填充
    '.@HhhHhhhhhH@.',   # y5   highlight continues / 高光延续
    '.HhhHbHHHHhh@.',   # y6   dark hair, blush hint / 暗发，腮红暗示
    'HHhH@@bb@@HhH@',   # y7   fringe, eyebrows / 刘海，眉毛
    '@HH@mTssTm@hhH',   # y8   eyes (T=iris) / 眼睛
    '@HHbw@ss@wbHhH',   # y9   eyes 2nd row / 眼睛第二行
    '@HHrssssssrHH.',   # y10  cheeks / 腮红
    '@HHHrssssrHH..',   # y11  mouth / 嘴
    '@HHHrrbbrrHH..',   # y12  chin / 下巴
    '.H@@mgssgm@@..',   # y13  neck + collar / 颈与领
    '.@ssmmggmmss@.',   # y14  chest / 胸
    '.@sbmwmmwmbs@.',   # y15  chest / 胸
    '@sbmwwwwwwmbs@',   # y16  chest / 胸
    '@sbmwwwwwwmbs@',   # y17  chest / 胸
    '@sbmmmmmmmmbs@',   # y18  belt / 腰带
    '@ssbmmmmmmbss@',   # y19  below belt / 腰带下
    '@sss@@@@@@sss@',   # y20  waistline / 腰线
    '.@b@pppptt@b@.',   # y21  hips / 胯
    '.@spppppttts@.',   # y22  thighs / 大腿
    '..@pppptttt@..',   # y23  legs / 腿
    '...@pp@@tt@...',   # y24  legs, seam / 腿，缝
    '...@pp@@tt@...',   # y25  legs / 腿
    '..@pp@.@tt@...',   # y26  boot tops / 靴筒
    '..@pp@.@tt@...',   # y27  boots / 靴
    '..@@@@.@@@@...',   # y28  boot soles / 靴底
    '..@GG@.@gg@...',   # y29  boot detail / 靴细节
    '.@GGG@.@ggg@..',   # y30  boot flare / 靴外扩
    '.@@@@...@@@...',   # y31  soles / 靴底
]

# ---------------------------------------------------------------------------
# Part ownership / 部件归属
#
# Every opaque pixel belongs to exactly ONE part, which is what makes the
# reconstruction invariant hold by construction.
# 每个不透明像素只属于【一个】部件 —— 重建不变量因此构造性成立。
#
#   1 head       y2..y12   (ART rows 0..10)   whole width / 整宽
#   2 body       y13..y20  (ART rows 11..18)  centre cols 3..10
#   3 arm_left   y13..y20  cols 0..2 / 列 0..2
#   4 arm_right  y13..y20  cols 11..13 / 列 11..13
#   5 leg_left   y21..y31  (ART rows 19..29)  cols 0..6 / 列 0..6
#   6 leg_right  y21..y31  cols 7..13 / 列 7..13
# ---------------------------------------------------------------------------

HEAD_ROWS = (0, 10)      # ART indices for y2..y12 (11 rows) / 头 11 行
TORSO_ROWS = (11, 18)    # ART indices for y13..y20 (8 rows) / 身体 8 行
LEG_ROWS = (19, 29)      # ART indices for y21..y31 (11 rows) / 腿 11 行
ARM_COLS = (0, 2, 11, 13)   # left lo, left hi, right lo, right hi
BODY_COLS = (3, 10)
LEG_SPLIT = 7               # cols 0..6 left, 7..13 right

PART_IDS = {  # name -> id, must match src/utils.cpp / 必须与 src/utils.cpp 一致
    'head': 1, 'body': 2, 'arm_left': 3,
    'arm_right': 4, 'leg_left': 5, 'leg_right': 6,
}

ORIGIN_X, ORIGIN_Y = 9, 2   # 30 rows in 32x32, y2..y31 / 30 行，y2..y31
                            # top margin 2px, bottom margin 0px / 上 2px，下 0px


def part_of(row, col):
    """Which part owns ART[row][col]?  Returns a part name or None."""
    if HEAD_ROWS[0] <= row <= HEAD_ROWS[1]:
        return 'head'
    if TORSO_ROWS[0] <= row <= TORSO_ROWS[1]:
        if ARM_COLS[0] <= col <= ARM_COLS[1]:
            return 'arm_left'
        if ARM_COLS[2] <= col <= ARM_COLS[3]:
            return 'arm_right'
        return 'body'
    if LEG_ROWS[0] <= row <= LEG_ROWS[1]:
        return 'leg_left' if col < LEG_SPLIT else 'leg_right'
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
