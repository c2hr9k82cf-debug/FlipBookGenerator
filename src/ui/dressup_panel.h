// dressup_panel.h — the dress-up tab (v0.10.2): pick one library item per
// slot, recolour roles, preview the three composed views, then "生成动画"
// injects them into the same pipeline the drag-and-drop views use.
// 捏人 tab（v0.10.2）：每槽选一件库物品、角色换色、三面合成预览，再由
// 「生成动画」注入拖图所用的同一条管线。
#pragma once

#include "panels.h"

namespace fb {
namespace ui {

// Draw the dress-up tab. Called inside the tab item.
// 画捏人 tab。在 tab item 内调用。
void drawDressUp(AppState& st, PreviewTextures& tex);

}  // namespace ui
}  // namespace fb
