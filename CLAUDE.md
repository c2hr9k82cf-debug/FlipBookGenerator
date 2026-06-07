# FlipBookGenerator

面向独立游戏开发者的动画资源生成工具。

## 当前版本

V0.1

## 已完成功能

- V0.1: 读取 assets/ 中 PNG 图片，按文件名排序，横向拼接为 Sprite Sheet，输出到 output/spritesheet.png

## 待开发功能

- 图片尺寸统一/自动对齐
- 自动切图
- 动画预览
- 自动补帧
- 多种导出格式
- AI 模型接入

## 技术栈

- 语言: C++17
- 构建: CMake 3.14+
- 图像库: OpenCV 4.x (via Homebrew)
- 环境: macOS + VSCode

## 项目结构

```
FlipBookGenerator/
├── CLAUDE.md           # 项目记录
├── CMakeLists.txt      # 构建配置
├── src/
│   └── main.cpp        # 主程序
├── assets/             # 输入图片
└── output/             # 输出目录
```

## 构建方式

```bash
mkdir -p build && cd build
cmake ..
make
cd ..
./build/flipbook
```

## 技术决策

- 使用 std::filesystem 遍历目录（C++17）
- 使用 cv::hconcat 横向拼接图片
- IMREAD_UNCHANGED 保留 Alpha 通道
- 单文件结构，不过度拆分
