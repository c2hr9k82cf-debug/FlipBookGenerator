// app_state.cpp — GUI application state implementation
// GUI 应用状态实现

#include "app_state.h"
#include "assets_locator.h"

#include <algorithm>
#include <cctype>

namespace fb {
namespace ui {

std::string baseNameLower(const std::string& path) {
    std::string name = path;
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return name;
}

bool AppState::ensureChips() {
    if (chipsReady) return true;
    chipsError.clear();
    if (chipsDir.empty()) chipsDir = findChipsDir();
    if (chipsDir.empty()) {
        chipsError =
            "motion_chips not found. Set FLIPBOOK_ASSETS or run from the "
            "project root. / 找不到 motion_chips。请设 FLIPBOOK_ASSETS 或在项目"
            "根目录运行。";
        status = chipsError;
        return false;
    }
    std::string err;
    if (!fb::loadChipSet(chipsDir, chips, err)) {
        chipsError = err;
        chipsDir.clear();
        status = err;
        return false;
    }
    chipsReady = true;
    return true;
}

bool AppState::setView(int idx, const std::string& path) {
    if (idx < 0 || idx > 2) return false;
    cv::Mat img;
    if (!fb::loadImageBgra(path, img)) {
        views[idx].path = path;
        views[idx].image = cv::Mat();
        pipelineError = "cannot load image: " + path;
        status = pipelineError;
        ++revision;
        return false;
    }
    views[idx].path = path;
    views[idx].image = img;
    ++revision;
    return runPipeline();
}

bool AppState::runPipeline() {
    hasResult = false;
    pipelineError.clear();
    ++revision;

    // Not all views in yet: idle, not an error. / 三视图未齐：静默，不算错误。
    if (!views[FRONT].loaded() || !views[SIDE].loaded() || !views[BACK].loaded()) {
        int have = 0;
        for (const ViewSlot& v : views) if (v.loaded()) ++have;
        status = std::to_string(have) + " / 3 views loaded — drop or choose the rest."
                 " / 已载入 " + std::to_string(have) + " / 3 张 —— 拖入或选择其余视图。";
        return false;
    }

    if (!ensureChips()) return false;

    std::string err;
    if (!fb::buildFourDirSheet(views[FRONT].image, views[SIDE].image,
                               views[BACK].image, chips, result, err)) {
        result = fb::FourDirResult{};
        pipelineError = err;
        status = "Pipeline failed / 管线失败: " + err;
        return false;
    }
    hasResult = true;
    status = "Sheet ready: " + std::to_string(result.sheet.cols) + "x" +
             std::to_string(result.sheet.rows) + " (" +
             std::to_string(fb::SHEET_ROWS) + " rows x " +
             std::to_string(fb::SHEET_COLS) + " cols). / 大图就绪。";
    return true;
}

bool AppState::exportSheet(const std::string& path) {
    if (!hasResult) return false;
    std::string err;
    if (!fb::saveImageBgra(path, result.sheet, err)) {
        status = err;
        return false;
    }
    status = "Exported: " + path;
    return true;
}

int assignDrops(AppState& st, const std::vector<std::string>& paths) {
    int assigned = 0;
    for (const std::string& p : paths) {
        const std::string name = baseNameLower(p);
        int target = -1;
        if (name.find("front") != std::string::npos)      target = AppState::FRONT;
        else if (name.find("side") != std::string::npos)  target = AppState::SIDE;
        else if (name.find("back") != std::string::npos)  target = AppState::BACK;
        if (target < 0) {
            for (int v = 0; v < 3; ++v)
                if (!st.views[v].loaded()) { target = v; break; }
        }
        if (target < 0) continue;   // all slots loaded and no name match / 槽满且名字没匹配
        st.setView(target, p);
        ++assigned;
    }
    return assigned;
}

}  // namespace ui
}  // namespace fb
