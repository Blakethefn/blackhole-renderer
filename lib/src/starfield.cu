#include "bhr/starfield.hpp"

#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace bhr {
namespace {
constexpr int kMaxUploadWidth = 16384;
constexpr std::int64_t kMaxSourcePixels = 128LL * 1024 * 1024;

struct ExrData {
    EXRHeader header{};
    EXRImage image{};
    ExrData() { InitEXRHeader(&header); InitEXRImage(&image); }
    ~ExrData() { FreeEXRImage(&image); FreeEXRHeader(&header); }
    ExrData(const ExrData&) = delete;
    ExrData& operator=(const ExrData&) = delete;
};

void check_exr(int status, const char* error, const char* operation) {
    const std::string message = error ? error : operation;
    if (error) FreeEXRErrorMessage(error);
    if (status != TINYEXR_SUCCESS) throw std::runtime_error(message);
}

std::vector<float4> decode(const std::string& path, int max_width, int& width, int& height,
                           std::int64_t source_limit, size_t upload_limit, int channel_limit) {
    EXRVersion version{};
    if (ParseEXRVersionFromFile(&version, path.c_str()) != TINYEXR_SUCCESS)
        throw std::runtime_error("Cannot read EXR version");
    if (version.tiled || version.multipart || version.non_image)
        throw std::runtime_error("Starfields require a single-part scanline RGB EXR (no tiled/deep images)");
    ExrData data;
    const char* error = nullptr;
    const int header_status = ParseEXRHeaderFromFile(&data.header, &version, path.c_str(), &error);
    check_exr(header_status, error, "Cannot read EXR header");
    const auto& header = data.header;
    const std::int64_t source_width = std::int64_t(header.data_window.max_x) - header.data_window.min_x + 1;
    const std::int64_t source_height = std::int64_t(header.data_window.max_y) - header.data_window.min_y + 1;
    if (source_width <= 0 || source_height <= 0 || source_width > source_limit / source_height)
        throw std::runtime_error("EXR dimensions exceed the supported source pixel limit");
    if (header.num_channels > channel_limit)
        throw std::runtime_error("Cinematic EXR supports RGB with at most one extra channel");
    const auto predicted_factor=std::max<std::int64_t>(1,(source_width+max_width-1)/max_width);
    if (static_cast<size_t>((source_width+predicted_factor-1)/predicted_factor)
        *static_cast<size_t>((source_height+predicted_factor-1)/predicted_factor)>upload_limit)
        throw std::runtime_error("EXR dimensions exceed the upload pixel limit before decode");
    int red = -1, green = -1, blue = -1;
    for (int channel = 0; channel < header.num_channels; ++channel) {
        const auto& info = header.channels[channel];
        if (header.pixel_types[channel] != TINYEXR_PIXELTYPE_HALF
            && header.pixel_types[channel] != TINYEXR_PIXELTYPE_FLOAT)
            throw std::runtime_error("EXR channels must contain HALF or FLOAT radiance");
        if (info.x_sampling != 1 || info.y_sampling != 1)
            throw std::runtime_error("Subsampled EXR channels are unsupported");
        if (std::strcmp(info.name, "R") == 0) red = channel;
        if (std::strcmp(info.name, "G") == 0) green = channel;
        if (std::strcmp(info.name, "B") == 0) blue = channel;
        data.header.requested_pixel_types[channel] = TINYEXR_PIXELTYPE_FLOAT;
    }
    if (red < 0 || green < 0 || blue < 0)
        throw std::runtime_error("EXR requires named R, G and B channels");
    error = nullptr;
    const int image_status = LoadEXRImageFromFile(&data.image, &data.header, path.c_str(), &error);
    check_exr(image_status, error, "Cannot decode EXR image");
    const auto& image = data.image;
    if (!image.images || image.width != source_width || image.height != source_height)
        throw std::runtime_error("EXR decoded dimensions or channel storage are invalid");
    const auto* r = reinterpret_cast<const float*>(image.images[red]);
    const auto* g = reinterpret_cast<const float*>(image.images[green]);
    const auto* b = reinterpret_cast<const float*>(image.images[blue]);
    if (!r || !g || !b) throw std::runtime_error("EXR RGB storage is missing");
    const auto source_pixels = static_cast<size_t>(source_width * source_height);
    for (size_t pixel = 0; pixel < source_pixels; ++pixel) {
        if (!std::isfinite(r[pixel]) || !std::isfinite(g[pixel]) || !std::isfinite(b[pixel])
            || r[pixel] < 0.0f || g[pixel] < 0.0f || b[pixel] < 0.0f)
            throw std::runtime_error("EXR radiance must be finite and nonnegative");
    }
    const int factor = std::max(1, (image.width + max_width - 1) / max_width);
    width = (image.width + factor - 1) / factor;
    height = (image.height + factor - 1) / factor;
    if (static_cast<size_t>(width)*height > upload_limit)
        throw std::runtime_error("EXR dimensions exceed the upload pixel limit");
    std::vector<float4> output(static_cast<size_t>(width) * height);
    // Partial edge boxes preserve odd dimensions and one-pixel-high maps.
    // Accumulate in double so finite HDR float values cannot overflow the sum.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int x_end = std::min((x + 1) * factor, image.width);
            const int y_end = std::min((y + 1) * factor, image.height);
            double rr = 0.0, gg = 0.0, bb = 0.0;
            for (int sy = y * factor; sy < y_end; ++sy) {
                for (int sx = x * factor; sx < x_end; ++sx) {
                    const auto index = static_cast<size_t>(sy) * image.width + sx;
                    rr += r[index]; gg += g[index]; bb += b[index];
                }
            }
            const double count = static_cast<double>(x_end - x * factor) * (y_end - y * factor);
            output[static_cast<size_t>(y) * width + x] = make_float4(
                static_cast<float>(rr / count), static_cast<float>(gg / count),
                static_cast<float>(bb / count), 1.0f);
        }
    }
    return output;
}

bool checked_cuda(cudaError_t status, const char* operation) {
    if (status == cudaSuccess) return true;
    std::fprintf(stderr, "Starfield %s failed: %s\n", operation, cudaGetErrorString(status));
    return false;
}
} // namespace

static bool load_impl(const std::string& path, int max_width, Starfield& out,
                      std::int64_t source_limit, size_t upload_limit, int channel_limit) {
    if (out.tex || out.d_array) {
        std::fprintf(stderr, "Starfield output already owns GPU resources; release it before loading\n");
        return false;
    }
    if (path.empty() || max_width <= 0 || max_width > kMaxUploadWidth) {
        std::fprintf(stderr, "Starfield needs a path and upload width between 1 and 16384\n");
        return false;
    }
    try {
        int width = 0, height = 0;
        const auto pixels = decode(path, max_width, width, height, source_limit, upload_limit,channel_limit);
        const auto desc = cudaCreateChannelDesc<float4>();
        if (!checked_cuda(cudaMallocArray(&out.d_array, &desc, width, height), "allocate array")) return false;
        const size_t pitch = static_cast<size_t>(width) * sizeof(float4);
        if (!checked_cuda(cudaMemcpy2DToArray(out.d_array, 0, 0, pixels.data(), pitch,
                pitch, height, cudaMemcpyHostToDevice), "upload pixels")) {
            destroy_starfield(out);
            return false;
        }
        cudaResourceDesc resource{};
        resource.resType = cudaResourceTypeArray;
        resource.res.array.array = out.d_array;
        cudaTextureDesc texture{};
        texture.addressMode[0] = cudaAddressModeWrap;
        texture.addressMode[1] = cudaAddressModeClamp;
        texture.filterMode = cudaFilterModeLinear;
        texture.readMode = cudaReadModeElementType;
        texture.normalizedCoords = 1;
        if (!checked_cuda(cudaCreateTextureObject(&out.tex, &resource, &texture, nullptr), "create texture")) {
            destroy_starfield(out);
            return false;
        }
        out.width = width;
        out.height = height;
        return true;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Starfield load failed: %s\n", error.what());
        return false;
    }
}

bool load_starfield(const std::string& path, int max_width, Starfield& out) {
    return load_impl(path,max_width,out,kMaxSourcePixels,static_cast<size_t>(kMaxSourcePixels),INT32_MAX);
}
bool load_cinematic_starfield(const std::string& path, Starfield& out) {
    return load_impl(path,2048,out,8LL*1024*1024,2*1024*1024,4);
}

bool destroy_starfield(Starfield& sf) {
    // On failure retain ownership for a retry; never free an array still used by
    // a texture whose destruction failed. Call only after submitted work settles.
    if (sf.tex) {
        if (!checked_cuda(cudaDestroyTextureObject(sf.tex), "destroy texture")) return false;
        sf.tex = 0;
    }
    if (sf.d_array) {
        if (!checked_cuda(cudaFreeArray(sf.d_array), "free array")) return false;
        sf.d_array = nullptr;
    }
    sf = Starfield{};
    return true;
}
} // namespace bhr
