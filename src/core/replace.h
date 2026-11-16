// replace.h — per-frame region replacement data model (v0.13.0, plan D5/M21)
// 逐帧区域替换数据模型（v0.13.0，计划 D5/M21）
//
// Phase 2 lifts the region classification from tools/extract_regions.py and
// tools/make_parts_fixture.py into core so the renderer can decide, per frame
// and per cell, which slot owns the cell and whose pixels win it. The rules
// live here once — the gates compare against the committed artefacts those
// tools produced (ref_* sprites, chibi_regions.json masks), so a drift fails
// the build.
// 阶段二把 tools/extract_regions.py 与 tools/make_parts_fixture.py 的区域归属
// 升格进 core，渲染器才能逐帧逐格决定"这格归谁、谁的像素赢"。规则只住这里
// —— 闸门拿那两个工具的产物（ref_* 精灵、chibi_regions.json 掩码）做对照，
// 规则一漂移构建就红。
//
// Coordinate conventions / 坐标约定:
//   - FRAME row y: as drawn in one chip frame (bobs with body_top_y).
//     帧行 y：芯片帧里的画法行（随 body_top_y 起伏）。
//   - NORMAL row yNorm = y - (body_top_y - 2): the reference pose row. Row
//     bands (head/body/leg) are judged in normal rows.
//     规范行 yNorm = y - (body_top_y - 2)：参考姿态行。行带（头/躯/腿）按规范行判。
//   - Region boxes in chibi_regions.json use FRAME coords (union of frame
//     footprints) — see tools/extract_regions.py.
//     chibi_regions.json 的框用帧坐标（帧足迹并集）—— 见 tools/extract_regions.py。
#pragma once

#include <opencv2/core.hpp>

#include <map>
#include <string>
#include <vector>

#include "core/core.h"           // PaletteMap
#include "core/template_spec.h"

namespace fb {

// The six slots, indices fixed to this order. / 六槽，下标固定为此序。
constexpr int SLOT_HAIR = 0;
constexpr int SLOT_FACE = 1;
constexpr int SLOT_TOP = 2;
constexpr int SLOT_PANTS = 3;
constexpr int SLOT_BOOTS = 4;
constexpr int SLOT_BODY = 5;
constexpr int SLOT_NONE = -1;      // no owner (transparent / dropped) / 无归属（透明/丢弃）

// classifyPixel sentinel returns / classifyPixel 的哨兵返回值
constexpr int SLOT_OUTLINE = -2;   // outline colour: join by neighbour vote / 描边色：按邻投票
constexpr int SLOT_UNKNOWN = -1;   // colour not in the palette / 颜色不在调色板

// Slot names in index order. / 下标序的槽名。
const std::vector<std::string>& slotNames();
// -1 when the name is not one of the six. / 名字不在六槽内时 -1。
int slotIndex(const std::string& name);

// classify(role, region) -> slot index. Verbatim lift of
// tools/extract_regions.py::classify (== make_parts_fixture.py::classify);
// keep the three in lockstep.
// classify(role, region) -> 槽下标。逐字升格自 tools/extract_regions.py::
// classify（== make_parts_fixture.py::classify）；三处保持同步。
int classifyRoleRegion(const std::string& role, const std::string& region);

// Colour/role/region lookup tables, built once from the template.
// 从模板一次性建的颜色/角色/区域查表。
struct SlotTables {
    // packed BGR (0xBBGGRR) -> role name. Shared RGB keeps the non-gold name
    // (hair_highlight / gold disambiguate by region afterwards).
    // 打包 BGR（0xBBGGRR）-> 角色名。同 RGB 保留非 gold 名（hair_highlight/gold
    // 之后由区域消歧）。
    std::map<unsigned int, std::string> colourToRole;
    // Row bands in normal coords: yNorm -> region. / 规范行 -> 区域。
    // 0 = head, 1 = body, 2 = leg, 3 = other.
    std::vector<int> headRows, bodyRows, legRows;

    std::string regionOf(int yNorm) const;   // "head"/"body"/"leg"/"other"
    // One opaque pixel -> slot, or SLOT_OUTLINE / SLOT_UNKNOWN.
    // 单个不透明像素 -> 槽，或 SLOT_OUTLINE / SLOT_UNKNOWN。
    int classifyPixel(const cv::Vec3b& bgr, int yNorm) const;
};

// Build the tables from the template. Fails when the three views disagree on
// the row bands (the classifier is band-global by design).
// 从模板建表。三视图行带不一致时报错（分类器按设计行带全局）。
bool buildSlotTables(const TemplateSpec& spec, SlotTables& out, std::string& err);

// Per-cell slot map (CV_8SC1, frame-sized). Pass 1 classifies every opaque
// pixel; pass 2 lets each outline pixel join the slot its already-assigned
// 8-neighbours vote for (scan order dy=-1..1 then dx=-1..1, ties break to the
// first slot scanned — Python's Counter.most_common). Cells with no owner
// stay SLOT_NONE.
// 逐格槽图（CV_8SC1，与帧同尺寸）。第一遍分类所有不透明像素；第二遍每个描边
// 像素按"已归属 8 邻"投票归槽（扫描序 dy=-1..1 再 dx=-1..1，平票取扫描序先见
// —— 即 Python 的 Counter.most_common）。无归属的格保持 SLOT_NONE。
// `off` = bodyTopY - 2 (0 for a normal-pose user view).
cv::Mat classifyFrame(const cv::Mat& frame, int off, const SlotTables& t);

// Split a user view into six per-slot sprites. The view may be recoloured
// (`map` = the extracted PaletteMap, ref colour -> user colour): a pixel's
// slot first tries the map's preimage roles (unique preimage -> its slot; an
// outline-only preimage joins the outline vote), then falls back to the
// reference design at the same cell (ambiguous or repaired-away colours),
// then to reading the pixel as a raw role colour. Without a map (or on
// fallback) this is exactly classifyFrame at off=0.
// 把用户视图拆成六槽精灵。视图可换过色（map = 提取的 PaletteMap，参考色 ->
// 用户色）：像素槽归属先试 map 的原像角色（唯一原像 -> 其槽；纯描边原像进描边
// 投票），再落到同一格的参考设计（歧义色、被修复掉的少数派色），最后把像素当
// 原始角色色读。不带 map（或走兜底）时与 classifyFrame(off=0) 完全一致。
struct SlotSplit {
    cv::Mat slotOf;                    // CV_8SC1, per-pixel slot / 逐像素槽
    std::vector<cv::Mat> sprites;      // 6 x CV_8UC4, pixels verbatim / 六精灵，像素原样
    int unassigned = 0;                // opaque pixels with no slot / 不透明无槽像素数
};
SlotSplit splitViewBySlot(const cv::Mat& view, const PaletteMap& map,
                          const SlotTables& t,
                          const cv::Mat& refView = cv::Mat());

// Read anim_reference.json -> flipbook name -> per-frame body_top_y. Keys are
// the chip flipbook names (front_idle, front_walk, side_idle, side_walk,
// back_idle, back_walk). Fails on a missing/empty frame_data series.
// 读 anim_reference.json -> 芯片名 -> 逐帧 body_top_y。键为芯片翻页书名
// （front_idle, front_walk, side_idle, side_walk, back_idle, back_walk）。
// frame_data 缺失或为空即失败。
bool loadAnimBodyTop(const std::string& jsonPath,
                     std::map<std::string, std::vector<int>>& out,
                     std::string& err);

// Per-frame region replacement renderer (M21, plan D5).
// 逐帧区域替换渲染器（M21，计划 D5）。
//
// For each frame cell (x, y) the FRAME occupant (the chip pixel, z = its
// slot's layer, colour = PaletteMap(chip)) competes with the MATERIAL — the
// user view's pixel sampled at the normal-pose position (x, sy),
// sy = y - (body_top_y - 2). The material only enters when the reference is
// transparent there (a user shape extension) or the chip paints the cell;
// higher defaultLayerZ wins, ties go to the material. A material that
// follows the reference colour layout outputs the mapped chip colour (the
// per-frame tone/redraw language survives); a deviating material outputs its
// own colour — except a colour that is no map destination (an extraction
// minority, already repaired per the tolerant warning): that one outputs
// map(N). Recession: where the user deleted a design cell (reference opaque,
// user transparent) the FRAME occupant of the same slot yields — the user's
// smaller shape really shrinks; a swinging limb of another slot passes
// untouched. With the user view equal to the reference and an identity map
// no deletion or repair ever fires and every cell outputs the chip pixel
// exactly — the replace_fidelity invariant, proven in the M21 design and
// gated in batch 3.
// 对每个帧格 (x,y)：帧占用（芯片像素，z = 槽层序，色 = PaletteMap(芯片)）与
// 素材（用户视图在规范位置 (x,sy) 的像素，sy = y - (body_top_y - 2)）竞争。
// 素材仅当参考处透明（用户形状扩展）或芯片画了这格才进候选；z 大者胜，平票
// 素材胜。沿用参考色布局的素材输出换色后的芯片色（逐帧色调/重绘语言得以保留）
// ；偏离的素材出自己的颜色 —— 唯一例外是非映射目标色的偏离色（提取的少数派，
// 宽容警告里已声明按主色修复）：它输出 map(N)。缩水：用户删掉设计格（参考不
// 透明、用户透明）时，同槽的帧占用让位 —— 用户的小形状真的变小；扫过的别的槽
// 肢体原样通过。用户视图==参考且 map 恒等时删除与修复均不触发，逐格必输出芯片
// 像素 —— replace_fidelity 不变量，M21 设计中已证，批次③闸门把守。
//
// `userSlots` = splitViewBySlot(userView, map, t).slotOf (pass it precomputed).
// `refView`   = the normal-pose reference of this view (renderTemplateView).
// `bodyTop`   = per-frame body_top_y (loadAnimBodyTop), one entry per frame.
// 返回空 vector 表示 bodyTop 与帧数不符。
std::vector<cv::Mat> renderReplaced(const MotionChips& chips, const PaletteMap& map,
                                    const cv::Mat& userView, const cv::Mat& userSlots,
                                    const cv::Mat& refView,
                                    const std::vector<int>& bodyTop,
                                    const SlotTables& t);

}  // namespace fb
