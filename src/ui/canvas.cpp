// canvas.cpp — pixel canvas widget implementation (v0.10.0).
// 像素画布控件实现（v0.10.0）。
#include "canvas.h"
#include "texture.h"

#include "imgui.h"

namespace fb {
namespace ui {

void PixelCanvas::release() {
    deleteTexture(tex_);
    deleteTexture(ovTex_);
    tex_ = ovTex_ = 0;
    texRev_ = ovRev_ = ~0ul;
}

PixelCanvas::~PixelCanvas() { release(); }

CanvasInput PixelCanvas::draw(const char* id, const cv::Mat& pixels,
                              unsigned long revision, const cv::Mat& overlay,
                              unsigned long overlayRevision) {
    CanvasInput in;
    if (pixels.empty() || pixels.type() != CV_8UC4) return in;
    if (zoom < 1) zoom = 1;
    if (zoom > 32) zoom = 32;

    // (Re-)upload only on revision change — pixels are tiny, GL calls are not.
    // 只在 revision 变化时重传 —— 像素很小，GL 调用才是开销。
    if (revision != texRev_) {
        deleteTexture(tex_);
        tex_ = matToTexture(pixels);
        texRev_ = revision;
    }
    if (!overlay.empty() && overlayRevision != ovRev_) {
        deleteTexture(ovTex_);
        ovTex_ = matToTexture(overlay);
        ovRev_ = overlayRevision;
    }

    const float cell = (float)zoom;
    const ImVec2 size((float)pixels.cols * cell, (float)pixels.rows * cell);
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Checkerboard under transparency (4x4 image-pixel blocks).
    // 透明区下的棋盘格（4x4 图像像素一块）。
    const ImU32 light = IM_COL32(196, 196, 196, 255);
    const ImU32 dark  = IM_COL32(152, 152, 152, 255);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), light);
    for (int by = 0; by < pixels.rows; by += 4) {
        for (int bx = 0; bx < pixels.cols; bx += 4) {
            if (((bx >> 2) + (by >> 2)) % 2) continue;   // light default / 默认浅色
            const float x0 = pos.x + (float)bx * cell;
            const float y0 = pos.y + (float)by * cell;
            const float w = (float)(bx + 4 > pixels.cols ? pixels.cols - bx : 4) * cell;
            const float h = (float)(by + 4 > pixels.rows ? pixels.rows - by : 4) * cell;
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), dark);
        }
    }

    // The sprite itself (GL_NEAREST texture — see texture.cpp).
    // 精灵本身（GL_NEAREST 纹理 —— 见 texture.cpp）。
    if (tex_)
        dl->AddImage((ImTextureID)(intptr_t)tex_, pos,
                     ImVec2(pos.x + size.x, pos.y + size.y));
    if (ovTex_)
        dl->AddImage((ImTextureID)(intptr_t)ovTex_, pos,
                     ImVec2(pos.x + size.x, pos.y + size.y));

    // Grid lines (only when zoom makes them readable).
    // 网格线（缩放够大、看得清才画）。
    if (showGrid && zoom >= 6) {
        const ImU32 grid = IM_COL32(0, 0, 0, 48);
        for (int x = 0; x <= pixels.cols; ++x)
            dl->AddLine(ImVec2(pos.x + x * cell, pos.y),
                        ImVec2(pos.x + x * cell, pos.y + size.y), grid);
        for (int y = 0; y <= pixels.rows; ++y)
            dl->AddLine(ImVec2(pos.x, pos.y + y * cell),
                        ImVec2(pos.x + size.x, pos.y + y * cell), grid);
    }

    // Hover highlight + pixel mapping. / hover 高亮 + 像素映射。
    if (hovered) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const int px = (int)((m.x - pos.x) / cell);
        const int py = (int)((m.y - pos.y) / cell);
        if (px >= 0 && py >= 0 && px < pixels.cols && py < pixels.rows) {
            in.px = px;
            in.py = py;
            dl->AddRect(ImVec2(pos.x + px * cell, pos.y + py * cell),
                        ImVec2(pos.x + (px + 1) * cell, pos.y + (py + 1) * cell),
                        IM_COL32(255, 255, 255, 220));
            in.painted = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            in.erased = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        }
    }
    ImGui::PopID();
    return in;
}

}  // namespace ui
}  // namespace fb
