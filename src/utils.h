// utils.h
// Common definitions and helper functions
// 公共定义和辅助函数

#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

// Part definition / 部件定义
struct PartDef {
    int id;
    std::string name;
    cv::Scalar color;  // highlight color for display / 高亮显示颜色
};

// All body parts / 所有身体部件
extern const std::vector<PartDef> PARTS;

// Get part name by ID / 根据 ID 获取部件名
std::string getPartName(int id);
