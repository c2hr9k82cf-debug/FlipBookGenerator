# FlipBookGenerator

Animation resource generation tool for indie game developers.
面向独立游戏开发者的动画资源生成工具。

## Current Version / 当前版本

V0.4.1

## Completed Features / 已完成功能

- V0.1: Read PNG images from assets/, sort by filename, concatenate horizontally to sprite sheet, output to output/spritesheet.png
  读取 assets/ 中 PNG 图片，按文件名排序，横向拼接为 Sprite Sheet，输出到 output/spritesheet.png
- V0.2: Auto-normalize frame sizes with transparent background, center-align each frame, then concatenate
  自动统一帧尺寸（透明背景），居中对齐每帧，再进行拼接
- V0.2.1: Runtime prompt for target frame size (default: 32), scale-to-fit with aspect ratio preserved
  运行时询问目标帧尺寸（默认 32），按比例缩放适配
- V0.3: Animation preview with adjustable FPS, pause/resume, step-through controls
  动画预览，支持帧率调节、暂停/继续、逐帧播放
- V0.4: Body part extraction via grid-based editor, mouse painting with 6 fixed parts (head, body, arms, legs)
  身体部件提取，格子编辑器，鼠标涂色，6 个固定部件（头、躯干、手臂、腿）
- V0.4.1: Code modularization — split main.cpp into utils, spritesheet, extractor modules
  代码模块化 — 将 main.cpp 拆分为 utils、spritesheet、extractor 模块

## Roadmap / 版本路线图

### V0.4: Body Part Extraction / 身体部件提取 ✅

Load a single character image (pixel art), use grid-based manual division to extract body parts (head, body, arms, legs) as separate PNG layers.
加载单张角色图（像素风），通过格子人工划分，提取身体部件（头、躯干、手臂、腿）为独立 PNG 图层。

### V0.5: Skeleton & Pose Definition / 骨骼与姿势定义

Define anchor points (pivot) for each body part (e.g., shoulder for arms, hip for legs).
为每个部件定义旋转锚点（如手臂锚点在肩膀，腿锚点在胯部）。

- Interactive anchor point placement
  交互式锚点设置
- Pose editor: rotate/translate body parts to define keyframe poses
  姿势编辑器：旋转/平移部件定义关键帧姿势

### V0.6: Tweening Animation / 补间动画

Automatically generate in-between frames by interpolating keyframe poses.
通过关键帧姿势插值，自动生成中间帧。

- Linear interpolation of rotation angles and positions
  旋转角度和位置的线性插值
- Configurable number of tween frames
  可配置补间帧数
- Generate and export flipbook animation
  生成并导出翻页书动画

### V0.7: Multi-view Fusion / 多视图融合

Use front, side, and back views to generate multi-angle animations.
利用正面、侧面、背面视图生成多角度动画。

## Design Discussion / 设计讨论

### Why not outline-based detection? / 为什么不基于描边检测？

Initial idea was to use black outlines to separate body parts. Problem: if outlines are not perfectly drawn, detection fails. Too fragile for real-world use.
最初设想用黑色描边分隔部件。问题：描边不完美时检测失败，太脆弱。

### Why not layered export? / 为什么不直接分层导出？

Requiring users to export layers from PS/Aseprite adds workflow friction. AI-generated images can't be layered. We want "one image in, animation out".
要求用户从 PS/Aseprite 分层导出增加了工作流摩擦。AI 生成的图片无法分层。我们希望"一张图进去，动画出来"。

### Final approach: Grid division for pixel art / 最终方案：像素风格子划分

Pixel art is low-res and grid-based by nature. A grid overlay tool lets users manually assign pixels to body parts. Combined with auto detection as a starting point, this is both robust and flexible.
像素风本身低分辨率且基于格子。格子覆盖工具让用户手动分配像素到各部件。结合自动检测作为起点，既稳健又灵活。

- Works with any single image (AI-generated or hand-drawn)
  适用于任何单张图片（AI 生成或手绘）
- No dependency on outline quality
  不依赖描边质量
- Small grid count for pixel art (32x32 = 1024 cells max)
  像素风格子数量少（32×32 最多 1024 格）
- Local computation, no cloud, no AI dependency in the tool itself
  本地计算，无云端，工具本身不依赖 AI

## Tech Stack / 技术栈

- Language / 语言: C++17
- Build / 构建: CMake 3.14+
- Image Library / 图像库: OpenCV 4.x (via Homebrew)
- Environment / 环境: macOS + VSCode

## Project Structure / 项目结构

```
FlipBookGenerator/
├── CLAUDE.md           # Project documentation / 项目记录
├── CMakeLists.txt      # Build config / 构建配置
├── src/
│   ├── main.cpp        # Entry point + mode selection / 入口 + 模式选择
│   ├── utils.h/cpp     # Common definitions / 公共定义
│   ├── spritesheet.h/cpp # Sprite sheet generation / Sprite Sheet 生成
│   └── extractor.h/cpp # Body part extraction / 身体部件提取
├── assets/             # Input images / 输入图片
└── output/             # Output directory / 输出目录
```

## Build Instructions / 构建方式

```bash
mkdir -p build && cd build
cmake ..
make
cd ..
./build/flipbook
```

## Technical Decisions / 技术决策

- Use std::filesystem for directory traversal (C++17)
  使用 std::filesystem 遍历目录（C++17）
- Use cv::hconcat for horizontal concatenation
  使用 cv::hconcat 横向拼接图片
- IMREAD_UNCHANGED preserves alpha channel
  IMREAD_UNCHANGED 保留 Alpha 通道
- Auto-normalize to max dimensions with transparent BGRA canvas
  自动统一到最大尺寸，使用透明 BGRA 画布居中对齐
- Single-file architecture, keep it simple
  单文件结构，不过度拆分
- Bilingual comments: English on top, Chinese below
  注释双语：英文在上，中文在下
- All image processing runs locally, no cloud dependency
  所有图像处理本地运行，无云端依赖
