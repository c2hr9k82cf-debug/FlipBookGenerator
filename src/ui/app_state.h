// app_state.h — GUI application state: three view slots + the fourdir pipeline
// GUI 应用状态：三个视图槽 + 四方向管线
//
// UI-free logic on purpose: no ImGui/GL types here, only core calls and
// strings, so the state machine stays readable and the GUI stays thin.
// 刻意不含 UI 逻辑：这里只有 core 调用与字符串，无 ImGui/GL 类型，状态机保持
// 可读，GUI 保持薄。
#pragma once

#include "core/align.h"
#include "core/core.h"
#include "core/part_editor.h"
#include "core/template_spec.h"

#include <string>

namespace fb {
namespace ui {

// One input view slot (front / side / back). `image` is the raw file; `aligned`
// is the integer-aligned 32x32 copy the pipeline actually consumes (v0.9.0).
// `diffOverlay` is the transparent diff-mark texture (magenta = silhouette
// diff, orange = colour conflict), empty when there is nothing to show.
// 一个输入视图槽（front / side / back）。image 是原始文件；aligned 是管线真正
// 消费的整数对齐 32x32 拷贝（v0.9.0）。diffOverlay 是透明差异标记图（品红 = 剪影
// 差、橙 = 配色冲突），无差异时为空。
struct ViewSlot {
    std::string path;      // loaded image path / 已加载图的路径
    cv::Mat image;         // raw BGRA / 原始 BGRA 图
    fb::AlignParams align; // integer scale + translation / 整数缩放 + 平移
    cv::Mat aligned;       // aligned copy fed to the pipeline / 喂给管线的对齐拷贝
    cv::Mat diffOverlay;   // BGRA diff marks (transparent base) / BGRA 差异标记（透明底）
    int maskDiffPx = 0;        // silhouette diff pixels / 剪影差像素数
    int colourConflictPx = 0;  // colour-conflict pixels / 配色冲突像素数
    bool loaded() const { return !image.empty(); }
    void clear() {
        path.clear();
        image = cv::Mat();
        align = fb::AlignParams{};
        aligned = cv::Mat();
        diffOverlay = cv::Mat();
        maskDiffPx = colourConflictPx = 0;
    }
};

class AppState {
public:
    static constexpr int FRONT = 0, SIDE = 1, BACK = 2;
    // Which tab is active (0 = animation export, 1 = part editor). Drops are
    // routed by it. / 当前 tab（0 = 动画导出，1 = 部件编辑器）。拖放按它分派。
    int mode = 0;

    ViewSlot views[3];

    std::string chipsDir;
    fb::ChipSet chips;
    bool chipsReady = false;
    std::string chipsError;

    fb::FourDirResult result;
    bool hasResult = false;
    std::string pipelineError;   // extraction/pipeline message / 提取与管线错误
    std::string pipelineWarning; // non-fatal (e.g. tolerated mask diff) / 非致命提醒

    // Status bar text / 状态栏文本
    std::string status = "拖入三张模板对齐视图（正面 / 侧面 / 背面）以开始。";

    // Bumped whenever the GL preview textures must be re-uploaded.
    // GL 预览纹理需重传时自增。
    unsigned long revision = 0;

    // Locate and load the chip bundle once. / 定位并加载一次芯片包。
    bool ensureChips();

    // Load one view's image (auto-aligned on load), then auto-run the pipeline
    // when all three are in. / 载入一张视图图（载入即自动对齐）；三张齐备时自动跑管线。
    bool setView(int idx, const std::string& path);

    // Manual fine-tune of one slot's alignment (v0.9.0): nudge scale/offset,
    // re-align, re-run the whole pipeline (milliseconds — no debounce needed).
    // 手动微调一个槽的对齐（v0.9.0）：改缩放/偏移、重新对齐、重跑整条管线
    // （毫秒级 —— 无需防抖）。
    void nudgeAlign(int idx, int dScale, int dx, int dy);
    // Re-run the automatic search for one slot. / 给一个槽重跑自动搜索。
    void autoAlignView(int idx);
    // Restore the params the last auto search chose. / 恢复上次自动搜索选定的参数。
    void resetAlign(int idx);

    // Drop one slot (or everything) and reset the pipeline — so the user can
    // start over without restarting the app. / 清掉一个槽（或全部）并重置管线 ——
    // 用户不必重启应用就能重来。
    void clearView(int idx);
    void clearAll();

    // Run the core pipeline (extract -> reskin -> assemble). / 跑核心管线。
    bool runPipeline();

    // Write the sheet PNG. / 写出大图 PNG。
    bool exportSheet(const std::string& path);

    // ---- part editor (v0.10.0) / 部件编辑器（v0.10.0） ----
    fb::PartEditor editor;
    fb::TemplateSpec templateSpec;
    bool templateReady = false;
    std::string templateError;

    // Import staging: the raw image + integer alignment params, so the user
    // can fine-tune after the auto search (P1 engine, same as the view slots).
    // 导入暂存：原图 + 整数对齐参数，自动搜索后还能微调（P1 引擎，与视图槽同款）。
    cv::Mat importRaw;
    fb::AlignParams importAlign;
    bool hasImport = false;

    // Locate + load the character template once. / 定位并加载一次角色模板。
    bool ensureTemplate();
    // Import a PNG into the editor's active view (auto-aligned to the
    // template silhouette). / 导入 PNG 到编辑器当前视图（对模板剪影自动对齐）。
    bool editorImport(const std::string& path);
    // Fine-tune the staged import (scale/offset), re-apply to the view.
    // 微调暂存导入（缩放/偏移），重新贴到视图。
    void nudgeImport(int dScale, int dx, int dy);
    void resetImport();
};

// Lowercased file name without directory (for front/side/back matching).
// 去目录的小写文件名（用于 front/side/back 匹配）。
std::string baseNameLower(const std::string& path);

// Assign dropped files to slots: by front/side/back in the name, else to the
// first empty slot. Returns how many files were assigned.
// 把拖入文件分配到槽：按文件名里的 front/side/back，否则填第一个空槽。
// 返回分配的文件数。
int assignDrops(AppState& st, const std::vector<std::string>& paths);

}  // namespace ui
}  // namespace fb
