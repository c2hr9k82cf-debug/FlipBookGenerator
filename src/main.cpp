#include <opencv2/opencv.hpp>
#include <filesystem>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <limits>

namespace fs = std::filesystem;

int main() {
    const std::string inputDir = "assets";
    const std::string outputDir = "output";
    const std::string outputFile = outputDir + "/spritesheet.png";

    // Ask for target frame size (default: 32)
    // 询问目标帧尺寸（默认：32）
    std::cout << "Enter target frame size (default: 32): ";
    std::string sizeInput;
    std::getline(std::cin, sizeInput);

    int targetSize = 32;
    if (!sizeInput.empty()) {
        try {
            targetSize = std::stoi(sizeInput);
            if (targetSize <= 0) {
                std::cerr << "Error: Size must be positive." << std::endl;
                return 1;
            }
        } catch (...) {
            std::cerr << "Error: Invalid number." << std::endl;
            return 1;
        }
    }
    std::cout << "Target frame size: " << targetSize << "x" << targetSize << std::endl;

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

    // Normalize each frame: scale to fit within target size, center on transparent canvas
    // 统一每帧：缩放适配目标尺寸，居中放到透明画布上
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

        // Scale to fit within target size while preserving aspect ratio
        // 缩放适配目标尺寸，保持宽高比
        double scale = std::min(
            static_cast<double>(targetSize) / img.cols,
            static_cast<double>(targetSize) / img.rows
        );
        int newW = static_cast<int>(img.cols * scale);
        int newH = static_cast<int>(img.rows * scale);

        cv::Mat scaled;
        cv::resize(img, scaled, cv::Size(newW, newH), 0, 0, cv::INTER_AREA);

        // Create transparent canvas of target size
        // 创建目标尺寸的透明画布
        cv::Mat canvas = cv::Mat::zeros(targetSize, targetSize, CV_8UC4);

        // Calculate offset to center the scaled frame
        // 计算居中偏移量
        int x = (targetSize - newW) / 2;
        int y = (targetSize - newH) / 2;

        // Paste scaled frame onto canvas
        // 将缩放后的帧粘贴到画布上
        scaled.copyTo(canvas(cv::Rect(x, y, newW, newH)));

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
