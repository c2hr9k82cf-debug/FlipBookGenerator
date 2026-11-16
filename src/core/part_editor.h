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

#include "core/regions.h"
#include "core/template_spec.h"

#include <opencv2/core.hpp>

#include <map>
#include <string>

namespace fb {

struct PartEditor {
    static constexpr int FRONT = 0, SIDE = 1, BACK = 2;
    // Piece tabs (v0.12.0 plan D2): MAIN = "X.png" (front piece / top layer),
    // BACKP = "X_b.png" (back-layer piece — for hair the back-of-head piece).
    // 片 tab（v0.12.0 计划 D2）：MAIN = X.png（前发片/盖顶层），BACKP = X_b.png
    // （垫底片 —— hair 即后发片）。
    static constexpr int MAIN = 0, BACKP = 1;

    cv::Size canvas{32, 32};
    std::string slot = "hair";        // current slot / 当前槽
    std::string itemName = "untitled";
    int view = FRONT;                 // active view tab / 当前视图 tab
    int piece = MAIN;                 // active piece tab / 当前片 tab

    // BGRA work images [piece][view]; empty = not drawn yet.
    // BGRA 工作图 [片][视图]；空 = 还没画。
    cv::Mat pixels[2][3];
    bool touched[2][3] = {{false, false, false}, {false, false, false}};

    // Exactly one undo step (P3 scope) — snapshots BOTH pieces.
    // 恰好一步撤销（P3 范围）—— 快照含两片。
    cv::Mat undoSnap[2][3];
    bool hasUndo = false;

    // Per-slot draft stash (v0.11.1 bug B7): the work canvas belongs to ONE
    // slot. Switching slots stashes the whole [piece][view] set here and
    // restores the target slot's set, so one slot's pixels never leak into
    // another's.
    // 按槽草稿暂存（v0.11.1 bug B7）：工作画布属于唯一槽位。切槽时整套
    // [片][视图] 存进这里、目标槽的整套恢复回去 —— 一个槽的像素绝不漏进另一个槽。
    struct Draft {
        cv::Mat pixels[2][3];
        bool touched[2][3] = {{false, false, false}, {false, false, false}};
        std::string itemName = "untitled";
        int piece = MAIN;
    };
    std::map<std::string, Draft> drafts;   // every slot but the active one /
                                           // 除当前槽外的各槽

    bool viewReady(int p, int v) const;   // drawn or loaded / 画过或载入过
    bool viewReady(int v) const { return viewReady(piece, v); }
    // Create the view's canvas on first edit. / 首次编辑时创建该视图画布。
    void ensureCanvas(int p, int v);
    // Start a fresh empty item. / 开一个全新空部件。
    void blank(cv::Size sz);
    void setView(int v);
    // Switch the active piece tab. / 切换当前片。
    void setPiece(int p);
    // Switch slots with draft stash/restore — never leaks pixels across
    // slots; the single undo step dies with the old canvas.
    // 切槽并暂存/恢复草稿 —— 像素绝不跨槽泄漏；单步撤销随旧画布一起作废。
    void switchSlot(const std::string& newSlot);

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
// plus the optional back-layer pieces {front_b.png,side_b.png[,back_b.png]}
// (v0.12.0). back.png is written only when the back view is drawn (then
// item.json says back:"file"); otherwise item.json says back:"auto" —
// matching the loader's no-ambiguity rule. back_b.png needs back.png for
// the same reason. When `boxes` is non-null, EVERY piece must paint inside
// its slot's region box (the drawing contract) or the save is refused with
// the out-of-box pixel count.
// 把编辑器部件存成库文件夹：
//   <libraryRoot>/<slot>/<itemName>/{front.png,side.png[,back.png],item.json}
// 外加可选垫底片 {front_b.png,side_b.png[,back_b.png]}（v0.12.0）。画了背面才
// 写 back.png（item.json 为 back:"file"）；否则 item.json 为 back:"auto" ——
// 与加载器的无歧义规则一致。back_b.png 同理需要 back.png。boxes 非空时，每一片
// 都必须画在本槽轮廓框内（绘制契约），否则拒绝保存并报出越界像素数。
bool saveEditorItem(const PartEditor& ed, const std::string& libraryRoot,
                    const RegionBoxes* boxes, std::string& err);

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
