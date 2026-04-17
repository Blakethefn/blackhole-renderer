#include "bhr/image.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace bhr {

void Image::allocate(int w, int h) {
    width = w;
    height = h;
    rgba.assign(static_cast<size_t>(w) * h * 4, 0);
}

bool write_png(const Image& img, const std::string& path) {
    if (img.width <= 0 || img.height <= 0 || img.rgba.empty()) return false;
    const int stride = img.width * 4;
    const int ok = stbi_write_png(path.c_str(), img.width, img.height, 4,
                                  img.rgba.data(), stride);
    return ok != 0;
}

} // namespace bhr
