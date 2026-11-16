#!/usr/bin/env python3
# make_test_kit.py — V0.12.0: generate the fresh debug kit (整改测试素材).
# 生成全新调试素材包（V0.12.0 整改测试素材）。
#
# Why / 为什么:
#   The user asked for brand-new material that follows the chibi_standard
#   rules, to drag into the app and debug with. One kit covers every path the
#   V0.11 overhaul touched:
#   用户要求按 chibi_standard 规范生成全新素材，拖进应用调试。一套素材覆盖
#   V0.11 整改涉及的每条路径：
#
#   * positive: a NEW character (bob haircut, full recolour) in three
#     template-aligned views — drop in, get a four-direction sheet.
#     正向：新角色（波波头 + 全身换色）三张模板对齐视图 —— 拖入即出四方向大图。
#   * tolerant: a mixed/anti-aliased variant (majority-mapping must win),
#     a shifted view (auto-align must fix it), a non-32x32 canvas (best-
#     effort align + warning, never a hard reject).
#     宽容：混色/抗锯齿样图（多数派映射须胜出）、错位图（自动对齐须修正）、
#     非 32×32 画布（尽力对齐 + 提醒，绝不硬拒）。
#   * parts: an importable new hairstyle and a new top, per the colour-slot
#     rules (collar painted into the collar band; no gold/hair_highlight
#     same-RGB trap — those two roles share RGB and only region
#     disambiguates them). v0.12.0: the top hugs the top region box (plan
#     D1) and the hairstyle ships TWO pieces (front fringe/crown + back-layer
#     rear mass, plan D2) — asserted inside their boxes at generation time.
#     部件：可导入的新发型与新上衣，符合颜色-槽位规则（领口画进领口带；
#     规避 gold/hair_highlight 同 RGB 陷阱 —— 两角色同色、仅靠区域消歧）。
#     v0.12.0：上衣收进上衣轮廓框（计划 D1）、发型出双片（前发刘海/顶发 +
#     垫底后发团，计划 D2）—— 生成时即断言坐在框内。
#
# The mixed variant is ALSO written to tests/fixtures/test_kit/ so the
# tolerant palette assertions can consume it headlessly.
# 混色样图同时写入 tests/fixtures/test_kit/，供宽容配色断言无头消费。
#
# Constraints honoured / 遵守的硬约束:
#   * chibi_standard spec: 15-colour palette, content x9-x22 y2-y31,
#     30x14 pixel art per view. / 规范：15 色、内容 x9-x22 y2-y31、每视图 30×14。
#   * Every part pixel is a template role colour (the role constraint).
#     每个部件像素都是模板角色色（role 约束）。
#   * Pure stdlib PNG writer, no Pillow dependency (mirrors
#     make_test_character.py). / 纯标准库写 PNG，零依赖。
#
# Run with: python3 -I tools/make_test_kit.py [--out DIR]
# Untrusted-input rule: run with -I. / 不可信输入规则：用 -I 运行。

import json
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEMPLATES = ROOT / "assets" / "templates"
REGIONS_JSON = TEMPLATES / "chibi_regions.json"
DEFAULT_OUT = Path.home() / "Desktop" / "FlipBook整改测试素材"
FIXTURE_OUT = ROOT / "tests" / "fixtures" / "test_kit"

# ---------------------------------------------------------------------------
# The bob-haircut variant head (rows y2..y12), hand-authored pixel by pixel.
# Same 30x14 grammar as chibi_standard_pixels.txt; body/leg rows (y13..y31)
# are reused verbatim from the standard views.
# 波波头变体头部（y2..y12 行），逐像素手工绘制。与标准像素图同为 30×14 语法；
# 身体/腿行（y13..y31）原样复用标准视图。
# ---------------------------------------------------------------------------

BOB_HEAD = {
    "front": [
        "....@@@@@@....",   # y2  round crown / 圆顶
        "..@@hhhhhh@@..",   # y3  hair fill / 发体
        ".@hhhhhhhhhh@.",   # y4
        ".@HhhhhhhhhH@.",   # y5
        ".@HHhhhhhhHH@.",   # y6  bob reaches the ears / 波波头齐耳
        ".@HHHHHHHHHH@.",   # y7  straight fringe over the brows / 齐刘海盖眉
        ".@H@mTssTm@H@.",   # y8  eyes below the fringe / 刘海下的眼
        ".@Hbw@ss@wbH@.",   # y9
        ".@HrssssssrH@.",   # y10 cheeks / 腮红
        ".@HHHrssrHHH@.",   # y11 mouth / 嘴
        "..@HHHrrHHH@..",   # y12 chin, hair closing / 下巴，发收
    ],
    "side": [
        ".....@@@@@....",   # y2
        "....@hhhy@....",   # y3
        "...@hhhhhh@...",   # y4
        "..@HhhhhhhH@..",   # y5
        "..@HHhhhHHH@..",   # y6
        "..@HH@@@@HH@..",   # y7  fringe edge + sideburn line / 刘海缘 + 鬓角
        "..@HH@TssHH@..",   # y8  side eye / 侧眼
        "..@HHbss@HH@..",   # y9
        "..@HHrsssH@...",   # y10
        "..@HHHrrrH@...",   # y11
        "..@HHHrbbrH@..",   # y12
    ],
    "back": [
        "....@@@@@@....",   # y2
        "...@hhhhhh@...",   # y3
        "..@hhhhhhhh@..",   # y4
        ".@HhhhhhhhhH@.",   # y5
        ".@HHhhhhhhHH@.",   # y6
        ".@HHHHHHHHHH@.",   # y7  the bob's back edge / 波波头后缘
        ".@HHHhhhhHHH@.",   # y8
        ".@HHHhhhhHHH@.",   # y9
        ".@HHHhhhhHHH@.",   # y10
        "..@HHhhhHH@...",   # y11
        "...@HHHHH@....",   # y12
    ],
}

# Full recolour of the 15 template roles (1:1, so strict extraction works).
# The character is unmistakably NOT the reference one.
# 15 个模板角色的整体换色（1:1，严格提取也成立）。一眼就不是参考角色。
RECOLOUR = {
    "outline": (40, 35, 60),
    "hair_mid": (86, 120, 220),
    "hair_dark": (58, 82, 170),
    "hair_highlight": (140, 170, 255),
    "skin": (255, 224, 196),
    "blush": (255, 190, 170),
    "tan": (210, 160, 110),
    "mint": (255, 230, 150),
    "white": (255, 255, 255),
    "red": (200, 70, 120),
    "pants": (90, 90, 110),
    "grey": (150, 160, 190),
    "grey_dark": (100, 105, 135),
    "gold": (140, 170, 255),   # template alias of hair_highlight / 模板里即同色
    "teal": (40, 60, 120),
}

# The minority colour planted in the mixed variant: the midpoint between the
# hair_mid and hair_dark user colours. Same reference pixel role, two user
# colours -> the majority mapping must win (v0.11.0 tolerant rule).
# 混色样图里种的少数派色：hair_mid 与 hair_dark 用户色的中点。同一参考角色色、
# 两个用户色 —— 多数派映射必须胜出（v0.11.0 宽容规则）。
MIXED_MINORITY = (72, 101, 195)


def load_template():
    """(spec, palette_chars->rgb, rows per view) from chibi_standard*.
    从 chibi_standard* 读（spec，字符->RGB，各视图像素行）。"""
    with open(TEMPLATES / "chibi_standard.json", encoding="utf-8") as f:
        spec = json.load(f)
    palette = {k: tuple(v) for k, v in spec["palette"].items()}
    char_rgb = {ch: (palette[role] if role else None)
                for ch, role in spec["palette_chars"].items()}
    rows = {}
    for view in ("front", "side", "back"):
        path = TEMPLATES / spec["views"][view]["pixels_file"]
        art = []
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.rstrip("\n")
                if not line or line.lstrip().startswith("#"):
                    continue
                art.append(line)
        assert len(art) == 30, f"{path}: expected 30 art rows, got {len(art)}"
        rows[view] = art
    return spec, char_rgb, rows


def build_frame(view, char_rgb, std_rows, recolour, head_override=True):
    """One 32x32 RGBA frame: bob head (or standard), standard body/legs,
    recoloured. Returns [[(r,g,b,a)...]] in row-major 32x32.
    一帧 32×32 RGBA：波波头（或标准头）+ 标准身体腿，整体换色。"""
    art = list(std_rows[view])
    if head_override:
        art[:11] = BOB_HEAD[view]
    frame = [[(0, 0, 0, 0) for _ in range(32)] for _ in range(32)]
    for row, line in enumerate(art):
        for col, ch in enumerate(line):
            rgb = char_rgb[ch]
            if rgb is None:
                continue
            if recolour:
                # colour name lookup via the inverse palette / 经调色板反查色名
                rgb = RECOLOUR[RECOLOUR_NAME[rgb]]
            frame[row + 2][col + 9] = (rgb[0], rgb[1], rgb[2], 255)
    return frame


# Built lazily in main() once the template is loaded (palette -> role name).
# main() 载入模板后惰性构建（调色板色 -> 角色名）。
RECOLOUR_NAME = {}


def write_png(path, frame):
    h = len(frame)
    w = len(frame[0])
    raw = b"".join(b"\x00" + bytes(px for p in row for px in p) for row in frame)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)  # 8-bit RGBA
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


def blank_frame(w, h):
    return [[(0, 0, 0, 0) for _ in range(w)] for _ in range(h)]


def blit(dst, src, ox, oy):
    """Place src onto dst at (ox, oy), clipping at the edges.
    把 src 放到 dst 的 (ox, oy)，越界裁剪。"""
    for y, row in enumerate(src):
        for x, px in enumerate(row):
            if px[3] == 0:
                continue
            dy, dx = oy + y, ox + x
            if 0 <= dy < len(dst) and 0 <= dx < len(dst[0]):
                dst[dy][dx] = px


def build_mixed(front):
    """The anti-aliasing variant: ~25% of hair_mid pixels become the midpoint
    colour, so one reference role maps to two user colours (majority wins).
    抗锯齿变体：hair_mid 像素约 25% 变中间色 —— 一个参考角色色对两个用户色
    （多数派胜）。Returns (frame, conflict_pixel_count)."""
    out = [list(r) for r in front]
    n = 0
    for y in range(32):
        for x in range(32):
            if out[y][x][3] == 0:
                continue
            if out[y][x][:3] == RECOLOUR["hair_mid"] and (x * 7 + y * 13) % 4 == 0:
                out[y][x] = MIXED_MINORITY + (255,)
                n += 1
    return out, n


def part_frame(view, char_rgb, std_rows, keep_chars, piece="main"):
    """A library part sprite: the template-colour pixels whose art character
    is in keep_chars, on a transparent 32x32 canvas (template coords).
    piece (v0.12.0 plan D2): "main" = the FRONT hair piece (fringe + crown —
    the layer that covers the face), "back" = the BACK-LAYER piece (rear mass
    + hanging side hair — the layer that beds under face and clothes).
    库部件精灵：像素图中字符属于 keep_chars 的模板色像素，画在透明 32×32
    画布上（模板坐标）。piece（v0.12.0 计划 D2）："main" = 前发片（刘海 + 顶发
    —— 盖脸层），"back" = 垫底片（后脑发团 + 侧垂发 —— 垫在脸和衣服下面）。"""
    art = list(std_rows[view])
    art[:11] = BOB_HEAD[view]
    frame = blank_frame(32, 32)
    for row, line in enumerate(art):
        # HEAD rows only (y2-y12). The old version also collected the '@'
        # outline of body/leg rows into the hairstyle — invisible while the
        # pieces shared the outline colour, but it put leg pixels into the
        # hair region box (caught by assert_in_box in v0.12.0).
        # 只收【头部行】（y2-y12）。旧版把身体/腿行的 '@' 描边也收进了发型件
        # —— 描边同色时看不出来，但腿像素坐进了头发轮廓框（v0.12.0 被
        # assert_in_box 抓住）。
        if row >= 11:
            break
        for col, ch in enumerate(line):
            if ch not in keep_chars:
                continue
            rgb = char_rgb[ch]
            if rgb is None:
                continue
            # Piece ownership / 片归属:
            #   front: y8+ is the hanging side hair (back piece); y2-y7 crown
            #     + fringe is the front piece.
            #   side: y8+ splits at the midline — col<=6 is the rear mass
            #     (back piece), col>=7 the fringe/sideburn side (front piece).
            #   正面：y8 起是侧垂发（垫底片）；y2-y7 顶发 + 刘海是前发片。
            #   侧面：y8 起按中线分 —— col<=6 后脑（垫底片），col>=7 刘海侧
            #   （前发片）。
            if view == "front":
                to_back = row >= 6
            else:
                to_back = row >= 6 and col <= 6
            if (piece == "back") != to_back:
                continue
            frame[row + 2][col + 9] = (rgb[0], rgb[1], rgb[2], 255)
    return frame


def top_part_frame(view):
    """The new top (turtleneck sweater), hand-authored, template colours.
    The collar reaches UP into the collar band (y12, bottom of the head
    region) so the neck is never bare — the colour-slot rule from
    docs/PARTS_FORMAT.md. Never uses gold/hair_highlight (same-RGB trap).
    v0.12.0: the outline now hugs the TOP REGION BOX (plan D1) — the old
    version drew full-width shoulders into the arm columns and broke the
    drawing contract ("the test top swallows the arms").
    新上衣（高领毛衣），手工绘制，模板色。领口上延进领口带（y12，头部区下沿），
    脖子永不裸露 —— docs/PARTS_FORMAT.md 的颜色-槽位规则。绝不使用
    gold/hair_highlight（同 RGB 陷阱）。v0.12.0：轮廓收进【上衣轮廓框】（计划
    D1）—— 旧版画满宽肩、侵入臂列、违反绘制契约（"测试上衣吞胳膊"）。"""
    # char -> template RGB, asserted below / 字符 -> 模板 RGB，下方断言
    art = {
        "front": [
            "..T.TT..TTT...",   # y12 collar band / 领口带
            ".TT.TTTTTTT...",   # y13 neck roll / 领圈
            ".@TTTTTTTT@...",   # y14 shoulders / 肩
            ".@TTwwwwwwT@..",   # y15 chest stripe / 胸前白条
            "...@TwwwwwT@..",   # y16
            "...@TTTTTT@...",   # y17
            "...@mmmmmm@...",   # y18 hem band / 下摆带
            "...@TTTTTT@...",   # y19
            "....@@@@@@@...",   # y20 hem edge / 下摆缘
        ],
        # Same shape as front: legal under the shared region box (the box is
        # the union across all animation frames and directions).
        # 与正面同形：共享轮廓框内合法（框是全部动画帧、全部方向的并集）。
        "side": [
            "..T.TT..TTT...",   # y12 collar band / 领口带
            ".TT.TTTTTTT...",   # y13
            ".@TTTTTTTT@...",   # y14
            ".@TTwwwwwwT@..",   # y15
            "...@TwwwwwT@..",   # y16
            "...@TTTTTT@...",   # y17
            "...@mmmmmm@...",   # y18
            "...@TTTTTT@...",   # y19
            "....@@@@@@@...",   # y20
        ],
    }
    chars = art[view]
    frame = blank_frame(32, 32)
    for row, line in enumerate(chars):
        assert len(line) == 14, f"top part {view} row {row}: {line!r}"
        for col, ch in enumerate(line):
            if ch == ".":
                continue
            frame[row + 12][col + 9] = RECOLOUR_NAME_INV[ch] + (255,)
    return frame


def load_region_masks():
    """slot -> 32 row strings, '#' = inside the drawing box (v0.12.0 plan D1).
    槽 -> 32 行字符串，'#' = 绘制框内（v0.12.0 计划 D1）。"""
    with open(REGIONS_JSON, encoding="utf-8") as f:
        data = json.load(f)
    return {s: v["mask"] for s, v in data["slots"].items()}


def assert_in_box(name, frame, mask):
    """Fail hard if any opaque pixel sits outside the slot's region box —
    the drawing contract ("a top may never swallow the arms").
    有不透明像素坐在槽轮廓框外就硬失败 —— 绘制契约（"上衣绝不吞胳膊"）。"""
    bad = [(x, y) for y in range(32) for x in range(32)
           if frame[y][x][3] and mask[y][x] != "#"]
    assert not bad, f"{name}: {len(bad)} px outside the region box, first {bad[:5]}"


# char -> template RGB for the hand-authored top rows (built in main()).
# 手工上衣行的字符 -> 模板 RGB（main() 里构建）。
RECOLOUR_NAME_INV = {}


def write_item_json(path, name, slot, back):
    # v0.12.0: no zOrder field — layering is the defaultLayerZ table
    # (pants/boots 10 -> top 20 -> face 30 -> bare skin 45 -> hair 50,
    # back-layer pieces 5). / v0.12.0：不写 zOrder —— 层序走 defaultLayerZ 表。
    meta = {
        "name": name,
        "slot": slot,
        "back": back,
        "silhouette": "template",
        "roleColors": [],
        "note": "V0.12.0 debug kit part / 整改测试素材部件",
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=2)
        f.write("\n")


def write_readme(path, mixed_count):
    text = f"""FlipBook整改测试素材（V0.12.0）—— 每张测什么
==================================================

一、正向：新角色三视图（拖进「动画导出」tab，三张齐了自动出四方向大图）
--------------------------------------------------
正向_新角色_front.png   正面（文件名带 front，自动对上正面槽）
正向_新角色_side.png    侧面
正向_新角色_back.png    背面
    新角色：波波头发型 + 全身换色（蓝发），32x32 模板对齐。
    预期：三张都拖入后直接出图，无红字；如果缩略图有红/橙点，点「自动对齐」。

二、宽容路径（以前会红字拒绝，现在应该出图 + 黄字提醒）
    这三张是参考剪影 + 新配色（与正向的新发型无关），专测宽容管线。
--------------------------------------------------
宽容_混色_front.png     边缘模拟抗锯齿（同一参考色出现两个用户色，共 {mixed_count} 个
                        少数派像素）。预期：出图 + 黄字"配色不一致…已按主色处理"，
                        缩略图橙点标出冲突像素。
宽容_错位_front.png     内容整体平移 (+2,-1)（32x32 画布）。预期：「自动对齐」
                        能修正回来；或手动微调按钮挪 2 格。
容错_非32x32_80x48.png  80x48 画布、内容居中。预期：尽力对齐出图 + 黄字"输入为
                        80x48…建议导出 32x32 整数倍像素图"，绝不硬拒。

三、部件（拖进「部件编辑器」tab 或「捏人换装」tab —— 会送进编辑器）
--------------------------------------------------
部件_新发型/hair_bob_blue/    新发型"蓝发波波头"（前发片 front+side、
                              垫底后发片 front_b+side_b，back 自动推断）
部件_新上衣/scarf_jacket/     新上衣"高领条纹衫"（轮廓收在上衣轮廓框内；
                              领口画进领口带 y12，不露脖子）
    导入后微调、点「保存到库」，切「捏人换装」立刻能看到缩略图。
    测点：保存必弹结果框 + 路径进剪贴板；缩略图网格可见；层序按槽位缺省表
    （头发 z50 盖顶、脸 z30、上衣 z20、裸肤 z45 护臂、后发垫底片 z5 垫最底）。
    硬规范：部件像素只许画在本槽的轮廓框内（编辑器画布框外打灰），保存时
    框外有像素直接拒绝并报数 —— 这就是"上衣不许吞胳膊"的机制。
    发型双片：前发片（刘海+顶发）盖住脸，后发片（后脑+侧垂发）垫在脸和
    衣服下面 —— 脸夹在两层中间，正是分层合成的要点。
    注：部件像素是模板角色色（role 约束），想要别的颜色用捏人 tab 的
    全局换色改。

四、已知陷阱（素材里已规避，遇到别慌）
--------------------------------------------------
gold 与 hair_highlight 是同一个 RGB，只有靠区域（头部带 vs 其他）区分。
本套部件刻意不用这两个色。自己画部件时：饰品画进对应槽（头部带饰品归发型）。

生成命令：python3 -I tools/make_test_kit.py
"""
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def main():
    argv = sys.argv[1:]
    out = DEFAULT_OUT
    if "--out" in argv:
        i = argv.index("--out")
        if i + 1 >= len(argv):
            print("ERROR: --out needs a directory")
            return 1
        out = Path(argv[i + 1])

    spec, char_rgb, std_rows = load_template()
    # palette colour -> role name, for the recolour lookup.
    # 调色板色 -> 角色名，供换色查表。
    global RECOLOUR_NAME
    RECOLOUR_NAME = {tuple(v): k for k, v in spec["palette"].items()}
    # char -> template RGB for the hand-authored top rows.
    # 手工上衣行的字符 -> 模板 RGB。
    global RECOLOUR_NAME_INV
    RECOLOUR_NAME_INV = {
        "@": tuple(spec["palette"]["outline"]),
        "T": tuple(spec["palette"]["teal"]),
        "w": tuple(spec["palette"]["white"]),
        "m": tuple(spec["palette"]["mint"]),
    }

    # ---- 1. positive: the new character, three views ----
    # ---- 1. 正向：新角色三视图 ----
    frames = {}
    for view in ("front", "side", "back"):
        frames[view] = build_frame(view, char_rgb, std_rows, recolour=True)
        write_png(out / f"正向_新角色_{view}.png", frames[view])
        opaque = sum(1 for r in frames[view] for p in r if p[3])
        print(f"positive {view}: {opaque} opaque px")

    # ---- 2. tolerant: mixed / shifted / non-32x32 ----
    # ---- 2. 宽容：混色 / 错位 / 非 32×32 ----
    # These three keep the REFERENCE silhouette (recoloured only) — palette
    # extraction compares against the reference chip frames, so a matching
    # silhouette is what makes the majority-mapping assertion exact. The bob
    # character above is the "different shape" positive case instead.
    # 这三张保持参考剪影（只换色）—— 配色提取是与参考芯片帧对照的，剪影一致
    # 才能让多数派断言精确。上面的波波头是"不同形状"的正向用例。
    std_front = build_frame("front", char_rgb, std_rows, recolour=True,
                            head_override=False)
    mixed, conflict = build_mixed(std_front)
    write_png(out / "宽容_混色_front.png", mixed)
    print(f"tolerant mixed: {conflict} minority pixels")

    shifted = blank_frame(32, 32)
    blit(shifted, std_front, 2, -1)   # shift right 2, up 1 / 右移 2、上移 1
    write_png(out / "宽容_错位_front.png", shifted)

    big = blank_frame(80, 48)
    blit(big, std_front, 24, 8)       # centred-ish / 大致居中
    write_png(out / "容错_非32x32_80x48.png", big)

    # ---- 3. parts: hairstyle + top (template colours, role constraint,
    #          inside their region boxes; hair in TWO pieces, v0.12.0) ----
    # ---- 3. 部件：发型 + 上衣（模板色、role 约束、坐在轮廓框内；发型双片，
    #          v0.12.0）----
    masks = load_region_masks()
    hair_dir = out / "部件_新发型" / "hair_bob_blue"
    for view in ("front", "side"):
        # The hairstyle owns hair chars + the outline contour (no face pixels,
        # and deliberately NO 'y' — the gold/hair_highlight same-RGB trap).
        # Two pieces (plan D2): front piece = fringe + crown, back piece =
        # rear mass + hanging side hair. A single piece would work too (the
        # loader stamps it twice) — the split is what lets the face sit
        # BETWEEN the two layers.
        # 发型件收头发字符 + 描边轮廓（不含脸像素，且刻意不用 'y' ——
        # gold/hair_highlight 同 RGB 陷阱）。两片（计划 D2）：前发片 = 刘海 +
        # 顶发，垫底片 = 后脑发团 + 侧垂发。单片也能用（加载器贴两次）——
        # 拆开才让脸夹在两层【中间】。
        for piece, suffix in (("main", ""), ("back", "_b")):
            fr = part_frame(view, char_rgb, std_rows, keep_chars=set("@hH"),
                            piece=piece)
            assert_in_box(f"hair_bob_blue/{view}{suffix}", fr, masks["hair"])
            write_png(hair_dir / f"{view}{suffix}.png", fr)
    write_item_json(hair_dir / "item.json", "hair_bob_blue", "hair", "auto")

    top_dir = out / "部件_新上衣" / "scarf_jacket"
    for view in ("front", "side"):
        fr = top_part_frame(view)
        assert_in_box(f"scarf_jacket/{view}", fr, masks["top"])
        write_png(top_dir / f"{view}.png", fr)
    write_item_json(top_dir / "item.json", "scarf_jacket", "top", "auto")

    # ---- 4. readme ----
    # ---- 4. 说明 ----
    write_readme(out / "说明.txt", conflict)
    print(f"wrote kit -> {out}")

    # ---- 5. fixture copy for the headless tolerant gate ----
    # ---- 5. 闸门用的夹具副本（无头宽容断言）----
    FIXTURE_OUT.mkdir(parents=True, exist_ok=True)
    write_png(FIXTURE_OUT / "mixed_front.png", mixed)
    write_png(FIXTURE_OUT / "newchar_front.png", frames["front"])
    write_png(FIXTURE_OUT / "newchar_side.png", frames["side"])
    write_png(FIXTURE_OUT / "newchar_back.png", frames["back"])
    with open(FIXTURE_OUT / "expected.json", "w", encoding="utf-8") as f:
        json.dump({
            "mixedConflictPixels": conflict,
            "majorityHairMid": list(RECOLOUR["hair_mid"]),
            "minority": list(MIXED_MINORITY),
            "note": "generated by tools/make_test_kit.py / 由 make_test_kit.py 生成",
        }, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print(f"wrote fixtures -> {FIXTURE_OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
