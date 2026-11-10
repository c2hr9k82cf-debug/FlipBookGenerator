// rig.cpp — part-bundle loading and saving
// 部件包的读写
//
// The bundle format is the project's interface between extraction and
// animation. Full spec in CLAUDE.md "Data formats".
// 部件包格式是提取与动画之间的接口。完整规格见 CLAUDE.md「数据格式」。

#include "core.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace fb {
namespace {

// Canonical part names in id order (must match utils.cpp PARTS).
// 按 id 顺序的规范部件名（必须与 utils.cpp 的 PARTS 一致）。
const char* kPartNames[PART_COUNT] = {
    "head", "body", "arm_left", "arm_right", "leg_left", "leg_right",
};

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Skip blank lines and '#' comments.
// 跳过空行与 '#' 注释。
bool nextDataLine(std::istream& in, std::string& line) {
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') continue;
        // Trim trailing CR from files written on other platforms.
        // 去掉跨平台写入的行尾 CR。
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();
        if (line.empty()) continue;
        return true;
    }
    return false;
}

}  // namespace

int Rig::presentCount() const {
    int n = 0;
    for (bool p : present) if (p) ++n;
    return n;
}

bool loadRigFromDir(const std::string& dir, Rig& out, std::string& err) {
    out = Rig{};

    // --- image_path.txt -> source image / 源图 ---
    std::string pathRaw;
    if (!readFile(dir + "/image_path.txt", pathRaw)) {
        err = "image_path.txt not found in " + dir;
        return false;
    }
    {
        std::stringstream ss(pathRaw);
        std::string line;
        std::getline(ss, line);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        out.sourcePath = line;
    }

    // Resolve relative to the current working directory (the project root in
    // normal use). Fall back to also trying next to the bundle.
    // 相对当前工作目录解析（正常使用时是项目根）。失败时再试部件包旁边。
    out.source = cv::imread(out.sourcePath, cv::IMREAD_UNCHANGED);
    if (out.source.empty()) {
        out.source = cv::imread(dir + "/" + out.sourcePath, cv::IMREAD_UNCHANGED);
    }
    if (out.source.empty()) {
        err = "failed to load source image: " + out.sourcePath;
        return false;
    }
    if (out.source.channels() == 3) {
        cv::cvtColor(out.source, out.source, cv::COLOR_BGR2BGRA);
    } else if (out.source.channels() != 4) {
        err = "source image must be 3 or 4 channel, got " +
              std::to_string(out.source.channels());
        return false;
    }
    out.size = out.source.size();

    // Defaults / 默认值
    out.present.fill(false);
    out.parts.fill(cv::Mat());
    out.bbox.fill(cv::Rect());
    out.pivots.fill(cv::Point2f(0.f, 0.f));
    // legs -> body -> arms -> head: limbs behind the torso, head on top.
    // 腿→躯干→手臂→头：四肢在躯干后，头在最上。
    out.zOrder = {PART_LEG_LEFT, PART_LEG_RIGHT, PART_BODY,
                  PART_ARM_LEFT, PART_ARM_RIGHT, PART_HEAD};

    // --- anchors.txt -> pivots (optional) / 旋转锚点（可选）---
    // Format: "partId x y", one pivot per part. '#' comments allowed.
    // 格式：partId x y，每部件一个。允许 '#' 注释。
    {
        std::ifstream f(dir + "/anchors.txt");
        if (f) {
            std::string line;
            while (nextDataLine(f, line)) {
                std::stringstream ss(line);
                int id = 0;
                float x = 0.f, y = 0.f;
                if (!(ss >> id >> x >> y)) continue;
                if (id < 1 || id > PART_COUNT) continue;
                out.pivots[id - 1] = cv::Point2f(x, y);
            }
        }
        // Missing anchors.txt is not fatal — defaultPivot() values are filled in
        // below from each bbox. / 缺 anchors.txt 不致命，下面按 bbox 填默认值。
    }

    // --- each part's PNG + bbox / 每个部件的 PNG 与 bbox ---
    for (int i = 0; i < PART_COUNT; ++i) {
        const std::string name = kPartNames[i];
        const std::string pngPath = dir + "/" + name + ".png";

        cv::Mat sprite = cv::imread(pngPath, cv::IMREAD_UNCHANGED);
        if (sprite.empty()) continue;   // part absent / 部件缺失
        if (sprite.channels() == 3) {
            cv::cvtColor(sprite, sprite, cv::COLOR_BGR2BGRA);
        }

        cv::Rect box;
        std::ifstream bf(dir + "/" + name + "_bbox.txt");
        if (bf) {
            std::string line;
            if (nextDataLine(bf, line)) {
                std::stringstream ss(line);
                int x = 0, y = 0, w = 0, h = 0;
                if (ss >> x >> y >> w >> h) box = cv::Rect(x, y, w, h);
            }
        }
        if (box.width <= 0 || box.height <= 0) {
            // No bbox recorded: assume the sprite sits at the origin. This is
            // lossy and should not happen for a bundle written by the extractor.
            // 没有 bbox：假定部件在原点。这会丢位置信息，正常不会发生。
            box = cv::Rect(0, 0, sprite.cols, sprite.rows);
        }
        if (box.size() != sprite.size()) {
            err = name + ": bbox " + std::to_string(box.width) + "x" +
                  std::to_string(box.height) + " does not match sprite " +
                  std::to_string(sprite.cols) + "x" + std::to_string(sprite.rows);
            return false;
        }

        out.parts[i]   = sprite;
        out.bbox[i]    = box;
        out.present[i] = true;

        // If anchors.txt gave no pivot for this part, fall back to a sensible
        // joint derived from the bbox (mirrors utils.cpp defaultPivot).
        // 若 anchors.txt 没给该部件锚点，退回按 bbox 推的合理关节（同 utils.cpp defaultPivot）。
        if (out.pivots[i].x == 0.f && out.pivots[i].y == 0.f) {
            const float cx = box.x + (box.width - 1) / 2.f;
            const float topY = static_cast<float>(box.y);
            switch (i + 1) {
                case PART_HEAD:      out.pivots[i] = cv::Point2f(cx, box.y + box.height - 1.f); break;
                case PART_ARM_LEFT:  out.pivots[i] = cv::Point2f(box.x + box.width - 1.f, topY); break;
                case PART_ARM_RIGHT: out.pivots[i] = cv::Point2f(static_cast<float>(box.x), topY); break;
                default:             out.pivots[i] = cv::Point2f(cx, topY); break;
            }
        }
    }

    if (out.presentCount() == 0) {
        err = "no parts found in " + dir + " (expected head.png, body.png, ...)";
        return false;
    }
    return true;
}

bool saveRigToDir(const Rig& rig, const std::string& dir, std::string& err) {
    // Deferred: the GUI writes bundles in Phase 3. The extractor's own writer
    // is authoritative until then. Keeping the loader symmetry here costs
    // nothing and gives the reconstruction test a round-trip to check.
    // 暂缓：GUI 在 Phase 3 写部件包。在此之前 extractor 自己的写出是权威。
    // 这里保持读写对称不花成本，也让重建校验能做往返检查。
    (void)rig;
    (void)dir;
    err = "saveRigToDir not implemented yet (Phase 3)";
    return false;
}

}  // namespace fb
