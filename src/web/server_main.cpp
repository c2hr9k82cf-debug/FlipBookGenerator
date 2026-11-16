// flipbook_server — the web service shell over flipbook_core / 网站服务薄壳
//
// Three template-aligned views in (multipart upload), one four-direction sheet
// plus engine-agnostic metadata out. It calls the SAME buildFourDirSheet the
// GUI and the CLI call — the golden invariant holds by construction — in the
// GUI's tolerant mode (warn and repair, never reject a repairable view).
// 三张模板对齐视图进（multipart 上传），一张四方向大图 + 引擎无关元数据出。
// 调用的是与 GUI/CLI 完全相同的 buildFourDirSheet —— 金不变量构造性成立 ——
// 模式取 GUI 的宽容（提醒并修复，可修复的视图绝不拒绝）。
//
// Endpoints / 接口：
//   GET  /health                      -> {"ok":true,"version":...}
//   POST /api/convert                 -> multipart: front / side / back (PNG)
//   GET  /api/jobs/<id>/sheet.png     -> the sheet / 大图
//   GET  /api/jobs/<id>/meta.json     -> metadata / 元数据
//   GET  /api/jobs/<id>/diff_<view>.png -> per-view diff overlay / 差异标注图
//   GET  /                            -> web/index.html (static) / 前端页面
//
// English on top, Chinese below — per the project's bilingual convention.
// 英文在上，中文下 —— 沿用项目双语约定。

#include <httplib.h>

#include "core/align.h"
#include "core/core.h"
#include "core/template_spec.h"
#include "version.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Upload contract / 上传契约
constexpr size_t kMaxImageBytes = 16u * 1024 * 1024;   // per image / 单图上限
constexpr size_t kMaxPayload    = 64u * 1024 * 1024;   // whole request / 整请求上限
constexpr auto   kJobMaxAge     = std::chrono::hours(24);
constexpr size_t kJobMaxCount   = 256;

struct ServerArgs {
    int port = 8080;
    std::string webDir = "web";
    std::string jobsDir = "jobs";
    std::string assetsDir;      // resolved below / 下面解析
    std::string chipsDir;       // default <assets>/templates/motion_chips
    std::string templatesDir;   // default <assets>/templates
};

// Everything the handlers need, loaded once at startup / 处理器所需，启动时载入
struct Service {
    fb::ChipSet chips;
    fb::TemplateSpec spec;
    cv::Mat refFrame[3];        // idle chip frame 0 per view (autoAlign target)
                                // 每视图 idle 芯片第 0 帧（自动对齐目标）
    fs::path jobsDir;
};

const char* kViewNames[3] = {"front", "side", "back"};
const char* kViewCn[3]    = {"正面", "侧面", "背面"};

// ---------------------------------------------------------------------------
// Small JSON helpers (output only — parsing is not needed server-side)
// 小 JSON 辅助（只需输出 —— 服务端不需要解析）
// ---------------------------------------------------------------------------
std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string jsonArray(const std::vector<std::string>& items) {
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ", ";
        out += "\"" + jsonEscape(items[i]) + "\"";
    }
    out += "]";
    return out;
}

// ---------------------------------------------------------------------------
// Job ids and housekeeping / 任务 id 与内务
// ---------------------------------------------------------------------------
std::string newJobId() {
    static std::mutex m;
    static std::mt19937_64 rng{std::random_device{}()};
    std::lock_guard<std::mutex> lock(m);
    std::ostringstream os;
    os << std::hex << (rng() & 0xffffffffffffULL);
    return os.str();
}

bool validJobId(const std::string& id) {
    if (id.size() < 8 || id.size() > 32) return false;
    return std::all_of(id.begin(), id.end(),
                       [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

// Delete job dirs past their TTL, and the oldest beyond the count cap. Called
// on every convert — cheap for the small dirs a LAN tool ever accumulates.
// 删除过期任务目录，超出数量上限时删最旧的。每次 convert 调用 —— 局域网工具
// 攒下的目录很小，代价可忽略。
void cleanupJobs(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    struct Entry { fs::path p; fs::file_time_type t; };
    std::vector<Entry> entries;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_directory(ec)) continue;
        entries.push_back({e.path(), e.last_write_time(ec)});
    }
    const auto now = fs::file_time_type::clock::now();
    std::vector<Entry> keep;
    for (const auto& e : entries) {
        if (now - e.t > kJobMaxAge) {
            fs::remove_all(e.p, ec);
        } else {
            keep.push_back(e);
        }
    }
    std::sort(keep.begin(), keep.end(),
              [](const Entry& a, const Entry& b) { return a.t < b.t; });
    while (keep.size() > kJobMaxCount) {
        fs::remove_all(keep.front().p, ec);
        keep.erase(keep.begin());
    }
}

bool isPng(const std::string& bytes) {
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    return bytes.size() >= 8 &&
           std::equal(sig, sig + 8,
                      reinterpret_cast<const unsigned char*>(bytes.data()));
}

void sendJson(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_content(body, "application/json; charset=utf-8");
}

void sendError(httplib::Response& res, int status, const std::string& msg) {
    sendJson(res, status,
             "{\"ok\":false,\"error\":\"" + jsonEscape(msg) + "\"}");
}

// ---------------------------------------------------------------------------
// Metadata — engine-agnostic, mirrors the Phase 5 export contract
// 元数据 —— 引擎无关，对应 Phase 5 导出契约
// ---------------------------------------------------------------------------
std::string buildMetaJson(const Service& svc, const fb::FourDirResult& res,
                          const std::vector<std::string>& warnings,
                          const int imgW[3], const int imgH[3],
                          const fb::AlignParams align[3],
                          const fb::ExtractStats stats[3]) {
    // Row -> source chip flipbook (left rows mirror the side chips).
    // 行 -> 源芯片翻页书（左侧行是侧视芯片的镜像）。
    static const char* rowChips[8] = {"front_idle", "front_walk",
                                      "back_idle",  "back_walk",
                                      "side_idle",  "side_walk",
                                      "side_idle",  "side_walk"};
    static const char* rowNames[8] = {"down_idle", "down_walk",
                                      "up_idle",   "up_walk",
                                      "left_idle", "left_walk",
                                      "right_idle", "right_walk"};

    std::ostringstream os;
    os << "{\n"
       << "  \"app\": \"FlipBookGenerator\",\n"
       << "  \"version\": \"" << FLIPBOOK_VERSION << "\",\n"
       << "  \"frame\": {\"w\": " << svc.chips.cell.width
       << ", \"h\": " << svc.chips.cell.height << "},\n"
       << "  \"sheet\": {\"file\": \"sheet.png\", \"w\": " << res.sheet.cols
       << ", \"h\": " << res.sheet.rows
       << ", \"cols\": " << fb::SHEET_COLS
       << ", \"rows\": " << fb::SHEET_ROWS << "},\n"
       << "  \"animations\": [\n";
    for (int r = 0; r < fb::SHEET_ROWS; ++r) {
        const fb::MotionChips* m = svc.chips.find(rowChips[r]);
        const int frames = m ? m->frameCount : (r % 2 ? 6 : 2);
        const int fps    = m ? m->fps : (r % 2 ? 10 : 4);
        os << "    {\"name\": \"" << rowNames[r] << "\", \"row\": " << r
           << ", \"frames\": " << frames << ", \"fps\": " << fps
           << ", \"loop\": true, \"mirror\": " << (r >= 4 && r <= 5 ? "true" : "false")
           << "}" << (r + 1 < fb::SHEET_ROWS ? "," : "") << "\n";
    }
    os << "  ],\n";
    os << "  \"warnings\": " << jsonArray(warnings) << ",\n";
    os << "  \"inputs\": [\n";
    for (int v = 0; v < 3; ++v) {
        os << "    {\"view\": \"" << kViewNames[v] << "\", \"w\": " << imgW[v]
           << ", \"h\": " << imgH[v]
           << ", \"align\": {\"scale\": " << align[v].scale
           << ", \"dx\": " << align[v].dx << ", \"dy\": " << align[v].dy << "}"
           << ", \"mask_diff_px\": " << stats[v].maskDiffPixels
           << ", \"colour_conflict_px\": " << stats[v].colourConflictPixels
           << "}" << (v < 2 ? "," : "") << "\n";
    }
    os << "  ]\n"
       << "}\n";
    return os.str();
}

// Warnings, in the GUI's wording (app_state.cpp) so the two surfaces say the
// same thing about the same input. / 警告文案与 GUI（app_state.cpp）一致，
// 同一输入两端说法相同。
std::vector<std::string> buildWarnings(const int imgW[3], const int imgH[3],
                                       const fb::AlignParams align[3],
                                       const fb::ExtractStats stats[3],
                                       const cv::Size& cell) {
    std::vector<std::string> warn;
    for (int v = 0; v < 3; ++v) {
        std::string w;
        if (stats[v].maskDiffPixels > 0) {
            w += std::string(kViewCn[v]) + "：剪影与模板差 " +
                 std::to_string(stats[v].maskDiffPixels) + " 像素（宽容模式已放行，见差异图）。";
        }
        if (stats[v].colourConflictPixels > 0) {
            if (!w.empty()) w += " ";
            w += std::string(kViewCn[v]) + "：有 " +
                 std::to_string(stats[v].colourConflictPixels) +
                 " 个像素配色不一致（见差异图），已按主色处理。";
        }
        if (align[v].scale > 1 || align[v].dx != 0 || align[v].dy != 0) {
            if (!w.empty()) w += " ";
            w += std::string(kViewCn[v]) + "：已按 x" +
                 std::to_string(align[v].scale) + " 缩放、(" +
                 std::to_string(align[v].dx) + "," + std::to_string(align[v].dy) +
                 ") 平移对齐。";
        }
        if (imgW[v] != cell.width || imgH[v] != cell.height) {
            if (!w.empty()) w += " ";
            w += std::string(kViewCn[v]) + "：输入为 " + std::to_string(imgW[v]) + "x" +
                 std::to_string(imgH[v]) + "，已尽力对齐到 " +
                 std::to_string(cell.width) + "x" + std::to_string(cell.height) +
                 " 模板（建议导出 32x32 整数倍像素图）。";
        }
        if (!stats[v].warning.empty()) {
            if (!w.empty()) w += " ";
            w += std::string(kViewCn[v]) + "：" + stats[v].warning;
        }
        if (!w.empty()) warn.push_back(w);
    }
    return warn;
}

// Composite the diff overlay onto the aligned view (z-order, no blending —
// pixel-art rules hold even in diagnostics). / 把差异标注叠到对齐视图上
// （z 序覆盖、不混合 —— 像素风规则连诊断图也遵守）。
cv::Mat compositeDiff(const cv::Mat& base, const cv::Mat& overlay) {
    cv::Mat out = base.clone();
    if (overlay.empty() || overlay.size() != base.size()) return out;
    for (int y = 0; y < out.rows; ++y) {
        const cv::Vec4b* o = overlay.ptr<cv::Vec4b>(y);
        cv::Vec4b* d = out.ptr<cv::Vec4b>(y);
        for (int x = 0; x < out.cols; ++x)
            if (o[x][3] != 0) d[x] = cv::Vec4b(o[x][0], o[x][1], o[x][2], 255);
    }
    return out;
}

// ---------------------------------------------------------------------------
// The convert handler body / convert 处理主体
// ---------------------------------------------------------------------------
void handleConvert(const Service& svc, const httplib::Request& req,
                   httplib::Response& res) {
    struct Upload { std::string bytes; std::string path; };
    Upload up[3];
    for (int v = 0; v < 3; ++v) {
        if (!req.form.has_file(kViewNames[v])) {
            sendError(res, 400, std::string("缺少 ") + kViewNames[v] +
                                " 图片（multipart 字段名须为 front / side / back）。");
            return;
        }
        up[v].bytes = req.form.get_file(kViewNames[v]).content;
        if (up[v].bytes.empty()) {
            sendError(res, 400, std::string(kViewNames[v]) + " 图片为空。");
            return;
        }
        if (up[v].bytes.size() > kMaxImageBytes) {
            sendError(res, 400, std::string(kViewNames[v]) + " 图片超过 16MB 限制。");
            return;
        }
        if (!isPng(up[v].bytes)) {
            sendError(res, 400, std::string(kViewNames[v]) +
                                " 不是 PNG —— 请导出带透明通道的 PNG（像素图）。");
            return;
        }
    }

    cleanupJobs(svc.jobsDir);
    const std::string id = newJobId();
    const fs::path job = svc.jobsDir / id;
    std::error_code ec;
    fs::create_directories(job, ec);
    if (ec) { sendError(res, 500, "无法创建任务目录：" + ec.message()); return; }

    cv::Mat aligned[3];
    fb::AlignParams align[3];
    int imgW[3] = {0, 0, 0}, imgH[3] = {0, 0, 0};

    for (int v = 0; v < 3; ++v) {
        up[v].path = (job / ("input_" + std::string(kViewNames[v]) + ".png")).string();
        std::ofstream f(up[v].path, std::ios::binary);
        f.write(up[v].bytes.data(), (std::streamsize)up[v].bytes.size());
        f.close();

        cv::Mat img;
        if (!fb::loadImageBgra(up[v].path, img)) {
            sendError(res, 400, std::string("无法解码 ") + kViewNames[v] + " 图片。");
            return;
        }
        imgW[v] = img.cols;
        imgH[v] = img.rows;

        // Auto-align every upload (the GUI's 自动对齐 button): an already
        // aligned view returns identity params, so the clean path is untouched.
        // 每张图都跑自动对齐（GUI 的「自动对齐」按钮）：已对齐的图返回恒等参数，
        // 干净路径不受影响。
        int mismatch = 0;
        if (!fb::autoAlign(img, svc.refFrame[v], align[v], &mismatch)) {
            align[v] = fb::AlignParams{};
        }
        aligned[v] = fb::applyAlign(img, align[v], svc.chips.cell);
    }

    fb::FourDirResult out;
    fb::ExtractStats stats[3];
    std::string err;
    if (!fb::buildFourDirSheet(aligned[0], aligned[1], aligned[2], svc.chips,
                               svc.spec, out, err, stats, /*tolerantMask=*/true)) {
        sendError(res, 400, "管线失败：" + err);
        return;
    }

    // Per-view diff overlays (GUI parity diagnostics) — only written when the
    // view actually has diffs. / 每视图差异标注图（与 GUI 诊断对齐）—— 仅在
    // 确有差异时落盘。
    std::string diffUrls[3];
    for (int v = 0; v < 3; ++v) {
        // Only when the view actually has diffs — the masks are allocated
        // (all-zero) whenever stats are requested. / 仅在确有差异时写 ——
        // 请求 stats 时掩码总会被分配（哪怕全零）。
        if (stats[v].maskDiffPixels == 0 && stats[v].colourConflictPixels == 0)
            continue;
        const cv::Mat overlay =
            fb::renderDiffOverlay(aligned[v], stats[v].maskDiffMask,
                                  stats[v].colourConflictMask);
        const cv::Mat marked = compositeDiff(aligned[v], overlay);
        const std::string name = "diff_" + std::string(kViewNames[v]) + ".png";
        std::string saveErr;
        if (fb::saveImageBgra((job / name).string(), marked, saveErr))
            diffUrls[v] = "/api/jobs/" + id + "/" + name;
    }

    std::string saveErr;
    if (!fb::saveImageBgra((job / "sheet.png").string(), out.sheet, saveErr)) {
        sendError(res, 500, "无法写出大图：" + saveErr);
        return;
    }

    const std::vector<std::string> warnings =
        buildWarnings(imgW, imgH, align, stats, svc.chips.cell);
    const std::string meta = buildMetaJson(svc, out, warnings, imgW, imgH, align, stats);
    {
        std::ofstream f(job / "meta.json");
        f << meta;
    }

    std::ostringstream body;
    body << "{\n"
         << "  \"ok\": true,\n"
         << "  \"job\": \"" << id << "\",\n"
         << "  \"sheet_url\": \"/api/jobs/" << id << "/sheet.png\",\n"
         << "  \"meta_url\": \"/api/jobs/" << id << "/meta.json\",\n"
         << "  \"diff_urls\": {";
    bool firstDiff = true;
    for (int v = 0; v < 3; ++v) {
        if (diffUrls[v].empty()) continue;
        if (!firstDiff) body << ", ";
        firstDiff = false;
        body << "\"" << kViewNames[v] << "\": \"" << diffUrls[v] << "\"";
    }
    body << "},\n"
         << "  \"warnings\": " << jsonArray(warnings) << ",\n"
         << "  \"meta\": " << meta << "\n"
         << "}";
    sendJson(res, 200, body.str());

    std::printf("[job %s] views %dx%d/%dx%d/%dx%d -> sheet %dx%d, %zu warning(s)\n",
                id.c_str(), imgW[0], imgH[0], imgW[1], imgH[1], imgW[2], imgH[2],
                out.sheet.cols, out.sheet.rows, warnings.size());
    std::fflush(stdout);
}

bool parseArgs(int argc, char** argv, ServerArgs& a, std::string& err) {
    // Env fallback mirrors the GUI's asset convention / 环境变量回退沿用 GUI 的
    // 资产约定（FLIPBOOK_ASSETS）。
    if (const char* env = std::getenv("FLIPBOOK_ASSETS")) a.assetsDir = env;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) { err = std::string(what) + " needs a value"; return nullptr; }
            return argv[++i];
        };
        if (s == "--help" || s == "-h") { err = "help"; return false; }
        else if (s == "--port") { const char* v = next("--port"); if (!v) return false; a.port = std::atoi(v); }
        else if (s == "--web") { const char* v = next("--web"); if (!v) return false; a.webDir = v; }
        else if (s == "--jobs") { const char* v = next("--jobs"); if (!v) return false; a.jobsDir = v; }
        else if (s == "--assets") { const char* v = next("--assets"); if (!v) return false; a.assetsDir = v; }
        else if (s == "--chips") { const char* v = next("--chips"); if (!v) return false; a.chipsDir = v; }
        else if (s == "--templates") { const char* v = next("--templates"); if (!v) return false; a.templatesDir = v; }
        else { err = "unknown option: " + s; return false; }
    }
    if (a.assetsDir.empty()) a.assetsDir = "assets";
    if (a.templatesDir.empty()) a.templatesDir = a.assetsDir + "/templates";
    if (a.chipsDir.empty()) a.chipsDir = a.templatesDir + "/motion_chips";
    if (a.port <= 0 || a.port > 65535) { err = "bad --port"; return false; }
    return true;
}

void printUsage() {
    std::printf(
        "flipbook_server — FlipBookGenerator web service / 网站服务\n"
        "Usage / 用法:\n"
        "  flipbook_server [--port 8080] [--web DIR] [--jobs DIR]\n"
        "                  [--assets DIR | --templates DIR --chips DIR]\n"
        "Defaults / 默认: port 8080, web=web, jobs=jobs, assets=assets\n"
        "                (FLIPBOOK_ASSETS env overrides assets / 环境变量可覆盖)\n"
        "API: POST /api/convert (multipart front/side/back PNG) -> sheet + meta\n"
        "     GET  /api/jobs/<id>/sheet.png | meta.json | diff_<view>.png\n"
        "     GET  /health, / (static frontend / 静态前端)\n");
}

}  // namespace

int main(int argc, char** argv) {
    ServerArgs args;
    std::string err;
    if (!parseArgs(argc, argv, args, err)) {
        if (err == "help") { printUsage(); return 0; }
        std::fprintf(stderr, "Error: %s\n\n", err.c_str());
        printUsage();
        return 2;
    }

    Service svc;
    svc.jobsDir = args.jobsDir;
    if (!fb::loadChipSet(args.chipsDir, svc.chips, err)) {
        std::fprintf(stderr, "FAIL: %s\n", err.c_str());
        return 1;
    }
    if (!fb::loadTemplateSpec(args.templatesDir + "/chibi_standard.json",
                              svc.spec, err)) {
        std::fprintf(stderr, "FAIL: %s\n", err.c_str());
        return 1;
    }
    // Idle chip frame 0 per view — the autoAlign target, exactly what the GUI
    // diagnostics use. / 每视图 idle 芯片第 0 帧 —— 自动对齐目标，与 GUI 诊断一致。
    static const char* kIdleChips[3] = {"front_idle", "side_idle", "back_idle"};
    for (int v = 0; v < 3; ++v) {
        const fb::MotionChips* idle = svc.chips.find(kIdleChips[v]);
        if (!idle || idle->frames.empty()) {
            std::fprintf(stderr, "FAIL: chip set has no %s\n", kIdleChips[v]);
            return 1;
        }
        svc.refFrame[v] = idle->frames[0];
    }

    std::error_code ec;
    fs::create_directories(svc.jobsDir, ec);
    if (!fs::is_directory(args.webDir, ec)) {
        std::fprintf(stderr, "FAIL: web dir not found: %s\n", args.webDir.c_str());
        return 1;
    }

    httplib::Server svr;
    svr.set_payload_max_length(kMaxPayload);

    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, 200,
                 std::string("{\"ok\":true,\"version\":\"") + FLIPBOOK_VERSION + "\"}");
    });

    svr.Post("/api/convert", [&svc](const httplib::Request& req,
                                    httplib::Response& res) {
        handleConvert(svc, req, res);
    });

    // Regex route: 8-32 hex job id + exactly two file names — no path is ever
    // taken from user text. / 正则路由：8-32 位十六进制 id + 两个固定文件名 ——
    // 路径绝不来自用户文本。
    svr.Get(R"(/api/jobs/([0-9a-f]{8,32})/(sheet\.png|meta\.json|diff_(?:front|side|back)\.png))",
            [&svc](const httplib::Request& req, httplib::Response& res) {
                const std::string id = req.matches[1].str();
                const std::string file = req.matches[2].str();
                if (!validJobId(id)) { sendError(res, 400, "bad job id"); return; }
                const fs::path p = svc.jobsDir / id / file;
                std::error_code ec2;
                if (!fs::is_regular_file(p, ec2)) {
                    sendError(res, 404, "job file not found");
                    return;
                }
                std::ifstream f(p, std::ios::binary);
                std::ostringstream buf;
                buf << f.rdbuf();
                const char* type = (file == "meta.json")
                    ? "application/json; charset=utf-8" : "image/png";
                res.set_content(buf.str(), type);
                // sheet.png doubles as the download link / sheet.png 同时是下载链接
                if (file == "sheet.png") {
                    res.set_header("Content-Disposition",
                                   "attachment; filename=\"fourdir_sheet.png\"");
                }
            });

    svr.set_mount_point("/", args.webDir);

    std::printf("flipbook_server %s / 网站服务\n", FLIPBOOK_VERSION);
    std::printf("  chips:     %s\n", args.chipsDir.c_str());
    std::printf("  templates: %s\n", args.templatesDir.c_str());
    std::printf("  web:       %s\n", args.webDir.c_str());
    std::printf("  jobs:      %s\n", args.jobsDir.c_str());
    std::printf("  listening on 0.0.0.0:%d — open http://localhost:%d/\n",
                args.port, args.port);
    std::fflush(stdout);

    if (!svr.listen("0.0.0.0", args.port)) {
        std::fprintf(stderr, "FAIL: cannot listen on port %d\n", args.port);
        return 1;
    }
    return 0;
}
