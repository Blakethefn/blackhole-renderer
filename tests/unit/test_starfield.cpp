#include "doctest.h"
#include "bhr/starfield.hpp"
#include "tinyexr.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

namespace {

struct ExrFixture {
    std::filesystem::path path = std::filesystem::temp_directory_path()
        / ("bhr-starfield-" + std::to_string(std::chrono::steady_clock::now()
            .time_since_epoch().count()) + ".exr");
    ~ExrFixture() {
        std::error_code error;
        std::filesystem::remove(path, error);
        CHECK_FALSE(error);
    }
};

struct OwnedStarfield {
    bhr::Starfield value;
    ~OwnedStarfield() { bhr::destroy_starfield(value); }
};

void save_fixture(const ExrFixture& file, const std::vector<const char*>& names,
                  int width = 2, int height = 2, bool tiled = false,
                  int pixel_type = TINYEXR_PIXELTYPE_FLOAT, float value = 1.0f) {
    EXRHeader header;
    InitEXRHeader(&header);
    EXRImage image;
    InitEXRImage(&image);
    const int channel_count = static_cast<int>(names.size());
    std::vector<EXRChannelInfo> channels(names.size());
    std::vector<int> source_types(names.size(), TINYEXR_PIXELTYPE_FLOAT);
    std::vector<int> saved_types(names.size(), pixel_type);
    std::vector<float> pixels(size_t(width) * height, value);
    std::vector<unsigned char*> planes(names.size(), reinterpret_cast<unsigned char*>(pixels.data()));
    for (size_t i = 0; i < names.size(); ++i) {
        std::strncpy(channels[i].name, names[i], sizeof(channels[i].name) - 1);
        channels[i].x_sampling = 1;
        channels[i].y_sampling = 1;
        if (pixel_type == TINYEXR_PIXELTYPE_UINT) source_types[i] = pixel_type;
    }
    header.channels = channels.data();
    header.num_channels = channel_count;
    header.pixel_types = source_types.data();
    header.requested_pixel_types = saved_types.data();
    header.compression_type = TINYEXR_COMPRESSIONTYPE_NONE;
    image.width = width;
    image.height = height;
    image.num_channels = channel_count;
    image.images = planes.data();
    EXRTile tile{};
    if (tiled) {
        header.tiled = 1;
        header.tile_size_x = width;
        header.tile_size_y = height;
        tile.width = width;
        tile.height = height;
        tile.images = planes.data();
        image.images = nullptr;
        image.tiles = &tile;
        image.num_tiles = 1;
    }
    const char* error = nullptr;
    const int status = SaveEXRImageToFile(&image, &header, file.path.c_str(), &error);
    if (error) {
        MESSAGE(error);
        FreeEXRErrorMessage(error);
    }
    REQUIRE(status == TINYEXR_SUCCESS);
}

} // namespace

TEST_CASE("Starfield rejects incomplete RGB channels") {
    ExrFixture file;
    save_fixture(file, {"R"});
    OwnedStarfield field;
    CHECK_FALSE(bhr::load_starfield(file.path.string(), 2, field.value));
    CHECK_FALSE(field.value.is_valid());
    CHECK(field.value.d_array == nullptr);
}

TEST_CASE("Starfield rejects unsupported EXR formats and width limits") {
    ExrFixture file;
    int maximum_width = 2;
    bool tiled = false;
    int pixel_type = TINYEXR_PIXELTYPE_FLOAT;
    float value = 1.0f;
    SUBCASE("tiled") { tiled = true; }
    SUBCASE("integer channels") { pixel_type = TINYEXR_PIXELTYPE_UINT; }
    SUBCASE("zero width limit") { maximum_width = 0; }
    SUBCASE("negative width limit") { maximum_width = -1; }
    SUBCASE("nonfinite radiance") { value = std::numeric_limits<float>::infinity(); }
    save_fixture(file, {"B", "G", "R"}, 2, 2, tiled, pixel_type, value);
    OwnedStarfield field;
    CHECK_FALSE(bhr::load_starfield(file.path.string(), maximum_width, field.value));
    CHECK_FALSE(field.value.is_valid());
    CHECK(field.value.d_array == nullptr);
}

TEST_CASE("Starfield loads float and half RGB and keeps thin images nonempty") {
    ExrFixture file;
    int pixel_type = TINYEXR_PIXELTYPE_FLOAT;
    SUBCASE("half channels") { pixel_type = TINYEXR_PIXELTYPE_HALF; }
    SUBCASE("float channels") {}
    save_fixture(file, {"B", "G", "R"}, 7, 1, false, pixel_type);
    OwnedStarfield field;
    REQUIRE(bhr::load_starfield(file.path.string(), 2, field.value));
    CHECK(field.value.width > 0);
    CHECK(field.value.width <= 2);
    CHECK(field.value.height == 1);
    std::vector<float4> pixels(size_t(field.value.width) * field.value.height);
    REQUIRE(cudaMemcpy2DFromArray(pixels.data(), size_t(field.value.width) * sizeof(float4),
        field.value.d_array, 0, 0, size_t(field.value.width) * sizeof(float4),
        field.value.height, cudaMemcpyDeviceToHost) == cudaSuccess);
    for (const auto& pixel : pixels) {
        CHECK(pixel.x == 1.0f);
        CHECK(pixel.y == 1.0f);
        CHECK(pixel.z == 1.0f);
        CHECK(pixel.w == 1.0f);
    }
    bhr::destroy_starfield(field.value);
    CHECK(field.value.tex == 0);
    CHECK(field.value.d_array == nullptr);
    CHECK(field.value.width == 0);
    bhr::destroy_starfield(field.value); // Empty destruction is harmless.
}

TEST_CASE("Starfield refuses to overwrite existing CUDA ownership") {
    ExrFixture file;
    save_fixture(file, {"B", "G", "R"});
    OwnedStarfield field;
    REQUIRE(bhr::load_starfield(file.path.string(), 2, field.value));
    const auto texture = field.value.tex;
    const auto array = field.value.d_array;
    CHECK_FALSE(bhr::load_starfield("", 2, field.value));
    CHECK(field.value.tex == texture);
    CHECK(field.value.d_array == array);
}
