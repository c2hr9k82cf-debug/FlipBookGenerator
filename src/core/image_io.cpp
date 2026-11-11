// image_io.cpp — BGRA image loading/saving, the 3->4 channel rule in one place
// BGRA 图像读写，3→4 通道规则收拢一处

#include "core.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace fb {

bool loadImageBgra(const std::string& path, cv::Mat& out) {
    out = cv::imread(path, cv::IMREAD_UNCHANGED);
    if (out.empty()) return false;
    if (out.channels() == 3) {
        cv::cvtColor(out, out, cv::COLOR_BGR2BGRA);
    } else if (out.channels() != 4) {
        out = cv::Mat();
        return false;
    }
    return true;
}

bool saveImageBgra(const std::string& path, const cv::Mat& img, std::string& err) {
    if (img.empty() || img.type() != CV_8UC4) {
        err = "saveImageBgra: image must be non-empty BGRA (CV_8UC4)";
        return false;
    }
    if (!cv::imwrite(path, img)) {
        err = "saveImageBgra: failed to write " + path;
        return false;
    }
    return true;
}

}  // namespace fb
