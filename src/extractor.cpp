// extractor.cpp
// Body part extraction via grid-based editor
// 格子编辑器身体部件提取

#include "extractor.h"
#include "utils.h"
#include <opencv2/opencv.hpp>
#include <filesystem>
#include <iostream>
#include <vector>
#include <string>

namespace fs = std::filesystem;

// Global state for mouse callback / 鼠标回调全局状态
struct EditorState {
    cv::Mat partMap;       // each pixel stores part ID (0=unassigned) / 每像素存储部件 ID
    int currentPart = 1;   // currently selected part / 当前选中的部件
    int zoomFactor = 8;    // display zoom / 显示缩放
    bool needsRedraw = true;
    cv::Mat originalImage; // original loaded image / 原始加载图片
};

// Mouse callback: paint cells with current part / 鼠标回调：用当前部件涂色
static void onMouse(int event, int x, int y, int flags, void* userdata) {
    EditorState* state = static_cast<EditorState*>(userdata);

    // Convert display coordinates to image coordinates
    // 将显示坐标转换为图片坐标
    int imgX = x / state->zoomFactor;
    int imgY = y / state->zoomFactor;

    if (imgX < 0 || imgX >= state->partMap.cols ||
        imgY < 0 || imgY >= state->partMap.rows) {
        return;
    }

    bool paint = false;
    int partId = state->currentPart;

    if (event == cv::EVENT_LBUTTONDOWN || event == cv::EVENT_MOUSEMOVE) {
        // Left click or drag: assign current part / 左键或拖拽：分配当前部件
        if (event == cv::EVENT_LBUTTONDOWN || (flags & cv::EVENT_FLAG_LBUTTON)) {
            paint = true;
            partId = state->currentPart;
        }
    } else if (event == cv::EVENT_RBUTTONDOWN || event == cv::EVENT_MOUSEMOVE) {
        // Right click or drag: unassign / 右键或拖拽：取消分配
        if (event == cv::EVENT_RBUTTONDOWN || (flags & cv::EVENT_FLAG_RBUTTON)) {
            paint = true;
            partId = 0;
        }
    }

    if (paint) {
        state->partMap.at<uchar>(imgY, imgX) = static_cast<uchar>(partId);
        state->needsRedraw = true;
    }
}

// Build display image: original + semi-transparent part overlay + grid
// 构建显示图片：原始图 + 半透明部件覆盖 + 网格
static cv::Mat buildDisplayImage(const EditorState& state) {
    int w = state.originalImage.cols;
    int h = state.originalImage.rows;
    int z = state.zoomFactor;

    // Scale up original image (nearest neighbor for pixel art)
    // 放大原始图片（最近邻插值保持像素风）
    cv::Mat scaled;
    cv::resize(state.originalImage, scaled, cv::Size(w * z, h * z), 0, 0, cv::INTER_NEAREST);

    // Convert to BGR for display
    // 转换为 BGR 以显示
    cv::Mat display;
    if (scaled.channels() == 4) {
        cv::cvtColor(scaled, display, cv::COLOR_BGRA2BGR);
    } else if (scaled.channels() == 1) {
        cv::cvtColor(scaled, display, cv::COLOR_GRAY2BGR);
    } else {
        display = scaled.clone();
    }

    // Draw semi-transparent part overlay
    // 绘制半透明部件覆盖
    cv::Mat overlay = display.clone();
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int partId = state.partMap.at<uchar>(y, x);
            if (partId > 0 && partId <= static_cast<int>(PARTS.size())) {
                cv::Scalar color = PARTS[partId - 1].color;
                // Fill the zoomed cell with part color
                // 用部件颜色填充缩放后的格子
                cv::Rect cell(x * z, y * z, z, z);
                cv::rectangle(overlay, cell, color, cv::FILLED);
            }
        }
    }

    // Blend overlay with original (alpha = 0.4)
    // 混合覆盖层与原图（透明度 0.4）
    cv::addWeighted(overlay, 0.4, display, 0.6, 0, display);

    // Draw grid lines
    // 绘制网格线
    cv::Scalar gridColor(128, 128, 128);
    for (int x = 0; x <= w; x++) {
        cv::line(display, cv::Point(x * z, 0), cv::Point(x * z, h * z), gridColor, 1);
    }
    for (int y = 0; y <= h; y++) {
        cv::line(display, cv::Point(0, y * z), cv::Point(w * z, y * z), gridColor, 1);
    }

    return display;
}

int runExtractPartsMode() {
    // Ask for image path
    // 询问图片路径
    std::cout << "Enter image path: ";
    std::string imagePath;
    std::getline(std::cin, imagePath);

    // Load image
    // 加载图片
    cv::Mat img = cv::imread(imagePath, cv::IMREAD_UNCHANGED);
    if (img.empty()) {
        std::cerr << "Error: Failed to load image: " << imagePath << std::endl;
        return 1;
    }

    // Convert to BGRA if needed
    // 如非 BGRA 则转换
    if (img.channels() == 3) {
        cv::cvtColor(img, img, cv::COLOR_BGR2BGRA);
    } else if (img.channels() == 1) {
        cv::cvtColor(img, img, cv::COLOR_GRAY2BGRA);
    }

    std::cout << "Image loaded: " << img.cols << "x" << img.rows << std::endl;

    // Initialize editor state
    // 初始化编辑器状态
    EditorState state;
    state.originalImage = img;
    state.partMap = cv::Mat::zeros(img.rows, img.cols, CV_8UC1);

    // Print controls
    // 打印操作说明
    std::cout << "\nEditor Controls / 编辑器控制:" << std::endl;
    std::cout << "  1-6     - Select part / 选择部件" << std::endl;
    for (const auto& p : PARTS) {
        std::cout << "           " << p.id << " = " << p.name << std::endl;
    }
    std::cout << "  Left    - Paint cell / 左键涂色" << std::endl;
    std::cout << "  Right   - Erase cell / 右键擦除" << std::endl;
    std::cout << "  Enter   - Confirm & extract / 确认并提取" << std::endl;
    std::cout << "  ESC     - Cancel / 取消" << std::endl;
    std::cout << "\nCurrent part: " << getPartName(state.currentPart) << std::endl;

    // Create window and set mouse callback
    // 创建窗口并设置鼠标回调
    const std::string windowName = "Body Part Editor";
    cv::namedWindow(windowName, cv::WINDOW_AUTOSIZE);
    cv::setMouseCallback(windowName, onMouse, &state);

    // Editor loop
    // 编辑器循环
    while (true) {
        if (state.needsRedraw) {
            cv::Mat display = buildDisplayImage(state);
            cv::imshow(windowName, display);
            state.needsRedraw = false;
        }

        int key = cv::waitKey(16) & 0xFF;  // ~60 FPS

        if (key == 27) {
            // ESC to cancel / ESC 取消
            std::cout << "Cancelled / 已取消" << std::endl;
            cv::destroyAllWindows();
            return 0;
        } else if (key == 13 || key == 10) {
            // Enter to confirm / 回车确认
            break;
        } else if (key >= '1' && key <= '6') {
            // Number key to select part / 数字键选择部件
            state.currentPart = key - '0';
            state.needsRedraw = true;
            std::cout << "Selected part: " << getPartName(state.currentPart) << std::endl;
        }
    }

    cv::destroyAllWindows();

    // Check if any parts were assigned
    // 检查是否有部件被分配
    double minVal, maxVal;
    cv::minMaxLoc(state.partMap, &minVal, &maxVal);
    if (maxVal == 0) {
        std::cerr << "Error: No parts assigned." << std::endl;
        return 1;
    }

    // Extract and save each part
    // 提取并保存每个部件
    const std::string outputDir = "output/parts";
    fs::create_directories(outputDir);

    for (const auto& part : PARTS) {
        // Create mask for this part
        // 创建此部件的掩膜
        cv::Mat mask = (state.partMap == part.id);

        // Check if this part has any pixels
        // 检查此部件是否有像素
        if (cv::countNonZero(mask) == 0) {
            std::cout << "  Skipped: " << part.name << " (no pixels)" << std::endl;
            continue;
        }

        // Extract part with transparent background
        // 提取部件（透明背景）
        cv::Mat result = cv::Mat::zeros(img.rows, img.cols, CV_8UC4);
        img.copyTo(result, mask);

        // Crop to bounding box of the part
        // 裁切到部件的边界框
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        cv::Rect bbox = cv::boundingRect(contours[0]);
        for (size_t i = 1; i < contours.size(); i++) {
            bbox |= cv::boundingRect(contours[i]);
        }

        cv::Mat cropped = result(bbox).clone();

        // Save
        // 保存
        std::string outputFile = outputDir + "/" + part.name + ".png";
        cv::imwrite(outputFile, cropped);
        std::cout << "  Saved: " << outputFile
                  << " (" << cropped.cols << "x" << cropped.rows << ")" << std::endl;
    }

    std::cout << "\nParts extracted to: " << outputDir << std::endl;
    return 0;
}
