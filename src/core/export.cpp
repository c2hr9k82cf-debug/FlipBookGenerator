// export.cpp — render animations and write frames + spritesheet
// 渲染动画并写出帧序列 + 精灵图

#include "core.h"

#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace fb {

std::vector<cv::Mat> renderAnimation(const Rig& rig, const Animation& anim) {
    std::vector<cv::Mat> frames;
    frames.reserve(anim.frameCount);
    for (int f = 0; f < anim.frameCount; ++f) {
        frames.push_back(composeFrame(rig, anim.poseAt(f)));
    }
    return frames;
}

bool exportAnimation(const std::vector<cv::Mat>& frames,
                     const std::string& outDir,
                     const std::string& baseName,
                     std::string& err,
                     std::string* sheetPath) {
    if (frames.empty()) {
        err = "no frames to export";
        return false;
    }
    const cv::Size sz = frames.front().size();
    for (const auto& f : frames) {
        if (f.size() != sz) {
            err = "frame size mismatch — all frames must be the same size";
            return false;
        }
    }

    // Each animation gets its own self-contained directory:
    //   <outDir>/<baseName>/frame_0.png, frame_1.png, ...
    //   <outDir>/<baseName>/spritesheet.png
    // cv::imwrite does NOT create directories, so make it first.
    // 每个动画一个自包含目录：帧序列 + 精灵图都放里面。
    // cv::imwrite 不会自建目录，必须先建。
    const fs::path animDir = fs::path(outDir) / fs::path(baseName);
    std::error_code ec;
    fs::create_directories(animDir, ec);
    if (ec) {
        err = "cannot create " + animDir.string() + ": " + ec.message();
        return false;
    }

    for (size_t i = 0; i < frames.size(); ++i) {
        const std::string path = (animDir / fs::path("frame_" + std::to_string(i) + ".png")).string();
        if (!cv::imwrite(path, frames[i])) {
            err = "failed to write " + path;
            return false;
        }
    }

    // Horizontal spritesheet. Frames already share a size, so no normalisation
    // is needed here — hconcat is exactly right. (spritesheet.cpp's
    // scale-to-fit path is for *unrelated* input images of varying sizes.)
    // 横向精灵图。各帧尺寸已相同，无需归一化 —— hconcat 正好。
    // （spritesheet.cpp 的缩放适配是给尺寸不一的【无关】输入图用的。）
    cv::Mat sheet;
    cv::hconcat(frames, sheet);

    const std::string sheetOut = (animDir / fs::path("spritesheet.png")).string();
    if (!cv::imwrite(sheetOut, sheet)) {
        err = "failed to write " + sheetOut;
        return false;
    }
    if (sheetPath) *sheetPath = sheetOut;
    return true;
}

cv::Mat mirrorFrame(const cv::Mat& frame) {
    cv::Mat out;
    // Exact and free for pixel art: a pure horizontal flip.
    // 对像素图精确且免费：纯水平翻转。
    cv::flip(frame, out, 1);
    return out;
}

}  // namespace fb
