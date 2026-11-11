// dressup_panel.cpp — the dress-up tab (v0.10.2).
// 捏人 tab（v0.10.2）。
//
// v1 honest boundary (also stated in the UI): the animation shape comes from
// the motion chips — dress-up changes the colours and role layout, NOT the
// silhouette. Shape-level freedom is the P5 spike, not this release.
// v1 诚实边界（UI 文案也写明）：动作形状来自动作芯片 —— 换装改变的是配色与
// 角色色布局，不是剪影。形状自由是 P5 的 spike，不在本期承诺。
#include "dressup_panel.h"

#include "imgui.h"

#include <array>
#include <map>
#include <string>

namespace fb {
namespace ui {
namespace {

const char* kSlotNames[6] = {"hair", "face", "top", "pants", "boots", "body"};
const char* kSlotLabels[6] = {"发型", "脸", "上衣", "裤", "靴", "身体"};
const char* kViewLabels[3] = {"正面", "侧面", "背面"};

// Per-role working colours for the recolour grid (RGB floats for ImGui),
// initialised from the template palette. / 换色网格的逐角色工作色（ImGui 的
// RGB 浮点），初值取模板色板。
std::map<std::string, std::array<float, 3>>& editCols() {
    static std::map<std::string, std::array<float, 3>> cols;
    return cols;
}

void resetEditCols(const fb::TemplateSpec& spec) {
    auto& cols = editCols();
    cols.clear();
    for (const auto& kv : spec.palette)   // BGR -> RGB floats / BGR -> RGB 浮点
        cols[kv.first] = {kv.second[2] / 255.f, kv.second[1] / 255.f,
                          kv.second[0] / 255.f};
}

// Upsert/remove one role override: keeping the template colour removes the
// override (recording "changed to the same colour" would be noise).
// 增改/移除一条角色覆盖：改回模板色即移除覆盖（记"改成了同样的色"毫无意义）。
void syncOverride(fb::ComposeRecipe& recipe, const std::string& role,
                  const cv::Vec3b& templateBgr, const std::array<float, 3>& col) {
    const cv::Vec3b dst(
        (uchar)(col[2] * 255.f + 0.5f), (uchar)(col[1] * 255.f + 0.5f),
        (uchar)(col[0] * 255.f + 0.5f));
    auto& list = recipe.roleOverrides;
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].role != role) continue;
        if (dst == templateBgr) {
            list.erase(list.begin() + (long)i);
        } else {
            list[i].hasSrc = true;
            list[i].srcBgr = templateBgr;
            list[i].dstBgr = dst;
        }
        return;
    }
    if (dst == templateBgr) return;
    fb::LibraryItem::RoleColorOverride ro;
    ro.role = role;
    ro.hasSrc = true;
    ro.srcBgr = templateBgr;
    ro.dstBgr = dst;
    list.push_back(ro);
}

}  // namespace

void drawDressUp(AppState& st, PreviewTextures& tex) {
    const bool templOk = st.ensureTemplate();
    const bool libOk = st.ensureLibrary();
    if (st.composeDirty && libOk) st.recomposePreview();

    ImGui::BeginChild("du_left", ImVec2(360, 0), false);

    if (!libOk) {
        ImGui::TextWrapped("%s", st.libraryError.c_str());
        ImGui::EndChild();
        return;
    }
    ImGui::Text("部件库：%d 件（起步库 + 用户库，重名用户库胜）",
                (int)st.library.items.size());

    // ---- slot pickers: one row per slot, item buttons ----
    // 槽选择：每槽一行，物品按钮
    for (int s = 0; s < 6; ++s) {
        const std::string slot = kSlotNames[s];
        ImGui::Spacing();
        ImGui::TextUnformatted(kSlotLabels[s]);
        ImGui::SameLine(70.f);
        const auto items = st.library.inSlot(slot);
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) ImGui::SameLine();
            const bool sel = (st.recipe.items[slot] == items[i]->name);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button,
                                           ImVec4(0.24f, 0.52f, 0.88f, 1.f));
            ImGui::PushID(items[i]->name.c_str());
            if (ImGui::Button(items[i]->name.c_str())) {
                st.recipe.items[slot] = items[i]->name;
                st.composeDirty = true;
            }
            ImGui::PopID();
            if (sel) ImGui::PopStyleColor();
        }
        if (items.empty()) ImGui::TextDisabled("（该槽暂无物品）");
    }

    // ---- role recolour grid ----
    // 角色换色网格
    if (templOk) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("角色配色（改色即覆盖，改回原色即取消）");
        auto& cols = editCols();
        if (cols.empty()) resetEditCols(st.templateSpec);
        int n = 0;
        for (const auto& kv : st.templateSpec.palette) {
            auto& col = cols[kv.first];
            ImGui::PushID(kv.first.c_str());
            ImGui::SetNextItemWidth(110);
            if (ImGui::ColorEdit3("##c", col.data(),
                                  ImGuiColorEditFlags_NoTooltip |
                                      ImGuiColorEditFlags_NoInputs)) {
                syncOverride(st.recipe, kv.first, kv.second, col);
                st.composeDirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kv.first.c_str());
            ImGui::PopID();
            if (++n % 3 != 0) ImGui::SameLine();
        }
        if (ImGui::Button("恢复默认配色")) {
            st.recipe.roleOverrides.clear();
            resetEditCols(st.templateSpec);
            st.composeDirty = true;
        }
    }

    // ---- honest boundary + go ----
    // 诚实边界 + 开跑
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled(
        "v1 说明：动画动作形状来自动作库（逐帧手绘芯片），换装改变配色与"
        "角色色布局，不改变剪影形状。形状自由是后续实验方向。");
    if (ImGui::Button("生成动画  →")) {
        if (st.applyComposeToViews()) st.mode = 0;   // show the result / 展示成果
    }
    ImGui::SameLine();
    ImGui::TextDisabled("合成三视图并走管线出四方向大图");

    ImGui::EndChild();
    ImGui::SameLine();

    // ---- right: three composed views ----
    // 右：三张合成视图
    ImGui::BeginChild("du_right", ImVec2(0, 0), false);
    ImGui::TextUnformatted("三面预览（32x32 模板坐标）");
    for (int v = 0; v < 3; ++v) {
        ImGui::BeginGroup();
        ImGui::TextUnformatted(kViewLabels[v]);
        if (tex.dressup[v]) {
            ImGui::Image((ImTextureID)(intptr_t)tex.dressup[v],
                         ImVec2(192, 192));
        } else {
            ImGui::Dummy(ImVec2(192, 192));
        }
        ImGui::EndGroup();
        if (v < 2) ImGui::SameLine();
    }
    ImGui::Spacing();
    ImGui::TextDisabled(
        "改槽位或配色后预览自动更新。「生成动画」后切到「动画导出」tab 播放。");
    ImGui::EndChild();
}

}  // namespace ui
}  // namespace fb
