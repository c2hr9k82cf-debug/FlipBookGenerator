// parts_library.h — the swappable parts library (v0.9.1 format + loader).
// 可换部件库（v0.9.1 格式 + 加载器）。
//
// On-disk layout (the folder IS the exchange format):
//   parts_library/<slot>/<item_name>/
//     front.png   32x32 BGRA, template coords (required)
//     side.png    32x32 BGRA, template coords (required)
//     back.png    32x32 BGRA (required iff item.json says back:"file")
//     item.json   metadata — see docs/PARTS_FORMAT.md
//     poses/      reserved for v2 shape-free animation; v1 never touches it
// 磁盘布局（文件夹即交换格式）：
//   parts_library/<槽位>/<物品名>/
//     front.png   32x32 BGRA，模板坐标（必备）
//     side.png    32x32 BGRA，模板坐标（必备）
//     back.png    32x32 BGRA（item.json 为 back:"file" 时必备）
//     item.json   元数据 —— 见 docs/PARTS_FORMAT.md
//     poses/      v2 形状自由动画预留；v1 不读不写
//
// The user library lives in
//   ~/Library/Application Support/FlipBookGenerator/parts_library/
// and ships read-only starter content inside the .app Resources. Import/export
// copies whole library folders (v1: folder copy; UI comes in P3).
// 用户库在 ~/Library/Application Support/FlipBookGenerator/parts_library/，
// .app Resources 内另带只读起步库。导入/导出 = 整库文件夹复制（v1 只做复制，
// UI 在 P3）。
#pragma once

#include <map>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace fb {

// One library item (a hairdo, a shirt...). / 一件库物品（发型、上衣……）。
struct LibraryItem {
    std::string name;        // item name (dir name is the fallback) / 物品名（目录名兜底）
    std::string slot;        // "hair"|"face"|"top"|"pants"|"boots"|"body"
    std::string dir;         // containing folder / 所在目录
    int zOrder = 0;          // compositing order (higher = on top) / 合成序

    // Per-view sprite in template coords. Index 0 front, 1 side, 2 back.
    // back may be empty when backMode == "auto" (inferred at compose time).
    // 模板坐标下的逐视图精灵。下标 0 正、1 侧、2 背。
    // backMode == "auto" 时 back 可为空（合成时推断）。
    cv::Mat views[3];

    // Per-view anchor overrides (part pivot hints). / 逐视图锚点覆盖。
    bool hasAnchor[3] = {false, false, false};
    cv::Point anchors[3];

    // Role recolour for this item: exact src colour → dst colour. src may be
    // omitted (the template palette's value for `role` is used at compose time).
    // 本物品的角色换色：精确 src 色 → dst 色。src 可省略（合成时用模板
    // palette 里该 role 的值）。
    struct RoleColorOverride {
        std::string role;
        bool hasSrc = false;
        cv::Vec3b srcBgr{0, 0, 0};
        cv::Vec3b dstBgr{0, 0, 0};
    };
    std::vector<RoleColorOverride> roleColors;

    // "auto" = infer back from front+side (P2b); "file" = back.png is the back.
    // "auto" = 从正+侧推断背面（P2b）；"file" = back.png 即背面。
    std::string backMode = "auto";
    std::string silhouette = "template";  // v1 fixed / v1 固定
};

// A whole library root. / 一整棵库。
struct PartsLibrary {
    std::string root;
    std::vector<LibraryItem> items;   // sorted by (slot, name) / 按 (slot, name) 排序

    // Find one item; nullptr when absent. / 查一件；没有则 nullptr。
    const LibraryItem* find(const std::string& slot, const std::string& name) const;
    // All items of a slot. / 某槽位的全部物品。
    std::vector<const LibraryItem*> inSlot(const std::string& slot) const;
};

// The six legal slots (PartLayer slots). / 六个合法槽位（PartLayer 槽位）。
const std::vector<std::string>& librarySlotNames();

// Load a whole library directory (slot folders → item folders). An invalid
// item fails the whole load with a reason naming the offending folder —
// a half-loaded library is worse than none.
// 加载整库目录（槽位目录 → 物品目录）。任一物品非法则整库加载失败，报错指明
// 出事文件夹 —— 半加载的库比没有库更糟。
bool loadPartsLibrary(const std::string& rootDir, cv::Size expectedSize,
                      PartsLibrary& out, std::string& err);

// Load one item folder. `slot` is the parent folder name and must match
// item.json's "slot" (guards against items filed under the wrong slot).
// 加载一个物品目录。slot 为父目录名，必须与 item.json 的 "slot" 一致
// （防止物品放错槽位目录）。
bool loadLibraryItem(const std::string& itemDir, const std::string& slot,
                     cv::Size expectedSize, LibraryItem& out, std::string& err);

}  // namespace fb
