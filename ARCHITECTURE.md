# Architecture / 架构文档

This document describes the file structure, module relationships, and function references for FlipBookGenerator.
本文档描述 FlipBookGenerator 的文件结构、模块关系和函数参考。

## Module Relationships / 模块关系

```
main.cpp
  ├── includes spritesheet.h → calls runSpriteSheetMode()
  └── includes extractor.h  → calls runExtractPartsMode()

spritesheet.cpp
  └── includes <opencv2/opencv.hpp> (no internal deps)

extractor.cpp
  ├── includes extractor.h
  └── includes utils.h → uses PARTS, getPartName()

utils.cpp
  └── includes utils.h
```

## File Reference / 文件参考

### main.cpp — Entry Point / 入口文件

Program entry point with mode selection menu.
程序入口，包含模式选择菜单。

| Function | Description |
|----------|-------------|
| `main()` | Display mode menu (1=Sprite Sheet, 2=Extract Parts), call corresponding mode function. 显示模式菜单，调用对应模式函数。 |

---

### utils.h / utils.cpp — Common Definitions / 公共定义

Shared data structures and helper functions used across modules.
跨模块共享的数据结构和辅助函数。

| Symbol | Type | Description |
|--------|------|-------------|
| `PartDef` | struct | Body part definition with id, name, and highlight color. 身体部件定义，包含 ID、名称和高亮颜色。 |
| `PARTS` | const vector | All 6 body parts: head(1), body(2), arm_left(3), arm_right(4), leg_left(5), leg_right(6). 6 个身体部件。 |
| `getPartName(int id)` | function | Returns part name string by ID. 根据 ID 返回部件名称。 |

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
| `EditorState` | struct | Editor state: partMap (pixel→partID), currentPart, zoomFactor, needsRedraw, originalImage. 编辑器状态：部件映射、当前部件、缩放倍数、是否需要重绘、原始图片。 |
| `onMouse()` | function (static) | Mouse callback: left click/drag assigns current part, right click/drag erases. Converts display coords to image coords. 鼠标回调：左键涂色、右键擦除，坐标转换。 |
| `buildDisplayImage()` | function (static) | Builds display image: scales up original, overlays semi-transparent part colors, draws grid lines. 构建显示图片：放大原图、叠加半透明部件颜色、绘制网格线。 |
| `runExtractPartsMode()` | Main function for Mode 2. Handles: image loading, editor loop (mouse + keyboard), part extraction (mask + crop), save as transparent PNGs. 模式二主函数：图片加载、编辑器循环、部件提取（掩膜+裁切）、保存为透明 PNG。 |

**Internal flow / 内部流程:**
1. Ask image path, load as BGRA / 询问路径，加载为 BGRA
2. Initialize partMap (all zeros) / 初始化部件映射
3. Editor loop: number keys select part, mouse paints / 编辑器循环
4. On Enter: extract each part via mask + bounding box crop / 回车后提取
5. Save to output/parts/{name}.png / 保存到 output/parts/

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

## Version History / 版本历史

| Version | Changes |
|---------|---------|
| V0.1 | Basic sprite sheet generation / 基础 Sprite Sheet 生成 |
| V0.2 | Auto-normalize frame sizes + fixed size / 自动统一帧尺寸 + 固定尺寸 |
| V0.3 | Animation preview / 动画预览 |
| V0.4 | Body part extraction via grid editor / 格子编辑器部件提取 |
| V0.4.1 | Code modularization / 代码模块化 |
