// utils.cpp
// Common definitions and helper functions
// 公共定义和辅助函数

#include "utils.h"

// Part definitions with highlight colors
// 部件定义及高亮颜色
const std::vector<PartDef> PARTS = {
    {1, "head",       cv::Scalar(0, 255, 0)},      // green / 绿色
    {2, "body",       cv::Scalar(255, 0, 0)},      // blue / 蓝色
    {3, "arm_left",   cv::Scalar(0, 0, 255)},      // red / 红色
    {4, "arm_right",  cv::Scalar(255, 255, 0)},    // cyan / 青色
    {5, "leg_left",   cv::Scalar(255, 0, 255)},    // magenta / 品红
    {6, "leg_right",  cv::Scalar(0, 255, 255)},    // yellow / 黄色
};

// Get part name by ID / 根据 ID 获取部件名
std::string getPartName(int id) {
    for (const auto& p : PARTS) {
        if (p.id == id) return p.name;
    }
    return "unknown";
}
