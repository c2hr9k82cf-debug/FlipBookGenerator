// FlipBookGenerator
// Entry point: CLI dispatch (headless engine) + temporary mode menu
// 入口：CLI 分发（无头引擎）+ 临时模式菜单
//
// With arguments  -> headless engine commands (Phase 1). Testable, scriptable.
//                    带参数 -> 无头引擎命令。可测试、可脚本化。
// Without args    -> the TEMPORARY interactive menu.
//                    不带参数 -> 【临时】交互菜单。
//
// The interactive menu is being replaced by a guided wizard GUI (Dear ImGui)
// with a double-clickable .app. Do not add features to the menu — add them to
// src/ui/ instead. See CLAUDE.md.
// 交互菜单正被 Dear ImGui 引导式向导 + 可双击 .app 取代。不要往菜单加功能。

#include "version.h"
#include "core/core.h"
#include "spritesheet.h"
#include "extractor.h"

#include <opencv2/imgcodecs.hpp>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

void printCliUsage() {
    std::cout <<
        "FlipBookGenerator V" << FLIPBOOK_VERSION << " — headless engine / 无头引擎\n"
        "\n"
        "Usage / 用法:\n"
        "  flipbook --recon-test [--bundle DIR]\n"
        "      Reassemble the parts in DIR at their bbox offsets and compare with\n"
        "      the source image. Exit 0 if pixel-exact.\n"
        "      将 DIR 中的部件按 bbox 偏移贴回并与源图比对。逐像素相同则退出码 0。\n"
        "      Default DIR: tests/fixtures/character_32\n"
        "\n"
        "  flipbook --preset NAME [--frames N] [--out DIR] [--bundle DIR]\n"
        "      Render a preset animation and export frames + spritesheet.\n"
        "      渲染预设动画，导出帧序列 + 精灵图。\n"
        "      NAME: idle | walk_side | walk_front | walk_up\n"
        "\n"
        "  flipbook --presets [--out DIR] [--bundle DIR]\n"
        "      Render every preset. The visual review gate for Phase 1.\n"
        "      渲染全部预设。Phase 1 的人工验收关口。\n"
        "\n"
        "  flipbook --help\n"
        "\n"
        "Without arguments, the interactive menu opens.\n"
        "不带参数则打开交互菜单。\n";
}

struct CliArgs {
    std::string bundle = "tests/fixtures/character_32";
    std::string out    = "output/preview";
    std::string preset;
    int  frames = -1;        // -1 = per-preset default / -1 = 用各预设默认值
    bool reconTest = false;
    bool allPresets = false;
};

bool parseArgs(int argc, char** argv, CliArgs& a, std::string& err) {
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto needValue = [&](const char* what) -> const char* {
            if (i + 1 >= argc) { err = std::string("missing value for ") + what; return nullptr; }
            return argv[++i];
        };
        if (s == "--recon-test") {
            a.reconTest = true;
        } else if (s == "--presets") {
            a.allPresets = true;
        } else if (s == "--preset") {
            const char* v = needValue("--preset"); if (!v) return false;
            a.preset = v;
        } else if (s == "--frames") {
            const char* v = needValue("--frames"); if (!v) return false;
            a.frames = std::atoi(v);
        } else if (s == "--out") {
            const char* v = needValue("--out"); if (!v) return false;
            a.out = v;
        } else if (s == "--bundle") {
            const char* v = needValue("--bundle"); if (!v) return false;
            a.bundle = v;
        } else if (s == "--help" || s == "-h") {
            err = "help";
            return false;
        } else {
            err = "unknown argument: " + s;
            return false;
        }
    }
    return true;
}

fb::Animation buildPreset(const std::string& name, const fb::Rig& rig, int frames) {
    const int n = frames > 0 ? frames : -1;
    if (name == "idle")       return n > 0 ? fb::makeIdle(rig, n)      : fb::makeIdle(rig);
    if (name == "walk_side")  return n > 0 ? fb::makeWalkSide(rig, n)  : fb::makeWalkSide(rig);
    if (name == "walk_front") return n > 0 ? fb::makeWalkFront(rig, n) : fb::makeWalkFront(rig);
    if (name == "walk_up")    return n > 0 ? fb::makeWalkUp(rig, n)    : fb::makeWalkUp(rig);
    return fb::Animation{};
}

int runReconTest(const CliArgs& a) {
    fb::Rig rig;
    std::string err;
    if (!fb::loadRigFromDir(a.bundle, rig, err)) {
        std::cerr << "FAIL: " << err << std::endl;
        return 1;
    }
    std::cout << "Bundle:  " << a.bundle << "\n"
              << "Source:  " << rig.sourcePath
              << " (" << rig.size.width << "x" << rig.size.height << ")\n"
              << "Parts:   " << rig.presentCount() << "/" << fb::PART_COUNT << "\n";

    cv::Mat recon;
    const fb::ReconResult r = fb::reconstructAndCompare(rig, &recon);

    std::cout << "Pixels:  " << r.totalPixels << "\n"
              << "Diff:    " << r.diffPixels
              << "  (of which RGB-only where source alpha=0: " << r.alphaOnlyDiff << ")\n";

    if (!r.passed()) {
        std::cout << "\nRESULT: FAIL — extraction lost pixels.\n"
                  << "结果：失败 —— 提取环节丢像素了。必须先修好再做动画。\n";
        return 1;
    }
    std::cout << "\nRESULT: PASS — parts reassemble to the source exactly.\n"
              << "结果：通过 —— 部件能精确还原为源图。\n";
    return 0;
}

int runPresets(const CliArgs& a, const std::vector<std::string>& names) {
    fb::Rig rig;
    std::string err;
    if (!fb::loadRigFromDir(a.bundle, rig, err)) {
        std::cerr << "FAIL: " << err << std::endl;
        return 1;
    }
    std::cout << "Bundle: " << a.bundle << "  Source: " << rig.sourcePath
              << " (" << rig.size.width << "x" << rig.size.height << ")\n\n";

    int failures = 0;
    for (const auto& name : names) {
        fb::Animation anim = buildPreset(name, rig, a.frames);
        if (anim.keyframes.empty()) {
            std::cerr << "  FAIL: unknown preset '" << name << "'\n";
            ++failures;
            continue;
        }
        const std::vector<cv::Mat> frames = fb::renderAnimation(rig, anim);
        std::string sheetPath;
        if (!fb::exportAnimation(frames, a.out, anim.name, err, &sheetPath)) {
            std::cerr << "  FAIL: " << anim.name << ": " << err << "\n";
            ++failures;
            continue;
        }
        std::cout << "  " << anim.name
                  << ": " << frames.size() << " frames @ " << anim.fps << "fps"
                  << "  -> " << sheetPath << "\n";
    }
    std::cout << "\nOutput: " << a.out << "/\n";
    return failures == 0 ? 0 : 1;
}

int runCli(int argc, char** argv) {
    CliArgs a;
    std::string err;
    if (!parseArgs(argc, argv, a, err)) {
        if (err == "help") { printCliUsage(); return 0; }
        std::cerr << "Error: " << err << "\n\n";
        printCliUsage();
        return 2;
    }

    if (a.reconTest)   return runReconTest(a);
    if (a.allPresets)  return runPresets(a, {"idle", "walk_side", "walk_front", "walk_up"});
    if (!a.preset.empty()) return runPresets(a, {a.preset});

    printCliUsage();
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    // Any argument means headless engine mode.
    // 只要有参数就是无头引擎模式。
    if (argc > 1) {
        return runCli(argc, argv);
    }

    // ---- TEMPORARY interactive menu / 临时交互菜单 ----
    std::cout << "=== FlipBookGenerator V" << FLIPBOOK_VERSION << " ===" << std::endl;
    std::cout << std::endl;
    std::cout << "Choose mode / 选择模式:" << std::endl;
    std::cout << "  1. Sprite Sheet (generate sprite sheet) / 生成 Sprite Sheet" << std::endl;
    std::cout << "  2. Extract Parts (extract body parts) / 提取身体部件" << std::endl;
    std::cout << std::endl;
    std::cout << "Tip: run with --help for the headless engine commands." << std::endl;
    std::cout << "     (engine commands are the future; this menu is temporary.)" << std::endl;
    std::cout << std::endl;
    std::cout << "Enter choice (1 or 2): ";

    std::string choice;
    std::getline(std::cin, choice);

    if (choice == "1") {
        return runSpriteSheetMode();
    } else if (choice == "2") {
        return runExtractPartsMode();
    } else {
        std::cerr << "Error: Invalid choice. Please enter 1 or 2." << std::endl;
        return 1;
    }
}
