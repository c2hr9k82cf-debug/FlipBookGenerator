// canvas.h — zoomable pixel canvas widget (v0.10.0).
// 可缩放像素画布控件（v0.10.0）。
//
// The shared foundation of the part editor (CLAUDE.md roadmap Phase 3): a
// DrawList-drawn pixel canvas with integer zoom, checkerboard under
// transparency, optional grid lines and overlay, hover highlight and
// pixel-space mouse mapping. Pixel art rules: zoom is a positive integer and
// the texture is GL_NEAREST — nothing here ever interpolates.
// 部件编辑器的共享地基（CLAUDE.md 路线图 Phase 3）：DrawList 画的像素画布，
// 整数缩放、透明区下棋盘格、可选网格线与叠加层、hover 高亮、鼠标映射到像素。
// 像素风规则：缩放为正整数、纹理 GL_NEAREST —— 这里绝不插值。
#pragma once

#include <opencv2/core.hpp>

namespace fb {
namespace ui {

// What one canvas frame reports back. / 一帧画布回报的内容。
struct CanvasInput {
    int px = -1, py = -1;   // hover pixel (-1 = outside) / hover 像素（外为 -1）
    bool painted = false;   // left button held over the canvas / 画布上按住左键
    bool erased = false;    // right button held over the canvas / 画布上按住右键
};

class PixelCanvas {
public:
    // Draw the canvas. `pixels` is the source of truth (BGRA); `revision` must
    // change whenever its content changes (the GL texture is cached). The
    // optional `overlay` is composited on top the same way (e.g. P1 diff
    // marks). Returns this frame's input.
    // 画画布。pixels 是真源（BGRA）；内容一变 revision 就要变（GL 纹理有缓存）。
    // 可选 overlay 以同样方式叠在上面（如 P1 的差异标记）。回报本帧输入。
    CanvasInput draw(const char* id, const cv::Mat& pixels, unsigned long revision,
                     const cv::Mat& overlay = cv::Mat(),
                     unsigned long overlayRevision = 0);

    int zoom = 8;            // user-set integer scale / 用户设定的整数缩放
    bool showGrid = true;

    // Release the GL textures — call before the GL context dies (same
    // lifetime contract as PreviewTextures).
    // 释放 GL 纹理 —— GL 上下文销毁前调用（与 PreviewTextures 同一生命周期契约）。
    void release();
    ~PixelCanvas();

private:
    unsigned int tex_ = 0, ovTex_ = 0;
    unsigned long texRev_ = ~0ul, ovRev_ = ~0ul;
};

}  // namespace ui
}  // namespace fb
