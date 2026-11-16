// compose.cpp — recipe + library -> three views (v0.10.1).
// 配方 + 部件库 -> 三视图（v0.10.1）。
#include "core/compose.h"

#include "core/backinfer.h"
#include "core/json_min.h"
#include "core/parts.h"

#include <fstream>
#include <sstream>

namespace fb {
namespace {

// JSON [R,G,B] -> cv::Vec3b BGR. / JSON [R,G,B] -> cv::Vec3b BGR。
bool parseRgb(const JsonValue& v, const std::string& where, cv::Vec3b& out,
              std::string& err) {
    if (v.type != JsonValue::Array || v.arr.size() != 3) {
        err = where + " must be an [R,G,B] array";
        return false;
    }
    int ch[3];
    for (int i = 0; i < 3; ++i) {
        if (v.arr[i].type != JsonValue::Int || v.arr[i].i < 0 || v.arr[i].i > 255) {
            err = where + " has a channel outside 0..255";
            return false;
        }
        ch[i] = (int)v.arr[i].i;
    }
    out = cv::Vec3b((uchar)ch[2], (uchar)ch[1], (uchar)ch[0]);
    return true;
}

// Override -> PartLayer::RoleColor. src falls back to the template palette's
// value for the role (that is what "omit src" means).
// Override -> PartLayer::RoleColor。src 缺省时取模板 palette 里该 role 的值
// （"省略 src"的语义就是这个）。
bool toRoleColor(const LibraryItem::RoleColorOverride& ro,
                 const TemplateSpec& spec, PartLayer::RoleColor& out,
                 std::string& err) {
    out.role = ro.role;
    out.dstBgr = ro.dstBgr;
    if (ro.hasSrc) {
        out.srcBgr = ro.srcBgr;
        return true;
    }
    if (!spec.roleBgr(ro.role, out.srcBgr)) {
        err = "role '" + ro.role +
              "' has no 'src' colour and is not in the template palette";
        return false;
    }
    return true;
}

bool readWholeFile(const std::string& path, std::string& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "cannot open file: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string rgbJson(const cv::Vec3b& bgr) {
    return "[" + std::to_string((int)bgr[2]) + "," + std::to_string((int)bgr[1]) +
           "," + std::to_string((int)bgr[0]) + "]";
}

bool parseRoleOverride(const JsonValue& e, const std::string& where,
                       LibraryItem::RoleColorOverride& ro, std::string& err) {
    if (e.type != JsonValue::Object) {
        err = where + " must be an object";
        return false;
    }
    const JsonValue* role = e.find("role");
    if (!role || role->type != JsonValue::String || role->s.empty()) {
        err = where + " needs a non-empty string 'role'";
        return false;
    }
    ro.role = role->s;
    if (const JsonValue* src = e.find("src")) {
        if (!parseRgb(*src, where + "/src", ro.srcBgr, err)) return false;
        ro.hasSrc = true;
    }
    const JsonValue* dst = e.find("dst");
    if (!dst) {
        err = where + " needs 'dst' ([R,G,B])";
        return false;
    }
    return parseRgb(*dst, where + "/dst", ro.dstBgr, err);
}

}  // namespace

bool loadComposeRecipe(const std::string& path, ComposeRecipe& out,
                       std::string& err) {
    std::string text;
    if (!readWholeFile(path, text, err)) return false;
    JsonValue root;
    if (!jsonParse(text, root, err)) {
        err = path + ": " + err;
        return false;
    }
    if (root.type != JsonValue::Object) {
        err = path + ": root must be an object";
        return false;
    }
    out = ComposeRecipe{};
    if (const JsonValue* t = root.find("template")) {
        if (t->type != JsonValue::String || t->s.empty()) {
            err = path + ": 'template' must be a non-empty string";
            return false;
        }
        out.templateName = t->s;
    }
    if (const JsonValue* items = root.find("items")) {
        if (items->type != JsonValue::Object) {
            err = path + ": 'items' must be an object {slot: item_name}";
            return false;
        }
        for (const auto& kv : items->obj) {
            bool known = false;
            for (const std::string& s : librarySlotNames())
                if (s == kv.first) known = true;
            if (!known) {
                err = path + ": unknown slot '" + kv.first + "'";
                return false;
            }
            if (kv.second.type != JsonValue::String || kv.second.s.empty()) {
                err = path + ": items." + kv.first + " must be a non-empty string";
                return false;
            }
            out.items[kv.first] = kv.second.s;
        }
    }
    if (const JsonValue* ov = root.find("roleOverrides")) {
        if (ov->type != JsonValue::Array) {
            err = path + ": 'roleOverrides' must be an array";
            return false;
        }
        for (size_t i = 0; i < ov->arr.size(); ++i) {
            LibraryItem::RoleColorOverride ro;
            const std::string where =
                path + "/roleOverrides[" + std::to_string(i) + "]";
            if (!parseRoleOverride(ov->arr[i], where, ro, err)) return false;
            out.roleOverrides.push_back(ro);
        }
    }
    return true;
}

bool saveComposeRecipe(const std::string& path, const ComposeRecipe& in,
                       std::string& err) {
    std::ostringstream ss;
    ss << "{\n  \"template\": \"" << in.templateName << "\",\n  \"items\": {";
    bool first = true;
    for (const auto& kv : in.items) {
        ss << (first ? "\n" : ",\n") << "    \"" << kv.first << "\": \""
           << kv.second << "\"";
        first = false;
    }
    if (!first) ss << "\n  ";
    ss << "},\n  \"roleOverrides\": [";
    for (size_t i = 0; i < in.roleOverrides.size(); ++i) {
        const auto& ro = in.roleOverrides[i];
        ss << (i ? ",\n" : "\n") << "    {\"role\": \"" << ro.role << "\"";
        if (ro.hasSrc) ss << ", \"src\": " << rgbJson(ro.srcBgr);
        ss << ", \"dst\": " << rgbJson(ro.dstBgr) << "}";
    }
    if (!in.roleOverrides.empty()) ss << "\n  ";
    ss << "]\n}\n";

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        err = "cannot write file: " + path;
        return false;
    }
    f << ss.str();
    return true;
}

bool composeThreeViews(const ComposeRecipe& recipe, const PartsLibrary& lib,
                       const TemplateSpec& spec, cv::Mat out[3],
                       std::string& err) {
    for (int v = 0; v < 3; ++v) {
        // compositeCharacter stamps full-frame sprites at (0,0) in z-order:
        // alpha>0 wins, no blending. The library views are already in
        // template coords, so this is pure z-order composition.
        // compositeCharacter 把全幅精灵贴在 (0,0)、按 z 序：alpha>0 胜出、不混合。
        // 库视图已在模板坐标里，所以这就是纯 z 序合成。
        CharacterSpec cs;
        cs.templateName = spec.name;
        cs.frameSize = spec.size;

        for (const std::string& slot : librarySlotNames()) {
            const auto it = recipe.items.find(slot);
            if (it == recipe.items.end() || it->second.empty()) continue;
            const LibraryItem* item = lib.find(slot, it->second);
            if (!item) {
                err = "item not in library: " + slot + "/" + it->second;
                return false;
            }

            // Resolve this view's TWO layers (v0.12.0 plan D3): a back-layer
            // piece (hair back-of-head — the bedding under clothes and face)
            // and the main piece (on top). A single-piece item stamps its
            // main sprite twice, pixel-identical to the old single-layer
            // look. On the back view with back:"auto", the pieces CROSS: the
            // front piece's inference becomes the bedding and the back
            // piece's inference becomes the top layer (front hair mirrors to
            // the bedding, back hair mirrors to the top — the user's
            // "reverse the order, flip horizontally" rule).
            // 解析本视图的【两层】（v0.12.0 计划 D3）：垫底片（hair 后发片 ——
            // 衣服和脸下面的衬底）与主片（盖顶）。单片件主片贴两次 —— 与旧单层
            // 观感逐像素一致。背面 + back:"auto" 时两片【交叉】：前发（主片）的
            // 推断变垫底、后发（垫底片）的推断变盖顶（前发镜像垫底、后发镜像
            // 盖顶 —— 用户的"顺序反过来、水平翻转"规则）。
            cv::Mat mainPiece, backPiece;
            if (v == 2 && item->backMode == "auto") {
                if (slot == "hair") {
                    // Plan D3: hair pieces are PURE horizontal mirrors of the
                    // front pieces — the front piece's mirror beds down, the
                    // back piece's mirror covers (the user's "flip once"
                    // rule). The old whole-item inference (mirror + rear-mass
                    // fill) is a single-layer relic and is not used here.
                    // 计划 D3：发片是正面片的【纯水平镜像】—— 前发镜像垫底、
                    // 后发镜像盖顶（用户的"翻转一次"规则）。旧的整件推断
                    // （镜像 + 后脑发底填充）是单层时代的遗物，此处不再使用。
                    cv::flip(item->views[0], backPiece, 1);
                    if (item->hasB[0])
                        cv::flip(item->viewsB[0], mainPiece, 1);
                    else
                        mainPiece = backPiece.clone();
                } else {
                    std::string ierr;
                    backPiece = inferBackView(item->views[0], item->views[1],
                                              spec, ierr);
                    if (backPiece.empty()) {
                        err = "back inference failed for " + slot + "/" +
                              item->name + ": " + ierr;
                        return false;
                    }
                    if (item->hasB[0]) {
                        const cv::Mat& sideB =
                            item->hasB[1] ? item->viewsB[1] : item->views[1];
                        mainPiece = inferBackView(item->viewsB[0], sideB,
                                                  spec, ierr);
                        if (mainPiece.empty()) {
                            err = "back-piece inference failed for " + slot +
                                  "/" + item->name + ": " + ierr;
                            return false;
                        }
                    } else {
                        mainPiece = backPiece.clone();
                    }
                }
            } else {
                mainPiece = item->views[v];
                if (mainPiece.empty()) {
                    err = "item " + slot + "/" + item->name + " has no " +
                          (v == 2 ? "back" : (v == 0 ? "front" : "side")) +
                          " sprite";
                    return false;
                }
                backPiece = item->hasB[v] ? item->viewsB[v] : item->views[v];
            }

            for (int piece = 0; piece < 2; ++piece) {
                PartLayer layer;
                layer.slot = slot;
                layer.name = item->name;
                layer.sprite = (piece == 0 ? backPiece : mainPiece).clone();
                layer.anchor = cv::Rect(0, 0, spec.size.width, spec.size.height);
                // The bedding always beds down at z=5; the main piece keeps
                // the item's zOrder (explicit or the slot table).
                // 垫底片恒在 z=5；主片用物品的 zOrder（显式或槽位表）。
                layer.zOrder = piece == 0 ? defaultLayerZ(slot, true)
                                          : item->zOrder;

                // The item's own colours first, then the recipe's global
                // overrides. / 物品自身颜色在前，配方全局覆盖在后。
                for (const auto& ro : item->roleColors) {
                    PartLayer::RoleColor rc;
                    if (!toRoleColor(ro, spec, rc, err)) return false;
                    layer.paletteRoles.push_back(rc);
                }
                for (const auto& ro : recipe.roleOverrides) {
                    PartLayer::RoleColor rc;
                    if (!toRoleColor(ro, spec, rc, err)) return false;
                    layer.paletteRoles.push_back(rc);
                }
                if (!layer.paletteRoles.empty())
                    applyPaletteRoles(layer.sprite, layer.paletteRoles);

                cs.layers.push_back(std::move(layer));
            }
        }
        out[v] = compositeCharacter(cs);
    }
    return true;
}

}  // namespace fb
