// parts_library.cpp — parts library loading + validation.
// 部件库加载 + 校验。
#include "core/parts_library.h"

#include "core/json_min.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <opencv2/imgcodecs.hpp>

namespace fs = std::filesystem;

namespace fb {
namespace {

bool readFile(const std::string& path, std::string& out, std::string& err) {
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

// Load a view PNG: must exist (when required), be expectedSize and BGRA.
// 载入视图 PNG：必须存在（需要时）、尺寸为 expectedSize、BGRA。
bool loadViewPng(const std::string& path, cv::Size expectedSize, cv::Mat& out,
                 std::string& err) {
    cv::Mat img = cv::imread(path, cv::IMREAD_UNCHANGED);
    if (img.empty()) {
        err = "cannot read image (missing or not a PNG): " + path;
        return false;
    }
    if (img.size() != expectedSize) {
        err = path + ": expected " + std::to_string(expectedSize.width) + "x" +
              std::to_string(expectedSize.height) + ", got " +
              std::to_string(img.cols) + "x" + std::to_string(img.rows);
        return false;
    }
    if (img.type() != CV_8UC4) {
        err = path + ": expected BGRA (8-bit, 4 channels)";
        return false;
    }
    out = img;
    return true;
}

bool parseBgrArray(const JsonValue& v, cv::Vec3b& out, const std::string& where,
                   std::string& err) {
    if (v.type != JsonValue::Array || v.arr.size() != 3) {
        err = where + " must be an [R,G,B] array";
        return false;
    }
    int rgb[3];
    for (int i = 0; i < 3; ++i) {
        if (v.arr[i].type != JsonValue::Int || v.arr[i].i < 0 || v.arr[i].i > 255) {
            err = where + " has a channel outside 0..255";
            return false;
        }
        rgb[i] = (int)v.arr[i].i;
    }
    out = cv::Vec3b((uchar)rgb[2], (uchar)rgb[1], (uchar)rgb[0]);
    return true;
}

bool parseAnchor(const JsonValue& v, cv::Point& out, const std::string& where,
                 std::string& err) {
    if (v.type != JsonValue::Array || v.arr.size() != 2) {
        err = where + " must be [x,y]";
        return false;
    }
    if (v.arr[0].type != JsonValue::Int || v.arr[1].type != JsonValue::Int) {
        err = where + " must contain integers";
        return false;
    }
    out = cv::Point((int)v.arr[0].i, (int)v.arr[1].i);
    return true;
}

}  // namespace

const std::vector<std::string>& librarySlotNames() {
    // Same six slots as PartLayer. / 与 PartLayer 的六槽一致。
    static const std::vector<std::string> kSlots = {
        "hair", "face", "top", "pants", "boots", "body"};
    return kSlots;
}

const LibraryItem* PartsLibrary::find(const std::string& slot,
                                      const std::string& name) const {
    for (const auto& it : items)
        if (it.slot == slot && it.name == name) return &it;
    return nullptr;
}

std::vector<const LibraryItem*> PartsLibrary::inSlot(const std::string& slot) const {
    std::vector<const LibraryItem*> out;
    for (const auto& it : items)
        if (it.slot == slot) out.push_back(&it);
    return out;
}

bool loadLibraryItem(const std::string& itemDir, const std::string& slot,
                     cv::Size expectedSize, LibraryItem& out, std::string& err) {
    out = LibraryItem{};
    out.dir = itemDir;
    out.slot = slot;

    const std::string where = itemDir;

    // -- item.json / item.json --
    std::string text;
    if (!readFile(itemDir + "/item.json", text, err)) return false;
    JsonValue meta;
    if (!jsonParse(text, meta, err)) {
        err = where + "/item.json: " + err;
        return false;
    }
    if (meta.type != JsonValue::Object) {
        err = where + "/item.json: root must be an object";
        return false;
    }

    out.name = meta.has("name") ? meta.find("name")->asString() : "";
    if (out.name.empty()) {
        // Fall back to the folder name so a missing key is not fatal.
        // 落回目录名，缺键不至于致命。
        const size_t slash = itemDir.find_last_of("/\\");
        out.name = (slash == std::string::npos) ? itemDir : itemDir.substr(slash + 1);
    }

    if (meta.has("slot")) {
        const std::string declared = meta.find("slot")->asString();
        if (declared != slot) {
            err = where + "/item.json: slot '" + declared +
                  "' does not match its folder slot '" + slot + "'";
            return false;
        }
    }

    if (meta.has("zOrder")) {
        const JsonValue& z = *meta.find("zOrder");
        if (z.type != JsonValue::Int) {
            err = where + "/item.json: 'zOrder' must be an integer";
            return false;
        }
        out.zOrder = (int)z.i;
    }

    // -- anchors (optional per view) / anchors（逐视图可选） --
    if (const JsonValue* an = meta.find("anchors")) {
        if (an->type != JsonValue::Object) {
            err = where + "/item.json: 'anchors' must be an object";
            return false;
        }
        const char* kViewNames[3] = {"front", "side", "back"};
        for (int v = 0; v < 3; ++v) {
            const JsonValue* p = an->find(kViewNames[v]);
            if (!p) continue;
            if (!parseAnchor(*p, out.anchors[v],
                             where + "/item.json: anchors." + kViewNames[v], err)) {
                return false;
            }
            out.hasAnchor[v] = true;
        }
    }

    // -- roleColors (optional) / roleColors（可选） --
    if (const JsonValue* rc = meta.find("roleColors")) {
        if (rc->type != JsonValue::Array) {
            err = where + "/item.json: 'roleColors' must be an array";
            return false;
        }
        for (size_t i = 0; i < rc->arr.size(); ++i) {
            const JsonValue& e = rc->arr[i];
            const std::string ewhere =
                where + "/item.json: roleColors[" + std::to_string(i) + "]";
            if (e.type != JsonValue::Object) {
                err = ewhere + " must be an object";
                return false;
            }
            LibraryItem::RoleColorOverride ro;
            if (!e.has("role") || e.find("role")->type != JsonValue::String ||
                e.find("role")->s.empty()) {
                err = ewhere + " needs a non-empty string 'role'";
                return false;
            }
            ro.role = e.find("role")->s;
            if (const JsonValue* src = e.find("src")) {
                if (!parseBgrArray(*src, ro.srcBgr, ewhere + ".src", err)) return false;
                ro.hasSrc = true;
            }
            if (const JsonValue* dst = e.find("dst")) {
                if (!parseBgrArray(*dst, ro.dstBgr, ewhere + ".dst", err)) return false;
            } else {
                err = ewhere + " needs 'dst' ([R,G,B])";
                return false;
            }
            out.roleColors.push_back(ro);
        }
    }

    // -- back / silhouette policy / 背面 / 剪影策略 --
    if (const JsonValue* b = meta.find("back")) {
        const std::string mode = b->asString();
        if (mode != "auto" && mode != "file") {
            err = where + "/item.json: 'back' must be \"auto\" or \"file\"";
            return false;
        }
        out.backMode = mode;
    }
    if (const JsonValue* s = meta.find("silhouette")) {
        const std::string sil = s->asString();
        if (sil != "template") {
            // v1 supports exactly one silhouette mode. / v1 只支持一种剪影模式。
            err = where + "/item.json: 'silhouette' must be \"template\" (v1)";
            return false;
        }
        out.silhouette = sil;
    }

    // -- sprites / 精灵 --
    if (!loadViewPng(itemDir + "/front.png", expectedSize, out.views[0], err))
        return false;
    if (!loadViewPng(itemDir + "/side.png", expectedSize, out.views[1], err))
        return false;
    const bool backExists = fs::exists(itemDir + "/back.png");
    if (out.backMode == "file") {
        if (!backExists) {
            err = where + ": back:\"file\" but back.png is missing";
            return false;
        }
        if (!loadViewPng(itemDir + "/back.png", expectedSize, out.views[2], err))
            return false;
    } else if (backExists) {
        // auto + back.png is ambiguous (use the file or the inference?): refuse
        // rather than guess. Delete one or set back:"file".
        // auto 却带 back.png 是歧义（用文件还是推断？）：宁可拒绝也不猜。
        err = where + ": back:\"auto\" but back.png exists — set back:\"file\" or remove it";
        return false;
    }
    return true;
}

bool loadPartsLibrary(const std::string& rootDir, cv::Size expectedSize,
                      PartsLibrary& out, std::string& err) {
    out = PartsLibrary{};
    out.root = rootDir;
    if (!fs::exists(rootDir) || !fs::is_directory(rootDir)) {
        err = "library root is not a directory: " + rootDir;
        return false;
    }

    // Slot folders may come in any order; items are collected then sorted so
    // the GUI shows a stable list.
    // 槽位目录顺序不限；收集后统一排序，GUI 列表才稳定。
    for (const std::string& slot : librarySlotNames()) {
        const fs::path slotDir = fs::path(rootDir) / slot;
        if (!fs::exists(slotDir)) continue;   // empty slot is fine / 空槽位合法
        if (!fs::is_directory(slotDir)) {
            err = slotDir.string() + ": expected a slot directory";
            return false;
        }
        for (const fs::directory_entry& e : fs::directory_iterator(slotDir)) {
            if (!e.is_directory()) continue;  // stray files ignored / 杂散文件忽略
            LibraryItem item;
            if (!loadLibraryItem(e.path().string(), slot, expectedSize, item, err)) {
                return false;
            }
            out.items.push_back(std::move(item));
        }
    }
    std::sort(out.items.begin(), out.items.end(),
              [](const LibraryItem& a, const LibraryItem& b) {
                  if (a.slot != b.slot) return a.slot < b.slot;
                  return a.name < b.name;
              });
    return true;
}

}  // namespace fb
