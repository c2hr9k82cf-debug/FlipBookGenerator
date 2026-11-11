// main.cpp — FlipBookGenerator GUI entry (macOS desktop app)
// GUI 入口（macOS 桌面应用）
//
// Drag three template-aligned views in (front / side / back), get one
// four-direction spritesheet PNG out. The pipeline is the core engine —
// the GUI is a thin shell around it (CLAUDE.md iron rule: no core change for
// UI, no cv::imshow in ui/).
// 拖入三张模板对齐视图（front / side / back），导出一张四方向精灵大图 PNG。
// 管线在核心引擎 —— GUI 只是薄壳（CLAUDE.md 铁律：core 不为 UI 改动，ui/ 不调
// cv::imshow）。

#include "app_state.h"
#include "assets_locator.h"
#include "panels.h"
#include "texture.h"
#include "version.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define GLFW_INCLUDE_NONE   // we include <OpenGL/gl3.h> below / 我们自己 include gl3.h
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#endif

namespace {

// Dropped paths, filled by GLFW's callback and drained once per frame (the
// state change must happen between ImGui frames).
// 拖入路径，由 GLFW 回调填充，每帧消费一次（状态变更必须发生在 ImGui 帧间）。
std::vector<std::string> g_drops;

void dropCallback(GLFWwindow*, int count, const char** paths) {
    for (int i = 0; i < count; ++i) g_drops.push_back(paths[i]);
}

// Split "a,b,c,d" on commas. / 按逗号拆分。
std::vector<std::string> splitCommas(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

}  // namespace

int main() {
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    // macOS core-profile context / macOS 核心配置上下文
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    const std::string title = std::string("FlipBookGenerator V") + FLIPBOOK_VERSION;
    GLFWwindow* window = glfwCreateWindow(1280, 800, title.c_str(), nullptr, nullptr);
    if (!window) { std::fprintf(stderr, "glfwCreateWindow failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetDropCallback(window, dropCallback);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    // The UI is Chinese — ImGui's default font has no CJK glyphs (every
    // character would render as a tofu box). Load a system Chinese font; every
    // macOS ships PingFang, so nothing needs bundling.
    // 界面是中文 —— ImGui 默认字体没有中文字形（会全部显示成豆腐块）。
    // 加载系统中文字体；每台 macOS 都自带苹方，无需打包字体。
    {
        const char* kFontCandidates[] = {
            "/System/Library/Fonts/PingFang.ttc",
            "/System/Library/Fonts/Hiragino Sans GB.ttc",
            "/System/Library/Fonts/STHeiti Light.ttc",
        };
        for (const char* fp : kFontCandidates) {
            if (std::FILE* f = std::fopen(fp, "rb")) {
                std::fclose(f);
                io.Fonts->AddFontFromFileTTF(
                    fp, 18.0f, nullptr,
                    io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
                break;
            }
        }
        if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();
    }
    // No multi-viewport (macOS NSWindow focus quirks) — single window.
    // 不开 multi-viewport（macOS NSWindow 焦点坑）—— 单窗口。
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");   // macOS GL 3.2 core / macOS GL 3.2 核心

    fb::ui::AppState st;
    st.ensureChips();   // best-effort; errors land in the status bar / 尽力加载；错误进状态栏
    fb::ui::PreviewTextures tex;

    // Optional headless self-test (the golden-invariant check drives this):
    // FLIPBOOK_AUTOTEST=front,side,back,out.png loads the three views through
    // the same code path as the GUI, exports the sheet and exits.
    // 可选无头自测（金不变量检查驱动它）：FLIPBOOK_AUTOTEST=front,side,back,out.png
    // 经与 GUI 相同的代码路径载入三视图、导出大图并退出。
    if (const char* at = std::getenv("FLIPBOOK_AUTOTEST")) {
        const std::vector<std::string> parts = splitCommas(at);
        bool ok = false;
        if (parts.size() == 4) {
            fb::ui::assignDrops(st, {parts[0], parts[1], parts[2]});
            ok = st.exportSheet(parts[3]);
        }
        std::printf("AUTOTEST: %s — %s\n", ok ? "PASS" : "FAIL", st.status.c_str());
        std::fflush(stdout);
        tex.release();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return ok ? 0 : 1;
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (!g_drops.empty()) {
            std::vector<std::string> drops;
            drops.swap(g_drops);
            fb::ui::assignDrops(st, drops);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // One borderless window filling the GLFW window — the old framed window
        // plus framed child panels read as ugly "double boxes".
        // 一个无边框窗口铺满 GLFW 窗口 —— 旧的外框窗 + 带框子面板看着是丑陋的"两层"。
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##main", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        fb::ui::drawMainContent(st, tex);
        ImGui::End();

        ImGui::Render();
        int dw = 0, dh = 0;
        glfwGetFramebufferSize(window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    tex.release();   // before the GL context dies / GL 上下文销毁前
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
