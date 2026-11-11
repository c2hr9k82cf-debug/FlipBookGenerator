// backinfer.cpp — back-view inference (v0.9.2).
// 背面推断（v0.9.2）。
//
// Algorithm (all deterministic, integer pixels, no blending):
//  1. The back silhouette is the front silhouette (alpha copied verbatim).
//  2. Hair-class pixels (hair_mid / hair_dark / hair_highlight) are mirrored
//     horizontally (x -> width-1-x) and composited LAST (hair is the topmost
//     layer per ANIMATION_SPEC §7).
//  3. Head rows: everything else opaque becomes the rear hair mass (hair_mid),
//     except silhouette-edge outline pixels which stay (the drawn contour).
//     This is what covers the face — measured: back skin starts at y14.
//  4. Body/leg rows: per back_policy — keep roles copy through, flatten:<role>
//     replaces with the base role's palette colour (gold/red buttons and
//     collar detail vanish into the garment colour).
// 算法（全程确定性、整数像素、无混合）：
//  1. 背面轮廓 = 正面轮廓（alpha 逐字节照搬）。
//  2. 头发类像素（hair_mid/hair_dark/hair_highlight）水平镜像
//     （x -> width-1-x），最后合成（ANIMATION_SPEC §7：头发是最上层）。
//  3. 头部行：其余不透明像素变为后脑头发（hair_mid），剪影边缘的 outline 像素
//     保留（画出来的轮廓线）。这层盖住脸 —— 实测背面皮肤从 y14 起。
//  4. 躯干/腿行：按 back_policy —— keep 角色照搬，flatten:<role> 换成基色
//     （扣子、领饰消失进衣服基色）。
//
// Role disambiguation: hair_highlight and gold share RGB (249,194,43). Inside
// head rows the colour is hair_highlight (mirrors); elsewhere it is gold
// (flattens to mint). Region decides — roles are semantic, pixels are not.
// 角色消歧：hair_highlight 与 gold 同 RGB（249,194,43）。头部行内该色是
// hair_highlight（镜像）；其余行是 gold（压平为 mint）。由区域决定 —— 角色有
// 语义，像素没有。
#include "core/backinfer.h"

#include <map>
#include <set>
#include <vector>

namespace fb {
namespace {

// cv::Vec3b has no operator<, so colours are keyed as packed ints.
// cv::Vec3b 没有 operator<，颜色用打包整数作键。
int keyOf(const cv::Vec3b& c) {
    return (int(c[0]) << 16) | (int(c[1]) << 8) | int(c[2]);
}

// Build colour -> role lookup. Two roles can share a colour; the map keeps one
// canonical name and callers disambiguate by region.
// 建 颜色 -> role 反查。两个角色可同色；表里保留一个规范名，调用方按区域消歧。
std::map<int, std::string> buildRoleLookup(const TemplateSpec& spec) {
    std::map<int, std::string> byColour;
    for (const auto& kv : spec.palette) {
        // Prefer the non-gold name when colours collide: gold is resolved by
        // region in roleAt() below.
        // 同色冲突时优先非 gold 名：gold 在下面的 roleAt() 里按区域判定。
        const int k = keyOf(kv.second);
        auto it = byColour.find(k);
        if (it == byColour.end() || it->second == "gold") byColour[k] = kv.first;
    }
    return byColour;
}

bool inRows(const std::vector<int>& rows, int y) {
    for (int r : rows)
        if (r == y) return true;
    return false;
}

// Resolve the semantic role of one opaque pixel. / 解析一个不透明像素的语义角色。
std::string roleAt(const TemplateSpec& spec,
                   const std::map<int, std::string>& byColour,
                   const cv::Vec4b& px, int y) {
    auto it = byColour.find(keyOf(cv::Vec3b(px[0], px[1], px[2])));
    if (it == byColour.end()) return "";
    // Colour collision: (249,194,43) is hair_highlight on the head, gold
    // elsewhere. / 同色冲突：(249,194,43) 头部是 hair_highlight，别处是 gold。
    if (it->second == "hair_highlight" || it->second == "gold") {
        return inRows(spec.views[0].map.headRows, y) ? "hair_highlight" : "gold";
    }
    return it->second;
}

bool isHairRole(const std::string& r) {
    return r == "hair_mid" || r == "hair_dark" || r == "hair_highlight";
}

}  // namespace

cv::Mat inferBackView(const cv::Mat& front, const cv::Mat& side,
                      const TemplateSpec& spec, std::string& err) {
    (void)side;  // reserved for shoulder-width correction (v1 unused)
                 // 预留肩宽校正（v1 未用）
    if (front.empty()) {
        err = "inferBackView: front image is empty";
        return {};
    }
    if (front.size() != spec.size || front.type() != CV_8UC4) {
        err = "inferBackView: front must be " + std::to_string(spec.size.width) +
              "x" + std::to_string(spec.size.height) + " BGRA";
        return {};
    }

    const std::map<int, std::string> byColour = buildRoleLookup(spec);
    const std::set<std::string> hairRoles = {"hair_mid", "hair_dark", "hair_highlight"};

    cv::Vec3b hairBase(0, 0, 0);
    if (!spec.roleBgr("hair_mid", hairBase)) {
        err = "inferBackView: palette has no 'hair_mid'";
        return {};
    }

    const int w = spec.size.width, h = spec.size.height;
    cv::Mat out = cv::Mat::zeros(spec.size, CV_8UC4);

    // Mirror buffer for hair pixels (collected first, composited last).
    // 头发像素镜像缓冲（先收集，最后合成）。
    struct HairPx { int x, y; cv::Vec4b px; };
    std::vector<HairPx> hair;

    // Kept contour pixels (drawn edge outline) — the mirror layer must not
    // paint over them. Keyed y*w+x.
    // 保留的轮廓像素（画出的边缘描边）—— 镜像层不得覆盖。键为 y*w+x。
    std::set<int> keptOutline;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const cv::Vec4b px = front.at<cv::Vec4b>(y, x);
            if (px[3] == 0) continue;   // silhouette: transparent stays / 轮廓：透明保持

            const std::string role = roleAt(spec, byColour, px, y);
            const auto bit = spec.backPolicy.find(role);
            const std::string rule =
                (bit != spec.backPolicy.end()) ? bit->second : "keep";

            if (isHairRole(role) || rule == "mirror") {
                // Hair (or any mirror-policy role) goes to the mirror layer.
                // Hair's origin pixel becomes the hair base (the rear mass);
                // any other mirror role keeps its own colour in place — only
                // hair has a "rear base" concept.
                // 头发（或任何 mirror 策略角色）进镜像层。头发的原位变发底
                // （后脑）；其余 mirror 角色原位照搬自己的颜色 —— 只有头发
                // 才有"后脑底色"的概念。
                hair.push_back({x, y, px});
                out.at<cv::Vec4b>(y, x) = isHairRole(role)
                    ? cv::Vec4b(hairBase[0], hairBase[1], hairBase[2], 255)
                    : px;
                continue;
            }

            if (inRows(spec.views[0].map.headRows, y)) {
                // Head rows: the face is covered by the rear hair mass. Only
                // the drawn contour (outline touching transparency) survives.
                // 头部行：脸被后脑头发盖住。只有画出来的轮廓（邻接透明的
                // outline）保留。
                if (role == "outline") {
                    bool edge = false;
                    const int nb[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                    for (const auto& d : nb) {
                        const int nx = x + d[0], ny = y + d[1];
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h ||
                            front.at<cv::Vec4b>(ny, nx)[3] == 0) {
                            edge = true;
                            break;
                        }
                    }
                    if (edge) {
                        out.at<cv::Vec4b>(y, x) = px;
                        keptOutline.insert(y * w + x);
                        continue;
                    }
                }
                out.at<cv::Vec4b>(y, x) = cv::Vec4b(hairBase[0], hairBase[1], hairBase[2], 255);
                continue;
            }

            // Body / legs: back_policy keep / flatten:<role>. Unknown roles
            // default to keep (custom colours pass through untouched).
            // 躯干 / 腿：back_policy 的 keep / flatten:<role>。未知角色默认
            // keep（自定义色原样穿过）。
            if (rule.rfind("flatten:", 0) == 0) {
                const std::string target = rule.substr(8);
                cv::Vec3b dst;
                if (!spec.roleBgr(target, dst)) {
                    err = "inferBackView: back_policy flattens '" + role +
                          "' to unknown role '" + target + "'";
                    return {};
                }
                out.at<cv::Vec4b>(y, x) = cv::Vec4b(dst[0], dst[1], dst[2], 255);
            } else {
                out.at<cv::Vec4b>(y, x) = px;   // keep / 照搬
            }
        }
    }

    // Hair layer last, mirrored. Hair is the topmost layer — but it stays
    // inside the silhouette (the alpha invariant: the back's contour IS the
    // front's) and never paints over a kept contour pixel.
    // 头发层最后合成，镜像。头发是最上层 —— 但不得出轮廓（alpha 不变量：
    // 背面轮廓 = 正面轮廓），也不得盖住保留的轮廓像素。
    for (const HairPx& hp : hair) {
        const int xm = w - 1 - hp.x;
        if (keptOutline.count(hp.y * w + xm)) continue;
        cv::Vec4b& dst = out.at<cv::Vec4b>(hp.y, xm);
        if (dst[3] == 0) continue;   // outside the front silhouette / 出正面轮廓
        dst = hp.px;
    }
    return out;
}

}  // namespace fb
