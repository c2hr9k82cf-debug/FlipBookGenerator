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

// Default rotation pivot for a part, given its bounding box in original-image
// coordinates. Each part gets ONE pivot (the joint it swings about):
//   head      -> neck          (bottom center)
//   body      -> shoulder line (top center)
//   arm_left  -> shoulder, torso side  (top right of the arm's bbox)
//   arm_right -> shoulder, torso side  (top left  of the arm's bbox)
//   leg_*     -> hip           (top center)
// 返回部件的默认旋转锚点（原图坐标）。每个部件一个锚点，即它摆动所绕的关节。
// 头=颈部、躯干=肩线、手臂=靠躯干一侧的顶端、腿=胯部。
//
// NOTE: the old 2-anchor format (bbox top/bottom center) is wrong for arms —
// it put the arm's pivot on its own mid-axis instead of at the shoulder.
// 旧的 2 锚点格式（包围盒上下中点）对手臂是错的：它把手臂锚点放在手臂自身
// 中轴上，而不是肩关节处。
cv::Point defaultPivot(int partId, const cv::Rect& bbox);
