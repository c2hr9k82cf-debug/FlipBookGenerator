// parts.cpp — modular character layer composition
// 分层角色图层合成
//
// Implements the layer composition and part-splitting logic for the character
// customization system. See parts.h for the design contract.
// 实现角色定制系统的图层合成与部件拆分逻辑。设计契约见 parts.h。

#include "parts.h"

#include <algorithm>
#include <opencv2/imgproc.hpp>

namespace fb {

// ---------------------------------------------------------------------------
// Palette role remapping / 调色板角色重映射
// ---------------------------------------------------------------------------

void applyPaletteRoles(cv::Mat& sprite, const std::vector<PartLayer::RoleColor>& roles) {
    // For each pixel, if its color matches a role's original value, replace it
    // with the override. Simple nearest-color match — the palette is small (15).
    // 对每个像素，若颜色匹配角色的原始值，替换为覆盖值。
    // 最近色匹配 —— 调色板很小（15 色）。
    if (roles.empty() || sprite.empty()) return;

    for (int y = 0; y < sprite.rows; ++y) {
        for (int x = 0; x < sprite.cols; ++x) {
            cv::Vec4b& px = sprite.at<cv::Vec4b>(y, x);
            if (px[3] == 0) continue;  // transparent / 透明

            // Find the closest role by BGR distance.
            // 找 BGR 距离最近的角色。
            int bestDist = INT_MAX;
            const cv::Vec3b* bestOverride = nullptr;
            for (const auto& rc : roles) {
                const int db = px[0] - rc.bgr[0];
                const int dg = px[1] - rc.bgr[1];
                const int dr = px[2] - rc.bgr[2];
                const int dist = db * db + dg * dg + dr * dr;
                if (dist < bestDist) {
                    bestDist = dist;
                    bestOverride = &rc.bgr;
                }
            }
            // Only remap if the color is close enough (exact palette match).
            // 只在颜色足够接近时重映射（精确调色板匹配）。
            if (bestOverride && bestDist < 100) {
                px[0] = (*bestOverride)[0];
                px[1] = (*bestOverride)[1];
                px[2] = (*bestOverride)[2];
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Composition / 合成
// ---------------------------------------------------------------------------

cv::Mat compositeCharacter(const CharacterSpec& spec) {
    cv::Mat canvas = cv::Mat::zeros(spec.frameSize, CV_8UC4);

    // Sort layers by zOrder (bottom to top). Stable sort preserves insertion
    // order for equal zOrder.
    // 按 zOrder 排序（从下到上）。稳定排序保持相同 zOrder 的插入顺序。
    std::vector<const PartLayer*> sorted;
    sorted.reserve(spec.layers.size());
    for (const auto& layer : spec.layers) sorted.push_back(&layer);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const PartLayer* a, const PartLayer* b) {
                         return a->zOrder < b->zOrder;
                     });

    // Stamp each layer at its anchor position. Z-order wins at overlaps.
    // 把每个图层贴到锚点位置。重叠处 z 序胜出。
    for (const auto* layer : sorted) {
        if (layer->sprite.empty()) continue;
        const cv::Rect box = layer->anchor;

        // Copy with bounds checking — the anchor must fit in the canvas.
        // 带边界检查的复制 —— 锚点必须在画布内。
        const int x0 = std::max(0, box.x);
        const int y0 = std::max(0, box.y);
        const int x1 = std::min(canvas.cols, box.x + layer->sprite.cols);
        const int y1 = std::min(canvas.rows, box.y + layer->sprite.rows);

        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const int sx = x - box.x;
                const int sy = y - box.y;
                const cv::Vec4b px = layer->sprite.at<cv::Vec4b>(sy, sx);
                if (px[3] > 0) {
                    canvas.at<cv::Vec4b>(y, x) = px;
                }
            }
        }
    }
    return canvas;
}

// ---------------------------------------------------------------------------
// Part splitting / 部件拆分
// ---------------------------------------------------------------------------

SplitResult splitFrameToParts(const cv::Mat& frame, const CharacterSpec& spec) {
    SplitResult result;
    result.present.fill(false);

    // Part ownership rules from the template.
    // 模板的部件归属规则。
    //   head:      y_margin_top .. y_margin_top + headRows - 1, whole width
    //   body:      next bodyRows rows, centre columns
    //   arm_left:  body rows, left columns (0..2 in 14-wide content)
    //   arm_right: body rows, right columns (11..13)
    //   leg_left:  leg rows, cols 0..6
    //   leg_right: leg rows, cols 7..13

    const int& mx = spec.proportions.marginX;
    const int& mt = spec.proportions.marginTop;
    const int& cw = spec.proportions.contentWidth;

    const int headY0 = mt;
    const int headY1 = mt + spec.proportions.headRows - 1;
    const int bodyY0 = headY1 + 1;
    const int bodyY1 = bodyY0 + spec.proportions.bodyRows - 1;
    const int legY0  = bodyY1 + 1;
    const int legY1  = legY0 + spec.proportions.legRows - 1;

    // Helper: extract a region and compute its tight bbox.
    // 辅助函数：提取区域并计算紧致 bbox。
    auto extractPart = [&](int y0, int y1, int x0, int x1, int partIdx) {
        // Collect opaque pixels in the region.
        // 收集区域内的不透明像素。
        std::vector<cv::Point> opaque;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                if (y < 0 || y >= frame.rows || x < 0 || x >= frame.cols) continue;
                if (frame.at<cv::Vec4b>(y, x)[3] > 0) {
                    opaque.push_back({x, y});
                }
            }
        }
        if (opaque.empty()) return;

        // Tight bbox / 紧致 bbox
        int minX = frame.cols, minY = frame.rows, maxX = 0, maxY = 0;
        for (const auto& p : opaque) {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
        const cv::Rect box(minX, minY, maxX - minX + 1, maxY - minY + 1);

        // Extract sprite / 提取精灵
        cv::Mat sprite(box.height, box.width, CV_8UC4, cv::Scalar(0, 0, 0, 0));
        for (const auto& p : opaque) {
            sprite.at<cv::Vec4b>(p.y - minY, p.x - minX) = frame.at<cv::Vec4b>(p.y, p.x);
        }

        result.sprites[partIdx] = sprite;
        result.bboxes[partIdx] = box;
        result.present[partIdx] = true;
    };

    // 1 head — whole width / 整宽
    extractPart(headY0, headY1, mx, mx + cw - 1, 0);
    // 2 body — centre columns (3..10 in 14-wide content)
    extractPart(bodyY0, bodyY1, mx + 3, mx + 10, 1);
    // 3 arm_left — cols 0..2 / 列 0..2
    extractPart(bodyY0, bodyY1, mx, mx + 2, 2);
    // 4 arm_right — cols 11..13 / 列 11..13
    extractPart(bodyY0, bodyY1, mx + 11, mx + 13, 3);
    // 5 leg_left — cols 0..6 / 列 0..6
    extractPart(legY0, legY1, mx, mx + 6, 4);
    // 6 leg_right — cols 7..13 / 列 7..13
    extractPart(legY0, legY1, mx + 7, mx + 13, 5);

    return result;
}

// ---------------------------------------------------------------------------
// Standard template convenience / 标准模板便捷函数
// ---------------------------------------------------------------------------

CharacterSpec makeStandardSpec(cv::Size frameSize) {
    CharacterSpec spec;
    spec.templateName = "chibi_standard";
    spec.frameSize = frameSize;
    // Proportions already match the defaults in the struct definition.
    // 比例已与结构体定义中的默认值一致。
    return spec;
}

}  // namespace fb
