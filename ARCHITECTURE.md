# Architecture / 架构文档

This document describes the file structure, module relationships, and function references for FlipBookGenerator.
本文档描述 FlipBookGenerator 的文件结构、模块关系和函数参考。

**Companion document / 配套文档**: `CLAUDE.md` holds the data-format spec, the lost-animator
incident, the design decisions and the roadmap. This file is the module-level reference.
`CLAUDE.md` 记录数据格式规格、animator 丢失事故、设计决策与路线图。本文件是模块级参考。

## Module Relationships / 模块关系

```
main.cpp
  ├── includes version.h        (generated from src/version.h.in)
  ├── includes spritesheet.h → calls runSpriteSheetMode()
  └── includes extractor.h  → calls runExtractPartsMode()

spritesheet.cpp
  └── includes <opencv2/opencv.hpp> (no internal deps)

extractor.cpp
  ├── includes extractor.h
  └── includes utils.h → uses PARTS, getPartName(), defaultPivot()

utils.cpp
  └── includes utils.h
```

## File Reference / 文件参考

### main.cpp — Entry Point / 入口文件

Program entry point with a **temporary** mode selection menu.
程序入口，含**临时**模式选择菜单。

> The menu is being replaced by a guided wizard GUI (Dear ImGui). Do not add features here.
> 该菜单正被 Dear ImGui 引导式向导取代。不要在这里加功能。

| Function | Description |
|----------|-------------|
| `main()` | Display mode menu (1=Sprite Sheet, 2=Extract Parts), call corresponding mode function. Version string comes from `FLIPBOOK_VERSION`. 显示模式菜单，调用对应模式函数。版本串取自 `FLIPBOOK_VERSION`。 |

---

### version.h.in — Version Header / 版本号头文件

Processed by CMake `configure_file` into `build/generated/version.h`.
由 CMake `configure_file` 生成到 `build/generated/version.h`。

| Symbol | Description |
|--------|-------------|
| `FLIPBOOK_VERSION` | Version string. **Single source of truth is `project(VERSION ...)` in CMakeLists.txt.** 版本串。**唯一来源是 CMakeLists.txt 的 `project(VERSION ...)`。** |

---

### utils.h / utils.cpp — Common Definitions / 公共定义

Shared data structures and helper functions used across modules.
跨模块共享的数据结构和辅助函数。

| Symbol | Type | Description |
|--------|------|-------------|
| `PartDef` | struct | Body part definition with id, name, and highlight color. 身体部件定义，包含 ID、名称和高亮颜色。 |
| `PARTS` | const vector | All 6 body parts: head(1), body(2), arm_left(3), arm_right(4), leg_left(5), leg_right(6). 6 个身体部件。 |
| `getPartName(int id)` | function | Returns part name string by ID. 根据 ID 返回部件名称。 |
| `defaultPivot(int partId, const cv::Rect& bbox)` | function | Default rotation pivot in original-image coords, per part joint: head→neck (bottom centre), body→shoulder line (top centre), arms→top edge on the torso side, legs→hip (top centre). **Supersedes the old 2-anchor bbox-centre model**, which placed arm pivots on each arm's own mid-axis instead of at the shoulder. 返回部件默认旋转锚点（原图坐标），按关节取值：头=颈部、躯干=肩线、手臂=靠躯干一侧顶端、腿=胯。**取代旧的 2 锚点包围盒中点模型**（那会把手臂锚点放在手臂自身中轴上而非肩关节）。 |

---

### spritesheet.h / spritesheet.cpp — Sprite Sheet Module / Sprite Sheet 模块

Sprite sheet generation from multiple images, with animation preview.
从多张图片生成 Sprite Sheet，含动画预览。

| Function | Description |
|----------|-------------|
| `runSpriteSheetMode()` | Main function for Mode 1. Handles: target size input, image loading, normalization (scale-to-fit + center on transparent canvas), horizontal concatenation, sprite sheet output, and animation preview loop. 模式一主函数：目标尺寸输入、图片加载、标准化（缩放适配+居中透明画布）、横向拼接、输出、动画预览循环。 |

**Internal flow / 内部流程:**
1. Ask target frame size / 询问目标帧尺寸
2. Load PNGs from assets/ / 从 assets/ 加载 PNG
3. Normalize each frame (BGRA, scale, center) / 标准化每帧
4. cv::hconcat → save spritesheet / 拼接并保存
5. Optional animation preview / 可选动画预览

---

### extractor.h / extractor.cpp — Body Part Extractor / 身体部件提取模块

Grid-based editor for marking and extracting body parts from a single image.
格子编辑器，用于标记和提取单张图片中的身体部件。

| Symbol | Type | Description |
|----------|------|-------------|
| `EditorState` | struct | Editor state: partMap (pixel→partID), currentPart (1-6), mode (0=paint, 1=eraser, 2=overview), eraserTarget, zoomFactor, needsRedraw, originalImage. 编辑器状态：部件映射、当前部件、模式（涂色/橡皮/总览）、橡皮目标、缩放倍数、是否需要重绘、原始图片。 |
| `onMouse()` | function (static) | Mouse callback: left click/drag to paint or erase (eraser only erases target part). Converts display coords to image coords. 鼠标回调：左键涂色/擦除（橡皮仅擦除目标部件），坐标转换。 |
| `buildDisplayImage()` | function (static) | Builds display image: original image visible, light gray bg for transparent areas, light blue-gray overlay for assigned cells, colored border + cross for selected cells. 构建显示图片：原图可见、透明区域浅灰背景、已分配格子浅蓝灰覆盖、选中格子彩色边框+十字。 |
| `buildOverviewImage()` | function (static) | Builds overview image: original image + colored boundary outlines for all parts (no fill). 构建总览图片：原图 + 所有部件的彩色边界轮廓（无填充）。 |
| `autoDetectParts()` | function | Auto detect body parts using k-means color clustering. Groups non-transparent pixels by color, maps clusters to parts by vertical position (top=head, bottom=legs). 使用 k-means 颜色聚类自动检测身体部件，按垂直位置映射。 |
| `runExtractPartsMode()` | Main function for Mode 2. Handles: image loading, detection mode selection (manual/auto/auto+refine), editor loop, part extraction, save as transparent PNGs. 模式二主函数：图片加载、检测模式选择、编辑器循环、部件提取、保存。 |

**Internal flow / 内部流程:**
1. Ask image path, load as BGRA / 询问路径，加载为 BGRA
2. Initialize partMap (all zeros) / 初始化部件映射
3. Editor loop: number keys select part, mouse paints / 编辑器循环
4. On Enter: extract each part via mask + bounding box crop / 回车后提取
5. Save the part bundle to `output/parts/` — sprite **and** placement metadata / 保存部件包（精灵图**及**位置元数据）

**Output contract / 输出契约** — see `CLAUDE.md` § *Data formats* for the full spec:

| File | Format | Purpose |
|---|---|---|
| `{name}.png` | BGRA | part sprite cropped to its bbox / 部件精灵图，已裁到包围盒 |
| `{name}_bbox.txt` | `x y w h` | placement in original-image coords / 原图坐标中的位置 |
| `anchors.txt` | `partId x y` | one rotation pivot per part (`defaultPivot`) / 每部件一个旋转锚点 |
| `image_path.txt` | path | source image, for reconstruction / 源图路径，用于重建 |

> The bbox is computed at the crop step and used to be discarded there, which left the
> cropped PNG with no position to return to. Writing it out is what makes the
> **reconstruction invariant** checkable: blit parts at their bbox offsets and the result
> must equal the source pixel-exact.
> bbox 在裁剪时算出却曾被丢弃，导致裁出的 PNG 无处可归。写出它才使**重建不变量**可校验：
> 按 bbox 偏移贴回部件，结果必须与源图逐像素相同。

## Key OpenCV Functions Used / 使用的关键 OpenCV 函数

| Function | Used In | Purpose |
|----------|---------|---------|
| `cv::imread` | Both | Load PNG images / 加载 PNG |
| `cv::imwrite` | Both | Save PNG images / 保存 PNG |
| `cv::hconcat` | spritesheet | Horizontal concatenation / 横向拼接 |
| `cv::resize` | Both | Scale images / 缩放图片 |
| `cv::cvtColor` | Both | Color space conversion / 颜色空间转换 |
| `cv::imshow` | Both | Display image in window / 窗口显示图片 |
| `cv::waitKey` | Both | Wait for keyboard input / 等待按键 |
| `cv::setMouseCallback` | extractor | Register mouse handler / 注册鼠标回调 |
| `cv::addWeighted` | extractor | Blend overlay with image / 混合覆盖层 |
| `cv::findContours` | extractor | Find contours for bounding box / 查找轮廓获取边界框 |
| `cv::bitwise_and` | extractor | Apply mask to extract part / 应用掩膜提取部件 |
| `cv::kmeans` | extractor | Color clustering for auto detection / 颜色聚类自动检测 |

## Planned Structure / 计划中的结构

The animation engine is built **UI-free first** so the core mechanism can be proven
without a GUI. Target split in CMake: `flipbook_core` (static lib) + `flipbook` + `tests`.
动画引擎**先做成无 UI**，以便不靠 GUI 就验证核心机制。CMake 目标拆分为
`flipbook_core`（静态库）+ `flipbook` + `tests`。

```
src/
├── main.cpp            # TEMPORARY CLI menu → CLI arg dispatch + GUI launch
├── core/               # ⚠️ never includes a UI header / 永不 include UI 头
│   ├── image_io.*      # BGRA load/save
│   ├── rig.*           # Rig; read/write the part bundle (bbox/anchors/image_path)
│   ├── transform.*     # PartTransform, Pose, lerpAngleDeg, inverse-map raster
│   ├── animation.*     # Keyframe, Animation, poseAt() + preset generators
│   ├── composer.*      # compose a frame by z-order; reconstruction test
│   └── exporter.*      # frames → frame_N.png + spritesheet.png
├── extract/
│   └── extractor.*     # partMap + k-means; algorithms only, no imshow
└── ui/                 # ⚠️ never calls cv::imshow / 永不调用 cv::imshow
    ├── app.*           # shell + wizard state machine
    ├── canvas.*        # zoomable pixel canvas (shared foundation)
    └── step_*.*        # import / split / anchor / animate / export
```

See `CLAUDE.md` § *Roadmap* for what each phase delivers and how it is verified.

## Version History / 版本历史

| Version | Changes |
|---------|---------|
| V0.1 | Basic sprite sheet generation / 基础 Sprite Sheet 生成 |
| V0.2 | Auto-normalize frame sizes + fixed size / 自动统一帧尺寸 + 固定尺寸 |
| V0.2.1 | Runtime target frame size prompt / 运行时询问目标帧尺寸 |
| V0.3 | Animation preview / 动画预览 |
| V0.4 | Body part extraction via grid editor / 格子编辑器部件提取 |
| V0.4.1 | Code modularization / 代码模块化 |
| V0.4.2 | Auto detection (k-means) + manual refinement / 自动检测 + 人工微调 |
| V0.4.3 | Editor UX: white bg, eraser, highlight current part / 编辑器 UX 改进 |
| V0.4.4 | Editor display fix: original visible, smart eraser / 编辑器显示修复 |
| V0.4.5 | Editor visibility: colored border + cross for selected cells / 编辑器可见性改进 |
| V0.4.6 | Thinner border, overview mode (key 9) / 更细边框、总览模式 |
| V0.5.0 | **Re-planned.** Repo hygiene, committed test fixture, forensic archive of the lost animator, placement metadata + default pivots, single version source. Animation engine not yet built. / **重新规划。** 仓库清理、提交测试夹具、丢失的 animator 法医归档、位置元数据 + 默认锚点、版本号单一来源。动画引擎尚未构建。 |
