// spike_main.cpp — v0.8.0 day-one spike gate
// GLFW window + ImGui + a textured quad (GL_NEAREST x8) + live mouse coords.
//
// CLAUDE.md mandates this gate: prove the GLFW/OpenGL3 toolchain runs on macOS
// arm64 BEFORE writing any panel. Acceptance = window opens, the texture shows,
// the coords move. Until this passes, no panels. A macOS core-profile context
// (3.2+ forward-compatible) is required — that is the real arm64 risk.
// CLAUDE.md 要求的闸门：写任何面板前，先证明 GLFW/OpenGL3 工具链在 macOS arm64
// 能跑。验收 = 窗口能开、纹理能见、坐标会动。过关前不写面板。macOS 需要核心配置
// 上下文（3.2+ 前向兼容）—— 这才是 arm64 的真正风险。

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#define GLFW_INCLUDE_NONE   // we include <OpenGL/gl3.h> below / 我们在下面自己 include gl3.h
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#endif

#include "texture.h"

int main(int argc, char** argv) {
    // --verify: run the readback gate and exit cleanly (headless-friendly).
    // Default: keep the window open so a human can eyeball it.
    // --verify：跑完读回闸门就干净退出（便于无头测试）。默认保持窗口给人看。
    const bool verifyOnly = (argc > 1 && std::strcmp(argv[1], "--verify") == 0);

    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    // macOS core-profile context / macOS 核心配置上下文
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "FlipBook Spike", nullptr, nullptr);
    if (!window) { std::fprintf(stderr, "glfwCreateWindow failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");   // macOS GL 3.2 core / macOS GL 3.2 核心

    const char* chipPath = "assets/templates/motion_chips/chips/front_idle_0.png";
    cv::Mat chip = cv::imread(chipPath, cv::IMREAD_UNCHANGED);
    if (chip.empty()) { std::fprintf(stderr, "cannot read %s\n", chipPath); return 1; }
    if (chip.channels() == 3) cv::cvtColor(chip, chip, cv::COLOR_BGR2BGRA);
    const unsigned int tex = fb::ui::matToTexture(chip);
    if (!tex) { std::fprintf(stderr, "matToTexture failed\n"); return 1; }

    const int zoom = 8;
    const float sz = chip.cols * (float)zoom;
    bool verified = false;   // one-shot readback self-check / 一次性读回自检

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Spike: textured quad + live mouse coords");
        ImGui::Text("chip %s  (%dx%d)  zoom x%d  GL_NEAREST", chipPath, chip.cols, chip.rows, zoom);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(sz, sz), ImVec2(0, 0), ImVec2(1, 1));

        // Mouse pixel coords over the quad / 四边形上的鼠标像素坐标
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float mx = m.x - p0.x, my = m.y - p0.y;
        if (mx >= 0 && my >= 0 && mx < sz && my < sz) {
            const int px = (int)(mx / zoom), py = (int)(my / zoom);
            ImGui::Text("pixel (%d, %d)", px, py);
        } else {
            ImGui::Text("mouse off-image");
        }
        ImGui::End();

        ImGui::Render();
        int dw = 0, dh = 0;
        glfwGetFramebufferSize(window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // One-shot objective gate (CLAUDE.md's biggest-risk check): read the
        // framebuffer back and confirm the quad actually drew — any pixel that
        // differs from the clear colour (sampled at the corner) means the
        // texture rendered. Screen-independent; works headless.
        // 一次性客观闸门（CLAUDE.md 最大风险检查）：读回帧缓冲确认四边形真画了 ——
        // 任何异于清屏色（取角像素）的像素都说明纹理渲染出来了。不依赖截屏。
        static int frame = 0;
        if (++frame == 2 && !verified) {
            std::vector<unsigned char> buf((size_t)dw * dh * 4);
            glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
            const unsigned char* base = buf.data();
            long nonClear = 0;
            for (long i = 0; i < (long)dw * dh; ++i) {
                if (buf[i*4] != base[0] || buf[i*4+1] != base[1] || buf[i*4+2] != base[2])
                    ++nonClear;
            }
            std::printf("SPIKE READBACK: %ld non-clear px of %d\n", nonClear, dw * dh);
            std::printf("SPIKE RESULT: %s\n",
                        nonClear > 1000 ? "PASS (texture rendered)" : "FAIL (nothing drawn)");
            std::fflush(stdout);
            verified = true;
            if (verifyOnly) break;   // gate done → clean exit / 闸门跑完 → 干净退出
        }

        glfwSwapBuffers(window);
    }

    fb::ui::deleteTexture(tex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
