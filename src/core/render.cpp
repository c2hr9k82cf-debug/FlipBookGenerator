// render.cpp — pixel-art-safe rasterisation, composition, reconstruction test
// 像素风安全的光栅化、合成、重建校验
//
// PIXEL-ART TRANSFORM RULES / 像素风变换硬约束 (see CLAUDE.md):
//   1. Nearest-neighbour only. Never bilinear/cubic.
//      只用最近邻，永不双线性/三次。
//   2. Rotation rasterises by INVERSE mapping. Forward-sampling a rotation
//      punches holes; inverse mapping does not.
//      旋转用【逆向映射】光栅化。正向采样会打洞，逆向映射不会。
//   3. Translations round to the integer pixel grid. This IS the pixel-
//      perfect mode; there is no sub-pixel option.
//      平移取整到像素网格。这就是像素完美模式，没有亚像素选项。
//   4. No anti-aliasing. No blending at part overlaps — z-order wins.
//      无抗锯齿。重叠处不混合，按 zOrder 覆盖。

#include "core.h"

#include <cmath>

namespace fb {
namespace {

constexpr float kDegToRad = 0.017453292519943295f;   // pi / 180

// Forward transform: source-image coords -> dest coords.
// 正向变换：原图坐标 -> 目标坐标。
//
//   v   = p - pivot
//   v.y *= scaleY
//   v   = R(rotateDeg) * v
//   p'  = v + pivot + move
struct Affine2D {
    float cosA, sinA;
    float scaleY;
    float px, py;        // pivot / 旋转中心
    float tx, ty;        // translation / 平移

    static Affine2D from(const PartTransform& t, const cv::Point2f& pivot,
                         float rootX, float rootY) {
        Affine2D m;
        const float r = t.rotateDeg * kDegToRad;
        m.cosA = std::cos(r);
        m.sinA = std::sin(r);
        m.scaleY = (t.scaleY <= 0.05f) ? 0.05f : t.scaleY;
        m.px = pivot.x;
        m.py = pivot.y;
        // Translations snap to the pixel grid at raster time.
        // 平移在光栅化时取整到像素网格。
        m.tx = (float)std::lround(t.moveX + rootX);
        m.ty = (float)std::lround(t.moveY + rootY);
        return m;
    }

    // Dest -> source. Sampling this way is what stops a rotated part from
    // developing holes. Forward map is:
    //   v  = p - pivot;  v.y *= scaleY;  f = R(a)*v;  p' = f + pivot + move
    // so the inverse is R(-a) applied to (q - pivot - move), then v.y / scaleY.
    // 目标 -> 源。这样采样才不会让旋转后的部件出现空洞。正向为
    //   v = p - pivot; v.y *= scaleY; f = R(a)*v; p' = f + pivot + move
    // 故逆向为对 (q - pivot - move) 施以 R(-a)，再把 v.y 除以 scaleY。
    void inverse(float qx, float qy, float& outPx, float& outPy) const {
        const float dx = qx - px - tx;
        const float dy = qy - py - ty;
        outPx = (cosA * dx + sinA * dy) + px;
        outPy = ((-sinA * dx + cosA * dy) / scaleY) + py;
    }
};

// Rasterise one part's sprite into the frame, honouring the rules above.
// `owner` records WHICH part painted each pixel (0 = nobody); the seam repair
// needs it to tell one part's own drawing from a gap between two parts.
// 按上述规则把一个部件的精灵图光栅化进画面。`owner` 记下每个像素是【谁】画的
// （0 = 没人画）；补缝要用它来区分"一个部件自己的画法"和"两个部件之间的缝"。
void blitPart(cv::Mat& frame, cv::Mat& owner, int partId, const cv::Mat& sprite,
              const cv::Rect& bbox, const Affine2D& m) {
    // Conservative dest bounds: the source bbox corners mapped forward.
    // 保守的目标范围：源包围盒四角经正向变换后的位置。
    const float xs[4] = {
        (float)bbox.x, (float)(bbox.x + bbox.width),
        (float)bbox.x, (float)(bbox.x + bbox.width),
    };
    const float ys[4] = {
        (float)bbox.y, (float)bbox.y,
        (float)(bbox.y + bbox.height), (float)(bbox.y + bbox.height),
    };
    float minXd = 1e9f, maxXd = -1e9f, minYd = 1e9f, maxYd = -1e9f;
    for (int i = 0; i < 4; ++i) {
        float vx = xs[i] - m.px, vy = ys[i] - m.py;
        vy *= m.scaleY;
        const float fx = m.cosA * vx - m.sinA * vy + m.px + m.tx;
        const float fy = m.sinA * vx + m.cosA * vy + m.py + m.ty;
        minXd = std::min(minXd, fx); maxXd = std::max(maxXd, fx);
        minYd = std::min(minYd, fy); maxYd = std::max(maxYd, fy);
    }

    // Dest pixels to touch: those whose CELL overlaps the transformed bbox,
    // i.e. x < maxXd and x+1 > minXd. That is floor(minXd) ..= ceil(maxXd)-1.
    // The old ceil(maxXd) bound was one pixel too wide on every side: under an
    // identity pose it walked one row/column past the bbox and the out-of-bbox
    // check silently skipped it. Harmless, but iterating exactly the covered
    // region keeps the rasteriser's cost and its reach in step.
    // 要动的目标像素：其【格子】与变换后的包围盒相交，即 x < maxXd 且 x+1 > minXd，
    // 也就是 floor(minXd) ..= ceil(maxXd)-1。旧的 ceil(maxXd) 每边都宽出一个像素：
    // 单位位姿下会多走一圈，靠"越界就跳过"蒙混过去。倒也无害，但只遍历真正覆盖到的
    // 区域，光栅化的开销与作用范围才对得上。
    const int x0 = std::max(0, (int)std::floor(minXd));
    const int y0 = std::max(0, (int)std::floor(minYd));
    const int x1 = std::min(frame.cols - 1, (int)std::ceil(maxXd) - 1);
    const int y1 = std::min(frame.rows - 1, (int)std::ceil(maxYd) - 1);
    if (x1 < x0 || y1 < y0) return;   // degenerate after scaling / 缩放后退化为空

    for (int y = y0; y <= y1; ++y) {
        cv::Vec4b* dstRow = frame.ptr<cv::Vec4b>(y);
        uchar* ownerRow = owner.ptr<uchar>(y);
        for (int x = x0; x <= x1; ++x) {
            float px, py;
            m.inverse((float)x + 0.5f, (float)y + 0.5f, px, py);

            // Nearest source pixel. A sample that falls outside the part's own
            // bbox is DISCARDED: that pixel belongs to the part's past, not its
            // present — its far end swung away and left it behind. Clamping it
            // instead smears the edge colour outward into a horizontal streak,
            // which is far worse than the gap it tried to repair.
            // 最近源像素。采样点落到部件 bbox 之外时【丢弃】：那个像素属于部件的
            // 过去而非现在 —— 远端已经摆走，把它留在了身后。若改用夹取，边缘色
            // 会向外抹成横向拖尾，比它想修的裂缝糟糕得多。
            //
            // Why a rotated part samples outside itself / 为何旋转后会采样到自身之外:
            //   Inverse mapping rotates the output back into the source, so a
            //   pixel near the part's far corner can map to a source point just
            //   past the bbox edge. That pixel belonged to the part before the
            //   rotation; after it, the pixel falls on a column the part no
            //   longer covers. Drawing it there is the smear.
            //   逆映射把输出转回源坐标，于是部件远角附近的像素会映射到 bbox 边缘
            //   之外一点点。该像素旋转前属于部件，旋转后落到部件不再覆盖的列上。
            //   在那里画它就是涂抹。
            //
            // The gap that IS a joint seam is repaired after composition by
            // fillInteriorHoles(). Distance from the bbox cannot tell seam from
            // vacated space (measured 0.61px vs 0.58px); enclosure can.
            // 真正属于关节缝的空缺由 fillInteriorHoles() 在合成后修补。
            // 离 bbox 的距离分不清缝和摆空的区域（实测 0.61px 对 0.58px）；
            // 封闭性可以。
            int sx = (int)std::floor(px);
            int sy = (int)std::floor(py);
            if (sx < bbox.x || sx >= bbox.x + bbox.width) continue;
            if (sy < bbox.y || sy >= bbox.y + bbox.height) continue;

            const cv::Vec4b s = sprite.at<cv::Vec4b>(sy - bbox.y, sx - bbox.x);
            if (s[3] == 0) continue;   // fully transparent / 全透明

            // Overwrite, no blending. z-order decides.
            // 直接覆盖，不混合。由 zOrder 决定。
            dstRow[x] = s;
            ownerRow[x] = (uchar)partId;
        }
    }
}

// Fill ENCLOSED transparent pixels — the joint seams.
//
// A transparent pixel the background flood cannot reach from the border is
// boxed in by opaque parts on every side: that is a seam squeezed out between
// two neighbours at a joint, not open background. The space a limb vacates
// when it swings away IS open (it runs out to the border), so it is never
// touched — which is exactly the discrimination sampling-distance cannot make
// (seam measured 0.61px from the bbox edge vs vacated space 0.58px).
//
// Fill with an opaque neighbour's own colour: no blending, no anti-aliasing,
// no invented colour (rule 4). Filled pixels become opaque themselves, so the
// fill propagates inward and a seam wider than one pixel is repaired too.
//
// Under an identity pose nothing is enclosed — the parts abut exactly — so
// this is a no-op there and the reconstruction invariant is untouched.
//
// 填补【被包围】的透明像素 —— 关节缝。
//
// 背景从边界灌不到的透明像素，四面都被不透明部件封住：那是关节处被两个邻居
// 挤出来的缝，不是开放背景。肢体摆走后空出的区域是【开放】的（一路连到边界），
// 因此永不被碰 —— 而这正是采样距离做不到的判别（缝离 bbox 边缘实测 0.61px，
// 摆空的区域 0.58px）。
//
// 用不透明邻居自己的颜色填充：不混合、不抗锯齿、不发明颜色（规则 4）。
// 填出的像素本身变为不透明，于是填充向内推进，宽于一像素的缝也能补上。
//
// 单位位姿下没有任何像素被包围（部件严丝合缝），因此这里是恒等操作，
// 重建不变量不受影响。
void fillInteriorHoles(cv::Mat& frame, cv::Mat& owner) {
    const int W = frame.cols, H = frame.rows;
    if (W < 3 || H < 3) return;   // nothing can be enclosed / 不可能有封闭区

    static const int dx[4] = {-1, 1, 0, 0};
    static const int dy[4] = {0, 0, -1, 1};

    // 1. Flood the open background inward from the border.
    // 1. 从边界向内灌入开放背景。
    cv::Mat open(H, W, CV_8U, cv::Scalar(0));
    std::vector<cv::Point> stack;
    auto tryPush = [&](int x, int y) {
        if (x < 0 || x >= W || y < 0 || y >= H) return;
        if (open.at<uchar>(y, x)) return;
        if (frame.at<cv::Vec4b>(y, x)[3] != 0) return;   // opaque stops the flood / 不透明挡住灌水
        open.at<uchar>(y, x) = 1;
        stack.push_back(cv::Point(x, y));
    };
    for (int x = 0; x < W; ++x) { tryPush(x, 0); tryPush(x, H - 1); }
    for (int y = 0; y < H; ++y) { tryPush(0, y); tryPush(W - 1, y); }
    while (!stack.empty()) {
        const cv::Point p = stack.back();
        stack.pop_back();
        for (int k = 0; k < 4; ++k) tryPush(p.x + dx[k], p.y + dy[k]);
    }

    // First opaque neighbour in a fixed order, so the fill is deterministic.
    // Also hands back that neighbour's owner, so a repaired pixel inherits the
    // part it was repaired FROM and the tear pass can still tell parts apart.
    // 按固定顺序找第一个不透明邻居，保证填充结果确定。同时带回该邻居的归属，
    // 让补出的像素继承【它所修复的那个部件】，撕裂判定才能继续分辨部件。
    auto opaqueNeighbour = [&](int x, int y, cv::Vec4b& out, uchar& outOwner) {
        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            const cv::Vec4b s = frame.at<cv::Vec4b>(ny, nx);
            if (s[3] == 0) continue;
            out = s;
            outOwner = owner.at<uchar>(ny, nx);
            return true;
        }
        return false;
    };

    // 2. Everything transparent the flood never reached is a seam. Seed the
    //    queue with the ones already touching an opaque pixel.
    // 2. 灌水没到的透明像素都是缝。先把已挨着不透明像素的入队。
    cv::Mat isSeam(H, W, CV_8U, cv::Scalar(0));
    std::vector<cv::Point> queue;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (frame.at<cv::Vec4b>(y, x)[3] != 0) continue;
            if (open.at<uchar>(y, x)) continue;
            isSeam.at<uchar>(y, x) = 1;
            cv::Vec4b c;
            uchar o = 0;
            if (opaqueNeighbour(x, y, c, o)) queue.push_back(cv::Point(x, y));
        }
    }

    // 3. Fill frontier-first; each filled pixel turns opaque and pulls its own
    //    seam neighbours into the queue, so the fill marches inward.
    // 3. 从边界先填；每个填出的像素变为不透明，把相邻的缝像素也拉进队列，
    //    于是填充向内推进。
    for (size_t head = 0; head < queue.size(); ++head) {
        const int x = queue[head].x, y = queue[head].y;
        if (!isSeam.at<uchar>(y, x)) continue;    // already filled / 已填过
        cv::Vec4b c;
        uchar o = 0;
        if (!opaqueNeighbour(x, y, c, o)) continue;  // still interior, retry later / 仍在内部，稍后再来
        frame.at<cv::Vec4b>(y, x) = c;
        owner.at<uchar>(y, x) = o;
        isSeam.at<uchar>(y, x) = 0;
        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            if (isSeam.at<uchar>(ny, nx)) queue.push_back(cv::Point(nx, ny));
        }
    }
}

// Repair TEARS — the second class of seam, which the flood above cannot see.
//
// A tear is a run of transparent pixels in one column that is boxed in
// VERTICALLY: opaque directly above the run and opaque directly below it.
// That is the signature of two parts that moved differently and pulled apart
// at their joint. It is not enclosed — the run usually reaches the open
// background to the side — so the flood above never touches it, and the
// detach gate (1 blob / 0 holes) cannot see it either.
//
// The discriminator needs BOTH tests / 判据需要【两个条件同时成立】:
//
//   (1) OWNERSHIP — the two opaque ends belong to different parts. One part
//       cannot tear itself: it is rasterised by a continuous transform, so the
//       pixels it owns stay one piece. If both ends are the same part, the gap
//       is what the art draws — the front's leg outline steps in at (11,24)
//       and the step moves with the leg under the idle bob without ever being
//       damage.
//   （1）【归属】—— 两端分属不同部件。一个部件撕不开自己：它由连续变换光栅化，
//        拥有的像素始终连成一片。两头同属一个部件时，缺口就是画出来的 ——
//        正面的腿轮廓在 (11,24) 收进一格，整体起伏时它跟着腿走，从来不是损伤。
//
//   (2) THE SOURCE had those pixels opaque — the neutral pose carried a pixel
//       there and this frame dropped it. Measured in the SOURCE'S OWN FRAME:
//       the query is offset by the pose's global translation (rootX, rootY).
//   （2）【原图】该处不透明 —— 静止姿势本来有这个像素，这一帧把它弄丢了。
//        查询在【原图自己的参考系】里做：按位姿的整体平移 (rootX, rootY) 反向
//        偏移后再去原图取。
//
// Each test alone is WRONG, and both counterexamples are in this project
// already / 单用任何一个都错，两个反例本项目里都有:
//
//   (1) alone redraws the character. The back view steps its hair outline in
//       at (10,13) — head above, arm_left below, i.e. CROSS-PART, exactly at
//       the shoulder where the two parts do abut. It is a drawn pixel, not a
//       seam, and it must survive or the reconstruction invariant dies.
//   （1）单用会重画角色。背视在 (10,13) 把头发描边收进一格 —— 上面是 head、
//        下面是 arm_left，【跨部件】，而且就在两部件相接的肩部。那是画出来的
//        像素，不是缝；它必须活下来，否则重建不变量就没了。
//
//   (2) alone is anchored to coordinates, so it compares a gap against the
//       pixel the neutral pose had SOMEWHERE ELSE once the character moves.
//       Measured on the idle bob — every part shifted by the same 2px, which
//       cannot tear anything — an unshifted query reported damage at (11,22)
//       and filled a hole the artist drew.
//   （2）单用锚死在坐标上，角色一动，它就拿空隙去比静止姿势在【别处】的像素。
//        实测整体起伏 —— 所有部件位移同为 2px，根本撕不开任何东西 —— 不做偏移的
//        查询照样在 (11,22) 报出损伤，并把画师画的缺口填掉。
//
// The offset in (2) is what disposes of that counterexample. The source is
// drawn in the character's neutral frame; a frame that is only a global bob
// away from it must be compared against the source shifted by the same
// amount. With the offset, a pure translation can never report damage — the
// frame IS the source moved, so every transparent run lands on a transparent
// source pixel and fails (2). That is what makes the idle animation provably
// untouched by this pass, and it is checked, not assumed: --idle-purity
// asserts every idle frame is an exact translation of the source.
// （2）里的偏移正是那个反例的解法。原图画在角色的静止参考系里；画面若只是整体
//      起伏的结果，就必须拿【同样平移过】的原图去比。加上偏移后，纯平移不可能
//      报出损伤 —— 画面就是移动过的原图，任何透明段都落在原图的透明像素上，
//      过不了 (2)。这使待机动作可证明不受本函数影响；而且这是【验过】的，不是
//      假设的：--idle-purity 断言每一帧待机都是原图的精确平移。
//
// Together they are exact on every case measured, and (2) still makes this a
// PROVABLE no-op at an identity pose (the offset is zero there and the frame
// IS the source). That is what keeps the reconstruction invariant intact.
// 两者合用在所有实测情形上都正确；(2) 仍让本函数在单位位姿下【可证明】是恒等
// 操作（那里偏移为零，画面就是原图）。重建不变量因此不受影响。
//
// Why vertical only / 为何只补竖向:
//   The safe primitives pin the JOINTS — neck, shoulder, hip — which stack
//   parts vertically, so a vertical squeeze across parts is always damage. A
//   horizontal gap is how a swinging limb reads; closing those would glue an
//   arm back onto the torso and erase the motion.
//   安全动作钉住的是【关节】——颈、肩、胯 —— 它们把部件竖着摞起来，所以跨部件的
//   竖向受压必然是损伤。横向的缝正是摆动的肢体该有的样子；补上它等于把手臂粘回
//   躯干，动作就没了。
//
// Colour: each pixel of the run takes the colour of the end it is nearer — the
// top of the run continues the part above, the bottom continues the part below.
// A 1px run therefore takes the upper part's colour. Neighbours' own colours
// only, never a blend and never an invented one (rule 4).
// 颜色：段上每一格取【离它更近的那一端】的颜色 —— 段的上半接着上面那个部件，
// 下半接着下面那个。一格的段于是取上面部件的颜色。只用邻居自己的颜色，
// 不混合、不发明颜色（规则 4）。
//
// Under an identity pose every part sits exactly on its neighbours, so no
// cross-part run exists and this is a no-op. That is what keeps the
// reconstruction invariant intact — checked by --recon-test on all three rigs.
// 单位位姿下每个部件都严丝合缝地贴着邻居，不存在跨部件的空段，于是这里是恒等
// 操作。重建不变量因此不受影响 —— 三套 rig 的 --recon-test 都验过。
void fillJointPinches(cv::Mat& frame, const cv::Mat& owner, const cv::Mat& source,
                      int shiftX, int shiftY,
                      const std::vector<cv::Rect>& hiddenBoxes) {
    const int W = frame.cols, H = frame.rows;
    const bool haveSource = !source.empty()
        && source.rows == H && source.cols == W;

    for (int x = 0; x < W; ++x) {
        int y = 0;
        while (y < H) {
            if (frame.at<cv::Vec4b>(y, x)[3] != 0) { ++y; continue; }
            const int y0 = y;
            while (y < H && frame.at<cv::Vec4b>(y, x)[3] == 0) ++y;
            const int y1 = y - 1;

            // Boxed in vertically: opaque above the run and opaque below it.
            // 竖向被夹住：段的正上方与正下方都不透明。
            if (y0 == 0 || y1 == H - 1) continue;
            const cv::Vec4b above = frame.at<cv::Vec4b>(y0 - 1, x);
            const cv::Vec4b below = frame.at<cv::Vec4b>(y1 + 1, x);
            if (above[3] == 0 || below[3] == 0) continue;

            // Test 1 — cross-part. Same owner on both ends => that one part
            // drew the gap. Keep it.
            // 条件 1 —— 跨部件。两头同一归属 => 缺口是那一个部件画的。保留。
            const uchar oa = owner.at<uchar>(y0 - 1, x);
            const uchar ob = owner.at<uchar>(y1 + 1, x);
            if (oa == ob) continue;

            // Test 2 — the neutral pose carried pixels here, QUERIED IN THE
            // SOURCE'S OWN FRAME. Without a source there is no way to tell art
            // from artifact, so repair nothing. A query that falls outside the
            // source counts as transparent: there is no evidence the pixel was
            // ever there, and under-filling leaves a visible tear while
            // over-filling silently redraws art — so err toward not filling.
            // 条件 2 —— 静止姿势此处有像素，【在原图自己的参考系里查】。没有原图
            // 就分不清画法与损伤，宁可不补。查到原图之外算作透明：没有证据表明
            // 那里曾有过像素，而少补只留一道看得见的裂，多补却悄悄重画了原画
            // —— 所以宁可不补。
            if (!haveSource) continue;
            bool lost = true;
            for (int yy = y0; yy <= y1; ++yy) {
                const int syy = yy - shiftY, sxx = x - shiftX;
                if (syy < 0 || syy >= H || sxx < 0 || sxx >= W) { lost = false; break; }
                if (source.at<cv::Vec4b>(syy, sxx)[3] == 0) { lost = false; break; }
                // A part hidden this frame leaves its source pixels transparent
                // ON PURPOSE (the far arm the torso occludes). Restoring them
                // would redraw the arm we just hid, so treat these as not-lost.
                // 本帧被隐藏的部件【故意】让它的源像素透明（被躯干遮挡的远端手臂）。
                // 还原它们会把刚隐藏的手臂重画出来，所以当作"没丢"。
                bool inHidden = false;
                for (const cv::Rect& hb : hiddenBoxes) {
                    if (hb.contains(cv::Point(sxx, syy))) { inHidden = true; break; }
                }
                if (inHidden) { lost = false; break; }
            }
            if (!lost) continue;

            for (int yy = y0; yy <= y1; ++yy) {
                frame.at<cv::Vec4b>(yy, x) =
                    ((yy - y0) <= (y1 - yy)) ? above : below;
            }
        }
    }
}

// One palette-swap pass: replace each `from` colour with its `to`. Exact BGRA
// match — pixel art has no blending, so a pixel is either the tone or it is not.
// Drives the pants two-tone trade (ANIMATION_SPEC §5); leg shapes stay put.
// 一次调色板换色：把每个 `from` 色替换成 `to`。精确 BGRA 匹配 —— 像素风无混合，
// 像素要么是该色、要么不是。驱动裤子两主色互换（ANIMATION_SPEC §5）；腿形不动。
void applySwaps(cv::Mat& frame,
                const std::vector<std::pair<cv::Vec4b, cv::Vec4b>>& swaps) {
    if (swaps.empty()) return;
    for (int y = 0; y < frame.rows; ++y) {
        cv::Vec4b* row = frame.ptr<cv::Vec4b>(y);
        for (int x = 0; x < frame.cols; ++x) {
            for (const auto& sw : swaps) {
                if (row[x] == sw.first) { row[x] = sw.second; break; }
            }
        }
    }
}

}  // namespace

cv::Mat composeFrame(const Rig& rig, const Pose& pose) {
    cv::Mat frame = cv::Mat::zeros(rig.size.height, rig.size.width, CV_8UC4);
    // Who painted each pixel (0 = nobody). The seam repairs need it.
    // 每个像素是谁画的（0 = 没人）。补缝需要它。
    cv::Mat owner = cv::Mat::zeros(rig.size.height, rig.size.width, CV_8U);

    // Source-space boxes of parts hidden this frame. A hidden part's pixels are
    // still opaque in the source, so the tear repair must not "restore" them.
    // 本帧隐藏部件的原图包围盒。隐藏部件在原图里仍不透明，补缝不能把它们"还原"。
    std::vector<cv::Rect> hiddenBoxes;

    for (int zi = 0; zi < PART_COUNT; ++zi) {
        const int i = rig.zOrder[zi] - 1;          // zOrder stores part ids
        if (i < 0 || i >= PART_COUNT) continue;
        if (!rig.present[i]) continue;
        if (pose.hidden[i]) { hiddenBoxes.push_back(rig.bbox[i]); continue; }

        const Affine2D m = Affine2D::from(pose.parts[i], rig.pivots[i],
                                          pose.rootX, pose.rootY);
        blitPart(frame, owner, i + 1, rig.parts[i], rig.bbox[i], m);
    }
    // Per-frame palette swap (the pants two-tone trade). Applied here, after
    // rasterising and BEFORE the seam repairs, so repaired pixels inherit the
    // already-swapped colours rather than a stale tone.
    // 逐帧调色板换色（裤子两主色互换）。在此应用 —— 光栅化后、缝修补【前】，
    // 让修补出的像素继承换过的颜色，而不是旧色。
    applySwaps(frame, pose.swaps);
    fillInteriorHoles(frame, owner);                  // enclosed seams / 封闭缝
    // Tears are judged against the source in ITS OWN frame, so undo the pose's
    // global translation before looking a pixel up. Rounds the same way the
    // rasteriser does, or the two would disagree at the halves.
    // 撕裂要拿【原图自己的参考系】来判，所以查像素前先撤掉位姿的整体平移。
    // 取整方式与光栅化器一致，否则在 .5 处会对不上。
    fillJointPinches(frame, owner, rig.source,
                     (int)std::lround(pose.rootX), (int)std::lround(pose.rootY),
                     hiddenBoxes);
    return frame;
}

ReconResult reconstructAndCompare(const Rig& rig, cv::Mat* outRecon) {
    ReconResult r;
    Pose identity;   // all transforms neutral / 全部变换为单位
    cv::Mat recon = composeFrame(rig, identity);
    if (outRecon) *outRecon = recon;

    if (recon.size() != rig.source.size()) {
        r.totalPixels = rig.source.rows * rig.source.cols;
        r.diffPixels  = r.totalPixels;   // size mismatch is a total failure / 尺寸不符即全错
        return r;
    }

    r.totalPixels = recon.rows * recon.cols;
    for (int y = 0; y < recon.rows; ++y) {
        const cv::Vec4b* a = recon.ptr<cv::Vec4b>(y);
        const cv::Vec4b* b = rig.source.ptr<cv::Vec4b>(y);
        for (int x = 0; x < recon.cols; ++x) {
            if (a[x] == b[x]) continue;
            ++r.diffPixels;
            // A pixel that differs only where the SOURCE is fully transparent is
            // benign: a transparent pixel's RGB carries no information.
            // 仅在【源】全透明处不同的像素是良性的：透明像素的 RGB 不携带信息。
            if (b[x][3] == 0 && a[x][3] == 0) ++r.alphaOnlyDiff;
        }
    }
    return r;
}

}  // namespace fb
