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
#   * Content is 14 wide x 30 tall inside 32x32. Side margins are 9px; top and
#     bottom margins are 1px each (2px total). Proportions match the reference:
#     head ~13 rows, torso ~6 rows, legs+boots ~11 rows.
#     内容 14 宽 x 30 高，装在 32x32 里。左右各留 9px，上下各留 1px（共 2px）。
#     比例对齐参考图：头约 13 行、躯干约 6 行、腿加靴约 11 行。
#   * Chibi proportions taken from that reference: big head (~11 rows), short
#     torso (~6 rows), legs + boots (~10 rows).
#     取自参考图的 Q 版比例：大头（约 11 行）、短躯干（约 6 行）、腿加靴（约 10 行）。
#   * Everything is bounded by a dark outline — that is what makes parts legible
#     at this size.
#     所有形体都由深色描边包围 —— 这个尺寸下，部件之所以看得清全靠它。
#   * Left and right limbs use two different tones so they read as separate
#     limbs even where they touch.
#     左右肢体用两种不同色阶，即使相接也读得出是两条。
#
# Usage / 用法:
#   python3 -I tools/make_test_character.py            # write into the repo
#   python3 -I tools/make_test_character.py /tmp/peek  # write somewhere to look

import os
import sys
import struct
import zlib

# ---------------------------------------------------------------------------
# Palette / 配色
# ---------------------------------------------------------------------------

OUT   = (32, 26, 44)      # outline / 描边
HAIR1 = (170, 150, 216)   # hair highlight / 头发高光
HAIR2 = (124, 100, 178)   # hair mid / 头发中调
HAIR3 = (84, 64, 130)     # hair shadow / 头发暗部
SKIN  = (248, 208, 176)   # skin / 皮肤
SKIN2 = (226, 160, 130)   # skin shadow / 皮肤阴影
MOUTH = (206, 92, 96)     # mouth / 嘴
TOP1  = (86, 168, 122)    # tunic, lit / 上衣亮部
TOP2  = (52, 118, 88)     # tunic, shade / 上衣暗部
BELT  = (222, 176, 82)    # belt / 腰带
PANT1 = (70, 90, 138)     # trousers, lit / 裤子亮部
PANT2 = (46, 60, 100)     # trousers, shade / 裤子暗部
BOOT1 = (150, 100, 68)    # boots, lit / 靴亮部
BOOT2 = (102, 66, 48)     # boots, shade / 靴暗部

MAP = {
    '.': None,
    '@': OUT,
    'h': HAIR1, 'H': HAIR2, 'd': HAIR3,
    's': SKIN,  'b': SKIN2, 'k': MOUTH,
    'r': TOP1,  'R': TOP2,  'g': BELT,
    'n': PANT1, 'N': PANT2,
    'w': BOOT1, 'W': BOOT2,
}

# ---------------------------------------------------------------------------
# The art / 画面
# 14 columns wide (x9..x22), 27 rows tall (y2..y28).
# 每行 14 列（x9..x22），共 27 行（y2..y28）。
# ---------------------------------------------------------------------------

ART = [
    '.....@@@@.....',   # y1   very top of the hair / 头发最顶端
    '....@@@@@@....',   # y2   crown / 头发顶
    '...@hHHHHd@...',   # y3   highlight streak: y3 p4, y4 p5, y5 p6, y6 p7
    '..@HHhHHHdd@..',   # y4   mid fills; shadow hugs the right rim only.
    '.@dHHHhHHHdd@.',   # y5   高光从左上斜向右下（y3 第4列、y4 第5列、y5 第6列、
    '.@dHHHHhHHdd@.',   # y6   y6 第7列）。中调填充，暗部只贴右侧轮廓。
    '@dddHHHHHHddd@',   # y7   fringe in mid tone / 刘海用中调
    '@Hdds@ss@sddd@',   # y8   eyes; left hair mid tone (lit side) / 眼睛，左侧头发给中调
    '@Hdds@ss@sddd@',   # y9   eyes (2px tall so they read) / 眼睛（2px 高才看得清）
    '@Hddbssssbddd@',   # y10  cheeks / 腮红
    '@Hddsskkssddd@',   # y11  mouth / 嘴
    '.@Hdssssssdd@.',   # y12  chin / 下巴
    '.@bbrrrrrrbb@.',   # y13  neck + collar / 颈与领
    '@sbRrrrrrrRbs@',   # y14  chest / 胸
    '@sbRrrrrrrRbs@',   # y15  chest / 胸
    '@sbRrrrrrrRbs@',   # y16  chest / 胸
    '@sbRggggggRbs@',   # y17  belt / 腰带
    '@sbRRRRRRRRbs@',   # y18  below belt / 腰带下
    '@sss@@@@@@sss@',   # y19  waistline / 腰线
    '.@nnnnnNNNNN@.',   # y20  hips / 胯
    '..@nnnnNNNN@..',   # y21  thighs / 大腿
    '...@nn@@NN@...',   # y22  legs, @@ is the seam / 腿，@@ 是缝
    '...@nn@@NN@...',   # y23
    '...@nn@@NN@...',   # y24
    '...@nn@@NN@...',   # y25
    '...@ww@@WW@...',   # y26  boot tops / 靴筒
    '..@www@@WWW@..',   # y27  boots flare so feet look planted / 靴外扩，脚才像踩实
    '..@www@@WWW@..',   # y28
    '..@www@@WWW@..',   # y29
    '..@@@@..@@@@..',   # y30  soles / 靴底
]

# ---------------------------------------------------------------------------
# Part ownership / 部件归属
#
# Every opaque pixel belongs to exactly ONE part, which is what makes the
# reconstruction invariant hold by construction.
# 每个不透明像素只属于【一个】部件 —— 重建不变量因此构造性成立。
#
#   1 head       y2..y12   whole width
#   2 body       y13..y18  centre columns 3..10
#   3 arm_left   y13..y18  columns 0..2
#   4 arm_right  y13..y18  columns 11..13
#   5 leg_left   y19..y28  columns 0..6
#   6 leg_right  y19..y28  columns 7..13
# ---------------------------------------------------------------------------

HEAD_ROWS = (0, 12)     # inclusive, in ART indices / ART 下标，含端点
TORSO_ROWS = (13, 18)
LEG_ROWS = (19, 29)
ARM_COLS = (0, 2, 11, 13)   # left lo, left hi, right lo, right hi
BODY_COLS = (3, 10)
LEG_SPLIT = 7               # cols 0..6 left, 7..13 right

PART_IDS = {  # name -> id, must match src/utils.cpp / 必须与 src/utils.cpp 一致
    'head': 1, 'body': 2, 'arm_left': 3,
    'arm_right': 4, 'leg_left': 5, 'leg_right': 6,
}

ORIGIN_X, ORIGIN_Y = 9, 1   # 30 rows in 32x32 → 1px top + 1px bottom = 2px gap
                           # 30 行装进 32x32，上下各留 1px，共 2px 空隙


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
