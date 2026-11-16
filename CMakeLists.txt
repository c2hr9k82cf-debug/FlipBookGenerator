cmake_minimum_required(VERSION 3.18)
project(FlipBookGenerator VERSION 0.13.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Only the modules the code actually uses. Linking all of OpenCV drags in
# contrib modules and their fortran/BLAS chains (libgfortran), which breaks
# dylibbundler (load-command overflow) and bloats the .app.
# 只链真正用到的模块。链全量 OpenCV 会拖进 contrib 模块及其 fortran/BLAS 链
# （libgfortran），既撑爆 dylibbundler 的 load command 空间，又让 .app 虚胖。
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs highgui)

# The GUI ships without highgui/videoio — that chain drags in all of ffmpeg
# (hundreds of MB). Only the temporary CLI menu's cv::imshow modes need it.
# GUI 不带 highgui/videoio —— 那条链会拖进整个 ffmpeg（数百 MB）。只有临时 CLI
# 菜单的 cv::imshow 模式需要它。
set(OPENCV_MINIMAL_LIBS opencv_core opencv_imgproc opencv_imgcodecs)

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
    src/core/parts_library.cpp
    src/core/motion.cpp
    src/core/image_io.cpp
    src/core/palette.cpp
    src/core/sheet.cpp
    src/core/align.cpp
    src/core/json_min.cpp
    src/core/regions.cpp
    src/core/replace.cpp
    src/core/template_spec.cpp
    src/core/backinfer.cpp
    src/core/part_editor.cpp
    src/core/compose.cpp
)
target_link_libraries(flipbook_core ${OPENCV_MINIMAL_LIBS})

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

# Four-direction sheet gates (the .app pipeline). Palette extraction round-trips
# a synthesised view and stays strict (the alt character is the negative
# fixture); the sheet gate checks layout / idle fill / mirroring / masks /
# coverage. Template-level, run once.
# 四方向大图闸门（app 管线）。配色提取对合成视图精确往返并保持严格（换皮角色图
# 是负向夹具）；大图闸门检查布局/补帧/镜像/掩码/覆盖。模板级，跑一次。
add_test(
    NAME palette_tolerant
    COMMAND flipbook --palette-tolerant-gate --chips
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates/motion_chips
            --kit ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/test_kit
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(
    NAME palette_extract
    COMMAND flipbook --palette-gate --chips
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates/motion_chips
            --map ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates/motion_chips/palette_map.txt
            --front ${CMAKE_CURRENT_SOURCE_DIR}/assets/character_alt.png
            --side ${CMAKE_CURRENT_SOURCE_DIR}/assets/character_alt_side.png
            --back ${CMAKE_CURRENT_SOURCE_DIR}/assets/character_alt_back.png
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(
    NAME fourdir_sheet
    COMMAND flipbook --fourdir-gate --chips
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates/motion_chips
            --map ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates/motion_chips/palette_map.txt
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Alignment diagnostics gates (v0.9.0). apply: identity / known shift / known
# scale resampling byte-exact; auto: exact recovery of a known (scale,dx,dy)
# perturbation; diff: masks mark exactly the differing pixels and the overlay
# colours them. Template-level, run once.
# 对齐诊断闸门（v0.9.0）。apply：恒等/已知平移/已知缩放重采样逐字节；
# auto：精确恢复已知 (scale,dx,dy) 扰动；diff：掩码恰标差异像素、叠加图着色。
# 模板级，跑一次。
foreach(align_case apply auto diff)
    if(align_case STREQUAL "diff")
        set(align_test diff_mask)
    else()
        set(align_test align_${align_case})
    endif()
    add_test(
        NAME ${align_test}
        COMMAND flipbook --align-gate ${align_case} --chips
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates/motion_chips
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endforeach()

# Template / parts / library gates (v0.9.1). The template JSONs are the data
# source of truth; parts semantics must match the committed bundles exactly
# (both characters x three views); the library loader must refuse every known
# bad shape with a reason.
# 模板 / 部件 / 库闸门（v0.9.1）。模板 JSON 是数据真源；部件语义必须与已提交
# 部件包逐字节一致（两角色 x 三视图）；库加载器必须拒绝全部已知坏形态并给出原因。
add_test(
    NAME template_load
    COMMAND flipbook --template-load-gate
            --templates ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates
            --assets ${CMAKE_CURRENT_SOURCE_DIR}/assets
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(
    NAME parts_semantics
    COMMAND flipbook --parts-semantics-gate
            --templates ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates
            --assets ${CMAKE_CURRENT_SOURCE_DIR}/assets
            --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(
    NAME parts_library
    COMMAND flipbook --parts-library-gate
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Back-view inference gate (v0.9.2). Synthetic self-checks are the hard rule
# (exact mirror, face covered, edge outline kept, interior eaten, flatten
# swaps, alpha invariant); the reference comparison asserts the symmetric crown
# band byte-exact and reports — not fails — the hand-drawn deviations.
# 背面推断闸门（v0.9.2）。合成自证是硬规则（精确镜像、脸被盖住、边缘描边保留、
# 内部吃掉、flatten 换色、alpha 不变量）；参考对照断言对称冠区逐字节一致，
# 与手绘的偏差如实报告不失败。
add_test(
    NAME backinfer
    COMMAND flipbook --backinfer-gate
            --templates ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates
            --assets ${CMAKE_CURRENT_SOURCE_DIR}/assets
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Part-editor fill gate (v0.10.0): the in-tool fill loop end to end —
# blank template -> fill/brush/undo -> role constraint -> library round trip.
# 部件编辑器填色闸门（v0.10.0）：工具内填色闭环到底 —— 空白模板 -> 填色/画笔/
# 撤销 -> 角色约束 -> 库往返。
add_test(
    NAME fill
    COMMAND flipbook --fill-gate
            --templates ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates
            --assets ${CMAKE_CURRENT_SOURCE_DIR}/assets
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Region-box gate (v0.12.0): the drawing contract — chibi_regions.json must be
# self-consistent, every reference item must sit inside its slot's box, and a
# synthetic out-of-box pixel must be caught.
# 轮廓框闸门（v0.12.0）：绘制契约 —— chibi_regions.json 必须自洽；每件参考件
# 必须坐在本槽框内；合成的框外像素必须被抓住。
add_test(
    NAME region_boxes
    COMMAND flipbook --region-gate
            --templates ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates
            --assets ${CMAKE_CURRENT_SOURCE_DIR}/assets
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Editor box gate (v0.12.0): the save side of the drawing contract — an
# in-box two-piece item round-trips byte-exact (front_b/side_b included), one
# out-of-box pixel refuses the save with a count and writes nothing, back_b
# needs the main back.png, a wrong-size piece is refused.
# 编辑器框闸门（v0.12.0）：绘制契约的保存侧 —— 框内双片件逐字节往返（含
# front_b/side_b）；一个框外像素即拒绝保存、报数、且不写盘；back_b 需要主片
# back.png；坏尺寸片被拒。
add_test(
    NAME editor_box
    COMMAND flipbook --editor-box-gate
            --templates ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Replace-classify gate (v0.13.0): the core region classifier reproduces the
# committed tool artefacts — reference splits == ref_* sprites byte-exact,
# 24-frame classify union == chibi_regions.json masks, anim series sane.
# 替换分类闸门（v0.13.0）：core 的区域分类器复现工具提交产物 —— 参考拆分 ==
# ref_* 精灵逐字节、24 帧分类并集 == chibi_regions.json 掩码、动画序列自洽。
add_test(
    NAME replace_classify
    COMMAND flipbook --replace-classify-gate
            --templates ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates
            --assets ${CMAKE_CURRENT_SOURCE_DIR}/assets
            --chips ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates/motion_chips
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Replace-fidelity gate (v0.13.0): user == reference with an identity map must
# reproduce all 24 chip frames byte-exact — replacement without customisation
# is lossless. Replace-custom gate: custom shapes enter the frames (hat
# extension stamps every frame, no holes), and the arm/clothes rule holds per
# frame (skin defends arm cells, a custom sleeve replaces a shirt cell).
# 替换保真闸门（v0.13.0）：用户 == 参考、映射恒等必须逐字节复现 24 帧芯片 ——
# 无自定义的替换是无损的。替换自定义闸门：自定义形状进帧（扩展的帽子盖到每一
# 帧、不出洞），衣/臂规则逐帧成立（肤护臂格、自定义袖替换上衣格）。
add_test(
    NAME replace_fidelity
    COMMAND flipbook --replace-fidelity-gate
            --templates ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates
            --chips ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates/motion_chips
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(
    NAME replace_custom
    COMMAND flipbook --replace-custom-gate
            --templates ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates
            --chips ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates/motion_chips
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# D3 layering gate (v0.12.0): the back piece beds down under the face, the
# front piece covers it, bare skin defends the arms, the back view crosses
# the pieces (mirror rules), single-piece items stamp twice.
# D3 层序闸门（v0.12.0）：后发垫底、前发盖脸、裸肤护臂、背面两片交叉（镜像
# 规则）、单片件贴两次。
add_test(
    NAME compose_layers
    COMMAND flipbook --compose-layers-gate
            --templates ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Composer gate (v0.10.1): six reference items recompose the reference views
# byte-exact, role overrides recolour exactly, back:"auto" equals per-item
# inference, recipes round-trip, and the composed views produce the same
# fourdir sheet as the reference views (full chain).
# 合成闸门（v0.10.1）：六件参考物品逐字节重合成参考视图、角色覆盖精确换色、
# back:"auto" 等于逐物品推断、配方往返一致，合成视图出的四方向大图与参考
# 视图一致（全链）。
add_test(
    NAME compose
    COMMAND flipbook --compose-gate
            --library ${CMAKE_CURRENT_SOURCE_DIR}/assets/parts_library
            --templates ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates
            --assets ${CMAKE_CURRENT_SOURCE_DIR}/assets
            --chips ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates/motion_chips
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
    FetchContent_Declare(pfd
        GIT_REPOSITORY https://github.com/samhocevar/portable-file-dialogs.git
        GIT_TAG 0.1.0 GIT_SHALLOW TRUE
        # SOURCE_SUBDIR pointing at a nonexistent dir stops MakeAvailable from
        # add_subdirectory-ing upstream's ancient CMakeLists (CMake 4 refuses
        # its low policy floor). We only want the header.
        # SOURCE_SUBDIR 指向不存在的目录，阻止 MakeAvailable 执行上游那个古老
        # CMakeLists（CMake 4 拒绝它过低的 policy 下限）。我们只要那个头文件。
        SOURCE_SUBDIR __no_cmake_here__)
    # glfw has a CMakeLists (gets a target); imgui does not (sources only — we
    # build a static lib below). MakeAvailable skips add_subdirectory when the
    # fetched content has no CMakeLists.txt.
    # glfw 有 CMakeLists（出 target）；imgui 没有（只取源码 —— 下面自己编静态库）。
    # 内容无 CMakeLists.txt 时 MakeAvailable 跳过 add_subdirectory。
    FetchContent_MakeAvailable(glfw imgui pfd)

    # portable-file-dialogs is a single header (native dialogs via osascript on
    # macOS). The upstream repo may or may not ship a CMake target — provide one.
    # portable-file-dialogs 是单头文件（macOS 上经 osascript 弹原生对话框）。
    # 上游仓库不一定带 CMake target —— 这里补一个。
    if(NOT TARGET portable-file-dialogs)
        add_library(portable-file-dialogs INTERFACE)
        target_include_directories(portable-file-dialogs INTERFACE ${pfd_SOURCE_DIR})
    endif()

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
    target_link_libraries(ui_spike imgui ${OPENCV_MINIMAL_LIBS})

    # The GUI app: three views in, one four-direction sheet PNG out. Thin shell
    # over flipbook_core — the GUI must export byte-identical to the CLI.
    # GUI 应用：三视图进，一张四方向大图 PNG 出。flipbook_core 的薄壳 ——
    # GUI 导出必须与 CLI 逐字节一致。
    add_executable(flipbook_gui
        src/ui/main.cpp
        src/ui/panels.cpp
        src/ui/part_editor_panel.cpp
        src/ui/dressup_panel.cpp
        src/ui/canvas.cpp
        src/ui/path_dialog.cpp
        src/ui/app_state.cpp
        src/ui/assets_locator.cpp
        src/ui/texture.cpp
    )
    target_link_libraries(flipbook_gui
        flipbook_core imgui portable-file-dialogs ${OPENCV_MINIMAL_LIBS})
    # Header padding so install_name_tool can rewrite the packaged binary's
    # install names (the standard packaging trick).
    # 留头空间，让 install_name_tool 能改写打包后二进制的 install name（打包惯例）。
    target_link_options(flipbook_gui PRIVATE "LINKER:-headerpad_max_install_names")
endif()
