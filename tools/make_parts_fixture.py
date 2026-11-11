#!/usr/bin/env python3
# make_parts_fixture.py — build the compose test fixture: the reference
# character cut into six swappable library items (one per slot), all three
# views, real colours. Used by --compose-gate (reconstruction invariant) and
# usable as starter content for the customization GUI.
# 生成 compose 测试夹具：参考角色切成六件可换库物品（每槽一件）、三视图、
# 真实配色。供 --compose-gate（重建不变量）用，也可当捏人 GUI 的起步内容。
#
# Slot assignment rules are IDENTICAL to tools/make_blank_parts.py (same
# classifier, same outline 8-neighbour vote) — only the rendering differs:
# pixels keep their source colour instead of a white placeholder.
# 槽位归属规则与 tools/make_blank_parts.py 完全相同（同分类器、同轮廓 8 邻
# 投票）—— 只有渲染不同：像素保留原色而非白色占位。
#
# Self-check: the six slots tile each view's silhouette EXACTLY — disjoint,
# union == original alpha. Any gap or overlap aborts.
# 自检：六槽严丝合缝铺满每视图剪影 —— 互不重叠、并集 == 原 alpha。
# 有缺口或重叠即中止。
#
# Usage / 用法:
#   python3 -I tools/make_parts_fixture.py

import json
import sys
from collections import Counter
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
TEMPLATE_JSON = ROOT / "assets" / "templates" / "chibi_standard.json"
ASSETS = ROOT / "assets"
OUT_DIR = ROOT / "tests" / "fixtures" / "parts_library"

SLOTS = ("hair", "face", "top", "pants", "boots", "body")
Z_ORDER = {"hair": 40, "face": 30, "top": 20, "pants": 10, "boots": 10,
           "body": 5}
VIEWS = (("front", "character_32.png"),
         ("side", "character_32_side.png"),
         ("back", "character_32_back.png"))


def rows_to_set(rows):
    return set(rows)


def load_spec():
    with open(TEMPLATE_JSON, encoding="utf-8") as f:
        spec = json.load(f)
    palette = {role: tuple(rgb) for role, rgb in spec["palette"].items()}
    views = {}
    for name in ("front", "side", "back"):
        pm = spec["views"][name]["part_map"]
        views[name] = {
            "head": rows_to_set(pm["head_rows"]),
            "body": rows_to_set(pm["body_rows"]),
            "leg": rows_to_set(pm["leg_rows"]),
        }
    return palette, views


def build_role_lookup(palette):
    """colour -> role, preferring the non-gold name (region disambiguates).
    颜色 -> 角色，同色冲突时优先非 gold 名（由区域消歧）。"""
    by_colour = {}
    for role, rgb in palette.items():
        if rgb not in by_colour or by_colour[rgb] == "gold":
            by_colour[rgb] = role
    return by_colour


def region_of(y, view_rows):
    if y in view_rows["head"]:
        return "head"
    if y in view_rows["body"]:
        return "body"
    if y in view_rows["leg"]:
        return "leg"
    return "other"


def classify(role, region):
    if role in ("hair_mid", "hair_dark", "hair_highlight"):
        return "hair"
    if role in ("skin", "blush"):
        return "face" if region == "head" else "body"
    if role in ("white", "teal"):
        return "face" if region == "head" else "top"
    if role in ("mint", "red", "gold"):
        return "top"
    if role == "grey":
        return "boots" if region == "leg" else "top"
    if role == "grey_dark":
        return "boots"
    if role in ("pants", "tan"):
        return "pants"
    return None


def assign_slots(px, w, h, view_rows, by_colour, palette, view_name):
    """Two passes: classify, then outline joins by 8-neighbour vote.
    两遍：先分类，轮廓按 8 邻投票归槽。"""
    slot_of = {}
    outline_px = []
    unknown = Counter()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            role = by_colour.get((r, g, b), "")
            if role == "":
                unknown[(r, g, b)] += 1
                continue
            if role == "outline":
                outline_px.append((x, y))
                continue
            region = region_of(y, view_rows)
            # hair_highlight / gold share RGB — region decides.
            # hair_highlight / gold 同 RGB —— 由区域决定。
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
        # else: dropped (no shape owner) / 否则丢弃（无形状归属）

    if unknown:
        for (r, g, b), n in unknown.most_common():
            print(f"  WARNING {view_name}: unclassified colour "
                  f"({r},{g},{b}) x{n}", file=sys.stderr)
        sys.exit(1)
    return slot_of


def main():
    palette, views = load_spec()
    by_colour = build_role_lookup(palette)

    for slot in SLOTS:
        (OUT_DIR / slot / f"ref_{slot}").mkdir(parents=True, exist_ok=True)

    for view_name, png_name in VIEWS:
        img = Image.open(ASSETS / png_name).convert("RGBA")
        w, h = img.size
        px = img.load()
        slot_of = assign_slots(px, w, h, views[view_name], by_colour,
                               palette, view_name)

        # Render each slot's REAL-colour sprite (source pixels verbatim).
        # 渲染每槽真实色精灵（像素原样照搬）。
        covered = Counter()
        for slot in SLOTS:
            out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            op = out.load()
            n = 0
            for (x, y), s in slot_of.items():
                if s != slot:
                    continue
                op[x, y] = px[x, y]
                covered[(x, y)] += 1
                n += 1
            out_dir = OUT_DIR / slot / f"ref_{slot}"
            out.save(out_dir / f"{view_name}.png")
            print(f"  {slot}/ref_{slot}/{view_name}.png: {n} px")

        # Self-check: disjoint + union == original silhouette.
        # 自检：互不重叠 + 并集 == 原剪影。
        problems = []
        for y in range(h):
            for x in range(w):
                _, _, _, a = px[x, y]
                c = covered[(x, y)]
                if a != 0 and c == 0:
                    problems.append(f"gap at ({x},{y})")
                if c > 1:
                    problems.append(f"overlap x{c} at ({x},{y})")
        if problems:
            print(f"SELFCHECK {view_name}: FAIL — {len(problems)} problems, "
                  f"first: {problems[:5]}", file=sys.stderr)
            sys.exit(1)
        print(f"SELFCHECK {view_name}: OK — six slots tile the silhouette")

    # item.json per item: back:"file" (all three views come from the reference).
    # 每件 item.json：back:"file"（三视图都来自参考角色）。
    for slot in SLOTS:
        item_dir = OUT_DIR / slot / f"ref_{slot}"
        meta = {
            "name": f"ref_{slot}",
            "slot": slot,
            "zOrder": Z_ORDER[slot],
            "back": "file",
            "silhouette": "template",
            "roleColors": [],
            "note": "Reference character part (fixture + starter content). / "
                    "参考角色部件（夹具 + 起步内容）。",
        }
        with open(item_dir / "item.json", "w", encoding="utf-8") as f:
            json.dump(meta, f, ensure_ascii=False, indent=2)
            f.write("\n")
    print(f"Wrote library: {OUT_DIR}")


if __name__ == "__main__":
    main()
