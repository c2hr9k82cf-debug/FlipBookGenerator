// compose.h — the customization composer (v0.10.1): recipe + library →
// three template-aligned views. The output is just another three-view input
// source for the existing pipeline (buildFourDirSheet) — core, golden
// invariant and all gates stay untouched.
// 捏人合成器（v0.10.1）：配方 + 部件库 → 三张模板对齐视图。产出只是现有管线
// （buildFourDirSheet）的又一个三视图输入源 —— core、金不变量、全部闸门不动。
#pragma once

#include "core/parts_library.h"
#include "core/template_spec.h"

#include <map>
#include <string>
#include <vector>

namespace fb {

// A customization recipe: one item per slot + optional global role
// recolours. The SINGLE source of truth for "who wears what" (P4) — canvas
// edits go back into the library first, then the recipe re-composes.
// 捏人配方：每槽一件物品 + 可选全局角色换色。"谁穿什么"的唯一真源（P4）——
// 画布改动先写回部件库，再由配方重新合成。
struct ComposeRecipe {
    std::string templateName = "chibi_standard";
    // slot -> item name. A missing slot composites nothing (partial looks
    // are allowed — e.g. "just a hair swap" against a fixed base).
    // 槽位 -> 物品名。缺槽不合成任何东西（允许局部造型 —— 如只换发型）。
    std::map<std::string, std::string> items;
    // Global recolours applied after each item's own roleColors.
    // 全局换色，在各物品自身 roleColors 之后应用。
    std::vector<LibraryItem::RoleColorOverride> roleOverrides;
};

// Load / save recipe.json ({template, items: {slot: name},
// roleOverrides: [{role, src?, dst}]}).
// 读/写 recipe.json（{template, items: {槽位: 名}, roleOverrides: [...] }）。
bool loadComposeRecipe(const std::string& path, ComposeRecipe& out, std::string& err);
bool saveComposeRecipe(const std::string& path, const ComposeRecipe& in, std::string& err);

// Compose the three template-aligned views (index 0 front, 1 side, 2 back).
// Per item: back:"file" uses its back sprite; back:"auto" infers it from the
// item's own front+side via the P2b silhouette rules. Role overrides recolour
// EXACT colours (the chip-reskin path). Z-order wins at overlaps, no blending.
// 合成三张模板对齐视图（下标 0 正、1 侧、2 背）。逐物品：back:"file" 直接用其
// 背面精灵；back:"auto" 用 P2b 剪影规则从该物品自身正+侧推断。角色覆盖精确换色
// （芯片换皮路径）。重叠处 z 序胜出，不混合。
bool composeThreeViews(const ComposeRecipe& recipe, const PartsLibrary& lib,
                       const TemplateSpec& spec, cv::Mat out[3], std::string& err);

}  // namespace fb
