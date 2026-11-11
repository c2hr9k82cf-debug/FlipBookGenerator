// panels.h — all ImGui panels of the main window / 主窗口全部 ImGui 面板
#pragma once

#include "app_state.h"
#include "canvas.h"

namespace fb {
namespace ui {

// GL texture cache for the previews (slot thumbnails + diff overlays + the
// sheet). Re-uploaded whenever AppState::revision changes. Owns GL textures:
// call release() before the GL context dies.
// 预览用 GL 纹理缓存（槽缩略图 + 差异叠加 + 大图）。AppState::revision 变化时
// 重传。持有 GL 纹理：GL 上下文销毁前调用 release()。
struct PreviewTextures {
    unsigned int slot[3] = {0, 0, 0};
    unsigned int slotDiff[3] = {0, 0, 0};   // transparent diff marks / 透明差异标记
    unsigned int sheet = 0;
    unsigned long uploadedRevision = ~0ul;
    // The part editor's pixel canvas (v0.10.0) — same GL lifetime as above.
    // 部件编辑器的像素画布（v0.10.0）—— GL 生命周期同上。
    PixelCanvas editorCanvas;
    // The dress-up tab's three composed previews (v0.10.2) — re-uploaded
    // when AppState::composeRevision changes. / 捏人 tab 的三张合成预览
    // （v0.10.2）—— AppState::composeRevision 变化时重传。
    unsigned int dressup[3] = {0, 0, 0};
    unsigned long dressupRevision = ~0ul;

    void sync(AppState& st);
    void release();
    ~PreviewTextures() { release(); }
};

// Draw the whole window content: mode tabs (animation export / part editor),
// status bar. Called inside ImGui::Begin("##main").
// 画整个窗口内容：模式 tab（动画导出 / 部件编辑器）、状态栏。在
// ImGui::Begin("##main") 内调用。
void drawMainContent(AppState& st, PreviewTextures& tex);

}  // namespace ui
}  // namespace fb
