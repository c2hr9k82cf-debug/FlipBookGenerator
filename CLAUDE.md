# FlipBookGenerator

Animation resource generation tool for indie game developers.
面向独立游戏开发者的动画资源生成工具。

## Current Version / 当前版本

V0.2

## Completed Features / 已完成功能

- V0.1: Read PNG images from assets/, sort by filename, concatenate horizontally to sprite sheet, output to output/spritesheet.png
  读取 assets/ 中 PNG 图片，按文件名排序，横向拼接为 Sprite Sheet，输出到 output/spritesheet.png
- V0.2: Auto-normalize frame sizes with transparent background, center-align each frame, then concatenate
  自动统一帧尺寸（透明背景），居中对齐每帧，再进行拼接
- V0.2.1: Runtime prompt for target frame size (default: 32), scale-to-fit with aspect ratio preserved
  运行时询问目标帧尺寸（默认 32），按比例缩放适配

## Planned Features / 待开发功能

- Auto-slicing / 自动切图
- Animation preview / 动画预览
- Auto-frame interpolation / 自动补帧
- Multiple export formats / 多种导出格式
- AI model integration / AI 模型接入

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
│   └── main.cpp        # Main program / 主程序
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
