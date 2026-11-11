// backinfer.h — infer a back view from front + side (v0.9.2).
// 从正面 + 侧面推断背面（v0.9.2）。
//
// Used when a part (or a whole character) has no designed back: item.json says
// back:"auto" and the compose path calls this instead of reading back.png.
// The ANIMATION_SPEC §7 design rule is "back hair = horizontal mirror of the
// front hair", and the drawn reference proves the rest of the body is carried
// over with role flattening (no buttons, no collar detail on the back).
// 当部件（或整角色）没有设计背面时使用：item.json 为 back:"auto"，合成路径
// 调用本函数而非读 back.png。ANIMATION_SPEC §7 的设计规则是"背面头发 = 正面
// 头发水平翻转"，手绘参考证明身体其余部分照搬并做角色压平（背面无扣子、无领饰）。
//
// Known deviation from the hand-drawn back (recorded, not hidden): the drawn
// back hair is its own artwork — a solid rear mass with decorative bands —
// larger than a plain mirror. The mirror rule is what keeps a CUSTOM hairstyle
// turning around consistently; it is not a pixel replica of the reference.
// 与手绘背面的已知偏差（如实记录）：手绘背面头发是独立创作（实心后脑 + 装饰
// 发带），比单纯镜像更大。镜像规则保证自定义发型转身连贯；它不是参考的逐像素
// 复刻。
#pragma once

#include "core/template_spec.h"

#include <opencv2/core.hpp>

namespace fb {

// Infer the back view. Pure and deterministic. `side` is accepted for future
// shoulder-width correction (v1 does not use it). Fails when `front` is empty
// or not the template canvas size.
// 推断背面。纯函数、确定性。side 为将来肩宽校正预留（v1 不使用）。
// front 为空或尺寸非模板画布时报错。
cv::Mat inferBackView(const cv::Mat& front, const cv::Mat& side,
                      const TemplateSpec& spec, std::string& err);

}  // namespace fb
