// path_dialog.h — ImGui modal path dialog with clipboard feedback (v0.11.1).
// ImGui 模态路径框 + 剪贴板反馈（v0.11.1）。
//
// Why not pfd (B8): the system dialogs are osascript-driven and can fail
// silently — nothing appears and the button looks dead ("save does
// nothing"). This dialog is drawn by ImGui itself so it ALWAYS appears,
// and the outcome is always visible: a result line plus the path copied
// to the clipboard.
// 为何不用 pfd（B8）：系统对话框走 osascript，可能静默失败 —— 什么都不弹，
// 按钮像死了一样（"保存没反应"）。本框由 ImGui 自画，必弹；结局必可见：
// 结果行 + 路径自动复制进剪贴板。
#pragma once

#include <functional>
#include <string>

namespace fb {
namespace ui {

// Copy text to the system clipboard (pbcopy; no extra TCC permission).
// 把文本复制进系统剪贴板（pbcopy；无需额外权限）。
bool copyToClipboard(const std::string& text);

// Two modes:
//   openInput  — editable path + confirm button; confirm runs doRun(path)
//                and shows its result inline.
//   openResult — result-only popup (the action already ran), with the
//                path shown and copyable.
// 两种模式：
//   openInput  —— 可编辑路径 + 确认按钮；确认时执行 doRun(path)，就地显示结果。
//   openResult —— 纯结果框（动作已经执行），显示路径、可复制。
class PathDialog {
public:
    void openInput(const std::string& title, const std::string& defaultPath,
                   const std::string& confirmLabel,
                   std::function<bool(const std::string&, std::string&)> doRun);
    void openResult(const std::string& title, const std::string& message,
                    const std::string& pathToCopy, bool ok = true);
    void draw();   // once per frame / 每帧一次

private:
    bool wantOpen_ = false;
    bool inputMode_ = true;
    char path_[2048] = {0};
    std::string title_, confirm_;
    std::string message_;   // result line, empty until there is one / 结果行，无结果时为空
    bool ok_ = false;
    std::function<bool(const std::string&, std::string&)> doRun_;
};

}  // namespace ui
}  // namespace fb
