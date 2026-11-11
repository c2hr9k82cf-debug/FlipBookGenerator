// palette.cpp — exact palette extraction: reference cell -> user image
// 精确配色提取：参考格 → 用户图
//
// Strict by contract (the app plan's input alignment): same size, identical
// alpha mask, and every ref colour must map to exactly ONE user colour — the
// relation has to be a function or the reskin is ambiguous. No heuristics, no
// tolerance: pixel art has no blending, a pixel is either the tone or not.
// 契约严格（app 计划的输入对齐）：同尺寸、alpha 掩码一致、每个参考色恰好映射到
// 一个用户色 —— 关系必须是函数，否则换皮有歧义。无启发式、无容差：像素风无混合，
// 像素要么是该色要么不是。

#include "core.h"

#include <map>
#include <set>
#include <sstream>

namespace fb {
namespace {

uint32_t colourKey(const cv::Vec4b& c) {
    return (uint32_t)c[0] | ((uint32_t)c[1] << 8) |
           ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 24);
}

std::string colourStr(const cv::Vec4b& c) {
    std::ostringstream os;
    os << "(B=" << (int)c[0] << ",G=" << (int)c[1]
       << ",R=" << (int)c[2] << ",A=" << (int)c[3] << ")";
    return os.str();
}

// One ref colour's tally of user colours seen at its pixels.
// 一个参考色在其像素处见到的用户色统计。
struct Tally {
    std::map<uint32_t, int> userCounts;   // user colour key -> pixel count
    std::map<uint32_t, cv::Point> firstAt;  // user colour key -> first coordinate
    int pixels = 0;
};

}  // namespace

bool extractPaletteMap(const cv::Mat& userImg, const cv::Mat& refCell,
                       PaletteMap& out, std::string& err, ExtractStats* stats,
                       bool tolerant) {
    out.clear();
    if (stats) *stats = ExtractStats{};

    if (userImg.empty() || refCell.empty()) {
        err = "extractPaletteMap: empty image";
        return false;
    }
    if (userImg.type() != CV_8UC4 || refCell.type() != CV_8UC4) {
        err = "extractPaletteMap: both images must be BGRA (CV_8UC4)";
        return false;
    }
    if (userImg.size() != refCell.size()) {
        std::ostringstream os;
        os << "extractPaletteMap: size mismatch — user " << userImg.cols << "x"
           << userImg.rows << " vs ref " << refCell.cols << "x" << refCell.rows
           << " (input must be template-aligned / 输入必须模板对齐)";
        err = os.str();
        return false;
    }

    std::map<uint32_t, Tally> tally;   // ref colour key -> tally
    int maskDiff = 0, compared = 0;
    cv::Point maskExample(-1, -1);

    for (int y = 0; y < refCell.rows; ++y) {
        const cv::Vec4b* r = refCell.ptr<cv::Vec4b>(y);
        const cv::Vec4b* u = userImg.ptr<cv::Vec4b>(y);
        for (int x = 0; x < refCell.cols; ++x) {
            const bool refOpaque = r[x][3] != 0;
            const bool usrOpaque = u[x][3] != 0;
            if (refOpaque != usrOpaque) {
                ++maskDiff;
                if (maskExample.x < 0) maskExample = cv::Point(x, y);
                continue;
            }
            if (!refOpaque) continue;   // transparent pixels carry no colour / 透明像素无颜色
            ++compared;
            Tally& t = tally[colourKey(r[x])];
            ++t.pixels;
            const uint32_t uk = colourKey(u[x]);
            if (!t.userCounts.count(uk)) t.firstAt[uk] = cv::Point(x, y);
            ++t.userCounts[uk];
        }
    }

    if (maskDiff) {
        if (stats) stats->maskDiffPixels = maskDiff;
        // Tolerant: the mismatching pixels were skipped above; the output shape
        // always comes from the chips, so a few stray silhouette pixels cost
        // nothing but are worth telling the user about (the GUI shows a warning).
        // 宽容：差异像素已在上面跳过；输出形状永远来自芯片，多出的剪影像素无伤
        // 大雅，但值得告知用户（GUI 显示警告）。
        if (!tolerant) {
            std::ostringstream os;
            os << "extractPaletteMap: alpha mask differs at " << maskDiff
               << " pixel(s), first at (" << maskExample.x << "," << maskExample.y
               << ") — silhouette misaligned (input must be template-aligned / "
               << "剪影不对齐，输入必须模板对齐)";
            err = os.str();
            return false;
        }
    }

    // Function check: one ref colour -> exactly one user colour.
    // 函数性检查：一个参考色恰好对应一个用户色。
    std::vector<std::pair<uint32_t, uint32_t>> pairs;   // (ref key, user key), sorted
    for (const auto& kv : tally) {
        const Tally& t = kv.second;
        if (t.userCounts.size() != 1) {
            std::ostringstream os;
            os << "extractPaletteMap: ref colour " << colourStr(cv::Vec4b(
                   (uchar)(kv.first & 0xFF), (uchar)((kv.first >> 8) & 0xFF),
                   (uchar)((kv.first >> 16) & 0xFF), (uchar)((kv.first >> 24) & 0xFF)))
               << " maps to " << t.userCounts.size() << " user colours:";
            for (const auto& uc : t.userCounts) {
                const cv::Vec4b c((uchar)(uc.first & 0xFF), (uchar)((uc.first >> 8) & 0xFF),
                                  (uchar)((uc.first >> 16) & 0xFF), (uchar)((uc.first >> 24) & 0xFF));
                const cv::Point p = t.firstAt.at(uc.first);
                os << " " << colourStr(c) << " x" << uc.second
                   << " (first at " << p.x << "," << p.y << ");";
            }
            os << " — not a function (is the image anti-aliased or misaligned? / "
               << "不是函数（图有抗锯齿或没对齐？）)";
            err = os.str();
            return false;
        }
        pairs.push_back({kv.first, t.userCounts.begin()->first});
    }

    out.reserve(pairs.size());
    for (const auto& p : pairs) {
        out.push_back({cv::Vec4b((uchar)(p.first & 0xFF), (uchar)((p.first >> 8) & 0xFF),
                                 (uchar)((p.first >> 16) & 0xFF), (uchar)((p.first >> 24) & 0xFF)),
                       cv::Vec4b((uchar)(p.second & 0xFF), (uchar)((p.second >> 8) & 0xFF),
                                 (uchar)((p.second >> 16) & 0xFF), (uchar)((p.second >> 24) & 0xFF))});
    }

    if (stats) {
        stats->comparedPixels = compared;
        stats->mappedColours  = (int)out.size();
        stats->maskDiffPixels = maskDiff;   // 0 in strict mode / 严格模式下为 0
    }
    return true;
}

}  // namespace fb
