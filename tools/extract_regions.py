#!/usr/bin/env python3
# extract_regions.py — per-slot region boxes from the motion chips (v0.12.0).
# 从动作芯片提取逐槽轮廓框（v0.12.0）。
#
# The region box of a slot = union of every cell that slot ever occupies in
# ANY chip frame (walk/idle x front/side/back). It is the drawing contract for
# the part editor: an item may only paint inside its slot's box. Overlap
# between boxes is allowed (sleeve cells live in both top and body boxes) —
# frame-level ownership decides who paints what at render time.
# 槽的轮廓框 = 该槽在所有芯片帧（走/待机 x 正/侧/背）中出现过的格子并集。
# 它是部件编辑器的绘制契约：素材只能画在本槽的框内。框与框允许重叠（袖格
# 同属 top 框与 body 框）—— 渲染时由逐帧归属决定谁画。
#
# Classification mirrors tools/make_parts_fixture.py exactly (colour role +
# row region, outline joins by 8-neighbour vote) so the boxes agree with the
# part slicing semantics. Per-frame body_top_y shifts the row bands.
# 分类与 tools/make_parts_fixture.py 完全一致（角色色 + 行区域，描边 8 邻
# 投票归槽），框与部件切分语义对齐。行带按逐帧 body_top_y 平移。
#
# Usage: python3 -I tools/extract_regions.py --chips DIR --templates DIR --out FILE
import argparse
import json
import sys
from collections import Counter
from pathlib import Path

from PIL import Image

# Region -> slot classifier, identical to make_parts_fixture.classify().
# 区域 -> 槽分类器，与 make_parts_fixture.classify() 一致。
def classify(role, region):
    if role in ("hair_mid", "hair_dark", "hair_highlight"):
        return "hair"
    if role in ("skin", "blush"):
        return "face" if region == "head" else "body"
    if role in ("white", "teal"):
        return "face" if region == "head" else "top"
    if role in ("mint", "red", "gold"):
        return "hair" if region == "head" else "top"
    if role == "grey":
        return "boots" if region == "leg" else "top"
    if role == "grey_dark":
        return "boots"
    if role in ("pants", "tan"):
        return "pants"
    return None


# chip flipbook name -> anim_reference.json animation key / 芯片名 -> 动画键
FLIPBOOK_TO_ANIM = {
    "front_idle": "idle_front",
    "front_walk": "walk_front",
    "side_idle": "idle_side",
    "side_walk": "walk_side",
    "back_idle": "idle_back",
    "back_walk": "walk_up",
}

SLOTS = ("hair", "face", "top", "pants", "boots", "body")


def build_role_lookup(palette):
    """colour -> role, preferring the non-gold name (region disambiguates).
    颜色 -> 角色，同色冲突时优先非 gold 名（由区域消歧）。"""
    by_colour = {}
    for role, rgb in palette.items():
        t = tuple(rgb)
        if t not in by_colour or by_colour[t] == "gold":
            by_colour[t] = role
    return by_colour


def region_of(y_norm, head_rows, body_rows, leg_rows):
    if y_norm in head_rows:
        return "head"
    if y_norm in body_rows:
        return "body"
    if y_norm in leg_rows:
        return "leg"
    return "other"


def frame_cells(img, by_colour, head_rows, body_rows, leg_rows, body_top_y):
    """One chip frame -> {cell: slot}. Rows are the *normalised* bands; the
    frame is drawn at body_top_y, so content at row y belongs to normal row
    y - (body_top_y - 2). / 一帧 -> {格: 槽}。行带按规范坐标判：帧画在
    body_top_y，行 y 的内容属于规范行 y - (body_top_y - 2)。"""
    off = body_top_y - 2
    slot_of = {}
    outline_px = []
    unknown = Counter()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = img.getpixel((x, y))
            if a == 0:
                continue
            role = by_colour.get((r, g, b), "")
            if role == "":
                unknown[(r, g, b)] += 1
                continue
            if role == "outline":
                outline_px.append((x, y))
                continue
            region = region_of(y - off, head_rows, body_rows, leg_rows)
            if role in ("hair_highlight", "gold"):
                role = "hair_highlight" if region == "head" else "gold"
            slot = classify(role, region)
            if slot is None:
                unknown[(r, g, b)] += 1
                continue
            slot_of[(x, y)] = slot
    for x, y in outline_px:
        votes = Counter()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                s = slot_of.get((x + dx, y + dy))
                if s:
                    votes[s] += 1
        if votes:
            slot_of[(x, y)] = votes.most_common(1)[0][0]
    return slot_of, unknown


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--chips", required=True)
    ap.add_argument("--templates", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    chips_dir = Path(args.chips)
    templates = Path(args.templates)
    spec = json.load(open(templates / "chibi_standard.json"))
    anim = json.load(open(templates / "anim_reference.json"))

    by_colour = build_role_lookup(spec["palette"])
    prop = spec["proportions"]
    head_rows = set(range(prop["head"]["y_start"], prop["head"]["y_end"] + 1))
    body_rows = set(range(prop["body"]["y_start"], prop["body"]["y_end"] + 1))
    leg_rows = set(range(prop["legs"]["y_start"], prop["legs"]["y_end"] + 1))

    # chip name -> per-frame body_top_y / 芯片 -> 逐帧 body_top_y
    body_top = {}
    for fb_name, anim_key in FLIPBOOK_TO_ANIM.items():
        a = anim["animations"][anim_key]
        body_top[fb_name] = [f["body_top_y"] for f in a["frame_data"]]

    cells = {s: set() for s in SLOTS}
    frames = 0
    for png in sorted((chips_dir / "chips").glob("*.png")):
        stem = png.stem                       # e.g. front_walk_3
        fb_name, idx = stem.rsplit("_", 1)
        if fb_name not in body_top:
            print(f"skip unknown chip {png.name}", file=sys.stderr)
            continue
        bty = body_top[fb_name][int(idx)]
        img = Image.open(png).convert("RGBA")
        if img.size != (32, 32):
            sys.exit(f"chip {png.name}: expected 32x32, got {img.size}")
        slot_of, unknown = frame_cells(img, by_colour, head_rows, body_rows,
                                       leg_rows, bty)
        if unknown:
            for (r, g, b), n in unknown.most_common():
                print(f"  WARNING {png.name}: unclassified colour "
                      f"({r},{g},{b}) x{n}", file=sys.stderr)
            sys.exit(1)
        frames += 1
        for cell, s in slot_of.items():
            cells[s].add(cell)

    out = {
        "_schema": "chibi regions v1",
        "_note": ("per-slot drawing boxes: union of cells the slot occupies "
                  "across all motion chip frames; overlap allowed; written "
                  "by tools/extract_regions.py"),
        "source": "assets/templates/motion_chips",
        "cell": 32,
        "slots": {},
    }
    print(f"{frames} frames scanned / 扫描帧数")
    for s in SLOTS:
        cs = sorted(cells[s], key=lambda c: (c[1], c[0]))
        if not cs:
            sys.exit(f"slot {s}: empty region — extraction is wrong")
        xs = [c[0] for c in cs]
        ys = [c[1] for c in cs]
        mask = []
        for y in range(32):
            mask.append("".join("#" if (x, y) in cells[s] else "."
                                for x in range(32)))
        out["slots"][s] = {
            "count": len(cs),
            "bbox": [min(xs), min(ys), max(xs), max(ys)],
            "mask": mask,
        }
        print(f"  {s:6s} {len(cs):3d} cells  bbox "
              f"x{min(xs)}-{max(xs)} y{min(ys)}-{max(ys)}")

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w") as f:
        json.dump(out, f, indent=1)
        f.write("\n")
    print(f"written: {out_path}")


if __name__ == "__main__":
    main()
