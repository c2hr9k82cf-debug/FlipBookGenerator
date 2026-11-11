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
