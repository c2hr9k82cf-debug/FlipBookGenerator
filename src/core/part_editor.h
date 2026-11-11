// part_editor.h — part editor state machine, UI-free (v0.10.0).
// 部件编辑器状态机，无 UI（v0.10.0）。
//
// The editor works on the six customisation slots (hair/face/top/pants/boots/
// body) — NOT the six rig parts (head/body/arms/legs) that the animation
// pipeline splits. Each slot is a 32x32 BGRA sprite in template coordinates
// with up to three views (front/side/back; back may be inferred).
// 编辑器操作六槽（hair/face/top/pants/boots/body）—— 不是动画管线切的六部件
// （head/body/arms/legs）。每槽是模板坐标系里的 32x32 BGRA 精灵，最多三个视图
// （front/side/back；back 可推断）。
//
// v1 scope (per plan, deliberately minimal): role-colour brush + eraser, one
// whole-region recolour, exactly ONE undo step, back inference. No layers, no
// magic wand, no symmetry brush, no sub-pixel anything.
// v1 范围（按计划刻意最小）：角色色画笔 + 橡皮、整区换色、恰好一步撤销、背面
// 推断。无图层、无魔棒、无对称画笔、无任何子像素。
#pragma once

#include "core/template_spec.h"

#include <opencv2/core.hpp>

#include <string>

namespace fb {

struct PartEditor {
    static constexpr int FRONT = 0, SIDE = 1, BACK = 2;

    cv::Size canvas{32, 32};
    std::string slot = "hair";        // current slot / 当前槽
    std::string itemName = "untitled";
    int view = FRONT;                 // active view tab / 当前视图 tab

    cv::Mat pixels[3];   // BGRA work images; empty = view not drawn yet
                         // BGRA 工作图；空 = 该视图还没画
    bool touched[3] = {false, false, false};   // modified since load / 载入后改过

    // Exactly one undo step (P3 scope). / 恰好一步撤销（P3 范围）。
    cv::Mat undoSnap[3];
    bool hasUndo = false;

    bool viewReady(int v) const;   // drawn or loaded / 画过或载入过
    // Create the view's canvas on first edit. / 首次编辑时创建该视图画布。
    void ensureCanvas(int v);
    // Start a fresh empty item. / 开一个全新空部件。
    void blank(cv::Size sz);
    void setView(int v);

    // Snapshot the whole item for the single undo step.
    // 为单步撤销给整件拍快照。
    void snapshot();
    void undo();

    // Brush / eraser on one view. Coordinates are template pixels; out-of-
    // bounds is ignored. Brush writes the role colour with alpha 255, eraser
    // clears to fully transparent.
    // 画笔 / 橡皮，单视图。坐标是模板像素；越界忽略。画笔写角色色 alpha 255，
    // 橡皮清为全透明。
    void paint(int v, int x, int y, const cv::Vec3b& bgr);
    void erase(int v, int x, int y);

    // Whole-region recolour on one view via the EXACT src->dst engine
    // (applyPaletteRoles — same code path as chip reskin). Transparent pixels
    // are never touched.
    // 单视图整区换色，走精确 src→dst 引擎（applyPaletteRoles —— 与芯片换皮同一
    // 代码路径）。透明像素绝不触碰。
    void fillRole(int v, const cv::Vec3b& fromBgr, const cv::Vec3b& toBgr);

    // Infer the back view from the front (back:"auto" path). Requires front.
    // 从正面推断背面（back:"auto" 路径）。需要正面。
    bool inferBack(const TemplateSpec& spec, std::string& err);
};

// Save the editor item as a library folder:
//   <libraryRoot>/<slot>/<itemName>/{front.png,side.png[,back.png],item.json}
// back.png is written only when the back view is drawn (then item.json says
// back:"file"); otherwise item.json says back:"auto" — matching the loader's
// no-ambiguity rule.
// 把编辑器部件存成库文件夹：
//   <libraryRoot>/<slot>/<itemName>/{front.png,side.png[,back.png],item.json}
// 画了背面才写 back.png（item.json 为 back:"file"）；否则 item.json 为
// back:"auto" —— 与加载器的无歧义规则一致。
bool saveEditorItem(const PartEditor& ed, const std::string& libraryRoot,
                    std::string& err);

// Load a library item folder into the editor (all views it has).
// 把库物品文件夹读进编辑器（有啥视图读啥）。
bool loadEditorItem(PartEditor& ed, const std::string& itemDir,
                    const std::string& slot, cv::Size expectedSize,
                    std::string& err);

// Role-constraint check: every opaque pixel colour must equal one palette
// role colour (exact match — pixel art never blends). Returns the number of
// violating pixels; 0 means the sprite is legal for the library.
// 角色约束检查：每个不透明像素色必须等于某调色板角色色（精确匹配 —— 像素风
// 绝不混合）。返回违规像素数；0 表示该精灵合库规。
int roleConstraintViolations(const cv::Mat& img, const TemplateSpec& spec);

}  // namespace fb
