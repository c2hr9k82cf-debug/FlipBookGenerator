# make_characters.py — author template-aligned character three views, pixel maps recoloured
# 逐像素生成模板对齐的角色三视图（模板像素图换色）
#
# One character = one uniform table (key -> colour) applied to the
# chibi_standard pixel maps, with band-aware overrides (cap rows / face band /
# body band) so a shared template key can wear the right colour per region —
# the renderer's deviating rule keeps those colours verbatim as long as they
# are palette destinations. Silhouette is byte-equal to the template, so both
# the strict and the tolerant pipelines accept the views.
# 一个角色 = 一张制服色表（键 -> 色）套在 chibi_standard 像素图上，外加分带覆盖
# （帽行/脸带/身体带），同一个模板键可按区域穿对颜色 —— 渲染的偏离规则把这些颜色
# 原样保留（只要是映射目标色）。剪影与模板逐字节一致，严格/宽容管线均可过。
#
# Usage / 用法:
#   python3 -I tools/make_characters.py [OUT_DIR]
# Default OUT_DIR = <project>/assets/characters. Writes {name}_{view}.png
# (front/side/back) per character plus chars_preview.png (10x strip grid).
# 默认输出 <项目>/assets/characters。每角色写 {名字}_{view}.png（front/side/back）
# 外加 chars_preview.png（10 倍放大拼图）。

import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TPL_DIR = os.path.join(ROOT, "assets", "templates")

EYE_FRAME = (246, 246, 244)   # eye whites / 眼眶白
CAP_ROWS = range(0, 6)        # y2..y7: crown + brim rows / 帽冠+帽檐行
BRIM_ROW = 5                  # y7: the brim row / 帽檐行
FACE_TOP = 10                 # rows 6..10 = y8..y12 / 脸带

# name -> uniform table. / 名字 -> 制服色表。
CHARACTERS = {
    "肯德基员工": dict(
        cap=(200, 38, 48), cap_dark=(108, 26, 38), sheen=(226, 220, 212),
        hair=(38, 38, 48), apron=(186, 30, 42), shirt=(246, 246, 244),
        lip=(228, 110, 98), pants=(54, 54, 66), pants_far=(38, 38, 48),
        boot=(74, 74, 86), boot_dark=(42, 42, 54), eye=(66, 42, 36),
        skin=(248, 205, 175), blush=(242, 156, 136), outline=(38, 32, 42),
        badge=(246, 246, 244)),
    "麦当劳员工": dict(
        cap=(226, 52, 42), cap_dark=(158, 32, 28), sheen=(245, 190, 70),
        hair=(96, 64, 42), apron=(240, 190, 55), shirt=(226, 52, 42),
        lip=(220, 110, 100), pants=(60, 50, 48), pants_far=(44, 38, 36),
        boot=(90, 75, 60), boot_dark=(60, 48, 38), eye=(70, 48, 34),
        skin=(248, 205, 175), blush=(242, 156, 136), outline=(38, 30, 26),
        badge=(245, 200, 80)),
    "警察": dict(
        cap=(42, 62, 112), cap_dark=(28, 42, 80), sheen=(120, 150, 195),
        hair=(30, 30, 38), apron=(48, 72, 128), shirt=(228, 230, 238),
        lip=(216, 120, 110), pants=(30, 34, 46), pants_far=(22, 26, 36),
        boot=(24, 24, 30), boot_dark=(16, 16, 22), eye=(40, 32, 28),
        skin=(248, 205, 175), blush=(242, 156, 136), outline=(30, 32, 42),
        badge=(238, 198, 92)),
    "护士": dict(
        cap=(250, 250, 250), cap_dark=(214, 220, 230), sheen=(255, 255, 255),
        hair=(110, 80, 58), apron=(238, 241, 248), shirt=(176, 212, 232),
        lip=(228, 116, 116), pants=(150, 185, 212), pants_far=(120, 158, 190),
        boot=(238, 238, 244), boot_dark=(198, 204, 214), eye=(66, 48, 40),
        skin=(248, 205, 175), blush=(242, 156, 136), outline=(38, 42, 52),
        badge=(212, 58, 58)),
    "魔法师": dict(
        cap=(92, 62, 152), cap_dark=(62, 42, 112), sheen=(244, 214, 110),
        hair=(226, 226, 236), apron=(72, 52, 136), shirt=(242, 216, 120),
        lip=(214, 110, 120), pants=(52, 38, 102), pants_far=(40, 28, 80),
        boot=(70, 50, 126), boot_dark=(46, 32, 88), eye=(88, 60, 140),
        skin=(248, 205, 175), blush=(242, 156, 136), outline=(32, 26, 48),
        badge=None),
}


def palette_of(u):
    """Template key -> base colour. / 模板键 -> 基础色。"""
    return {
        "@": u["outline"], "h": u["cap"], "H": u["cap_dark"], "y": u["sheen"],
        "s": u["skin"], "b": u["blush"], "t": u["pants_far"], "m": u["apron"],
        "w": u["shirt"], "r": u["lip"], "p": u["pants"], "g": u["boot"],
        "G": u["boot_dark"], "T": u["eye"], ".": None,
    }


def pick(u, view, i, key):
    """Colour for one cell: band-aware key overrides. / 单格取色：分带键覆盖。"""
    base = palette_of(u)
    if i in CAP_ROWS:
        if i == BRIM_ROW and key in "hHy":
            return u["cap"]              # solid brim / 整行帽檐
        if key == "H" and view == "back":
            return u["cap"]              # clean cap back / 干净帽后
        return base[key]
    if i <= FACE_TOP:                    # face band / 脸带
        if key in "hHy":
            return u["hair"]             # sideburns / back hair / 鬓角后发
        if key == "m":
            return EYE_FRAME             # eye whites / 眼眶白
        if key == "r":
            # back: ears up to y10, ribbon below folds into the hair; front:
            # the chin row is skin, not lip. / back: y10 以下为耳，其下带红收进
            # 发色；front: 下巴行是肤不是唇。
            if view == "back":
                return u["skin"] if i <= 8 else u["hair"]
            if view == "front" and i == 10:
                return u["skin"]
            return u["lip"]
        return base[key]
    # body band / 身体带
    if key in "hHy":
        return u["hair"]                 # loose hair below the cap / 帽下垂发
    if view == "back" and key == "r":
        return u["hair"]                 # back hair ribbon folds into the hair / 后发带收进发色
    return base[key]


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


VIEWS = (("front", "chibi_standard_pixels.txt"),
         ("side", "chibi_standard_side_pixels.txt"),
         ("back", "chibi_standard_back_pixels.txt"))


def render(u, rows, view):
    """30x14 key rows -> 32x32 RGBA (content at x9..x22, y2..y31)."""
    img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    px = img.load()
    for i, row in enumerate(rows):
        for j, key in enumerate(row):
            col = pick(u, view, i, key)
            if col is None:
                continue
            px[9 + j, 2 + i] = (col[0], col[1], col[2], 255)
    # Cap badge: two cells at the crown front (y3, centre), front + side only.
    # 帽徽：帽前两格（y3，居中），只在正面与侧面。
    if u["badge"] and view in ("front", "side"):
        bc = u["badge"]
        for x in (15, 16):
            if img.getpixel((x, 3))[3] != 0:
                px[x, 3] = (bc[0], bc[1], bc[2], 255)
    return img


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "assets", "characters")
    os.makedirs(out_dir, exist_ok=True)
    pix = {name: load_pixels(fname) for _, fname in VIEWS for name in [fname]}
    made = []
    for cname, u in CHARACTERS.items():
        for view, fname in VIEWS:
            img = render(u, pix[fname], view)
            out = os.path.join(out_dir, f"{cname}_{view}.png")
            img.save(out)
        made.append(cname)
        print(f"{cname}: front/side/back written")
    # 10x preview grid: one row per character (front side back).
    # 10 倍预览拼图：每角色一行（正 侧 背）。
    Z, GAP = 10, 4
    cell_w, cell_h = 32 * 3 + GAP * 2, 32
    grid = Image.new("RGBA", (cell_w, (32 + GAP) * len(made)),
                     (188, 188, 196, 255))
    for r, cname in enumerate(made):
        x = 0
        for view, _ in VIEWS:
            im = Image.open(os.path.join(out_dir, f"{cname}_{view}.png"))
            grid.paste(im, (x, r * (32 + GAP)), im)
            x += 32 + GAP
    grid = grid.resize((grid.width * Z, grid.height * Z), Image.NEAREST)
    prev = os.path.join(out_dir, "chars_preview.png")
    grid.save(prev)
    print("wrote", prev)


if __name__ == "__main__":
    main()
