// template_spec.cpp — template JSON loading + pixels_file rendering.
// 模板 JSON 加载 + pixels_file 渲染。
#include "core/template_spec.h"

#include "core/json_min.h"

#include <fstream>
#include <sstream>

#include <opencv2/imgcodecs.hpp>

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

bool parseIntArray(const JsonValue* v, std::vector<int>& out, const char* what,
                   const std::string& where, std::string& err) {
    if (!v || v->type != JsonValue::Array) {
        err = where + ": missing or non-array '" + std::string(what) + "'";
        return false;
    }
    out.clear();
    for (const JsonValue& e : v->arr) {
        if (e.type != JsonValue::Int) {
            err = where + ": '" + std::string(what) + "' must contain integers";
            return false;
        }
        out.push_back((int)e.i);
    }
    return true;
}

bool loadPartMap(const JsonValue& pmObj, PartMap& out, const std::string& where,
                 std::string& err) {
    if (pmObj.type != JsonValue::Object) {
        err = where + ": 'part_map' must be an object";
        return false;
    }
    if (!parseIntArray(pmObj.find("head_rows"), out.headRows, "head_rows", where, err) ||
        !parseIntArray(pmObj.find("body_rows"), out.bodyRows, "body_rows", where, err) ||
        !parseIntArray(pmObj.find("leg_rows"), out.legRows, "leg_rows", where, err) ||
        !parseIntArray(pmObj.find("arm_left_cols"), out.armLeftCols, "arm_left_cols", where, err) ||
        !parseIntArray(pmObj.find("arm_right_cols"), out.armRightCols, "arm_right_cols", where, err) ||
        !parseIntArray(pmObj.find("leg_split_per_row"), out.legSplitPerRow,
                       "leg_split_per_row", where, err)) {
        return false;
    }
    if (out.legSplitPerRow.size() != out.legRows.size()) {
        err = where + ": leg_split_per_row length (" +
              std::to_string(out.legSplitPerRow.size()) +
              ") must match leg_rows length (" + std::to_string(out.legRows.size()) + ")";
        return false;
    }
    return true;
}

// pixels_file format: '#' comment lines and blank lines ignored; every other
// line is exactly content_width ASCII chars, one per pixel. totalRows data
// lines in total (the first is template row `margin_top`).
// pixels_file 格式：'#' 注释行与空行忽略；其余每行恰 content_width 个 ASCII
// 字符，每字符一像素。共 totalRows 行数据（首行是模板行 margin_top）。
bool loadPixelsFile(const std::string& path, const TemplateSpec& spec,
                    cv::Mat& out, std::string& err) {
    std::string text;
    if (!readFile(path, text, err)) return false;
    std::istringstream ss(text);
    std::string line;
    std::vector<std::string> rows;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        rows.push_back(line);
    }
    if ((int)rows.size() != spec.totalRows) {
        err = path + ": expected " + std::to_string(spec.totalRows) +
              " data rows, got " + std::to_string(rows.size());
        return false;
    }
    out = cv::Mat::zeros(spec.size, CV_8UC4);
    for (int r = 0; r < spec.totalRows; ++r) {
        const std::string& row = rows[r];
        if ((int)row.size() != spec.contentWidth) {
            err = path + ": data row " + std::to_string(r) + " has width " +
                  std::to_string(row.size()) + ", expected " +
                  std::to_string(spec.contentWidth);
            return false;
        }
        const int y = spec.marginTop + r;
        if (y < 0 || y >= spec.size.height) {
            err = path + ": row " + std::to_string(r) + " lands outside the canvas";
            return false;
        }
        for (int c = 0; c < spec.contentWidth; ++c) {
            const char ch = row[c];
            cv::Vec4b px(0, 0, 0, 0);
            if (ch != '.') {
                auto it = spec.paletteChars.find(ch);
                if (it == spec.paletteChars.end() || it->second.empty()) {
                    err = path + ": unknown pixel char '" + std::string(1, ch) +
                          "' at data row " + std::to_string(r) + " col " +
                          std::to_string(c);
                    return false;
                }
                auto pit = spec.palette.find(it->second);
                if (pit == spec.palette.end()) {
                    err = path + ": pixel char '" + std::string(1, ch) +
                          "' maps to unknown role '" + it->second + "'";
                    return false;
                }
                px = cv::Vec4b(pit->second[0], pit->second[1], pit->second[2], 255);
            }
            const int x = spec.marginX + c;
            if (x < 0 || x >= spec.size.width) {
                err = path + ": col " + std::to_string(c) +
                      " lands outside the canvas";
                return false;
            }
            out.at<cv::Vec4b>(y, x) = px;
        }
    }
    return true;
}

}  // namespace

bool TemplateSpec::roleBgr(const std::string& role, cv::Vec3b& out) const {
    auto it = palette.find(role);
    if (it == palette.end()) return false;
    out = it->second;
    return true;
}

bool loadTemplateSpec(const std::string& jsonPath, TemplateSpec& out,
                      std::string& err) {
    out = TemplateSpec{};
    std::string text;
    if (!readFile(jsonPath, text, err)) return false;

    JsonValue root;
    if (!jsonParse(text, root, err)) {
        err = jsonPath + ": " + err;
        return false;
    }
    if (root.type != JsonValue::Object) {
        err = jsonPath + ": root must be a JSON object";
        return false;
    }
    out.jsonPath = jsonPath;
    const size_t slash = jsonPath.find_last_of("/\\");
    out.baseDir = (slash == std::string::npos) ? "." : jsonPath.substr(0, slash);

    out.name = root.has("name") ? root.find("name")->asString() : "";

    if (const JsonValue* sz = root.find("size")) {
        if (sz->type != JsonValue::Object) {
            err = jsonPath + ": 'size' must be an object";
            return false;
        }
        const long long w = sz->has("width") ? sz->find("width")->asInt(-1) : -1;
        const long long h = sz->has("height") ? sz->find("height")->asInt(-1) : -1;
        if (w <= 0 || h <= 0) {
            err = jsonPath + ": 'size' needs positive width/height";
            return false;
        }
        out.size = cv::Size((int)w, (int)h);
    }

    if (const JsonValue* pr = root.find("proportions")) {
        if (pr->type != JsonValue::Object) {
            err = jsonPath + ": 'proportions' must be an object";
            return false;
        }
        if (pr->has("margin_x")) out.marginX = (int)pr->find("margin_x")->asInt(out.marginX);
        if (pr->has("margin_top")) out.marginTop = (int)pr->find("margin_top")->asInt(out.marginTop);
        if (pr->has("content_width")) out.contentWidth = (int)pr->find("content_width")->asInt(out.contentWidth);
        if (pr->has("total_rows")) out.totalRows = (int)pr->find("total_rows")->asInt(out.totalRows);
    }

    // palette: role -> [R, G, B] in the file; stored here as BGR for OpenCV.
    // palette：文件里 role -> [R, G, B]；此处按 OpenCV 习惯存 BGR。
    if (const JsonValue* pal = root.find("palette")) {
        if (pal->type != JsonValue::Object) {
            err = jsonPath + ": 'palette' must be an object";
            return false;
        }
        for (const auto& kv : pal->obj) {
            const JsonValue& v = kv.second;
            if (v.type != JsonValue::Array || v.arr.size() != 3) {
                err = jsonPath + ": palette entry '" + kv.first +
                      "' must be an [R,G,B] array";
                return false;
            }
            int rgb[3];
            for (int i = 0; i < 3; ++i) {
                if (v.arr[i].type != JsonValue::Int || v.arr[i].i < 0 || v.arr[i].i > 255) {
                    err = jsonPath + ": palette entry '" + kv.first +
                          "' has a channel outside 0..255";
                    return false;
                }
                rgb[i] = (int)v.arr[i].i;
            }
            out.palette[kv.first] = cv::Vec3b((uchar)rgb[2], (uchar)rgb[1], (uchar)rgb[0]);
        }
    } else {
        err = jsonPath + ": missing 'palette'";
        return false;
    }

    if (const JsonValue* pc = root.find("palette_chars")) {
        if (pc->type != JsonValue::Object) {
            err = jsonPath + ": 'palette_chars' must be an object";
            return false;
        }
        for (const auto& kv : pc->obj) {
            if (kv.first.size() != 1) {
                err = jsonPath + ": palette_chars key '" + kv.first +
                      "' must be a single character";
                return false;
            }
            // null means transparent ('.'). / null 表示透明（'.'）。
            out.paletteChars[kv.first[0]] =
                kv.second.type == JsonValue::Null ? "" : kv.second.asString();
        }
    } else {
        err = jsonPath + ": missing 'palette_chars'";
        return false;
    }

    // back_policy is optional in the schema but required for back inference;
    // the template-load gate asserts the shipped templates carry it.
    // back_policy 在 schema 里可选，但背面推断要用；template-load 闸门断言
    // 出厂模板都带它。
    if (const JsonValue* bp = root.find("back_policy")) {
        if (bp->type != JsonValue::Object) {
            err = jsonPath + ": 'back_policy' must be an object";
            return false;
        }
        for (const auto& kv : bp->obj) {
            if (kv.second.type != JsonValue::String) {
                err = jsonPath + ": back_policy entry '" + kv.first +
                      "' must be a string";
                return false;
            }
            const std::string& rule = kv.second.s;
            const bool ok = rule == "keep" || rule == "mirror" ||
                            (rule.rfind("flatten:", 0) == 0 && rule.size() > 8);
            if (!ok) {
                err = jsonPath + ": back_policy entry '" + kv.first +
                      "' has unknown rule '" + rule +
                      "' (keep | mirror | flatten:<role>)";
                return false;
            }
            out.backPolicy[kv.first] = rule;
        }
    }

    // views: front / side / back. The '_schema' doc entry is skipped.
    // views：front / side / back。'_schema' 文档项跳过。
    const JsonValue* views = root.find("views");
    if (!views || views->type != JsonValue::Object) {
        err = jsonPath + ": missing 'views' object";
        return false;
    }
    const char* kViewNames[3] = {"front", "side", "back"};
    for (int v = 0; v < 3; ++v) {
        const JsonValue* vo = views->find(kViewNames[v]);
        if (!vo || vo->type != JsonValue::Object) {
            err = jsonPath + ": missing views." + kViewNames[v];
            return false;
        }
        TemplateSpec::View& dst = out.views[v];
        dst.pixelsFile = vo->has("pixels_file")
                             ? vo->find("pixels_file")->asString()
                             : "";
        if (dst.pixelsFile.empty()) {
            err = jsonPath + ": views." + kViewNames[v] + " needs 'pixels_file'";
            return false;
        }
        if (const JsonValue* sc = vo->find("sheet_cell")) {
            if (sc->type != JsonValue::Array || sc->arr.size() != 2) {
                err = jsonPath + ": views." + kViewNames[v] +
                      ".sheet_cell must be [col,row]";
                return false;
            }
            dst.sheetCell = cv::Point((int)sc->arr[0].asInt(), (int)sc->arr[1].asInt());
        }
        if (const JsonValue* pm = vo->find("part_map")) {
            if (!loadPartMap(*pm, dst.map,
                             jsonPath + ": views." + kViewNames[v], err)) {
                return false;
            }
        } else {
            err = jsonPath + ": views." + kViewNames[v] + " needs 'part_map'";
            return false;
        }
        if (const JsonValue* an = vo->find("anchors")) {
            if (an->type != JsonValue::Object) {
                err = jsonPath + ": views." + kViewNames[v] +
                      ".anchors must be an object";
                return false;
            }
            for (const auto& kv : an->obj) {
                const JsonValue& p = kv.second;
                if (p.type != JsonValue::Array || p.arr.size() != 2) {
                    err = jsonPath + ": anchors." + kv.first + " must be [x,y]";
                    return false;
                }
                dst.anchors[kv.first] =
                    cv::Point((int)p.arr[0].asInt(), (int)p.arr[1].asInt());
            }
        }
    }
    return true;
}

cv::Mat renderTemplateView(const TemplateSpec& spec, int view, std::string& err) {
    if (view < 0 || view > 2) {
        err = "renderTemplateView: view must be 0..2";
        return {};
    }
    const std::string path = spec.baseDir + "/" + spec.views[view].pixelsFile;
    cv::Mat out;
    if (!loadPixelsFile(path, spec, out, err)) return {};
    return out;
}

}  // namespace fb
