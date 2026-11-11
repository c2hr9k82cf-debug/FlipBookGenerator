// parts.h — modular character layers for future customization
// 可定制角色的分层部件
//
// This is the foundation for the character customization system (捏人系统):
// hair, face, clothing, body type, and limbs are swappable layers that compose
// into a standard 32x32 frame. The layer system is data-only — it feeds the
// existing Rig pipeline unchanged.
// 这是角色定制系统（捏人系统）的基础：发型、脸部、衣服、体型、四肢都是可换的
// 图层，合成到标准 32x32 帧。图层系统纯数据 —— 喂给现有 Rig 管线，零改动。
//
// Architecture / 架构:
//   PartLayer (swappable) → CharacterSpec (assembled) → Rig (existing pipeline)
//   图层（可换）        → 角色规范（组装）     → Rig（现有管线）
//
// Design rules / 设计规则:
//   * Each layer has a FIXED integer anchor box in canonical 32x32 space.
//     No placement math — stamp at (x,y) and done. Pixel-exact by construction.
//     每个图层在标准 32x32 空间有固定的整数锚点框。无定位计算 —— 贴到 (x,y)
//     即可。构造上像素精确。
//   * Layers composite in z-order (bottom to top). Overlaps: z-order wins,
//     no blending (matches the pixel-art transform rules).
//     图层按 z 序合成（从下到上）。重叠：z 序胜出，不混合（符合像素风规则）。
//   * Palette roles enable recoloring: each layer maps template color roles to
//     its own hues. A recipe can override any role.
//     调色板角色支持换色：每个图层把模板颜色角色映射到自己的色相。
//     配方可以覆盖任何角色。

#pragma once

#include "core/template_spec.h"

#include <opencv2/core.hpp>
#include <array>
#include <string>
#include <vector>

namespace fb {

// ---------------------------------------------------------------------------
// PartLayer — one swappable visual layer / 一个可换的视觉图层
// ---------------------------------------------------------------------------

// A single cosmetic layer (hair, face, top, pants, boots, body base...).
// 单个装饰图层（发型、脸、上衣、裤、靴、身体基础...）。
struct PartLayer {
    std::string slot;      // "hair", "face", "top", "pants", "boots", "body"
                           // 槽位名
    std::string name;      // "hair_orange_bob" / item name / 物品名
    cv::Mat sprite;        // BGRA, transparent background / BGRA，透明背景
    cv::Rect anchor;       // where it sits in canonical 32x32 / 在标准 32x32 中的位置
    int zOrder = 0;        // compositing order (higher = on top) / 合成序（大在上）

    // Palette role remap: EXACT source colour → destination colour.
    // 调色板角色重映射：精确源色 → 目标色。
    // v0.9.1 semantics: no fuzzy matching. A pixel is remapped only when it
    // equals srcBgr exactly — near-miss colours stay untouched (the old
    // distance<100 match could silently recolour pixels that merely looked
    // similar). This is the same code path as chip reskinning.
    // v0.9.1 语义：无模糊匹配。像素恰等于 srcBgr 才重映射 —— 近似色保持原样
    // （旧的距离<100 匹配会悄悄改掉"看着像"的像素）。与芯片换皮同一条代码路径。
    struct RoleColor {
        std::string role;  // "hair_mid", "pants", "skin", ... (for UI / 供 UI 用)
        cv::Vec3b srcBgr;  // exact sprite colour to replace / 要替换的精确精灵色
        cv::Vec3b dstBgr;  // replacement colour / 替换色
    };
    std::vector<RoleColor> paletteRoles;
};

// ---------------------------------------------------------------------------
// CharacterSpec — an assembled character / 组装好的角色
// ---------------------------------------------------------------------------

// A complete character assembled from layers, plus the template proportions
// that define how it splits into animation parts.
// 从图层组装的完整角色，加上定义如何拆分为动画部件的模板比例。
struct CharacterSpec {
    std::string templateName;   // "chibi_standard" / 模板名
    cv::Size frameSize;         // 32x32 (but not hardcoded) / 32x32（但不写死）

    // Layers, sorted by zOrder before compositing.
    // 图层，合成前按 zOrder 排序。
    std::vector<PartLayer> layers;

    // Proportions from the template / 模板比例
    struct Proportions {
        int headRows = 11;      // y2-y12 / 头 11 行
        int bodyRows = 8;       // y13-y20 / 身体 8 行
        int legRows  = 11;      // y21-y31 / 腿 11 行
        int contentWidth = 14;  // x9-x22 / 内容宽 14
        int marginX = 9;        // left/right margin / 左右边距
        int marginTop = 2;      // y2 / 上边距
    } proportions;
};

// ---------------------------------------------------------------------------
// Composition / 合成
// ---------------------------------------------------------------------------

// Composite all layers into a single frame. Layers are stamped at their
// anchor positions in z-order (bottom to top). Overlaps: z-order wins.
// 把所有图层合成到单帧。图层按锚点位置以 z 序（从下到上）贴上。
// 重叠：z 序胜出。
cv::Mat compositeCharacter(const CharacterSpec& spec);

// Apply palette-role remapping to a layer's sprite in-place. EXACT src→dst
// colour replacement via PaletteMap/applyPaletteMap (the chip-reskin path);
// alpha is preserved and transparent pixels are never touched.
// 对图层精灵原地应用调色板角色重映射。走 PaletteMap/applyPaletteMap 的
// 精确 src→dst 换色（芯片换皮路径）；alpha 保留，透明像素永不触及。
void applyPaletteRoles(cv::Mat& sprite, const std::vector<PartLayer::RoleColor>& roles);

// Split a frame into the standard 6-part bundle using the template's PER-VIEW
// part ownership (absolute coords in spec.views[view].map). Front, side and
// back each have their own column/leg-split layout — nothing is hardcoded.
// 按模板的逐视图部件归属（spec.views[view].map 中的绝对坐标）把帧拆成标准
// 6 部件包。正/侧/背各有自己的列与分腿布局 —— 无任何硬编码。
// Returns (part sprites, tight bboxes). / 返回（部件精灵、紧致 bbox）。
struct SplitResult {
    std::array<cv::Mat, 6> sprites;   // head, body, arm_left, arm_right, leg_left, leg_right
    std::array<cv::Rect, 6> bboxes;   // placement in original-image coords / 原图坐标位置
    std::array<bool, 6> present;      // which parts have pixels / 哪些部件有像素
};
SplitResult splitFrameToParts(const cv::Mat& frame, const TemplateSpec& spec, int view);

// ---------------------------------------------------------------------------
// Convenience: build a CharacterSpec from the standard template.
// 便捷函数：从标准模板构建 CharacterSpec。
// ---------------------------------------------------------------------------
CharacterSpec makeStandardSpec(cv::Size frameSize = cv::Size(32, 32));

}  // namespace fb
