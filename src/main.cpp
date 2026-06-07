// FlipBookGenerator V0.2
// Auto-normalize frame sizes with transparent background, then concatenate to sprite sheet
// 自动统一帧尺寸（透明背景），拼接为 Sprite Sheet

#include <opencv2/opencv.hpp>
#include <filesystem>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

namespace fs = std::filesystem;

int main() {
    const std::string inputDir = "assets";
    const std::string outputDir = "output";
    const std::string outputFile = outputDir + "/spritesheet.png";

    // Check input directory
    // 检查输入目录
    if (!fs::exists(inputDir) || !fs::is_directory(inputDir)) {
        std::cerr << "Error: " << inputDir << " directory not found." << std::endl;
        return 1;
    }

    // Collect PNG files
    // 收集 PNG 文件
    std::vector<std::string> pngFiles;
    for (const auto& entry : fs::directory_iterator(inputDir)) {
        if (entry.path().extension() == ".png") {
            pngFiles.push_back(entry.path().string());
        }
    }

    if (pngFiles.empty()) {
        std::cerr << "Error: No PNG files found in " << inputDir << std::endl;
        return 1;
    }

    // Sort by filename
    // 按文件名排序
    std::sort(pngFiles.begin(), pngFiles.end());

    std::cout << "Found " << pngFiles.size() << " PNG files." << std::endl;

    // Load images
    // 加载图片
    std::vector<cv::Mat> images;
    for (const auto& file : pngFiles) {
        cv::Mat img = cv::imread(file, cv::IMREAD_UNCHANGED);
        if (img.empty()) {
            std::cerr << "Warning: Failed to load " << file << ", skipping." << std::endl;
            continue;
        }
        std::cout << "  Loaded: " << file
                  << " (" << img.cols << "x" << img.rows << ")" << std::endl;
        images.push_back(img);
    }

    if (images.empty()) {
        std::cerr << "Error: No images loaded successfully." << std::endl;
        return 1;
    }

    // Find max dimensions across all frames
    // 找出所有帧中的最大宽高
    int maxW = 0, maxH = 0;
    for (const auto& img : images) {
        maxW = std::max(maxW, img.cols);
        maxH = std::max(maxH, img.rows);
    }
    std::cout << "Normalized size: " << maxW << "x" << maxH
              << " (transparent background)" << std::endl;

    // Normalize each frame: center-paste onto a transparent canvas
    // 统一每帧：居中粘贴到透明画布上
    std::vector<cv::Mat> normalized;
    for (size_t i = 0; i < images.size(); i++) {
        cv::Mat img = images[i];

        // Convert to BGRA if needed (ensure 4 channels for transparency)
        // 如非 BGRA 则转换（确保 4 通道以支持透明）
        if (img.channels() == 3) {
            cv::cvtColor(img, img, cv::COLOR_BGR2BGRA);
        } else if (img.channels() == 1) {
            cv::cvtColor(img, img, cv::COLOR_GRAY2BGRA);
        }

        // Create transparent canvas of max size
        // 创建最大尺寸的透明画布
        cv::Mat canvas = cv::Mat::zeros(maxH, maxW, CV_8UC4);

        // Calculate offset to center the frame
        // 计算居中偏移量
        int x = (maxW - img.cols) / 2;
        int y = (maxH - img.rows) / 2;

        // Paste frame onto canvas
        // 将帧粘贴到画布上
        img.copyTo(canvas(cv::Rect(x, y, img.cols, img.rows)));

        normalized.push_back(canvas);
    }

    // Concatenate horizontally
    // 横向拼接
    cv::Mat spritesheet;
    cv::hconcat(normalized, spritesheet);

    // Create output directory and save
    // 创建输出目录并保存
    fs::create_directories(outputDir);
    cv::imwrite(outputFile, spritesheet);

    std::cout << "Spritesheet saved: " << outputFile
              << " (" << spritesheet.cols << "x" << spritesheet.rows << ")" << std::endl;

    return 0;
}
