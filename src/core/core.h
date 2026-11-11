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
// Image I/O / 图像读写
// ---------------------------------------------------------------------------
// One place for the 3->4 channel rule: everything inside the core is BGRA
// (CV_8UC4). Reads keep alpha (IMREAD_UNCHANGED).
// 3→4 通道规则收拢一处：core 内部一律 BGRA（CV_8UC4）。读取保留 alpha（IMREAD_UNCHANGED）。

// Load an image as BGRA. 3-channel sources are promoted; 2/1-channel fail.
// 以 BGRA 读图。3 通道源提升为 4 通道；2/1 通道失败。
bool loadImageBgra(const std::string& path, cv::Mat& out);

// Save a BGRA image (PNG keeps alpha). Fills `err` on failure.
// 保存 BGRA 图像（PNG 保留 alpha）。失败填 err。
bool saveImageBgra(const std::string& path, const cv::Mat& img, std::string& err);

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

// ---------------------------------------------------------------------------
// Tier B: per-frame hand-drawn chip playback / Tier B 逐帧手绘芯片回放
// ---------------------------------------------------------------------------
// Verbatim per-frame sprites cropped from the reference sheet. Each frame is a
// FULL-SIZE BGRA chip — the artist's hand-drawn position, the bob, the two-tone
// leg swap, the side-view far-arm on f2/f3 are all baked into the pixels.
// Playback is a plain overwrite (no transform), so it is pixel-exact by
// construction. Reskin is a pure palette swap. This is the CLAUDE.md "Tier B
// (chip playback)" path, kept independent of the Tier A rig/PartTransform
// pipeline so a procedural preset and a chip flipbook never interfere.
// 从参考图逐帧裁出的手绘精灵。每帧是整幅 BGRA 芯片 —— 画师画的位置、起伏、
// 两棕换色、侧视 f2/f3 远臂全烘在像素里。回放是直接覆盖（无变换），构造上逐像素
// 精确。换皮是纯调色板替换。此即 CLAUDE.md「Tier B（芯片回放）」通道，与 Tier A
// 的 rig/PartTransform 管线彼此独立，程序化预设与芯片翻页书互不干扰。

// One flipbook's per-frame chips (e.g. "front_walk", 6 frames @ 10fps). Each
// entry of `frames` is a full-cell BGRA sprite. `sheetRow` records the source
// row in the reference sheet so a fidelity gate can look the cell back up.
// 一本翻页书的逐帧芯片（如 "front_walk"，6 帧 @ 10fps）。frames 每项是整格
// BGRA 精灵。sheetRow 记下参考图里的源行，供保真闸门回查该格。
struct MotionChips {
    std::string name;
    int  frameCount = 0;
    int  fps        = 0;
    int  sheetRow   = -1;
    std::vector<cv::Mat> frames;   // [frameCount] CV_8UC4, each cell-sized
};

// A whole chip bundle: every flipbook in one motion_chips/ directory.
// 一整套芯片包：一个 motion_chips/ 目录里的全部翻页书。
struct ChipSet {
    std::string sourcePath;        // reference sheet, for reporting / 参考图，便于报错
    cv::Size cell{0, 0};           // 32x32 / 每格尺寸
    std::vector<MotionChips> flipbooks;
    const MotionChips* find(const std::string& name) const;
};

// Palette map (exact BGRA pairs) for reskinning a chip verbatim. An identity map
// keeps the original colours; an empty map is a no-op.
// 换皮调色板映射（精确 BGRA 对），逐帧照搬地换色。恒等映射保原色；空映射不动。
using PaletteMap = std::vector<std::pair<cv::Vec4b, cv::Vec4b>>;

// Apply a palette map in place. Exact BGRA match — pixel art has no blending, so
// a pixel is either the tone or it is not. Shared by the Tier A seam path and
// the Tier B reskin.
// 就地应用调色板映射。精确 BGRA 匹配 —— 像素风无混合，像素要么是该色要么不是。
// Tier A 的补缝与 Tier B 的换皮共用它。
void applyPaletteMap(cv::Mat& img, const PaletteMap& map);

// Load a chip bundle from a motion_chips/ directory (index.txt + chips/*.png).
// 从 motion_chips/ 目录读取芯片包（index.txt + chips/*.png）。
bool loadChipSet(const std::string& dir, ChipSet& out, std::string& err);

// Load a palette map from palette_map.txt ("sB,sG,sR,sA dB,dG,dR,dA" per line).
// 从 palette_map.txt 读取换皮映射（每行一对 BGRA）。
bool loadPaletteMap(const std::string& path, PaletteMap& out, std::string& err);

// Render one flipbook's frames with a palette map applied (identity = verbatim).
// 用调色板映射渲染一本翻页书的各帧（恒等 = 照搬）。
std::vector<cv::Mat> renderFromChips(const MotionChips& chips, const PaletteMap& map);

// ---------------------------------------------------------------------------
// Palette extraction — reskin from a user image / 配色提取 —— 从用户图换皮
// ---------------------------------------------------------------------------

// Diagnostics from extractPaletteMap. The two masks are 8-bit, refCell-sized,
// 255 = the pixel is a diff; filled even when extraction FAILS (a failure is
// exactly when the user most needs the visual diagnosis). Empty when stats
// were not requested or nothing was compared.
// extractPaletteMap 的诊断数据。两张掩码为 8 位、与 refCell 同尺寸，255 = 该像素
// 是差异；**提取失败时也照填**（失败恰恰是用户最需要可视化诊断的时候）。未请求
// stats 或无可比像素时为空。
struct ExtractStats {
    int comparedPixels = 0;   // non-transparent ref pixels compared / 比对过的参考非透明像素
    int mappedColours  = 0;   // distinct ref colours mapped / 映射出的参考色数
    int maskDiffPixels = 0;   // alpha-mask mismatches / alpha 掩码不一致像素数
    cv::Mat maskDiffMask;       // CV_8U; 255 = alpha-mask mismatch pixel / 255=alpha 掩码不一致像素
    cv::Mat colourConflictMask; // CV_8U; 255 = minority pixel of a conflicted ref colour / 255=函数性冲突的少数派像素
    int colourConflictPixels = 0;   // set bits in colourConflictMask / colourConflictMask 的置位数
};

// Transparent BGRA overlay marking the diff pixels on top of a (typically
// aligned) user image: magenta = silhouette mismatch, orange = colour
// conflict. Marks only — no grey base — so the GUI can composite it straight
// over the slot thumbnail. Pure core function, headless-testable; the UI only
// uploads the result as a texture.
// 透明 BGRA 叠加图，在（通常是已对齐的）用户图上标出差异像素：品红 = 剪影不一致，
// 橙 = 配色冲突。只有标记、无灰底 —— GUI 可直接叠在槽缩略图上。纯 core 函数，
// 可无头测试；UI 只负责把结果当纹理上传。
cv::Mat renderDiffOverlay(const cv::Mat& userAligned,
                          const cv::Mat& maskDiffMask,
                          const cv::Mat& colourConflictMask);

// Extract an exact BGRA palette map (refCell -> userImg) by pixel diff. Strict:
// same size, identical alpha mask, and every ref colour must map to exactly ONE
// user colour (the relation must be a function). On failure `err` names the
// conflicting colours with counts and an example coordinate. Per the app plan,
// extraction runs on idle frame 0 only — it covers every colour the view's
// chips use; the fourdir gate watches for regressions.
// 用逐像素差分提取精确 BGRA 映射（参考格 → 用户图）。严格：同尺寸、alpha 掩码
// 一致、每个参考色恰好映射到一个用户色（关系必须是函数）。失败时 err 列出冲突色、
// 计数与示例坐标。按 app 计划：提取只对 idle 第 0 帧做 —— 它覆盖该视图芯片用到的
// 全部颜色；fourdir 闸门负责常驻盯着回归。
//
// tolerant=true downgrades SILHOUETTE mismatch to a warning (stats->maskDiffPixels
// counts it; mismatching pixels are simply skipped, and the output shape always
// comes from the chips anyway). Function violations still fail — those make the
// reskin ambiguous. The GUI offers this as "warn, don't reject"; gates stay strict.
// tolerant=true 把**剪影**不一致降级为警告（stats->maskDiffPixels 计数；差异像素
// 直接跳过 —— 反正输出形状永远来自芯片）。函数性冲突仍然失败 —— 那会让换皮有歧义。
// GUI 用它实现"提醒而不拒绝"；闸门保持严格。
bool extractPaletteMap(const cv::Mat& userImg, const cv::Mat& refCell,
                       PaletteMap& out, std::string& err,
                       ExtractStats* stats = nullptr, bool tolerant = false);

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

// ---------------------------------------------------------------------------
// Four-direction sheet — the .app's product / 四方向拼接大图 —— .app 的产品
// ---------------------------------------------------------------------------
// 8 rows x 6 cols of cell-sized frames. Row order (top -> bottom):
//   down_idle, down_walk, up_idle, up_walk,
//   left_idle, left_walk, right_idle, right_walk
// down = front, up = back. Right/left come from the side chips (which face
// RIGHT — eyeballed from side_idle_0.png): right rows use them verbatim, left
// rows mirror them. Idle is 2 frames cycle-filled to 6 (f0 f1 f0 f1 f0 f1).
// 8 行 x 6 列，每格为 cell 尺寸。行序（上→下）：
//   down_idle, down_walk, up_idle, up_walk,
//   left_idle, left_walk, right_idle, right_walk
// down = front，up = back。左右出自侧视芯片（芯片本身朝右 —— 目检 side_idle_0.png
// 确认）：right 行直接用，left 行水平镜像。待机 2 帧循环补到 6（f0 f1 f0 f1 f0 f1）。
constexpr int SHEET_COLS = 6;
constexpr int SHEET_ROWS = 8;

// The 8 assembled rows, each exactly SHEET_COLS frames. / 组装好的 8 行，每行恰好 6 帧。
struct FourDirRows {
    std::array<std::vector<cv::Mat>, SHEET_ROWS> rows;
};

// Cycle-repeat (f0 f1 f0 f1 ...) or truncate `frames` to exactly `cols`.
// 循环重复（f0 f1 f0 f1 …）或截断到恰好 cols 帧。
std::vector<cv::Mat> repeatToLength(const std::vector<cv::Mat>& frames, int cols);

// Tile the 8x6 rows into one sheet image (cell.width*6 x cell.height*8).
// 把 8x6 各行平铺成一张大图（cell.width*6 x cell.height*8）。
bool composeFourDirSheet(const FourDirRows& rows, cv::Size cell,
                         cv::Mat& out, std::string& err);

// The full app pipeline: extract palettes from three template-aligned user
// views, reskin the chips, assemble the rows, compose the sheet. Enforces the
// coverage invariant — every opaque output pixel must be a map destination,
// which catches a walk-frame colour that idle frame 0 never showed.
// app 完整管线：从三张模板对齐的用户视图提取配色、给芯片换皮、组装各行、拼成
// 大图。执行覆盖不变量 —— 每个非透明输出像素必须是映射目标色，抓"行走帧有
// idle 第 0 帧没出现过的颜色"这类回归。
struct FourDirResult {
    cv::Mat sheet;                     // the big PNG / 最终大图
    FourDirRows rows;                  // assembled rows (for tests/preview) / 组装的行
    std::array<PaletteMap, 3> maps;    // extracted maps: front, side, back / 提取的映射
};
// stats[3] (optional) receives per-view extraction diagnostics; tolerantMask
// passes through to extractPaletteMap (warn, don't reject — the GUI's mode).
// stats[3]（可选）接收各视图的提取诊断；tolerantMask 透传给 extractPaletteMap
// （提醒而不拒绝 —— GUI 的模式）。
bool buildFourDirSheet(const cv::Mat& front, const cv::Mat& side, const cv::Mat& back,
                       const ChipSet& chips, FourDirResult& out, std::string& err,
                       ExtractStats stats[3] = nullptr, bool tolerantMask = false);

}  // namespace fb
