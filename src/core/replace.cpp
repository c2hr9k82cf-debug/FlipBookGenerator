// replace.cpp — region classification + slot split + anim body_top loader
// (v0.13.0, phase 2 batch 1)
// 区域分类 + 槽拆分 + anim body_top 加载（v0.13.0，阶段二批次①）
#include "core/replace.h"

#include "core/json_min.h"
#include "core/parts_library.h"   // defaultLayerZ

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace fb {

namespace {

inline unsigned int packBgr(const cv::Vec3b& c) {
    return ((unsigned int)c[0] << 16) | ((unsigned int)c[1] << 8) | c[2];
}

// Row band table: yNorm (may be -1..32 under a ±1 bob) -> 0 head / 1 body /
// 2 leg / 3 other. Offset keeps it a plain array.
// 行带表：yNorm（起伏 ±1 下取 -1..32）-> 0 头 / 1 躯 / 2 腿 / 3 其他。带偏移
// 使它成为普通数组。
constexpr int kBandLo = -2, kBandHi = 33, kBandN = kBandHi - kBandLo + 1;

// The one assignment core: classify every opaque pixel, then let outline
// pixels join by 8-neighbour vote. map = {} reads raw role colours. When the
// map cannot classify a recoloured pixel (ambiguous dst, or a minority colour
// the extraction repaired away), the reference design at the same cell decides
// the slot. ref = {} disables that fallback.
// 唯一归属核心：先分类所有不透明像素，再让描边像素按 8 邻投票归槽。
// map = {} 时按原始角色色读。映射分不出槽时（歧义 dst、被多数派修复掉的少数派
// 色），由同一格的参考设计定槽。ref = {} 时无此兜底。
cv::Mat assignSlots(const cv::Mat& img, int off, const SlotTables& t,
                    const PaletteMap& map, const cv::Mat& ref) {
    // SLOT_NONE (=-1), NOT zero — zero is SLOT_HAIR, and transparent cells
    // must not vote in the outline pass.
    // SLOT_NONE（=-1），不是零 —— 零是 SLOT_HAIR，透明格绝不能给描边投票。
    cv::Mat slotOf(img.rows, img.cols, CV_8SC1, cv::Scalar(SLOT_NONE));
    // Per-pixel preimage resolution: scan the map for the dst colour, take the
    // classify of its src role colours. A unique result wins; two distinct
    // results = an ambiguous recolour -> SLOT_UNKNOWN. An outline-only dst
    // (the user's outline colour) returns SLOT_OUTLINE so the pixel joins the
    // neighbour vote instead of being dropped.
    // 逐像素原像解析：扫 map 找 dst 色，取其 src 角色色的 classify。唯一结果胜出；
    // 两个不同结果 = 换色歧义 -> SLOT_UNKNOWN。纯描边 dst（用户的描边色）返回
    // SLOT_OUTLINE，让像素进邻域投票而不是被丢弃。
    auto slotViaMap = [&](const cv::Vec4b& px, int yNorm) -> int {
        if (map.empty()) return SLOT_NONE;
        int got = SLOT_NONE;
        bool sawOutline = false;
        for (const auto& kv : map) {
            if (kv.second != px) continue;
            const int s = t.classifyPixel(
                cv::Vec3b(kv.first[0], kv.first[1], kv.first[2]), yNorm);
            if (s == SLOT_OUTLINE) { sawOutline = true; continue; }
            if (s < 0) continue;       // unknown src / 未知 src
            if (got != SLOT_NONE && got != s) return SLOT_UNKNOWN;  // ambiguous / 歧义
            got = s;
        }
        if (got == SLOT_NONE && sawOutline) return SLOT_OUTLINE;
        return got;
    };

    std::vector<cv::Point> outlinePx;
    for (int y = 0; y < img.rows; ++y) {
        for (int x = 0; x < img.cols; ++x) {
            const cv::Vec4b px = img.at<cv::Vec4b>(y, x);
            if (px[3] == 0) continue;
            const int yNorm = y - off;
            const cv::Vec3b bgr(px[0], px[1], px[2]);
            int slot = slotViaMap(px, yNorm);
            if (slot == SLOT_UNKNOWN || slot == SLOT_NONE) {
                // Fallback order: the reference design at this cell, then a raw
                // role-colour read (the chips path, map = {}).
                // 兜底顺序：本格的参考设计，然后原始角色色读取（芯片路径，map = {}）。
                if (!ref.empty() && y < ref.rows && x < ref.cols) {
                    const cv::Vec4b r = ref.at<cv::Vec4b>(y, x);
                    if (r[3] != 0) slot = t.classifyPixel(
                        cv::Vec3b(r[0], r[1], r[2]), yNorm);
                }
                if (slot == SLOT_UNKNOWN || slot == SLOT_NONE) {
                    // Fallback: read the pixel as a raw role colour.
                    // 兜底：把像素当原始角色色读。
                    slot = t.classifyPixel(bgr, yNorm);
                }
            }
            if (slot == SLOT_OUTLINE) {
                outlinePx.push_back(cv::Point(x, y));
                continue;
            }
            if (slot < 0) continue;    // unknown -> SLOT_NONE / 未知 -> SLOT_NONE
            slotOf.at<signed char>(y, x) = (signed char)slot;
        }
    }

    // Outline pass, scan order = the Python tools' iteration order.
    // 描边遍，扫描序 = Python 工具的迭代序。
    for (const cv::Point& p : outlinePx) {
        int counts[6] = {0, 0, 0, 0, 0, 0};
        int insertion[6];              // scan order of first vote / 首票的扫描序
        int insertN = 0;
        int order = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nx = p.x + dx, ny = p.y + dy;
                if (nx < 0 || ny < 0 || nx >= img.cols || ny >= img.rows) continue;
                const signed char s = slotOf.at<signed char>(ny, nx);
                if (s < 0) continue;
                if (counts[s] == 0) insertion[insertN++] = s;
                counts[s]++;
                ++order;
            }
        }
        (void)order;
        if (insertN == 0) continue;    // no owner: dropped / 无归属：丢弃
        int best = insertion[0];
        for (int i = 1; i < insertN; ++i)
            if (counts[insertion[i]] > counts[best]) best = insertion[i];
        // Ties keep the earlier insertion — Counter.most_common's stability.
        // 平票取更早插入者 —— Counter.most_common 的稳定性。
        slotOf.at<signed char>(p.y, p.x) = (signed char)best;
    }
    return slotOf;
}

}  // namespace

const std::vector<std::string>& slotNames() {
    static const std::vector<std::string> kNames = {
        "hair", "face", "top", "pants", "boots", "body"};
    return kNames;
}

int slotIndex(const std::string& name) {
    const auto& n = slotNames();
    for (int i = 0; i < (int)n.size(); ++i)
        if (n[(size_t)i] == name) return i;
    return SLOT_NONE;
}

int classifyRoleRegion(const std::string& role, const std::string& region) {
    // Verbatim lift of tools/extract_regions.py::classify. Keep in lockstep
    // with make_parts_fixture.py / make_blank_parts.py.
    // 逐字升格自 tools/extract_regions.py::classify。与 make_parts_fixture.py /
    // make_blank_parts.py 保持同步。
    if (role == "hair_mid" || role == "hair_dark" || role == "hair_highlight")
        return SLOT_HAIR;
    if (role == "skin" || role == "blush")
        return region == "head" ? SLOT_FACE : SLOT_BODY;
    if (role == "white" || role == "teal")
        return region == "head" ? SLOT_FACE : SLOT_TOP;
    if (role == "mint" || role == "red" || role == "gold")
        return region == "head" ? SLOT_HAIR : SLOT_TOP;
    if (role == "grey")
        return region == "leg" ? SLOT_BOOTS : SLOT_TOP;
    if (role == "grey_dark")
        return SLOT_BOOTS;
    if (role == "pants" || role == "tan")
        return SLOT_PANTS;
    return SLOT_NONE;
}

std::string SlotTables::regionOf(int yNorm) const {
    const auto in = [&](const std::vector<int>& v) {
        return std::find(v.begin(), v.end(), yNorm) != v.end();
    };
    if (in(headRows)) return "head";
    if (in(bodyRows)) return "body";
    if (in(legRows)) return "leg";
    return "other";
}

int SlotTables::classifyPixel(const cv::Vec3b& bgr, int yNorm) const {
    auto it = colourToRole.find(packBgr(bgr));
    if (it == colourToRole.end()) return SLOT_UNKNOWN;
    const std::string& role = it->second;
    if (role == "outline") return SLOT_OUTLINE;
    const std::string region = regionOf(yNorm);
    std::string r = role;
    // hair_highlight / gold share RGB — region decides (tools' rule).
    // hair_highlight / gold 同 RGB —— 由区域决定（工具的规则）。
    if (r == "hair_highlight" || r == "gold")
        r = (region == "head") ? "hair_highlight" : "gold";
    return classifyRoleRegion(r, region);
}

bool buildSlotTables(const TemplateSpec& spec, SlotTables& out, std::string& err) {
    out.colourToRole.clear();
    // Shared-RGB rule: the non-gold role name wins (the tools build the same
    // lookup with "prefer non-gold"); after that the region disambiguates.
    // 同 RGB 规则：非 gold 角色名胜出（工具用"优先非 gold"建同一张表）；之后
    // 由区域消歧。
    for (const auto& kv : spec.palette) {
        const unsigned int key = packBgr(kv.second);
        auto it = out.colourToRole.find(key);
        if (it == out.colourToRole.end() || it->second == "gold")
            out.colourToRole[key] = kv.first;
    }

    // Band-global by design: all three views must agree (the tools agree
    // today — this pins it). / 按设计行带全局：三视图必须一致（工具今天就一致
    // —— 这里钉死它）。
    const PartMap& m0 = spec.views[0].map;
    for (int v = 1; v < 3; ++v) {
        const PartMap& m = spec.views[v].map;
        if (m.headRows != m0.headRows || m.bodyRows != m0.bodyRows ||
            m.legRows != m0.legRows) {
            err = "buildSlotTables: view row bands disagree (band must be global)";
            return false;
        }
    }
    out.headRows = m0.headRows;
    out.bodyRows = m0.bodyRows;
    out.legRows = m0.legRows;
    if (out.headRows.empty() || out.bodyRows.empty() || out.legRows.empty()) {
        err = "buildSlotTables: empty row band";
        return false;
    }
    return true;
}

cv::Mat classifyFrame(const cv::Mat& frame, int off, const SlotTables& t) {
    return assignSlots(frame, off, t, PaletteMap(), cv::Mat());
}

SlotSplit splitViewBySlot(const cv::Mat& view, const PaletteMap& map,
                          const SlotTables& t, const cv::Mat& refView) {
    SlotSplit out;
    out.slotOf = assignSlots(view, 0, t, map, refView);
    // One fresh Mat per slot — Mat copies share pixels, assign() would make
    // the six sprites aliases of one buffer.
    // 每槽一张新 Mat —— Mat 拷贝共享像素，assign() 会让六精灵互为别名。
    out.sprites.clear();
    for (int i = 0; i < 6; ++i)
        out.sprites.push_back(cv::Mat::zeros(view.rows, view.cols, CV_8UC4));
    out.unassigned = 0;
    for (int y = 0; y < view.rows; ++y) {
        for (int x = 0; x < view.cols; ++x) {
            const cv::Vec4b px = view.at<cv::Vec4b>(y, x);
            if (px[3] == 0) continue;
            const signed char s = out.slotOf.at<signed char>(y, x);
            if (s < 0) {
                ++out.unassigned;
                continue;
            }
            out.sprites[(size_t)s].at<cv::Vec4b>(y, x) = px;
        }
    }
    return out;
}

bool loadAnimBodyTop(const std::string& jsonPath,
                     std::map<std::string, std::vector<int>>& out,
                     std::string& err) {
    // chip flipbook name -> anim_reference.json animation key / 芯片名 -> 动画键
    // (same table as tools/extract_regions.py / tools/extract_reference_animation.py)
    static const std::pair<const char*, const char*> kFlipbookToAnim[] = {
        {"front_idle", "idle_front"}, {"front_walk", "walk_front"},
        {"side_idle", "idle_side"},   {"side_walk", "walk_side"},
        {"back_idle", "idle_back"},   {"back_walk", "walk_up"},
    };
    std::ifstream f(jsonPath);
    if (!f) {
        err = "loadAnimBodyTop: cannot open " + jsonPath;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    JsonValue root;
    if (!jsonParse(ss.str(), root, err)) {
        err = "loadAnimBodyTop: " + err;
        return false;
    }
    const JsonValue* anims = root.find("animations");
    if (!anims || anims->type != JsonValue::Object) {
        err = "loadAnimBodyTop: missing animations object";
        return false;
    }
    out.clear();
    for (const auto& fb : kFlipbookToAnim) {
        const JsonValue* a = anims->find(fb.second);
        if (!a || a->type != JsonValue::Object) {
            err = std::string("loadAnimBodyTop: missing animation ") + fb.second;
            return false;
        }
        const JsonValue* frames = a->find("frame_data");
        if (!frames || frames->type != JsonValue::Array || frames->arr.empty()) {
            err = std::string("loadAnimBodyTop: ") + fb.second +
                  ": frame_data missing or empty";
            return false;
        }
        std::vector<int> tops;
        for (const auto& fr : frames->arr) {
            const JsonValue* bty = fr.find("body_top_y");
            if (!bty) {
                err = std::string("loadAnimBodyTop: ") + fb.second +
                      ": frame_data entry without body_top_y";
                return false;
            }
            tops.push_back((int)bty->asInt(-999));
        }
        out[fb.first] = std::move(tops);
    }
    return true;
}

std::vector<cv::Mat> renderReplaced(const MotionChips& chips, const PaletteMap& map,
                                    const cv::Mat& userView, const cv::Mat& userSlots,
                                    const cv::Mat& refView,
                                    const std::vector<int>& bodyTop,
                                    const SlotTables& t) {
    std::vector<cv::Mat> frames;
    if (bodyTop.size() != chips.frames.size()) return frames;   // caller guards / 调用方守
    if (userView.empty() || userSlots.empty() || refView.empty()) return frames;
    if (userView.size() != refView.size() || userSlots.size() != refView.size())
        return frames;

    // Exact-colour palette lookup (the applyPaletteMap rule: no match = keep).
    // 精确换色查表（applyPaletteMap 的规则：无匹配 = 原样）。
    auto colourKey4 = [](const cv::Vec4b& c) {
        return (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) |
               ((uint32_t)c[3] << 24);
    };
    std::map<uint32_t, cv::Vec4b> lut;
    std::set<uint32_t> dstSet;
    for (const auto& sw : map) {
        lut[colourKey4(sw.first)] = sw.second;
        dstSet.insert(colourKey4(sw.second));
    }
    auto mapColour = [&](const cv::Vec4b& px) {
        auto it = lut.find(colourKey4(px));
        return it == lut.end() ? px : it->second;
    };
    // defaultLayerZ: pants/boots 10 < top 20 < face 30 < body(bare skin) 45 <
    // hair 50. / 层序表。
    int zOf[6];
    for (int s = 0; s < 6; ++s) zOf[s] = defaultLayerZ(slotNames()[(size_t)s], false);

    // Reference design slots (normal pose): the recession rule compares the
    // frame cell's slot against the design cell the user omitted.
    // 参考设计槽（规范姿态）：缩水规则用它比对用户删掉的设计格与帧内格是否同槽。
    const cv::Mat refSlots = classifyFrame(refView, 0, t);

    frames.reserve(chips.frames.size());
    for (size_t f = 0; f < chips.frames.size(); ++f) {
        const cv::Mat& C = chips.frames[f];
        const int off = bodyTop[f] - 2;
        const cv::Mat slotC = classifyFrame(C, off, t);
        cv::Mat out = cv::Mat::zeros(C.rows, C.cols, CV_8UC4);
        for (int y = 0; y < C.rows; ++y) {
            for (int x = 0; x < C.cols; ++x) {
                const cv::Vec4b Cpx = C.at<cv::Vec4b>(y, x);
                // Material position in normal coords / 素材的规范坐标位置
                const int sy = y - off;
                const bool inView = sy >= 0 && sy < refView.rows && x < refView.cols;
                const cv::Vec4b zero(0, 0, 0, 0);
                const cv::Vec4b Npx = inView ? refView.at<cv::Vec4b>(sy, x) : zero;
                const cv::Vec4b Upx = inView ? userView.at<cv::Vec4b>(sy, x) : zero;

                int bestZ = -1;
                cv::Vec4b bestCol = zero;
                bool have = false;
                // Recession rule: the user deleted this design cell (reference
                // opaque, user transparent) and the frame cell belongs to the
                // same part — honour the deletion. Slot matching keeps a
                // swinging limb that passes through the deleted cell intact.
                // 缩水规则：用户删掉了这个设计格（参考不透明、用户透明）且帧内格
                // 属于同一部件 —— 尊重删除。同槽比对保证扫过删除格的摆动肢体不被
                // 挖洞。
                bool suppressed = false;
                if (Cpx[3] != 0 && Npx[3] != 0 && Upx[3] == 0 && inView) {
                    const signed char sN = refSlots.at<signed char>(sy, x);
                    const signed char sC = slotC.at<signed char>(y, x);
                    suppressed = sN >= 0 && sN == sC;
                }
                // FRAME candidate: the chip occupant at its slot's layer.
                // 帧占用候选：芯片像素，按其槽层序。
                if (Cpx[3] != 0 && !suppressed) {
                    const signed char sC = slotC.at<signed char>(y, x);
                    bestZ = (sC >= 0) ? zOf[(size_t)sC] : 0;   // unowned = bottom / 无主垫底
                    bestCol = mapColour(Cpx);
                    have = true;
                }
                // MATERIAL candidate — gate: the reference is transparent here
                // (user extension) OR the chip paints the cell. Without the
                // gate a swinging leg's vacated cell would keep showing the
                // static leg (double legs).
                // 素材候选 —— 闸门：参考处透明（用户扩展）或芯片画了这格。没闸门
                // 的话摆腿腾空的格会继续显示静态腿（双腿重影）。
                if (Upx[3] != 0 && (Npx[3] == 0 || Cpx[3] != 0)) {
                    const signed char sU = userSlots.at<signed char>(sy, x);
                    if (sU >= 0) {
                        // M15 colour rule: follows the reference layout ->
                        // mapped chip colour (tone swaps survive); deviates ->
                        // its own colour. A deviating colour that is no map
                        // destination is a minority the extraction repaired
                        // away (the GUI warns "majority chosen"): output the
                        // repaired colour map(N), keeping the map a function.
                        // M15 色规：沿参考布局 -> 换色后的芯片色（色调交换存活）；
                        // 偏离 -> 出自己的色。偏离色若不是映射目标色，就是提取按
                        // 主色修复掉的少数派（GUI 警告"已按主色处理"）：输出修复后
                        // 的 map(N)，保持映射的函数性。
                        const bool follows = Npx[3] != 0 && Upx == mapColour(Npx);
                        cv::Vec4b colM;
                        if (follows) {
                            colM = mapColour(Cpx);
                        } else if (Npx[3] != 0 && !dstSet.count(colourKey4(Upx))) {
                            colM = mapColour(Npx);
                        } else {
                            colM = Upx;
                        }
                        const int zM = zOf[(size_t)sU];
                        // Higher z wins, ties go to the material.
                        // z 大者胜，平票素材胜。
                        if (!have || zM >= bestZ) {
                            bestZ = zM;
                            bestCol = colM;
                            have = true;
                        }
                    }
                }
                if (have) out.at<cv::Vec4b>(y, x) = bestCol;
            }
        }
        frames.push_back(out);
    }
    return frames;
}

}  // namespace fb
