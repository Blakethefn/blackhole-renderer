#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace bhr {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    void allocate(int w, int h);

    inline void set_pixel(int x, int y,
                          std::uint8_t r, std::uint8_t g,
                          std::uint8_t b, std::uint8_t a = 255) {
        const size_t i = (static_cast<size_t>(y) * width + x) * 4;
        rgba[i + 0] = r;
        rgba[i + 1] = g;
        rgba[i + 2] = b;
        rgba[i + 3] = a;
    }
};

bool write_png(const Image& img, const std::string& path);

} // namespace bhr
