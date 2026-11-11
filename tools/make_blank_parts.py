#!/usr/bin/env python3
# make_blank_parts.py — generate the blank-but-shaped part templates (v0.9.2).
# 生成"空白但有形状"的部件模板（v0.9.2）。
#
# Cuts the reference character into six slots (hair / face / top / pants /
# boots / body) per the template part_map, per view (front + side), and writes
# "blank" PNGs: the SHAPE survives (outline contour kept in its own colour),
# every interior pixel is white placeholder for the user to fill in — either
# in-tool (P3 fill editor) or in any external editor. Output:
#   assets/parts/_blank/<slot>_{front,side}.png + part.json (manifest)
# 按模板 part_map 把参考角色切成六槽（hair/face/top/pants/boots/body），
# 逐视图（front + side）输出"空白"PNG：形状保留（轮廓线描边色保留），
# 内部像素全填白色占位，供用户填色 —— 工具内（P3 填色编辑器）或外部软件均可。
# 产物：assets/parts/_blank/<slot>_{front,side}.png + part.json（清单）。
#
# Slot assignment (region resolves same-colour roles — hair_highlight and gold
# share RGB; roles are semantic, pixels are not):
#   hair       hair_mid / hair_dark / hair_highlight
#   face       head rows: skin / blush / white / teal
#   top        mint / red / gold, body-row grey, non-head white
#   pants      pants / tan
#   boots      grey_dark anywhere, leg-row grey
#   body       body/leg rows: skin / blush
#   outline    kept contour pixels join the slot their 8-neighbours vote for
# 槽位划分（区域消歧同色角色 —— hair_highlight 与 gold 同 RGB；角色有语义，
# 像素没有）：
#   hair       hair_mid / hair_dark / hair_highlight
#   face       头部行：skin / blush / white / teal
#   top        mint / red / gold、躯干行 grey、非头部 white
#   pants      pants / tan
#   boots      任意行 grey_dark、腿部行 grey
#   body       躯干/腿部行：skin / blush
#   outline    保留的轮廓像素按 8 邻投票归槽
#
# Run with: python3 -I tools/make_blank_parts.py
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
TEMPLATE_JSON = ROOT / "assets/templates/chibi_standard.json"
ASSETS = ROOT / "assets"
OUT_DIR = ROOT / "assets/parts/_blank"

VIEWS = [("front", "character_32.png"), ("side", "character_32_side.png")]
SLOTS = ["hair", "face", "top", "pants", "boots", "body"]
Z_ORDER = {"hair": 40, "face": 30, "top": 20, "pants": 10, "boots": 10, "body": 5}
WHITE = (255, 255, 255, 255)


def rows_to_set(rows):
    return set(rows)


def load_spec():
    with open(TEMPLATE_JSON, encoding="utf-8") as f:
        spec = json.load(f)
    palette = {role: tuple(rgb) for role, rgb in spec["palette"].items()}
    views = {}
    for name in ("front", "side"):
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


def main():
    palette, views = load_spec()
    by_colour = build_role_lookup(palette)
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    manifest = {
        "_schema": "blank parts template v1",
        "canvas": [32, 32],
        "note": ("Blank but shaped: outline kept, interior is white "
                 "placeholder. Fill in-tool or externally, then save into the "
                 "parts library. / 空白有形状：描边保留，内部白色占位。"
                 "工具内或外部填色后存入部件库。"),
        "slots": {},
    }

    problems = 0
    for view_name, png_name in VIEWS:
        img = Image.open(ASSETS / png_name).convert("RGBA")
        w, h = img.size
        px = img.load()
        view_rows = views[view_name]

        # Pass 1: assign every opaque pixel a slot; record outline pixels.
        # 第一遍：给每个不透明像素定槽；记录轮廓像素。
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
                    outline_px.append((x, y, (r, g, b, a)))
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

        # Pass 2: outline pixels join the slot their opaque 8-neighbours vote
        # for. Contour without any neighbour slot is dropped (no shape owner).
        # 第二遍：轮廓像素按不透明 8 邻投票归槽。无邻槽的轮廓丢弃（无形状归属）。
        for x, y, rgba in outline_px:
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
            # else: dropped / 否则丢弃

        if unknown:
            for (r, g, b), n in unknown.most_common():
                print(f"  WARNING {view_name}: unclassified colour "
                      f"({r},{g},{b}) x{n}", file=sys.stderr)
            problems += 1

        # Pass 3: render each slot's blank PNG — outline keeps its colour,
        # everything else is white placeholder.
        # 第三遍：渲染每槽空白 PNG —— 描边保留本色，其余白色占位。
        for slot in SLOTS:
            out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            op = out.load()
            n_interior = n_outline = 0
            for (x, y), s in slot_of.items():
                if s != slot:
                    continue
                src = px[x, y]
                if src[:3] == tuple(palette["outline"]):
                    op[x, y] = src
                    n_outline += 1
                else:
                    op[x, y] = WHITE
                    n_interior += 1
            out_name = f"{slot}_{view_name}.png"
            out.save(OUT_DIR / out_name)
            entry = manifest["slots"].setdefault(slot, {
                "zOrder": Z_ORDER[slot],
                "placeholder": "white",
            })
            entry[view_name] = out_name
            print(f"  {out_name}: {n_interior} fillable + "
                  f"{n_outline} outline px")

    manifest["slots"] = {s: manifest["slots"][s] for s in SLOTS
                         if s in manifest["slots"]}
    with open(OUT_DIR / "part.json", "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print(f"Wrote manifest: {OUT_DIR / 'part.json'}")

    # Self-check: the six slot PNGs must tile the source silhouette exactly —
    # disjoint slots, union == original alpha. Any gap or overlap means the
    # cut lost or duplicated pixels.
    # 自检：六槽 PNG 必须严丝合缝铺满源剪影 —— 槽间不重叠、并集 == 原图 alpha。
    # 有缺口或重叠即切分丢像素或重像素。
    for view_name, png_name in VIEWS:
        src_img = Image.open(ASSETS / png_name).convert("RGBA")
        w, h = src_img.size
        src = src_img.load()
        hit = [[0] * w for _ in range(h)]
        for slot in SLOTS:
            p = manifest["slots"].get(slot, {}).get(view_name)
            if not p:
                continue
            sp = Image.open(OUT_DIR / p).convert("RGBA").load()
            for y in range(h):
                for x in range(w):
                    if sp[x, y][3]:
                        hit[y][x] += 1
        bad = 0
        for y in range(h):
            for x in range(w):
                want = 1 if src[x, y][3] else 0
                if hit[y][x] != want:
                    bad += 1
        if bad:
            print(f"  SELF-CHECK {view_name}: {bad} px gap/overlap   FAIL",
                  file=sys.stderr)
            problems += 1
        else:
            print(f"  self-check {view_name}: slots tile the silhouette "
                  f"exactly   OK")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
