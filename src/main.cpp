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
#include "core/align.h"
#include "core/backinfer.h"
#include "core/compose.h"
#include "core/core.h"
#include "core/part_editor.h"
#include "core/parts.h"
#include "core/parts_library.h"
#include "core/template_spec.h"
#include "spritesheet.h"
#include "extractor.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
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
        "  flipbook --idle-purity [--bundle DIR]\n"
        "      The idle-invariant gate. The idle is a STRETCH (legs planted,\n"
        "      torso grows, head/arms rise). Checks: one frame is byte-identical\n"
        "      to the source (seam repair is a no-op on the rest pose), the leg\n"
        "      region never moves, and no frame splits into more blobs than the\n"
        "      source (no detach).\n"
        "      待机不变量闸门。待机是【伸展】（腿钉住、躯干加长、头/臂抬起）。检查：\n"
        "      有一帧与源图逐字节相同（补缝在放松姿势下恒等）、腿部区域从不动、\n"
        "      没有帧比源图裂成更多连通块（不脱节）。\n"
        "\n"
        "  flipbook --chip-recon [--chips DIR] [--sheet PNG]\n"
        "      Tier B fidelity gate: every hand-drawn chip must equal its\n"
        "      reference-sheet cell pixel-for-pixel. Verbatim copy, no error.\n"
        "      Tier B 保真闸门：每个手绘芯片必须与参考格逐像素相同。照搬，零误差。\n"
        "\n"
        "  flipbook --chip-reskin --chips DIR --map palette_map.txt\n"
        "      Tier B reskin gate: a reskin changes ONLY colour — the shape\n"
        "      (alpha mask) is identical and every colour is a map destination.\n"
        "      Tier B 换皮闸门：换皮只改颜色 —— 形状（alpha 掩码）不变，且每个色\n"
        "      都是映射的目标色。\n"
        "\n"
        "  flipbook --chip-render [--chips DIR] [--map palette_map.txt] [--out DIR]\n"
        "      Render every flipbook from the chips. Empty map = original.\n"
        "      从芯片渲染全部翻页书。空映射 = 原版。\n"
        "\n"
        "  flipbook --palette-gate [--chips DIR] [--map palette_map.txt]\n"
        "      [--front NEG.png --side NEG.png --back NEG.png]\n"
        "      Palette-extraction gate. Positive: synthesise a user view per view\n"
        "      (map applied to idle frame 0), extract it back, require the exact\n"
        "      round trip. Negative: a one-pixel third colour must be REJECTED,\n"
        "      and so must the --front/--side/--back images when given (the alt\n"
        "      character is the fixture for that).\n"
        "      配色提取闸门。正向：每视图合成用户图（对 idle f0 应用映射），提取回来\n"
        "      必须精确往返。负向：单像素第三色必须被拒绝；给了 --front/--side/--back\n"
        "      时它们也必须被拒绝（换皮角色图就是它的夹具）。\n"
        "\n"
        "  flipbook --fourdir-gate [--chips DIR] [--map palette_map.txt]\n"
        "      Four-direction sheet gate: 8x6 layout, idle cycle-fill (f0 f1 f0\n"
        "      f1 f0 f1), left rows = mirrored right rows, alpha masks preserved,\n"
        "      and every opaque colour is a palette-map destination.\n"
        "      四方向大图闸门：8x6 布局、待机循环补帧（f0 f1 f0 f1 f0 f1）、left 行\n"
        "      = right 行镜像、alpha 掩码不变、非透明色全部是映射目标色。\n"
        "\n"
        "  flipbook --align-gate CASE [--chips DIR]\n"
        "      Alignment diagnostics gate (v0.9.0). CASE is one of:\n"
        "        apply — identity / known shift / known scale are byte-exact;\n"
        "        auto  — autoAlign recovers a known (scale,dx,dy) perturbation\n"
        "                exactly (aligned input returns identity, mismatch 0);\n"
        "        diff  — diff masks mark exactly the differing pixels, and the\n"
        "                overlay colours them (magenta = silhouette, orange =\n"
        "                colour conflict).\n"
        "      对齐诊断闸门（v0.9.0）。CASE 为：apply 恒等/已知平移/已知缩放逐字节；\n"
        "      auto 自动对齐精确恢复已知扰动（已对齐输入返回恒等、mismatch 0）；\n"
        "      diff 差异掩码恰标出差异像素，叠加图着色（品红=剪影、橙=配色冲突）。\n"
        "\n"
        "  flipbook --template-load-gate [--templates DIR] [--assets DIR]\n"
        "      Template JSON load gate (v0.9.1): re-render each template's three\n"
        "      views from pixels_file + palette, require byte-exact equality with\n"
        "      the reference PNGs, and require back_policy to cover every role.\n"
        "      模板 JSON 加载闸门（v0.9.1）：从 pixels_file + palette 重渲染三视图，\n"
        "      必须与参考 PNG 逐字节相同；back_policy 必须覆盖全部角色。\n"
        "\n"
        "  flipbook --parts-semantics-gate [--templates DIR] [--assets DIR]\n"
        "      [--fixtures DIR]\n"
        "      Parts semantics gate (v0.9.1): applyPaletteRoles is exact src->dst\n"
        "      (a near-miss colour must NOT be recoloured), and splitFrameToParts\n"
        "      follows each view's part_map — compared byte-exact against the\n"
        "      committed part bundles (sprite + bbox).\n"
        "      部件语义闸门（v0.9.1）：applyPaletteRoles 是精确 src->dst（近似色\n"
        "      不得被换色），splitFrameToParts 按逐视图 part_map 划分 —— 与已提交\n"
        "      部件包（精灵 + bbox）逐字节比对。\n"
        "\n"
        "  flipbook --parts-library-gate\n"
        "      Parts library gate (v0.9.1): load a synthesised library, then\n"
        "      verify every known bad shape is refused (missing files, bad json,\n"
        "      slot mismatch, wrong size, ambiguous back, bad roleColors...).\n"
        "      部件库闸门（v0.9.1）：加载合成库，再验证已知坏形态全部被拒绝\n"
        "      （缺文件、坏 json、槽位不符、尺寸错、背面歧义、roleColors 坏……）。\n"
        "\n"
        "  flipbook --backinfer-gate [--templates DIR] [--assets DIR]\n"
        "      Back-view inference gate (v0.9.2): synthetic self-checks (hair\n"
        "      mirrors exactly, face covered by the rear hair mass, edge outline\n"
        "      kept, interior lines eaten, flatten roles swapped), then reference\n"
        "      comparison (crown rows byte-exact, no face-colour residue, alpha\n"
        "      invariant; hand-drawn deviations are reported, not failed).\n"
        "      背面推断闸门（v0.9.2）：合成自检（头发精确镜像、脸被后脑头发盖住、\n"
        "      边缘描边保留、内部线条吃掉、flatten 角色换色），再对照参考（冠区\n"
        "      逐字节、无脸色残留、alpha 不变量；与手绘的偏差如实报告不失败）。\n"
        "\n"
        "  flipbook --fill-gate [--templates DIR] [--assets DIR]\n"
        "      Part-editor fill gate (v0.10.0): blank template -> in-tool fill ->\n"
        "      legal library item. Blank interior recolours exactly (outline\n"
        "      untouched), brush/eraser/one-step-undo round-trip byte-exact,\n"
        "      inferred back keeps the alpha invariant and the role constraint,\n"
        "      save -> load returns byte-identical views.\n"
        "      部件编辑器填色闸门（v0.10.0）：空白模板 -> 工具内填色 -> 合法库物品。\n"
        "      空白内部精确换色（描边不动）、画笔/橡皮/单步撤销逐字节往返、推断背面\n"
        "      保持 alpha 不变量与角色约束、存库 -> 读回逐字节一致。\n"
        "\n"
        "  flipbook --compose --library DIR --recipe r.json --out DIR\n"
        "      [--templates DIR]\n"
        "      The customization composer (v0.10.1): assemble three\n"
        "      template-aligned views from library items named by the recipe\n"
        "      (back:\"file\" verbatim, back:\"auto\" inferred per item), then\n"
        "      write front/side/back.png into --out.\n"
        "      捏人合成器（v0.10.1）：按配方从库物品组装三张模板对齐视图\n"
        "      （back:\"file\" 直接用、back:\"auto\" 逐物品推断），写入 --out 的\n"
        "      front/side/back.png。\n"
        "\n"
        "  flipbook --compose-gate [--library DIR] [--templates DIR]\n"
        "      [--assets DIR] [--chips DIR]\n"
        "      Composer gate (v0.10.1): the six reference items must recompose\n"
        "      the reference views BYTE-EXACT (reconstruction invariant), role\n"
        "      overrides recolour exactly, back:\"auto\" equals per-item\n"
        "      inference, recipe.json round-trips, bad recipes are refused,\n"
        "      and the composed views run the fourdir pipeline to the SAME\n"
        "      sheet as the reference views (full chain).\n"
        "      合成闸门（v0.10.1）：六件参考物品必须逐字节重合成参考视图（重建\n"
        "      不变量）、角色覆盖精确换色、back:\"auto\" 等于逐物品推断、recipe.json\n"
        "      往返一致、坏配方被拒、合成视图走四方向管线出图与参考视图一致（全链）。\n"
        "\n"
        "  flipbook --fourdir --front F.png --side S.png --back B.png\n"
        "      [--chips DIR] --out DIR\n"
        "      The app pipeline: extract palettes from three template-aligned\n"
        "      views, reskin the chips, write the 8x6 four-direction sheet PNG.\n"
        "      app 管线：从三张模板对齐视图提取配色、给芯片换皮、写出 8x6 四方向\n"
        "      大图 PNG。\n"
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
    bool idlePurity = false;

    // Tier B chip gates / Tier B 芯片闸门
    std::string chips = "assets/templates/motion_chips";
    std::string map;         // palette_map.txt; empty = identity / 空 = 恒等
    std::string sheet;       // reference sheet override / 参考图覆盖
    bool chipRecon  = false;
    bool chipReskin = false;
    bool chipRender = false;

    // Four-direction sheet (the .app pipeline) / 四方向大图（app 管线）
    std::string frontImg, sideImg, backImg;   // user views / 用户三视图
    bool paletteGate = false;
    bool fourdirGate = false;
    bool fourdir     = false;

    // Alignment diagnostics gate (v0.9.0) / 对齐诊断闸门（v0.9.0）
    std::string alignCase;   // apply | auto | diff

    // Template / parts / library gates (v0.9.1) / 模板 / 部件 / 库闸门（v0.9.1）
    std::string templates = "assets/templates";
    std::string assets    = "assets";
    std::string fixtures  = "tests/fixtures";
    bool templateLoadGate   = false;
    bool partsSemanticsGate = false;
    bool partsLibraryGate   = false;
    bool backinferGate      = false;
    bool fillGate           = false;

    // Composer (v0.10.1) / 合成器（v0.10.1）
    std::string library = "tests/fixtures/parts_library";
    std::string recipe;
    bool compose     = false;
    bool composeGate = false;
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
        } else if (s == "--idle-purity") {
            a.idlePurity = true;
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
        } else if (s == "--chip-recon") {
            a.chipRecon = true;
        } else if (s == "--chip-reskin") {
            a.chipReskin = true;
        } else if (s == "--chip-render") {
            a.chipRender = true;
        } else if (s == "--chips") {
            const char* v = needValue("--chips"); if (!v) return false;
            a.chips = v;
        } else if (s == "--map") {
            const char* v = needValue("--map"); if (!v) return false;
            a.map = v;
        } else if (s == "--sheet") {
            const char* v = needValue("--sheet"); if (!v) return false;
            a.sheet = v;
        } else if (s == "--palette-gate") {
            a.paletteGate = true;
        } else if (s == "--fourdir-gate") {
            a.fourdirGate = true;
        } else if (s == "--align-gate") {
            const char* v = needValue("--align-gate"); if (!v) return false;
            a.alignCase = v;
        } else if (s == "--template-load-gate") {
            a.templateLoadGate = true;
        } else if (s == "--parts-semantics-gate") {
            a.partsSemanticsGate = true;
        } else if (s == "--parts-library-gate") {
            a.partsLibraryGate = true;
        } else if (s == "--backinfer-gate") {
            a.backinferGate = true;
        } else if (s == "--fill-gate") {
            a.fillGate = true;
        } else if (s == "--compose") {
            a.compose = true;
        } else if (s == "--compose-gate") {
            a.composeGate = true;
        } else if (s == "--library") {
            const char* v = needValue("--library"); if (!v) return false;
            a.library = v;
        } else if (s == "--recipe") {
            const char* v = needValue("--recipe"); if (!v) return false;
            a.recipe = v;
        } else if (s == "--templates") {
            const char* v = needValue("--templates"); if (!v) return false;
            a.templates = v;
        } else if (s == "--assets") {
            const char* v = needValue("--assets"); if (!v) return false;
            a.assets = v;
        } else if (s == "--fixtures") {
            const char* v = needValue("--fixtures"); if (!v) return false;
            a.fixtures = v;
        } else if (s == "--fourdir") {
            a.fourdir = true;
        } else if (s == "--front") {
            const char* v = needValue("--front"); if (!v) return false;
            a.frontImg = v;
        } else if (s == "--side") {
            const char* v = needValue("--side"); if (!v) return false;
            a.sideImg = v;
        } else if (s == "--back") {
            const char* v = needValue("--back"); if (!v) return false;
            a.backImg = v;
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

// Is `frame` the source moved by exactly (dx, dy), pixel for pixel? A sample
// that falls outside the source must be fully transparent — that is the border
// the move revealed. Two transparent pixels count as equal whatever their RGB
// is: a transparent pixel's RGB carries no information (same rule as
// reconstructAndCompare).
// `frame` 是否正是源图平移 (dx, dy) 的结果，逐像素？采样落到源图之外必须全透明
// —— 那是平移露出来的边缘。两个透明像素不论 RGB 都算相同：透明像素的 RGB 不携带
// 信息（与 reconstructAndCompare 同一规则）。
bool isTranslationOf(const cv::Mat& frame, const cv::Mat& src, int dx, int dy) {
    for (int y = 0; y < frame.rows; ++y) {
        const int sy = y - dy;
        const cv::Vec4b* f = frame.ptr<cv::Vec4b>(y);
        for (int x = 0; x < frame.cols; ++x) {
            const int sx = x - dx;
            cv::Vec4b s(0, 0, 0, 0);
            if (sx >= 0 && sx < src.cols && sy >= 0 && sy < src.rows)
                s = src.at<cv::Vec4b>(sy, sx);
            if (f[x][3] == 0 && s[3] == 0) continue;
            if (f[x] != s) return false;
        }
    }
    return true;
}

// Are two images byte-identical within rows [y0, y1)? Transparent pixels match
// each other regardless of RGB — the same rule as isTranslationOf and the recon
// comparison (a fully transparent pixel's colour carries no information).
// 两张图在行 [y0, y1) 内是否逐字节相同？透明像素互认不论 RGB —— 规则同
// isTranslationOf 与重建比对（全透明像素的颜色不携带信息）。
bool regionIdentical(const cv::Mat& a, const cv::Mat& b, int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
        const cv::Vec4b* pa = a.ptr<cv::Vec4b>(y);
        const cv::Vec4b* pb = b.ptr<cv::Vec4b>(y);
        for (int x = 0; x < a.cols; ++x) {
            if (pa[x][3] == 0 && pb[x][3] == 0) continue;
            if (pa[x] != pb[x]) return false;
        }
    }
    return true;
}

// Count 4-connected components of non-transparent pixels. The DETACH gate: a
// part that pulls away from its neighbour splits the silhouette into two blobs,
// which is exactly the artifact the detach rule forbids. Compared against the
// source's own count so a source that legitimately reads as two blobs does not
// spuriously fail — the motion must not CREATE a new split.
// 统计非透明像素的 4 连通分量。脱节闸门：部件从相邻部件拽开时，剪影会断成两个
// 连通块 —— 正是脱节铁律禁止的瑕疵。与源图自身的数量比对，这样源图本来就该是
// 两块时不会误报 —— 要求的是【动作不得制造新的断裂】。
int blobCount(const cv::Mat& img) {
    const int H = img.rows, W = img.cols;
    cv::Mat seen(H, W, CV_8U, cv::Scalar(0));
    static const int dx[4] = {1, -1, 0, 0};
    static const int dy[4] = {0, 0, 1, -1};
    int blobs = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (img.at<cv::Vec4b>(y, x)[3] == 0) continue;
            if (seen.at<uchar>(y, x)) continue;
            ++blobs;
            std::vector<cv::Point> stack;
            stack.push_back(cv::Point(x, y));
            seen.at<uchar>(y, x) = 1;
            while (!stack.empty()) {
                const cv::Point p = stack.back();
                stack.pop_back();
                for (int k = 0; k < 4; ++k) {
                    const int nx = p.x + dx[k], ny = p.y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    if (seen.at<uchar>(ny, nx)) continue;
                    if (img.at<cv::Vec4b>(ny, nx)[3] == 0) continue;
                    seen.at<uchar>(ny, nx) = 1;
                    stack.push_back(cv::Point(nx, ny));
                }
            }
        }
    }
    return blobs;
}

// The idle-invariant gate / 待机不变量闸门.
//
// The idle is a STRETCH (ANIMATION_SPEC §9), not a whole-character bob: the
// rest frame is the source untouched, and the stretch frame grows the torso
// one row and raises the head + arms while the LEGS stay planted. This gate
// checks exactly those invariants without trusting the preset:
//   (1) at least one frame is byte-identical to the source — the seam repair
//       is a no-op on the rest pose, which is what catches an over-fill (a
//       notch the artist drew, redrawn) or an under-fill (a tear left open);
//   (2) the leg region never moves — the user's "the legs do not move";
//   (3) no frame splits into more blobs than the source — motion must not
//       pull a part away from its neighbour (the detach rule).
// 待机是【伸展】（ANIMATION_SPEC §9），不是整体起伏：放松帧 = 源图原样，伸展帧把
// 躯干加长一行、头与臂抬高，而【腿】钉住不动。本闸门正好查这三条不变量，且不信任
// 预设：
//   (1) 至少一帧与源图逐字节相同 —— 补缝在放松姿势下是恒等操作，这正是抓多补
//       （把画师画的缺口重画）与漏补（撕裂没修）的地方；
//   (2) 腿部区域从不移动 —— 用户要的"腿不动"；
//   (3) 没有任何帧比源图裂成更多连通块 —— 动作不得把部件从相邻部件拽开（脱节铁律）。
//
// Each check is read from the rendered frames, not out of the keyframes, so a
// preset that started moving the legs or detaching a part fails here too.
// 每项检查都从【渲染出的帧】读取，不从关键帧读，所以预设哪天开始挪腿或拽开部件，
// 这里也会红。
int runIdlePurity(const CliArgs& a) {
    fb::Rig rig;
    std::string err;
    if (!fb::loadRigFromDir(a.bundle, rig, err)) {
        std::cerr << "FAIL: " << err << std::endl;
        return 1;
    }
    const fb::Animation anim = fb::makeIdle(rig);
    const std::vector<cv::Mat> frames = fb::renderAnimation(rig, anim);

    std::cout << "Bundle: " << a.bundle << "  Source: " << rig.sourcePath
              << " (" << rig.size.width << "x" << rig.size.height << ")\n"
              << "Idle:   " << frames.size() << " frames @ " << anim.fps << "fps\n\n";

    // The planted region = the leg bboxes' union (legs + boots). It must stay
    // byte-identical across the breath — the legs do not move.
    // 钉住区域 = 两条腿 bbox 的并集（腿+靴）。呼吸全程必须逐字节不变 —— 腿不动。
    const cv::Rect legRegion =
        rig.bbox[fb::PART_LEG_LEFT - 1] | rig.bbox[fb::PART_LEG_RIGHT - 1];
    const int legY0 = legRegion.y;
    const int legY1 = legRegion.y + legRegion.height;
    const int srcBlobs = blobCount(rig.source);

    int failures = 0;
    bool sawIdentity = false;
    for (size_t i = 0; i < frames.size(); ++i) {
        bool ok = true;
        if (isTranslationOf(frames[i], rig.source, 0, 0)) sawIdentity = true;

        if (!regionIdentical(frames[i], rig.source, legY0, legY1)) {
            std::cout << "  frame " << i << ": leg region MOVED   FAIL\n";
            ok = false;
        }
        const int blobs = blobCount(frames[i]);
        if (blobs != srcBlobs) {
            std::cout << "  frame " << i << ": " << blobs << " blobs vs source "
                      << srcBlobs << " (detach)   FAIL\n";
            ok = false;
        }
        if (ok) std::cout << "  frame " << i << ": OK\n";
        else ++failures;
    }
    if (!sawIdentity) {
        std::cout << "  no rest frame is byte-identical to the source   FAIL\n";
        ++failures;
    }

    if (failures) {
        std::cout << "\nRESULT: FAIL — " << failures << " idle invariant(s) broken.\n"
                  << "        Keep the legs planted, the body in one piece, and a\n"
                  << "        clean rest pose (the seam repair must not touch it).\n"
                  << "结果：失败 —— " << failures << " 条待机不变量被破坏。\n"
                  << "        保持腿钉住、身子不散、放松姿势干净（补缝不得改动它）。\n";
        return 1;
    }
    std::cout << "\nRESULT: PASS — legs planted, no detach, clean rest pose.\n"
              << "结果：通过 —— 腿钉住、不脱节、放松姿势干净。\n";
    return 0;
}

// Compare two same-size images pixel-for-pixel. Two fully transparent pixels
// match regardless of RGB (a transparent pixel's colour carries no information).
// 同尺寸两图逐像素比对。两个全透明像素不论 RGB 都算相同（透明像素颜色无信息）。
bool imagesIdentical(const cv::Mat& a, const cv::Mat& b) {
    if (a.size() != b.size()) return false;
    for (int y = 0; y < a.rows; ++y) {
        const cv::Vec4b* pa = a.ptr<cv::Vec4b>(y);
        const cv::Vec4b* pb = b.ptr<cv::Vec4b>(y);
        for (int x = 0; x < a.cols; ++x) {
            if (pa[x][3] == 0 && pb[x][3] == 0) continue;
            if (pa[x] != pb[x]) return false;
        }
    }
    return true;
}

// Pack a BGRA colour into a sortable key for set membership tests.
// 把 BGRA 颜色打包成可排序的键，用于集合成员测试。
uint32_t colourKey(const cv::Vec4b& c) {
    return (uint32_t)c[0] | ((uint32_t)c[1] << 8) |
           ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 24);
}

// Tier B fidelity gate: every loaded chip must equal its reference-sheet cell
// pixel-for-pixel. The chips are crops of the sheet by construction, so any
// diff is a load/decode/cell-mapping fault — this gate catches it.
// Tier B 保真闸门：每个加载的芯片必须与参考格逐像素相同。芯片本就是参考格的裁剪，
// 故任何差异都是读取/解码/格映射故障 —— 本闸门负责揪出。
int runChipRecon(const CliArgs& a) {
    fb::ChipSet cs;
    std::string err;
    if (!fb::loadChipSet(a.chips, cs, err)) { std::cerr << "FAIL: " << err << "\n"; return 1; }
    const std::string sheetPath = a.sheet.empty() ? cs.sourcePath : a.sheet;
    cv::Mat sheet = cv::imread(sheetPath, cv::IMREAD_UNCHANGED);
    if (sheet.empty()) { std::cerr << "FAIL: cannot load reference sheet " << sheetPath << "\n"; return 1; }
    if (sheet.channels() == 3) cv::cvtColor(sheet, sheet, cv::COLOR_BGR2BGRA);

    std::cout << "Chips: " << a.chips << "\nSheet: " << sheetPath
              << " (" << sheet.cols << "x" << sheet.rows << ")\n\n";

    int failures = 0, checked = 0;
    for (const fb::MotionChips& m : cs.flipbooks) {
        for (int i = 0; i < m.frameCount; ++i) {
            const cv::Rect cell(i * cs.cell.width, m.sheetRow * cs.cell.height,
                                cs.cell.width, cs.cell.height);
            if (m.sheetRow < 0 || cell.x + cell.width > sheet.cols ||
                cell.y + cell.height > sheet.rows) {
                std::cout << "  " << m.name << " f" << i << ": cell outside sheet   FAIL\n";
                ++failures; continue;
            }
            ++checked;
            if (!imagesIdentical(m.frames[i], sheet(cell))) {
                std::cout << "  " << m.name << " f" << i << ": chip != reference cell   FAIL\n";
                ++failures;
            }
        }
    }
    if (failures) {
        std::cout << "\nRESULT: FAIL — " << failures << " chip(s) differ from the reference.\n"
                  << "结果：失败 —— " << failures << " 个芯片与参考格不符。\n";
        return 1;
    }
    std::cout << "\nRESULT: PASS — all " << checked << " chips match the reference sheet.\n"
              << "结果：通过 —— 全部 " << checked << " 个芯片与参考图逐格一致。\n";
    return 0;
}

// Tier B reskin gate: a reskin must change ONLY colour — the transparency mask
// and the opaque-pixel count stay identical (shape invariant), and every
// resulting colour must be a palette-map destination (no leaked original tone).
// Tier B 换皮闸门：换皮只能改颜色 —— 透明掩码与非透明像素数保持不变（形状不变），
// 且每个结果色都必须是调色板映射的目标色（不得漏出原色）。
int runChipReskin(const CliArgs& a) {
    fb::ChipSet cs;
    std::string err;
    if (!fb::loadChipSet(a.chips, cs, err)) { std::cerr << "FAIL: " << err << "\n"; return 1; }
    fb::PaletteMap map;
    if (!fb::loadPaletteMap(a.map, map, err)) { std::cerr << "FAIL: " << err << "\n"; return 1; }

    std::set<uint32_t> allowed;
    for (const auto& sw : map) allowed.insert(colourKey(sw.second));

    std::cout << "Chips: " << a.chips << "\nMap:   " << a.map
              << " (" << map.size() << " entries)\n\n";

    int failures = 0, checked = 0;
    for (const fb::MotionChips& m : cs.flipbooks) {
        const std::vector<cv::Mat> orig = fb::renderFromChips(m, {});
        const std::vector<cv::Mat> alt  = fb::renderFromChips(m, map);
        for (int i = 0; i < m.frameCount; ++i) {
            ++checked;
            const cv::Mat& o = orig[i];
            const cv::Mat& r = alt[i];
            bool shapeOK = true, colourOK = true;
            for (int y = 0; y < o.rows; ++y) {
                const cv::Vec4b* po = o.ptr<cv::Vec4b>(y);
                const cv::Vec4b* pr = r.ptr<cv::Vec4b>(y);
                for (int x = 0; x < o.cols; ++x) {
                    const bool ao = po[x][3] != 0, ar = pr[x][3] != 0;
                    if (ao != ar) shapeOK = false;
                    if (ar && !allowed.count(colourKey(pr[x]))) colourOK = false;
                }
            }
            if (!shapeOK) {
                std::cout << "  " << m.name << " f" << i << ": alpha mask changed   FAIL\n";
                ++failures;
            } else if (!colourOK) {
                std::cout << "  " << m.name << " f" << i << ": leaked original colour   FAIL\n";
                ++failures;
            }
        }
    }
    if (failures) {
        std::cout << "\nRESULT: FAIL — " << failures << " frame(s) break the reskin contract.\n"
                  << "结果：失败 —— " << failures << " 帧破坏了换皮契约。\n";
        return 1;
    }
    std::cout << "\nRESULT: PASS — all " << checked << " frames change colour only.\n"
              << "结果：通过 —— 全部 " << checked << " 帧只换色不换形。\n";
    return 0;
}

// Render every flipbook with a palette map (empty map = identity / original).
// 用调色板映射渲染全部翻页书（空映射 = 恒等 / 原版）。
int runChipRender(const CliArgs& a) {
    fb::ChipSet cs;
    std::string err;
    if (!fb::loadChipSet(a.chips, cs, err)) { std::cerr << "FAIL: " << err << "\n"; return 1; }
    fb::PaletteMap map;
    if (!a.map.empty() && !fb::loadPaletteMap(a.map, map, err)) {
        std::cerr << "FAIL: " << err << "\n"; return 1;
    }
    std::cout << "Chips: " << a.chips
              << (a.map.empty() ? "  (identity palette / 恒等调色板)" : "  map: " + a.map) << "\n";

    int failures = 0;
    for (const fb::MotionChips& m : cs.flipbooks) {
        const std::vector<cv::Mat> frames = fb::renderFromChips(m, map);
        std::string sheetPath;
        if (!fb::exportAnimation(frames, a.out, m.name, err, &sheetPath)) {
            std::cerr << "  FAIL: " << m.name << ": " << err << "\n"; ++failures; continue;
        }
        std::cout << "  " << m.name << ": " << frames.size() << " frames @ "
                  << m.fps << "fps -> " << sheetPath << "\n";
    }
    std::cout << "\nOutput: " << a.out << "/\n";
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Palette extraction gate / 配色提取闸门
// ---------------------------------------------------------------------------

// Count a ref cell's distinct non-transparent colours (as sorted keys).
// 统计参考格的不同非透明颜色（排序键）。
std::set<uint32_t> opaqueColourKeys(const cv::Mat& img) {
    std::set<uint32_t> keys;
    for (int y = 0; y < img.rows; ++y) {
        const cv::Vec4b* row = img.ptr<cv::Vec4b>(y);
        for (int x = 0; x < img.cols; ++x)
            if (row[x][3] != 0) keys.insert(colourKey(row[x]));
    }
    return keys;
}

// Alpha-mask equality: transparent vs opaque at every pixel. RGB ignored.
// alpha 掩码相等：逐像素透明/不透明一致。忽略 RGB。
bool alphaMaskEqual(const cv::Mat& a, const cv::Mat& b) {
    if (a.size() != b.size()) return false;
    for (int y = 0; y < a.rows; ++y) {
        const cv::Vec4b* pa = a.ptr<cv::Vec4b>(y);
        const cv::Vec4b* pb = b.ptr<cv::Vec4b>(y);
        for (int x = 0; x < a.cols; ++x)
            if ((pa[x][3] != 0) != (pb[x][3] != 0)) return false;
    }
    return true;
}

// The palette-extraction gate. Positive: synthesise the ideal user image per
// view (the palette map applied to idle frame 0) and require extractPaletteMap
// to round-trip it exactly. Negative: the extractor must REJECT a one-pixel
// third colour, and must reject the --front/--side/--back images when given
// (assets/character_alt*.png is the committed fixture that fails strict
// extraction — real AI images must be anti-alias-free and template-aligned).
// 配色提取闸门。正向：每视图合成理想用户图（对 idle f0 应用调色板映射），
// extractPaletteMap 必须精确往返。负向：提取器必须拒绝"单像素第三色"，且给了
// --front/--side/--back 时必须拒绝它们（assets/character_alt*.png 就是过不了
// 严格提取的已提交夹具 —— 真实 AI 图必须无抗锯齿、模板对齐）。
// Alignment diagnostics gate (v0.9.0). Three cases:
//   apply — identity / known shift / known scale resampling is byte-exact;
//   auto  — autoAlign recovers a known (scale,dx,dy) perturbation exactly;
//   diff  — diff masks mark exactly the differing pixels, overlay colours them.
// 对齐诊断闸门（v0.9.0）。三种情形：
//   apply 恒等/已知平移/已知缩放重采样逐字节；
//   auto  自动对齐精确恢复已知 (scale,dx,dy) 扰动；
//   diff  差异掩码恰标出差异像素，叠加图着色。
int runAlignGate(const CliArgs& a) {
    fb::ChipSet cs;
    std::string err;
    if (!fb::loadChipSet(a.chips, cs, err)) { std::cerr << "FAIL: " << err << "\n"; return 1; }
    const fb::MotionChips* idle = cs.find("front_idle");
    if (!idle || idle->frames.empty()) {
        std::cerr << "FAIL: front_idle chips missing\n";
        return 1;
    }
    const cv::Mat& ref = idle->frames[0];
    int failures = 0;

    // ---- apply: byte-exact transforms ----
    // ---- apply：变换逐字节 ----
    if (a.alignCase == "apply" || a.alignCase == "all") {
        const fb::AlignParams ident{1, 0, 0};
        if (!imagesIdentical(fb::applyAlign(ref, ident, ref.size()), ref)) {
            std::cout << "  identity: applyAlign(ref,{1,0,0}) != ref   FAIL\n";
            ++failures;
        } else {
            std::cout << "  identity: byte-exact   OK\n";
        }
        // Known shift: ref content moved by (+3,-2). Expected = ref sampled at
        // (x-3, y+2), transparent where out of range.
        // 已知平移：内容移 (+3,-2)。期望 = ref 采样 (x-3, y+2)，越界透明。
        for (const fb::AlignParams p : {fb::AlignParams{1, 3, -2},
                                        fb::AlignParams{1, -5, 4}}) {
            cv::Mat expect = cv::Mat::zeros(ref.size(), CV_8UC4);
            for (int y = 0; y < ref.rows; ++y)
                for (int x = 0; x < ref.cols; ++x) {
                    const int sx = x - p.dx, sy = y - p.dy;
                    if (sx >= 0 && sy >= 0 && sx < ref.cols && sy < ref.rows)
                        expect.at<cv::Vec4b>(y, x) = ref.at<cv::Vec4b>(sy, sx);
                }
            const cv::Mat got = fb::applyAlign(ref, p, ref.size());
            if (!imagesIdentical(got, expect)) {
                std::cout << "  shift(" << p.dx << "," << p.dy
                          << "): not byte-exact   FAIL\n";
                ++failures;
            } else {
                std::cout << "  shift(" << p.dx << "," << p.dy
                          << "): byte-exact   OK\n";
            }
        }
        // Known scale: nearest-neighbour upscale x2 then scale back == ref.
        // 已知缩放：最近邻放大 x2 再缩回 == ref。
        {
            cv::Mat up(ref.rows * 2, ref.cols * 2, CV_8UC4);
            for (int y = 0; y < up.rows; ++y)
                for (int x = 0; x < up.cols; ++x)
                    up.at<cv::Vec4b>(y, x) = ref.at<cv::Vec4b>(y / 2, x / 2);
            const cv::Mat back = fb::applyAlign(up, fb::AlignParams{2, 0, 0}, ref.size());
            if (!imagesIdentical(back, ref)) {
                std::cout << "  scale x2 round trip: not byte-exact   FAIL\n";
                ++failures;
            } else {
                std::cout << "  scale x2 round trip: byte-exact   OK\n";
            }
        }
    }

    // ---- auto: exact recovery of a known perturbation ----
    // ---- auto：精确恢复已知扰动 ----
    if (a.alignCase == "auto" || a.alignCase == "all") {
        // Aligned input -> identity, mismatch 0. / 已对齐输入 -> 恒等，mismatch 0。
        fb::AlignParams got;
        int mis = -1;
        if (!fb::autoAlign(ref, ref, got, &mis) ||
            !(got == fb::AlignParams{1, 0, 0}) || mis != 0) {
            std::cout << "  aligned input: got scale=" << got.scale << " d=("
                      << got.dx << "," << got.dy << ") mis=" << mis
                      << " (want identity, 0)   FAIL\n";
            ++failures;
        } else {
            std::cout << "  aligned input: identity, mismatch 0   OK\n";
        }
        // Perturbations the search must undo exactly. Content touches the
        // bottom row of the template, so dy perturbations stay <= 0 (a +dy
        // would clip content and make exact recovery impossible).
        // 搜索必须精确抵消的扰动。内容贴着模板底行，所以 dy 扰动取 <= 0（+dy 会
        // 裁掉内容，不可能精确恢复）。
        const fb::AlignParams cases[] = {
            {1, 3, 0}, {1, -4, -2}, {1, 0, -3},
        };
        for (const fb::AlignParams want : cases) {
            // Build the perturbed user image the way a shifted export would
            // look: content moved by (want.dx, want.dy).
            // 按平移导出的样子构造扰动图：内容移 (want.dx, want.dy)。
            cv::Mat user = cv::Mat::zeros(ref.size(), CV_8UC4);
            for (int y = 0; y < ref.rows; ++y)
                for (int x = 0; x < ref.cols; ++x) {
                    const int sx = x - want.dx, sy = y - want.dy;
                    if (sx >= 0 && sy >= 0 && sx < ref.cols && sy < ref.rows)
                        user.at<cv::Vec4b>(y, x) = ref.at<cv::Vec4b>(sy, sx);
                }
            fb::AlignParams out;
            int mm = -1;
            fb::autoAlign(user, ref, out, &mm);
            // Recovered params must move the content BACK: (-want.dx, -want.dy).
            // 恢复参数必须把内容移回去：(-want.dx, -want.dy)。
            const fb::AlignParams expect{1, -want.dx, -want.dy};
            if (!(out == expect)) {
                std::cout << "  perturb(" << want.dx << "," << want.dy
                          << "): recovered (" << out.dx << "," << out.dy
                          << ") scale=" << out.scale << "   FAIL\n";
                ++failures;
            } else {
                std::cout << "  perturb(" << want.dx << "," << want.dy
                          << "): recovered exactly   OK\n";
            }
        }
    }

    // ---- diff: masks mark exactly the differing pixels ----
    // ---- diff：掩码恰标出差异像素 ----
    if (a.alignCase == "diff" || a.alignCase == "all") {
        // user1 = ref with one pixel's alpha cleared (silhouette diff) and one
        // pixel swapped to a third colour (functional conflict). Expected:
        // maskDiffMask has exactly the first pixel, colourConflictMask exactly
        // the second (it is the minority of its ref colour's tally).
        // user1 = ref 清掉一个像素的 alpha（剪影差）+ 一个像素换成第三色（函数性
        // 冲突）。期望：maskDiffMask 恰标第一个像素，colourConflictMask 恰标第二
        // 个（它是其参考色统计里的少数派）。
        cv::Mat user1 = ref.clone();
        cv::Point diffPt(-1, -1), conflictPt(-1, -1);
        // Pick two opaque pixels with the SAME ref colour so the third-colour
        // swap creates a 1-vs-N tally (a real functional conflict).
        // 选两个同色不透明像素，换第三色才造成 1 对 N 的统计（真函数性冲突）。
        for (int y = 0; y < ref.rows && (diffPt.x < 0 || conflictPt.x < 0); ++y) {
            for (int x = 0; x < ref.cols; ++x) {
                const cv::Vec4b c = ref.at<cv::Vec4b>(y, x);
                if (c[3] == 0) continue;
                if (diffPt.x < 0) { diffPt = cv::Point(x, y); continue; }
                if (conflictPt.x < 0 && c == ref.at<cv::Vec4b>(diffPt.y, diffPt.x)) {
                    conflictPt = cv::Point(x, y);
                    break;
                }
            }
        }
        if (conflictPt.x < 0) {
            std::cerr << "FAIL: fixture has no repeated opaque colour\n";
            return 1;
        }
        user1.at<cv::Vec4b>(diffPt.y, diffPt.x)[3] = 0;
        user1.at<cv::Vec4b>(conflictPt.y, conflictPt.x) = cv::Vec4b(1, 2, 3, 255);

        fb::PaletteMap dummy;
        fb::ExtractStats st;
        const bool okExtract =
            fb::extractPaletteMap(user1, ref, dummy, err, &st, /*tolerant=*/true);
        if (okExtract) {
            std::cout << "  conflict: extraction ACCEPTED a functional conflict   FAIL\n";
            ++failures;
        }
        // The masks must be exact even though extraction failed.
        // 提取虽然失败，掩码必须精确。
        auto maskExact = [](const cv::Mat& m, const cv::Point& p) {
            if (m.empty() || m.size() != cv::Size(32, 32)) return false;
            int set = 0;
            for (int y = 0; y < m.rows; ++y)
                for (int x = 0; x < m.cols; ++x)
                    if (m.at<uchar>(y, x)) ++set;
            return set == 1 && m.at<uchar>(p.y, p.x) == 255;
        };
        if (!maskExact(st.maskDiffMask, diffPt)) {
            std::cout << "  diff mask: not exactly at (" << diffPt.x << ","
                      << diffPt.y << ")   FAIL\n";
            ++failures;
        } else {
            std::cout << "  diff mask: exactly 1 pixel   OK\n";
        }
        if (!maskExact(st.colourConflictMask, conflictPt)) {
            std::cout << "  conflict mask: not exactly at (" << conflictPt.x
                      << "," << conflictPt.y << ")   FAIL\n";
            ++failures;
        } else {
            std::cout << "  conflict mask: exactly 1 pixel   OK\n";
        }
        // Overlay colours: magenta on the diff pixel, orange on the conflict.
        // 叠加图着色：差异像素品红，冲突像素橙。
        const cv::Mat ov = fb::renderDiffOverlay(user1, st.maskDiffMask,
                                                 st.colourConflictMask);
        const cv::Vec4b magenta(255, 0, 255, 255), orange(0, 165, 255, 255);
        if (ov.size() != ref.size() ||
            ov.at<cv::Vec4b>(diffPt.y, diffPt.x) != magenta ||
            ov.at<cv::Vec4b>(conflictPt.y, conflictPt.x) != orange) {
            std::cout << "  overlay: wrong colours at the diff pixels   FAIL\n";
            ++failures;
        } else {
            std::cout << "  overlay: magenta + orange at the exact pixels   OK\n";
        }
        // Tolerant mask-only diff must still succeed (warn, don't reject).
        // 仅剪影差在宽容模式必须仍成功（提醒而不拒绝）。
        cv::Mat user2 = ref.clone();
        user2.at<cv::Vec4b>(diffPt.y, diffPt.x)[3] = 0;
        fb::ExtractStats st2;
        if (!fb::extractPaletteMap(user2, ref, dummy, err, &st2, true) ||
            st2.maskDiffPixels != 1) {
            std::cout << "  tolerant mask-only diff: should pass with 1 warning   FAIL\n";
            ++failures;
        } else {
            std::cout << "  tolerant mask-only diff: passes, 1 pixel warned   OK\n";
        }
    }

    std::cout << (failures ? "ALIGN GATE FAIL\n" : "ALIGN GATE OK\n");
    return failures ? 1 : 0;
}

// Count differing pixels (any channel), for gate diagnostics.
// 统计差异像素（任一通道），供闸门诊断用。
int countPixelDiff(const cv::Mat& a, const cv::Mat& b) {
    if (a.size() != b.size() || a.type() != b.type()) return -1;
    int diff = 0;
    for (int y = 0; y < a.rows; ++y) {
        const cv::Vec4b* pa = a.ptr<cv::Vec4b>(y);
        const cv::Vec4b* pb = b.ptr<cv::Vec4b>(y);
        for (int x = 0; x < a.cols; ++x)
            if (pa[x] != pb[x]) ++diff;
    }
    return diff;
}

// Template JSON load gate (v0.9.1): loading + re-rendering a template must be
// byte-exact against the committed reference PNGs, and back_policy must cover
// every palette role (policy lives in the JSON, not in code).
// 模板 JSON 加载闸门（v0.9.1）：加载 + 重渲染必须与已提交参考 PNG 逐字节相同，
// back_policy 必须覆盖全部调色板角色（策略在 JSON 里，不在代码里）。
int runTemplateLoadGate(const CliArgs& a) {
    struct Case { const char* json; const char* refBase; };
    const Case cases[] = {
        {"chibi_standard.json", "character_32"},
        {"chibi_alt.json",      "character_alt"},
    };
    const char* viewSuffix[3] = {"", "_side", "_back"};
    const char* viewNames[3]  = {"front", "side", "back"};
    int failures = 0;

    for (const Case& c : cases) {
        fb::TemplateSpec spec;
        std::string err;
        if (!fb::loadTemplateSpec(a.templates + "/" + c.json, spec, err)) {
            std::cout << c.json << ": load FAILED: " << err << "   FAIL\n";
            ++failures;
            continue;
        }
        std::cout << c.json << ": loaded — palette " << spec.palette.size()
                  << " roles, " << spec.paletteChars.size() << " pixel chars\n";
        for (const auto& kv : spec.palette) {
            if (!spec.backPolicy.count(kv.first)) {
                std::cout << "  back_policy: role '" << kv.first
                          << "' uncovered   FAIL\n";
                ++failures;
            }
        }
        for (int v = 0; v < 3; ++v) {
            cv::Mat rendered = fb::renderTemplateView(spec, v, err);
            const std::string refPath =
                a.assets + "/" + c.refBase + viewSuffix[v] + ".png";
            const cv::Mat ref = cv::imread(refPath, cv::IMREAD_UNCHANGED);
            if (rendered.empty()) {
                std::cout << "  " << viewNames[v] << ": render FAILED: " << err
                          << "   FAIL\n";
                ++failures;
                continue;
            }
            if (ref.empty()) {
                std::cout << "  " << viewNames[v] << ": cannot read " << refPath
                          << "   FAIL\n";
                ++failures;
                continue;
            }
            const int diff = countPixelDiff(rendered, ref);
            if (diff != 0) {
                std::cout << "  " << viewNames[v] << ": differs from " << refPath
                          << " (" << diff << " px)   FAIL\n";
                ++failures;
            } else {
                std::cout << "  " << viewNames[v]
                          << ": byte-exact vs reference   OK\n";
            }
        }
    }
    std::cout << (failures ? "TEMPLATE LOAD GATE FAIL\n" : "TEMPLATE LOAD GATE OK\n");
    return failures ? 1 : 0;
}

// Parts semantics gate (v0.9.1):
//   (a) applyPaletteRoles is EXACT src→dst — the old fuzzy match recoloured
//       any pixel within distance^2 < 100, which ate near-miss hand-painted
//       colours. A near-miss pixel must survive untouched.
//   (b) splitFrameToParts follows the PER-VIEW part_map — every split must
//       match the committed part bundle (sprite + tight bbox) byte-exactly.
// 部件语义闸门（v0.9.1）：
//   (a) applyPaletteRoles 是精确 src→dst —— 旧的模糊匹配会换掉距离平方 < 100
//       的任何像素，误吃手绘近似色。近似像素必须原样幸存。
//   (b) splitFrameToParts 按逐视图 part_map —— 每次切分必须与已提交部件包
//       （精灵 + 紧致 bbox）逐字节一致。
int runPartsSemanticsGate(const CliArgs& a) {
    int failures = 0;
    std::string err;

    // ---- (a) exact remap / 精确重映射 ----
    {
        const cv::Vec3b src(200, 150, 100), dst(7, 8, 9);       // BGR
        const cv::Vec3b src2(30, 20, 10), dst2(40, 50, 60);     // BGR
        cv::Mat spr(1, 4, CV_8UC4);
        spr.at<cv::Vec4b>(0, 0) = cv::Vec4b(src[0], src[1], src[2], 255);
        // distance^2 from src = 75 < 100 — the OLD fuzzy match ate this one.
        // 与 src 的距离平方 = 75 < 100 —— 旧的模糊匹配会吃掉这个像素。
        spr.at<cv::Vec4b>(0, 1) =
            cv::Vec4b(src[0] + 5, src[1] + 5, src[2] + 5, 255);
        spr.at<cv::Vec4b>(0, 2) = cv::Vec4b(src[0], src[1], src[2], 0);
        spr.at<cv::Vec4b>(0, 3) = cv::Vec4b(src2[0], src2[1], src2[2], 255);

        const std::vector<fb::PartLayer::RoleColor> roles = {
            {"r1", src, dst},
            {"r2", src2, dst2},
        };
        fb::applyPaletteRoles(spr, roles);

        struct Check { int x; cv::Vec4b want; const char* what; };
        const Check checks[] = {
            {0, cv::Vec4b(dst[0], dst[1], dst[2], 255), "exact src -> dst"},
            {1, cv::Vec4b(src[0] + 5, src[1] + 5, src[2] + 5, 255),
             "near-miss colour untouched (old fuzzy match would eat it)"},
            {2, cv::Vec4b(src[0], src[1], src[2], 0),
             "transparent pixel untouched, alpha preserved"},
            {3, cv::Vec4b(dst2[0], dst2[1], dst2[2], 255),
             "second role exact src -> dst"},
        };
        for (const Check& ck : checks) {
            if (spr.at<cv::Vec4b>(0, ck.x) != ck.want) {
                std::cout << "  exact remap: " << ck.what << "   FAIL\n";
                ++failures;
            } else {
                std::cout << "  exact remap: " << ck.what << "   OK\n";
            }
        }
    }

    // ---- (b) per-view split vs committed bundles / 逐视图切分 vs 已提交部件包 ----
    struct Case { const char* json; const char* refBase; };
    const Case cases[] = {
        {"chibi_standard.json", "character_32"},
        {"chibi_alt.json",      "character_alt"},
    };
    const char* viewSuffix[3] = {"", "_side", "_back"};
    const char* partNames[6]  = {"head", "body", "arm_left",
                                 "arm_right", "leg_left", "leg_right"};

    for (const Case& c : cases) {
        fb::TemplateSpec spec;
        if (!fb::loadTemplateSpec(a.templates + "/" + c.json, spec, err)) {
            std::cout << c.json << ": load FAILED: " << err << "   FAIL\n";
            ++failures;
            continue;
        }
        for (int v = 0; v < 3; ++v) {
            const std::string framePath =
                a.assets + "/" + c.refBase + viewSuffix[v] + ".png";
            const cv::Mat frame = cv::imread(framePath, cv::IMREAD_UNCHANGED);
            if (frame.empty()) {
                std::cout << c.refBase << viewSuffix[v] << ": cannot read "
                          << framePath << "   FAIL\n";
                ++failures;
                continue;
            }
            const fb::SplitResult sr = fb::splitFrameToParts(frame, spec, v);
            const std::string bundleDir =
                a.fixtures + "/" + c.refBase + viewSuffix[v];
            for (int p = 0; p < 6; ++p) {
                const std::string tag =
                    std::string(c.refBase) + viewSuffix[v] + "/" + partNames[p];
                std::ifstream bf(bundleDir + "/" + partNames[p] + "_bbox.txt");
                int x = 0, y = 0, w = 0, h = 0;
                if (!(bf >> x >> y >> w >> h)) {
                    std::cout << "  " << tag << ": fixture bbox missing   FAIL\n";
                    ++failures;
                    continue;
                }
                const cv::Mat fpng = cv::imread(
                    bundleDir + "/" + partNames[p] + ".png", cv::IMREAD_UNCHANGED);
                if (fpng.empty()) {
                    std::cout << "  " << tag << ": fixture png missing   FAIL\n";
                    ++failures;
                    continue;
                }
                const cv::Rect want(x, y, w, h);
                if (!sr.present[p]) {
                    std::cout << "  " << tag << ": split produced nothing   FAIL\n";
                    ++failures;
                } else if (sr.bboxes[p] != want) {
                    std::cout << "  " << tag << ": bbox got ("
                              << sr.bboxes[p].x << "," << sr.bboxes[p].y << ","
                              << sr.bboxes[p].width << "," << sr.bboxes[p].height
                              << ") want (" << x << "," << y << "," << w << ","
                              << h << ")   FAIL\n";
                    ++failures;
                } else {
                    const int diff = countPixelDiff(sr.sprites[p], fpng);
                    if (diff != 0) {
                        std::cout << "  " << tag << ": sprite differs ("
                                  << diff << " px)   FAIL\n";
                        ++failures;
                    } else {
                        std::cout << "  " << tag
                                  << ": bbox+sprite byte-exact   OK\n";
                    }
                }
            }
        }
    }
    std::cout << (failures ? "PARTS SEMANTICS GATE FAIL\n"
                           : "PARTS SEMANTICS GATE OK\n");
    return failures ? 1 : 0;
}

// Parts library gate (v0.9.1): a synthesised library must load with all
// metadata intact, and every known bad shape must be refused with a reason.
// 部件库闸门（v0.9.1）：合成库必须完整加载，已知坏形态必须全部被拒并给出原因。
int runPartsLibraryGate(const CliArgs& a) {
    (void)a;
    namespace fs = std::filesystem;
    int failures = 0;

    const fs::path root =
        fs::temp_directory_path() / "flipbook_parts_library_gate";
    std::error_code ec;
    fs::remove_all(root, ec);

    auto writePng = [](const fs::path& p, const cv::Vec4b& col) {
        cv::Mat img(32, 32, CV_8UC4, cv::Scalar(0, 0, 0, 0));
        for (int y = 10; y < 20; ++y)
            for (int x = 10; x < 20; ++x)
                img.at<cv::Vec4b>(y, x) = col;
        cv::imwrite(p.string(), img);
    };
    auto writeText = [](const fs::path& p, const std::string& text) {
        std::ofstream f(p);
        f << text;
    };
    // BGR (64,124,231) == JSON RGB [231,124,64] / BGR (64,124,231) 即 JSON RGB [231,124,64]
    const cv::Vec4b hairCol(64, 124, 231, 255);
    const std::string hairJson = R"({
  "name": "hair_test",
  "slot": "hair",
  "zOrder": 30,
  "anchors": { "front": [15, 12], "side": [16, 12] },
  "roleColors": [
    { "role": "hair_mid", "src": [231, 124, 64], "dst": [120, 80, 200] },
    { "role": "hair_dark", "dst": [80, 50, 150] }
  ],
  "back": "auto",
  "silhouette": "template"
})";
    const std::string topJson = R"({
  "name": "shirt_test",
  "slot": "top",
  "zOrder": 20,
  "back": "file",
  "silhouette": "template"
})";

    auto makeItem = [&](const fs::path& dir, const std::string& json) {
        fs::create_directories(dir);
        writePng(dir / "front.png", hairCol);
        writePng(dir / "side.png", hairCol);
        writeText(dir / "item.json", json);
    };

    // ---- happy path / 正向 ----
    {
        makeItem(root / "hair" / "hair_test", hairJson);
        makeItem(root / "top" / "shirt_test", topJson);
        writePng(root / "top" / "shirt_test" / "back.png", hairCol);

        fb::PartsLibrary lib;
        std::string err;
        if (!fb::loadPartsLibrary(root.string(), cv::Size(32, 32), lib, err)) {
            std::cout << "  happy path: load FAILED: " << err << "   FAIL\n";
            ++failures;
        } else if (lib.items.size() != 2) {
            std::cout << "  happy path: expected 2 items, got " << lib.items.size()
                      << "   FAIL\n";
            ++failures;
        } else {
            auto check = [&](bool ok, const char* what) {
                if (ok) {
                    std::cout << "  happy path: " << what << "   OK\n";
                } else {
                    std::cout << "  happy path: " << what << "   FAIL\n";
                    ++failures;
                }
            };
            const fb::LibraryItem* hair = lib.find("hair", "hair_test");
            const fb::LibraryItem* top  = lib.find("top", "shirt_test");
            check(hair != nullptr && top != nullptr,
                  "find(hair_test) + find(shirt_test)");
            if (hair) {
                check(hair->zOrder == 30 && hair->backMode == "auto" &&
                      hair->silhouette == "template",
                      "zOrder/back/silhouette parsed");
                check(hair->hasAnchor[0] && hair->anchors[0] == cv::Point(15, 12) &&
                      hair->hasAnchor[1] && hair->anchors[1] == cv::Point(16, 12) &&
                      !hair->hasAnchor[2],
                      "anchors per view");
                check(hair->roleColors.size() == 2 &&
                      hair->roleColors[0].hasSrc &&
                      hair->roleColors[0].srcBgr == cv::Vec3b(64, 124, 231) &&
                      hair->roleColors[0].dstBgr == cv::Vec3b(200, 80, 120) &&
                      !hair->roleColors[1].hasSrc &&
                      hair->roleColors[1].dstBgr == cv::Vec3b(150, 50, 80),
                      "roleColors src/dst (RGB->BGR)");
                check(!hair->views[0].empty() && !hair->views[1].empty() &&
                      hair->views[2].empty(),
                      "front+side loaded, back empty under back:\"auto\"");
            }
            if (top) {
                check(top->backMode == "file" && !top->views[2].empty(),
                      "back:\"file\" loads back.png");
            }
        }
    }

    // ---- error paths / 错误路径 ----
    struct BadCase {
        const char* name;
        std::function<void(const fs::path&)> mutate;
    };
    const BadCase bads[] = {
        {"missing_front",
         [&](const fs::path& d) { fs::remove(d / "front.png"); }},
        {"missing_side",
         [&](const fs::path& d) { fs::remove(d / "side.png"); }},
        {"missing_item_json",
         [&](const fs::path& d) { fs::remove(d / "item.json"); }},
        {"bad_json",
         [&](const fs::path& d) { writeText(d / "item.json", "{ not json"); }},
        {"slot_mismatch",
         [&](const fs::path& d) {
             writeText(d / "item.json", R"({"name":"hair_test","slot":"top"})");
         }},
        {"wrong_size",
         [&](const fs::path& d) {
             cv::Mat img(24, 24, CV_8UC4, cv::Scalar(0, 0, 0, 0));
             cv::imwrite((d / "front.png").string(), img);
         }},
        {"file_back_missing",
         [&](const fs::path& d) {
             writeText(d / "item.json",
                       R"({"name":"hair_test","back":"file","silhouette":"template"})");
         }},
        {"auto_with_back",
         [&](const fs::path& d) { writePng(d / "back.png", hairCol); }},
        {"roles_no_dst",
         [&](const fs::path& d) {
             writeText(d / "item.json",
                       R"({"name":"hair_test","roleColors":[{"role":"hair_mid"}]})");
         }},
        {"bad_silhouette",
         [&](const fs::path& d) {
             writeText(d / "item.json",
                       R"({"name":"hair_test","silhouette":"freeform"})");
         }},
    };
    for (const BadCase& bc : bads) {
        const fs::path br = root / "bad" / bc.name;
        makeItem(br / "hair" / "hair_test", hairJson);
        bc.mutate(br / "hair" / "hair_test");

        fb::PartsLibrary lib;
        std::string err;
        if (fb::loadPartsLibrary(br.string(), cv::Size(32, 32), lib, err)) {
            std::cout << "  " << bc.name << ": was accepted, must be refused   FAIL\n";
            ++failures;
        } else if (err.empty()) {
            std::cout << "  " << bc.name << ": refused but no reason given   FAIL\n";
            ++failures;
        } else {
            std::cout << "  " << bc.name << ": refused (" << err << ")   OK\n";
        }
    }

    fs::remove_all(root, ec);
    std::cout << (failures ? "PARTS LIBRARY GATE FAIL\n" : "PARTS LIBRARY GATE OK\n");
    return failures ? 1 : 0;
}

// Back-inference gate (v0.9.2).
//  1. Synthetic self-checks: an asymmetric hairstyle must mirror EXACTLY (same
//     colours at the mirrored positions), the face must vanish under the rear
//     hair mass, silhouette-edge outline survives while interior lines are
//     eaten, flatten roles swap to their base colours, and the output alpha
//     equals the front alpha (the silhouette is the front's — the invariant
//     that keeps every frame's contour stable).
//  2. Reference comparison: inferred back vs the hand-drawn back. The crown
//     band (y2..y4) is symmetric in the reference and must match BYTE-EXACT;
//     head rows may only contain hair + outline colours; the remaining
//     differences are REPORTED, not failed — the drawn back hair is its own
//     artwork (solid rear mass + decorative bands), larger than a plain
//     mirror, and that deviation is recorded on purpose.
// 背面推断闸门（v0.9.2）。
//  1. 合成自证：不对称发型必须精确镜像（镜像位置同色）、脸必须消失在后脑头发下、
//     剪影边缘 outline 保留而内部线条被吃、flatten 角色换基色、输出 alpha 等于
//     正面 alpha（轮廓 = 正面轮廓 —— 让每帧外框稳定的不变量）。
//  2. 参考对照：推断背面 vs 手绘背面。冠区（y2..y4）在参考里对称，必须逐字节一致；
//     头部行只许出现头发 + 描边色；其余差异只报告不失败 —— 手绘背面头发是独立
//     创作（实心后脑 + 装饰发带），比纯镜像大，该偏差是刻意记录的。
int runBackinferGate(const CliArgs& a) {
    struct Case { const char* json; const char* refBase; };
    const Case cases[] = {
        {"chibi_standard.json", "character_32"},
        {"chibi_alt.json",      "character_alt"},
    };
    int failures = 0;

    for (const Case& c : cases) {
        fb::TemplateSpec spec;
        std::string err;
        if (!fb::loadTemplateSpec(a.templates + "/" + c.json, spec, err)) {
            std::cout << c.json << ": load FAILED: " << err << "   FAIL\n";
            ++failures;
            continue;
        }
        std::cout << c.json << ":\n";

        cv::Vec3b cHairMid, cHairDark, cOutline, cSkin, cBlush, cWhite, cTeal,
            cGold, cRed, cMint;
        const char* need[] = {"hair_mid", "hair_dark", "outline", "skin",
                              "blush", "white", "teal", "gold", "red", "mint"};
        cv::Vec3b* dsts[] = {&cHairMid, &cHairDark, &cOutline, &cSkin, &cBlush,
                             &cWhite, &cTeal, &cGold, &cRed, &cMint};
        bool palOk = true;
        for (int i = 0; i < 10; ++i)
            if (!spec.roleBgr(need[i], *dsts[i])) {
                std::cout << "  palette missing '" << need[i] << "'   FAIL\n";
                ++failures;
                palOk = false;
            }
        if (!palOk) continue;

        // ---- 1. synthetic self-checks / 合成自证 ----
        // Mirror targets are pre-filled with a KEEP colour (mint): the mirror
        // only lands inside the silhouette, so the target must be opaque for
        // the mirror write to happen at all — and being a keep role, the
        // placeholder never enters the mirror layer itself.
        // 镜像目标先用 keep 角色色（mint）占位：镜像只落在轮廓内，目标必须不透明
        // 镜像写入才会发生 —— 占位是 keep 角色，自身绝不进镜像层。
        cv::Mat front = cv::Mat::zeros(spec.size, CV_8UC4);
        auto put = [&](int x, int y, const cv::Vec3b& bgr) {
            front.at<cv::Vec4b>(y, x) = cv::Vec4b(bgr[0], bgr[1], bgr[2], 255);
        };
        // Asymmetric hair: x -> w-1-x. Targets (19,5)/(18,5) pre-filled.
        // 不对称头发：x -> w-1-x。目标 (19,5)/(18,5) 先占位。
        put(12, 5, cHairMid);    put(19, 5, cMint);
        put(13, 5, cHairDark);   put(18, 5, cMint);
        // Face pixel on the head — must be covered by the rear hair mass. Its
        // mirror target (16,8) is left EMPTY: skin never mirrors, and empty
        // stays empty (alpha invariant).
        // 头部行的脸色像素 —— 必须被后脑头发盖住。镜像目标 (16,8) 故意留空：
        // skin 不镜像，空的保持空（alpha 不变量）。
        put(15, 8, cSkin);
        // hair_highlight vs gold disambiguation: same RGB, head row mirrors.
        // hair_highlight 与 gold 消歧：同 RGB，头部行镜像。
        put(14, 3, cGold);       put(17, 3, cMint);
        // Silhouette-edge outline survives — AND the mirror must not paint
        // over it: (21,10) is hair, its mirror lands exactly on (10,10).
        // 剪影边缘 outline 保留 —— 而且镜像不得盖它：(21,10) 是头发，它的镜像
        // 正好落在 (10,10)。
        put(10, 10, cOutline);
        put(21, 10, cHairDark);
        put(15, 10, cOutline);
        // Interior outline (all 4 neighbours opaque) is eaten.
        // 内部 outline（四邻皆不透明）被吃。
        put(15, 11, cOutline);
        put(14, 11, cSkin);
        put(16, 11, cSkin);
        put(15, 12, cSkin);
        // Body rows: gold/red buttons flatten to mint, blush to skin, mint
        // passes through. The same RGB gold here is 'gold', not hair_highlight.
        // 躯干行：金/红扣子压平为 mint，blush 压平为 skin，mint 照搬。
        // 这里的同色 gold 是 gold 而非 hair_highlight。
        put(12, 15, cGold);
        put(13, 15, cRed);
        put(14, 15, cBlush);
        put(15, 15, cMint);

        cv::Mat out = fb::inferBackView(front, cv::Mat(), spec, err);
        if (out.empty()) {
            std::cout << "  synthetic: infer FAILED: " << err << "   FAIL\n";
            ++failures;
            continue;
        }

        struct PxCheck { int x, y; cv::Vec3b want; const char* what; };
        const PxCheck pxc[] = {
            {19, 5,  cHairMid,  "hair_mid mirrored exactly to (19,5)"},
            {18, 5,  cHairDark, "hair_dark mirrored exactly to (18,5)"},
            {12, 5,  cHairMid,  "origin becomes hair base (12,5)"},
            {13, 5,  cHairMid,  "origin becomes hair base (13,5)"},
            {15, 8,  cHairMid,  "face covered by rear hair (15,8)"},
            {17, 3,  cGold,     "head-row gold mirrors as hair_highlight (17,3)"},
            {14, 3,  cHairMid,  "origin of head-row gold becomes hair base"},
            {10, 10, cOutline,  "mirror does not paint over kept contour (10,10)"},
            {21, 10, cHairMid,  "hair origin becomes hair base (21,10)"},
            {15, 10, cOutline,  "silhouette-edge outline kept (15,10)"},
            {15, 11, cHairMid,  "interior outline eaten (15,11)"},
            {12, 15, cMint,     "body gold flattened to mint (12,15)"},
            {13, 15, cMint,     "body red flattened to mint (13,15)"},
            {14, 15, cSkin,     "blush flattened to skin (14,15)"},
            {15, 15, cMint,     "mint passes through (15,15)"},
        };
        for (const PxCheck& p : pxc) {
            const cv::Vec4b got = out.at<cv::Vec4b>(p.y, p.x);
            if (got != cv::Vec4b(p.want[0], p.want[1], p.want[2], 255)) {
                std::cout << "  synthetic: " << p.what << " got ("
                          << int(got[0]) << "," << int(got[1]) << ","
                          << int(got[2]) << ")   FAIL\n";
                ++failures;
            }
        }
        // Alpha invariant: the silhouette is the front's, pixel for pixel.
        // alpha 不变量：轮廓 = 正面轮廓，逐像素。
        bool alphaOk = true;
        for (int y = 0; y < front.rows && alphaOk; ++y)
            for (int x = 0; x < front.cols; ++x)
                if (front.at<cv::Vec4b>(y, x)[3] != out.at<cv::Vec4b>(y, x)[3]) {
                    std::cout << "  synthetic: alpha differs at (" << x << ","
                              << y << ")   FAIL\n";
                    ++failures;
                    alphaOk = false;
                    break;
                }
        if (alphaOk)
            std::cout << "  synthetic: 15 pixel checks + alpha invariant   OK\n";

        // ---- 2. reference comparison / 参考对照 ----
        const std::string frontPath = a.assets + "/" + c.refBase + ".png";
        const std::string backPath  = a.assets + "/" + c.refBase + "_back.png";
        const cv::Mat refFront = cv::imread(frontPath, cv::IMREAD_UNCHANGED);
        const cv::Mat refBack  = cv::imread(backPath, cv::IMREAD_UNCHANGED);
        if (refFront.empty() || refBack.empty()) {
            std::cout << "  reference: cannot read " << frontPath << " / "
                      << backPath << "   FAIL\n";
            ++failures;
            continue;
        }
        cv::Mat inf = fb::inferBackView(refFront, cv::Mat(), spec, err);
        if (inf.empty()) {
            std::cout << "  reference: infer FAILED: " << err << "   FAIL\n";
            ++failures;
            continue;
        }

        // Alpha invariant on the real character too.
        // 真实角色上也跑 alpha 不变量。
        bool refAlphaOk = true;
        for (int y = 0; y < refFront.rows && refAlphaOk; ++y)
            for (int x = 0; x < refFront.cols; ++x)
                if (refFront.at<cv::Vec4b>(y, x)[3] != inf.at<cv::Vec4b>(y, x)[3]) {
                    std::cout << "  reference: alpha differs at (" << x << ","
                              << y << ")   FAIL\n";
                    ++failures;
                    refAlphaOk = false;
                    break;
                }
        if (refAlphaOk) std::cout << "  reference: alpha invariant   OK\n";

        // Crown band y2..y4 is the symmetric region — byte-exact vs drawn.
        // 冠区 y2..y4 是对称区 —— 与手绘逐字节一致。
        int crownDiff = 0;
        for (int y = 2; y <= 4; ++y)
            for (int x = 0; x < inf.cols; ++x)
                if (inf.at<cv::Vec4b>(y, x) != refBack.at<cv::Vec4b>(y, x))
                    ++crownDiff;
        if (crownDiff != 0) {
            std::cout << "  reference: crown band y2-4 differs (" << crownDiff
                      << " px)   FAIL\n";
            ++failures;
        } else {
            std::cout << "  reference: crown band y2-4 byte-exact   OK\n";
        }

        // Head rows may only hold hair + outline (face colours would mean the
        // rear hair mass failed to cover the face). Colours keyed as packed
        // ints (cv::Vec3b has no operator<).
        // 头部行只许头发 + 描边（出现脸色 = 后脑头发没盖住脸）。颜色作打包整数键
        // （cv::Vec3b 没有 operator<）。
        auto keyOf = [](const cv::Vec3b& c) {
            return (int(c[0]) << 16) | (int(c[1]) << 8) | int(c[2]);
        };
        const std::set<int> headAllowed = {
            keyOf(cHairMid), keyOf(cHairDark),
            keyOf(cGold) /* hair_highlight shares gold's RGB */,
            keyOf(cOutline)};
        int residue = 0;
        for (int y : spec.views[0].map.headRows) {
            for (int x = 0; x < inf.cols; ++x) {
                const cv::Vec4b px = inf.at<cv::Vec4b>(y, x);
                if (px[3] == 0) continue;
                if (!headAllowed.count(keyOf(cv::Vec3b(px[0], px[1], px[2])))) {
                    if (residue < 5)
                        std::cout << "  reference: non-hair colour at (" << x
                                  << "," << y << ") in head rows   FAIL\n";
                    ++residue;
                    ++failures;
                }
            }
        }
        if (residue == 0)
            std::cout << "  reference: head rows are hair + outline only   OK\n";

        // Remaining differences vs the hand-drawn back: reported, not failed.
        // 与手绘背面的其余差异：如实报告，不失败。
        const int diff = countPixelDiff(inf, refBack);
        std::cout << "  reference: inferred vs hand-drawn = " << diff
                  << " px differ (known deviation: drawn rear hair is its own "
                     "artwork, larger than a plain mirror)   INFO\n";
    }
    std::cout << (failures ? "BACKINFER GATE FAIL\n" : "BACKINFER GATE OK\n");
    return failures ? 1 : 0;
}

// Part-editor fill gate (v0.10.0) — the in-tool fill loop:
//   blank template -> fill/brush -> role constraint -> library round trip.
// The engine under fillRole is applyPaletteRoles (already gated exact);
// what this gate adds is the EDITOR semantics around it: outline survives a
// fill, undo restores byte-exact, an inferred back obeys the alpha invariant
// and the role constraint, and save -> load returns identical views.
// 部件编辑器填色闸门（v0.10.0）—— 工具内填色闭环：
//   空白模板 -> 填色/画笔 -> 角色约束 -> 库往返。
// fillRole 底下的引擎是 applyPaletteRoles（已闸门覆盖其精确语义）；本闸门补的是
// 环绕它的编辑器语义：填色不动描边、撤销逐字节恢复、推断背面守 alpha 不变量与
// 角色约束、存库 -> 读回视图一致。
int runFillGate(const CliArgs& a) {
    int failures = 0;
    std::string err;
    fb::TemplateSpec spec;
    if (!fb::loadTemplateSpec(a.templates + "/chibi_standard.json", spec, err)) {
        std::cout << "template load FAILED: " << err << "   FAIL\n";
        return 1;
    }
    cv::Vec3b cWhite, cOutline, cHairMid, cHairDark, cSkin;
    const struct { const char* role; cv::Vec3b* dst; } need[] = {
        {"white", &cWhite}, {"outline", &cOutline}, {"hair_mid", &cHairMid},
        {"hair_dark", &cHairDark}, {"skin", &cSkin},
    };
    for (const auto& n : need)
        if (!spec.roleBgr(n.role, *n.dst)) {
            std::cout << "palette missing '" << n.role << "'   FAIL\n";
            return 1;
        }

    auto countCol = [](const cv::Mat& img, const cv::Vec3b& c) {
        int n = 0;
        for (int y = 0; y < img.rows; ++y)
            for (int x = 0; x < img.cols; ++x) {
                const cv::Vec4b p = img.at<cv::Vec4b>(y, x);
                if (p[3] && p[0] == c[0] && p[1] == c[1] && p[2] == c[2]) ++n;
            }
        return n;
    };
    auto sameAlpha = [](const cv::Mat& x, const cv::Mat& y) {
        for (int r = 0; r < x.rows; ++r)
            for (int c = 0; c < x.cols; ++c)
                if (x.at<cv::Vec4b>(r, c)[3] != y.at<cv::Vec4b>(r, c)[3])
                    return false;
        return true;
    };
    auto px = [](const cv::Mat& img, int x, int y) {
        return img.at<cv::Vec4b>(y, x);
    };

    // ---- 1. blank -> fill: exact recolour, outline untouched ----
    // ---- 1. 空白 -> 填色：精确换色，描边不动 ----
    {
        const cv::Mat blank = cv::imread(a.assets + "/parts/_blank/hair_front.png",
                                         cv::IMREAD_UNCHANGED);
        if (blank.empty() || blank.type() != CV_8UC4) {
            std::cout << "blank hair_front.png unreadable   FAIL\n";
            ++failures;
        } else {
            const int nWhite = countCol(blank, cWhite);
            const int nOutline = countCol(blank, cOutline);
            fb::PartEditor ed;
            ed.blank(spec.size);
            ed.pixels[0] = blank.clone();
            ed.fillRole(0, cWhite, cHairMid);
            const cv::Mat& got = ed.pixels[0];
            bool ok = true;
            if (countCol(got, cWhite) != 0) {
                std::cout << "  fill: white residue after fill   FAIL\n";
                ++failures; ok = false;
            }
            if (countCol(got, cOutline) != nOutline) {
                std::cout << "  fill: outline count changed   FAIL\n";
                ++failures; ok = false;
            }
            if (countCol(got, cHairMid) != nWhite) {
                std::cout << "  fill: interior did not recolour 1:1   FAIL\n";
                ++failures; ok = false;
            }
            if (!sameAlpha(blank, got)) {
                std::cout << "  fill: alpha changed by fillRole   FAIL\n";
                ++failures; ok = false;
            }
            const int viol = fb::roleConstraintViolations(got, spec);
            if (viol != 0) {
                std::cout << "  fill: " << viol
                          << " pixels violate the role constraint   FAIL\n";
                ++failures; ok = false;
            }
            if (ok)
                std::cout << "  fill: recolour exact, outline kept, legal   OK\n";
        }
    }

    // ---- 2. brush / eraser / one-step undo ----
    // ---- 2. 画笔 / 橡皮 / 单步撤销 ----
    {
        fb::PartEditor ed;
        ed.blank(spec.size);
        ed.paint(fb::PartEditor::FRONT, 5, 5, cHairDark);
        ed.snapshot();
        ed.paint(fb::PartEditor::FRONT, 5, 5, cHairMid);
        ed.paint(fb::PartEditor::FRONT, 7, 3, cSkin);
        ed.erase(fb::PartEditor::FRONT, 5, 5);
        ed.paint(fb::PartEditor::FRONT, -1, 99, cSkin);   // ignored / 越界忽略
        ed.undo();
        bool ok = true;
        if (px(ed.pixels[0], 5, 5) != cv::Vec4b(cHairDark[0], cHairDark[1], cHairDark[2], 255)) {
            std::cout << "  undo: (5,5) not restored to the snapshot   FAIL\n";
            ++failures; ok = false;
        }
        if (px(ed.pixels[0], 7, 3)[3] != 0) {
            std::cout << "  undo: (7,3) paint survived undo   FAIL\n";
            ++failures; ok = false;
        }
        ed.erase(fb::PartEditor::FRONT, 5, 5);
        if (px(ed.pixels[0], 5, 5)[3] != 0) {
            std::cout << "  eraser: (5,5) not transparent   FAIL\n";
            ++failures; ok = false;
        }
        if (ok) std::cout << "  brush/eraser/undo round-trip byte-exact   OK\n";
    }

    // ---- 3. inferred back: alpha invariant + role constraint ----
    // ---- 3. 推断背面：alpha 不变量 + 角色约束 ----
    {
        const cv::Mat refFront = cv::imread(a.assets + "/character_32.png",
                                            cv::IMREAD_UNCHANGED);
        fb::PartEditor ed;
        ed.blank(spec.size);
        if (refFront.empty()) {
            std::cout << "  back: cannot read character_32.png   FAIL\n";
            ++failures;
        } else {
            ed.pixels[0] = refFront.clone();
            if (!ed.inferBack(spec, err)) {
                std::cout << "  back: infer FAILED: " << err << "   FAIL\n";
                ++failures;
            } else {
                bool ok = true;
                if (!sameAlpha(ed.pixels[0], ed.pixels[2])) {
                    std::cout << "  back: alpha invariant broken   FAIL\n";
                    ++failures; ok = false;
                }
                const int viol = fb::roleConstraintViolations(ed.pixels[2], spec);
                if (viol != 0) {
                    std::cout << "  back: " << viol
                              << " pixels violate the role constraint   FAIL\n";
                    ++failures; ok = false;
                }
                if (ok) std::cout << "  back: alpha invariant + legal colours   OK\n";
            }
        }
    }

    // ---- 4. save -> load round trip (auto back and file back) ----
    // ---- 4. 存库 -> 读回往返（auto 与 file 两种背面） ----
    {
        namespace fs = std::filesystem;
        const fs::path lib = fs::temp_directory_path() / "flipbook_fill_gate_lib";
        std::error_code ec;
        fs::remove_all(lib, ec);

        fb::PartEditor ed;
        ed.blank(spec.size);
        ed.itemName = "roundtrip_test";
        ed.slot = "hair";
        const cv::Mat rf = cv::imread(a.assets + "/character_32.png", cv::IMREAD_UNCHANGED);
        const cv::Mat rs = cv::imread(a.assets + "/character_32_side.png", cv::IMREAD_UNCHANGED);
        ed.pixels[0] = rf.clone();
        ed.pixels[1] = rs.clone();

        bool ok = true;
        if (!fb::saveEditorItem(ed, lib.string(), err)) {
            std::cout << "  save: FAILED: " << err << "   FAIL\n";
            ++failures; ok = false;
        } else if (fs::exists(lib / "hair/roundtrip_test/back.png")) {
            std::cout << "  save: back.png written although back is un-drawn   FAIL\n";
            ++failures; ok = false;
        } else {
            fb::PartEditor ed2;
            if (!fb::loadEditorItem(ed2, (lib / "hair/roundtrip_test").string(),
                                    "hair", spec.size, err)) {
                std::cout << "  load: FAILED: " << err << "   FAIL\n";
                ++failures; ok = false;
            } else {
                if (countPixelDiff(ed2.pixels[0], ed.pixels[0]) != 0 ||
                    countPixelDiff(ed2.pixels[1], ed.pixels[1]) != 0) {
                    std::cout << "  save->load: views not byte-identical   FAIL\n";
                    ++failures; ok = false;
                }
                if (!ed2.pixels[2].empty()) {
                    std::cout << "  save->load: back appeared from nowhere   FAIL\n";
                    ++failures; ok = false;
                }
            }
        }
        // Now with an inferred back (item.json must flip to back:"file").
        // 再带推断背面存一次（item.json 必须翻成 back:"file"）。
        if (ok && ed.inferBack(spec, err) &&
            fb::saveEditorItem(ed, lib.string(), err)) {
            if (!fs::exists(lib / "hair/roundtrip_test/back.png")) {
                std::cout << "  save: back.png missing although back is drawn   FAIL\n";
                ++failures; ok = false;
            } else {
                fb::PartEditor ed3;
                if (!fb::loadEditorItem(ed3, (lib / "hair/roundtrip_test").string(),
                                        "hair", spec.size, err) ||
                    countPixelDiff(ed3.pixels[2], ed.pixels[2]) != 0) {
                    std::cout << "  save->load: back not byte-identical   FAIL\n";
                    ++failures; ok = false;
                }
            }
        } else if (ok) {
            std::cout << "  save(with back): FAILED: " << err << "   FAIL\n";
            ++failures; ok = false;
        }
        // Name validation. / 名称校验。
        ed.itemName = "bad/name";
        if (fb::saveEditorItem(ed, lib.string(), err)) {
            std::cout << "  save: slash name accepted   FAIL\n";
            ++failures; ok = false;
        }
        fs::remove_all(lib, ec);
        if (ok) std::cout << "  save->load round trip + name rules   OK\n";
    }

    // ---- 5. every blank template is legal as-is (white + outline roles) ----
    // ---- 5. 每张空白模板原样即合法（white + outline 都是角色色） ----
    {
        const char* slots[6] = {"hair", "face", "top", "pants", "boots", "body"};
        const char* views[2] = {"front", "side"};
        int bad = 0;
        for (const char* s : slots)
            for (const char* v : views) {
                const std::string p = a.assets + "/parts/_blank/" + s + "_" + v + ".png";
                const cv::Mat img = cv::imread(p, cv::IMREAD_UNCHANGED);
                if (img.empty() || fb::roleConstraintViolations(img, spec) != 0) {
                    std::cout << "  blank: " << p << " illegal   FAIL\n";
                    ++bad; ++failures;
                }
            }
        if (!bad) std::cout << "  blank: 12 templates legal (fill-ready)   OK\n";
    }

    std::cout << (failures ? "FILL GATE FAIL\n" : "FILL GATE OK\n");
    return failures ? 1 : 0;
}

// --compose: recipe + library -> three template-aligned views on disk.
// --compose：配方 + 部件库 -> 三张模板对齐视图落盘。
int runCompose(const CliArgs& a) {
    std::string err;
    if (a.recipe.empty()) {
        std::cerr << "FAIL: --recipe is required\n";
        return 1;
    }
    fb::ComposeRecipe recipe;
    if (!fb::loadComposeRecipe(a.recipe, recipe, err)) {
        std::cerr << "FAIL: " << err << "\n";
        return 1;
    }
    fb::TemplateSpec spec;
    if (!fb::loadTemplateSpec(a.templates + "/" + recipe.templateName + ".json",
                              spec, err)) {
        std::cerr << "FAIL: " << err << "\n";
        return 1;
    }
    fb::PartsLibrary lib;
    if (!fb::loadPartsLibrary(a.library, spec.size, lib, err)) {
        std::cerr << "FAIL: " << err << "\n";
        return 1;
    }
    cv::Mat out[3];
    if (!fb::composeThreeViews(recipe, lib, spec, out, err)) {
        std::cerr << "FAIL: " << err << "\n";
        return 1;
    }
    std::error_code ec;
    std::filesystem::create_directories(a.out, ec);
    const char* names[3] = {"front.png", "side.png", "back.png"};
    for (int v = 0; v < 3; ++v) {
        const std::string p = a.out + "/" + names[v];
        if (!fb::saveImageBgra(p, out[v], err)) {
            std::cerr << "FAIL: " << err << "\n";
            return 1;
        }
        std::cout << "Wrote: " << p << "\n";
    }
    return 0;
}

// --compose-gate: the composer's correctness gate (v0.10.1). The six
// reference items recompose the reference views byte-exact (reconstruction
// invariant), then every semantic contract is asserted, then the composed
// views run the fourdir pipeline to the same sheet as the reference views.
// --compose-gate：合成器正确性闸门（v0.10.1）。六件参考物品逐字节重合成参考
// 视图（重建不变量），再断言全部语义契约，最后合成视图走四方向管线出图与
// 参考视图一致。
int runComposeGate(const CliArgs& a) {
    namespace fs = std::filesystem;
    int failures = 0;
    std::string err;

    fb::TemplateSpec spec;
    if (!fb::loadTemplateSpec(a.templates + "/chibi_standard.json", spec, err)) {
        std::cout << "template load FAILED: " << err << "   FAIL\n";
        return 1;
    }
    fb::PartsLibrary lib;
    if (!fb::loadPartsLibrary(a.library, spec.size, lib, err)) {
        std::cout << "library load FAILED: " << err << "   FAIL\n";
        return 1;
    }

    auto sameMat = [](const cv::Mat& x, const cv::Mat& y) {
        return x.size() == y.size() && x.type() == y.type() &&
               std::memcmp(x.data, y.data, x.total() * x.elemSize()) == 0;
    };
    auto sameAlpha = [](const cv::Mat& x, const cv::Mat& y) {
        for (int r = 0; r < x.rows; ++r)
            for (int c = 0; c < x.cols; ++c)
                if (x.at<cv::Vec4b>(r, c)[3] != y.at<cv::Vec4b>(r, c)[3])
                    return false;
        return true;
    };
    auto writeText = [](const fs::path& p, const std::string& text) {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f << text;
    };

    // Full-reference recipe: every slot names its reference item.
    // 全参考配方：每槽点名其参考物品。
    const char* slots[6] = {"hair", "face", "top", "pants", "boots", "body"};
    fb::ComposeRecipe ref;
    ref.templateName = "chibi_standard";
    for (const char* s : slots) ref.items[s] = std::string("ref_") + s;

    const char* refNames[3] = {"character_32.png", "character_32_side.png",
                               "character_32_back.png"};
    cv::Mat refImgs[3];
    for (int v = 0; v < 3; ++v)
        refImgs[v] = cv::imread(a.assets + "/" + refNames[v], cv::IMREAD_UNCHANGED);

    // ---- 1. reconstruction invariant ----
    // ---- 1. 重建不变量 ----
    cv::Mat composed[3];
    {
        const bool ok = fb::composeThreeViews(ref, lib, spec, composed, err);
        int bad = 0;
        for (int v = 0; v < 3; ++v) {
            if (!ok || refImgs[v].empty() || !sameMat(composed[v], refImgs[v])) {
                std::cout << "  recon view " << v
                          << ": composed != reference   FAIL\n";
                ++bad; ++failures;
            }
        }
        if (ok && !bad)
            std::cout << "  recon: six items recompose the reference views "
                         "byte-exact   OK\n";
        if (!ok) std::cout << "  recon: compose failed: " << err << "   FAIL\n";
    }

    // ---- 2. role overrides recolour exactly ----
    // ---- 2. 角色覆盖精确换色 ----
    {
        fb::ComposeRecipe pink = ref;
        fb::LibraryItem::RoleColorOverride ro;
        ro.role = "hair_mid";
        ro.hasSrc = true;
        if (!spec.roleBgr("hair_mid", ro.srcBgr)) {
            std::cout << "  palette: hair_mid missing   FAIL\n";
            ++failures;
        }
        ro.dstBgr = cv::Vec3b(255, 0, 255);   // pure pink in BGR / BGR 纯粉
        pink.roleOverrides.push_back(ro);
        cv::Mat out[3];
        if (!fb::composeThreeViews(pink, lib, spec, out, err)) {
            std::cout << "  role override: compose failed: " << err << "   FAIL\n";
            ++failures;
        } else {
            const cv::Mat& before = composed[0];
            const cv::Mat& after = out[0];
            int bad = 0, changed = 0;
            for (int y = 0; y < before.rows; ++y)
                for (int x = 0; x < before.cols; ++x) {
                    const cv::Vec4b pa = before.at<cv::Vec4b>(y, x);
                    const cv::Vec4b pb = after.at<cv::Vec4b>(y, x);
                    const bool wasHair =
                        pa[3] && pa[0] == ro.srcBgr[0] && pa[1] == ro.srcBgr[1] &&
                        pa[2] == ro.srcBgr[2];
                    if (pa[3] != pb[3]) { ++bad; continue; }
                    if (wasHair) {
                        if (pb[0] == 255 && pb[1] == 0 && pb[2] == 255) ++changed;
                        else ++bad;
                    } else if (pa != pb) ++bad;
                }
            if (bad || !changed) {
                std::cout << "  role override: " << bad
                          << " wrong pixels, " << changed
                          << " recoloured   FAIL\n";
                ++failures;
            } else {
                std::cout << "  role override: " << changed
                          << " hair_mid pixels -> pink, rest untouched   OK\n";
            }
        }
    }

    // ---- 3. back:"auto" == per-item inference ----
    // ---- 3. back:"auto" == 逐物品推断 ----
    {
        cv::Mat inferred;
        fb::LibraryItem* hair = nullptr;
        fb::PartsLibrary autoLib = lib;
        for (auto& it : autoLib.items)
            if (it.slot == "hair" && it.name == "ref_hair") hair = &it;
        if (!hair) {
            std::cout << "  back auto: ref_hair missing   FAIL\n";
            ++failures;
        } else {
            inferred = fb::inferBackView(hair->views[0], hair->views[1], spec, err);
            if (inferred.empty() || !sameAlpha(inferred, hair->views[0])) {
                std::cout << "  back auto: inference broke the alpha invariant"
                             "   FAIL\n";
                ++failures;
            }
            hair->backMode = "auto";
            hair->views[2] = cv::Mat();   // no hand-drawn back / 无手绘背面
            cv::Mat outA[3], outB[3];
            const bool okA = fb::composeThreeViews(ref, autoLib, spec, outA, err);
            fb::PartsLibrary fileLib = lib;
            for (auto& it : fileLib.items)
                if (it.slot == "hair" && it.name == "ref_hair")
                    it.views[2] = inferred;   // inference fed as a file sprite
            const bool okB = fb::composeThreeViews(ref, fileLib, spec, outB, err);
            if (!okA || !okB || !sameMat(outA[2], outB[2])) {
                std::cout << "  back auto: compose(auto) != compose(inference"
                             " as file)   FAIL\n";
                ++failures;
            } else if (sameMat(outA[2], composed[2])) {
                std::cout << "  back auto: output identical to hand-drawn — "
                             "inference not actually used   FAIL\n";
                ++failures;
            } else {
                std::cout << "  back auto: per-item inference, alpha invariant"
                             "   OK\n";
            }
        }
    }

    // ---- 4. recipe.json round-trip ----
    // ---- 4. recipe.json 往返 ----
    {
        const fs::path tmp =
            fs::temp_directory_path() / "flipbook_recipe_gate.json";
        fb::ComposeRecipe r;
        r.templateName = "chibi_alt";
        r.items["hair"] = "ref_hair";
        r.items["top"] = "ref_top";
        fb::LibraryItem::RoleColorOverride ro1;
        ro1.role = "hair_mid";
        ro1.hasSrc = true;
        ro1.srcBgr = cv::Vec3b(1, 2, 3);
        ro1.dstBgr = cv::Vec3b(4, 5, 6);
        fb::LibraryItem::RoleColorOverride ro2;
        ro2.role = "skin";
        ro2.hasSrc = false;
        ro2.dstBgr = cv::Vec3b(7, 8, 9);
        r.roleOverrides = {ro1, ro2};
        bool ok = fb::saveComposeRecipe(tmp.string(), r, err);
        fb::ComposeRecipe loaded;
        if (ok) ok = fb::loadComposeRecipe(tmp.string(), loaded, err);
        if (!ok || loaded.templateName != "chibi_alt" ||
            loaded.items.size() != 2 || loaded.items["hair"] != "ref_hair" ||
            loaded.items["top"] != "ref_top" ||
            loaded.roleOverrides.size() != 2 ||
            !loaded.roleOverrides[0].hasSrc ||
            loaded.roleOverrides[0].srcBgr != cv::Vec3b(1, 2, 3) ||
            loaded.roleOverrides[0].dstBgr != cv::Vec3b(4, 5, 6) ||
            loaded.roleOverrides[1].hasSrc ||
            loaded.roleOverrides[1].dstBgr != cv::Vec3b(7, 8, 9)) {
            std::cout << "  recipe round-trip: mismatch (" << err << ")   FAIL\n";
            ++failures;
        } else {
            std::cout << "  recipe round-trip: save -> load fields intact   OK\n";
        }
        fs::remove(tmp);
    }

    // ---- 5. bad recipes are refused ----
    // ---- 5. 坏配方被拒 ----
    {
        int bad = 0;
        const fs::path tmp =
            fs::temp_directory_path() / "flipbook_recipe_bad.json";
        // Unknown slot name in items. / items 里的槽名未知。
        writeText(tmp, "{\"items\": {\"hat\": \"ref_hair\"}}\n");
        fb::ComposeRecipe r;
        if (fb::loadComposeRecipe(tmp.string(), r, err)) ++bad;
        // Unknown item name. / 物品名未知。
        fb::ComposeRecipe missing = ref;
        missing.items["hair"] = "no_such_item";
        cv::Mat out[3];
        if (fb::composeThreeViews(missing, lib, spec, out, err)) ++bad;
        // Role override with no src and a role outside the palette. /
        // 覆盖无 src 且角色不在 palette。
        fb::ComposeRecipe badRole = ref;
        fb::LibraryItem::RoleColorOverride ro;
        ro.role = "made_up_role";
        ro.hasSrc = false;
        ro.dstBgr = cv::Vec3b(0, 0, 0);
        badRole.roleOverrides.push_back(ro);
        if (fb::composeThreeViews(badRole, lib, spec, out, err)) ++bad;
        fs::remove(tmp);
        if (bad) {
            std::cout << "  bad recipes: " << bad << " accepted   FAIL\n";
            ++failures;
        } else {
            std::cout << "  bad recipes: unknown slot / item / role all refused"
                         "   OK\n";
        }
    }

    // ---- 6. full chain: composed views == reference views through the
    //         fourdir pipeline (same sheet, byte-exact) ----
    // ---- 6. 全链：合成视图经四方向管线出图 == 参考视图出图（逐字节） ----
    {
        fb::ChipSet chips;
        if (!fb::loadChipSet(a.chips, chips, err)) {
            std::cout << "  full chain: chips load failed: " << err << "   FAIL\n";
            ++failures;
        } else {
            fb::FourDirResult r1, r2;
            std::string e1, e2;
            const bool ok1 = fb::buildFourDirSheet(
                composed[0], composed[1], composed[2], chips, r1, e1);
            const bool ok2 = fb::buildFourDirSheet(
                refImgs[0], refImgs[1], refImgs[2], chips, r2, e2);
            if (!ok1 || !ok2 || !sameMat(r1.sheet, r2.sheet)) {
                std::cout << "  full chain: sheet mismatch (" << e1 << " / "
                          << e2 << ")   FAIL\n";
                ++failures;
            } else {
                std::cout << "  full chain: compose -> fourdir sheet matches "
                             "the reference chain byte-exact   OK\n";
            }
        }
    }

    std::cout << (failures ? "COMPOSE GATE FAIL\n" : "COMPOSE GATE OK\n");
    return failures ? 1 : 0;
}

int runPaletteGate(const CliArgs& a) {
    fb::ChipSet cs;
    std::string err;
    if (!fb::loadChipSet(a.chips, cs, err)) { std::cerr << "FAIL: " << err << "\n"; return 1; }
    fb::PaletteMap map;
    if (!fb::loadPaletteMap(a.map, map, err)) { std::cerr << "FAIL: " << err << "\n"; return 1; }

    // The map must be a function on its own: one source colour, one target.
    // 映射本身必须是函数：一个源色只能有一个目标色。
    std::map<uint32_t, cv::Vec4b> mapBySrc;
    for (const auto& sw : map) {
        const auto it = mapBySrc.find(colourKey(sw.first));
        if (it != mapBySrc.end() && it->second != sw.second) {
            std::cerr << "FAIL: palette map sends one source colour to two targets\n";
            return 1;
        }
        mapBySrc[colourKey(sw.first)] = sw.second;
    }

    std::cout << "Chips: " << a.chips << "\nMap:   " << a.map
              << " (" << map.size() << " entries)\n\n";

    const char* viewNames[3] = {"front", "side", "back"};
    const char* negSuffix[3] = {"", "_side", "_back"};
    int failures = 0;

    for (int v = 0; v < 3; ++v) {
        const fb::MotionChips* idle = cs.find(std::string(viewNames[v]) + "_idle");
        const fb::MotionChips* walk = cs.find(std::string(viewNames[v]) + "_walk");
        if (!idle || !walk) {
            std::cout << "  " << viewNames[v] << ": chip set missing flipbooks   FAIL\n";
            ++failures;
            continue;
        }
        const cv::Mat& ref0 = idle->frames[0];

        // ---- positive: exact round trip / 正向：精确往返 ----
        cv::Mat user = ref0.clone();
        fb::applyPaletteMap(user, map);

        fb::PaletteMap extracted;
        fb::ExtractStats st;
        if (!fb::extractPaletteMap(user, ref0, extracted, err, &st)) {
            std::cout << "  " << viewNames[v] << ": extraction FAILED on the synthetic "
                      << "input: " << err << "   FAIL\n";
            ++failures;
            continue;
        }

        // Expected: exactly the map entries whose source appears in ref0.
        // 期望：恰好是"源色出现在 ref0 里"的那些映射条目。
        const std::set<uint32_t> refKeys = opaqueColourKeys(ref0);
        std::map<uint32_t, uint32_t> expected, got;
        for (const auto& kv : mapBySrc)
            if (refKeys.count(kv.first))
                expected[kv.first] = colourKey(kv.second);
        for (const auto& sw : extracted)
            got[colourKey(sw.first)] = colourKey(sw.second);

        const int opaquePixels = st.comparedPixels;
        bool ok = (st.maskDiffPixels == 0) && (got == expected);
        int refOpaque = 0;
        for (int y = 0; y < ref0.rows; ++y) {
            const cv::Vec4b* row = ref0.ptr<cv::Vec4b>(y);
            for (int x = 0; x < ref0.cols; ++x) if (row[x][3] != 0) ++refOpaque;
        }
        ok = ok && (opaquePixels == refOpaque);

        std::cout << "  " << viewNames[v] << ": round trip "
                  << extracted.size() << " colours / " << opaquePixels << " px";
        if (!ok) {
            std::cout << "   FAIL (map mismatch or pixel count)\n";
            ++failures;
        } else {
            std::cout << "   OK\n";
        }

        // ---- negative 1: a one-pixel third colour must be rejected ----
        // 负向 1：单像素第三色必须被拒绝 ----
        std::set<uint32_t> dstKeys;
        for (const auto& sw : map) dstKeys.insert(colourKey(sw.second));
        cv::Vec4b third(1, 2, 3, 255);
        for (int t = 0; t < 256 && dstKeys.count(colourKey(third)); ++t)
            third = cv::Vec4b((uchar)t, (uchar)(255 - t), 7, 255);

        cv::Mat dirty = user.clone();
        bool flipped = false;
        for (int y = 0; y < dirty.rows && !flipped; ++y) {
            cv::Vec4b* row = dirty.ptr<cv::Vec4b>(y);
            for (int x = 0; x < dirty.cols; ++x) {
                if (row[x][3] != 0) { row[x] = third; flipped = true; break; }
            }
        }
        fb::PaletteMap dummy;
        if (fb::extractPaletteMap(dirty, ref0, dummy, err)) {
            std::cout << "  " << viewNames[v]
                      << ": extractor ACCEPTED a one-pixel third colour   FAIL\n";
            ++failures;
        } else {
            std::cout << "  " << viewNames[v] << ": third colour rejected   OK\n";
        }

        // ---- negative 2: the given user images must be rejected when they
        // fail strict alignment (only if --front/--side/--back were passed).
        // ---- 负向 2：给定的用户图若不满足严格对齐必须被拒绝（仅当传了
        // --front/--side/--back）。
        const std::string& negPath =
            (v == 0 ? a.frontImg : (v == 1 ? a.sideImg : a.backImg));
        if (!negPath.empty()) {
            cv::Mat neg;
            if (!fb::loadImageBgra(negPath, neg)) {
                std::cout << "  " << viewNames[v] << ": cannot load " << negPath << "   FAIL\n";
                ++failures;
            } else if (fb::extractPaletteMap(neg, ref0, dummy, err)) {
                std::cout << "  " << viewNames[v] << ": extractor ACCEPTED " << negPath
                          << " (must be strict)   FAIL\n";
                ++failures;
            } else {
                std::cout << "  " << viewNames[v] << ": " << negPath << " rejected   OK\n";
            }
        }
    }

    if (failures) {
        std::cout << "\nRESULT: FAIL — " << failures << " check(s) failed.\n"
                  << "结果：失败 —— " << failures << " 项检查未过。\n";
        return 1;
    }
    std::cout << "\nRESULT: PASS — extraction round-trips exactly and stays strict.\n"
              << "结果：通过 —— 提取精确往返且保持严格。\n";
    return 0;
}

// The four-direction sheet gate. Everything is synthesised from the chips and
// the palette map, so no fixture images are needed. Checks the layout, the
// idle cycle-fill, left = mirrored right, the alpha-mask invariant (with the
// row-order contract), and the palette coverage (map completeness).
// 四方向大图闸门。一切由芯片与调色板映射合成，无需夹具图。检查布局、待机补帧、
// left=right 镜像、alpha 掩码不变量（连同行序契约）、调色板覆盖（映射完整性）。
int runFourdirGate(const CliArgs& a) {
    fb::ChipSet cs;
    std::string err;
    if (!fb::loadChipSet(a.chips, cs, err)) { std::cerr << "FAIL: " << err << "\n"; return 1; }
    fb::PaletteMap map;
    if (!fb::loadPaletteMap(a.map, map, err)) { std::cerr << "FAIL: " << err << "\n"; return 1; }

    // Synthetic user views: idle frame 0 with the map applied.
    // 合成用户视图：对 idle f0 应用映射。
    const char* viewNames[3] = {"front", "side", "back"};
    cv::Mat user[3];
    for (int v = 0; v < 3; ++v) {
        const fb::MotionChips* idle = cs.find(std::string(viewNames[v]) + "_idle");
        if (!idle) { std::cerr << "FAIL: chip set missing " << viewNames[v] << "_idle\n"; return 1; }
        user[v] = idle->frames[0].clone();
        fb::applyPaletteMap(user[v], map);
    }

    fb::FourDirResult res;
    if (!fb::buildFourDirSheet(user[0], user[1], user[2], cs, res, err)) {
        std::cerr << "FAIL: buildFourDirSheet: " << err << "\n";
        return 1;
    }

    std::cout << "Chips: " << a.chips << "\nMap:   " << a.map
              << " (" << map.size() << " entries)\n"
              << "Sheet: " << res.sheet.cols << "x" << res.sheet.rows << "\n\n";

    int failures = 0;
    auto fail = [&](const std::string& what) {
        std::cout << "  " << what << "   FAIL\n";
        ++failures;
    };

    // (1) layout / 布局
    if (res.sheet.type() != CV_8UC4 ||
        res.sheet.cols != cs.cell.width * fb::SHEET_COLS ||
        res.sheet.rows != cs.cell.height * fb::SHEET_ROWS)
        fail("sheet is not " + std::to_string(cs.cell.width * fb::SHEET_COLS) + "x" +
             std::to_string(cs.cell.height * fb::SHEET_ROWS) + " BGRA");
    else
        std::cout << "  layout 8x6   OK\n";

    // (2) idle cycle-fill: f0 f1 f0 f1 f0 f1 / 待机补帧
    bool fillOK = true;
    for (int r = 0; r < fb::SHEET_ROWS && fillOK; r += 2) {   // idle rows 0,2,4,6
        const std::vector<cv::Mat>& row = res.rows.rows[r];
        if (row.size() != (size_t)fb::SHEET_COLS) { fillOK = false; break; }
        for (int i = 0; i < fb::SHEET_COLS && fillOK; ++i) {
            if (!imagesIdentical(row[i], row[i % 2])) fillOK = false;
        }
    }
    if (!fillOK) fail("idle rows are not cycle-filled f0 f1 f0 f1 f0 f1");
    else std::cout << "  idle cycle-fill   OK\n";

    // (3) left = mirrored right / left = right 镜像
    bool mirrorOK = true;
    for (int i = 0; i < fb::SHEET_COLS && mirrorOK; ++i) {
        if (!imagesIdentical(res.rows.rows[4][i], fb::mirrorFrame(res.rows.rows[6][i]))) mirrorOK = false;
        if (!imagesIdentical(res.rows.rows[5][i], fb::mirrorFrame(res.rows.rows[7][i]))) mirrorOK = false;
    }
    if (!mirrorOK) fail("left rows are not the mirrored right rows");
    else std::cout << "  left = mirror(right)   OK\n";

    // (4) alpha masks preserved + row order contract. The expected chip for
    //     each cell: rows 0/2/6 = idle[i%2] of front/back/side, rows 1/3/7 =
    //     walk[i], rows 4/5 = mirrored side idle/walk. Mirroring flips x.
    // (4) alpha 掩码不变 + 行序契约。每格期望芯片：行 0/2/6 = front/back/side 的
    //     idle[i%2]，行 1/3/7 = walk[i]，行 4/5 = 镜像的 side idle/walk。镜像翻转 x。
    const fb::MotionChips* chipsById[3][2] = {
        {cs.find("front_idle"), cs.find("front_walk")},
        {cs.find("side_idle"),  cs.find("side_walk")},
        {cs.find("back_idle"),  cs.find("back_walk")},
    };
    // row -> (view index, anim index) for the verbatim rows; -1 = mirrored side.
    // 行 ->（视图下标，动画下标）用于原样行；-1 = 镜像 side。
    const int rowView[fb::SHEET_ROWS]   = {0, 0, 2, 2, -1, -1, 1, 1};
    const int rowAnim[fb::SHEET_ROWS]   = {0, 1, 0, 1, 0, 1, 0, 1};
    bool maskOK = true;
    for (int r = 0; r < fb::SHEET_ROWS && maskOK; ++r) {
        const int v = rowView[r] < 0 ? 1 : rowView[r];
        const fb::MotionChips* m = chipsById[v][rowAnim[r]];
        if (!m) { maskOK = false; break; }
        for (int i = 0; i < fb::SHEET_COLS && maskOK; ++i) {
            const cv::Mat& chip = m->frames[i % m->frameCount];
            const cv::Mat expect = (rowView[r] < 0) ? fb::mirrorFrame(chip) : chip;
            if (!alphaMaskEqual(res.rows.rows[r][i], expect)) maskOK = false;
        }
    }
    if (!maskOK) fail("alpha mask changed (or row order is wrong)");
    else std::cout << "  alpha masks + row order   OK\n";

    // (5) coverage: every opaque colour is a map destination, and the map
    //     covers every ref colour the view's chips use (idle AND walk) —
    //     the standing guard for "extraction ran on idle f0 only".
    // (5) 覆盖：非透明色全是映射目标色，且映射覆盖该视图芯片（idle 与 walk）
    //     用到的全部参考色 —— "提取只做 idle f0"的常驻守门人。
    for (int v = 0; v < 3; ++v) {
        std::set<uint32_t> dst, allRef;
        for (const auto& sw : res.maps[v]) dst.insert(colourKey(sw.second));
        for (int anim = 0; anim < 2; ++anim) {
            const fb::MotionChips* m = chipsById[v][anim];
            if (!m) { ++failures; continue; }
            for (const cv::Mat& f : m->frames) {
                const std::set<uint32_t> keys = opaqueColourKeys(f);
                allRef.insert(keys.begin(), keys.end());
            }
        }
        bool covOK = ((int)res.maps[v].size() == (int)allRef.size());
        // Output rows for this view: 0/1 front, 2/3 back, 4-7 side.
        // 该视图的输出行：0/1 front，2/3 back，4-7 side。
        const int r0 = (v == 0) ? 0 : (v == 1 ? 4 : 2);
        for (int r = r0; r < r0 + 2 && covOK; ++r) {
            for (const cv::Mat& f : res.rows.rows[r]) {
                for (int y = 0; y < f.rows && covOK; ++y) {
                    const cv::Vec4b* row = f.ptr<cv::Vec4b>(y);
                    for (int x = 0; x < f.cols; ++x) {
                        if (row[x][3] != 0 && !dst.count(colourKey(row[x]))) {
                            covOK = false;
                            break;
                        }
                    }
                }
            }
        }
        if (!covOK) {
            std::cout << "  " << viewNames[v] << " palette coverage ("
                      << res.maps[v].size() << " mapped vs " << allRef.size()
                      << " ref colours)   FAIL\n";
            ++failures;
        } else {
            std::cout << "  " << viewNames[v] << " palette coverage ("
                      << res.maps[v].size() << " colours)   OK\n";
        }
    }

    if (failures) {
        std::cout << "\nRESULT: FAIL — " << failures << " check(s) failed.\n"
                  << "结果：失败 —— " << failures << " 项检查未过。\n";
        return 1;
    }
    std::cout << "\nRESULT: PASS — the 8x6 four-direction sheet is well-formed.\n"
              << "结果：通过 —— 8x6 四方向大图形态正确。\n";
    return 0;
}

// The app pipeline from the CLI: three template-aligned user views in, one
// four-direction sheet PNG out. The GUI must export byte-identical output for
// the same inputs (the golden invariant).
// CLI 版 app 管线：三张模板对齐的用户视图进，一张四方向大图 PNG 出。同样的输入，
// GUI 必须导出逐字节一致的结果（金不变量）。
int runFourdir(const CliArgs& a) {
    if (a.frontImg.empty() || a.sideImg.empty() || a.backImg.empty()) {
        std::cerr << "--fourdir needs --front, --side and --back images.\n";
        return 2;
    }
    fb::ChipSet cs;
    std::string err;
    if (!fb::loadChipSet(a.chips, cs, err)) { std::cerr << "FAIL: " << err << "\n"; return 1; }

    cv::Mat views[3];
    const std::string paths[3] = {a.frontImg, a.sideImg, a.backImg};
    for (int v = 0; v < 3; ++v) {
        if (!fb::loadImageBgra(paths[v], views[v])) {
            std::cerr << "FAIL: cannot load " << paths[v] << "\n";
            return 1;
        }
    }

    fb::FourDirResult res;
    if (!fb::buildFourDirSheet(views[0], views[1], views[2], cs, res, err)) {
        std::cerr << "FAIL: " << err << "\n";
        return 1;
    }

    const std::string outPath = a.out + "/fourdir_sheet.png";
    std::error_code ec;
    std::filesystem::create_directories(a.out, ec);   // out dir may not exist yet / 输出目录可能还不存在
    if (!fb::saveImageBgra(outPath, res.sheet, err)) {
        std::cerr << "FAIL: " << err << "\n";
        return 1;
    }

    std::cout << "Chips:  " << a.chips << "\n"
              << "Views:  " << a.frontImg << " / " << a.sideImg << " / " << a.backImg << "\n"
              << "Colours: front " << res.maps[0].size() << ", side " << res.maps[1].size()
              << ", back " << res.maps[2].size() << "\n"
              << "Sheet:  " << res.sheet.cols << "x" << res.sheet.rows
              << " (" << fb::SHEET_ROWS << " rows x " << fb::SHEET_COLS << " cols)\n"
              << "Wrote:  " << outPath << "\n";
    return 0;
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
    if (a.idlePurity)  return runIdlePurity(a);
    if (a.chipRecon)   return runChipRecon(a);
    if (a.chipReskin)  return runChipReskin(a);
    if (a.chipRender)  return runChipRender(a);
    if (a.paletteGate) return runPaletteGate(a);
    if (a.fourdirGate) return runFourdirGate(a);
    if (!a.alignCase.empty()) return runAlignGate(a);
    if (a.templateLoadGate)   return runTemplateLoadGate(a);
    if (a.partsSemanticsGate) return runPartsSemanticsGate(a);
    if (a.partsLibraryGate)   return runPartsLibraryGate(a);
    if (a.backinferGate)      return runBackinferGate(a);
    if (a.fillGate)           return runFillGate(a);
    if (a.compose)     return runCompose(a);
    if (a.composeGate) return runComposeGate(a);
    if (a.fourdir)     return runFourdir(a);
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
