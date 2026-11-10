cmake_minimum_required(VERSION 3.14)
project(FlipBookGenerator VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

# Version header — single source of truth is project(VERSION ...) above
# 版本号头文件 —— 唯一来源是上面的 project(VERSION ...)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h
    @ONLY
)

# src/ on the include path so sources can say "core/core.h"
# 把 src/ 加进包含路径，源码才能写 "core/core.h"
include_directories(
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# ---------------------------------------------------------------------------
# flipbook_core — UI-free animation engine / 无 UI 的动画核心引擎
#
# RULE / 铁律: this target must never include a UI header or call cv::imshow.
#              本目标永不 include UI 头文件，永不调用 cv::imshow。
# ---------------------------------------------------------------------------
add_library(flipbook_core STATIC
    src/core/rig.cpp
    src/core/animation.cpp
    src/core/render.cpp
    src/core/export.cpp
)
target_link_libraries(flipbook_core ${OpenCV_LIBS})

# ---------------------------------------------------------------------------
# flipbook — the app / 应用
# ---------------------------------------------------------------------------
add_executable(flipbook
    src/main.cpp
    src/utils.cpp
    src/spritesheet.cpp
    src/extractor.cpp
)
target_link_libraries(flipbook flipbook_core ${OpenCV_LIBS})

# ---------------------------------------------------------------------------
# Tests / 测试
#
# The reconstruction gate is the project's correctness invariant: parts must
# reassemble to their source pixel-exact before anything is animated.
# 重建闸门是本项目的正确性不变量：部件必须能逐像素还原，才能开始做动画。
# ---------------------------------------------------------------------------
enable_testing()
add_test(
    NAME reconstruction
    COMMAND flipbook --recon-test --bundle
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/character_32
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(
    NAME presets_smoke
    COMMAND flipbook --presets --bundle
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/character_32
            --out ${CMAKE_CURRENT_BINARY_DIR}/test_preview
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
