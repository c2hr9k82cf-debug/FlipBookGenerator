// part_editor.cpp — part editor state machine implementation (v0.10.0).
// 部件编辑器状态机实现（v0.10.0）。
#include "core/part_editor.h"

#include "core/backinfer.h"
#include "core/parts.h"
#include "core/parts_library.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <vector>

#include <opencv2/imgcodecs.hpp>

namespace fs = std::filesystem;

namespace fb {
namespace {

// cv::Vec3b has no operator< — pack colours as ints. / cv::Vec3b 没有 operator<，
// 颜色打包成整数。
int keyOf(const cv::Vec3b& c) {
    return (int(c[0]) << 16) | (int(c[1]) << 8) | int(c[2]);
}

}  // namespace

bool PartEditor::viewReady(int p, int v) const {
    return p >= 0 && p < 2 && v >= 0 && v < 3 && !pixels[p][v].empty();
}

void PartEditor::blank(cv::Size sz) {
    canvas = sz;
    for (int p = 0; p < 2; ++p)
        for (int v = 0; v < 3; ++v) {
            // Views start EMPTY — "not drawn yet" is distinct from "drawn all
            // transparent", and only drawn views may be saved (back:"auto" vs
            // back:"file" depends on it). The canvas is created on first paint.
            // 视图从"空"开始 —— "还没画"与"画了全透明"不同，只有画过的视图才入库
            // （back:"auto" 与 back:"file" 靠它区分）。画布在第一笔时才创建。
            pixels[p][v] = cv::Mat();
            undoSnap[p][v] = cv::Mat();
            touched[p][v] = false;
        }
    hasUndo = false;
    view = FRONT;
    piece = MAIN;
}

void PartEditor::setView(int v) {
    if (v >= 0 && v < 3) view = v;
}

void PartEditor::setPiece(int p) {
    if (p >= 0 && p < 2) piece = p;
}

// Switch slots with draft stash/restore (v0.11.1 bug B7). The work canvas
// belongs to one slot only: the whole [piece][view] set stashes into
// drafts[old], the target slot's set comes back (or a fresh empty one). The
// single undo step belongs to the old canvas and dies with it.
// 切槽并暂存/恢复草稿（v0.11.1 bug B7）。工作画布只属于一个槽：整套 [片][视图]
// 存进 drafts[旧槽]，目标槽的整套取回来（没有就是全新空件）。单步撤销属于旧画布，
// 随之作废。
void PartEditor::switchSlot(const std::string& newSlot) {
    if (newSlot.empty() || newSlot == slot) return;
    Draft out;
    for (int p = 0; p < 2; ++p)
        for (int v = 0; v < 3; ++v) {
            out.pixels[p][v] = pixels[p][v];
            out.touched[p][v] = touched[p][v];
        }
    out.itemName = itemName;
    out.piece = piece;
    drafts[slot] = out;

    const auto it = drafts.find(newSlot);
    if (it != drafts.end()) {
        for (int p = 0; p < 2; ++p)
            for (int v = 0; v < 3; ++v) {
                pixels[p][v] = it->second.pixels[p][v];
                touched[p][v] = it->second.touched[p][v];
            }
        itemName = it->second.itemName;
        piece = it->second.piece;
        drafts.erase(it);
    } else {
        // Same contract as blank(): views start EMPTY ("not drawn yet" is
        // distinct from "drawn all transparent"). / 与 blank() 同契约：视图
        // 从"空"开始（"还没画"与"画了全透明"不同）。
        for (int p = 0; p < 2; ++p)
            for (int v = 0; v < 3; ++v) {
                pixels[p][v] = cv::Mat();
                touched[p][v] = false;
            }
        itemName = "untitled";
        piece = MAIN;
    }
    for (int p = 0; p < 2; ++p)
        for (int v = 0; v < 3; ++v) undoSnap[p][v] = cv::Mat();
    hasUndo = false;
    slot = newSlot;
}

void PartEditor::snapshot() {
    for (int p = 0; p < 2; ++p)
        for (int v = 0; v < 3; ++v) undoSnap[p][v] = pixels[p][v].clone();
    hasUndo = true;
}

void PartEditor::undo() {
    if (!hasUndo) return;
    for (int p = 0; p < 2; ++p)
        for (int v = 0; v < 3; ++v) pixels[p][v] = undoSnap[p][v];
    hasUndo = false;
}

// Create the canvas on first edit. / 第一笔编辑时创建画布。
void PartEditor::ensureCanvas(int p, int v) {
    if (p < 0 || p >= 2 || v < 0 || v >= 3) return;
    if (pixels[p][v].empty()) pixels[p][v] = cv::Mat::zeros(canvas, CV_8UC4);
}

void PartEditor::paint(int v, int x, int y, const cv::Vec3b& bgr) {
    if (v < 0 || v >= 3) return;
    ensureCanvas(piece, v);
    cv::Mat& img = pixels[piece][v];
    if (x < 0 || y < 0 || x >= img.cols || y >= img.rows) return;
    img.at<cv::Vec4b>(y, x) = cv::Vec4b(bgr[0], bgr[1], bgr[2], 255);
    touched[piece][v] = true;
}

void PartEditor::erase(int v, int x, int y) {
    if (v < 0 || v >= 3) return;
    ensureCanvas(piece, v);
    cv::Mat& img = pixels[piece][v];
    if (x < 0 || y < 0 || x >= img.cols || y >= img.rows) return;
    img.at<cv::Vec4b>(y, x) = cv::Vec4b(0, 0, 0, 0);
    touched[piece][v] = true;
}

void PartEditor::fillRole(int v, const cv::Vec3b& fromBgr, const cv::Vec3b& toBgr) {
    if (v < 0 || v >= 3) return;
    ensureCanvas(piece, v);
    // Same exact src->dst path as chip reskin (parts_semantics gate covers its
    // semantics: near-miss colours survive, alpha preserved).
    // 与芯片换皮同一条精确 src→dst 路径（parts_semantics 闸门已覆盖其语义：
    // 近似色幸存、alpha 保留）。
    const std::vector<PartLayer::RoleColor> roles = {{"", fromBgr, toBgr}};
    applyPaletteRoles(pixels[piece][v], roles);
    touched[piece][v] = true;
}

bool PartEditor::inferBack(const TemplateSpec& spec, std::string& err) {
    if (!viewReady(FRONT)) {
        err = "需要正面视图才能推断背面";
        return false;
    }
    const cv::Mat side = viewReady(SIDE) ? pixels[piece][SIDE] : cv::Mat();
    cv::Mat back = inferBackView(pixels[piece][FRONT], side, spec, err);
    if (back.empty()) return false;
    pixels[piece][BACK] = back;
    touched[piece][BACK] = true;
    return true;
}

bool saveEditorItem(const PartEditor& ed, const std::string& libraryRoot,
                    const RegionBoxes* boxes, std::string& err) {
    // Names become folder names — no separators, no empties.
    // 名字即目录名 —— 不许带分隔符、不许为空。
    if (ed.itemName.empty() ||
        ed.itemName.find_first_of("/\\.") != std::string::npos) {
        err = "部件名不能为空，也不能含 / \\ 或 .";
        return false;
    }
    if (ed.slot.find_first_of("/\\") != std::string::npos || ed.slot.empty()) {
        err = "非法槽位名: " + ed.slot;
        return false;
    }
    if (!ed.viewReady(PartEditor::MAIN, PartEditor::FRONT) ||
        !ed.viewReady(PartEditor::MAIN, PartEditor::SIDE)) {
        err = "正面与侧面必须都有图才能入库";
        return false;
    }
    // Loader rule: back:"auto" must NOT have back_b.png (inference owns the
    // back pieces there). / 载入器规则：back:"auto" 不得有 back_b.png（推断拥有
    // 背面两片）。
    const bool hasBack = ed.viewReady(PartEditor::MAIN, PartEditor::BACK);
    if (!hasBack && ed.viewReady(PartEditor::BACKP, PartEditor::BACK)) {
        err = "垫底片画了背面，主片也得有背面（可先推断）才能入库";
        return false;
    }

    // Validate EVERYTHING before writing anything (v0.12.0): size, type and
    // the region-box drawing contract. A half-saved item is worse than none.
    // 写任何东西之前先校验一切（v0.12.0）：尺寸、类型与轮廓框绘制契约。
    // 半成品部件比没有更糟。
    for (int p = 0; p < 2; ++p) {
        for (int v = 0; v < 3; ++v) {
            if (!ed.viewReady(p, v)) continue;
            const cv::Mat& img = ed.pixels[p][v];
            if (img.size() != ed.canvas || img.type() != CV_8UC4) {
                err = "视图必须是 " + std::to_string(ed.canvas.width) + "x" +
                      std::to_string(ed.canvas.height) + " BGRA";
                return false;
            }
            if (boxes && boxes->hasSlot(ed.slot)) {
                const int bad = outOfBoxPixels(img, ed.slot, *boxes);
                if (bad > 0) {
                    static const char* kPieceName[2] = {"", "垫底片"};
                    static const char* kViewName[3] = {"正面", "侧面", "背面"};
                    err = std::string(kPieceName[p]) + kViewName[v] + "有 " +
                          std::to_string(bad) + " 个像素画出了" + ed.slot +
                          "槽的轮廓框（框外打灰的格子不可着色）";
                    return false;
                }
            }
        }
    }

    const fs::path dir = fs::path(libraryRoot) / ed.slot / ed.itemName;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        err = "无法创建目录 " + dir.string() + ": " + ec.message();
        return false;
    }

    const char* names[2][3] = {{"front.png", "side.png", "back.png"},
                               {"front_b.png", "side_b.png", "back_b.png"}};
    for (int p = 0; p < 2; ++p) {
        for (int v = 0; v < 3; ++v) {
            if (!ed.viewReady(p, v)) continue;
            if (!cv::imwrite((dir / names[p][v]).string(), ed.pixels[p][v])) {
                err = "写图失败: " + (dir / names[p][v]).string();
                return false;
            }
        }
    }

    std::ofstream f(dir / "item.json");
    if (!f) {
        err = "无法写 " + (dir / "item.json").string();
        return false;
    }
    // v0.11.2 fix A: write the slot's zOrder explicitly — the old save wrote
    // none and the loader's 0 default sank user items under everything.
    // v0.11.2 修复 A：显式写入槽位 zOrder —— 旧保存不写、加载缺省 0，把用户
    // 部件垫到一切底下。
    f << "{\n"
      << "  \"name\": \"" << ed.itemName << "\",\n"
      << "  \"slot\": \"" << ed.slot << "\",\n"
      << "  \"zOrder\": " << defaultZOrderForSlot(ed.slot) << ",\n"
      << "  \"back\": \"" << (hasBack ? "file" : "auto") << "\",\n"
      << "  \"silhouette\": \"template\"\n"
      << "}\n";
    return true;
}

bool loadEditorItem(PartEditor& ed, const std::string& itemDir,
                    const std::string& slot, cv::Size expectedSize,
                    std::string& err) {
    LibraryItem item;
    if (!loadLibraryItem(itemDir, slot, expectedSize, item, err)) return false;
    ed.blank(expectedSize);
    ed.slot = slot;
    ed.itemName = item.name;
    for (int i = 0; i < 3; ++i) {
        if (!item.views[i].empty()) {
            ed.pixels[PartEditor::MAIN][i] = item.views[i].clone();
            ed.touched[PartEditor::MAIN][i] = false;
        }
        if (item.hasB[i]) {
            ed.pixels[PartEditor::BACKP][i] = item.viewsB[i].clone();
            ed.touched[PartEditor::BACKP][i] = false;
        }
    }
    return true;
}

int roleConstraintViolations(const cv::Mat& img, const TemplateSpec& spec) {
    std::set<int> legal;
    for (const auto& kv : spec.palette) legal.insert(keyOf(kv.second));
    int bad = 0;
    for (int y = 0; y < img.rows; ++y) {
        const cv::Vec4b* row = img.ptr<cv::Vec4b>(y);
        for (int x = 0; x < img.cols; ++x) {
            if (row[x][3] == 0) continue;
            if (!legal.count(keyOf(cv::Vec3b(row[x][0], row[x][1], row[x][2]))))
                ++bad;
        }
    }
    return bad;
}

}  // namespace fb
