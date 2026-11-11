#!/usr/bin/env python3
# make_review_sheet.py — build magnified review sheets for the character template
# and every preset animation.
# 生成放大的审阅图：角色模版、部件归属、预设动画效果。
#
# Why this exists / 为什么有这个文件:
#   The template and the animation frames are 32x32. At 1:1 a reviewer cannot
#   see a 1px detach gap, a wrong part boundary, or whether a rotation reads
#   well. These sheets are the human review gate CLAUDE.md calls for ("eyeball
#   the output strips — the single most important manual review"), magnified
#   with nearest-neighbour so nothing is invented by the scaling.
#   模版和动画帧都是 32x32。1:1 下审阅者看不出 1px 脱节、部件边界错误、
#   或旋转是否好看。这些审阅图就是 CLAUDE.md 要求的人工验收关口，
#   用最近邻放大，缩放不发明任何像素。
#
# This is a DEVELOPMENT AID, not product code: it lives in tools/, is never
# linked into flipbook, and its output is gitignored under output/review/.
# 这是开发辅助，不是产品代码：放在 tools/，不参与构建，输出在 output/review/ 下被忽略。
#
# Dependency / 依赖: Pillow (dev-only). The product itself has no Python dependency.
# 仅开发用 Pillow。产品本身不依赖 Python。
#
# Usage / 用法:
#   python3 -I tools/make_review_sheet.py                  # all three views
#   python3 -I tools/make_review_sheet.py --view side       # one view only
#   python3 -I tools/make_review_sheet.py /tmp/peek         # another output dir
#
# Three views, three rigs / 三视图、三套 rig:
#   front rig -> idle + walk_front (walks DOWN / 向下)
#   side  rig -> idle + walk_side  (walks RIGHT / 向右)
#   back  rig -> idle + walk_up    (walks UP / 向上)
# Left is a horizontal mirror of right and is produced at EXPORT time (A6), not
# here. 左走是右走的水平镜像，在【导出】阶段（A6）产出，不在此处。
#
# Output / 输出:  OUT/<view>/*.png + anim_<view>_<preset>.gif
# Frames read from / 帧取自:  output/preview/<view>/<preset>/frame_N.png

import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# --- CLI / 命令行 ---
_argv = sys.argv[1:]
OUT = os.path.join(ROOT, "output", "review")
VIEW_ARG = "all"
TEMPLATE = "chibi_standard"    # character template / 角色模板
_rest = []
_i = 0
while _i < len(_argv):
    if _argv[_i] == "--view":
        if _i + 1 >= len(_argv):
            sys.exit("ERROR: --view needs front|side|back|all")
        VIEW_ARG = _argv[_i + 1]
        _i += 2
    elif _argv[_i] == "--template":
        if _i + 1 >= len(_argv):
            sys.exit("ERROR: --template needs a name (e.g. chibi_standard, chibi_alt)")
        TEMPLATE = _argv[_i + 1]
        _i += 2
    else:
        _rest.append(_argv[_i])
        _i += 1
if _rest:
    OUT = _rest[0]
if VIEW_ARG not in ("front", "side", "back", "all"):
    sys.exit(f"ERROR: unknown view {VIEW_ARG!r}, expected front|side|back|all")

# The output PNG + fixture basename comes from the template JSON's
# "output_basename" (default character_32) — same rule as make_test_character.
# 输出 PNG + 夹具基名来自模板 JSON 的 "output_basename"（默认 character_32）
# —— 与 make_test_character 同一规则。
with open(os.path.join(ROOT, "assets", "templates", TEMPLATE + ".json")) as _f:
    BASE = json.load(_f).get("output_basename", "character_32")

SIZE = 32                      # canonical canvas / 标准画布
ORIGIN_X, ORIGIN_Y = 9, 2      # content placement in canvas / 内容在画布中的位置

VIEWS = ("front", "side", "back")

# One colour per part id, for the ownership tint. Purely cosmetic — this is
# not the character's palette, which lives in the template JSON.
# 每部件一色，用于归属着色。纯装饰 —— 这不是角色调色板（那在 chibi_standard.json）。
PART_COLOURS = {
    1: (255,  99,  71), 2: ( 60, 170, 255), 3: (255, 200,  60),
    4: (255, 140,  26), 5: (150, 220, 120), 6: (200, 130, 255),
}
PART_NAMES = {1: "head", 2: "body", 3: "arm_left",
              4: "arm_right", 5: "leg_left", 6: "leg_right"}

# Each view animates with its own walk preset — the one whose motion matches
# the direction that view reads as. Applying walk_side to a front rig is a
# combination the tool never ships, so it is not reviewed here either.
# 每个视图用【它自己方向】的走路预设。把 walk_side 套到正面 rig 上是本工具从不
# 产出的组合，所以也不在这里审。
VIEW_PRESETS = {
    "front": ["idle", "walk_front"],
    "side":  ["idle", "walk_side"],
    "back":  ["idle", "walk_up"],
}
PRESET_LABELS = {
    "idle":       "idle / 待机",
    "walk_side":  "walk_side / 侧走（→ 右走）",
    "walk_front": "walk_front / 正走（↓ 向下）",
    "walk_up":    "walk_up / 背走（↑ 向上）",
}
VIEW_LABELS = {
    "front": "front / 正面",
    "side":  "side / 侧面",
    "back":  "back / 背面",
}
GIF_MS = {"idle": 125, "walk_side": 100, "walk_front": 100, "walk_up": 100}
# Frame rates match the reference sheet (and src/core/animation.cpp): idle is
# 2 frames @ 4fps = one breath per second, walk is 6 frames @ 10fps = a 600ms
# stride. These drive the GIFs' playback speed.
# 帧率对齐参考图（以及 src/core/animation.cpp）：idle 2 帧 @ 4fps = 一秒一次呼吸，
# walk 6 帧 @ 10fps = 一步 600ms。它们决定 GIF 的播放速度。
PRESET_FPS = {"idle": 4, "walk_side": 10, "walk_front": 10, "walk_up": 10}

# --- Per-view state, set by select_view() / 按视图状态，由 select_view() 设定 ---
#
# The drawing code reads these and needs no per-view branching of its own.
# 绘图代码读它们，自身无需按视图分支。
VIEW = "front"
PART_REGIONS = []   # (name, pid, x, y, w, h, colour) / 部件归属
ANCHORS = []        # (pid, x, y) / 旋转锚点
PRESETS = []        # this view's preset names / 本视图的预设名
BUNDLE = ""         # this view's fixture dir / 本视图的夹具目录


# ---------------------------------------------------------------------------
# Fonts / 字体
# ---------------------------------------------------------------------------

def load_font(size):
    # CJK-capable fonts first: the labels are bilingual, and a font without
    # CJK glyphs renders every Chinese character as a tofu box.
    # CJK 字体放前面：标签是双语的，缺 CJK 字形的字体会把中文全画成豆腐块。
    for p in (
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
        "/Library/Fonts/Arial Unicode.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/Monaco.ttf",
    ):
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except Exception:
                pass
    return ImageFont.load_default()


F_TITLE = load_font(22)
F_BODY = load_font(14)
F_SMALL = load_font(12)
F_TINY = load_font(11)


# ---------------------------------------------------------------------------
# Template loading / 载入模版
# ---------------------------------------------------------------------------

def select_view(view):
    """Load one view's part data into the module globals; return its bundle dir.
    把某个视图的部件数据载入模块全局量，返回它的夹具目录。

    Part regions and pivots are READ BACK from the committed fixture rather
    than kept as constants here. The fixture is the interface the engine
    consumes (CLAUDE.md: "the part bundle is the project's interface"), so
    reading it back is what keeps this review sheet from drifting away from
    what the renderer actually used.
    部件区域与锚点从【已提交的夹具】回读，不在这里写常量。夹具是引擎消费的接口
    （CLAUDE.md："部件包是提取与动画之间的接口"），回读它才使本审阅图不会与渲染器
    实际使用的数据脱节。
    """
    global VIEW, PART_REGIONS, ANCHORS, PRESETS, BUNDLE
    VIEW = view
    suffix = "" if view == "front" else "_" + view
    bundle = os.path.join(ROOT, "tests", "fixtures", BASE + suffix)
    BUNDLE = bundle

    regions = []
    for pid in range(1, 7):
        name = PART_NAMES[pid]
        with open(os.path.join(bundle, f"{name}_bbox.txt")) as f:
            x, y, w, h = (int(v) for v in f.read().split())
        regions.append((name, pid, x, y, w, h, PART_COLOURS[pid]))
    PART_REGIONS = regions

    anchors = []
    with open(os.path.join(bundle, "anchors.txt")) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            pid, x, y = (int(v) for v in line.split())
            anchors.append((pid, x, y))
    ANCHORS = anchors

    PRESETS = list(VIEW_PRESETS[view])
    return bundle


def load_template(view):
    """Render the view's pixel map through the template JSON's palette.
    用模板 JSON 的调色板渲染该视图的像素图。"""
    with open(os.path.join(ROOT, "assets", "templates", TEMPLATE + ".json")) as f:
        spec = json.load(f)
    palette = {k: tuple(v) for k, v in spec["palette"].items()}
    char_map = spec["palette_chars"]

    rows = []
    pixels_file = spec["views"][view]["pixels_file"]
    with open(os.path.join(ROOT, "assets", "templates", pixels_file)) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.lstrip().startswith("#"):
                continue
            rows.append(line)

    assert len(rows) == 30, f"{view}: expected 30 pixel rows, got {len(rows)}"
    assert all(len(r) == 14 for r in rows), f"{view}: each row must be 14 chars wide"

    im = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    px = im.load()
    for ry, row in enumerate(rows):
        y = ORIGIN_Y + ry
        for cx, ch in enumerate(row):
            x = ORIGIN_X + cx
            name = char_map.get(ch)
            if name is None:
                continue
            r, g, b = palette[name]
            px[x, y] = (r, g, b, 255)
    return im, spec


def check_against_asset(rendered):
    """The rendered template must equal assets/<BASE>{,_side,_back}.png.
    渲染出的模版必须与 assets/<BASE>{,_side,_back}.png 逐像素相同。"""
    suffix = "" if VIEW == "front" else "_" + VIEW
    ref = Image.open(os.path.join(ROOT, "assets", f"{BASE}{suffix}.png")).convert("RGBA")
    if ref.size != rendered.size:
        return False, f"size mismatch {ref.size} vs {rendered.size}"
    a, b = ref.load(), rendered.load()
    bad = []
    for y in range(SIZE):
        for x in range(SIZE):
            if a[x, y] != b[x, y]:
                bad.append((x, y, a[x, y], b[x, y]))
    return (not bad), ("" if not bad else f"{len(bad)} differing px, first {bad[0]}")


def check_sprite_sizes():
    """Every part sprite must be exactly its bbox size — loadRigFromDir rejects
    the bundle otherwise, and a sprite that is off by one will be blitted at
    the wrong place with no other symptom.
    每个部件精灵必须与 bbox 尺寸严格一致 —— 否则 loadRigFromDir 会拒收整个部件包；
    而差一个像素的精灵会被贴到错位的地方，且没有别的症状。"""
    problems = []
    for name, pid, x, y, w, h, _c in PART_REGIONS:
        png = os.path.join(BUNDLE, f"{name}.png")
        try:
            im = Image.open(png)
        except Exception as e:
            problems.append(f"{name}: cannot read {png} ({e})")
            continue
        if (im.width, im.height) != (w, h):
            problems.append(f"{name}: sprite {im.width}x{im.height} vs bbox {w}x{h}")
    return problems


# ---------------------------------------------------------------------------
# Drawing helpers / 绘图辅助
# ---------------------------------------------------------------------------

CHECK_A = (200, 204, 210)
CHECK_B = (232, 235, 240)


def checkerboard(w, h, cell=8):
    im = Image.new("RGB", (w, h), CHECK_B)
    d = ImageDraw.Draw(im)
    for yy in range(0, h, cell):
        for xx in range(0, w, cell):
            if ((xx // cell) + (yy // cell)) % 2 == 0:
                d.rectangle([xx, yy, xx + cell - 1, yy + cell - 1], fill=CHECK_A)
    return im


def scale_nn(im, k):
    """Nearest-neighbour magnify. Never bilinear — this is a pixel-art rule.
    最近邻放大。绝不用双线性 —— 这是像素风硬约束。"""
    return im.resize((im.width * k, im.height * k), Image.NEAREST)


def paste_on_checker(im):
    bg = checkerboard(im.width, im.height, cell=max(4, im.width // 16))
    bg.paste(im, (0, 0), im)
    return bg


def draw_grid(d, x0, y0, k, w=SIZE, h=SIZE, minor=(255, 255, 255, 60), major=(255, 255, 255, 150)):
    """Pixel grid over a magnified sprite: every pixel, heavier every 4.
    放大精灵上的像素网格：每像素一根，每 4 像素一根重线。"""
    for i in range(w + 1):
        xx = x0 + i * k
        d.line([xx, y0, xx, y0 + h * k], fill=(major if i % 4 == 0 else minor), width=1)
    for j in range(h + 1):
        yy = y0 + j * k
        d.line([x0, yy, x0 + w * k, yy], fill=(major if j % 4 == 0 else minor), width=1)


def draw_cross(d, cx, cy, r, fill):
    d.line([cx - r, cy, cx + r, cy], fill=fill, width=2)
    d.line([cx, cy - r, cx, cy + r], fill=fill, width=2)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=fill, width=2)


def text(d, xy, s, font, fill=(25, 28, 34)):
    d.text(xy, s, font=font, fill=fill)


# ---------------------------------------------------------------------------
# Sheet 1 — template + part ownership / 模版图 + 部件归属
# ---------------------------------------------------------------------------

def build_template_sheet(rendered):
    k = 12
    pw, ph = SIZE * k, SIZE * k
    gap, pad, title_h, label_h = 28, 28, 52, 30
    W = pad * 2 + pw * 2 + gap
    H = title_h + label_h + ph + pad + 128

    sheet = Image.new("RGB", (W, H), (248, 249, 251))
    d = ImageDraw.Draw(sheet, "RGBA")

    text(d, (pad, 14), f"{TEMPLATE} — {VIEW_LABELS[VIEW]} template / 模版图（32x32，12x 最近邻放大）", F_TITLE)

    # Panel A: the template itself on a checkerboard, with pixel grid.
    # 面板 A：模版本体，棋盘格底 + 像素网格。
    ax, ay = pad, title_h + label_h
    sheet.paste(paste_on_checker(rendered).resize((pw, ph), Image.NEAREST), (ax, ay))
    d.rectangle([ax - 1, ay - 1, ax + pw, ay + ph], outline=(120, 126, 136), width=1)
    draw_grid(d, ax, ay, k)
    text(d, (ax, title_h + 6), "A  template pixels / 模版像素", F_BODY)

    # Panel B: part ownership, tinted over the same pixels.
    # 面板 B：部件归属，在同样像素上着色。
    bx, by = ax + pw + gap, title_h + label_h
    base = paste_on_checker(rendered).resize((pw, ph), Image.NEAREST)
    tint = Image.new("RGBA", (pw, ph), (0, 0, 0, 0))
    td = ImageDraw.Draw(tint)
    for name, pid, x, y, w, h, col in PART_REGIONS:
        td.rectangle([x * k, y * k, (x + w) * k - 1, (y + h) * k - 1],
                     fill=col + (110,))
    sheet.paste(Image.alpha_composite(base.convert("RGBA"), tint).convert("RGB"), (bx, by))
    d.rectangle([bx - 1, by - 1, bx + pw, by + ph], outline=(120, 126, 136), width=1)
    draw_grid(d, bx, by, k)
    # Part boundary outlines + anchors. Offsets are panel-relative: bx/by were
    # missing here once and the boxes drew over panel A instead.
    # 部件边界框 + 锚点。坐标要相对面板：这里曾漏了 bx/by，框画到了面板 A 上。
    for name, pid, x, y, w, h, col in PART_REGIONS:
        d.rectangle([bx + x * k, by + y * k,
                     bx + (x + w) * k - 1, by + (y + h) * k - 1],
                    outline=col + (255,), width=2)
    for pid, ax_, ay_ in ANCHORS:
        draw_cross(d, bx + ax_ * k + k // 2, by + ay_ * k + k // 2, 9, (20, 20, 20, 255))
        draw_cross(d, bx + ax_ * k + k // 2, by + ay_ * k + k // 2, 6, (255, 255, 255, 255))
    text(d, (bx, title_h + 6), "B  part ownership + pivots / 部件归属 + 旋转锚点", F_BODY)

    # Legend: 3 columns x 2 rows so nothing runs off the sheet.
    # 图例：3 列 x 2 行，避免文字跑出图外。
    ly = title_h + label_h + ph + 16
    for i, (name, pid, x, y, w, h, col) in enumerate(PART_REGIONS):
        lx = pad + (i % 3) * 268
        lyy = ly + (i // 3) * 24
        d.rectangle([lx, lyy + 3, lx + 14, lyy + 17], fill=col, outline=(90, 95, 105))
        text(d, (lx + 20, lyy + 1), f"{pid} {name}  {w}x{h} @({x},{y})", F_SMALL)
    text(d, (pad, ly + 52), "anchor = rotation pivot, one per part / 锚点 = 旋转轴，每部件一个", F_SMALL, (90, 95, 105))
    text(d, (pad, ly + 72), "reconstruction gate: 6 parts blit back pixel-exact (0 diff) / 重建门禁：6 部件贴回逐像素一致（0 差异）", F_SMALL, (40, 120, 70))
    text(d, (pad, ly + 92), "grid: light = 1px, heavy = 4px  ·  magnification is nearest-neighbour / 网格：细=1px，粗=4px · 放大为最近邻", F_SMALL, (90, 95, 105))

    out = out_path("template_sheet.png")
    sheet.save(out)
    return out


# ---------------------------------------------------------------------------
# Sheet 2 — part breakdown + pivots / 部件拆解 + 锚点
# ---------------------------------------------------------------------------

def build_parts_sheet(rendered, bundle_dir):
    k = 8
    cell = SIZE * k
    gap, pad, title_h = 20, 28, 52
    cols, rows = 3, 2
    W = pad * 2 + cols * cell + (cols - 1) * gap
    H = title_h + rows * (cell + 56) + 30

    sheet = Image.new("RGB", (W, H), (248, 249, 251))
    d = ImageDraw.Draw(sheet, "RGBA")
    text(d, (pad, 14), f"parts + pivots — {VIEW_LABELS[VIEW]} / 部件拆解 + 锚点（8x 最近邻）", F_TITLE)

    for idx, (name, pid, x, y, w, h, col) in enumerate(PART_REGIONS):
        cx = pad + (idx % cols) * (cell + gap)
        cy = title_h + (idx // cols) * (cell + 56)

        # Crop the part exactly as extraction does: sprite = bbox-sized crop.
        # 按提取时的方式裁部件：精灵 = bbox 尺寸的裁剪图。
        crop = rendered.crop((x, y, x + w, y + h))
        panel = paste_on_checker(scale_nn(crop, k))
        # Scale up into a 32x32 cell so all panels share a size and the part
        # sits at its true position. 放进 32x32 单元，使所有面板同尺寸且部件在真实位置。
        stage = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
        stage.paste(crop.resize((w * k, h * k), Image.NEAREST), (x * k, y * k))
        panel = paste_on_checker(stage)
        d.rectangle([cx - 1, cy - 1, cx + cell, cy + cell], outline=(120, 126, 136), width=1)
        draw_grid(d, cx, cy, k, minor=(255, 255, 255, 40), major=(255, 255, 255, 110))
        d.rectangle([cx + x * k, cy + y * k, cx + (x + w) * k - 1, cy + (y + h) * k - 1],
                    outline=col + (255,), width=2)
        for p2, ax_, ay_ in ANCHORS:
            if p2 == pid:
                draw_cross(d, cx + ax_ * k + k // 2, cy + ay_ * k + k // 2, 10, (20, 20, 20, 255))
                draw_cross(d, cx + ax_ * k + k // 2, cy + ay_ * k + k // 2, 7, (255, 255, 255, 255))
        sheet.paste(panel, (cx, cy))

        png = os.path.join(bundle_dir, f"{name}.png")
        try:
            part = Image.open(png).convert("RGBA")
            note = f"{part.width}x{part.height} sprite, bbox {w}x{h}"
        except Exception:
            note = f"bbox {w}x{h} (sprite not found)"
        text(d, (cx, cy + cell + 6), f"{pid} {name}", F_BODY, (25, 28, 34))
        text(d, (cx, cy + cell + 26), f"bbox=({x},{y}) {w}x{h}   pivot=({[a[1] for a in ANCHORS if a[0]==pid][0]},{[a[2] for a in ANCHORS if a[0]==pid][0]})", F_TINY, (95, 100, 110))
        text(d, (cx, cy + cell + 42), note, F_TINY, (95, 100, 110))

    out = out_path("parts_sheet.png")
    sheet.save(out)
    return out


# ---------------------------------------------------------------------------
# Sheet 3 — every preset, every frame / 全部预设全部帧
# ---------------------------------------------------------------------------

def out_path(name):
    """Every sheet lands in OUT/<view>/ so three views never overwrite each
    other. 每张审阅图都落在 OUT/<view>/ 下，三个视图互不覆盖。"""
    d = os.path.join(OUT, VIEW)
    os.makedirs(d, exist_ok=True)
    return os.path.join(d, name)


def load_frames(name):
    """Frames the engine wrote for THIS view's bundle + this preset.
    引擎为【本视图】的部件包 + 该预设写出的帧。"""
    paths = []
    i = 0
    while True:
        p = os.path.join(ROOT, "output", "preview", VIEW, name, f"frame_{i}.png")
        if not os.path.exists(p):
            break
        paths.append(p)
        i += 1
    return [Image.open(p).convert("RGBA") for p in paths]


def build_review_sheet():
    k = 5
    fw = SIZE * k
    gap, pad, title_h, label_w, row_gap = 8, 24, 54, 236, 30
    frames = max(len(load_frames(n)) for n in PRESETS)

    W = pad * 2 + label_w + frames * (fw + gap)
    H = title_h + len(PRESETS) * (fw + row_gap) + 46

    sheet = Image.new("RGB", (W, H), (248, 249, 251))
    d = ImageDraw.Draw(sheet, "RGBA")
    text(d, (pad, 12), f"preset animations — {VIEW_LABELS[VIEW]} every frame / 预设动画逐帧（5x 最近邻）", F_TITLE)
    text(d, (pad, 36), "watch for: detach gaps at joints, missing pixels, wrong z-order, chunky rotation reads as intended / "
                       "重点看：关节脱节缝、丢像素、层级错误；非 90° 旋转的锯齿感是像素风惯例，不是 bug", F_SMALL, (95, 100, 110))

    for r, name in enumerate(PRESETS):
        ry = title_h + r * (fw + row_gap)
        text(d, (pad, ry + 10), PRESET_LABELS[name], F_BODY)
        fs = load_frames(name)
        text(d, (pad, ry + 32), f"{len(fs)} frames @ {PRESET_FPS[name]}fps", F_SMALL, (95, 100, 110))
        text(d, (pad, ry + 50), f"→ output/preview/{VIEW}/{name}/", F_TINY, (150, 155, 165))
        for i, fr in enumerate(fs):
            fx = pad + label_w + i * (fw + gap)
            sheet.paste(paste_on_checker(scale_nn(fr, k)), (fx, ry))
            d.rectangle([fx - 1, ry - 1, fx + fw, ry + fw], outline=(150, 155, 165), width=1)
            text(d, (fx + 3, ry + fw + 4), f"f{i}", F_TINY, (120, 125, 135))
        # Frame 0 outline in accent so the loop seam is easy to find.
        fx0 = pad + label_w
        d.rectangle([fx0 - 2, ry - 2, fx0 + fw + 1, ry + fw + 1], outline=(230, 120, 40), width=2)

    out = out_path("review_sheet.png")
    sheet.save(out)
    return out


# ---------------------------------------------------------------------------
# Sheet 4 — joint close-ups / 关节特写
# ---------------------------------------------------------------------------

# Two observation windows over the canonical canvas: where a detach gap would
# appear. 16 px wide covers both shoulders and both hips at once.
# 两个观察窗，覆盖脱节会出现在哪里。16px 宽同时含双肩和双髋。
JOINT_WINDOWS = [
    ("torso / 躯干  y9-19", 8, 9, 16, 11),
    ("hips+legs / 髋腿  y18-28", 8, 18, 16, 11),
]
def build_joint_sheet():
    k = 8
    ww, wh = 16 * k, 11 * k
    gap, pad, title_h, label_w, row_gap = 6, 24, 56, 176, 8
    cols = 8

    W = pad * 2 + label_w + cols * (ww + gap)
    rows = len(PRESETS) * len(JOINT_WINDOWS)
    H = title_h + rows * (wh + 22 + row_gap) + 24

    sheet = Image.new("RGB", (W, H), (248, 249, 251))
    d = ImageDraw.Draw(sheet, "RGBA")
    text(d, (pad, 12), f"joint close-ups — {VIEW_LABELS[VIEW]} / 关节特写（8x 最近邻）", F_TITLE)
    text(d, (pad, 38), "the ugly artifact is a 1px gap at a joint / 最难看的产物是关节处 1px 缝 —— "
                       "看每格的肩、颈、髋处有没有背景色漏进来", F_SMALL, (95, 100, 110))

    r = 0
    for name in PRESETS:
        fs = load_frames(name)
        for wlabel, wx, wy, ww_, wh_ in JOINT_WINDOWS:
            ry = title_h + r * (wh + 22 + row_gap)
            text(d, (pad, ry + 6), f"{PRESET_LABELS[name]}", F_SMALL, (25, 28, 34))
            text(d, (pad, ry + 22), wlabel, F_SMALL, (95, 100, 110))
            for i, fr in enumerate(fs):
                crop = fr.crop((wx, wy, wx + ww_, wy + wh_))
                fx = pad + label_w + i * (ww + gap)
                sheet.paste(paste_on_checker(scale_nn(crop, k)), (fx, ry))
                d.rectangle([fx - 1, ry - 1, fx + ww, ry + wh], outline=(150, 155, 165), width=1)
                draw_grid(d, fx, ry, k, minor=(255, 255, 255, 30), major=(255, 255, 255, 90))
                text(d, (fx + 3, ry + wh + 3), f"f{i}", F_TINY, (120, 125, 135))
            r += 1

    out = out_path("joint_sheet.png")
    sheet.save(out)
    return out


# ---------------------------------------------------------------------------
# Automated detach check / 自动脱节检测
# ---------------------------------------------------------------------------

def analyse_frame(im):
    """(bbox, components, holes) for one RGBA frame.
    A healthy frame is 1 connected opaque blob with 0 enclosed transparent
    holes: a detach gap shows up as a hole or as a second blob.
    健康的帧 = 1 个连通不透明块 + 0 个封闭透明洞。脱节表现为洞或第二块。"""
    px = im.load()
    opaque = [[px[x, y][3] > 0 for x in range(SIZE)] for y in range(SIZE)]

    xs = [x for y in range(SIZE) for x in range(SIZE) if opaque[y][x]]
    ys = [y for y in range(SIZE) for x in range(SIZE) if opaque[y][x]]
    bbox = (min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1) if xs else None

    # Opaque side: 8-connectivity (diagonal touch counts as attached).
    # 不透明侧用 8 连通（斜角接触算相连）。
    seen = [[False] * SIZE for _ in range(SIZE)]
    comps = 0
    for y0 in range(SIZE):
        for x0 in range(SIZE):
            if not opaque[y0][x0] or seen[y0][x0]:
                continue
            comps += 1
            seen[y0][x0] = True
            stack = [(x0, y0)]
            while stack:
                cx, cy = stack.pop()
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < SIZE and 0 <= ny < SIZE and opaque[ny][nx] and not seen[ny][nx]:
                            seen[ny][nx] = True
                            stack.append((nx, ny))

    # Transparent side: 4-connectivity (its dual), flood from the border.
    # 透明侧用 4 连通（对偶），从边界灌入。
    reach = [[False] * SIZE for _ in range(SIZE)]
    stack = []
    for i in range(SIZE):
        for x, y in ((i, 0), (i, SIZE - 1), (0, i), (SIZE - 1, i)):
            if not opaque[y][x] and not reach[y][x]:
                reach[y][x] = True
                stack.append((x, y))
    while stack:
        cx, cy = stack.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = cx + dx, cy + dy
            if 0 <= nx < SIZE and 0 <= ny < SIZE and not opaque[ny][nx] and not reach[ny][nx]:
                reach[ny][nx] = True
                stack.append((nx, ny))
    holes = sum(1 for y in range(SIZE) for x in range(SIZE)
                if not opaque[y][x] and not reach[y][x])
    return bbox, comps, holes


def run_detach_check():
    """Print one line per preset per frame. Fails loud on gaps or holes.
    每预设每帧一行。有缝或有洞就大声报错。"""
    print()
    print(f"[detach:{VIEW}] frame analysis — expect 1 blob, 0 holes / 期望 1 块、0 洞")
    any_bad = False
    for name in PRESETS:
        fs = load_frames(name)
        widths = []
        lines = []
        for i, fr in enumerate(fs):
            bbox, comps, holes = analyse_frame(fr)
            w = bbox[2] if bbox else 0
            widths.append(w)
            flag = ""
            if comps != 1:
                flag += f"  !! {comps} blobs (detach?)"
            if holes:
                flag += f"  !! {holes} holes (gap?)"
            if flag:
                any_bad = True
            lines.append(f"    f{i}  bbox={bbox}  blobs={comps} holes={holes}{flag}")
        spread = max(widths) - min(widths)
        print(f"  {name}: width {min(widths)}..{max(widths)} px (spread {spread})")
        for ln in lines:
            print(ln)
    print("  ->", "FAIL: gaps detected / 检出脱节" if any_bad
          else "PASS: no gaps, no holes — parts stay attached / 无脱节无破洞")
    return not any_bad

# ---------------------------------------------------------------------------
# GIFs — the only way to judge motion / 动图：判断动作的唯一方式
# ---------------------------------------------------------------------------

def build_gifs():
    outs = []
    for name in PRESETS:
        fs = load_frames(name)
        if not fs:
            continue
        k = 10
        big = [paste_on_checker(scale_nn(f, k)) for f in fs]
        # Convert to palette with a shared adaptive palette across all frames
        # so the colours do not shimmer. 所有帧共用调色板，避免颜色闪烁。
        base = big[0].convert("P", palette=Image.ADAPTIVE, colors=64)
        pframes = [im.quantize(palette=base, dither=Image.NONE) for im in big]
        out = out_path(f"anim_{VIEW}_{name}.gif")
        pframes[0].save(
            out,
            save_all=True,
            append_images=pframes[1:],
            duration=GIF_MS[name],
            loop=0,
            optimize=False,
            disposal=2,
        )
        outs.append(out)
    return outs


# ---------------------------------------------------------------------------
# Main / 主流程
# ---------------------------------------------------------------------------

def review_view(view):
    """All sheets plus the detach gate for one view. Returns the gate's verdict.
    一个视图的全部审阅图加脱节闸门。返回闸门结论。"""
    suffix = "" if view == "front" else "_" + view
    bundle = select_view(view)
    rendered, _spec = load_template(view)

    ok, detail = check_against_asset(rendered)
    print(f"[template] render vs assets/{BASE}{suffix}.png: "
          f"{'PASS (pixel-exact)' if ok else 'FAIL — ' + detail}")
    if not ok:
        print("  !! template is NOT the committed asset; fix before judging animation")

    size_problems = check_sprite_sizes()
    print(f"[sprites]  part sprite size vs bbox: "
          f"{'PASS (all 6 match)' if not size_problems else 'FAIL'}")
    for p in size_problems:
        print("  !!", p)

    print()
    print("template :", build_template_sheet(rendered))
    print("parts    :", build_parts_sheet(rendered, bundle))
    print("review   :", build_review_sheet())
    print("joints   :", build_joint_sheet())
    for g in build_gifs():
        print("gif      :", g)
    return run_detach_check()


def main():
    os.makedirs(OUT, exist_ok=True)
    views = VIEWS if VIEW_ARG == "all" else (VIEW_ARG,)

    all_ok = True
    for view in views:
        bundle_name = BASE + ("" if view == "front" else "_" + view)
        print()
        print("=" * 76)
        print(f"  {VIEW_LABELS[view]}   rig = tests/fixtures/{bundle_name}")
        print("=" * 76)
        if not review_view(view):
            all_ok = False

    print()
    print(f"Output dir / 输出目录: {OUT}")
    if not all_ok:
        print("DETACH GATE FAILED / 脱节闸门失败")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
