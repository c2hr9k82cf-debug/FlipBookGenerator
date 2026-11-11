// assets_locator.h — find the bundled assets at runtime / 运行时定位捆绑资源
//
// Search order: $FLIPBOOK_ASSETS (override), then next to the executable
// (the .app Resources layout), then the build tree, then the cwd. First
// directory containing index.txt wins.
// 搜索顺序：$FLIPBOOK_ASSETS（覆盖）→ 可执行文件旁（.app 的 Resources 布局）
// → 构建树 → 当前目录。第一个含 index.txt 的目录胜出。
#pragma once

#include <string>

namespace fb {
namespace ui {

// Returns the motion_chips/ directory, or "" if not found.
// 返回 motion_chips/ 目录，找不到则 ""。
std::string findChipsDir();

}  // namespace ui
}  // namespace fb
