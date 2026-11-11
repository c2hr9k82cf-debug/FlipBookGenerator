cmake_minimum_required(VERSION 3.14)
project(FlipBookGenerator VERSION 0.8.0 LANGUAGES CXX)

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
    src/core/motion.cpp
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

# Tier B chip gates (template-level, character-independent). Fidelity proves the
# chips are a verbatim copy of the reference sheet; reskin proves a reskin only
# swaps colour. The chip template is shared by every character, so each gate
# runs once.
# Tier B 芯片闸门（模板级，与角色无关）。保真证明芯片是参考图的照搬；换皮证明换皮
# 只换色。芯片模板为所有角色共享，故各跑一次。
add_test(
    NAME chip_recon
    COMMAND flipbook --chip-recon --chips
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates/motion_chips
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(
    NAME chip_reskin
    COMMAND flipbook --chip-reskin --chips
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates/motion_chips
            --map ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates/motion_chips/palette_map.txt
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# ---------------------------------------------------------------------------
# GUI (macOS desktop app) / GUI（macOS 桌面应用）
#
# Optional (FetchContent pulls GLFW/ImGui on first configure, needs network).
# core and the tests never include UI headers — the CLAUDE.md iron rule.
# 可选（FetchContent 首次 configure 拉 GLFW/ImGui，需网络）。
# core 与测试永不 include UI 头 —— CLAUDE.md 铁律。
# ---------------------------------------------------------------------------
option(FLIPBOOK_BUILD_GUI "Build the ImGui macOS desktop app" ON)
if(FLIPBOOK_BUILD_GUI)
    include(FetchContent)

    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

    # Pinned for reproducibility. imgui v1.91.9 is the last with the classic
    # ImTextureID texture API (v1.92 introduced ImTextureRef). GLFW 3.4 is the
    # current stable with native arm64 Cocoa support.
    # 钉死版本保证可复现。imgui v1.91.9 是最后一个经典 ImTextureID 纹理 API 的版本
    # （v1.92 引入 ImTextureRef）。GLFW 3.4 是原生支持 arm64 Cocoa 的稳定版。
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4 GIT_SHALLOW TRUE)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.9-docking GIT_SHALLOW TRUE)
    # glfw has a CMakeLists (gets a target); imgui does not (sources only — we
    # build a static lib below). MakeAvailable skips add_subdirectory when the
    # fetched content has no CMakeLists.txt.
    # glfw 有 CMakeLists（出 target）；imgui 没有（只取源码 —— 下面自己编静态库）。
    # 内容无 CMakeLists.txt 时 MakeAvailable 跳过 add_subdirectory。
    FetchContent_MakeAvailable(glfw imgui)

    find_package(OpenGL REQUIRED)

    # Dear ImGui as a static lib (upstream ships no CMake). Core + GLFW/OpenGL3
    # backends. / ImGui 静态库（上游无 CMake）：核心 + GLFW/OpenGL3 后端。
    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
    target_compile_definitions(imgui PUBLIC GL_SILENCE_DEPRECATION=1)

    # v0.8.0 day-one spike gate: window + textured quad (GL_NEAREST x8) + live
    # mouse coords. Proves GLFW/OpenGL3 on arm64 before any panel is written.
    # v0.8.0 day-one 闸门：窗口 + 纹理四边形（GL_NEAREST x8）+ 实时鼠标坐标。
    # 写任何面板前先证明 GLFW/OpenGL3 在 arm64 能跑。
    add_executable(ui_spike
        src/ui/spike_main.cpp
        src/ui/texture.cpp
    )
    target_link_libraries(ui_spike imgui ${OpenCV_LIBS})
endif()
