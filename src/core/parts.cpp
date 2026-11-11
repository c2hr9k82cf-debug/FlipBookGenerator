// parts.cpp — modular character layer composition
// 分层角色图层合成
//
// Implements the layer composition and part-splitting logic for the character
// customization system. See parts.h for the design contract.
// 实现角色定制系统的图层合成与部件拆分逻辑。设计契约见 parts.h。

#include "parts.h"

#include "core/core.h"

#include <algorithm>
#include <map>
#include <opencv2/imgproc.hpp>

namespace fb {

// ---------------------------------------------------------------------------
// Palette role remapping / 调色板角色重映射
// ---------------------------------------------------------------------------

void applyPaletteRoles(cv::Mat& sprite, const std::vector<PartLayer::RoleColor>& roles) {
    // EXACT src→dst colour replacement on opaque pixels, via the same
    // PaletteMap/applyPaletteMap path the chip reskin uses. No fuzzy matching:
    // a pixel whose colour is merely NEAR srcBgr stays untouched (the old
    // distance<100 rule recoloured near-misses — a real bug for hand-painted
    // sprites with anti-aliased-looking edges).
    // 对不透明像素做精确 src→dst 换色，走芯片换皮同款 PaletteMap/applyPaletteMap。
    // 无模糊匹配：颜色只是"接近"srcBgr 的像素保持原样（旧的 距离<100 会误改
    // 近似色 —— 手绘精灵带渐变边缘时就是实打实的 bug）。
    if (roles.empty() || sprite.empty()) return;

    PaletteMap map;
    map.reserve(roles.size());
    for (const auto& rc : roles) {
        map.emplace_back(cv::Vec4b(rc.srcBgr[0], rc.srcBgr[1], rc.srcBgr[2], 255),
                         cv::Vec4b(rc.dstBgr[0], rc.dstBgr[1], rc.dstBgr[2], 255));
    }
    applyPaletteMap(sprite, map);
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

SplitResult splitFrameToParts(const cv::Mat& frame, const TemplateSpec& spec, int view) {
    SplitResult result;
    result.present.fill(false);
    if (frame.empty() || view < 0 || view > 2) return result;

    // Per-view ownership in ABSOLUTE image coords (see the template _schema):
    //   head:      head_rows, whole width / head_rows 行，全宽
    //   body:      body_rows minus the two arm column sets / body_rows 行减臂列
    //   arm_left:  body_rows ∩ arm_left_cols
    //   arm_right: body_rows ∩ arm_right_cols
    //   leg_left:  leg_rows[i], x <= leg_split_per_row[i]
    //   leg_right: leg_rows[i], x >  leg_split_per_row[i]
    // Front / side / back differ only in these lists — side legs shift the
    // split per row, side arms use different columns.
    // 正/侧/背只差这几张表 —— 侧面腿的分界逐行移动，侧面臂用不同的列。
    const PartMap& m = spec.views[view].map;

    // Row classification lookup: y -> (kind, index).
    // kind: 0 head, 1 body, 2 leg (index = position in legRows).
    // 行分类查找：y -> (种类, 序号)。种类：0 头、1 躯干、2 腿（序号 = legRows 位置）。
    struct RowInfo { char kind; int legIdx; };
    std::map<int, RowInfo> rowOf;
    for (int y : m.headRows) rowOf[y] = {0, -1};
    for (int y : m.bodyRows) rowOf[y] = {1, -1};
    for (size_t i = 0; i < m.legRows.size(); ++i) rowOf[m.legRows[i]] = {2, (int)i};

    // Column classification for body rows: 0 body, 1 arm_left, 2 arm_right.
    // 躯干行的列分类：0 躯干、1 左臂、2 右臂。
    std::map<int, char> armColOf;
    for (int x : m.armLeftCols) armColOf[x] = 1;
    for (int x : m.armRightCols) armColOf[x] = 2;

    // Bucket every opaque pixel by part id (0..5, same order as the bundle).
    // 把每个不透明像素按部件 id（0..5，与部件包同序）入桶。
    std::array<std::vector<cv::Point>, 6> buckets;
    for (int y = 0; y < frame.rows; ++y) {
        auto rit = rowOf.find(y);
        if (rit == rowOf.end()) continue;
        const cv::Vec4b* row = frame.ptr<cv::Vec4b>(y);
        for (int x = 0; x < frame.cols; ++x) {
            if (row[x][3] == 0) continue;  // transparent / 透明
            int part = -1;
            switch (rit->second.kind) {
                case 0: part = 0; break;                        // head / 头
                case 1: {                                       // body row / 躯干行
                    auto ait = armColOf.find(x);
                    if (ait == armColOf.end()) part = 1;        // body / 躯干
                    else part = (ait->second == 1) ? 2 : 3;     // arm_left / arm_right
                    break;
                }
                case 2: {                                       // leg row / 腿行
                    const int i = rit->second.legIdx;
                    part = (x <= m.legSplitPerRow[i]) ? 4 : 5;  // leg_left / leg_right
                    break;
                }
            }
            if (part >= 0) buckets[part].push_back({x, y});
        }
    }

    // Tight-bbox extract per bucket. / 逐桶紧致 bbox 提取。
    for (int p = 0; p < 6; ++p) {
        const auto& opaque = buckets[p];
        if (opaque.empty()) continue;

        int minX = frame.cols, minY = frame.rows, maxX = 0, maxY = 0;
        for (const auto& pt : opaque) {
            minX = std::min(minX, pt.x); maxX = std::max(maxX, pt.x);
            minY = std::min(minY, pt.y); maxY = std::max(maxY, pt.y);
        }
        const cv::Rect box(minX, minY, maxX - minX + 1, maxY - minY + 1);

        cv::Mat sprite(box.height, box.width, CV_8UC4, cv::Scalar(0, 0, 0, 0));
        for (const auto& pt : opaque) {
            sprite.at<cv::Vec4b>(pt.y - minY, pt.x - minX) =
                frame.at<cv::Vec4b>(pt.y, pt.x);
        }

        result.sprites[p] = sprite;
        result.bboxes[p] = box;
        result.present[p] = true;
    }
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
