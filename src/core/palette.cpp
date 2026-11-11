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

// One ref colour's tally of user colours seen at its pixels. Full coordinate
// lists so the conflict mask can mark every minority pixel (32x32 = 1024 px
// max, memory is free).
// 一个参考色在其像素处见到的用户色统计。记全坐标表，冲突掩码才能标出每个少数派
// 像素（32x32 最多 1024 px，内存无成本）。
struct Tally {
    std::map<uint32_t, std::vector<cv::Point>> at;   // user colour key -> coordinates
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

    // Diff masks are collected even on the failure paths below — a failure is
    // exactly when the user needs the visual diagnosis (v0.9.0 alignment tool).
    // 差异掩码在下面的失败路径也照收集 —— 失败恰恰是用户最需要可视化诊断的时候
    // （v0.9.0 对齐工具）。
    if (stats) {
        stats->maskDiffMask       = cv::Mat::zeros(refCell.size(), CV_8U);
        stats->colourConflictMask = cv::Mat::zeros(refCell.size(), CV_8U);
    }

    for (int y = 0; y < refCell.rows; ++y) {
        const cv::Vec4b* r = refCell.ptr<cv::Vec4b>(y);
        const cv::Vec4b* u = userImg.ptr<cv::Vec4b>(y);
        for (int x = 0; x < refCell.cols; ++x) {
            const bool refOpaque = r[x][3] != 0;
            const bool usrOpaque = u[x][3] != 0;
            if (refOpaque != usrOpaque) {
                ++maskDiff;
                if (maskExample.x < 0) maskExample = cv::Point(x, y);
                if (stats) stats->maskDiffMask.at<uchar>(y, x) = 255;
                continue;
            }
            if (!refOpaque) continue;   // transparent pixels carry no colour / 透明像素无颜色
            ++compared;
            Tally& t = tally[colourKey(r[x])];
            ++t.pixels;
            t.at[colourKey(u[x])].push_back(cv::Point(x, y));
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

    // Function check: one ref colour -> exactly one user colour. All conflicts
    // across ALL ref colours are collected into colourConflictMask before the
    // early return, so the diagnosis is complete even on failure.
    // 函数性检查：一个参考色恰好对应一个用户色。所有参考色的冲突先全部收进
    // colourConflictMask 再提前返回 —— 失败时诊断也是完整的。
    std::vector<std::pair<uint32_t, uint32_t>> pairs;   // (ref key, user key), sorted
    std::ostringstream conflictErr;
    bool anyConflict = false;
    for (const auto& kv : tally) {
        const Tally& t = kv.second;
        if (t.at.size() != 1) {
            // Majority user colour = largest count (ties: smaller key — the map
            // order is deterministic). Everything else is the minority.
            // 多数派用户色 = 计数最大者（平局取较小 key —— map 顺序确定性）。
            // 其余都是少数派。
            uint32_t majority = 0;
            size_t bestCount = 0;
            for (const auto& uc : t.at) {
                if (uc.second.size() > bestCount) {
                    bestCount = uc.second.size();
                    majority = uc.first;
                }
            }
            if (stats) {
                for (const auto& uc : t.at) {
                    if (uc.first == majority) continue;
                    for (const cv::Point& p : uc.second) {
                        stats->colourConflictMask.at<uchar>(p.y, p.x) = 255;
                        ++stats->colourConflictPixels;
                    }
                }
            }
            if (!anyConflict) {
                anyConflict = true;
                conflictErr << "extractPaletteMap: ref colour " << colourStr(cv::Vec4b(
                        (uchar)(kv.first & 0xFF), (uchar)((kv.first >> 8) & 0xFF),
                        (uchar)((kv.first >> 16) & 0xFF), (uchar)((kv.first >> 24) & 0xFF)))
                    << " maps to " << t.at.size() << " user colours:";
                for (const auto& uc : t.at) {
                    const cv::Vec4b c((uchar)(uc.first & 0xFF), (uchar)((uc.first >> 8) & 0xFF),
                                      (uchar)((uc.first >> 16) & 0xFF), (uchar)((uc.first >> 24) & 0xFF));
                    const cv::Point p = uc.second.front();
                    conflictErr << " " << colourStr(c) << " x" << uc.second.size()
                                << " (first at " << p.x << "," << p.y << ");";
                }
                conflictErr << " — not a function (is the image anti-aliased or misaligned? / "
                            << "不是函数（图有抗锯齿或没对齐？）)";
            }
            continue;
        }
        pairs.push_back({kv.first, t.at.begin()->first});
    }
    if (anyConflict) {
        if (stats) {
            stats->comparedPixels = compared;
            stats->mappedColours  = (int)pairs.size();
            stats->maskDiffPixels = maskDiff;
        }
        err = conflictErr.str();
        return false;
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

cv::Mat renderDiffOverlay(const cv::Mat& userAligned,
                          const cv::Mat& maskDiffMask,
                          const cv::Mat& colourConflictMask) {
    cv::Mat out;
    if (maskDiffMask.empty() && colourConflictMask.empty()) return out;
    const cv::Size sz = !maskDiffMask.empty() ? maskDiffMask.size()
                                              : colourConflictMask.size();
    if (!userAligned.empty() && userAligned.size() != sz) return out;   // defensive / 防御
    out = cv::Mat::zeros(sz, CV_8UC4);   // fully transparent base / 全透明底

    // Orange first (colour conflict), magenta on top (silhouette diff reads
    // first when both mark the same pixel).
    // 橙先画（配色冲突），品红盖上（同像素两种差异时以剪影差为主）。
    if (!colourConflictMask.empty()) {
        for (int y = 0; y < sz.height; ++y) {
            const uchar* m = colourConflictMask.ptr<uchar>(y);
            cv::Vec4b* o = out.ptr<cv::Vec4b>(y);
            for (int x = 0; x < sz.width; ++x)
                if (m[x]) o[x] = cv::Vec4b(0, 165, 255, 255);   // orange / 橙
        }
    }
    if (!maskDiffMask.empty()) {
        for (int y = 0; y < sz.height; ++y) {
            const uchar* m = maskDiffMask.ptr<uchar>(y);
            cv::Vec4b* o = out.ptr<cv::Vec4b>(y);
            for (int x = 0; x < sz.width; ++x)
                if (m[x]) o[x] = cv::Vec4b(255, 0, 255, 255);   // magenta / 品红
        }
    }
    return out;
}

}  // namespace fb
