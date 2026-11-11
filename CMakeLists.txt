cmake_minimum_required(VERSION 3.14)
project(FlipBookGenerator VERSION 0.5.7 LANGUAGES CXX)

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
    src/core/parts.cpp
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
# reassemble to their source pixel-exact before anything is animated. It runs
# per view — front, side and back each have their own rig and their own source,
# so each has to prove itself separately.
# 重建闸门是本项目的正确性不变量：部件必须能逐像素还原，才能开始做动画。
# 它按视图各跑一次 —— 正面、侧视、背视各有自己的 rig 与源图，各自都要自证。
# ---------------------------------------------------------------------------
enable_testing()
foreach(view front side back)
    if(view STREQUAL "front")
        set(bundle tests/fixtures/character_32)
    else()
        set(bundle tests/fixtures/character_32_${view})
    endif()
    add_test(
        NAME reconstruction_${view}
        COMMAND flipbook --recon-test --bundle
                ${CMAKE_CURRENT_SOURCE_DIR}/${bundle}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    # The pinch/tear gate. A uniform bob cannot pull parts apart, so the seam
    # repair has to leave every idle frame byte-identical to the source moved.
    # Over-filling (redrawing the artist's notch) and under-filling (a missed
    # tear) both show up here as a frame that is no longer a translation.
    # 撕裂闸门。整体起伏拉不开部件，所以补缝必须让每帧待机与【移动过的源图】
    # 逐字节相同。多补（把画师的缺口重画）与少补（漏掉的撕裂）都会让该帧不再是
    # 平移，从而在这里现形。
    add_test(
        NAME idle_purity_${view}
        COMMAND flipbook --idle-purity --bundle
                ${CMAKE_CURRENT_SOURCE_DIR}/${bundle}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endforeach()

# Preset smoke test: the four presets are named after their visual intent
# (walk_side walks sideways, walk_front walks toward the camera). They are
# exercised on the front rig, which is the rig the preset names describe.
# 预设冒烟测试：四个预设按视觉意图命名（walk_side 侧走、walk_front 走向镜头）。
# 在正面 rig 上跑 —— 预设名描述的正是这套 rig。
add_test(
    NAME presets_smoke
    COMMAND flipbook --presets --bundle
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/character_32
            --out ${CMAKE_CURRENT_BINARY_DIR}/test_preview
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
