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

bool PartEditor::viewReady(int v) const {
    return v >= 0 && v < 3 && !pixels[v].empty();
}

void PartEditor::blank(cv::Size sz) {
    canvas = sz;
    for (int v = 0; v < 3; ++v) {
        // Views start EMPTY — "not drawn yet" is distinct from "drawn all
        // transparent", and only drawn views may be saved (back:"auto" vs
        // back:"file" depends on it). The canvas is created on first paint.
        // 视图从"空"开始 —— "还没画"与"画了全透明"不同，只有画过的视图才入库
        // （back:"auto" 与 back:"file" 靠它区分）。画布在第一笔时才创建。
        pixels[v] = cv::Mat();
        undoSnap[v] = cv::Mat();
        touched[v] = false;
    }
    hasUndo = false;
    view = FRONT;
}

void PartEditor::setView(int v) {
    if (v >= 0 && v < 3) view = v;
}

void PartEditor::snapshot() {
    for (int v = 0; v < 3; ++v) undoSnap[v] = pixels[v].clone();
    hasUndo = true;
}

void PartEditor::undo() {
    if (!hasUndo) return;
    for (int v = 0; v < 3; ++v) pixels[v] = undoSnap[v];
    hasUndo = false;
}

// Create the canvas on first edit. / 第一笔编辑时创建画布。
void PartEditor::ensureCanvas(int v) {
    if (v < 0 || v >= 3) return;
    if (pixels[v].empty()) pixels[v] = cv::Mat::zeros(canvas, CV_8UC4);
}

void PartEditor::paint(int v, int x, int y, const cv::Vec3b& bgr) {
    if (v < 0 || v >= 3) return;
    ensureCanvas(v);
    if (x < 0 || y < 0 || x >= pixels[v].cols || y >= pixels[v].rows) return;
    pixels[v].at<cv::Vec4b>(y, x) = cv::Vec4b(bgr[0], bgr[1], bgr[2], 255);
    touched[v] = true;
}

void PartEditor::erase(int v, int x, int y) {
    if (v < 0 || v >= 3) return;
    ensureCanvas(v);
    if (x < 0 || y < 0 || x >= pixels[v].cols || y >= pixels[v].rows) return;
    pixels[v].at<cv::Vec4b>(y, x) = cv::Vec4b(0, 0, 0, 0);
    touched[v] = true;
}

void PartEditor::fillRole(int v, const cv::Vec3b& fromBgr, const cv::Vec3b& toBgr) {
    if (v < 0 || v >= 3) return;
    ensureCanvas(v);
    // Same exact src->dst path as chip reskin (parts_semantics gate covers its
    // semantics: near-miss colours survive, alpha preserved).
    // 与芯片换皮同一条精确 src→dst 路径（parts_semantics 闸门已覆盖其语义：
    // 近似色幸存、alpha 保留）。
    const std::vector<PartLayer::RoleColor> roles = {{"", fromBgr, toBgr}};
    applyPaletteRoles(pixels[v], roles);
    touched[v] = true;
}

bool PartEditor::inferBack(const TemplateSpec& spec, std::string& err) {
    if (!viewReady(FRONT)) {
        err = "需要正面视图才能推断背面";
        return false;
    }
    const cv::Mat side = viewReady(SIDE) ? pixels[SIDE] : cv::Mat();
    cv::Mat back = inferBackView(pixels[FRONT], side, spec, err);
    if (back.empty()) return false;
    pixels[BACK] = back;
    touched[BACK] = true;
    return true;
}

bool saveEditorItem(const PartEditor& ed, const std::string& libraryRoot,
                    std::string& err) {
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
    if (!ed.viewReady(PartEditor::FRONT) || !ed.viewReady(PartEditor::SIDE)) {
        err = "正面与侧面必须都有图才能入库";
        return false;
    }
    for (int v = 0; v < 2; ++v) {
        if (ed.pixels[v].size() != ed.canvas || ed.pixels[v].type() != CV_8UC4) {
            err = "视图必须是 " + std::to_string(ed.canvas.width) + "x" +
                  std::to_string(ed.canvas.height) + " BGRA";
            return false;
        }
    }

    const fs::path dir = fs::path(libraryRoot) / ed.slot / ed.itemName;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        err = "无法创建目录 " + dir.string() + ": " + ec.message();
        return false;
    }

    const char* names[3] = {"front.png", "side.png", "back.png"};
    for (int v = 0; v < 3; ++v) {
        if (!ed.viewReady(v)) continue;
        if (!cv::imwrite((dir / names[v]).string(), ed.pixels[v])) {
            err = "写图失败: " + (dir / names[v]).string();
            return false;
        }
    }
    // Loader rule: back:"auto" must NOT have back.png and back:"file" must.
    // 载入器规则：back:"auto" 不得有 back.png，back:"file" 必须有。
    const bool hasBack = ed.viewReady(PartEditor::BACK);

    std::ofstream f(dir / "item.json");
    if (!f) {
        err = "无法写 " + (dir / "item.json").string();
        return false;
    }
    f << "{\n"
      << "  \"name\": \"" << ed.itemName << "\",\n"
      << "  \"slot\": \"" << ed.slot << "\",\n"
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
    const int map[3] = {PartEditor::FRONT, PartEditor::SIDE, PartEditor::BACK};
    for (int i = 0; i < 3; ++i) {
        if (!item.views[i].empty()) {
            ed.pixels[map[i]] = item.views[i].clone();
            ed.touched[map[i]] = false;
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
