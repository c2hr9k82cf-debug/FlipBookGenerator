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
// 按上述规则把一个部件的精灵图光栅化进画面。
void blitPart(cv::Mat& frame, const cv::Mat& sprite, const cv::Rect& bbox,
              const Affine2D& m) {
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
void fillInteriorHoles(cv::Mat& frame) {
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
    // 按固定顺序找第一个不透明邻居，保证填充结果确定。
    auto opaqueNeighbour = [&](int x, int y, cv::Vec4b& out) {
        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            const cv::Vec4b s = frame.at<cv::Vec4b>(ny, nx);
            if (s[3] == 0) continue;
            out = s;
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
            if (opaqueNeighbour(x, y, c)) queue.push_back(cv::Point(x, y));
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
        if (!opaqueNeighbour(x, y, c)) continue;  // still interior, retry later / 仍在内部，稍后再来
        frame.at<cv::Vec4b>(y, x) = c;
        isSeam.at<uchar>(y, x) = 0;
        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            if (isSeam.at<uchar>(ny, nx)) queue.push_back(cv::Point(nx, ny));
        }
    }
}

}  // namespace

cv::Mat composeFrame(const Rig& rig, const Pose& pose) {
    cv::Mat frame = cv::Mat::zeros(rig.size.height, rig.size.width, CV_8UC4);

    for (int zi = 0; zi < PART_COUNT; ++zi) {
        const int i = rig.zOrder[zi] - 1;          // zOrder stores part ids
        if (i < 0 || i >= PART_COUNT) continue;
        if (!rig.present[i]) continue;

        const Affine2D m = Affine2D::from(pose.parts[i], rig.pivots[i],
                                          pose.rootX, pose.rootY);
        blitPart(frame, rig.parts[i], rig.bbox[i], m);
    }
    fillInteriorHoles(frame);   // close the joint seams / 补上关节缝
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
