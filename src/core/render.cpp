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

    const int x0 = std::max(0, (int)std::floor(minXd));
    const int y0 = std::max(0, (int)std::floor(minYd));
    const int x1 = std::min(frame.cols - 1, (int)std::ceil(maxXd));
    const int y1 = std::min(frame.rows - 1, (int)std::ceil(maxYd));

    for (int y = y0; y <= y1; ++y) {
        cv::Vec4b* dstRow = frame.ptr<cv::Vec4b>(y);
        for (int x = x0; x <= x1; ++x) {
            float px, py;
            m.inverse((float)x + 0.5f, (float)y + 0.5f, px, py);

            // Nearest source pixel, inside the part's bbox.
            // 最近源像素，须落在部件包围盒内。
            const int sx = (int)std::floor(px);
            const int sy = (int)std::floor(py);
            if (sx < bbox.x || sy < bbox.y) continue;
            if (sx >= bbox.x + bbox.width || sy >= bbox.y + bbox.height) continue;

            const cv::Vec4b s = sprite.at<cv::Vec4b>(sy - bbox.y, sx - bbox.x);
            if (s[3] == 0) continue;   // fully transparent / 全透明

            // Overwrite, no blending. z-order decides.
            // 直接覆盖，不混合。由 zOrder 决定。
            dstRow[x] = s;
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
