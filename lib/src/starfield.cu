#include "bhr/starfield.hpp"

#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>

namespace bhr {

namespace {

/// Convert an IEEE 754 half-float (16-bit) to float32.
inline float half_to_float(unsigned short h) {
    const unsigned int sign     = (h >> 15) & 0x1u;
    const unsigned int exponent = (h >> 10) & 0x1fu;
    const unsigned int mantissa = h & 0x3ffu;

    unsigned int f;
    if (exponent == 0) {
        if (mantissa == 0) {
            f = sign << 31;
        } else {
            // Denormal
            int e = -1;
            unsigned int m = mantissa;
            do { ++e; m <<= 1; } while (!(m & 0x400u));
            f = (sign << 31) | (static_cast<unsigned int>(127 - 14 - e) << 23) |
                ((m & 0x3ffu) << 13);
        }
    } else if (exponent == 31) {
        // Inf / NaN
        f = (sign << 31) | 0x7f800000u | (mantissa << 13);
    } else {
        f = (sign << 31) | ((exponent + 112u) << 23) | (mantissa << 13);
    }
    float result;
    std::memcpy(&result, &f, sizeof(result));
    return result;
}

/// Box-downsample a planar float image (separate R,G,B arrays) to RGBA interleaved float,
/// applying integer downscale factor. src_* arrays have src_w * src_h elements each.
/// Result is dst_w * dst_h * 4 floats (RGBA, A=1).
std::vector<float> downsample_planar_to_rgba(
    const float* src_r, const float* src_g, const float* src_b,
    int src_w, int src_h, int factor,
    int& dst_w, int& dst_h)
{
    dst_w = src_w / factor;
    dst_h = src_h / factor;
    std::vector<float> dst(static_cast<size_t>(dst_w) * dst_h * 4, 0.0f);
    const float inv = 1.0f / static_cast<float>(factor * factor);

    for (int y = 0; y < dst_h; ++y) {
        for (int x = 0; x < dst_w; ++x) {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            for (int dy = 0; dy < factor; ++dy) {
                for (int dx = 0; dx < factor; ++dx) {
                    const size_t si =
                        (static_cast<size_t>(y) * factor + dy) * src_w +
                        (static_cast<size_t>(x) * factor + dx);
                    r += src_r[si];
                    g += src_g[si];
                    b += src_b[si];
                }
            }
            const size_t di = (static_cast<size_t>(y) * dst_w + x) * 4;
            dst[di + 0] = r * inv;
            dst[di + 1] = g * inv;
            dst[di + 2] = b * inv;
            dst[di + 3] = 1.0f;
        }
    }
    return dst;
}

/// Box-downsample an RGBA interleaved float image by integer factor.
std::vector<float> downsample_rgba(
    const float* src, int src_w, int src_h, int factor,
    int& dst_w, int& dst_h)
{
    dst_w = src_w / factor;
    dst_h = src_h / factor;
    std::vector<float> dst(static_cast<size_t>(dst_w) * dst_h * 4, 0.0f);
    const float inv = 1.0f / static_cast<float>(factor * factor);

    for (int y = 0; y < dst_h; ++y) {
        for (int x = 0; x < dst_w; ++x) {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            for (int dy = 0; dy < factor; ++dy) {
                for (int dx = 0; dx < factor; ++dx) {
                    const size_t si =
                        ((static_cast<size_t>(y) * factor + dy) * src_w +
                         (static_cast<size_t>(x) * factor + dx)) * 4;
                    r += src[si + 0];
                    g += src[si + 1];
                    b += src[si + 2];
                    a += src[si + 3];
                }
            }
            const size_t di = (static_cast<size_t>(y) * dst_w + x) * 4;
            dst[di + 0] = r * inv;
            dst[di + 1] = g * inv;
            dst[di + 2] = b * inv;
            dst[di + 3] = a * inv;
        }
    }
    return dst;
}

/// Load EXR using low-level API, keeping channels as float32.
/// On success, fills out_rgba (RGBA interleaved float), out_w, out_h and returns true.
bool load_exr_low_level(const char* path, int max_width,
                        std::vector<float>& out_rgba, int& out_w, int& out_h)
{
    EXRVersion version;
    int ret = ParseEXRVersionFromFile(&version, path);
    if (ret != TINYEXR_SUCCESS) {
        std::fprintf(stderr, "Starfield: ParseEXRVersionFromFile failed (%d)\n", ret);
        return false;
    }

    EXRHeader header;
    InitEXRHeader(&header);
    const char* err = nullptr;
    ret = ParseEXRHeaderFromFile(&header, &version, path, &err);
    if (ret != TINYEXR_SUCCESS) {
        std::fprintf(stderr, "Starfield: ParseEXRHeaderFromFile failed: %s\n",
                     err ? err : "(null)");
        if (err) FreeEXRErrorMessage(err);
        return false;
    }

    // Request float32 output for all channels to simplify downstream processing.
    for (int c = 0; c < header.num_channels; ++c) {
        header.requested_pixel_types[c] = TINYEXR_PIXELTYPE_FLOAT;
    }

    EXRImage image;
    InitEXRImage(&image);
    ret = LoadEXRImageFromFile(&image, &header, path, &err);
    if (ret != TINYEXR_SUCCESS) {
        std::fprintf(stderr, "Starfield: LoadEXRImageFromFile failed: %s\n",
                     err ? err : "(null)");
        if (err) FreeEXRErrorMessage(err);
        FreeEXRHeader(&header);
        return false;
    }

    std::fprintf(stderr, "Starfield: loaded %dx%d, %d channels\n",
                 image.width, image.height, image.num_channels);

    // Find R, G, B channel indices (channels may be in any order).
    int idx_r = -1, idx_g = -1, idx_b = -1;
    for (int c = 0; c < header.num_channels; ++c) {
        const char* name = header.channels[c].name;
        if (name[0] == 'R' && name[1] == '\0') idx_r = c;
        else if (name[0] == 'G' && name[1] == '\0') idx_g = c;
        else if (name[0] == 'B' && name[1] == '\0') idx_b = c;
    }
    // Fallback to 0,1,2 if named channels not found (e.g. unnamed single-part)
    if (idx_r < 0 && image.num_channels >= 3) { idx_r = 0; idx_g = 1; idx_b = 2; }
    if (idx_r < 0) {
        std::fprintf(stderr, "Starfield: could not identify RGB channels\n");
        FreeEXRImage(&image);
        FreeEXRHeader(&header);
        return false;
    }

    const int src_w = image.width;
    const int src_h = image.height;
    const float* ch_r = reinterpret_cast<const float*>(image.images[idx_r]);
    const float* ch_g = reinterpret_cast<const float*>(image.images[idx_g]);
    const float* ch_b = reinterpret_cast<const float*>(image.images[idx_b]);

    // Compute downscale factor: smallest power of 2 that gets us to max_width
    int factor = 1;
    while ((src_w / factor) > max_width) factor *= 2;

    int dw = 0, dh = 0;
    std::vector<float> rgba = downsample_planar_to_rgba(
        ch_r, ch_g, ch_b, src_w, src_h, factor, dw, dh);

    std::fprintf(stderr, "Starfield: downsampled %dx%d -> %dx%d (factor %d)\n",
                 src_w, src_h, dw, dh, factor);

    FreeEXRImage(&image);
    FreeEXRHeader(&header);

    out_rgba = std::move(rgba);
    out_w = dw;
    out_h = dh;
    return true;
}

/// Simple LoadEXR fallback (returns RGBA float, handles further downsampling).
bool load_exr_simple(const char* path, int max_width,
                     std::vector<float>& out_rgba, int& out_w, int& out_h)
{
    float* rgba = nullptr;
    int w = 0, h = 0;
    const char* err = nullptr;

    const int ret = LoadEXR(&rgba, &w, &h, path, &err);
    if (ret != TINYEXR_SUCCESS) {
        std::fprintf(stderr, "Starfield: LoadEXR failed: %s\n", err ? err : "(null)");
        if (err) FreeEXRErrorMessage(err);
        return false;
    }

    std::fprintf(stderr, "Starfield: LoadEXR loaded %dx%d\n", w, h);

    // Downsample by factors of 2 until width <= max_width
    const float* working = rgba;
    int cur_w = w, cur_h = h;
    std::vector<float> prev, curr;

    while (cur_w > max_width) {
        int new_w = 0, new_h = 0;
        curr = downsample_rgba(working, cur_w, cur_h, 2, new_w, new_h);
        if (working == rgba) {
            free(rgba);
            rgba = nullptr;
        }
        prev = std::move(curr);
        working = prev.data();
        cur_w = new_w;
        cur_h = new_h;
        std::fprintf(stderr, "Starfield: downsampled to %dx%d\n", cur_w, cur_h);
    }

    if (rgba) {
        out_rgba.assign(rgba, rgba + static_cast<size_t>(cur_w) * cur_h * 4);
        free(rgba);
    } else {
        out_rgba = std::move(prev);
    }
    out_w = cur_w;
    out_h = cur_h;
    return true;
}

} // namespace

bool load_starfield(const std::string& path, int max_width, Starfield& out) {
    out = Starfield{};
    if (path.empty()) return false;

    std::fprintf(stderr, "Starfield: loading %s (max_width=%d)...\n",
                 path.c_str(), max_width);

    std::vector<float> rgba;
    int cur_w = 0, cur_h = 0;

    // Use the low-level API first: it avoids allocating a full float32 RGBA buffer
    // for the entire source image by allowing us to downsample from planar channels.
    // Fall back to simple LoadEXR if the low-level path fails.
    if (!load_exr_low_level(path.c_str(), max_width, rgba, cur_w, cur_h)) {
        std::fprintf(stderr, "Starfield: low-level load failed, trying simple path...\n");
        if (!load_exr_simple(path.c_str(), max_width, rgba, cur_w, cur_h)) {
            return false;
        }
    }

    // Upload to CUDA array as float4
    cudaChannelFormatDesc desc =
        cudaCreateChannelDesc(32, 32, 32, 32, cudaChannelFormatKindFloat);
    cudaError_t cerr = cudaMallocArray(&out.d_array, &desc, cur_w, cur_h);
    if (cerr != cudaSuccess) {
        std::fprintf(stderr, "Starfield: cudaMallocArray failed: %s\n",
                     cudaGetErrorString(cerr));
        return false;
    }

    cerr = cudaMemcpy2DToArray(
        out.d_array, 0, 0,
        rgba.data(),
        static_cast<size_t>(cur_w) * 4 * sizeof(float),
        static_cast<size_t>(cur_w) * 4 * sizeof(float),
        cur_h,
        cudaMemcpyHostToDevice);
    if (cerr != cudaSuccess) {
        std::fprintf(stderr, "Starfield: cudaMemcpy2DToArray failed: %s\n",
                     cudaGetErrorString(cerr));
        cudaFreeArray(out.d_array);
        out.d_array = nullptr;
        return false;
    }

    // Free host buffer before creating texture (GPU copy already done)
    rgba.clear();
    rgba.shrink_to_fit();

    cudaResourceDesc res{};
    res.resType = cudaResourceTypeArray;
    res.res.array.array = out.d_array;

    cudaTextureDesc td{};
    td.addressMode[0] = cudaAddressModeWrap;   // wrap in longitude
    td.addressMode[1] = cudaAddressModeClamp;  // clamp at poles
    td.filterMode     = cudaFilterModeLinear;
    td.readMode       = cudaReadModeElementType;
    td.normalizedCoords = 1;

    cerr = cudaCreateTextureObject(&out.tex, &res, &td, nullptr);
    if (cerr != cudaSuccess) {
        std::fprintf(stderr, "Starfield: cudaCreateTextureObject failed: %s\n",
                     cudaGetErrorString(cerr));
        cudaFreeArray(out.d_array);
        out = Starfield{};
        return false;
    }

    out.width  = cur_w;
    out.height = cur_h;
    std::fprintf(stderr,
        "Starfield: %dx%d on GPU (~%zu MB VRAM)\n",
        out.width, out.height,
        static_cast<size_t>(out.width) * out.height * 16 / (1024 * 1024));
    return true;
}

void destroy_starfield(Starfield& sf) {
    if (sf.tex)     cudaDestroyTextureObject(sf.tex);
    if (sf.d_array) cudaFreeArray(sf.d_array);
    sf = Starfield{};
}

} // namespace bhr
