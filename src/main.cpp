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
#include <opencv2/imgproc.hpp>
#include <cstdint>
#include <cstring>
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
