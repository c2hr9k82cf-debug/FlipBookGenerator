// assets_locator.cpp — runtime asset location (macOS .app aware)
// 运行时资源定位（适配 macOS .app）

#include "assets_locator.h"

#include <cstdlib>
#include <filesystem>

#if defined(__APPLE__)
#include <mach-o/dyld.h>   // _NSGetExecutablePath / 取可执行文件路径
#endif

namespace fb {
namespace ui {
namespace {

namespace fs = std::filesystem;

// The chips are valid if index.txt is there. / 目录里有 index.txt 即有效。
bool isChipsDir(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / "index.txt", ec);
}

// A candidate root may be either the assets/ dir or the motion_chips/ dir.
// 候选根可以是 assets/ 目录，也可以直接是 motion_chips/ 目录。
bool tryRoot(const fs::path& root, std::string& out) {
    std::error_code ec;
    if (isChipsDir(root)) { out = root.string(); return true; }
    const fs::path nested = root / "templates" / "motion_chips";
    if (isChipsDir(nested)) { out = nested.string(); return true; }
    return false;
}

// The template JSON lives next to the chips: <assets>/templates/.
// 模板 JSON 与芯片同区：<assets>/templates/。
bool tryTemplate(const fs::path& root, std::string& out) {
    std::error_code ec;
    for (const fs::path& p : {root / "chibi_standard.json",
                              root / "templates" / "chibi_standard.json"}) {
        if (fs::is_regular_file(p, ec)) { out = p.string(); return true; }
    }
    return false;
}

fs::path executableDir() {
#if defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        std::error_code ec;
        fs::path p = fs::weakly_canonical(buf, ec);   // resolve symlink / 解析符号链接
        if (ec) p = buf;
        return p.parent_path();
    }
#endif
    return ".";
}

}  // namespace

std::string findChipsDir() {
    std::string found;

    // 1. Explicit override / 显式覆盖
    if (const char* env = std::getenv("FLIPBOOK_ASSETS")) {
        if (tryRoot(env, found)) return found;
    }

    const fs::path exe = executableDir();

    // 2. .app layout: Contents/MacOS/../Resources/assets / .app 布局
    if (tryRoot(exe / ".." / "Resources" / "assets", found)) return found;

    // 3. Build tree: the binary sits in build/, assets live at the repo root.
    //    构建树：二进制在 build/，assets 在仓库根。
    if (tryRoot(exe / ".." / ".." / "assets", found)) return found;
    if (tryRoot(exe / "assets", found)) return found;

    // 4. Current working directory / 当前工作目录
    if (tryRoot(fs::current_path() / "assets", found)) return found;

    return "";
}

std::string findTemplatePath() {
    std::string found;

    if (const char* env = std::getenv("FLIPBOOK_ASSETS")) {
        if (tryTemplate(env, found)) return found;
    }

    const fs::path exe = executableDir();

    if (tryTemplate(exe / ".." / "Resources" / "assets", found)) return found;
    if (tryTemplate(exe / ".." / ".." / "assets", found)) return found;
    if (tryTemplate(exe / "assets", found)) return found;
    if (tryTemplate(fs::current_path() / "assets", found)) return found;

    return "";
}

std::string userLibraryRoot() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return "";
    return (fs::path(home) / "Library" / "Application Support" /
            "FlipBookGenerator" / "parts_library").string();
}

}  // namespace ui
}  // namespace fb
