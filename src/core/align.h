// align.h — integer-only input alignment (v0.9.0 alignment diagnostics)
// 纯整数输入对齐（v0.9.0 对齐诊断）
//
// The input contract is "template-aligned 32x32", but real images are shifted
// a pixel or exported at 2x/4x. This module normalises them with integer-only
// transforms — pixel-art rules hold: nearest-neighbour, integer translation,
// never any interpolation.
// 输入契约是"模板对齐 32x32"，但真实图总会偏一两像素，或按 2x/4x 导出。本模块用
// 纯整数变换归一化它们 —— 像素风规则照旧：最近邻、整数平移、永不插值。
#pragma once

#include <opencv2/core.hpp>

namespace fb {

// Alignment parameters. Scale is an INTEGER downsample factor (1 = none):
// output(x,y) samples src((x-dx)*scale, (y-dy)*scale); dx/dy translate the
// content in OUTPUT pixels after scaling. Out-of-range samples are transparent.
// 对齐参数。scale 是整数降采样倍数（1 = 不缩放）：输出 (x,y) 采样
// src((x-dx)*scale, (y-dy)*scale)；dx/dy 是缩放后按输出像素的平移。越界采样透明。
struct AlignParams {
    int scale = 1;
    int dx = 0, dy = 0;
    bool operator==(const AlignParams& o) const {
        return scale == o.scale && dx == o.dx && dy == o.dy;
    }
};

// Resample src onto a target-sized canvas under p. scale==1 && dx==dy==0
// returns an exact copy (byte-identical).
// 按 p 把 src 重采样到 target 尺寸画布。scale==1 且 dx==dy==0 时返回逐字节
// 相同的拷贝。
cv::Mat applyAlign(const cv::Mat& src, const AlignParams& p, cv::Size target);

// Brute-force search: scale in [1,4] x dx,dy in [-8,+8], minimising the
// alpha-mask mismatch against refCell (the view's idle chip frame 0). Tie
// break is deterministic: smaller mismatch -> smaller |dx|+|dy| -> smaller
// scale. An already-aligned input comes back as the identity params with
// mismatch 0. Returns false only on empty/incompatible inputs.
// 暴力搜索：scale 在 [1,4]、dx,dy 在 [-8,+8]，最小化与 refCell（该视图 idle 芯片
// 第 0 帧）的 alpha 掩码不一致数。平局裁决确定性：更小 mismatch -> 更小
// |dx|+|dy| -> 更小 scale。已对齐输入返回恒等参数且 mismatch=0。
// 仅当输入为空/类型不符时返回 false。
bool autoAlign(const cv::Mat& userImg, const cv::Mat& refCell,
               AlignParams& out, int* maskMismatch = nullptr);

}  // namespace fb
