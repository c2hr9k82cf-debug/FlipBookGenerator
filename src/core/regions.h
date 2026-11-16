// regions.h — per-slot drawing boxes (v0.12.0 outline contract)
// 逐槽轮廓框（v0.12.0 绘制契约）
//
// A slot's region box is the union of cells the slot occupies across ALL
// motion chip frames. Parts may only paint inside their slot's box — this is
// what keeps a shirt from swallowing the arms. Boxes MAY overlap (a sleeve
// cell belongs to both the top and body boxes); per-frame ownership resolves
// who paints an overlapping cell at render time.
// 槽的轮廓框 = 该槽在所有动作芯片帧中出现过的格子并集。部件只许画在本槽框内
// —— 这正是防止上衣吞掉胳膊的机制。框允许重叠（袖格同属 top 框与 body 框）；
// 重叠格由渲染时的逐帧归属决定谁画。
#pragma once

#include <opencv2/core.hpp>

#include <map>
#include <string>
#include <vector>

namespace fb {

struct RegionBoxes {
    int cell = 0;
    // slot -> mask[y][x], true = inside the box / 槽 -> 掩码，true = 框内
    std::map<std::string, std::vector<std::vector<bool>>> masks;

    bool hasSlot(const std::string& slot) const { return masks.count(slot) != 0; }
    bool contains(const std::string& slot, int x, int y) const;
    int count(const std::string& slot) const;
};

// Load assets/templates/chibi_regions.json (schema "chibi regions v1").
// 读取 chibi_regions.json（schema "chibi regions v1"）。
bool loadRegions(const std::string& jsonPath, RegionBoxes& out, std::string& err);

// Count opaque pixels of `img` (32x32 template coords) painted outside the
// slot's box. 0 = clean, >0 = contract violation (the editor rejects these).
// 数出 img（32x32 模板坐标）画到槽框外的不透明像素数。0 = 合规；>0 = 违约
// （编辑器拒绝保存）。
int outOfBoxPixels(const cv::Mat& img, const std::string& slot,
                   const RegionBoxes& boxes);

}  // namespace fb
