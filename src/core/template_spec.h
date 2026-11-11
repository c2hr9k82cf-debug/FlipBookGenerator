// template_spec.h — the character template as loadable data (v0.9.1).
// 角色模板的可加载数据形态（v0.9.1）。
//
// `assets/templates/chibi_*.json` is the single source of truth for proportions,
// palette roles, per-view part ownership and back-view policy. Everything the
// pipeline needs about "how a character is laid out" comes from here — never
// from hardcoded constants.
// `assets/templates/chibi_*.json` 是比例、调色板角色、逐视图部件归属与背面策略的
// 唯一真源。管线所需的"角色怎么摆"全部来自这里 —— 绝不来自硬编码常量。
#pragma once

#include <map>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace fb {

// Per-view pixel ownership. Coordinates are ABSOLUTE image coordinates
// (already including the margin), so splitting a frame is a pure lookup.
// 逐视图像素归属。坐标是绝对图像坐标（已含边距），切帧即纯查表。
struct PartMap {
    std::vector<int> headRows;        // rows owned by the head / 头部拥有的行
    std::vector<int> bodyRows;        // rows owned by the body (minus arms) / 躯干行（除臂列）
    std::vector<int> legRows;         // rows owned by the legs / 腿部行
    std::vector<int> armLeftCols;     // left-arm columns in body rows / 躯干行内的左臂列
    std::vector<int> armRightCols;    // right-arm columns in body rows / 躯干行内的右臂列
    // Leg split x per leg row (same length as legRows): x <= split → leg_left,
    // x > split → leg_right. Absolute, may vary per row (side views).
    // 每腿行的分腿 x（与 legRows 等长）：x <= split 归 leg_left，x > split 归
    // leg_right。绝对坐标，可逐行不同（侧面视图）。
    std::vector<int> legSplitPerRow;
};

struct TemplateSpec {
    std::string name;
    cv::Size size{32, 32};            // canvas size / 画布尺寸

    // From `proportions` — needed to render pixels_file into the canvas.
    // 来自 proportions —— 渲染 pixels_file 进画布要用。
    int marginX = 9;
    int marginTop = 2;
    int contentWidth = 14;
    int totalRows = 30;

    // role -> BGR (the JSON stores RGB; converted on load).
    // role -> BGR（JSON 存 RGB，加载时转换）。
    std::map<std::string, cv::Vec3b> palette;

    // palette_chars: single ASCII char -> role name. "." maps to the empty role
    // (transparent). Used by renderTemplateView and the P3 fill editor.
    // palette_chars：单个 ASCII 字符 -> role 名。"." 映射空 role（透明）。
    // 供 renderTemplateView 与 P3 填色编辑器使用。
    std::map<char, std::string> paletteChars;

    // back_policy: role -> "keep" | "mirror" | "flatten:<role>".
    // back_policy：role -> "keep" | "mirror" | "flatten:<role>"。
    std::map<std::string, std::string> backPolicy;

    struct View {
        PartMap map;
        cv::Point sheetCell{0, 0};
        std::string pixelsFile;
        // Optional anchor overrides (part name -> pivot, e.g. side leg_right).
        // 可选锚点覆盖（部件名 -> 转轴，如侧面 leg_right）。
        std::map<std::string, cv::Point> anchors;
    };
    // Index 0 = front, 1 = side, 2 = back. / 下标 0 = 正面、1 = 侧面、2 = 背面。
    View views[3];

    std::string baseDir;              // dir of the JSON (pixels_file resolves here) / JSON 所在目录
    std::string jsonPath;

    // Palette lookup helper: role -> BGR, false when the role is unknown.
    // 查色辅助：role -> BGR，role 未知时 false。
    bool roleBgr(const std::string& role, cv::Vec3b& out) const;
};

// Load and validate a template JSON. On failure returns false and fills `err`
// with a human-readable reason (file path + what is wrong).
// 加载并校验模板 JSON。失败返回 false，err 里给出可读原因（路径 + 坏在哪）。
bool loadTemplateSpec(const std::string& jsonPath, TemplateSpec& out,
                      std::string& err);

// Re-render one view from its pixels_file + palette. Result is BGRA, size
// `spec.size`, transparent where the pixel map says so. Byte-exact to the
// reference PNG is the template-load gate's criterion.
// 从 pixels_file + palette 重渲染一个视图。结果为 BGRA、spec.size、
// 像素图透明处保持透明。与参考 PNG 逐字节相同是 template-load 闸门的判据。
cv::Mat renderTemplateView(const TemplateSpec& spec, int view, std::string& err);

}  // namespace fb
