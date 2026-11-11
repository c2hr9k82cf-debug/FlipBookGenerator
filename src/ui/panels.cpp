// panels.cpp — the panel set: inputs / extraction / export / animated preview.
// 面板集：输入 / 提取 / 导出 / 动画预览。
//
// Layout rules learned from the first UI screenshot: no nested bordered boxes
// ("two layers" look), the palette list lives in a collapsed header, and the
// preview is a per-row animation player with user-set fps and zoom.
// 从第一版截图学到的排版规矩：不套带框子面板（"两层"观感）、配色列表收进折叠头、
// 预览是逐行动画播放器（帧率、大小由用户设定）。

#include "panels.h"
#include "dressup_panel.h"
#include "part_editor_panel.h"
#include "texture.h"
#include "version.h"

#include "imgui.h"
#include "portable-file-dialogs.h"

#include <cmath>
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

// Display grid: 4 direction rows x 2 columns (idle | walk). Sheet row order is
// dir*2 + (0 idle, 1 walk) per the SHEET_ROWS contract.
// 显示网格：4 个方向行 x 2 列（待机 | 行走）。表内行序按 SHEET_ROWS 契约：
// dir*2 +（0 待机，1 行走）。
const char* kDirLabels[4] = {"下", "上", "左", "右"};

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
    deleteTexture(slotDiff[0]);
    deleteTexture(slotDiff[1]);
    deleteTexture(slotDiff[2]);
    deleteTexture(sheet);
    deleteTexture(dressup[0]);
    deleteTexture(dressup[1]);
    deleteTexture(dressup[2]);
    slot[0] = slot[1] = slot[2] = sheet = 0;
    slotDiff[0] = slotDiff[1] = slotDiff[2] = 0;
    dressup[0] = dressup[1] = dressup[2] = 0;
    editorCanvas.release();   // same GL lifetime contract / 同一 GL 生命周期契约
}

void PreviewTextures::sync(AppState& st) {
    // The dress-up previews track their own revision (independent of the
    // animation pipeline's). / 捏人预览有自己的版本号（与动画管线无关）。
    if (dressupRevision != st.composeRevision) {
        deleteTexture(dressup[0]);
        deleteTexture(dressup[1]);
        deleteTexture(dressup[2]);
        dressup[0] = dressup[1] = dressup[2] = 0;
        for (int v = 0; v < 3; ++v)
            if (!st.composePreview[v].empty())
                dressup[v] = matToTexture(st.composePreview[v]);
        dressupRevision = st.composeRevision;
    }
    if (uploadedRevision == st.revision) return;
    release();
    for (int v = 0; v < 3; ++v) {
        // Show the ALIGNED copy: it is what the pipeline consumes, and it is
        // the same 32x32 coordinate system as the diff overlay.
        // 显示对齐拷贝：它是管线实际消费的图，且与差异叠加同处 32x32 坐标系。
        if (st.views[v].loaded())
            slot[v] = matToTexture(!st.views[v].aligned.empty()
                                       ? st.views[v].aligned : st.views[v].image);
        if (!st.views[v].diffOverlay.empty())
            slotDiff[v] = matToTexture(st.views[v].diffOverlay);
    }
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

    // ---- mode tabs (v0.10.0): animation export / part editor ----
    // 模式 tab（v0.10.0）：动画导出 / 部件编辑器
    if (ImGui::BeginTabBar("modeTabs")) {
    if (ImGui::BeginTabItem("动画导出")) {
    st.mode = 0;

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
        // Thumbnail + diff overlay (v0.9.0): the transparent mark texture is
        // composited over the aligned copy — magenta = silhouette diff,
        // orange = colour conflict.
        // 缩略图 + 差异叠加（v0.9.0）：透明标记纹理叠在对齐拷贝上 —— 品红 = 剪影差、
        // 橙 = 配色冲突。
        const float kThumb = 96.f;
        const ImVec2 thumbPos = ImGui::GetCursorScreenPos();
        if (tex.slot[v]) {
            ImGui::Image((ImTextureID)(intptr_t)tex.slot[v], ImVec2(kThumb, kThumb));
        } else {
            ImGui::Dummy(ImVec2(kThumb, kThumb));
        }
        if (tex.slotDiff[v]) {
            ImGui::SetCursorScreenPos(thumbPos);
            ImGui::Image((ImTextureID)(intptr_t)tex.slotDiff[v], ImVec2(kThumb, kThumb));
        }
        ImGui::SetCursorScreenPos(thumbPos);
        ImGui::Dummy(ImVec2(kThumb, kThumb));   // re-anchor below the thumb / 锚回缩略图下方
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

        // ---- alignment fine-tune row (v0.9.0) ----
        // ---- 对齐微调行（v0.9.0）----
        if (st.views[v].loaded()) {
            const fb::AlignParams& al = st.views[v].align;
            const std::string alignHead = "对齐...###align" + std::to_string(v);
            if (ImGui::CollapsingHeader(alignHead.c_str())) {
                ImGui::TextDisabled("x%d 偏移(%+d,%+d) · 差异 %d 像素（剪影 %d · 配色 %d）",
                                    al.scale, al.dx, al.dy,
                                    st.views[v].maskDiffPx + st.views[v].colourConflictPx,
                                    st.views[v].maskDiffPx, st.views[v].colourConflictPx);
                const std::string vid = "##" + std::to_string(v);
                if (ImGui::SmallButton(("自动对齐" + vid).c_str())) st.autoAlignView(v);
                ImGui::SameLine();
                if (ImGui::SmallButton(("复位" + vid).c_str())) st.resetAlign(v);
                ImGui::SameLine();
                ImGui::TextDisabled("自动搜索 / 恢复默认");
                // Translation buttons: nudge 1px at a time. / 平移四键：每次 1px。
                if (ImGui::SmallButton(("左" + vid).c_str())) st.nudgeAlign(v, 0, -1, 0);
                ImGui::SameLine();
                if (ImGui::SmallButton(("右" + vid).c_str())) st.nudgeAlign(v, 0, +1, 0);
                ImGui::SameLine();
                if (ImGui::SmallButton(("上" + vid).c_str())) st.nudgeAlign(v, 0, 0, -1);
                ImGui::SameLine();
                if (ImGui::SmallButton(("下" + vid).c_str())) st.nudgeAlign(v, 0, 0, +1);
                ImGui::SameLine();
                ImGui::TextDisabled("平移 1px");
                // Scale buttons: integer factors only. / 缩放两键：只走整数倍。
                ImGui::BeginDisabled(al.scale <= 1);
                if (ImGui::SmallButton(("缩小" + vid).c_str())) st.nudgeAlign(v, -1, 0, 0);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(al.scale >= 4);
                if (ImGui::SmallButton(("放大" + vid).c_str())) st.nudgeAlign(v, +1, 0, 0);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("最近邻 x%d", al.scale);
            }
        }
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
            if (!st.pipelineWarning.empty()) {
                // Yellow: tolerated, not fatal. / 黄色：已容忍，非致命。
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.8f, 0.2f, 1.f));
                ImGui::TextWrapped("%s", st.pipelineWarning.c_str());
                ImGui::PopStyleColor();
            }
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
        const float colWalk = 90.f + cell + 24.f;   // second column x / 第二列 x
        // Every animation plays its own 6-cell loop; idle breathes (f0 f1 f0
        // f1 f0 f1 baked into the sheet) and walk runs its full cycle.
        // 每个动画播自己的 6 格循环；待机呼吸（表里就是 f0 f1 f0 f1 f0 f1），
        // 行走走完整循环。
        const int frame = ((int)(clockTime * (float)fps) % fb::SHEET_COLS +
                           fb::SHEET_COLS) % fb::SHEET_COLS;

        // ---- walk demo stage: up a while, then down, on a gently scrolling
        // ground so the direction switch reads clearly ----
        // ---- 走动演示舞台：朝上走一段再朝下走，地面轻度滚动，方向切换一目了然 ----
        {
            ImGui::Spacing();
            ImGui::TextUnformatted("走动演示（上走 → 下走 循环）");
            const float stageH = 224.f;
            const float stageW = ImGui::GetContentRegionAvail().x;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 ground   = IM_COL32(38, 42, 52, 255);
            const ImU32 stripe   = IM_COL32(52, 58, 72, 255);
            const ImU32 horizon  = IM_COL32(24, 27, 34, 255);
            dl->AddRectFilled(pos, ImVec2(pos.x + stageW, pos.y + stageH), ground);
            dl->AddRectFilled(pos, ImVec2(pos.x + stageW, pos.y + stageH * 0.32f), horizon);
            // Ground stripes scroll to sell "walking": down when the character
            // walks up, up when it walks down. Speed is deliberately gentle.
            // 地面条纹滚动营造"走动感"：角色朝上走时条纹下移，朝下走时上移。
            // 速度刻意轻度。
            constexpr float kPhaseSec = 3.f;   // seconds per direction / 每方向秒数
            constexpr float kStripe   = 24.f;  // stripe period in px / 条纹周期
            constexpr float kSpeed    = 14.f;  // scroll px per second / 每秒滚动像素
            const bool goingUp = fmodf(clockTime, 2.f * kPhaseSec) < kPhaseSec;
            const float dirSign = goingUp ? 1.f : -1.f;
            const float scrollY = fmodf(clockTime * kSpeed, kStripe);
            for (float y = -kStripe; y < stageH; y += kStripe) {
                const float yy = pos.y + stageH * 0.32f + y + dirSign * scrollY;
                dl->AddRectFilled(ImVec2(pos.x, yy),
                                  ImVec2(pos.x + stageW, yy + kStripe * 0.5f), stripe);
            }
            // Character: up_walk row 3 / down_walk row 1 of the sheet.
            // 角色：大图第 3 行（上走）/ 第 1 行（下走）。
            const int row = goingUp ? 3 : 1;
            ImVec2 uv0, uv1;
            cellUv(st.result, frame, row, uv0, uv1);
            ImGui::SetCursorScreenPos(
                ImVec2(pos.x + (stageW - cell) * 0.5f,
                       pos.y + stageH * 0.55f - cell * 0.5f));
            ImGui::Image((ImTextureID)(intptr_t)tex.sheet, ImVec2(cell, cell),
                         uv0, uv1);
            ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + stageH + 4.f));
            ImGui::TextDisabled("地面滚动方向随转身反转%s",
                                goingUp ? " · 朝上走" : " · 朝下走");
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("逐帧网格");
        // Column headers / 列头
        ImGui::TextDisabled("方向");
        ImGui::SameLine(90.f);
        ImGui::TextUnformatted("待机");
        ImGui::SameLine(colWalk);
        ImGui::TextUnformatted("行走");
        // 4 direction rows x 2 columns / 4 个方向行 x 2 列
        for (int d = 0; d < 4; ++d) {
            ImGui::TextUnformatted(kDirLabels[d]);
            ImGui::SameLine(90.f);
            ImVec2 uv0, uv1;
            cellUv(st.result, frame, d * 2, uv0, uv1);
            ImGui::Image((ImTextureID)(intptr_t)tex.sheet, ImVec2(cell, cell),
                         uv0, uv1);
            ImGui::SameLine(colWalk);
            cellUv(st.result, frame, d * 2 + 1, uv0, uv1);
            ImGui::Image((ImTextureID)(intptr_t)tex.sheet, ImVec2(cell, cell),
                         uv0, uv1);
        }
        ImGui::TextDisabled("每格 6 帧循环 · %dx%d · 最近邻放大",
                            st.result.sheet.cols, st.result.sheet.rows);
    } else {
        ImGui::TextDisabled("尚无预览 —— 载入三张视图后自动开播");
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("部件编辑器")) {
    st.mode = 1;
    drawPartEditor(st, tex);
    ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("捏人换装")) {
    st.mode = 2;
    drawDressUp(st, tex);
    ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    }

    // ---- status bar ----
    // 状态栏
    ImGui::Separator();
    ImGui::TextWrapped("%s", st.status.c_str());
}

}  // namespace ui
}  // namespace fb
