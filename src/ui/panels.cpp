// panels.cpp — the minimal panel set (input slots / extraction / preview /
// status / export). Per CLAUDE.md: no docking polish, no scope creep.
// 最小面板集（输入槽 / 提取结果 / 预览 / 状态 / 导出）。按 CLAUDE.md：
// 不做 docking 打磨，不扩范围。

#include "panels.h"
#include "texture.h"
#include "version.h"

#include "imgui.h"
#include "portable-file-dialogs.h"

#include <string>

namespace fb {
namespace ui {
namespace {

const char* kViewNames[3] = {"正面", "侧面", "背面"};

const char* kSlotLabels[3] = {
    "正面（下）",
    "侧面",
    "背面（上）",
};

// Row order must match core's SHEET_ROWS contract. / 行序必须与 core 的契约一致。
const char* kRowLabels[fb::SHEET_ROWS] = {
    "下 待机", "下 行走", "上 待机", "上 行走",
    "左 待机", "左 行走", "右 待机", "右 行走",
};

// cv::Vec4b is BGRA; ImVec4 is RGBA. / cv::Vec4b 是 BGRA，ImVec4 是 RGBA。
ImVec4 toImVec4(const cv::Vec4b& c) {
    return ImVec4(c[2] / 255.f, c[1] / 255.f, c[0] / 255.f, c[3] / 255.f);
}

}  // namespace

void PreviewTextures::release() {
    deleteTexture(slot[0]);
    deleteTexture(slot[1]);
    deleteTexture(slot[2]);
    deleteTexture(sheet);
    slot[0] = slot[1] = slot[2] = sheet = 0;
}

void PreviewTextures::sync(AppState& st) {
    if (uploadedRevision == st.revision) return;
    release();
    for (int v = 0; v < 3; ++v)
        if (st.views[v].loaded()) slot[v] = matToTexture(st.views[v].image);
    if (st.hasResult) sheet = matToTexture(st.result.sheet);
    uploadedRevision = st.revision;
}

void drawMainContent(AppState& st, PreviewTextures& tex) {
    tex.sync(st);

    // ---- left column: inputs + extraction + status + export ----
    // 左栏：输入 + 提取 + 状态 + 导出
    ImGui::BeginChild("left", ImVec2(430, 0), true);

    ImGui::TextUnformatted("输入视图");
    ImGui::TextDisabled("模板对齐的 32x32 PNG（拖入窗口或点「选择」）。");

    for (int v = 0; v < 3; ++v) {
        ImGui::Separator();
        ImGui::TextUnformatted(kSlotLabels[v]);
        if (tex.slot[v]) {
            ImGui::Image((ImTextureID)(intptr_t)tex.slot[v], ImVec2(64, 64));
        } else {
            ImGui::Dummy(ImVec2(64, 64));
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (st.views[v].loaded()) {
            ImGui::TextWrapped("%s", st.views[v].path.c_str());
        } else {
            ImGui::TextDisabled("未载入");
        }
        const std::string btnId = "选择...##" + std::to_string(v);
        if (ImGui::Button(btnId.c_str())) {
            const std::vector<std::string> files = pfd::open_file(
                "选择视图图片", ".",
                {"图片文件", "*.png *.jpg *.jpeg *.bmp"}).result();
            if (!files.empty()) st.setView(v, files[0]);
        }
        ImGui::EndGroup();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("配色提取");
    if (!st.pipelineError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.38f, 0.38f, 1.f));
        ImGui::TextWrapped("%s", st.pipelineError.c_str());
        ImGui::PopStyleColor();
    } else if (st.hasResult) {
        for (int v = 0; v < 3; ++v) {
            ImGui::Text("%s：%d 种颜色", kViewNames[v],
                        (int)st.result.maps[v].size());
            for (size_t i = 0; i < st.result.maps[v].size(); ++i) {
                const auto& sw = st.result.maps[v][i];
                const std::string id =
                    "##sw" + std::to_string(v) + "_" + std::to_string(i);
                ImGui::ColorButton((id + "a").c_str(), toImVec4(sw.first),
                                   ImGuiColorEditFlags_NoTooltip |
                                       ImGuiColorEditFlags_NoDragDrop,
                                   ImVec2(14, 14));
                ImGui::SameLine();
                ImGui::TextUnformatted("->");
                ImGui::SameLine();
                ImGui::ColorButton((id + "b").c_str(), toImVec4(sw.second),
                                   ImGuiColorEditFlags_NoTooltip |
                                       ImGuiColorEditFlags_NoDragDrop,
                                   ImVec2(14, 14));
                ImGui::SameLine();
                ImGui::TextDisabled("(%d,%d,%d) -> (%d,%d,%d)",
                                    sw.first[2], sw.first[1], sw.first[0],
                                    sw.second[2], sw.second[1], sw.second[0]);
            }
        }
    } else {
        ImGui::TextDisabled("等待输入");
    }

    ImGui::Separator();
    ImGui::TextWrapped("%s", st.status.c_str());
    ImGui::BeginDisabled(!st.hasResult);
    if (ImGui::Button("导出大图 PNG...")) {
        const std::string path = pfd::save_file(
            "导出四方向大图", "fourdir_sheet.png",
            {"PNG", "*.png"}).result();
        if (!path.empty()) {
            std::string out = path;
            if (out.size() < 4 || out.substr(out.size() - 4) != ".png") out += ".png";
            st.exportSheet(out);
        }
    }
    ImGui::EndDisabled();

    // Theme toggle: dark is the default, white is one click away.
    // 主题切换：默认黑色，白色一键即切。
    ImGui::SameLine();
    static bool lightTheme = false;
    if (ImGui::Button(lightTheme ? "黑色主题" : "白色主题")) {
        lightTheme = !lightTheme;
        if (!lightTheme) {
            ImGui::StyleColorsDark();
        } else {
            ImGui::StyleColorsLight();
            // A cleaner white than StyleColorsLight's grey defaults.
            // 比 StyleColorsLight 的默认灰更干净的白。
            ImGuiStyle& style = ImGui::GetStyle();
            style.Colors[ImGuiCol_WindowBg]  = ImVec4(1.f, 1.f, 1.f, 1.f);
            style.Colors[ImGuiCol_ChildBg]   = ImVec4(0.97f, 0.97f, 0.98f, 1.f);
            style.Colors[ImGuiCol_PopupBg]   = ImVec4(1.f, 1.f, 1.f, 1.f);
            style.Colors[ImGuiCol_Text]      = ImVec4(0.12f, 0.12f, 0.14f, 1.f);
        }
    }
    ImGui::TextDisabled("V%s", FLIPBOOK_VERSION);
    ImGui::EndChild();

    ImGui::SameLine();

    // ---- right column: the 8x6 preview ----
    // 右栏：8x6 预览
    ImGui::BeginChild("right", ImVec2(0, 0), true);
    ImGui::TextUnformatted("预览");
    static int zoom = 4;
    ImGui::SliderInt("缩放", &zoom, 1, 16);

    if (st.hasResult && tex.sheet) {
        const float cell = 32.f * zoom;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        for (int r = 0; r < fb::SHEET_ROWS; ++r) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + r * cell));
            ImGui::TextUnformatted(kRowLabels[r]);
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 110.f, origin.y));
        ImGui::Image((ImTextureID)(intptr_t)tex.sheet,
                     ImVec2(st.result.sheet.cols * zoom,
                            st.result.sheet.rows * zoom));
        ImGui::SetCursorScreenPos(
            ImVec2(origin.x, origin.y + st.result.sheet.rows * zoom + 8.f));
        ImGui::TextDisabled("%dx%d，最近邻放大 x%d",
                            st.result.sheet.cols, st.result.sheet.rows, zoom);
    } else {
        ImGui::TextDisabled("尚无预览");
    }
    ImGui::EndChild();
}

}  // namespace ui
}  // namespace fb
