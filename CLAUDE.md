# FlipBookGenerator

Turn AI-generated pixel-art character images into flipbook animations, entirely locally.
把 AI 生成的像素风角色图变成翻页动画，全程本地。

**One image in → animation out. No cloud, no AI dependency in the tool itself.**
**一张图进去，动画出来。无云端，工具本身不依赖 AI。**

## Current state / 当前状态

**V0.5.0 — re-planned. The roadmap below is the source of truth, not the old feature list.**
**V0.5.0 —— 已重新规划。下面的路线图是唯一准绳，旧功能清单不是。**

| What runs today / 今天能跑 | Status |
|---|---|
| Mode 1: multi-image → sprite sheet + preview / 多图拼表 + 预览 | ✅ works |
| Mode 2: single image → 6 body parts + placement metadata / 单图提取部件 + 位置元数据 | ✅ works |
| Grid editor (paint / erase / k-means auto-detect / overview) / 格子编辑器 | ✅ works |
| **Animation generation / 动画生成** | ❌ **not built** — this is the goal |

The CLI mode menu in `src/main.cpp` is **temporary**. It is being replaced by a
guided wizard GUI with a double-clickable `.app`. Do not add features to the menu.
`src/main.cpp` 的模式菜单是**临时的**，正被"可双击 `.app` 的引导式向导"取代。不要往菜单加功能。

---

## ⚠️ The lost-animator incident / animator 丢失事故

**Read this before writing any code. / 动手写代码前先读这段。**

A complete V0.5 animation module (`src/animator.cpp` + `animator.h`) was written,
compiled, run, and produced working output on 2026-06-12 — then deleted from the
working tree **without ever being committed to git**. The project's roadmap stalled
for a long time after that.

一个完整的 V0.5 动画模块（`src/animator.cpp` + `animator.h`）曾被写出、编译、运行，
并在 2026-06-12 产出可用结果 —— 随后从工作区删除，**且从未提交到 git**。之后项目路线图长期停滞。

What survived / 侥幸留下的：
- `build/CMakeFiles/flipbook.dir/src/animator.cpp.o` — an orphan object file
- `output/parts/*.png` + `*_bbox.txt` + `anchors.txt` + `image_path.txt` — its I/O
- `output/animation/` — one failed export test

All of it is now recorded in **`docs/lost-animator-symbols.txt`** (full API, data
model, UI flow, I/O contract, strings). Recovered design highlights:
现已全部记录在 `docs/lost-animator-symbols.txt`（完整 API、数据模型、交互流程、I/O 契约、字符串）。还原出的设计要点：

- A pose was a sparse `std::map<int, PartTransform>` keyed by part id; keyframes
  were lerped element-wise (`interpolate(map, map, float)`).
- Transform field set: `rotate / scaleY / moveX / moveY`.
- I/O: read `output/parts/{png,_bbox.txt,anchors.txt,image_path.txt}`, write
  `output/animation/frame_N.png` + `spritesheet.png`.
- UI was OpenCV highgui + console prompts — the layer this project is now leaving.

**The design is deliberately discarded** (see [Design decisions](#design-decisions));
the *record* stays.

### Lesson / 教训

> **Commit at every verifiable milestone.** Never let a working feature live only in
> the working tree. Small commits are cheap; lost work is not.
> **每个可验证的里程碑都要提交。** 绝不要让能跑的功能只活在工作区里。小提交很便宜，丢工作很贵。

---

## Data formats / 数据格式规格

The part bundle is the project's **interface** between extraction and animation.
Everything downstream reads it. Do not change these formats without updating both
sides and the fixture.
部件包是提取与动画之间的**接口**，下游全靠它读。改格式必须两边同步，并更新夹具。

Written by `runExtractPartsMode()` (`src/extractor.cpp`) to `output/parts/`:

| File | Format | Meaning |
|---|---|---|
| `{name}.png` | BGRA | part sprite cropped to its bbox / 部件精灵图，已裁到包围盒 |
| `{name}_bbox.txt` | `x y w h` | placement, **original-image coordinates** / 位置，**原图坐标** |
| `anchors.txt` | `partId x y` | one rotation pivot per part, original-image coords / 每部件一个旋转锚点 |
| `image_path.txt` | path | source image, relative to project root / 源图路径 |

`name` ∈ `head`, `body`, `arm_left`, `arm_right`, `leg_left`, `leg_right` (ids 1–6, see `src/utils.cpp`).

**Reconstruction invariant / 重建不变量** — the correctness gate for the whole pipeline:
> Blit each `{name}.png` at its `_bbox.txt` offset onto a transparent canvas of the
> source size. The result must equal the source image **pixel-exact, including alpha**.
> 把每个部件按 bbox 偏移贴回透明画布，结果必须**逐像素（含 alpha）**等于源图。

If this fails, extraction lost pixels and every animation frame built on top will be
wrong. It is checked headlessly (`--recon-test`) and via a Verify button in the GUI.
**Committed fixture / 已提交夹具**: `tests/fixtures/character_32/` — a complete,
verified bundle from `assets/character_32.png`. All 6 part PNG sizes match their
bbox w/h exactly. Use it as the test input; do not regenerate it casually.

---

## Design decisions / 设计决策

### Why not outline-based detection? / 为什么不基于描边检测？

If outlines are not perfectly drawn, detection fails. Too fragile for real use.
描边不完美时检测失败，太脆弱。

### Why not layered export? / 为什么不直接分层导出？

Requiring PS/Aseprite layer export adds workflow friction, and AI-generated images
have no layers. We want "one image in, animation out".
要求分层导出增加摩擦，AI 生成的图没有图层。我们要"一张图进去，动画出来"。

### Why grid division for pixel art? / 为什么用格子划分？

Pixel art is low-res and grid-based by nature. A grid overlay lets users assign
pixels to parts manually, with auto-detection as a starting point. Robust and flexible.
像素风本身低分辨率且基于格子。格子覆盖让用户手动分配像素，自动检测当起点。稳健又灵活。
Works with any single image, no outline dependency, ~1024 cells max at 32x32,
local computation only.

### GUI: Dear ImGui (docking) + GLFW + OpenGL3 / GUI 选型

| | Dear ImGui | Qt |
|---|---|---|
| Dependency | FetchContent, statically linked | Homebrew qt, hundreds of MB |
| Packaged `.app` | single binary; only OpenCV dylibs need bundling | `macdeployqt`, fragile |
| Zoomable pixel canvas | DrawList — its home turf | heavier, fights pixel-exact blits |
| Licence | MIT / Zlib | LGPL |

Target audience is game developers; a tool-style UI is the *right* look. Native file
dialogs via **portable-file-dialogs** (single header, MIT).
配套用 portable-file-dialogs 做原生文件框。

**Architectural rule / 架构铁律**: `flipbook_core` never includes a UI header;
`ui/` never calls `cv::imshow`. This is what makes a future framework swap survivable.
`core/` 永不 include UI 头；`ui/` 永不调用 `cv::imshow`。将来换框架才活得下来。

### Packaging / 打包

OpenCV stays, and its dylibs are bundled into the `.app` with `dylibbundler` into
`Contents/Frameworks/` with `@rpath` fixed. The killer failure is a `.app` that
crashes on launch on a machine without Homebrew — always test on a clean machine.
最大坑是没装 Homebrew 的机器上双击即崩 —— 一定要在干净机器上测。

Rejected alternative / 已排除的备选：drop OpenCV entirely (stb_image + hand-rolled
k-means/resize/bbox) for a zero-dependency single binary. Attractive — `cv::resize`
defaults to bilinear, which is **wrong** for pixel art — but it is a second
architectural change on top of the GUI migration. Revisit only if bundling proves painful.

### Skeleton vs. template — how 3 views become 3 flipbooks / 骨架与模板分离

The hard problem: front, side and back are three *different* flipbooks, and left/right
are a fourth and fifth. Solved by splitting per-view rig data from view-independent
animation templates.
难点是正面/侧面/背面是三套不同的翻页书，左右又是两套。解法是把**每视图的骨架数据**
和**与视图无关的动画模板**分开。

```
        动画模板（视图无关）: walkSide / walkFront / walkUp / idle
                    │ 套用到
   ┌────────────────┼────────────────┐
 正面骨架        侧面骨架          背面骨架
   │                │                │
 走路向下        走路向右 ──镜像──▶ 走路向左
```

One template × three rigs = three different flipbooks. Left is a horizontal mirror of
right (exact and free for pixel art), so **3 input images yield 4 walking directions
plus idle**. Three templates are needed because a side view's legs swing by rotation
while a front view's legs read as foreshortening — in-plane rotation looks wrong there.
一套模板 × 三个骨架 = 三套翻页书。左右靠水平镜像，所以 **3 张图给出 4 方向行走 + 待机**。
需要三套模板，因为侧面的腿靠旋转、正面的腿是遮挡关系，平面内旋转在正面很怪。

### Animation data model / 动画数据模型

Flat, **one free-placed pivot per part** (the old 2-anchor bone model is discarded —
see `docs/lost-animator-symbols.txt`). Structures live in `src/core/` and depend on
no UI or image-library types. See [Roadmap Phase 1](#phase-1--animation-core-engine-no-ui-).
扁平模型，**每部件一个自由放置的 pivot**（旧的 2 锚点骨骼模型已推翻）。结构体在
`src/core/`，不依赖 UI 或图像库类型。

Presets (`makeIdle` / `makeWalkSide` / `makeWalkFront` / `makeWalkUp`) are **data
generators that fill the same `Animation` struct** the editor edits — not separate
code paths. Idle and walk therefore ship before any GUI editor exists.
预设是**填充同一个 `Animation` 结构的数据生成器**，不是独立代码路径。所以待机和走路
能在任何 GUI 编辑器存在之前就先跑出来。

### Pixel-art transform rules / 像素风变换硬约束

Non-negotiable. Encode in `src/core/transform.cpp` and never violate in the render path.
没有商量余地。写进 `src/core/transform.cpp`，渲染路径永不违反。

- **Nearest-neighbour only.** Never `INTER_LINEAR`/`INTER_CUBIC`. (`spritesheet.cpp`
  uses `INTER_AREA` for multi-frame normalisation — that is a different path and fine.)
- **Rotation rasterises by inverse mapping + nearest sampling.** Forward-sampling a
  rotation punches holes; inverse mapping does not.
- **Translations round to the integer pixel grid** at raster time. This *is* the
  pixel-perfect mode; there is no sub-pixel option.
- **No anti-aliasing anywhere. No blending at part overlaps** — z-order wins.
  Semi-transparent edge blending instantly breaks pixel perfection.
- Off-multiple-of-90° rotations look chunky. That is the idiom, not a bug. Alt snaps
  to 15° in the editor.

### ⚠️ Quality expectation / 质量预期

**Rotating parts yields puppet / skeletal-animation motion, not hand-drawn pixel art.**
The input is a small number of static images and the motion is computed. This is
inherently unable to reach Aseprite hand-drawn quality.

**旋转部件得到的是"提线木偶/骨骼动画"式的活动感，不是逐帧手绘像素画的质感。**
输入是少数几张静态图，动作是算出来的，原理上到不了 Aseprite 手绘的程度。

Fine for prototyping, game jams and procedural animation. Say this plainly in the
README — do not market it as hand-drawn. If hand-drawn quality is required, the answer
is "have the AI draw multiple poses", not "compute the motion"; that is a possible
extension (two-pose tweening), not this plan's main line.

**16x16 warning / 16x16 警告**: a leg may be 3x6 px, where rotation is invisible.
**32x32 is the recommended minimum.** Below that, auto-degrade to translation-only
templates.

---

## Roadmap / 路线图

Every phase ends **runnable and visible**. Value first: the core mechanism is proven
*before* any GUI exists, because "does rotating pixel parts look acceptable?" is the
project's biggest unknown.
每阶段结束都**能跑能看见**。价值优先：核心机制在任何 GUI 存在之前就先被证明，因为
"旋转像素部件到底好不好看"是本项目最大的未知数。

### Phase 0 — Init / 初始化 ✅

Repo hygiene and truthful docs. Untrack `build/` + `output/`, commit
`tests/fixtures/character_32/`, archive the lost animator's symbols, write placement
metadata + sensible default pivots, single version source, rewrite this file.

### Phase 1 — Animation core engine (no UI) / 动画核心引擎（无 UI）

⭐ Highest risk, done first, without UI.

`src/core/` — pure C++, links OpenCV only, never includes a UI header:
`image_io`, `rig`, `transform`, `animation`, `composer`, `exporter`.

1. **First task is the reconstruction gate** — read the fixture, blit back, must be
   pixel-exact against `assets/character_32.png`. Fix extraction before animating.
2. Headless entry: `--recon-test`, `--preset idle|walk --frames N --out DIR`.
3. Eyeball the output strips. **The single most important manual review in the project.**
   If it looks bad, tune here (less rotation, translate-only, different fps) — not in the GUI.

### Phase 2 — GUI shell + packaging / GUI 壳 + 打包

ImGui/GLFW/GL3 via FetchContent; targets split into `flipbook_core` (lib) + `flipbook` +
`tests`. **Day-one spike gate: window + textured quad + mouse coords. Do not write
panels until that passes** — GLFW/OpenGL3 build friction on macOS arm64 is a real risk.
Then: wizard state machine (5 steps), `ui/canvas` (zoomable pixel canvas — the shared
foundation), `build_app.sh` producing `output/dist/FlipBookGenerator.app`, real import step.

### Phase 3 — Split + anchors in the wizard / 切分 + 锚点进向导

Port `extractor.cpp`'s *algorithms* to the canvas (partMap, paint/erase, k-means).
**Discard** `buildDisplayImage`/`buildOverviewImage` — per-pixel `cv::rectangle` is too
slow; draw with ImGui DrawList. Anchor placement per part with sensible defaults
(see `defaultPivot`). End with a reconstruction preview + Verify button.

### Phase 4 — Single-view preset export / 单视图预设动画导出 ⭐ Milestone A

Preset cards (walk / idle) with live preview + sliders (frames, fps, rotation amount,
bob amount). Export step with playback (port the `spritesheet.cpp` preview controls to
buttons/sliders), spritesheet PNG / frame sequence / both.
**Golden invariant: GUI export must be byte-identical to CLI export of the same data.**
*This is where "one image in, animation out" becomes real.*

### Phase 5 — Multi-view batch / 多视图批处理 ⭐⭐ Milestone B = **done**

1–3 views (front/side/back), each with its own rig. One set of template parameters
applied across all rigs. Direction mapping + mirroring (down=front, up=back,
right=side, left=side mirrored). Game-ready export: one spritesheet PNG per animation
+ engine-agnostic JSON metadata (frame size, count, fps, loop flag, name → frame range).
*This is what the tool is actually for.*

### Phase 6 — (optional) Custom keyframe tool / 自定义关键帧工具

Timeline UI, per-part transform editing, project save/load (`.fba.json`).
**Deferred on purpose**: in a multi-view workflow hand-editing keyframes is a burden,
not a help — you would be editing 3 rigs × N animations. Presets + sliders are the
right interaction. Kept because it was originally requested; not counted toward "done".
**Alternative extension if quality falls short / 质量不足时的备选扩展**: accept 2 images
per view (stand + walk pose) and tween between them. Much better output, still fully
automatic, costs 6 input images instead of 3.

### Phase 7 — Polish / 收尾

Icon, About, error messages, README rewrite, ARCHITECTURE.md refresh, distribution script.

**"Done" = Phase 5. / "完成" = Phase 5.** Phase 6/7 can be deferred indefinitely.

---

## Project structure / 项目结构

```
FlipBookGenerator/
├── CLAUDE.md               # this file — true state, incident, formats, decisions
├── ARCHITECTURE.md         # module map + function reference
├── docs/
│   └── lost-animator-symbols.txt   # forensic record of lost work
├── src/
│   ├── main.cpp            # TEMPORARY CLI menu + future CLI arg dispatch
│   ├── version.h.in        # generated to build/generated/version.h
│   ├── utils.h/cpp         # PartDef, PARTS, defaultPivot()
│   ├── spritesheet.h/cpp   # Mode 1: multi-image sprite sheet + preview
│   └── extractor.h/cpp     # Mode 2: grid editor + part extraction
├── tests/
│   └── fixtures/character_32/      # committed verified part bundle
├── assets/                 # input images
└── output/                 # 100% generated, 100% gitignored
```

Planned additions / 计划新增：`src/core/` (UI-free engine), `src/extract/` (algorithms),
`src/ui/` (ImGui wizard), `third_party/`, `build_app.sh`, `CMakeLists.txt` target split.

## Build / 构建

```bash
mkdir -p build && cd build
cmake ..
make
cd ..
./build/flipbook
```

Requires CMake 3.14+, C++17, OpenCV 4.x (Homebrew: `/opt/homebrew`), macOS + VSCode.

## Conventions / 约定

- **Bilingual comments: English on top, Chinese below.** / 注释双语：英文在上，中文在下
- All image processing runs locally. No cloud, no AI dependency in the tool.
  / 所有图像处理本地运行，无云端，工具本身不依赖 AI
- `std::filesystem` for directory traversal (C++17) / 用 std::filesystem 遍历目录
- `cv::IMREAD_UNCHANGED` preserves alpha / IMREAD_UNCHANGED 保留 Alpha
- Keep modules small and headers minimal / 模块保持小，头文件保持精简

## Working rules / 工作规则

- **Commit at every verifiable milestone** (see the incident above).
  / **每个可验证的里程碑都要提交**（见上文事故）
- Keep `flipbook_core` free of UI includes; keep `ui/` free of `cv::imshow`.
- Never violate the pixel-art transform rules in the render path.
- Do not polish the GUI past the stated minimum panel set per phase — GUI scope creep
  is the real schedule killer. Docking polish, sub-pixel modes and GIF export are
  explicitly out of scope.
