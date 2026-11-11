// sheet.cpp — the four-direction sheet: extract -> reskin -> assemble -> tile
// 四方向拼接大图：提取 → 换皮 → 组装 → 平铺
//
// This is the .app's whole product: three template-aligned user views in, one
// 8x6 spritesheet PNG out. Everything here is core-level (no UI), so the GUI
// and the CLI drive the exact same code — the byte-identical export invariant.
// 这就是 .app 的全部产品：三张模板对齐的用户视图进，一张 8x6 精灵大图出。
// 全部在 core 层（无 UI），故 GUI 与 CLI 走完全相同的代码 —— 导出逐字节一致的不变量。

#include "core.h"

#include <set>
#include <sstream>

namespace fb {
namespace {

// The side chips face RIGHT (eyeballed from side_idle_0.png: face/eyes on the
// right, hair back on the left). So the right rows use the side chips verbatim
// and the left rows mirror them. If a future chip set faces left, flip this.
// 侧视芯片朝右（目检 side_idle_0.png：脸/眼在右，后脑头发在左）。因此 right 行
// 直接用侧视芯片，left 行镜像。若将来的芯片集朝左，翻转此常量即可。
constexpr bool kSideChipsFaceRight = true;

std::vector<cv::Mat> mirrorAll(const std::vector<cv::Mat>& frames) {
    std::vector<cv::Mat> out;
    out.reserve(frames.size());
    for (const cv::Mat& f : frames) out.push_back(mirrorFrame(f));
    return out;
}

uint32_t colourKey(const cv::Vec4b& c) {
    return (uint32_t)c[0] | ((uint32_t)c[1] << 8) |
           ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 24);
}

}  // namespace

std::vector<cv::Mat> repeatToLength(const std::vector<cv::Mat>& frames, int cols) {
    std::vector<cv::Mat> out;
    if (frames.empty() || cols <= 0) return out;
    out.reserve(cols);
    for (int i = 0; i < cols; ++i) out.push_back(frames[(size_t)i % frames.size()]);
    return out;
}

bool composeFourDirSheet(const FourDirRows& rows, cv::Size cell,
                         cv::Mat& out, std::string& err) {
    if (cell.width <= 0 || cell.height <= 0) {
        err = "composeFourDirSheet: bad cell size";
        return false;
    }
    for (int r = 0; r < SHEET_ROWS; ++r) {
        if ((int)rows.rows[r].size() != SHEET_COLS) {
            err = "composeFourDirSheet: row " + std::to_string(r) + " has " +
                  std::to_string(rows.rows[r].size()) + " frames, need " +
                  std::to_string(SHEET_COLS);
            return false;
        }
        for (int c = 0; c < SHEET_COLS; ++c) {
            if (rows.rows[r][c].size() != cell || rows.rows[r][c].type() != CV_8UC4) {
                err = "composeFourDirSheet: row " + std::to_string(r) + " frame " +
                      std::to_string(c) + " is not a BGRA cell of " +
                      std::to_string(cell.width) + "x" + std::to_string(cell.height);
                return false;
            }
        }
    }
    out = cv::Mat::zeros(cell.height * SHEET_ROWS, cell.width * SHEET_COLS, CV_8UC4);
    for (int r = 0; r < SHEET_ROWS; ++r) {
        for (int c = 0; c < SHEET_COLS; ++c) {
            rows.rows[r][c].copyTo(out(cv::Rect(c * cell.width, r * cell.height,
                                                cell.width, cell.height)));
        }
    }
    return true;
}

bool buildFourDirSheet(const cv::Mat& front, const cv::Mat& side, const cv::Mat& back,
                       const ChipSet& chips, FourDirResult& out, std::string& err) {
    out = FourDirResult{};

    // Every flipbook the sheet needs must exist in the chip set.
    // 大图需要的每本翻页书都必须在芯片集里。
    const MotionChips* fbIdle = chips.find("front_idle");
    const MotionChips* fbWalk = chips.find("front_walk");
    const MotionChips* sdIdle = chips.find("side_idle");
    const MotionChips* sdWalk = chips.find("side_walk");
    const MotionChips* bkIdle = chips.find("back_idle");
    const MotionChips* bkWalk = chips.find("back_walk");
    if (!fbIdle || !fbWalk || !sdIdle || !sdWalk || !bkIdle || !bkWalk) {
        err = "buildFourDirSheet: chip set is missing a flipbook "
              "(need front/side/back x idle/walk)";
        return false;
    }

    const cv::Mat* views[3]   = {&front, &side, &back};
    const MotionChips* idles[3] = {fbIdle, sdIdle, bkIdle};
    const MotionChips* walks[3] = {fbWalk, sdWalk, bkWalk};
    const char* viewNames[3]  = {"front", "side", "back"};

    std::vector<cv::Mat> idleFrames[3], walkFrames[3];
    for (int v = 0; v < 3; ++v) {
        if (views[v]->empty()) {
            err = std::string("buildFourDirSheet: ") + viewNames[v] + " view is empty";
            return false;
        }
        // Palette extraction runs on idle frame 0 — it covers every colour the
        // view's chips use (the coverage check below keeps watch).
        // 配色提取对 idle 第 0 帧做 —— 它覆盖该视图芯片用到的全部颜色
        // （下面的覆盖检查负责把关）。
        if (!extractPaletteMap(*views[v], idles[v]->frames[0], out.maps[v], err)) {
            err = std::string(viewNames[v]) + " view: " + err;
            return false;
        }

        idleFrames[v] = renderFromChips(*idles[v], out.maps[v]);
        walkFrames[v] = renderFromChips(*walks[v], out.maps[v]);
    }

    // Coverage invariant: every opaque output pixel must be a map destination.
    // A walk-frame colour that idle frame 0 never showed would survive the
    // palette swap as an original (reference-sheet) colour and be caught here.
    // 覆盖不变量：每个非透明输出像素必须是映射目标色。行走帧若用了 idle 第 0 帧
    // 没出现的颜色，会以原参考色漏过换色 —— 在这里被抓住。
    for (int v = 0; v < 3; ++v) {
        std::set<uint32_t> dst;
        for (const auto& sw : out.maps[v]) dst.insert(colourKey(sw.second));
        for (int pass = 0; pass < 2; ++pass) {
            const std::vector<cv::Mat>& fr = pass ? walkFrames[v] : idleFrames[v];
            for (size_t i = 0; i < fr.size(); ++i) {
                const cv::Mat& f = fr[i];
                for (int y = 0; y < f.rows; ++y) {
                    const cv::Vec4b* row = f.ptr<cv::Vec4b>(y);
                    for (int x = 0; x < f.cols; ++x) {
                        if (row[x][3] == 0) continue;
                        if (!dst.count(colourKey(row[x]))) {
                            std::ostringstream os;
                            os << "buildFourDirSheet: " << viewNames[v] << " "
                               << (pass ? "walk" : "idle") << " f" << i
                               << " pixel (" << x << "," << y << ") colour "
                               << (int)row[x][0] << "," << (int)row[x][1] << ","
                               << (int)row[x][2] << "," << (int)row[x][3]
                               << " is not a palette destination — the view's "
                               << "idle frame 0 did not cover it (add the tone "
                               << "or fix the input / idle f0 未覆盖该色)";
                            err = os.str();
                            return false;
                        }
                    }
                }
            }
        }
    }

    // Assemble rows. Idle is 2 frames cycle-filled to 6 (f0 f1 f0 f1 f0 f1).
    // 组装各行。待机 2 帧循环补到 6（f0 f1 f0 f1 f0 f1）。
    const std::vector<cv::Mat> idleF = repeatToLength(idleFrames[0], SHEET_COLS);
    const std::vector<cv::Mat> walkF = walkFrames[0];
    const std::vector<cv::Mat> idleB = repeatToLength(idleFrames[2], SHEET_COLS);
    const std::vector<cv::Mat> walkB = walkFrames[2];
    const std::vector<cv::Mat> idleS = repeatToLength(idleFrames[1], SHEET_COLS);
    const std::vector<cv::Mat> walkS = walkFrames[1];
    const std::vector<cv::Mat> idleSM = mirrorAll(idleS);
    const std::vector<cv::Mat> walkSM = mirrorAll(walkS);

    // left rows mirror, right rows verbatim (side chips face right).
    // left 行镜像，right 行原样（侧视芯片朝右）。
    const std::vector<cv::Mat>& idleLeft  = kSideChipsFaceRight ? idleSM : idleS;
    const std::vector<cv::Mat>& walkLeft  = kSideChipsFaceRight ? walkSM : walkS;
    const std::vector<cv::Mat>& idleRight = kSideChipsFaceRight ? idleS : idleSM;
    const std::vector<cv::Mat>& walkRight = kSideChipsFaceRight ? walkS : walkSM;

    out.rows.rows[0] = idleF;
    out.rows.rows[1] = walkF;
    out.rows.rows[2] = idleB;
    out.rows.rows[3] = walkB;
    out.rows.rows[4] = idleLeft;
    out.rows.rows[5] = walkLeft;
    out.rows.rows[6] = idleRight;
    out.rows.rows[7] = walkRight;

    return composeFourDirSheet(out.rows, chips.cell, out.sheet, err);
}

}  // namespace fb
