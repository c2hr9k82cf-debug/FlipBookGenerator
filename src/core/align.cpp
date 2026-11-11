// align.cpp — integer-only input alignment implementation
// 纯整数输入对齐实现

#include "align.h"

namespace fb {

namespace {

// Alpha-mask XOR mismatch between src (sampled under p) and ref. Nearest
// sampling only — no allocation, no interpolation.
// src（按 p 采样）与 ref 的 alpha 掩码 XOR 不一致数。只用最近采样 —— 无分配、
// 无插值。
int maskMismatch(const cv::Mat& src, const cv::Mat& ref,
                 int scale, int dx, int dy) {
    int bad = 0;
    for (int y = 0; y < ref.rows; ++y) {
        const int sy = (y - dy) * scale;
        const bool syIn = sy >= 0 && sy < src.rows;
        const cv::Vec4b* r = ref.ptr<cv::Vec4b>(y);
        for (int x = 0; x < ref.cols; ++x) {
            const int sx = (x - dx) * scale;
            const bool refOp = r[x][3] != 0;
            bool srcOp = false;
            if (syIn && sx >= 0 && sx < src.cols)
                srcOp = src.ptr<cv::Vec4b>(sy)[sx][3] != 0;
            if (srcOp != refOp) ++bad;
        }
    }
    return bad;
}

}  // namespace

cv::Mat applyAlign(const cv::Mat& src, const AlignParams& p, cv::Size target) {
    if (src.empty() || target.width <= 0 || target.height <= 0) return cv::Mat();
    if (p.scale == 1 && p.dx == 0 && p.dy == 0 && src.size() == target)
        return src.clone();   // byte-exact identity / 逐字节恒等

    cv::Mat out = cv::Mat::zeros(target, CV_8UC4);
    const int s = p.scale < 1 ? 1 : p.scale;
    for (int y = 0; y < target.height; ++y) {
        const int sy = (y - p.dy) * s;
        if (sy < 0 || sy >= src.rows) continue;
        const cv::Vec4b* sp = src.ptr<cv::Vec4b>(sy);
        cv::Vec4b* op = out.ptr<cv::Vec4b>(y);
        for (int x = 0; x < target.width; ++x) {
            const int sx = (x - p.dx) * s;
            if (sx < 0 || sx >= src.cols) continue;
            op[x] = sp[sx];   // nearest, no blend / 最近邻，不混合
        }
    }
    return out;
}

bool autoAlign(const cv::Mat& userImg, const cv::Mat& refCell,
               AlignParams& out, int* maskMismatchOut) {
    if (userImg.empty() || refCell.empty()) return false;
    if (userImg.type() != CV_8UC4 || refCell.type() != CV_8UC4) return false;

    // Deterministic search with a lexicographic tie-break:
    // (mismatch, |dx|+|dy|, scale) — smallest wins, first candidate kept on
    // a full tie. An aligned input yields (0, 0, 1).
    // 确定性搜索，字典序平局裁决：(mismatch, |dx|+|dy|, scale) —— 小者胜，完全
    // 平局保留先搜到的。已对齐输入给出 (0, 0, 1)。
    int bestMis = 1 << 30, bestCost = 1 << 30, bestScale = 1 << 30;
    AlignParams best;
    for (int scale = 1; scale <= 4; ++scale) {
        for (int dx = -8; dx <= 8; ++dx) {
            for (int dy = -8; dy <= 8; ++dy) {
                const int mis = maskMismatch(userImg, refCell, scale, dx, dy);
                const int cost = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                if (mis < bestMis ||
                    (mis == bestMis && cost < bestCost) ||
                    (mis == bestMis && cost == bestCost && scale < bestScale)) {
                    bestMis = mis;
                    bestCost = cost;
                    bestScale = scale;
                    best = AlignParams{scale, dx, dy};
                }
            }
        }
    }
    out = best;
    if (maskMismatchOut) *maskMismatchOut = bestMis;
    return true;
}

}  // namespace fb
