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

const char* kViewNames[3] = {"front", "side", "back"};

const char* kSlotLabels[3] = {
    "Front (down) / 正面",
    "Side / 侧面",
    "Back (up) / 背面",
};

// Row order must match core's SHEET_ROWS contract. / 行序必须与 core 的契约一致。
const char* kRowLabels[fb::SHEET_ROWS] = {
    "down idle", "down walk", "up idle", "up walk",
    "left idle", "left walk", "right idle", "right walk",
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

    ImGui::TextUnformatted("Input views / 输入视图");
    ImGui::TextDisabled("Template-aligned 32x32 PNGs (drop or choose). "
                        "/ 模板对齐的 32x32 PNG（拖入或选择）。");

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
            ImGui::TextDisabled("not loaded / 未载入");
        }
        const std::string btnId = "Choose...##" + std::to_string(v);
        if (ImGui::Button(btnId.c_str())) {
            const std::vector<std::string> files = pfd::open_file(
                "Choose view image", ".",
                {"Image files", "*.png *.jpg *.jpeg *.bmp"}).result();
            if (!files.empty()) st.setView(v, files[0]);
        }
        ImGui::EndGroup();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Palette extraction / 配色提取");
    if (!st.pipelineError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.38f, 0.38f, 1.f));
        ImGui::TextWrapped("%s", st.pipelineError.c_str());
        ImGui::PopStyleColor();
    } else if (st.hasResult) {
        for (int v = 0; v < 3; ++v) {
            ImGui::Text("%s: %d colour%s", kViewNames[v],
                        (int)st.result.maps[v].size(),
                        st.result.maps[v].size() == 1 ? "" : "s");
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
        ImGui::TextDisabled("waiting for input / 等待输入");
    }

    ImGui::Separator();
    ImGui::TextWrapped("%s", st.status.c_str());
    ImGui::BeginDisabled(!st.hasResult);
    if (ImGui::Button("Export sheet PNG... / 导出大图 PNG")) {
        const std::string path = pfd::save_file(
            "Export four-direction sheet", "fourdir_sheet.png",
            {"PNG", "*.png"}).result();
        if (!path.empty()) {
            std::string out = path;
            if (out.size() < 4 || out.substr(out.size() - 4) != ".png") out += ".png";
            st.exportSheet(out);
        }
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("V%s", FLIPBOOK_VERSION);
    ImGui::EndChild();

    ImGui::SameLine();

    // ---- right column: the 8x6 preview ----
    // 右栏：8x6 预览
    ImGui::BeginChild("right", ImVec2(0, 0), true);
    ImGui::TextUnformatted("Preview / 预览");
    static int zoom = 4;
    ImGui::SliderInt("zoom", &zoom, 1, 16);

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
        ImGui::TextDisabled("%dx%d, nearest-neighbour zoom x%d",
                            st.result.sheet.cols, st.result.sheet.rows, zoom);
    } else {
        ImGui::TextDisabled("no preview yet / 尚无预览");
    }
    ImGui::EndChild();
}

}  // namespace ui
}  // namespace fb
