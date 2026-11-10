// animation.cpp — keyframe interpolation and preset generators
// 关键帧插值与预设生成器

#include "core.h"

#include <algorithm>
#include <cmath>

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

// Lift a limb by SHORTENING it from the far end. The part's pivot must be its
// joint, so the joint stays pinned to its neighbour while the far end rises.
// This is the only limb motion that cannot open a gap at the joint — see the
// detach rule spelled out in makeIdle.
// 把肢体从远端【缩短】来抬起它。部件的 pivot 必须是它的关节，于是关节钉在相邻
// 部件上不动、远端抬起。这是唯一不会在关节开缝的肢体动作 —— 见 makeIdle 里的
// 脱节铁律。
float liftScale(const Rig& rig, int partId, float pixels, const PresetParams& sp) {
    const int h = rig.bbox[partId - 1].height;
    if (h <= 0) return 1.f;
    const float s = 1.f - (pixels * sp.motionScale) / static_cast<float>(h);
    return (s < 0.05f) ? 0.05f : s;
}

// Wrap a set of keyframe poses evenly around the cycle.
// 把一组关键帧姿势均匀铺在循环上。
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

}  // namespace

Animation makeIdle(const Rig& rig, int frameCount, const PresetParams& sp) {
    (void)rig;
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
    // the same amount, which is easy to get wrong and invisible until rendered.
    // The presets therefore never use it.
    // 部件级的 moveX / moveY 只有在所有相邻部件位移【完全相同】时才安全，
    // 这一点极易写错，而且要渲染出来才看得见。所以预设里根本不用它。
    //
    // Hence breathing is a whole-character bob plus a small shoulder roll. No
    // scaleY for the chest here: a 2% squash is under a third of a pixel at
    // 32x32 — invisible — and with the torso pivot at the shoulders it would
    // pull the torso up off the legs. scaleY stays in the data model for the
    // keyframe editor, where the user places the pivot themselves.
    // 所以呼吸 = 整体起伏 + 轻微转肩。这里不给胸腔用 scaleY：2% 的压缩在 32x32
    // 下不到三分之一个像素，看不见；而躯干 pivot 在肩，会把躯干从腿上拽上去。
    // scaleY 留在数据模型里给关键帧编辑器用，那时 pivot 由用户自己放。
    //
    // Amplitude is 2px, not 1px: with a 1px bob every lerped in-between frame
    // rounds to the same offset, so the body stutters between two heights
    // instead of rising and falling. 2px triangulates cleanly through the
    // pixels. The reference sheet gets away with 1px only because it hard-cuts
    // between its two frames and never interpolates.
    // 幅度是 2 像素而非 1：起伏只有 1 像素时，所有插值中间帧取整到同一偏移，
    // 身体在两个高度间抽搐而不是起落。2 像素能在像素上走出干净的三角波。
    // 参考图能用 1 像素，只因它是两帧硬切、从不插值。
    //
    // No shoulder roll. Tried it, and at 8deg it read as the character
    // shrugging, not breathing — worse, nearest-neighbour rotation cost the
    // arms 8 of their 64 pixels (12%) in a single frame. A hand-drawn idle
    // (see the reference sheet) moves the whole upper body by a fraction of a
    // pixel; the closest honest equivalent here is the whole-character bob.
    // 不转肩。试过：8 度读起来是"耸肩"而不是呼吸，更糟的是最近邻旋转让手臂在
    // 单帧里丢了 64 个像素中的 8 个（12%）。手绘待机（见参考图）只是把整个上身
    // 挪动不到一个像素；这里最接近的诚实等价物就是整体起伏。
    // -----------------------------------------------------------------------

    const float bob = -2.f * sp.breathScale;

    // TWO keyframes, not four. The reference sheet gives idle exactly 2 frames
    // (rows 0/2/4 hold 2 cells each), and at a 2-frame count four keyframes
    // collapse onto the same two frame indices: the poses overwrite each other
    // and both frames come out identical. Two keyframes give exactly one
    // breath per cycle, which is what the sheet draws.
    // 两个关键帧，不是四个。参考图给 idle 正好 2 帧（row 0/2/4 各 2 格），而
    // 帧数为 2 时四个关键帧会塌到相同的两个帧序号上：姿势互相覆盖，两帧变得
    // 一模一样。两个关键帧恰好一次呼吸一个循环，正是参考图画的东西。
    Pose rest;
    Pose inhale = rest;
    inhale.rootY = bob;

    spreadKeyframes(a, {rest, inhale});
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

    // 2px, not 1px: with a 1px bob every lerped in-between frame rounds to the
    // same offset, so the body appears to stutter between two heights instead
    // of rising and falling. 2px triangulates cleanly through the pixels.
    // 2 像素而非 1：起伏只有 1 像素时，所有插值中间帧取整到同一偏移，
    // 看起来是在两个高度间抽搐，而不是起落。2 像素能在像素上走出干净的三角波。
    const float bob = -2.f * sp.bobScale;

    // Classic contact / pass rhythm, 4 keyframes over the cycle.
    // 经典 contact / pass 节奏，循环内 4 个关键帧。
    std::vector<Pose> poses(4);

    // K0 contact: left leg forward, right leg back. Arms counter-swing.
    // K0 触地：左腿在前，右腿在后。手臂反向摆。
    poses[0].parts[PART_LEG_LEFT - 1].rotateDeg  = +leg;
    poses[0].parts[PART_LEG_RIGHT - 1].rotateDeg = -leg;
    poses[0].parts[PART_ARM_LEFT - 1].rotateDeg  = -arm;
    poses[0].parts[PART_ARM_RIGHT - 1].rotateDeg = +arm;

    // K1 pass: legs together, body at the top of the bob.
    // K1 交叉：双腿并拢，身体在起伏最高点。
    poses[1].rootY = bob;

    // K2 contact: mirrored.
    // K2 触地：镜像。
    poses[2].parts[PART_LEG_LEFT - 1].rotateDeg  = -leg;
    poses[2].parts[PART_LEG_RIGHT - 1].rotateDeg = +leg;
    poses[2].parts[PART_ARM_LEFT - 1].rotateDeg  = +arm;
    poses[2].parts[PART_ARM_RIGHT - 1].rotateDeg = -arm;

    // K3 pass: legs together again, top of the bob.
    // K3 交叉：双腿再次并拢，起伏最高点。
    poses[3].rootY = bob;

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
                                 const PresetParams& sp, const char* name) {
    Animation a;
    a.name = name;
    a.frameCount = std::max(4, frameCount);
    a.fps = 10;
    a.loop = true;

    // Lift the foot by ~2px and the hand by ~1.5px however long the limbs
    // are, so a 16x16 rig lifts as visibly as a 32x32 one. Amplitudes are in
    // pixels because that is the unit the rasteriser can actually resolve —
    // a smaller lift silently rounds away on the in-between frames.
    // Measured from reference: stepping boot bottom y31 vs planted boot bottom
    // y28-y29 = ~2px lift (anim_reference.json walk_front frame_data).
    // 不论肢体多长，把脚抬起约 2 像素、手约 1.5 像素，这样 16x16 和 32x32
    // 抬起的可见度一致。幅度以像素为单位，因为那是光栅化器唯一能分辨的单位
    // —— 更小的抬起在中间帧会被静默取整掉。
    // 参考图测量：迈步靴底 y31 vs 站立靴底 y28-y29 = 约 2px 抬起
    // （anim_reference.json walk_front frame_data）。
    const float legLift = 2.0f;
    const float armLift = 1.5f;

    // 2px bob for the same reason as walk_side — see the note there.
    // 起伏 2 像素，理由同 walk_side —— 见那里的说明。
    const float bob = -2.f * sp.bobScale;

    std::vector<Pose> poses(4);

    // K0 — body low, left foot stepping toward the camera (shortened), right
    //      hand forward (shortened). Limbs are contralateral in a real walk.
    // K0 —— 身体低，左脚迈向镜头（变短），右手在前（变短）。真人走路是交叉的。
    poses[0].parts[PART_LEG_LEFT - 1].scaleY  = liftScale(rig, PART_LEG_LEFT,  legLift, sp);
    poses[0].parts[PART_ARM_RIGHT - 1].scaleY = liftScale(rig, PART_ARM_RIGHT, armLift, sp);

    // K1 — both feet down, body at the top of the bob.
    // K1 —— 双脚着地，身体在起伏最高点。
    poses[1].rootY = bob;

    // K2 — mirrored: right foot forward, left hand forward.
    // K2 —— 镜像：右脚在前，左手在前。
    poses[2].parts[PART_LEG_RIGHT - 1].scaleY = liftScale(rig, PART_LEG_RIGHT, legLift, sp);
    poses[2].parts[PART_ARM_LEFT - 1].scaleY  = liftScale(rig, PART_ARM_LEFT,  armLift, sp);

    // K3 — pass again.
    // K3 —— 再次交叉。
    poses[3].rootY = bob;

    spreadKeyframes(a, poses);
    return a;
}

Animation makeWalkFront(const Rig& rig, int frameCount, const PresetParams& sp) {
    return makeWalkFrontal(rig, frameCount, sp, "walk_front");
}
Animation makeWalkUp(const Rig& rig, int frameCount, const PresetParams& sp) {
    return makeWalkFrontal(rig, frameCount, sp, "walk_up");
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
