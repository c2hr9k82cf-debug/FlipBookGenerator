// texture.cpp — cv::Mat (BGRA) -> OpenGL texture implementation
// cv::Mat（BGRA）→ OpenGL 纹理实现

#include "texture.h"

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>   // macOS core-profile GL / macOS 核心配置 GL
#endif

namespace fb {
namespace ui {

unsigned int matToTexture(const cv::Mat& bgra) {
    if (bgra.empty() || bgra.type() != CV_8UC4) return 0;
    // glTexImage2D reads tightly-packed rows; guard against padded Mats.
    // glTexImage2D 按紧密行读取；防带步长 padding 的 Mat。
    const cv::Mat c = bgra.isContinuous() ? bgra : bgra.clone();

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    // Nearest-neighbour is non-negotiable for pixel art (CLAUDE.md).
    // 像素风必须最近邻（CLAUDE.md）。
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // cv::Mat is BGRA byte order; internal format is a sized RGBA8.
    // cv::Mat 是 BGRA 字节序；内部格式用带尺寸的 RGBA8。
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, c.cols, c.rows, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, c.data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return static_cast<unsigned int>(tex);
}

void deleteTexture(unsigned int tex) {
    if (tex) {
        GLuint t = tex;
        glDeleteTextures(1, &t);
    }
}

}  // namespace ui
}  // namespace fb
