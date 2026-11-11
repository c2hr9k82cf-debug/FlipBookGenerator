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

// Returns the full path of the character template JSON (chibi_standard.json),
// or "" if not found. Same search order as findChipsDir.
// 返回角色模板 JSON（chibi_standard.json）的完整路径，找不到则 ""。
// 搜索顺序与 findChipsDir 相同。
std::string findTemplatePath();

// The USER parts library root (created on demand by the callers that write):
//   ~/Library/Application Support/FlipBookGenerator/parts_library/
// 用户部件库根目录（写入方按需创建）：
//   ~/Library/Application Support/FlipBookGenerator/parts_library/
std::string userLibraryRoot();

}  // namespace ui
}  // namespace fb
