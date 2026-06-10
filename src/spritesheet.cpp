// spritesheet.cpp
// Sprite sheet generation and animation preview
// Sprite Sheet 生成与动画预览

#include "spritesheet.h"
#include <opencv2/opencv.hpp>
#include <filesystem>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

namespace fs = std::filesystem;

int runSpriteSheetMode() {
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

    // --- Animation Preview / 动画预览 ---

    // Ask if user wants to preview
    // 询问是否预览
    std::cout << "Preview animation? (y/n, default: y): ";
    std::string previewInput;
    std::getline(std::cin, previewInput);

    if (previewInput == "n" || previewInput == "N") {
        std::cout << "Done." << std::endl;
        return 0;
    }

    // Split sprite sheet into individual frames
    // 将 Sprite Sheet 拆分为单独的帧
    std::vector<cv::Mat> frames;
    int numFrames = spritesheet.cols / targetSize;
    for (int i = 0; i < numFrames; i++) {
        cv::Rect roi(i * targetSize, 0, targetSize, spritesheet.rows);
        frames.push_back(spritesheet(roi).clone());
    }

    // Scale up frames for better visibility
    // 放大帧以便观看
    const int zoomFactor = 8;
    const int displaySize = targetSize * zoomFactor;

    std::vector<cv::Mat> displayFrames;
    for (auto& frame : frames) {
        cv::Mat displayFrame;
        cv::resize(frame, displayFrame, cv::Size(displaySize, displaySize), 0, 0, cv::INTER_NEAREST);

        // Convert BGRA to BGR for display (imshow doesn't handle alpha well)
        // 转换 BGRA 为 BGR 以正确显示（imshow 不支持 alpha 通道）
        cv::Mat bgrFrame;
        cv::cvtColor(displayFrame, bgrFrame, cv::COLOR_BGRA2BGR);
        displayFrames.push_back(bgrFrame);
    }

    // Preview settings
    // 预览设置
    const std::string windowName = "FlipBook Preview";
    int fps = 8;
    int delay = 1000 / fps;
    bool paused = false;
    size_t currentFrame = 0;

    cv::namedWindow(windowName, cv::WINDOW_AUTOSIZE);

    // Helper: print status to console
    // 辅助函数：在控制台打印状态
    auto printStatus = [&]() {
        std::string status = paused ? "PAUSED / 已暂停" : "PLAYING / 播放中";
        std::cout << "\r[" << status << "] Frame " << currentFrame + 1
                  << "/" << numFrames << " | FPS: " << fps << std::flush;
    };

    std::cout << "\nPreview Controls / 预览控制:" << std::endl;
    std::cout << "  Space   - Pause/Resume / 暂停/继续" << std::endl;
    std::cout << "  A / D   - Prev/Next frame / 上一帧/下一帧" << std::endl;
    std::cout << "  W / S   - Speed up/Down / 加速/减速" << std::endl;
    std::cout << "  Q / ESC - Quit / 退出" << std::endl;
    std::cout << std::endl;

    // Animation loop
    // 动画循环
    while (true) {
        // Update console status
        // 更新控制台状态
        printStatus();

        // Show current frame
        // 显示当前帧
        cv::imshow(windowName, displayFrames[currentFrame]);

        // Wait for key input
        // 等待按键输入
        int key = cv::waitKey(paused ? 0 : delay) & 0xFF;

        if (key == 'q' || key == 'Q' || key == 27) {
            break;
        } else if (key == ' ') {
            paused = !paused;
            std::cout << (paused ? "Paused / 已暂停" : "Playing / 播放中") << std::endl;
        } else if (key == 'a' || key == 'A') {
            if (paused) {
                currentFrame = (currentFrame - 1 + numFrames) % numFrames;
            } else {
                paused = true;
                currentFrame = (currentFrame - 1 + numFrames) % numFrames;
            }
            std::cout << "Frame: " << currentFrame + 1 << "/" << numFrames
                      << " | FPS: " << fps << std::endl;
        } else if (key == 'd' || key == 'D') {
            if (paused) {
                currentFrame = (currentFrame + 1) % numFrames;
            } else {
                paused = true;
                currentFrame = (currentFrame + 1) % numFrames;
            }
            std::cout << "Frame: " << currentFrame + 1 << "/" << numFrames
                      << " | FPS: " << fps << std::endl;
        } else if (key == 'w' || key == 'W') {
            fps = std::min(fps + 2, 60);
            delay = 1000 / fps;
            std::cout << "Speed up / 加速 -> FPS: " << fps << std::endl;
        } else if (key == 's' || key == 'S') {
            fps = std::max(fps - 2, 1);
            delay = 1000 / fps;
            std::cout << "Speed down / 减速 -> FPS: " << fps << std::endl;
        }

        if (!paused) {
            currentFrame = (currentFrame + 1) % numFrames;
        }
    }

    cv::destroyAllWindows();
    std::cout << "Preview closed. Done." << std::endl;

    return 0;
}
