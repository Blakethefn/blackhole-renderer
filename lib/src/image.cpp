#include "bhr/image.hpp"
#include "bhr/appearance.hpp"
#include <memory>
#include <cstdio>

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

namespace detail { void atomic_write(const std::filesystem::path&,const std::string&); }
bool write_srgb_png(const Image& img, const std::string& path) {
    try {
        const auto s=cinematic_sizes(img.width,img.height);
        if(img.rgba.size()!=s.rgba_bytes) return false;
        int length=0;
        std::unique_ptr<unsigned char,decltype(&std::free)> bytes(
            stbi_write_png_to_mem(img.rgba.data(),img.width*4,img.width,img.height,4,&length),std::free);
        if(!bytes || length<33) return false;
        unsigned char chunk[13]={0,0,0,1,'s','R','G','B',0,0,0,0,0};
        const auto crc=stbiw__crc32(chunk+4,5);
        for(int i=0;i<4;++i)chunk[9+i]=static_cast<unsigned char>(crc>>(24-i*8));
        const std::string encoded(reinterpret_cast<char*>(bytes.get()),length);
        const auto result=encoded.substr(0,33)+std::string(reinterpret_cast<char*>(chunk),13)+encoded.substr(33);
        detail::atomic_write(path,result);
        return true;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"Cinematic PNG write failed: %s\n",error.what());return false;
    }
}
} // namespace bhr
