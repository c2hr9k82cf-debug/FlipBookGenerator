// extractor.h
// Body part extraction via grid-based editor
// 格子编辑器身体部件提取

#pragma once

#include <opencv2/opencv.hpp>

// Auto detect body parts using k-means color clustering
// 使用 k-means 颜色聚类自动检测身体部件
cv::Mat autoDetectParts(const cv::Mat& img, int k = 6);

// Run body part extraction mode
// 运行身体部件提取模式
int runExtractPartsMode();
