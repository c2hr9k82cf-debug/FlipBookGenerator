// animation.cpp — keyframe interpolation and preset generators
// 关键帧插值与预设生成器

#include "core.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

namespace fb {
namespace {

PartTransform lerpTransform(const PartTransform& a, const PartTransform& b, float t) {
    PartTransform r;
    r.rotateDeg = lerpAngleDeg(a.rotateDeg, b.rotateDeg, t);
    r.moveX  = a.moveX  + (b.moveX  - a.moveX)  * t;
    r.moveY  = a.moveY  + (b.moveY  - a.moveY)  * t;
    r.scaleY = a.scaleY + (b.scaleY - a.scaleY) * t;
    if (r.scaleY < 0.05f) r.scaleY = 0.05f;   // never collapse a limb flat / 别把部件压没
    return r;
}

Pose lerpPose(const Pose& a, const Pose& b, float t) {
    Pose r;
    for (int i = 0; i < PART_COUNT; ++i) {
        r.parts[i] = lerpTransform(a.parts[i], b.parts[i], t);
    }
    r.rootX = a.rootX + (b.rootX - a.rootX) * t;
    r.rootY = a.rootY + (b.rootY - a.rootY) * t;
    return r;
}

}  // namespace

float lerpAngleDeg(float a, float b, float t) {
    // Shortest arc: fold (b - a) into [-180, 180).
    // 最短弧：把 (b - a) 折进 [-180, 180]。
    float d = std::fmod(b - a + 540.f, 360.f) - 180.f;
    return a + t * d;
}

Pose Animation::poseAt(int frame) const {
    Pose identity;
    if (keyframes.empty()) return identity;
    if (keyframes.size() == 1) return keyframes.front().pose;

    // Work in a local, sorted copy so callers need not pre-sort.
    // 用有序副本，调用方不必预先排序。
    std::vector<Keyframe> sorted = keyframes;
    std::sort(sorted.begin(), sorted.end(),
              [](const Keyframe& x, const Keyframe& y) { return x.frame < y.frame; });

    const int first = sorted.front().frame;
    const int last  = sorted.back().frame;

    if (!loop) {
        // Clamp outside the keyframe span. / 关键帧范围外钳住。
        if (frame <= first) return sorted.front().pose;
        if (frame >= last)  return sorted.back().pose;
    }

    // Find the surrounding pair. For loop=true the segment that closes the
    // cycle runs from `last` back to `first` across frameCount.
    // 找相邻两点。loop=true 时收尾那段从 last 跨过 frameCount 回到 first。
    for (size_t i = 0; i + 1 < sorted.size(); ++i) {
        if (frame >= sorted[i].frame && frame < sorted[i + 1].frame) {
            const float span = static_cast<float>(sorted[i + 1].frame - sorted[i].frame);
            const float t = span > 0.f ? (frame - sorted[i].frame) / span : 0.f;
            return lerpPose(sorted[i].pose, sorted[i + 1].pose, t);
        }
    }

    if (!loop) return sorted.back().pose;

    // Wrapping segment / 环绕段
    const float span = static_cast<float>(first + frameCount - last);
    float t = (frame >= last) ? (frame - last) : (frame + frameCount - last);
    if (span > 0.f) t /= span; else t = 0.f;
    return lerpPose(sorted.back().pose, sorted.front().pose, t);
}

// ---------------------------------------------------------------------------
// Presets / 预设
// ---------------------------------------------------------------------------

namespace {

// The pants' two main tones, counted straight off the leg sprites. The spec
// (ANIMATION_SPEC §5) fixes the pants at exactly two similar-hue,
// different-lightness tones, so counting the two most frequent leg colours —
// skipping the outline, which is the darkest opaque colour — finds them with
// no per-pixel art. Returns false unless two clear tones remain.
// 裤子的两种主色，直接从腿精灵上数出来。规范（ANIMATION_SPEC §5）把裤子定为
// 恰好两种色相相近、明度不同的主色，所以数腿上最常见的两种颜色（跳过描边，
// 即最暗的不透明色）即可，无需逐像素手绘。数不出两种清晰主色则返回 false。
bool detectPantsTones(const Rig& rig, cv::Vec4b& dark, cv::Vec4b& light) {
    std::map<std::array<int, 4>, int> freq;
    for (int pid : {PART_LEG_LEFT, PART_LEG_RIGHT}) {
        const cv::Mat& spr = rig.parts[pid - 1];
        for (int y = 0; y < spr.rows; ++y)
            for (int x = 0; x < spr.cols; ++x) {
                const cv::Vec4b p = spr.at<cv::Vec4b>(y, x);
                if (p[3] == 0) continue;
                ++freq[{p[0], p[1], p[2], p[3]}];
            }
    }
    if (freq.size() < 3) return false;   // outline + at least two tones / 描边 + 至少两主色

    std::vector<std::pair<std::array<int, 4>, int>> v(freq.begin(), freq.end());
    std::sort(v.begin(), v.end(),
              [](const auto& x, const auto& y) { return x.second > y.second; });

    auto lum = [](const std::array<int, 4>& c) { return c[0] + c[1] + c[2]; };
    size_t darkest = 0;
    for (size_t i = 1; i < v.size(); ++i)
        if (lum(v[i].first) < lum(v[darkest].first)) darkest = i;

    std::array<int, 4> t[2];
    int got = 0;
    for (size_t i = 0; i < v.size() && got < 2; ++i) {
        if (i == darkest) continue;
        t[got++] = v[i].first;
    }
    if (got < 2) return false;

    if (lum(t[0]) > lum(t[1])) std::swap(t[0], t[1]);
    dark  = cv::Vec4b(t[0][0], t[0][1], t[0][2], t[0][3]);
    light = cv::Vec4b(t[1][0], t[1][1], t[1][2], t[1][3]);
    return true;
}

// Per-frame pants-tone trade. Group 0 keeps the leg tones as drawn; group 1
// trades dark and light (ANIMATION_SPEC §5). Two swaps that are each other's
// inverse = the two tones swap places; leg shapes stay untouched.
// 逐帧裤色互换。组 0 保留腿的原色；组 1 交换深浅（ANIMATION_SPEC §5）。
// 两条互为反向的换色 = 两色互换位置；腿形完全不动。
void applyToneSwap(Pose& p, const cv::Vec4b& dark, const cv::Vec4b& light, int group) {
    if (group == 0) return;
    p.swaps.push_back({dark, light});
    p.swaps.push_back({light, dark});
}

// Wrap a set of keyframe poses evenly around the cycle.
// 把一组关键帧姿势均匀铺在循环上。
//
// With poses.size() == frameCount this puts ONE KEYFRAME ON EVERY FRAME, so
// poseAt() never lerps and every rendered frame is exactly the keyframe that
// was authored. The presets below rely on that: the reference bob has 1px
// steps, which lround would flatten if anything were interpolated.
// poses.size() == frameCount 时【每帧一个关键帧】，poseAt() 从不插值，
// 渲染出的每帧都精确等于所写的姿势。下面的预设正依赖这一点：参考图的起伏有
// 1 像素的步进，一旦插值就会被取整压平。
void spreadKeyframes(Animation& anim, const std::vector<Pose>& poses) {
    const int n = static_cast<int>(poses.size());
    if (n == 0) return;
    anim.keyframes.clear();
    for (int i = 0; i < n; ++i) {
        Keyframe k;
        // Spread over [0, frameCount) without duplicating frameCount.
        // 铺在 [0, frameCount) 上，不重复 frameCount。
        k.frame = (i * anim.frameCount) / n;
        k.pose  = poses[i];
        anim.keyframes.push_back(k);
    }
}

// Is this rig small enough that rotation is invisible? At 16x16 a leg may be
// 3x6 px, where rotating produces noise, not motion. Degrade to translation.
// 这个骨架是否小到旋转看不出？16x16 时腿可能只有 3x6 像素，旋转产生的是噪声。
// 降级为纯位移。
bool tooSmallForRotation(const Rig& rig) {
    return rig.size.width <= 16 || rig.size.height <= 16;
}

// ---- The reference sheet's own numbers / 参考图自己的数字 --------------------
//
// body_offset_y, taken VERBATIM from assets/templates/anim_reference.json and
// used as the absolute template for the bob. POSITIVE = the character sits
// LOWER in the frame (y grows downward), which is exactly how the sheet draws
// it: a walk sinks into its footfalls and vaults up over its stance leg. The
// presets used to do the opposite — bob up 2px on the pass frames.
// body_offset_y，逐字取自 assets/templates/anim_reference.json，是起伏的绝对
// 模板。正值 = 角色在画面中更靠下（y 向下增长），正是参考图的画法：走路沉进落脚、
// 在支撑腿上方拱起。预设过去反着来 —— 在交叉帧往上抬 2 像素。
//
// The 1px steps are safe here even though CLAUDE.md warns against 1px
// amplitudes. That rule exists to stop lround flattening LERPED in-between
// frames; these presets lay one keyframe per frame, so nothing is interpolated
// and every step lands on a real frame and rasterises exactly.
// 这里的 1 像素步进是安全的，尽管 CLAUDE.md 反对 1 像素幅度。那条规定是为了防止
// 取整把【插值出来的】中间帧压平；本预设每帧一个关键帧，从不插值，每一步都落在
// 真实帧上、精确光栅化。
constexpr float kBobSide[6]    = {0.f, 1.f, 0.f, 0.f, 1.f, 0.f};
constexpr float kBobFrontal[6] = {0.f, 1.f, 0.f, 1.f, 2.f, 1.f};

// The leg rhythm the sheet draws, slot by slot: hold each stride for two
// frames and cut through the pass for one. +1 = left leg forward (side: swung
// about the hip; front: lifted toward the camera), -1 = mirrored, 0 = pass,
// both legs under the body. Measured from anim_reference.json walk_front
// frame_data — the swinging leg's y-range shortens on slots 0/5 (left) and
// 2/3 (right), both legs plant on slots 1/4 — and cross-checked on walk_side,
// whose bottom-row x-extent widens on exactly those same slots.
// 参考图逐槽位画的腿节奏：跨步保持两帧，交叉切过一帧。+1 = 左腿在前（侧面：绕胯
// 摆；正面：抬向镜头），-1 = 镜像，0 = 交叉，双腿在身下。由 anim_reference.json
// walk_front frame_data 实测 —— 摆动腿的 y 范围在槽 0/5（左）和 2/3（右）变短，
// 槽 1/4 双腿着地 —— 并用 walk_side 交叉核对，它底部行的 x 宽度恰好在同样槽位变宽。
constexpr float kSwingWalk[6]  = {+1.f, 0.f, -1.f, -1.f, 0.f, +1.f};

// Which frames share a pants-tone arrangement (anim_reference.json
// tone_swap_groups). 0 = the legs' own tones, 1 = the two tones traded.
// 哪些帧共用一种裤色排布（anim_reference.json 的 tone_swap_groups）。
// 0 = 腿的原色，1 = 两色互换。
constexpr int kToneFront[6] = {0, 0, 1, 1, 1, 0};   // [[0,1,5],[2,3,4]]
constexpr int kToneSide[6]  = {0, 0, 0, 1, 0, 0};   // [[0,1,2,4,5],[3]]
constexpr int kToneUp[6]    = {0, 0, 1, 1, 0, 0};   // [[0,1,4,5],[2,3]]

// Which frames hide the far arm (ANIMATION_SPEC §6): the torso occludes it on
// 4 of 6 walk_side frames — measured, arm_right is ~9px on these slots vs 19px
// on the two it shows. 1 = hide.
// 哪几帧隐藏远端手臂（ANIMATION_SPEC §6）：躯干在侧走 6 帧里的 4 帧挡住它 ——
// 实测 arm_right 在这些槽位约 9px，在显示的两帧 19px。1 = 隐藏。
constexpr int kHideFarArmSide[6] = {1, 1, 0, 0, 1, 1};

// Which of the cycle's `slots` the i-th of n frames uses. The reference fixes
// each cycle at 2 slots (idle) or 6 (walk); a different frame count resamples
// those slots — exact at the reference counts, nearest otherwise.
// n 帧循环里第 i 帧用哪个槽位（循环共 `slots` 个槽）。参考图把每个循环定死在
// 2 个槽（待机）或 6 个槽（走路）；帧数不同时对这些槽位重采样 —— 参考帧数时精确，
// 否则取最近。
int slotOf(int i, int n, int slots) { return (i * slots) / n; }

}  // namespace

Animation makeIdle(const Rig& rig, int frameCount, const PresetParams& sp) {
    Animation a;
    a.name = "idle";
    a.frameCount = std::max(2, frameCount);
    // 4fps, not 8: the reference sheet gives idle 2 frames, so a full breath
    // is one second only at 4fps. Slower reads as a statue; faster reads as
    // shivering.
    // 4fps 而非 8：参考图给 idle 2 帧，只有在 4fps 下一次呼吸才是整一秒。
    // 再慢像雕像，再快像打颤。
    a.fps = 4;
    a.loop = true;

    // ---- DETACH RULE / 脱节铁律 -------------------------------------------
    //
    // A part must never translate away from its neighbour. A one-pixel gap at
    // the neck or the hip is the ugliest artifact this renderer can produce,
    // and the first draft of this preset did exactly that: the head bobbed
    // 0.5px more than the torso, and `lround(-1.5) = -2` against the torso's
    // `-1` opened a visible seam at the neck.
    // 部件绝不可平移着离开相邻部件。脖子或胯部开一像素的缝，是本渲染器最难看的
    // 产物，而本预设的第一稿就犯了这个错：头比躯干多起伏 0.5 像素，
    // `lround(-1.5) = -2` 对上躯干的 `-1`，脖子上裂开一道明晃晃的缝。
    //
    // Motion that is safe by construction / 天然安全的动作：
    //   rootX / rootY  every part moves together / 所有部件一起动
    //   rotateDeg      about the part's own joint, so the joint stays glued
    //                  / 绕自身关节旋转，关节钉住
    //   scaleY         shortens from the far end with the joint pinned
    //                  / 从远端缩短，关节钉住
    //
    // Per-part moveX / moveY is safe only when every neighbour moves by exactly
    // the same amount. The idle stretch is exactly that case: the torso grows
    // one row at the TOP (its bottom stays pinned at the hip), and the head and
    // arms ride up by the SAME moveY, so the neck and shoulder joints stay
    // glued. That is why moveY appears here and nowhere else.
    // 部件级的 moveX / moveY 只有在所有相邻部件位移【完全相同】时才安全。待机伸展
    // 正是这种情况：躯干在【顶部】长高一行（底部钉在胯部），头与臂以【同样的】
    // moveY 上抬，脖子与肩关节全部粘牢。所以 moveY 只在这里出现，别处不用。
    //
    // The breath is therefore a STRETCH, not a whole-character bob: a uniform
    // bob would move the legs too, but the reference keeps the legs + boots
    // pixel-still and only raises the upper body (ANIMATION_SPEC §9). A shoulder
    // roll was tried and rejected: at 8deg it read as shrugging, and
    // nearest-neighbour rotation cost the arms 12% of their pixels in a frame.
    // 所以呼吸是【伸展】，不是整体起伏：整体起伏会连腿一起抬，而参考图让腿+靴
    // 逐像素不动、只抬上身（ANIMATION_SPEC §9）。转肩试过又否掉：8 度读起来像耸肩，
    // 而且最近邻旋转让手臂单帧丢 12% 像素。
    // -----------------------------------------------------------------------

    // The reference gives idle exactly 2 frames (rows 0/2/4 hold 2 cells
    // each) — one breath per cycle. Lay one keyframe per frame so poseAt()
    // never lerps the 1px step into a flat line.
    // 参考图给 idle 正好 2 帧（row 0/2/4 各 2 格）—— 一次呼吸一个循环。
    // 每帧一个关键帧，好让 poseAt() 不会把 1 像素的步进插成一条平线。
    //
    // The breath is a STRETCH, not a whole-character bob: the legs stay
    // planted and the upper body grows one row taller (ANIMATION_SPEC §9).
    // Measured: legs + boots are pixel-identical between the two frames; only
    // the upper body rises one row on the stretch frame.
    // 呼吸是【伸展】，不是整体起伏：腿钉住不动，上身长高一行（ANIMATION_SPEC §9）。
    // 实测：两帧的腿+靴逐像素相同；只有上身在伸展帧抬高一行。
    //
    // Mechanism (ANIMATION_SPEC §11.4): the torso scales scaleY=(h+rows)/h
    // about its shoulder pivot with a compensating moveY so its BOTTOM stays
    // pinned at the hip (DETACH). This repeats one row at the top = a
    // colour-sampled extra row. The head and arms follow the risen shoulder
    // with the same moveY, so every joint stays glued. Legs untouched.
    // 机制（ANIMATION_SPEC §11.4）：躯干绕肩部 pivot 做 scaleY=(h+rows)/h，
    // 并用 moveY 补偿，让【底部】钉在胯部（脱节铁律）。这会在顶部重复一行 =
    // 取色得到的额外行。头与臂以同样的 moveY 跟随抬起的肩，关节全部粘牢。腿不动。
    const cv::Rect& bodyBox = rig.bbox[PART_BODY - 1];
    const float h    = (float)std::max(1, bodyBox.height);
    const float rows = std::max(0.f, sp.breathScale);   // stretch rows; ref = 1
    std::vector<Pose> poses(static_cast<size_t>(a.frameCount));
    for (int i = 0; i < a.frameCount; ++i) {
        Pose& p = poses[i];
        // slot 0 = stretch, slot 1 = rest. The rest frame is byte-identical to
        // the source; the stretch frame is the transformed one.
        // 槽 0 = 伸展，槽 1 = 放松。放松帧与源图逐字节相同；伸展帧才是变换的。
        if (slotOf(i, a.frameCount, 2) == 1) continue;
        p.parts[PART_BODY - 1].scaleY     = (h + rows) / h;
        p.parts[PART_BODY - 1].moveY      = -rows;
        p.parts[PART_HEAD - 1].moveY      = -rows;
        p.parts[PART_ARM_LEFT - 1].moveY  = -rows;
        p.parts[PART_ARM_RIGHT - 1].moveY = -rows;
    }

    spreadKeyframes(a, poses);
    return a;
}

Animation makeWalkSide(const Rig& rig, int frameCount, const PresetParams& sp) {
    Animation a;
    a.name = "walk_side";
    a.frameCount = std::max(4, frameCount);
    a.fps = 10;
    a.loop = true;

    // Below 32x32 a leg may be a few pixels wide, where a wide swing reads as
    // noise rather than motion. Halve the amplitude — do NOT swap in
    // translation as a "fallback": a translated limb opens a gap at its joint,
    // a rotated one cannot (see the detach rule in makeIdle).
    // 32x32 以下腿可能只有几个像素宽，大角度摆动读起来是噪声不是动作。
    // 把幅度减半 —— 【不要】拿平移当"备选"：平移的肢体在关节开缝，旋转的不会
    // （见 makeIdle 的脱节铁律）。
    const float sizeFade = tooSmallForRotation(rig) ? 0.5f : 1.f;

    // Side view: the legs really are visible edge-on, so swinging about the hip
    // is the right motion. +Y is down, so a positive leg rotation swings the
    // leg forward; the two legs must stay opposite.
    // 侧面：腿是侧视可见的，绕胯摆动是正确动作。+Y 向下，正旋转把腿甩向前；
    // 两条腿必须始终相反。
    // 18deg, not 22: at 22deg a 6px-wide leg's edge pixel splits off during
    // rasterisation (the detach gate caught it). 18deg keeps the stride readable
    // without tearing the sprite.
    // Measured from reference: max boot x-displacement ~3px over hip-to-boot
    // length ~10px → atan(3/10) ≈ 17°, confirming 18° is close.
    // 18 度而非 22：22 度时 6px 宽的腿边缘像素在光栅化时会掉出去（脱节闸门抓到）。
    // 18 度跨步仍然看得清，又不会撕裂精灵。
    // 参考图测量：靴最大 x 位移约 3px，胯到靴长约 10px → atan(3/10) ≈ 17°，
    // 确认 18° 已接近。
    const float leg = 18.f * sizeFade * sp.motionScale;
    const float arm = 11.f * sizeFade * sp.motionScale;

    // The bob is kBobSide verbatim — 0/1px, sinking on the pass frames. This
    // preset used to bob 2px the other way (up on the pass), which is both the
    // wrong direction and twice the amplitude the sheet draws.
    // 起伏照搬 kBobSide —— 0/1 像素，交叉帧下沉。本预设过去朝反方向起伏 2 像素
    // （交叉帧上抬），方向错、幅度也是参考图画法的两倍。
    //
    // One keyframe per frame: the reference's rhythm holds each stride for two
    // frames and cuts through the pass for one, which is not something four
    // keyframes spread over six frames can express — that layout interpolated
    // frames 2 and 5 and turned a 6-frame cycle into a 4-frame one.
    // 每帧一个关键帧：参考图的节奏是跨步保持两帧、交叉一帧切过，四个关键帧铺在
    // 六帧上表达不了 —— 那样会把第 2、5 帧变成插值，把 6 帧的循环压成 4 帧的。
    cv::Vec4b dark, light;
    const bool haveTones = detectPantsTones(rig, dark, light);

    std::vector<Pose> poses(static_cast<size_t>(a.frameCount));

    for (int i = 0; i < a.frameCount; ++i) {
        const int   s = slotOf(i, a.frameCount, 6);
        const float w = kSwingWalk[s];
        Pose& p = poses[i];

        // Legs swing about the hip; the two stay opposite. Arms counter-swing.
        // 腿绕胯摆，两条始终相反。手臂反向摆。
        p.parts[PART_LEG_LEFT - 1].rotateDeg  = +leg * w;
        p.parts[PART_LEG_RIGHT - 1].rotateDeg = -leg * w;
        p.parts[PART_ARM_LEFT - 1].rotateDeg  = -arm * w;
        p.parts[PART_ARM_RIGHT - 1].rotateDeg = +arm * w;

        // The far arm is occluded by the torso on 4 of 6 frames — hide it.
        // 远端手臂在 6 帧里的 4 帧被躯干挡住 —— 隐藏它。
        p.hidden[PART_ARM_RIGHT - 1] = (kHideFarArmSide[s] != 0);

        p.rootY = kBobSide[s] * sp.bobScale;
        if (haveTones) applyToneSwap(p, dark, light, kToneSide[s]);
    }

    spreadKeyframes(a, poses);
    return a;
}

// Front and back share the motion. From both, a leg stepping toward the camera
// cannot show its stride as in-plane rotation — that reads as the puppet doing
// the splits, which is why this template never rotates a leg. The near leg
// reads as FORESHORTENING: it gets shorter. Shortening with scaleY, pivot at
// the joint, is also the one limb motion that cannot open a gap at the hip,
// so "what looks right" and "what cannot detach" happen to agree here.
// 正面与背面共用动作。从这两个角度看，迈向镜头的腿无法用平面内旋转表现跨步
// —— 那像木偶劈叉，所以本模板从不旋转腿。近处的腿表现成【透视缩短】：变短。
// 用 scaleY（pivot 在关节）缩短，同时也是唯一不会在胯开缝的肢体动作，
// 「好看」和「不脱节」在这里恰好是同一件事。
static Animation makeWalkFrontal(const Rig& rig, int frameCount,
                                 const PresetParams& sp, const char* name,
                                 bool facingAway) {
    Animation a;
    a.name = name;
    a.frameCount = std::max(4, frameCount);
    a.fps = 10;
    a.loop = true;

    // Legs stay STRAIGHT and the arms are copied VERBATIM (ANIMATION_SPEC §4):
    // the leg band is pure tone-swap, no per-frame limb geometry. Detect the
    // two pants tones off the legs; `facingAway` only picks the tone group.
    cv::Vec4b dark, light;
    const bool haveTones = detectPantsTones(rig, dark, light);
    const int* toneGroup = facingAway ? kToneUp : kToneFront;

    // The bob is kBobFrontal verbatim — 0/1/2px, sinking into the footfalls.
    // See the note above the kBob tables: the old 2px upward bob was the wrong
    // direction at twice the amplitude.
    // 起伏照搬 kBobFrontal —— 0/1/2 像素，沉进落脚。见 kBob 表上方的说明：
    // 旧的 2 像素上抬方向错、幅度翻倍。
    //
    // One keyframe per frame, for the same reason as walk_side: the reference
    // holds each stride for two frames and cuts the pass for one.
    // 每帧一个关键帧，理由同 walk_side：参考图每个跨步保持两帧、交叉切一帧。
    //
    // `facingAway` now only SELECTS THE TONE GROUP (front vs back trade the
    // two pants tones on different frames) — the legs no longer swing, so
    // there is no swing sign to flip. The back-view tone phase differs from
    // the front's (anim_reference.json tone_swap_groups).
    // `facingAway` 现在只【选择换色组】（正/背在不同帧互换两裤色）—— 腿不再摆，
    // 没有摆动符号可翻。背面的换色相位与正面不同（anim_reference.json
    // tone_swap_groups）。
    std::vector<Pose> poses(static_cast<size_t>(a.frameCount));
    for (int i = 0; i < a.frameCount; ++i) {
        const int s = slotOf(i, a.frameCount, 6);
        Pose& p = poses[i];

        p.rootY = kBobFrontal[s] * sp.bobScale;
        if (haveTones) applyToneSwap(p, dark, light, toneGroup[s]);
    }

    spreadKeyframes(a, poses);
    return a;
}

Animation makeWalkFront(const Rig& rig, int frameCount, const PresetParams& sp) {
    return makeWalkFrontal(rig, frameCount, sp, "walk_front", false);
}
Animation makeWalkUp(const Rig& rig, int frameCount, const PresetParams& sp) {
    return makeWalkFrontal(rig, frameCount, sp, "walk_up", true);
}

Animation makeIdle(const Rig& rig, int frameCount) {
    return makeIdle(rig, frameCount, PresetParams{});
}
Animation makeWalkSide(const Rig& rig, int frameCount) {
    return makeWalkSide(rig, frameCount, PresetParams{});
}
Animation makeWalkFront(const Rig& rig, int frameCount) {
    return makeWalkFront(rig, frameCount, PresetParams{});
}
Animation makeWalkUp(const Rig& rig, int frameCount) {
    return makeWalkUp(rig, frameCount, PresetParams{});
}

}  // namespace fb
