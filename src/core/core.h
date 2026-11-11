// flipbook_core — animation engine, UI-free
// 动画核心引擎，无 UI
//
// ARCHITECTURAL RULE / 架构铁律:
//   This directory must NEVER include a UI header, and must never call
//   cv::imshow. Everything here is testable headlessly. See CLAUDE.md.
//   本目录永不 include UI 头文件，永不调用 cv::imshow。这里的一切都可无头测试。
//
// Types depend only on OpenCV core types (cv::Mat, cv::Rect, cv::Point2f).
// 类型只依赖 OpenCV 核心类型。

#pragma once

#include <opencv2/core.hpp>
#include <array>
#include <string>
#include <vector>

namespace fb {

// Number of body parts (head, body, arm_left, arm_right, leg_left, leg_right)
// 身体部件数量
constexpr int PART_COUNT = 6;

// Part ids are 1-based to match utils.cpp / the on-disk data formats.
// 部件 id 从 1 开始，与 utils.cpp 及磁盘数据格式一致
constexpr int PART_HEAD      = 1;
constexpr int PART_BODY      = 2;
constexpr int PART_ARM_LEFT  = 3;
constexpr int PART_ARM_RIGHT = 4;
constexpr int PART_LEG_LEFT  = 5;
constexpr int PART_LEG_RIGHT = 6;

// ---------------------------------------------------------------------------
// Rig — per-view static data / 每视图的静态数据
// ---------------------------------------------------------------------------

// One rig per view (front / side / back). The animation templates are
// view-independent; they are applied to whichever rig you hand them.
// 一个视图一套骨架。动画模板与视图无关，套到哪个骨架都行。
struct Rig {
    cv::Mat source;                                  // original BGRA / 原图
    std::string sourcePath;                          // for reporting / 便于报错
    cv::Size size;                                   // source dimensions / 原图尺寸

    // Indexed by (partId - 1). Empty Mat / zero Rect if the part is absent.
    // 以下标 (partId - 1) 索引。部件缺失时为零值。
    std::array<cv::Mat,  PART_COUNT>   parts;        // BGRA crops / 裁出的部件
    std::array<cv::Rect, PART_COUNT>   bbox;         // placement, source coords / 原图坐标
    std::array<cv::Point2f, PART_COUNT> pivots;      // rotation centres, source coords
    std::array<bool, PART_COUNT>       present;

    // Draw order: later entries paint over earlier ones. Defaults to
    // legs -> body -> arms -> head so limbs sit behind the torso.
    // 绘制顺序：靠后的覆盖靠前的。默认 腿→躯干→手臂→头，让四肢在躯干后面。
    std::array<int, PART_COUNT> zOrder;

    int presentCount() const;
};

// Load the part bundle written by runExtractPartsMode() from a directory.
// Reads {name}.png, {name}_bbox.txt ("x y w h"), anchors.txt ("partId x y"),
// image_path.txt. Returns false and fills `err` on failure.
// 从目录读取 runExtractPartsMode() 写出的部件包。失败返回 false 并填 err。
bool loadRigFromDir(const std::string& dir, Rig& out, std::string& err);

// Write the same bundle (used by the GUI later; the extractor has its own copy
// of this logic until Phase 3 retires it).
// 写出同样的部件包（供 GUI 使用；Phase 3 之前 extractor 保留自己那份逻辑）。
bool saveRigToDir(const Rig& rig, const std::string& dir, std::string& err);

// ---------------------------------------------------------------------------
// Pose / transform / animation / 姿势、变换、动画
// ---------------------------------------------------------------------------

// A part's transform RELATIVE TO ITS REST POSE (identity = the pose in the
// source image). Rotation is about the part's pivot.
// 部件相对静止姿势的变换（单位变换 = 原图里的姿势）。旋转绕该部件的 pivot。
struct PartTransform {
    float rotateDeg = 0.f;   // about pivot / 绕 pivot
    float moveX     = 0.f;   // pixels / 像素
    float moveY     = 0.f;
    float scaleY    = 1.f;   // about pivot, Y only / 绕 pivot，仅 Y
};

// One frame's pose. rootX/rootY move the whole character (walk bob lives here).
// 一帧的姿势。rootX/rootY 移动整个角色（走路的起伏在这里）。
//
// NOTE / 注意: +Y is DOWN (screen convention). Negative rootY lifts the character.
// +Y 向下（屏幕惯例）。rootY 为负表示抬起角色。
struct Pose {
    std::array<PartTransform, PART_COUNT> parts;
    float rootX = 0.f;
    float rootY = 0.f;

    // Per-frame visibility. hidden[i] = do not draw part (i+1) this frame —
    // used for the far arm the torso occludes on some walk frames.
    // 逐帧可见性。hidden[i] = 本帧不画第 (i+1) 个部件 —— 用于某些行走帧里被躯干
    // 遮挡的远端手臂。
    std::array<bool, PART_COUNT> hidden{};

    // Per-frame palette swap: (from -> to) BGRA pairs, applied once after
    // rasterising and before the seam repairs. Drives the pants two-tone swap;
    // two tones trading places is just two entries that are each other's
    // inverse. Keeps the leg shapes untouched — only colour changes.
    // 逐帧调色板换色：(from -> to) BGRA 对，光栅化后、缝修补前应用一次。
    // 驱动裤子两主色换色；两个色互换就是两条互为反向的记录。腿的形状完全不动，
    // 只换颜色。
    std::vector<std::pair<cv::Vec4b, cv::Vec4b>> swaps;
};

// Reserved slot for hand-drawn per-frame chips (Tier B). Hip (y18-22) and boots
// (y26-31) are hand-redrawn per frame on the reference sheet; rules approximate
// them now, and a chip here can replace that slot's rule approximation later.
// 为逐帧手绘芯片（Tier B）预留的槽位。参考图里髋部（y18-22）和靴（y26-31）逐帧
// 手绘；本轮用规则近似，日后此芯片可替换该槽位的规则近似。
struct VariantChip {
    enum class Slot { Hip, Boot };
    Slot             slot   = Slot::Hip;    // which region / 哪个区域
    int              bindTo = PART_BODY;    // part id it rides on / 跟随的部件 id
    std::vector<cv::Mat> frames;            // one BGRA chip per frame / 每帧一张 BGRA 芯片
};

struct Keyframe {
    int  frame = 0;
    Pose pose;
};

struct Animation {
    std::string name;
    // Defaults are a fallback only: every preset overwrites both with the
    // reference-sheet counts — idle 2 frames @ 4fps (one breath per second),
    // walk 6 frames @ 10fps (a 600ms stride). Do not "improve" these numbers;
    // the reference sheet is the absolute template.
    // 默认值只是兜底：每个预设都会用参考图的帧数覆盖 —— idle 2 帧 @ 4fps
    // （一秒呼吸一次），walk 6 帧 @ 10fps（一步 600ms）。不要"优化"这些数字，
    // 参考图是绝对模板。
    int  frameCount = 8;
    int  fps        = 8;
    bool loop       = true;
    std::vector<Keyframe> keyframes;   // sorted by frame / 按 frame 排序

    // Interpolated pose at a frame index in [0, frameCount).
    // 取 [0, frameCount) 内某帧的插值姿势。
    //
    // With loop=true the segment from the last keyframe back to the first wraps
    // past frameCount, so a cycle needs no duplicated keyframe at the end.
    // loop=true 时末关键帧到首关键帧的段绕过 frameCount 衔接，循环动画不必在末尾重复关键帧。
    Pose poseAt(int frame) const;
};

// Shortest-arc angle interpolation. Solves the +-180 wrap without quaternions:
//   d = fmod(b - a + 540, 360) - 180
// 最短弧角度插值。解决 +-180 绕远路，不需要四元数。
float lerpAngleDeg(float a, float b, float t);

// ---------------------------------------------------------------------------
// Presets / 预设
// ---------------------------------------------------------------------------
// These are DATA GENERATORS that fill the same Animation struct an editor
// would — not separate code paths. That is why idle and walk can ship before
// any GUI editor exists.
// 这些是填充同一个 Animation 结构的【数据生成器】，不是独立代码路径。
// 所以待机和走路能在任何 GUI 编辑器存在之前先跑出来。

// Gentle vertical bob. 2 frames @ 4fps = one breath per second, exactly as
// the reference sheet draws it (rows 0/2/4 hold 2 cells each).
// 轻微上下起伏。2 帧 @ 4fps = 一秒呼吸一次，与参考图画法一致（row 0/2/4 各 2 格）。
Animation makeIdle(const Rig& rig, int frameCount = 2);

// Side view walk: legs swing about the hips, arms counter-swing, body bobs.
// 6 frames @ 10fps = a 600ms stride, exactly as the reference sheet draws it.
// 侧面走路：腿绕胯摆动，手臂反向摆，身体起伏。6 帧 @ 10fps = 一步 600ms，
// 与参考图画法一致。
Animation makeWalkSide(const Rig& rig, int frameCount = 6);

// Front/back walk: legs read as foreshortening, so in-plane rotation looks
// wrong — lift them by scaleY instead. See CLAUDE.md.
// 正面/背面走路：腿是遮挡关系，平面内旋转会怪 —— 改用 scaleY 抬腿。见 CLAUDE.md。
Animation makeWalkFront(const Rig& rig, int frameCount = 6);
Animation makeWalkUp(const Rig& rig, int frameCount = 6);

// Scale all motion by a factor (used by the GUI sliders). 1.0 = preset default.
// 按系数缩放全部动作（供 GUI 滑杆）。1.0 = 预设默认值。
struct PresetParams {
    float motionScale = 1.0f;    // rotation + translation amplitude
    float bobScale    = 1.0f;    // rootY bob only
    float breathScale = 1.0f;    // scaleY squash only
};
Animation makeWalkSide(const Rig& rig, int frameCount, const PresetParams& p);
Animation makeWalkFront(const Rig& rig, int frameCount, const PresetParams& p);
Animation makeWalkUp(const Rig& rig, int frameCount, const PresetParams& p);
Animation makeIdle(const Rig& rig, int frameCount, const PresetParams& p);

// ---------------------------------------------------------------------------
// Rendering / 渲染
// ---------------------------------------------------------------------------

// Compose one frame at the rig's source size. Parts are rasterised in zOrder
// with inverse mapping + nearest sampling (forward sampling punches holes).
// Translations are rounded to the integer pixel grid — this IS the pixel-
// perfect mode; there is no sub-pixel option. No anti-aliasing, no blending at
// overlaps. See CLAUDE.md "Pixel-art transform rules".
// 按 zOrder 合成一帧，尺寸同原图。逆向映射 + 最近邻（正向采样会打洞）。
// 平移取整到像素网格 —— 这就是像素完美模式，没有亚像素选项。
// 无抗锯齿，重叠处不混合。见 CLAUDE.md「像素风变换硬约束」。
cv::Mat composeFrame(const Rig& rig, const Pose& pose);

// Reassemble the parts at their bbox offsets with no motion and compare
// against the source. Returns the number of differing pixels; `alphaOnlyDiff`
// counts pixels differing only in RGB where the source alpha is 0 (benign —
// a fully transparent pixel's colour is meaningless).
// 将部件按 bbox 偏移无运动贴回，与源图比对。返回差异像素数；
// `alphaOnlyDiff` 统计仅在源 alpha=0 处 RGB 不同的像素（良性，全透明像素的颜色无意义）。
struct ReconResult {
    int totalPixels     = 0;
    int diffPixels      = 0;
    int alphaOnlyDiff   = 0;
    bool passed() const { return diffPixels == alphaOnlyDiff; }
};
ReconResult reconstructAndCompare(const Rig& rig, cv::Mat* outRecon = nullptr);

// ---------------------------------------------------------------------------
// Export / 导出
// ---------------------------------------------------------------------------

// Render every frame of an animation.
// 渲染动画的每一帧。
std::vector<cv::Mat> renderAnimation(const Rig& rig, const Animation& anim);

// Write frames as frame_0.png, frame_1.png, ... and a horizontal spritesheet.
// All frames must be the same size. Returns false and fills `err` on failure.
// 写出 frame_0.png、frame_1.png… 及横向精灵图。各帧尺寸必须相同。
bool exportAnimation(const std::vector<cv::Mat>& frames,
                     const std::string& outDir,
                     const std::string& baseName,
                     std::string& err,
                     std::string* sheetPath = nullptr);

// Mirror a rendered frame horizontally — how the left-facing direction is
// derived from the right-facing one. Exact and free for pixel art.
// 水平镜像一帧 —— 由向右的方向推出向左。对像素图精确且免费。
cv::Mat mirrorFrame(const cv::Mat& frame);

}  // namespace fb
