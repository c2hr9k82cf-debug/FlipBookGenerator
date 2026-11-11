// part_editor_panel.h — the in-tool part editor panel (v0.10.0).
// 工具内部件编辑器面板（v0.10.0）。
#pragma once

#include "panels.h"

namespace fb {
namespace ui {

// Draw the part-editor tab: slot picker, view tabs, brush/eraser/fill with
// the role palette, undo (one step), import with fine-tune, back inference,
// library list + save, library import/export. Called inside the tab item.
// 画部件编辑器 tab：槽选择、视图 tab、画笔/橡皮/填色 + 角色色板、撤销（一步）、
// 导入微调、背面推断、库列表 + 保存、库导入/导出。在 tab item 内调用。
void drawPartEditor(AppState& st, PreviewTextures& tex);

}  // namespace ui
}  // namespace fb
