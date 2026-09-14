#pragma once
#include <glad/glad.h>

namespace bhr::workbench {
/// Presentation already contains encoded sRGB bytes. Preserve the caller's
/// state but prevent an additional hardware sRGB conversion during the draw.
class EncodedFramebuffer final {
public:
    EncodedFramebuffer() : enabled_(glIsEnabled(GL_FRAMEBUFFER_SRGB)) {
        glDisable(GL_FRAMEBUFFER_SRGB);
    }
    ~EncodedFramebuffer() { if(enabled_) glEnable(GL_FRAMEBUFFER_SRGB); }
    EncodedFramebuffer(const EncodedFramebuffer&)=delete;
    EncodedFramebuffer& operator=(const EncodedFramebuffer&)=delete;
private:
    GLboolean enabled_;
};
}
