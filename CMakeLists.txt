cmake_minimum_required(VERSION 3.14)
project(FlipBookGenerator VERSION 0.6.3 LANGUAGES CXX)

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
# Both characters run the same gates: the reference character (character_32)
# and the same-skeleton reskin (character_alt). Each has three views.
# 两个角色跑同样的闸门：参考角色（character_32）与同骨架换皮（character_alt）。
# 各有三视图。
foreach(char character_32 character_alt)
    foreach(view front side back)
        if(view STREQUAL "front")
            set(bundle tests/fixtures/${char})
        else()
            set(bundle tests/fixtures/${char}_${view})
        endif()
        add_test(
            NAME reconstruction_${char}_${view}
            COMMAND flipbook --recon-test --bundle
                    ${CMAKE_CURRENT_SOURCE_DIR}/${bundle}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        # The idle-invariant gate. The idle is a STRETCH (legs planted, torso
        # grows, head/arms rise): one frame is byte-identical to the source, the
        # leg region never moves, and no frame splits into more blobs than the
        # source. Read from the rendered frames, not the keyframes.
        # 待机不变量闸门。待机是【伸展】（腿钉住、躯干加长、头/臂抬起）：有一帧与
        # 源图逐字节相同、腿部区域从不动、没有帧比源图裂成更多连通块。从渲染帧读取。
        add_test(
            NAME idle_purity_${char}_${view}
            COMMAND flipbook --idle-purity --bundle
                    ${CMAKE_CURRENT_SOURCE_DIR}/${bundle}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endforeach()

    # Preset smoke test on the front rig (the rig the preset names describe).
    # 在正面 rig 上跑预设冒烟测试（预设名描述的正是这套 rig）。
    add_test(
        NAME presets_smoke_${char}
        COMMAND flipbook --presets --bundle
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/${char}
                --out ${CMAKE_CURRENT_BINARY_DIR}/test_preview_${char}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endforeach()
