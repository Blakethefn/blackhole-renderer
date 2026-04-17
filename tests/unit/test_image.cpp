#include "doctest.h"
#include "bhr/image.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdio>

TEST_CASE("Image default-constructs empty") {
    bhr::Image img{};
    CHECK(img.width == 0);
    CHECK(img.height == 0);
    CHECK(img.rgba.empty());
}

TEST_CASE("Image::allocate sets size and zeroes memory") {
    bhr::Image img;
    img.allocate(4, 3);
    CHECK(img.width == 4);
    CHECK(img.height == 3);
    CHECK(img.rgba.size() == static_cast<size_t>(4 * 3 * 4));
    for (auto b : img.rgba) CHECK(b == 0);
}

TEST_CASE("Image::set_pixel writes RGBA at coord") {
    bhr::Image img;
    img.allocate(2, 2);
    img.set_pixel(1, 0, 10, 20, 30, 40);
    CHECK(img.rgba[(0 * 2 + 1) * 4 + 0] == 10);
    CHECK(img.rgba[(0 * 2 + 1) * 4 + 1] == 20);
    CHECK(img.rgba[(0 * 2 + 1) * 4 + 2] == 30);
    CHECK(img.rgba[(0 * 2 + 1) * 4 + 3] == 40);
}

TEST_CASE("write_png produces a valid PNG file") {
    namespace fs = std::filesystem;
    bhr::Image img;
    img.allocate(8, 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            img.set_pixel(x, y, x * 32, y * 32, 0, 255);

    fs::path tmp = fs::temp_directory_path() / "bhr_test.png";
    REQUIRE(bhr::write_png(img, tmp.string()));

    std::ifstream in(tmp, std::ios::binary);
    REQUIRE(in);
    unsigned char sig[8];
    in.read(reinterpret_cast<char*>(sig), 8);
    CHECK(sig[0] == 0x89);
    CHECK(sig[1] == 'P');
    CHECK(sig[2] == 'N');
    CHECK(sig[3] == 'G');

    std::remove(tmp.c_str());
}
