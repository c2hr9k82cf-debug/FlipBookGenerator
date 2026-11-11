// path_dialog.cpp — ImGui modal path dialog implementation (v0.11.1).
// ImGui 模态路径框实现（v0.11.1）。
#include "path_dialog.h"

#include "imgui.h"

#include <cstdio>

namespace fb {
namespace ui {

bool copyToClipboard(const std::string& text) {
    FILE* f = popen("pbcopy", "w");
    if (!f) return false;
    fwrite(text.data(), 1, text.size(), f);
    return pclose(f) == 0;
}

void PathDialog::openInput(
    const std::string& title, const std::string& defaultPath,
    const std::string& confirmLabel,
    std::function<bool(const std::string&, std::string&)> doRun) {
    title_ = title;
    confirm_ = confirmLabel.empty() ? "确定" : confirmLabel;
    std::snprintf(path_, sizeof(path_), "%s", defaultPath.c_str());
    message_.clear();
    ok_ = false;
    doRun_ = std::move(doRun);
    inputMode_ = true;
    wantOpen_ = true;
}

void PathDialog::openResult(const std::string& title,
                            const std::string& message,
                            const std::string& pathToCopy, bool ok) {
    title_ = title;
    std::snprintf(path_, sizeof(path_), "%s", pathToCopy.c_str());
    message_ = message;
    ok_ = ok;
    if (ok_ && !pathToCopy.empty()) copyToClipboard(pathToCopy);
    inputMode_ = false;
    wantOpen_ = true;
}

void PathDialog::draw() {
    if (wantOpen_) {
        ImGui::OpenPopup(title_.c_str());
        wantOpen_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(700, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title_.c_str(), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    if (inputMode_) {
        ImGui::TextUnformatted("路径（可编辑）：");
        ImGui::SetNextItemWidth(640);
        ImGui::InputText("##path", path_, sizeof(path_));
        if (ImGui::Button("复制路径")) copyToClipboard(path_);
        ImGui::SameLine();
        ImGui::TextDisabled("保存成功后路径也会自动进剪贴板");
        ImGui::Spacing();
        if (ImGui::Button(confirm_.c_str())) {
            std::string err;
            if (doRun_ && doRun_(path_, err)) {
                copyToClipboard(path_);
                message_ = std::string("已完成：") + path_ +
                           "\n（路径已复制到剪贴板）";
                ok_ = true;
            } else {
                message_ = err.empty() ? "操作失败。" : ("失败：" + err);
                ok_ = false;
            }
        }
        ImGui::SameLine();
    }

    if (!message_.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ok_ ? ImVec4(0.35f, 0.78f, 0.35f, 1.f)
                                                 : ImVec4(1.f, 0.38f, 0.38f, 1.f));
        ImGui::TextWrapped("%s", message_.c_str());
        ImGui::PopStyleColor();
        if (path_[0]) {
            if (ImGui::Button("复制路径")) copyToClipboard(path_);
            ImGui::SameLine();
        }
    }

    if (ImGui::Button(inputMode_ && message_.empty() ? "取消" : "关闭"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

}  // namespace ui
}  // namespace fb
