// panels.h — all ImGui panels of the main window / 主窗口全部 ImGui 面板
#pragma once

#include "app_state.h"

namespace fb {
namespace ui {

// GL texture cache for the previews (slot thumbnails + the sheet). Re-uploaded
// whenever AppState::revision changes. Owns GL textures: call release() before
// the GL context dies.
// 预览用 GL 纹理缓存（槽缩略图 + 大图）。AppState::revision 变化时重传。
// 持有 GL 纹理：GL 上下文销毁前调用 release()。
struct PreviewTextures {
    unsigned int slot[3] = {0, 0, 0};
    unsigned int sheet = 0;
    unsigned long uploadedRevision = ~0ul;

    void sync(AppState& st);
    void release();
    ~PreviewTextures() { release(); }
};

// Draw the whole window content: input slots, extraction, preview, status,
// export. Called inside ImGui::Begin("FlipBookGenerator").
// 画整个窗口内容：输入槽、提取结果、预览、状态、导出。在 ImGui::Begin 内调用。
void drawMainContent(AppState& st, PreviewTextures& tex);

}  // namespace ui
}  // namespace fb
