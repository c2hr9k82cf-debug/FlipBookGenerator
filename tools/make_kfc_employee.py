# make_kfc_employee.py — author the KFC-employee three views, pixel by pixel
# 逐像素绘制肯德基员工三视图
#
# Base = the chibi_standard pixel maps (template-aligned by construction);
# a global key->colour recolour turns them into a KFC uniform (red cap and
# apron, white shirt, black trousers), so every template key maps to exactly
# one colour and extractPaletteMap stays conflict-free. Shape stays the
# template silhouette, so the strict pipeline accepts the views.
# 基底 = chibi_standard 像素图（构造上模板对齐）；全局"键->色"换色把它变成肯德基
# 制服（红帽红围裙、白衬衫、黑裤），每个模板键恰好映射一个颜色，extractPaletteMap
# 无冲突。剪影保持模板形状，严格管线可过。
#
# Usage / 用法:
#   python3 -I tools/make_kfc_employee.py [OUT_DIR]
# Default OUT_DIR = ~/Desktop/FlipBook整改测试素材 (the acceptance folder).
# 默认输出到 ~/Desktop/FlipBook整改测试素材（验收文件夹）。

import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TPL_DIR = os.path.join(ROOT, "assets", "templates")

# Template key -> uniform colour (RGB). Keys whose role changes with the
# region (m = eye frame on the head but apron on the torso) are painted per
# row band below; a non-majority colour on a shared key is fine — the
# renderer's deviating rule outputs the user's own colour as long as it is
# one of the palette colours (a map destination).
# 模板键 -> 制服色（RGB）。角色随区域变的键（m 在头部是眼眶、在躯干是围裙）按下方
# 行带分别画；同键画非多数派色没问题 —— 渲染的偏离规则输出用户自己的颜色，只要它
# 是调色板色之一（映射目标色）。
PALETTE = {
    "@": (38, 32, 42),      # outline / 描边
    "h": (200, 38, 48),     # hair_mid -> cap red / 帽红
    "H": (108, 26, 38),     # hair_dark -> cap shadow / 帽阴影
    "y": (226, 220, 212),   # hair_highlight -> cap sheen / 帽高光
    "s": (248, 205, 175),   # skin / 肤
    "b": (242, 156, 136),   # blush / 腮
    "t": (38, 38, 48),      # tan -> far-leg dark / 远腿深黑
    "m": (186, 30, 42),     # mint -> apron red / 围裙红
    "w": (246, 246, 244),   # white -> shirt / 衬衫白
    "r": (228, 110, 98),    # red -> lips+cheeks coral / 唇腮珊瑚
    "p": (54, 54, 66),      # pants -> near-leg black / 近腿黑
    "g": (74, 74, 86),      # grey -> boot / 靴
    "G": (42, 42, 54),      # grey_dark -> boot shadow / 靴影
    "T": (66, 42, 36),      # teal -> eyes / 眼
    ".": None,
}

# Per-band overrides, row i in 0..29 (y = i+2). HEAD rows 0..10 (y2..y12),
# BODY rows 11..29 (y13..y31). / 分带覆盖，行 i 0..29（y=i+2）。头 0..10（y2..y12），
# 身 11..29（y13..y31）。
CAP_RED = (200, 38, 48)      # cap crown + brim / 帽冠+帽檐
HAIR_BLACK = (38, 38, 48)    # sideburns + back hair / 鬓角+后发
SHIRT = (246, 246, 244)      # eye frame + cap badge / 眼眶+帽徽
CAP_ROWS = range(0, 6)       # y2..y7: crown + brim rows / 帽冠+帽檐行
BRIM_ROW = 5                 # y7: the brim is solid cap red / 帽檐整行帽红


def pick(view, i, j, key):
    """Colour for one cell: band-aware key overrides. / 单格取色：分带键覆盖。"""
    if i in CAP_ROWS:
        if i == BRIM_ROW and key in "hHy":
            return CAP_RED
        if key == "H" and view == "back":
            return CAP_RED      # clean cap back (no mottled shadow) / 干净帽后（无斑驳阴影）
        if key == "m":
            return SHIRT        # eye frame (should not occur in cap rows) / 眼眶（帽行内不出现）
        return PALETTE[key]
    if i <= 10:   # y8..y12: face band — hair cells become black sideburns/back hair
        # y8..y12：脸带 —— 发格变黑鬓角/后发
        if key in "hHy":
            return HAIR_BLACK
        if key == "m":
            return SHIRT        # eye frame white / 眼眶白
        if key == "r":
            # back: ears up to y10, the red ribbon below folds into the hair;
            # front: the chin row's r is skin, not lip.
            # back：y10 以下的耳，其下发带红收进发色；front：下巴行的 r 是肤不是唇。
            if view == "back":
                return PALETTE["s"] if i <= 8 else HAIR_BLACK
            if view == "front" and i == 10:
                return PALETTE["s"]
            return PALETTE["r"]
        return PALETTE[key]
    # body band / 身体带
    if key in "hHy":
        return HAIR_BLACK       # loose hair below the cap / 帽下垂发
    if view == "back" and key == "r":
        return HAIR_BLACK       # the back hair ribbon folds into the hair / 后发带收进发色
    return PALETTE[key]


def load_pixels(name):
    """Parse one 30x14 pixel map file. / 解析一份 30x14 像素图文件。"""
    rows = []
    with open(os.path.join(TPL_DIR, name), encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            if len(line) != 14:
                raise SystemExit(f"{name}: bad row width {len(line)}: {line!r}")
            rows.append(line)
    if len(rows) != 30:
        raise SystemExit(f"{name}: {len(rows)} rows, want 30")
    return rows


def render(rows, view):
    """30x14 key rows -> 32x32 RGBA (content at x9..x22, y2..y31)."""
    img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    px = img.load()
    for i, row in enumerate(rows):
        for j, key in enumerate(row):
            col = pick(view, i, j, key)
            if col is None:
                continue
            px[9 + j, 2 + i] = (col[0], col[1], col[2], 255)
    # Cap badge: two white cells at the crown front (y3, centre).
    # 帽徽：帽前两格白（y3，居中）。
    if view in ("front", "side"):
        for x in (15, 16):
            if img.getpixel((x, 3))[3] != 0:
                px[x, 3] = (SHIRT[0], SHIRT[1], SHIRT[2], 255)
    return img


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
        "~/Desktop/FlipBook整改测试素材")
    os.makedirs(out_dir, exist_ok=True)
    for view, fname in (("front", "chibi_standard_pixels.txt"),
                        ("side", "chibi_standard_side_pixels.txt"),
                        ("back", "chibi_standard_back_pixels.txt")):
        img = render(load_pixels(fname), view)
        out = os.path.join(out_dir, f"正向_肯德基员工_{view}.png")
        img.save(out)
        opaque = sum(1 for y in range(32) for x in range(32)
                     if img.getpixel((x, y))[3] != 0)
        print(f"{view}: {opaque} opaque px -> {out}")


if __name__ == "__main__":
    main()
