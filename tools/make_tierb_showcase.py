#!/usr/bin/env python3
# make_tierb_showcase.py — build the Tier B showcase HTML (templates + flipbooks).
# 生成 Tier B 展示 HTML（模板展示 + 翻页书播放）。
#
# §1 「逐帧照搬模板」: every flipbook's per-frame chips, enlarged, with the
#     two-tone / far-arm / straight-leg annotations, plus the palette legend and
#     the reskin colour map (the user asked to include the templates).
# §2 「双角色 6 本翻页书」: each flipbook plays three synced columns — reference
#     cell | original | reskin — so the verbatim copy and the colour-only reskin
#     are visible side by side.
# §1「逐帧照搬模板」：每本翻页书的逐帧芯片，放大 + 两棕/远臂/直腿注记 + 调色板图例
#     + 换色映射表（用户要求把模板加进展示）。
# §2「双角色 6 本翻页书」：每本翻页书三列同步播放 —— 参考格 | 原版 | 换皮 ——
#     照搬与只换色并排可见。
#
# Usage:
#   python3 -I tools/make_tierb_showcase.py \
#     --frames output/preview/tierb --sheet assets/reference_sheet.png \
#     --standard assets/templates/chibi_standard.json \
#     --alt assets/templates/chibi_alt.json \
#     --out flipbooks_tierb.html

import argparse
import base64
import json
import os
from glob import glob
from PIL import Image

CELL = 32

# The 6 flipbooks, in reference-sheet row order. `row` = sheet row for cropping
# the reference cell. Annotations read off the pixel analysis (see plan).
# 六本翻页书，按参考图行序。row = 切参考格的行。注记取自像素分析。
FLIPBOOKS = [
    dict(key="front_idle", row=0, frames=2, fps=4,
         title="正面 · 待机", sub="idle · 2 帧 @ 4fps",
         annotate="腿+靴钉住 · 上身 1px 伸展",
         note="腿和靴逐帧逐像素不动，上身做 1px 伸展呼吸（f1 比 f0 高一行）。两帧照搬原版。"),
    dict(key="front_walk", row=1, frames=6, fps=10,
         title="正面 · 行走（向下）", sub="walk_front · 6 帧 @ 10fps",
         annotate="直腿 · 两棕换色 [0,1,5]↔[2,3,4]",
         note="前后走腿是直的：形状基本不变，靠两主色换色区分跨步；黑轮廓逐帧照抄。近腿透视缩短。"),
    dict(key="side_idle", row=2, frames=2, fps=4,
         title="侧面 · 待机", sub="idle · 2 帧 @ 4fps",
         annotate="腿钉住 · 远臂完整露出",
         note="远臂完整露出到手（绝无「插口袋」），腿钉住，上身伸展一行。"),
    dict(key="side_walk", row=3, frames=6, fps=10,
         title="侧面 · 行走（向右）", sub="walk_side · 6 帧 @ 10fps",
         annotate="真摆腿 · 远臂 f2/f3 · 换色 [0,1,2,4,5]↔[3]",
         note="唯一真摆腿的视图：6 帧 6 种手绘腿形（并腿、膝交叉 X、大跨步）。远臂仅 f2/f3 手绘露出，其余帧被躯干自然遮挡。向左 = 本动画水平镜像。"),
    dict(key="back_idle", row=4, frames=2, fps=4,
         title="背面 · 待机", sub="idle · 2 帧 @ 4fps",
         annotate="腿钉住 · 上身伸展",
         note="背面头发盖住脸与躯干上部，伸展逻辑同正面，腿钉住。"),
    dict(key="back_walk", row=5, frames=6, fps=10,
         title="背面 · 行走（向上）", sub="walk_up · 6 帧 @ 10fps",
         annotate="直腿 · 两棕换色 [0,1,4,5]↔[2,3]",
         note="背对镜头：腿全直、两主色换色区分跨步，背面头发盖脸。"),
]


def data_uri(path):
    with open(path, "rb") as f:
        return "data:image/png;base64," + base64.b64encode(f.read()).decode()


def frames_for(root, key):
    files = glob(os.path.join(root, key, "frame_*.png"))
    files.sort(key=lambda p: int(os.path.basename(p)[6:-4]))
    return [data_uri(p) for p in files]


def hexrgb(rgb):
    return "#%02x%02x%02x" % (rgb[0], rgb[1], rgb[2])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", required=True, help="root with orig/ and alt/ subdirs")
    ap.add_argument("--sheet", required=True)
    ap.add_argument("--standard", required=True)
    ap.add_argument("--alt", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    sheet = Image.open(args.sheet).convert("RGBA")
    std = json.load(open(args.standard))
    alt = json.load(open(args.alt))

    data = {"slots": [], "frames": {}, "palette": []}
    for s in FLIPBOOKS:
        data["slots"].append({k: s[k] for k in
                              ("key", "title", "sub", "note", "annotate", "fps", "frames")})
        # Reference cells: crop the sheet row / 参考格：切参考图该行
        ref = []
        for f in range(s["frames"]):
            cell = sheet.crop((f * CELL, s["row"] * CELL, (f + 1) * CELL, (s["row"] + 1) * CELL))
            import io
            buf = io.BytesIO()
            cell.save(buf, format="PNG")
            ref.append("data:image/png;base64," +
                       base64.b64encode(buf.getvalue()).decode())
        data["frames"][s["key"]] = {
            "ref":  ref,
            "orig": frames_for(os.path.join(args.frames, "orig"), s["key"]),
            "alt":  frames_for(os.path.join(args.frames, "alt"), s["key"]),
        }

    # Palette legend, aligned by role / 调色板图例，按 role 对齐
    for role in sorted(std["palette"]):
        data["palette"].append({
            "role": role,
            "std": hexrgb(std["palette"][role]),
            "alt": hexrgb(alt["palette"][role]),
        })

    html = build_html(json.dumps(data, separators=(",", ":"), ensure_ascii=False))
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(html)
    kb = len(html.encode("utf-8")) / 1024
    print(f"wrote {args.out}  ({kb:.1f} KB)")


def build_html(data_json):
    return HTML.replace("__DATA__", data_json)


HTML = """<!doctype html>
<title>逐帧照搬 · 六本翻页书</title>
<meta name="claude:viewport-fit-sides" content="cover">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<style>
  :root {
    --bg:#eef1f8; --bg2:#e3e8f4; --panel:#ffffff; --panel2:#f4f6fc;
    --ink:#1b2030; --ink-soft:#5a6480; --ink-faint:#8b93ad;
    --accent:#3b6fe0; --accent2:#c0397a; --line:#d3dae9;
    --good:#2fae7a; --warn:#c9860a;
    --chip:#e7ecf8; --chip-ink:#3a4a63;
    --shadow:0 1px 2px rgba(20,30,60,.08),0 8px 24px rgba(20,30,60,.08);
    --radius:16px;
    --font:"Inter","Helvetica Neue",system-ui,sans-serif;
    --display:"Space Grotesk","Inter",system-ui,sans-serif;
    --mono:ui-monospace,"SF Mono","Cascadia Code",Menlo,monospace;
  }
  @media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
    --bg:#0e1017; --bg2:#12141f; --panel:#171a26; --panel2:#1d2130;
    --ink:#e9ecf6; --ink-soft:#9aa3c0; --ink-faint:#6b7392;
    --accent:#6ea8ff; --accent2:#ff6ba6; --line:#2a2f42;
    --good:#4bd3a0; --warn:#f0b24a;
    --chip:#222839; --chip-ink:#c2c9e0;
    --shadow:0 1px 2px rgba(0,0,0,.3),0 10px 30px rgba(0,0,0,.35);
    color-scheme:dark;
  }}
  :root[data-theme="dark"] {
    --bg:#0e1017; --bg2:#12141f; --panel:#171a26; --panel2:#1d2130;
    --ink:#e9ecf6; --ink-soft:#9aa3c0; --ink-faint:#6b7392;
    --accent:#6ea8ff; --accent2:#ff6ba6; --line:#2a2f42;
    --good:#4bd3a0; --warn:#f0b24a;
    --chip:#222839; --chip-ink:#c2c9e0;
    --shadow:0 1px 2px rgba(0,0,0,.3),0 10px 30px rgba(0,0,0,.35);
    color-scheme:dark;
  }
  * { box-sizing:border-box; }
  html,body { margin:0; padding:0; }
  body {
    background:var(--bg); color:var(--ink); font-family:var(--font);
    line-height:1.55; -webkit-font-smoothing:antialiased;
    padding-left:max(16px,env(safe-area-inset-left,0px));
    padding-right:max(16px,env(safe-area-inset-right,0px));
    padding-block:0; min-height:100vh;
  }
  .wrap { max-width:1120px; margin:0 auto; padding-block:calc(env(safe-area-inset-top,0px) + 28px) 64px; }

  header.hero { margin-bottom:22px; }
  .eyebrow { font-family:var(--mono); font-size:12px; letter-spacing:.16em;
    text-transform:uppercase; color:var(--accent); font-weight:600; }
  h1 { font-family:var(--display); font-weight:700; letter-spacing:-.02em;
    font-size:clamp(30px,6vw,52px); line-height:1.02; margin:.2em 0 .3em; text-wrap:balance; }
  .lede { font-size:clamp(15px,2.4vw,18px); color:var(--ink-soft); max-width:64ch; margin:0; }

  .toolbar { display:flex; flex-wrap:wrap; gap:10px 14px; align-items:center;
    margin:20px 0 8px; padding:12px 14px; background:var(--panel);
    border:1px solid var(--line); border-radius:var(--radius); box-shadow:var(--shadow); }
  .toolbar .grp { display:flex; align-items:center; gap:8px; }
  .toolbar .spacer { flex:1; }
  button { font-family:var(--font); font-size:13px; font-weight:600; cursor:pointer;
    border:1px solid var(--line); background:var(--chip); color:var(--ink);
    padding:8px 13px; border-radius:10px; transition:.12s; line-height:1; }
  button:hover { border-color:var(--accent); transform:translateY(-1px); }
  button:active { transform:translateY(0); }
  button.primary { background:var(--accent); border-color:var(--accent); color:#fff; }
  .seg { display:flex; border:1px solid var(--line); border-radius:10px; overflow:hidden; }
  .seg button { border:0; border-radius:0; background:transparent; }
  .seg button[aria-pressed="true"] { background:var(--accent); color:#fff; }
  label.ctl { font-size:12px; color:var(--ink-soft); font-weight:600;
    display:flex; align-items:center; gap:8px; }
  input[type=range] { accent-color:var(--accent); width:120px; }
  .speed-val { font-family:var(--mono); font-size:12px; color:var(--accent); min-width:34px; }

  section.block { background:var(--panel); border:1px solid var(--line);
    border-radius:var(--radius); box-shadow:var(--shadow); padding:20px;
    margin:22px 0; }
  .block-head { display:flex; flex-wrap:wrap; align-items:baseline; gap:6px 12px; margin-bottom:6px; }
  .block-head h2 { font-family:var(--display); font-size:22px; font-weight:700; margin:0; letter-spacing:-.01em; }
  .block-note { color:var(--ink-soft); font-size:13.5px; margin:4px 0 14px; max-width:74ch; }

  /* ---- palette legend + reskin map / 调色板图例 + 换色表 ---- */
  .pal-wrap { display:flex; flex-wrap:wrap; gap:18px; margin:8px 0 4px; }
  .pal-tab { flex:1 1 320px; min-width:0; }
  .pal-tab h3 { font-family:var(--display); font-size:15px; margin:0 0 8px; }
  table.pal { border-collapse:collapse; width:100%; font-size:12.5px; }
  table.pal th, table.pal td { text-align:left; padding:5px 8px; border-bottom:1px solid var(--line); }
  table.pal th { color:var(--ink-faint); font-weight:600; font-family:var(--mono); font-size:11px;
    text-transform:uppercase; letter-spacing:.04em; }
  table.pal td.role { font-family:var(--mono); }
  .sw { display:inline-block; width:15px; height:15px; border-radius:4px; border:1px solid var(--line);
    vertical-align:-3px; margin-right:6px; }
  .arrow { color:var(--ink-faint); margin:0 6px; }

  /* ---- chip strip / 芯片条 ---- */
  .tmpl { border:1px solid var(--line); border-radius:12px; padding:14px; margin:14px 0;
    background:var(--panel2); }
  .tmpl-head { display:flex; flex-wrap:wrap; align-items:baseline; gap:6px 10px; margin-bottom:4px; }
  .tmpl-head h3 { font-family:var(--display); font-size:16px; margin:0; }
  .badge { font-family:var(--mono); font-size:11px; font-weight:600;
    background:var(--chip); color:var(--chip-ink); padding:3px 8px; border-radius:7px; }
  .badge.anno { background:color-mix(in srgb,var(--accent2) 16%,transparent); color:var(--accent2); }
  .tmpl-note { color:var(--ink-soft); font-size:12.5px; margin:2px 0 10px; max-width:74ch; }
  .strip { display:flex; flex-wrap:wrap; gap:10px; }
  .cellfig { text-align:center; }
  .cellfig .stage { width:76px; height:76px; border-radius:9px; overflow:hidden;
    border:1px solid var(--line); background-color:#aab;
    background-image:
      linear-gradient(45deg,#00000010 25%,transparent 25%,transparent 75%,#00000010 75%),
      linear-gradient(45deg,#00000010 25%,transparent 25%,transparent 75%,#00000010 75%);
    background-size:12px 12px; background-position:0 0,6px 6px; }
  .cellfig img { width:100%; height:100%; object-fit:contain; image-rendering:pixelated; image-rendering:crisp-edges; }
  .cellfig .cap { font-family:var(--mono); font-size:10.5px; color:var(--ink-faint); margin-top:3px; }

  /* ---- flipbook players / 翻页书播放器 ---- */
  .slot { background:var(--panel); border:1px solid var(--line); border-radius:var(--radius);
    box-shadow:var(--shadow); padding:18px; margin:18px 0; }
  .slot-head { display:flex; flex-wrap:wrap; align-items:baseline; gap:6px 12px; margin-bottom:4px; }
  .slot-head h2 { font-family:var(--display); font-size:20px; font-weight:700; margin:0; }
  .slot-note { color:var(--ink-soft); font-size:13px; margin:5px 0 14px; max-width:74ch; }
  .tri { display:flex; gap:14px; flex-wrap:wrap; }
  .tri .col { flex:1 1 200px; min-width:0; background:var(--panel2); border:1px solid var(--line);
    border-radius:11px; padding:11px; }
  .tri .col .who { display:flex; align-items:center; gap:8px; margin-bottom:9px; }
  .tri .col .who .dot { width:11px; height:11px; border-radius:50%; border:1px solid var(--line); flex:none; }
  .tri .col .who .nm { font-weight:700; font-size:13.5px; }
  .tri .col .who .id { font-family:var(--mono); font-size:10.5px; color:var(--ink-faint); }
  .tri .col .stage { position:relative; width:100%; aspect-ratio:1/1; border-radius:9px; overflow:hidden;
    border:1px solid var(--line); background-color:#aab;
    background-image:
      linear-gradient(45deg,#00000010 25%,transparent 25%,transparent 75%,#00000010 75%),
      linear-gradient(45deg,#00000010 25%,transparent 25%,transparent 75%,#00000010 75%);
    background-size:16px 16px; background-position:0 0,8px 8px; }
  .tri .col .stage img { position:absolute; inset:0; width:100%; height:100%;
    object-fit:contain; image-rendering:pixelated; image-rendering:crisp-edges; }
  .bar { display:flex; align-items:center; gap:9px; margin-top:11px; }
  .bar button { padding:7px 11px; font-size:12px; }
  .frame-ind { font-family:var(--mono); font-size:11px; color:var(--ink-soft); white-space:nowrap; }
  .bar input[type=range] { flex:1; min-width:40px; }

  footer { color:var(--ink-faint); font-size:12.5px; margin-top:28px;
    border-top:1px solid var(--line); padding-top:16px; }
  footer code { font-family:var(--mono); color:var(--ink-soft); }

  @media (max-width:560px){
    .tri { gap:10px; }
    .tri .col { flex:1 1 100%; }
    .toolbar .spacer { display:none; }
    h1 { font-size:clamp(26px,8vw,36px); }
    .cellfig .stage { width:62px; height:62px; }
  }
</style>

<div class="wrap">
  <header class="hero">
    <div class="eyebrow">FlipBookGenerator · Tier B 逐帧照搬（chip playback）</div>
    <h1>逐帧照搬 · 六本翻页书</h1>
    <p class="lede">走路的臂腿不再用旋转/缩放「算」出来，而是把参考图<strong>逐帧照搬</strong>：
      前后走的直腿、侧走的 6 种摆腿、被挡的手不插口袋、黑轮廓挨帧照抄。换皮只换颜色，
      形状逐像素不变。下面先看模板，再看双角色播放。</p>
  </header>

  <div class="toolbar" role="group" aria-label="全局控制">
    <div class="grp">
      <button class="primary" id="playAll">全部播放</button>
      <button id="pauseAll">全部暂停</button>
    </div>
    <div class="grp">
      <label class="ctl">速度 <input type="range" id="speed" min="0.25" max="2" step="0.25" value="1"></label>
      <span class="speed-val" id="speedVal">1.0×</span>
    </div>
    <div class="spacer"></div>
    <div class="grp">
      <label class="ctl">主题</label>
      <div class="seg" id="themeSeg">
        <button data-theme-set="light">浅</button>
        <button data-theme-set="dark">深</button>
        <button data-theme-set="auto">自动</button>
      </div>
    </div>
  </div>

  <section class="block" id="templates">
    <div class="block-head"><h2>§1 · 逐帧照搬模板</h2></div>
    <p class="block-note">每本翻页书的逐帧芯片（<strong>即参考图逐帧照搬</strong>，放大显示）。
      腿只有两种棕 + 黑轮廓，黑轮廓逐帧照抄；两棕换色区分跨步；侧走远臂只在 f2/f3 露出。
      换皮 = 把下表的原版色替换成换皮色，<strong>形状一个像素都不动</strong>。</p>

    <div class="pal-wrap">
      <div class="pal-tab">
        <h3>调色板 · 换色映射（按 role 对齐）</h3>
        <table class="pal">
          <thead><tr><th>role</th><th>原版</th><th></th><th>换皮</th></tr></thead>
          <tbody id="palBody"></tbody>
        </table>
      </div>
    </div>

    <div id="tmplRoot"></div>
  </section>

  <section class="block" id="flipbooks">
    <div class="block-head"><h2>§2 · 双角色 6 本翻页书</h2></div>
    <p class="block-note">每本翻页书三列同步播放：<strong>参考格</strong>（原版手绘）|
      <strong>原版</strong>（引擎照搬输出）| <strong>换皮</strong>（只换色）。三列逐帧对齐，
      可逐帧检视像素 —— 参考格与原版应当完全一致（照搬），换皮只在颜色上不同。</p>
    <div id="slots"></div>
  </section>

  <footer>
    本页由 <code>flipbook --chip-render</code>（Tier B 芯片回放）的本地渲染帧内联生成，无云端依赖。
    动作是<strong>逐帧照搬</strong>参考图的手绘像素，不是程序化旋转/缩放 —— 这是 CLAUDE.md 的
    Tier B（chip playback）通道。像素规则全程遵守：直接覆盖、整数贴放、无抗锯齿、无重叠混合。
  </footer>
</div>

<script>
const DATA = __DATA__;

const CHARS = [
  { key:"ref",  name:"参考格", id:"reference", dot:"#8b93ad", tag:"原版手绘 · 照抄源" },
  { key:"orig", name:"原版",   id:"character_32", dot:"#cd683d", tag:"橙发 · 薄荷背心" },
  { key:"alt",  name:"换皮",   id:"chibi_alt", dot:"#5876d0", tag:"蓝发 · 红上衣 · 蓝牛仔" },
];

let SPEED = 1;
const slots = [];

function el(tag, cls, txt){ const e=document.createElement(tag); if(cls)e.className=cls; if(txt!=null)e.textContent=txt; return e; }

// ---- palette table / 调色板表 ----
const palBody = document.getElementById("palBody");
for(const p of DATA.palette){
  const tr = el("tr");
  tr.appendChild(el("td","role",p.role));
  const td1 = el("td"); const s1 = el("span","sw"); s1.style.background = p.std; td1.appendChild(s1); td1.appendChild(document.createTextNode(p.std));
  tr.appendChild(td1);
  tr.appendChild(el("td",null,"→"));
  const td2 = el("td"); const s2 = el("span","sw"); s2.style.background = p.alt; td2.appendChild(s2); td2.appendChild(document.createTextNode(p.alt));
  tr.appendChild(td2);
  palBody.appendChild(tr);
}

// ---- §1 template strips / 模板条 ----
const tmplRoot = document.getElementById("tmplRoot");
for(const s of DATA.slots){
  const wrap = el("div","tmpl");
  const head = el("div","tmpl-head");
  head.appendChild(el("h3",null,s.title));
  head.appendChild(el("span","badge", s.sub));
  head.appendChild(el("span","badge anno", s.annotate));
  wrap.appendChild(head);
  wrap.appendChild(el("p","tmpl-note", s.note));
  const strip = el("div","strip");
  const fr = DATA.frames[s.key].ref;
  for(let i=0;i<fr.length;i++){
    const fig = el("div","cellfig");
    const stage = el("div","stage");
    const img = el("img"); img.src = fr[i]; img.alt = s.title+" f"+i; img.decoding="async";
    stage.appendChild(img); fig.appendChild(stage);
    fig.appendChild(el("div","cap","f"+i));
    strip.appendChild(fig);
  }
  wrap.appendChild(strip);
  tmplRoot.appendChild(wrap);
}

// ---- §2 flipbook slots / 翻页书槽 ----
const slotsRoot = document.getElementById("slots");
function buildSlot(slot){
  const sec = el("section","slot");
  const head = el("div","slot-head");
  head.appendChild(el("h2",null,slot.title));
  head.appendChild(el("span","badge", slot.sub));
  head.appendChild(el("span","badge anno", slot.annotate));
  sec.appendChild(head);
  sec.appendChild(el("p","slot-note", slot.note));

  const tri = el("div","tri");
  const imgs = {};
  for(const c of CHARS){
    const col = el("div","col");
    const who = el("div","who");
    const dot = el("span","dot"); dot.style.background = c.dot;
    who.appendChild(dot);
    who.appendChild(el("span","nm", c.name));
    who.appendChild(el("span","id", c.id + " · " + c.tag));
    col.appendChild(who);
    const stage = el("div","stage");
    const img = el("img"); img.alt = slot.title + " " + c.name; img.decoding="async";
    stage.appendChild(img); col.appendChild(stage);
    tri.appendChild(col);
    imgs[c.key] = img;
  }
  sec.appendChild(tri);

  const bar = el("div","bar");
  const prev = el("button",null,"‹");
  const play = el("button","primary","播放");
  const next = el("button",null,"›");
  const range = el("input"); range.type="range"; range.min=0; range.max=slot.frames-1; range.value=0; range.step=1;
  const ind = el("span","frame-ind","1/" + slot.frames);
  bar.appendChild(prev); bar.appendChild(play); bar.appendChild(next);
  bar.appendChild(range); bar.appendChild(ind);
  sec.appendChild(bar);

  const fr = DATA.frames[slot.key];
  const st = { imgs, fr, idx:0, playing:false, acc:0, last:0, fps:slot.fps, n:slot.frames, range, ind, play };
  function render(){
    for(const k of ["ref","orig","alt"]) st.imgs[k].src = st.fr[k][st.idx];
    range.value = st.idx; ind.textContent = (st.idx+1) + "/" + st.n;
  }
  function setPlay(on){ st.playing = on; play.textContent = on ? "暂停" : "播放"; }
  prev.addEventListener("click", ()=>{ setPlay(false); st.idx=(st.idx-1+st.n)%st.n; render(); });
  next.addEventListener("click", ()=>{ setPlay(false); st.idx=(st.idx+1)%st.n; render(); });
  play.addEventListener("click", ()=> setPlay(!st.playing));
  range.addEventListener("input", ()=>{ setPlay(false); st.idx=parseInt(range.value,10); render(); });
  render();
  slots.push(st);
  return sec;
}
DATA.slots.forEach(s=> slotsRoot.appendChild(buildSlot(s)));

// ---- global rAF playback / 全局 rAF 播放 ----
function tick(now){
  for(const st of slots){
    if(!st.playing){ st.last = now; continue; }
    if(!st.last) st.last = now;
    const dt = now - st.last; st.last = now;
    st.acc += dt;
    const interval = 1000 / (st.fps * SPEED);
    while(st.acc >= interval){
      st.acc -= interval;
      st.idx = (st.idx + 1) % st.n;
      for(const k of ["ref","orig","alt"]) st.imgs[k].src = st.fr[k][st.idx];
      st.range.value = st.idx;
      st.ind.textContent = (st.idx+1) + "/" + st.n;
    }
  }
  requestAnimationFrame(tick);
}
requestAnimationFrame(tick);

document.getElementById("playAll").addEventListener("click", ()=>{
  slots.forEach(s=>{ s.playing=true; s.play.textContent="暂停"; s.last=0; });
});
document.getElementById("pauseAll").addEventListener("click", ()=>{
  slots.forEach(s=>{ s.playing=false; s.play.textContent="播放"; });
});
const speed = document.getElementById("speed"), speedVal = document.getElementById("speedVal");
speed.addEventListener("input", ()=>{ SPEED = parseFloat(speed.value); speedVal.textContent = SPEED.toFixed(2).replace(/0$/,"") + "×"; });

// theme / 主题
const themeSeg = document.getElementById("themeSeg");
function setTheme(mode){
  if(mode==="auto"){ document.documentElement.removeAttribute("data-theme"); }
  else { document.documentElement.setAttribute("data-theme", mode); }
  themeSeg.querySelectorAll("button").forEach(b=> b.setAttribute("aria-pressed", String(b.dataset.themeSet===mode)));
  try{ localStorage.setItem("tierb-theme", mode); }catch(e){}
}
themeSeg.querySelectorAll("button").forEach(b=> b.addEventListener("click", ()=> setTheme(b.dataset.themeSet)));
let saved="auto"; try{ saved = localStorage.getItem("tierb-theme") || "auto"; }catch(e){}
setTheme(saved);
</script>
"""

if __name__ == "__main__":
    main()
