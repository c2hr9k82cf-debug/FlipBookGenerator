// app_state.h — GUI application state: three view slots + the fourdir pipeline
// GUI 应用状态：三个视图槽 + 四方向管线
//
// UI-free logic on purpose: no ImGui/GL types here, only core calls and
// strings, so the state machine stays readable and the GUI stays thin.
// 刻意不含 UI 逻辑：这里只有 core 调用与字符串，无 ImGui/GL 类型，状态机保持
// 可读，GUI 保持薄。
#pragma once

#include "core/core.h"

#include <string>

namespace fb {
namespace ui {

// One input view slot (front / side / back). The image must be
// template-aligned with the reference chips (strict extraction).
// 一个输入视图槽（front / side / back）。图必须与参考芯片模板对齐（严格提取）。
struct ViewSlot {
    std::string path;      // loaded image path / 已加载图的路径
    cv::Mat image;         // BGRA / BGRA 图
    bool loaded() const { return !image.empty(); }
    void clear() { path.clear(); image = cv::Mat(); }
};

class AppState {
public:
    static constexpr int FRONT = 0, SIDE = 1, BACK = 2;

    ViewSlot views[3];

    std::string chipsDir;
    fb::ChipSet chips;
    bool chipsReady = false;
    std::string chipsError;

    fb::FourDirResult result;
    bool hasResult = false;
    std::string pipelineError;   // extraction/pipeline message / 提取与管线错误

    // Status bar text / 状态栏文本
    std::string status = "拖入三张模板对齐视图（正面 / 侧面 / 背面）以开始。";

    // Bumped whenever the GL preview textures must be re-uploaded.
    // GL 预览纹理需重传时自增。
    unsigned long revision = 0;

    // Locate and load the chip bundle once. / 定位并加载一次芯片包。
    bool ensureChips();

    // Load one view's image, then auto-run the pipeline when all three are in.
    // 载入一张视图图；三张齐备时自动跑管线。
    bool setView(int idx, const std::string& path);

    // Drop one slot (or everything) and reset the pipeline — so the user can
    // start over without restarting the app. / 清掉一个槽（或全部）并重置管线 ——
    // 用户不必重启应用就能重来。
    void clearView(int idx);
    void clearAll();

    // Run the core pipeline (extract -> reskin -> assemble). / 跑核心管线。
    bool runPipeline();

    // Write the sheet PNG. / 写出大图 PNG。
    bool exportSheet(const std::string& path);
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
