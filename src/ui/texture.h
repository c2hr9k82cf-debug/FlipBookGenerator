// texture.h — cv::Mat (BGRA) -> OpenGL texture, nearest-neighbour only
// cv::Mat（BGRA）→ OpenGL 纹理，只用最近邻
//
// Part of the UI layer, not flipbook_core: it touches GL. The core stays
// UI-free (CLAUDE.md iron rule). Nearest-neighbour is mandatory — pixel art
// must never be interpolated. GL_NEAREST is hardcoded for that reason.
// 属 UI 层，不属 flipbook_core：它碰 GL。core 保持无 UI（CLAUDE.md 铁律）。
// 必须最近邻 —— 像素风绝不插值。故 GL_NEAREST 写死。
#pragma once

#include <opencv2/core.hpp>

namespace fb {
namespace ui {

// Upload a BGRA cv::Mat as an OpenGL texture. Returns the texture id, or 0 on
// failure (empty / not 4-channel). The mat is not modified.
// 把 BGRA cv::Mat 上传为 OpenGL 纹理。返回纹理 id，失败（空/非 4 通道）为 0。
// 不改动入参矩阵。
unsigned int matToTexture(const cv::Mat& bgra);

// Delete a texture created by matToTexture. No-op on 0.
// 删除 matToTexture 建的纹理。0 时无操作。
void deleteTexture(unsigned int tex);

}  // namespace ui
}  // namespace fb
