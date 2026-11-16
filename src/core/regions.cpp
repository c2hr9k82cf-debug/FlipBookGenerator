// regions.cpp — region box loading + contract check (v0.12.0)
// 轮廓框加载 + 契约校验（v0.12.0）
#include "core/regions.h"

#include "core/json_min.h"

#include <fstream>
#include <sstream>

namespace fb {

bool RegionBoxes::contains(const std::string& slot, int x, int y) const {
    auto it = masks.find(slot);
    if (it == masks.end()) return false;
    if (x < 0 || y < 0 || y >= (int)it->second.size()) return false;
    const auto& row = it->second[(size_t)y];
    if (x >= (int)row.size()) return false;
    return row[(size_t)x];
}

int RegionBoxes::count(const std::string& slot) const {
    auto it = masks.find(slot);
    if (it == masks.end()) return 0;
    int n = 0;
    for (const auto& row : it->second)
        for (bool b : row)
            if (b) ++n;
    return n;
}

bool loadRegions(const std::string& jsonPath, RegionBoxes& out, std::string& err) {
    std::ifstream f(jsonPath);
    if (!f) {
        err = "loadRegions: cannot open " + jsonPath;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    JsonValue root;
    if (!jsonParse(ss.str(), root, err)) {
        err = "loadRegions: " + err;
        return false;
    }
    const JsonValue* slots = root.find("slots");
    if (!slots || slots->type != JsonValue::Object) {
        err = "loadRegions: missing slots object";
        return false;
    }
    const JsonValue* cell = root.find("cell");
    out.cell = cell ? (int)cell->asInt(32) : 32;
    if (out.cell != 32) {
        err = "loadRegions: expected cell 32, got " + std::to_string(out.cell);
        return false;
    }
    out.masks.clear();
    for (const auto& kv : slots->obj) {
        const JsonValue* mask = kv.second.find("mask");
        if (!mask || mask->type != JsonValue::Array ||
            (int)mask->arr.size() != out.cell) {
            err = "loadRegions: slot " + kv.first + ": mask must be 32 rows";
            return false;
        }
        std::vector<std::vector<bool>> m;
        int cells = 0;
        for (const auto& rowV : mask->arr) {
            const std::string row = rowV.asString();
            if ((int)row.size() != out.cell) {
                err = "loadRegions: slot " + kv.first + ": mask row must be 32 chars";
                return false;
            }
            std::vector<bool> r;
            for (char c : row) {
                if (c != '.' && c != '#') {
                    err = "loadRegions: slot " + kv.first +
                          ": mask rows use '.' and '#'";
                    return false;
                }
                r.push_back(c == '#');
                if (c == '#') ++cells;
            }
            m.push_back(std::move(r));
        }
        // count field must agree with the mask itself (self-consistency gate).
        // count 字段必须与掩码自身一致（自洽闸门）。
        const JsonValue* cnt = kv.second.find("count");
        if (cnt && (int)cnt->asInt(-1) != cells) {
            err = "loadRegions: slot " + kv.first + ": count " +
                  std::to_string(cnt->asInt()) + " != mask cells " +
                  std::to_string(cells);
            return false;
        }
        // bbox field must agree with the mask it describes / bbox 字段必须与
        // 它描述的掩码一致
        if (const JsonValue* bb = kv.second.find("bbox")) {
            int x0 = 99, y0 = 99, x1 = -1, y1 = -1;
            for (int y = 0; y < (int)m.size(); ++y)
                for (int x = 0; x < (int)m[(size_t)y].size(); ++x)
                    if (m[(size_t)y][(size_t)x]) {
                        if (x < x0) x0 = x;
                        if (x > x1) x1 = x;
                        if (y < y0) y0 = y;
                        if (y > y1) y1 = y;
                    }
            const long long got[4] = {x0, y0, x1, y1};
            bool same = bb->type == JsonValue::Array && bb->arr.size() == 4;
            for (int i = 0; same && i < 4; ++i)
                same = bb->arr[(size_t)i].asInt(-999) == got[i];
            if (!same) {
                err = "loadRegions: slot " + kv.first +
                      ": bbox field does not match the mask";
                return false;
            }
        }
        if (cells == 0) {
            err = "loadRegions: slot " + kv.first + ": empty box";
            return false;
        }
        out.masks[kv.first] = std::move(m);
    }
    if (out.masks.empty()) {
        err = "loadRegions: no slots";
        return false;
    }
    return true;
}

int outOfBoxPixels(const cv::Mat& img, const std::string& slot,
                   const RegionBoxes& boxes) {
    if (img.empty() || img.type() != CV_8UC4) return -1;
    int bad = 0;
    for (int y = 0; y < img.rows; ++y) {
        for (int x = 0; x < img.cols; ++x) {
            const cv::Vec4b px = img.at<cv::Vec4b>(y, x);
            if (px[3] == 0) continue;
            if (!boxes.contains(slot, x, y)) ++bad;
        }
    }
    return bad;
}

}  // namespace fb
