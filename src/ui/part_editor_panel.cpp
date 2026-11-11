// part_editor_panel.cpp — the in-tool part editor (v0.10.0).
// 工具内部件编辑器（v0.10.0）。
//
// Interaction model (deliberately minimal, per plan): role-colour brush
// (drag), eraser (drag, right button works too), whole-region fill (click),
// ONE undo step, import with integer fine-tune, "infer back from front",
// save into the user library, library import/export as folder copies.
// 交互模型（按计划刻意最小）：角色色画笔（拖）、橡皮（拖，右键亦可）、整区填色
// （点）、一步撤销、导入整数微调、「从正面推断」、存入用户库、库导入/导出为
// 文件夹复制。
#include "part_editor_panel.h"

#include "assets_locator.h"

#include "imgui.h"
#include "portable-file-dialogs.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace fb {
namespace ui {
namespace {

const char* kSlotNames[6] = {"hair", "face", "top", "pants", "boots", "body"};
const char* kSlotLabels[6] = {"发型", "脸", "上衣", "裤", "靴", "身体"};
const char* kViewLabels[3] = {"正面", "侧面", "背面"};

enum Tool { TOOL_BRUSH = 0, TOOL_ERASER = 1, TOOL_FILL = 2 };

// cv::Vec4b (BGRA) -> ImVec4 (RGBA). / cv::Vec4b（BGRA）-> ImVec4（RGBA）。
ImVec4 toImVec4(const cv::Vec4b& c) {
    return ImVec4(c[2] / 255.f, c[1] / 255.f, c[0] / 255.f, c[3] / 255.f);
}

// Recursive folder copy (library import/export). Overwrites files in place.
// 递归复制文件夹（库导入/导出）。同名文件就地覆盖。
bool copyLibraryDir(const std::string& from, const std::string& to,
                    std::string& err) {
    std::error_code ec;
    if (!fs::is_directory(from, ec)) {
        err = "不是目录：" + from;
        return false;
    }
    fs::create_directories(to, ec);
    for (const fs::directory_entry& e :
         fs::recursive_directory_iterator(from, ec)) {
        const fs::path rel = fs::relative(e.path(), from, ec);
        const fs::path dst = fs::path(to) / rel;
        if (e.is_directory()) {
            fs::create_directories(dst, ec);
        } else if (e.is_regular_file()) {
            fs::create_directories(dst.parent_path(), ec);
            fs::copy_file(e.path(), dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                err = "复制失败：" + e.path().string() + "（" + ec.message() + "）";
                return false;
            }
        }
    }
    return true;
}

}  // namespace

void drawPartEditor(AppState& st, PreviewTextures& tex) {
    fb::PartEditor& ed = st.editor;
    static int tool = TOOL_BRUSH;
    static std::string brushRole = "hair_mid";
    static char nameBuf[64] = "untitled";
    static bool prevPaint = false, prevErase = false;
    static bool inited = false;

    // One-time: open a fresh item so the canvas is never a dead end.
    // 一次性：开一件新部件，画布永远不是死胡同。
    if (!inited) {
        inited = true;
        if (!ed.viewReady(0) && !ed.viewReady(1) && !ed.viewReady(2)) {
            ed.blank(cv::Size(32, 32));
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", ed.itemName.c_str());
        }
    }
    const bool templOk = st.ensureTemplate();

    ImGui::BeginChild("ed_left", ImVec2(340, 0), false);

    // ---- slot + name ----
    // 槽 + 名称
    ImGui::TextUnformatted("槽位");
    for (int i = 0; i < 6; ++i) {
        if (i) ImGui::SameLine();
        const bool sel = (ed.slot == kSlotNames[i]);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button,
                                       ImVec4(0.24f, 0.52f, 0.88f, 1.f));
        if (ImGui::Button(kSlotLabels[i])) {
            ed.slot = kSlotNames[i];
            st.hasImport = false;
        }
        if (sel) ImGui::PopStyleColor();
    }
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("部件名", nameBuf, sizeof(nameBuf)))
        ed.itemName = nameBuf;

    // ---- new / import ----
    // 新建 / 导入
    if (ImGui::Button("从空白新建")) {
        ed.blank(cv::Size(32, 32));
        st.hasImport = false;
        ++st.revision;
    }
    ImGui::SameLine();
    if (ImGui::Button("导入图片...")) {
        const std::vector<std::string> files = pfd::open_file(
            "选择部件图片", ".",
            {"图片文件", "*.png *.jpg *.jpeg *.bmp"}).result();
        if (!files.empty()) st.editorImport(files[0]);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("也可直接拖入窗口");

    // ---- view tabs ----
    // 视图 tab
    ImGui::Spacing();
    ImGui::TextUnformatted("视图");
    for (int v = 0; v < 3; ++v) {
        if (v) ImGui::SameLine();
        const bool sel = (ed.view == v);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button,
                                       ImVec4(0.24f, 0.52f, 0.88f, 1.f));
        if (ImGui::Button(kViewLabels[v])) ed.setView(v);
        if (sel) ImGui::PopStyleColor();
        if (ed.viewReady(v)) {
            ImGui::SameLine();
            ImGui::TextDisabled(ed.touched[v] ? "*" : "✓");
        }
    }
    if (ed.view == fb::PartEditor::BACK) {
        ImGui::BeginDisabled(!ed.viewReady(fb::PartEditor::FRONT) || !templOk);
        if (ImGui::Button("从正面推断（无手绘背面时用）")) {
            std::string err;
            if (ed.inferBack(st.templateSpec, err)) {
                st.status = "背面已从正面推断（头发镜像 + 身体压平）。";
                ++st.revision;
            } else {
                st.status = "推断失败：" + err;
            }
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("手绘了背面则直接用，无需推断。");
    }

    // ---- import fine-tune (P1 engine) ----
    // 导入微调（P1 引擎）
    if (st.hasImport) {
        ImGui::Spacing();
        ImGui::TextDisabled("导入微调：x%d 偏移(%+d,%+d)",
                            st.importAlign.scale, st.importAlign.dx,
                            st.importAlign.dy);
        if (ImGui::SmallButton("自动对齐")) {
            st.importAlign = fb::AlignParams{};
            if (st.ensureTemplate()) {
                const cv::Mat ref = fb::renderTemplateView(
                    st.templateSpec, ed.view, st.templateError);
                if (!ref.empty()) fb::autoAlign(st.importRaw, ref, st.importAlign);
            }
            st.resetImport();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("复位")) {
            st.importAlign = fb::AlignParams{};
            st.resetImport();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("左")) st.nudgeImport(0, -1, 0);
        ImGui::SameLine();
        if (ImGui::SmallButton("右")) st.nudgeImport(0, +1, 0);
        ImGui::SameLine();
        if (ImGui::SmallButton("上")) st.nudgeImport(0, 0, -1);
        ImGui::SameLine();
        if (ImGui::SmallButton("下")) st.nudgeImport(0, 0, +1);
        ImGui::SameLine();
        if (ImGui::SmallButton("缩小")) st.nudgeImport(-1, 0, 0);
        ImGui::SameLine();
        if (ImGui::SmallButton("放大")) st.nudgeImport(+1, 0, 0);
    }

    // ---- tools ----
    // 工具
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("工具（画笔/橡皮按住拖动，填色点一下）");
    ImGui::RadioButton("画笔", &tool, TOOL_BRUSH);
    ImGui::SameLine();
    ImGui::RadioButton("橡皮", &tool, TOOL_ERASER);
    ImGui::SameLine();
    ImGui::RadioButton("填色", &tool, TOOL_FILL);
    ImGui::SameLine();
    ImGui::BeginDisabled(!ed.hasUndo);
    if (ImGui::Button("撤销一步")) {
        ed.undo();
        ++st.revision;
    }
    ImGui::EndDisabled();

    // ---- role palette (brush colour) ----
    // 角色色板（画笔色）
    if (templOk) {
        ImGui::TextUnformatted("角色色（画笔/填色目标）");
        int n = 0;
        for (const auto& kv : st.templateSpec.palette) {
            const bool sel = (kv.first == brushRole);
            if (sel) {
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImVec4(0.1f, 0.1f, 0.1f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_Border,
                                      ImVec4(0.24f, 0.52f, 0.88f, 1.f));
            }
            const cv::Vec4b c(kv.second[0], kv.second[1], kv.second[2], 255);
            ImGui::PushID(kv.first.c_str());
            if (ImGui::ColorButton("##sw", toImVec4(c),
                                   ImGuiColorEditFlags_NoTooltip |
                                       ImGuiColorEditFlags_NoBorder |
                                       ImGuiColorEditFlags_NoDragDrop,
                                   ImVec2(22, 22))) {
                brushRole = kv.first;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kv.first.c_str());
            ImGui::PopID();
            if (sel) ImGui::PopStyleColor(2);
            if (++n % 8 != 0) ImGui::SameLine();
        }
        ImGui::TextDisabled("当前：%s", brushRole.c_str());
    }

    // ---- library: list + save + import/export ----
    // 库：列表 + 保存 + 导入/导出
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("部件库");
    const std::string libRoot = userLibraryRoot();
    ImGui::BeginDisabled(libRoot.empty() || ed.itemName.empty());
    if (ImGui::Button("保存到库")) {
        std::string err;
        if (!fb::saveEditorItem(ed, libRoot, err)) {
            st.status = "保存失败：" + err;
        } else {
            const int viol = templOk ? fb::roleConstraintViolations(
                                           ed.pixels[fb::PartEditor::FRONT],
                                           st.templateSpec)
                                     : 0;
            st.status = "已保存：" + libRoot + "/" + ed.slot + "/" + ed.itemName +
                        (viol ? "（警告：" + std::to_string(viol) +
                                    " 个非角色色像素，全局换色不保证覆盖它们）"
                              : "");
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("导入库...")) {
        const std::string dir = pfd::select_folder("选择要导入的部件库目录").result();
        if (!dir.empty()) {
            std::string err;
            if (copyLibraryDir(dir, libRoot, err)) st.status = "库已导入：" + libRoot;
            else st.status = err;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("导出库...")) {
        const std::string dir = pfd::select_folder("选择导出目标目录").result();
        if (!dir.empty()) {
            std::string err;
            if (copyLibraryDir(libRoot, dir + "/parts_library", err))
                st.status = "库已导出：" + dir + "/parts_library";
            else st.status = err;
        }
    }
    if (!libRoot.empty()) {
        ImGui::TextDisabled("库中「%s」槽物品：", kSlotLabels[
            (int)(std::find(std::begin(kSlotNames), std::end(kSlotNames), ed.slot) -
                  std::begin(kSlotNames)) % 6]);
        std::error_code ec;
        const fs::path slotDir = fs::path(libRoot) / ed.slot;
        int shown = 0;
        if (fs::is_directory(slotDir, ec)) {
            for (const fs::directory_entry& e : fs::directory_iterator(slotDir, ec)) {
                if (!e.is_directory()) continue;
                ++shown;
                if (ImGui::Button(e.path().filename().string().c_str())) {
                    std::string err;
                    if (fb::loadEditorItem(ed, e.path().string(), ed.slot,
                                           cv::Size(32, 32), err)) {
                        std::snprintf(nameBuf, sizeof(nameBuf), "%s",
                                      ed.itemName.c_str());
                        st.hasImport = false;
                        ++st.revision;
                        st.status = "已载入：" + e.path().filename().string();
                    } else {
                        st.status = "载入失败：" + err;
                    }
                }
                ImGui::SameLine();
            }
        }
        if (!shown) ImGui::TextDisabled("（空）");
    }

    ImGui::EndChild();
    ImGui::SameLine();

    // ---- right: the canvas ----
    // 右：画布
    ImGui::BeginChild("ed_right", ImVec2(0, 0), false);
    if (!templOk) {
        ImGui::TextWrapped("%s", st.templateError.c_str());
    } else {
        cv::Mat& px = ed.pixels[ed.view];
        if (px.empty()) {
            ImGui::TextDisabled("此视图为空 —— 画一笔、导入图片，或从正面推断。");
        }
        const cv::Mat& view = px.empty() ? ed.pixels[ed.view] : px;
        // Empty views still get a canvas frame so layout stays put: draw a
        // scratch blank. / 空视图也画个画框稳定版面：用临时空白。
        cv::Mat scratch;
        const cv::Mat& draw = view.empty()
            ? (scratch = cv::Mat::zeros(ed.canvas, CV_8UC4)) : view;
        const fb::ui::CanvasInput in =
            tex.editorCanvas.draw("ed_canvas", draw, st.revision);

        ImGui::Spacing();
        ImGui::SliderInt("缩放", &tex.editorCanvas.zoom, 1, 24);
        ImGui::SameLine();
        ImGui::Checkbox("网格", &tex.editorCanvas.showGrid);
        ImGui::SameLine();
        if (in.px >= 0) {
            const cv::Vec4b c = draw.at<cv::Vec4b>(in.py, in.px);
            ImGui::TextDisabled("(%d,%d) RGBA(%d,%d,%d,%d)", in.px, in.py,
                                c[2], c[1], c[0], c[3]);
        } else {
            ImGui::TextDisabled("把鼠标移到画布上");
        }

        // Tool application. Snapshot on the down edge (one-step undo).
        // 工具落笔。按下沿拍快照（一步撤销）。
        if (in.px >= 0 && !view.empty()) {
            const bool down = in.painted || in.erased;
            if (down && !prevPaint && !prevErase) ed.snapshot();
            if (tool == TOOL_BRUSH && in.painted) {
                cv::Vec3b bgr;
                if (st.templateSpec.roleBgr(brushRole, bgr))
                    ed.paint(ed.view, in.px, in.py, bgr);
                ++st.revision;
            } else if (tool == TOOL_ERASER && in.erased) {
                ed.erase(ed.view, in.px, in.py);
                ++st.revision;
            } else if (tool == TOOL_FILL && in.painted && !prevPaint) {
                // Click: recolour the clicked colour to the brush colour.
                // 点一下：把被点颜色整区换成画笔色。
                const cv::Vec4b src = view.at<cv::Vec4b>(in.py, in.px);
                cv::Vec3b bgr;
                if (st.templateSpec.roleBgr(brushRole, bgr))
                    ed.fillRole(ed.view, cv::Vec3b(src[0], src[1], src[2]), bgr);
                ++st.revision;
            }
        }
        prevPaint = in.painted;
        prevErase = in.erased;
    }
    ImGui::EndChild();
}

}  // namespace ui
}  // namespace fb
