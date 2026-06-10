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
    int currentPart = 1;   // currently selected part (1-6) / 当前选中部件 (1-6)
    int mode = 0;          // 0=paint, 1=eraser, 2=overview / 0=涂色, 1=橡皮, 2=总览
    int eraserTarget = 1;  // which part eraser can erase / 橡皮可擦除的部件
    int zoomFactor = 8;    // display zoom / 显示缩放
    bool needsRedraw = true;
    cv::Mat originalImage; // original loaded image / 原始加载图片
};

// Mouse callback: paint/erase cells with left click / 鼠标回调：左键涂色/擦除
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

    // Left click or drag: paint or erase
    // 左键或拖拽：涂色或擦除
    if (event == cv::EVENT_LBUTTONDOWN ||
        (event == cv::EVENT_MOUSEMOVE && (flags & cv::EVENT_FLAG_LBUTTON))) {
        if (state->mode == 1) {
            // Eraser mode: only erase the target part / 橡皮模式：仅擦除目标部件
            uchar cellPart = state->partMap.at<uchar>(imgY, imgX);
            if (cellPart == static_cast<uchar>(state->eraserTarget)) {
                state->partMap.at<uchar>(imgY, imgX) = 0;
                state->needsRedraw = true;
            }
        } else {
            // Paint mode: assign current part / 涂色模式：分配当前部件
            state->partMap.at<uchar>(imgY, imgX) = static_cast<uchar>(state->currentPart);
            state->needsRedraw = true;
        }
    }
}

// Build display image: original image + light overlay + grid
// 构建显示图片：原始图 + 浅色覆盖 + 网格
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

    // Light gray background behind transparent areas
    // 透明区域显示浅灰背景
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            cv::Vec4b pixel = state.originalImage.at<cv::Vec4b>(y, x);
            if (pixel[3] == 0) {
                cv::Rect cell(x * z, y * z, z, z);
                cv::rectangle(display, cell, cv::Scalar(230, 230, 230), cv::FILLED);
            }
        }
    }

    // Draw overlay for assigned cells
    // 绘制已分配格子的覆盖层
    cv::Mat overlay = display.clone();
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int partId = state.partMap.at<uchar>(y, x);
            if (partId > 0 && partId <= static_cast<int>(PARTS.size())) {
                cv::Rect cell(x * z, y * z, z, z);
                if (state.mode == 0 && partId == state.currentPart) {
                    // Paint mode + current part: highlight blue
                    // 涂色模式 + 当前部件：蓝色高亮
                    cv::rectangle(overlay, cell, cv::Scalar(100, 150, 255), cv::FILLED);
                } else if (state.mode == 1 && partId == state.eraserTarget) {
                    // Eraser mode + target part: highlight red
                    // 橡皮模式 + 目标部件：红色高亮
                    cv::rectangle(overlay, cell, cv::Scalar(100, 100, 255), cv::FILLED);
                } else {
                    // Other parts: light blue-gray
                    // 其他部件：浅蓝灰色
                    cv::rectangle(overlay, cell, cv::Scalar(200, 210, 220), cv::FILLED);
                }
            }
        }
    }

    // Blend overlay with original (alpha = 0.3)
    // 混合覆盖层与原图（透明度 0.3）
    cv::addWeighted(overlay, 0.3, display, 0.7, 0, display);

    // Draw grid lines and highlight markers
    // 绘制网格线和高亮标记
    cv::Scalar gridColor(200, 200, 200);  // default grid color / 默认网格颜色

    // Draw vertical grid lines
    // 绘制垂直网格线
    for (int x = 0; x <= w; x++) {
        cv::line(display, cv::Point(x * z, 0), cv::Point(x * z, h * z), gridColor, 1);
    }
    // Draw horizontal grid lines
    // 绘制水平网格线
    for (int y = 0; y <= h; y++) {
        cv::line(display, cv::Point(0, y * z), cv::Point(w * z, y * z), gridColor, 1);
    }

    // Draw colored border + cross for selected cells
    // 绘制选中格子的彩色边框 + 十字
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int partId = state.partMap.at<uchar>(y, x);
            bool isSelected = false;
            cv::Scalar highlightColor;

            if (state.mode == 0 && partId == state.currentPart && partId > 0) {
                // Paint mode + current part / 涂色模式 + 当前部件
                isSelected = true;
                highlightColor = cv::Scalar(255, 100, 0);  // bright blue / 亮蓝色
            } else if (state.mode == 1 && partId == state.eraserTarget && partId > 0) {
                // Eraser mode + target part / 橡皮模式 + 目标部件
                isSelected = true;
                highlightColor = cv::Scalar(0, 0, 255);  // bright red / 亮红色
            }

            if (isSelected) {
                cv::Rect cell(x * z, y * z, z, z);

                // Draw thin colored border / 绘制细彩色边框
                cv::rectangle(display, cell, highlightColor, 1);

                // Draw cross in center / 绘制中心十字
                int cx = x * z + z / 2;
                int cy = y * z + z / 2;
                int crossLen = z / 4;
                cv::line(display, cv::Point(cx - crossLen, cy), cv::Point(cx + crossLen, cy),
                         highlightColor, 1);
                cv::line(display, cv::Point(cx, cy - crossLen), cv::Point(cx, cy + crossLen),
                         highlightColor, 1);
            }
        }
    }

    return display;
}

// Build overview image: original + colored borders for all parts
// 构建总览图片：原始图 + 所有部件的彩色边框
static cv::Mat buildOverviewImage(const EditorState& state) {
    int w = state.originalImage.cols;
    int h = state.originalImage.rows;
    int z = state.zoomFactor;

    // Scale up original image
    // 放大原始图片
    cv::Mat scaled;
    cv::resize(state.originalImage, scaled, cv::Size(w * z, h * z), 0, 0, cv::INTER_NEAREST);

    // Convert to BGR
    // 转换为 BGR
    cv::Mat display;
    if (scaled.channels() == 4) {
        cv::cvtColor(scaled, display, cv::COLOR_BGRA2BGR);
    } else if (scaled.channels() == 1) {
        cv::cvtColor(scaled, display, cv::COLOR_GRAY2BGR);
    } else {
        display = scaled.clone();
    }

    // Light gray background behind transparent areas
    // 透明区域显示浅灰背景
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            cv::Vec4b pixel = state.originalImage.at<cv::Vec4b>(y, x);
            if (pixel[3] == 0) {
                cv::Rect cell(x * z, y * z, z, z);
                cv::rectangle(display, cell, cv::Scalar(230, 230, 230), cv::FILLED);
            }
        }
    }

    // For each part, find boundary pixels and draw colored border
    // 对每个部件，找到边界像素并绘制彩色边框
    for (const auto& part : PARTS) {
        // Create mask for this part
        // 创建此部件的掩膜
        cv::Mat mask = (state.partMap == part.id);
        if (cv::countNonZero(mask) == 0) continue;

        // Find boundary: pixel belongs to part but has a neighbor that doesn't
        // 找边界：属于此部件但有邻居不属于
        cv::Scalar color = part.color;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (mask.at<uchar>(y, x) == 0) continue;

                bool isBoundary = false;
                // Check 4 neighbors / 检查4个邻居
                if (x == 0 || state.partMap.at<uchar>(y, x - 1) != part.id) isBoundary = true;
                if (x == w - 1 || state.partMap.at<uchar>(y, x + 1) != part.id) isBoundary = true;
                if (y == 0 || state.partMap.at<uchar>(y - 1, x) != part.id) isBoundary = true;
                if (y == h - 1 || state.partMap.at<uchar>(y + 1, x) != part.id) isBoundary = true;

                if (isBoundary) {
                    // Draw colored border pixel / 绘制彩色边界像素
                    cv::Rect cell(x * z, y * z, z, z);
                    cv::rectangle(display, cell, color, 1);
                }
            }
        }
    }

    // Draw grid lines (very light)
    // 绘制网格线（非常浅）
    cv::Scalar gridColor(220, 220, 220);
    for (int x = 0; x <= w; x++) {
        cv::line(display, cv::Point(x * z, 0), cv::Point(x * z, h * z), gridColor, 1);
    }
    for (int y = 0; y <= h; y++) {
        cv::line(display, cv::Point(0, y * z), cv::Point(w * z, y * z), gridColor, 1);
    }

    return display;
}

// Auto detect body parts using k-means color clustering
// 使用 k-means 颜色聚类自动检测身体部件
cv::Mat autoDetectParts(const cv::Mat& img, int k) {
    int w = img.cols;
    int h = img.rows;

    // Collect non-transparent pixels as samples
    // 收集非透明像素作为样本
    std::vector<cv::Vec4f> samples;
    std::vector<cv::Point> positions;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            cv::Vec4b pixel = img.at<cv::Vec4b>(y, x);
            if (pixel[3] > 0) {  // non-transparent / 非透明
                samples.push_back(cv::Vec4f(pixel[0], pixel[1], pixel[2], pixel[3]));
                positions.push_back(cv::Point(x, y));
            }
        }
    }

    if (samples.empty()) {
        std::cerr << "Error: Image is completely transparent." << std::endl;
        return cv::Mat::zeros(h, w, CV_8UC1);
    }

    // Run k-means clustering
    // 运行 k-means 聚类
    cv::Mat sampleMat(static_cast<int>(samples.size()), 4, CV_32F);
    for (size_t i = 0; i < samples.size(); i++) {
        float* row = sampleMat.ptr<float>(static_cast<int>(i));
        row[0] = samples[i][0];
        row[1] = samples[i][1];
        row[2] = samples[i][2];
        row[3] = samples[i][3];
    }

    cv::Mat labels, centers;
    cv::kmeans(sampleMat, k, labels,
               cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 100, 1.0),
               3, cv::KMEANS_PP_CENTERS, centers);

    // Calculate average Y position for each cluster
    // 计算每个聚类的平均 Y 坐标
    std::vector<double> avgY(k, 0.0);
    std::vector<int> count(k, 0);
    for (int i = 0; i < labels.rows; i++) {
        int clusterId = labels.at<int>(i);
        avgY[clusterId] += positions[i].y;
        count[clusterId]++;
    }
    for (int i = 0; i < k; i++) {
        if (count[i] > 0) avgY[i] /= count[i];
    }

    // Sort clusters by average Y (top to bottom)
    // 按平均 Y 坐标排序聚类（从上到下）
    std::vector<int> clusterOrder(k);
    for (int i = 0; i < k; i++) clusterOrder[i] = i;
    std::sort(clusterOrder.begin(), clusterOrder.end(),
              [&avgY](int a, int b) { return avgY[a] < avgY[b]; });

    // Map cluster IDs to part IDs (top=head, bottom=legs)
    // 将聚类 ID 映射为部件 ID（顶部=头，底部=腿）
    // Part order: 1=head, 2=body, 3=arm_left, 4=arm_right, 5=leg_left, 6=leg_right
    std::vector<int> clusterToPart(k);
    for (int i = 0; i < k; i++) {
        clusterToPart[clusterOrder[i]] = i + 1;  // map to part 1-6
    }

    // Build partMap
    // 构建部件映射
    cv::Mat partMap = cv::Mat::zeros(h, w, CV_8UC1);
    for (size_t i = 0; i < positions.size(); i++) {
        int clusterId = labels.at<int>(static_cast<int>(i));
        int partId = clusterToPart[clusterId];
        partMap.at<uchar>(positions[i]) = static_cast<uchar>(partId);
    }

    std::cout << "Auto detection complete / 自动检测完成:" << std::endl;
    for (int i = 0; i < k; i++) {
        int partId = clusterToPart[i];
        std::cout << "  Cluster " << i << " -> " << getPartName(partId)
                  << " (avg Y: " << static_cast<int>(avgY[i])
                  << ", pixels: " << count[i] << ")" << std::endl;
    }

    return partMap;
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

    // Ask for detection mode
    // 询问检测模式
    std::cout << "\nDetection mode / 检测模式:" << std::endl;
    std::cout << "  1. Manual (fully manual painting) / 完全手动" << std::endl;
    std::cout << "  2. Auto detect (k-means clustering) / 自动检测" << std::endl;
    std::cout << "  3. Auto + refine (auto detect, then manual edit) / 自动 + 人工微调" << std::endl;
    std::cout << "Enter choice (1/2/3, default: 3): ";
    std::string modeInput;
    std::getline(std::cin, modeInput);

    int mode = 3;  // default: auto + refine
    if (!modeInput.empty()) {
        try {
            mode = std::stoi(modeInput);
            if (mode < 1 || mode > 3) {
                std::cerr << "Error: Invalid choice. Using default (3)." << std::endl;
                mode = 3;
            }
        } catch (...) {
            std::cerr << "Error: Invalid input. Using default (3)." << std::endl;
            mode = 3;
        }
    }

    // Initialize editor state
    // 初始化编辑器状态
    EditorState state;
    state.originalImage = img;
    state.partMap = cv::Mat::zeros(img.rows, img.cols, CV_8UC1);

    // Auto detect if mode 2 or 3
    // 模式 2 或 3 时进行自动检测
    if (mode == 2 || mode == 3) {
        std::cout << "\nRunning auto detection... / 正在自动检测..." << std::endl;
        state.partMap = autoDetectParts(img);
        state.needsRedraw = true;
    }

    // Mode 1 and 3: open editor for manual painting/refinement
    // 模式 1 和 3：打开编辑器进行手动涂色/微调
    if (mode == 1 || mode == 3) {
        // Print controls
        // 打印操作说明
        std::cout << "\nEditor Controls / 编辑器控制:" << std::endl;
        std::cout << "  1-6     - Select part & paint mode / 选择部件 & 涂色模式" << std::endl;
        for (const auto& p : PARTS) {
            std::cout << "           " << p.id << " = " << p.name << std::endl;
        }
        std::cout << "  7       - Eraser (only erases selected part) / 橡皮（仅擦除选中部件）" << std::endl;
        std::cout << "  9       - Overview (show all parts) / 总览（显示所有部件）" << std::endl;
        std::cout << "  Left    - Paint or erase (drag supported) / 左键涂色或擦除（支持拖拽）" << std::endl;
        std::cout << "  Enter   - Confirm & extract / 确认并提取" << std::endl;
        std::cout << "  ESC     - Cancel / 取消" << std::endl;
        if (mode == 3) {
            std::cout << "\n  [Auto detected parts loaded. Refine as needed. / 已加载自动检测结果，可微调。]" << std::endl;
        }
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
                cv::Mat display;
                if (state.mode == 2) {
                    // Overview mode: show all part borders / 总览模式：显示所有部件边框
                    display = buildOverviewImage(state);
                } else {
                    // Normal mode: show current part highlight / 普通模式：显示当前部件高亮
                    display = buildDisplayImage(state);
                }
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
                state.mode = 0;  // paint mode / 涂色模式
                state.needsRedraw = true;
                std::cout << "Selected part: " << getPartName(state.currentPart) << std::endl;
            } else if (key == '7') {
                // Eraser: set to eraser mode, target = current part
                // 橡皮：切换到橡皮模式，目标 = 当前部件
                state.mode = 1;
                state.eraserTarget = state.currentPart;
                state.needsRedraw = true;
                std::cout << "Eraser mode (erase: " << getPartName(state.eraserTarget)
                          << ") / 橡皮模式（擦除: " << getPartName(state.eraserTarget) << "）" << std::endl;
            } else if (key == '9') {
                // Overview: toggle overview mode / 总览：切换总览模式
                if (state.mode == 2) {
                    state.mode = 0;  // back to paint / 返回涂色模式
                    std::cout << "Exit overview / 退出总览" << std::endl;
                } else {
                    state.mode = 2;
                    std::cout << "Overview mode (press 9 to exit) / 总览模式（按 9 退出）" << std::endl;
                }
                state.needsRedraw = true;
            }
        }

        cv::destroyAllWindows();
    } else {
        // Mode 2: auto detect only
        // 模式 2：仅自动检测
        std::cout << "Auto detection done. / 自动检测完成。" << std::endl;
    }

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
