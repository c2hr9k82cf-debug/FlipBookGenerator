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

// Default rotation pivot for a part, given its bounding box in original-image
// coordinates. See utils.h for the per-part joint rationale.
// 返回部件默认旋转锚点（原图坐标）。各部件关节说明见 utils.h。
cv::Point defaultPivot(int partId, const cv::Rect& bbox) {
    const int centerX = bbox.x + (bbox.width  - 1) / 2;
    const int topY    = bbox.y;
    const int bottomY = bbox.y + bbox.height - 1;

    switch (partId) {
        case 1: return cv::Point(centerX, bottomY);       // head: neck / 头：颈部
        case 2: return cv::Point(centerX, topY);          // body: shoulders / 躯干：肩线
        case 3: return cv::Point(bbox.x + bbox.width - 1, topY);  // arm_left: shoulder on torso side
        case 4: return cv::Point(bbox.x, topY);                   // arm_right: shoulder on torso side
        case 5:                                                  // leg_left: hip / 腿：胯
        case 6: return cv::Point(centerX, topY);                 // leg_right: hip / 腿：胯
        default: return cv::Point(centerX, topY + bbox.height / 2);
    }
}
