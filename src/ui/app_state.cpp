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
            "找不到动作芯片（motion_chips）。请设置环境变量 FLIPBOOK_ASSETS，"
            "或在项目根目录运行。";
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
        views[idx].clear();
        views[idx].path = path;
        pipelineError = "无法加载图片：" + path;
        status = pipelineError;
        ++revision;
        return false;
    }
    views[idx].path = path;
    views[idx].image = img;
    // Auto-align on load (v0.9.0): brute-force integer search against this
    // view's idle chip frame 0. Falls back to identity when chips are missing.
    // 载入即自动对齐（v0.9.0）：对本视图 idle 芯片第 0 帧暴力搜索整数参数。
    // 芯片缺失时退回恒等。
    autoAlignView(idx);
    ++revision;
    return runPipeline();
}

void AppState::nudgeAlign(int idx, int dScale, int dx, int dy) {
    if (idx < 0 || idx > 2 || !views[idx].loaded()) return;
    ViewSlot& v = views[idx];
    fb::AlignParams& p = v.align;
    p.scale = std::max(1, std::min(4, p.scale + dScale));
    p.dx += dx;
    p.dy += dy;
    v.aligned = fb::applyAlign(v.image, p,
                               chipsReady ? chips.cell : v.image.size());
    ++revision;
    runPipeline();
}

void AppState::autoAlignView(int idx) {
    if (idx < 0 || idx > 2 || !views[idx].loaded()) return;
    ViewSlot& v = views[idx];
    v.align = fb::AlignParams{};
    // Target is the chip cell (32x32) — the template coordinate system.
    // 目标尺寸是芯片格（32x32）—— 模板坐标系。
    cv::Size target = v.image.size();
    if (ensureChips()) {
        target = chips.cell;
        static const char* kIdleName[3] = {"front_idle", "side_idle", "back_idle"};
        const fb::MotionChips* idle = chips.find(kIdleName[idx]);
        if (idle && !idle->frames.empty())
            fb::autoAlign(v.image, idle->frames[0], v.align);
    }
    v.aligned = fb::applyAlign(v.image, v.align, target);
    ++revision;
}

void AppState::resetAlign(int idx) {
    if (idx < 0 || idx > 2 || !views[idx].loaded()) return;
    // Identity: what "no correction" means. / 恒等：即"不做修正"。
    ViewSlot& v = views[idx];
    v.align = fb::AlignParams{};
    v.aligned = fb::applyAlign(v.image, v.align,
                              chipsReady ? chips.cell : v.image.size());
    ++revision;
    runPipeline();
}

void AppState::clearView(int idx) {
    if (idx < 0 || idx > 2) return;
    views[idx].clear();
    ++revision;
    runPipeline();   // recompute status for the remaining views / 为剩余视图重算状态
}

void AppState::clearAll() {
    for (ViewSlot& v : views) v.clear();
    result = fb::FourDirResult{};
    hasResult = false;
    pipelineError.clear();
    pipelineWarning.clear();
    ++revision;
    status = "已清空 —— 拖入三张模板对齐视图重新开始。";
}

bool AppState::runPipeline() {
    hasResult = false;
    pipelineError.clear();
    pipelineWarning.clear();
    ++revision;

    // Per-view diff diagnostics run even before all three views are in — the
    // point of v0.9.0 is "drop one image, see what is wrong with it".
    // 单视图差异诊断在三张齐之前就跑 —— v0.9.0 的意义正是"拖一张图进来，就看见
    // 哪里不行"。
    const char* viewNames[3] = {"正面", "侧面", "背面"};
    static const char* kIdleName[3] = {"front_idle", "side_idle", "back_idle"};
    if (ensureChips()) {
        for (int v = 0; v < 3; ++v) {
            ViewSlot& slot = views[v];
            if (!slot.loaded()) { slot.diffOverlay = cv::Mat(); continue; }
            if (slot.aligned.empty())
                slot.aligned = fb::applyAlign(slot.image, slot.align, chips.cell);
            const fb::MotionChips* idle = chips.find(kIdleName[v]);
            slot.diffOverlay = cv::Mat();
            slot.maskDiffPx = slot.colourConflictPx = 0;
            if (!idle || idle->frames.empty()) continue;
            fb::PaletteMap dummy;
            fb::ExtractStats st;
            std::string diagErr;
            // Tolerant so a mask diff yields stats instead of failing; a
            // function conflict fails but still leaves the conflict mask.
            // 宽容模式让剪影差出诊断而不是失败；函数性冲突虽失败，冲突掩码照留。
            fb::extractPaletteMap(slot.aligned, idle->frames[0], dummy, diagErr,
                                  &st, /*tolerant=*/true);
            slot.maskDiffPx = st.maskDiffPixels;
            slot.colourConflictPx = st.colourConflictPixels;
            slot.diffOverlay = fb::renderDiffOverlay(slot.aligned, st.maskDiffMask,
                                                     st.colourConflictMask);
        }
    }

    // Not all views in yet: idle, not an error. / 三视图未齐：静默，不算错误。
    if (!views[FRONT].loaded() || !views[SIDE].loaded() || !views[BACK].loaded()) {
        int have = 0;
        for (const ViewSlot& v : views) if (v.loaded()) ++have;
        status = "已载入 " + std::to_string(have) +
                 " / 3 张视图 —— 拖入或点「选择」补齐其余视图。";
        return false;
    }

    if (!ensureChips()) return false;

    // Tolerant mode: a silhouette mismatch warns instead of rejecting (the
    // sheet's shape comes from the chips; stray pixels cost nothing). The
    // pipeline consumes the ALIGNED copies (v0.9.0) — buildFourDirSheet's
    // signature is untouched, it just receives better inputs.
    // 宽容模式：剪影不齐只提醒、不拒绝（大图形状来自芯片；杂散像素无伤大雅）。
    // 管线消费对齐拷贝（v0.9.0）—— buildFourDirSheet 签名不动，只是收到更好的输入。
    fb::ExtractStats stats[3];
    std::string err;
    if (!fb::buildFourDirSheet(views[FRONT].aligned, views[SIDE].aligned,
                               views[BACK].aligned, chips, result, err,
                               stats, /*tolerantMask=*/true)) {
        result = fb::FourDirResult{};
        pipelineError = err;
        status = "管线失败：" + err;
        return false;
    }
    hasResult = true;

    std::string warn;
    for (int v = 0; v < 3; ++v) {
        ViewSlot& slot = views[v];
        if (slot.maskDiffPx > 0) {
            if (!warn.empty()) warn += " ";
            warn += std::string(viewNames[v]) + "：剪影与模板差 " +
                    std::to_string(slot.maskDiffPx) +
                    " 像素（见缩略图红点；可点「自动对齐」或手动微调）。";
        }
        if (slot.align.scale > 1 || slot.align.dx != 0 || slot.align.dy != 0) {
            if (!warn.empty()) warn += " ";
            warn += std::string(viewNames[v]) + "：已按 x" +
                    std::to_string(slot.align.scale) + " 缩放、(" +
                    std::to_string(slot.align.dx) + "," +
                    std::to_string(slot.align.dy) + ") 平移对齐。";
        }
    }
    pipelineWarning = warn;

    status = "大图就绪：" + std::to_string(result.sheet.cols) + "x" +
             std::to_string(result.sheet.rows) + "（" +
             std::to_string(fb::SHEET_ROWS) + " 行 x " +
             std::to_string(fb::SHEET_COLS) + " 列）。";
    return true;
}

bool AppState::exportSheet(const std::string& path) {
    if (!hasResult) return false;
    std::string err;
    if (!fb::saveImageBgra(path, result.sheet, err)) {
        status = err;
        return false;
    }
    status = "已导出：" + path;
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
