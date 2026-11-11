// motion.cpp — Tier B chip loading, palette reskin, verbatim playback
// Tier B 芯片读取、换皮、逐帧照搬回放
//
// The chips are full-cell sprites cropped from the reference sheet (see
// tools/extract_motion_chips.py). Playback is a plain clone + palette swap with
// NO transform, so a rendered frame is pixel-exact to the reference cell. This
// is the CLAUDE.md "Tier B (chip playback)" path.
// 芯片是参考图裁出的整格精灵（见 tools/extract_motion_chips.py）。回放是
// clone + 换色、不做任何变换，故渲染帧与参考格逐像素一致。此即 CLAUDE.md
// 「Tier B（芯片回放）」通道。

#include "core.h"

#include <fstream>
#include <sstream>

namespace fb {
namespace {

// Skip blank lines and '#' comments (same convention as rig.cpp).
// 跳过空行与 '#' 注释（约定同 rig.cpp）。
bool nextDataLine(std::istream& in, std::string& line) {
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();
        if (line.empty()) continue;
        return true;
    }
    return false;
}

// Parse "1,2,3,4 5,6,7,8" into two BGRA vectors. Commas become spaces.
// 把 "1,2,3,4 5,6,7,8" 解析成两个 BGRA 向量。逗号视作空格。
bool parseBgraPair(const std::string& line, cv::Vec4b& a, cv::Vec4b& b) {
    std::string cleaned;
    cleaned.reserve(line.size());
    for (char c : line) cleaned += (c == ',' ? ' ' : c);
    std::stringstream ss(cleaned);
    int v[8];
    for (int& x : v) x = 0;
    for (int i = 0; i < 8; ++i) if (!(ss >> v[i])) return false;
    a = cv::Vec4b((uchar)v[0], (uchar)v[1], (uchar)v[2], (uchar)v[3]);
    b = cv::Vec4b((uchar)v[4], (uchar)v[5], (uchar)v[6], (uchar)v[7]);
    return true;
}

}  // namespace

const MotionChips* ChipSet::find(const std::string& name) const {
    for (const MotionChips& m : flipbooks)
        if (m.name == name) return &m;
    return nullptr;
}

void applyPaletteMap(cv::Mat& img, const PaletteMap& map) {
    if (map.empty()) return;
    for (int y = 0; y < img.rows; ++y) {
        cv::Vec4b* row = img.ptr<cv::Vec4b>(y);
        for (int x = 0; x < img.cols; ++x) {
            for (const auto& sw : map) {
                if (row[x] == sw.first) { row[x] = sw.second; break; }
            }
        }
    }
}

bool loadChipSet(const std::string& dir, ChipSet& out, std::string& err) {
    out = ChipSet{};
    std::ifstream f(dir + "/index.txt");
    if (!f) {
        err = "index.txt not found in " + dir;
        return false;
    }

    std::string line;
    while (nextDataLine(f, line)) {
        std::stringstream ss(line);
        std::string key;
        if (!(ss >> key)) continue;

        if (key == "source") {
            ss >> out.sourcePath;
        } else if (key == "cell") {
            int c = 0; ss >> c;
            out.cell = cv::Size(c, c);
        } else if (key == "flipbook") {
            MotionChips m;
            int row = -1;
            if (!(ss >> m.name >> m.frameCount >> m.fps >> row)) {
                err = "bad flipbook line: " + line;
                return false;
            }
            m.sheetRow = row;
            if (m.frameCount <= 0) {
                err = m.name + ": frameCount must be > 0";
                return false;
            }
            for (int i = 0; i < m.frameCount; ++i) {
                const std::string p =
                    dir + "/chips/" + m.name + "_" + std::to_string(i) + ".png";
                cv::Mat chip;
                if (!loadImageBgra(p, chip)) {
                    err = "failed to load chip: " + p;
                    return false;
                }
                if (out.cell.width > 0 && chip.size() != out.cell) {
                    err = p + ": chip " + std::to_string(chip.cols) + "x" +
                          std::to_string(chip.rows) + " != cell " +
                          std::to_string(out.cell.width) + "x" +
                          std::to_string(out.cell.height);
                    return false;
                }
                m.frames.push_back(chip);
            }
            out.flipbooks.push_back(std::move(m));
        }
    }

    if (out.flipbooks.empty()) {
        err = "no flipbooks listed in " + dir + "/index.txt";
        return false;
    }
    return true;
}

bool loadPaletteMap(const std::string& path, PaletteMap& out, std::string& err) {
    out.clear();
    std::ifstream f(path);
    if (!f) {
        err = "palette map not found: " + path;
        return false;
    }
    std::string line;
    while (nextDataLine(f, line)) {
        cv::Vec4b a, b;
        if (!parseBgraPair(line, a, b)) {
            err = "bad palette line: " + line;
            return false;
        }
        out.push_back({a, b});
    }
    return true;
}

std::vector<cv::Mat> renderFromChips(const MotionChips& chips, const PaletteMap& map) {
    std::vector<cv::Mat> frames;
    frames.reserve(chips.frames.size());
    for (const cv::Mat& chip : chips.frames) {
        cv::Mat f = chip.clone();   // never mutate the cached chip / 绝不改动缓存芯片
        applyPaletteMap(f, map);
        frames.push_back(f);
    }
    return frames;
}

}  // namespace fb
