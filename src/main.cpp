// FlipBookGenerator
// Main entry point with mode selection
// 主程序入口 + 模式选择
//
// NOTE: the mode menu below is a TEMPORARY interface. It is being replaced by a
// guided wizard GUI (Dear ImGui) with a double-clickable .app. Do not add new
// features to this menu — add them to src/ui/ instead. See CLAUDE.md.
// 注意：下面的模式菜单是临时界面，正被 Dear ImGui 引导式向导 + 可双击 .app 取代。
// 不要往这个菜单加新功能，请加到 src/ui/。见 CLAUDE.md。

#include "version.h"
#include "spritesheet.h"
#include "extractor.h"
#include <iostream>
#include <string>

int main() {
    std::cout << "=== FlipBookGenerator V" << FLIPBOOK_VERSION << " ===" << std::endl;
    std::cout << std::endl;
    std::cout << "Choose mode / 选择模式:" << std::endl;
    std::cout << "  1. Sprite Sheet (generate sprite sheet) / 生成 Sprite Sheet" << std::endl;
    std::cout << "  2. Extract Parts (extract body parts) / 提取身体部件" << std::endl;
    std::cout << std::endl;
    std::cout << "Enter choice (1 or 2): ";

    std::string choice;
    std::getline(std::cin, choice);

    if (choice == "1") {
        return runSpriteSheetMode();
    } else if (choice == "2") {
        return runExtractPartsMode(); 
    } else {
        std::cerr << "Error: Invalid choice. Please enter 1 or 2." << std::endl;
        return 1;
    }
}