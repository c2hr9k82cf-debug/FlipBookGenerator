// panels.cpp — the panel set: inputs / extraction / export / animated preview.
// 面板集：输入 / 提取 / 导出 / 动画预览。
//
// Layout rules learned from the first UI screenshot: no nested bordered boxes
// ("two layers" look), the palette list lives in a collapsed header, and the
// preview is a per-row animation player with user-set fps and zoom.
// 从第一版截图学到的排版规矩：不套带框子面板（"两层"观感）、配色列表收进折叠头、
// 预览是逐行动画播放器（帧率、大小由用户设定）。

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

void drawThemeButton() {
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
}

// UV rectangle of one 32x32 cell in the sheet texture. / 表纹理里一格 32x32 的 UV 矩形。
void cellUv(const fb::FourDirResult& res, int col, int row,
            ImVec2& uv0, ImVec2& uv1) {
    const float w = (float)res.sheet.cols, h = (float)res.sheet.rows;
    uv0 = ImVec2(col * 32.f / w, row * 32.f / h);
    uv1 = ImVec2((col + 1) * 32.f / w, (row + 1) * 32.f / h);
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

    // ---- top bar: title + theme + version ----
    // 顶栏：标题 + 主题 + 版本
    ImGui::TextUnformatted("FlipBookGenerator");
    ImGui::SameLine();
    ImGui::TextDisabled("V" FLIPBOOK_VERSION);
    ImGui::SameLine(ImGui::GetWindowWidth() - 110.f);
    drawThemeButton();
    ImGui::Separator();

    const float kLeftW = 360.f;
    ImGui::BeginChild("left", ImVec2(kLeftW, 0), false);

    // ---- inputs ----
    // 输入
    ImGui::TextUnformatted("输入视图");
    ImGui::SameLine();
    if (ImGui::SmallButton("全部清除")) st.clearAll();
    ImGui::TextDisabled("模板对齐的 32x32 PNG（拖入窗口或点「选择」）。");
    for (int v = 0; v < 3; ++v) {
        ImGui::Spacing();
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
        ImGui::SameLine();
        ImGui::BeginDisabled(!st.views[v].loaded());
        if (ImGui::Button(("清除##" + std::to_string(v)).c_str())) st.clearView(v);
        ImGui::EndDisabled();
        ImGui::EndGroup();
    }

    // ---- palette extraction, collapsed: the swatch list is long ----
    // 配色提取，默认收起：色对列表很长
    ImGui::Spacing();
    std::string summary;
    if (!st.pipelineError.empty()) summary = "提取出错";
    else if (st.hasResult) {
        size_t n = st.result.maps[0].size() + st.result.maps[1].size() +
                   st.result.maps[2].size();
        summary = "3 视图 · 共 " + std::to_string(n) + " 条颜色映射";
    } else summary = "等待输入";
    const std::string head = "配色提取###palette";
    if (ImGui::CollapsingHeader(head.c_str())) {
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
    } else {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", summary.c_str());
    }

    // ---- export ----
    // 导出
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("导出");
    ImGui::TextDisabled("点击后弹出保存窗口，由你选择存放位置。");
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

    ImGui::EndChild();
    ImGui::SameLine();

    // ---- right column: animated preview ----
    // 右栏：动画预览
    ImGui::BeginChild("right", ImVec2(0, 0), false);

    static bool playing = true;
    static int fps = 8;
    static int zoom = 3;
    static float clockTime = 0.f;
    if (playing) clockTime += ImGui::GetIO().DeltaTime;

    if (ImGui::Button(playing ? "暂停" : "播放")) playing = !playing;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::SliderInt("帧率", &fps, 1, 30);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::SliderInt("大小", &zoom, 1, 16);
    ImGui::SameLine();
    ImGui::TextDisabled("fps %d · 放大 x%d", fps, zoom);

    if (st.hasResult && tex.sheet) {
        const float cell = 32.f * (float)zoom;
        // Every row plays its own 6-cell loop; idle rows breathe (f0 f1 f0 f1
        // f0 f1 baked into the sheet) and walk rows run their full cycle.
        // 每行播自己的 6 格循环；待机行呼吸（表里就是 f0 f1 f0 f1 f0 f1），
        // 行走行走完整循环。
        const int frame = ((int)(clockTime * (float)fps) % fb::SHEET_COLS +
                           fb::SHEET_COLS) % fb::SHEET_COLS;
        for (int r = 0; r < fb::SHEET_ROWS; ++r) {
            ImGui::TextUnformatted(kRowLabels[r]);
            ImGui::SameLine(90.f);
            ImVec2 uv0, uv1;
            cellUv(st.result, frame, r, uv0, uv1);
            ImGui::Image((ImTextureID)(intptr_t)tex.sheet, ImVec2(cell, cell),
                         uv0, uv1);
        }
        ImGui::TextDisabled("每行 6 帧循环 · %dx%d · 最近邻放大",
                            st.result.sheet.cols, st.result.sheet.rows);
    } else {
        ImGui::TextDisabled("尚无预览 —— 载入三张视图后自动开播");
    }
    ImGui::EndChild();

    // ---- status bar ----
    // 状态栏
    ImGui::Separator();
    ImGui::TextWrapped("%s", st.status.c_str());
}

}  // namespace ui
}  // namespace fb
