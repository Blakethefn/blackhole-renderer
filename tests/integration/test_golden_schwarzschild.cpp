#include "doctest.h"
#include "bhr/renderer.hpp"
#include "bhr/params.hpp"
#include "bhr/image.hpp"
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstdio>

static std::vector<std::uint8_t> read_file(const char* path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const size_t n = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<std::uint8_t> out(n);
    in.read(reinterpret_cast<char*>(out.data()), n);
    return out;
}

TEST_CASE("Golden Schwarzschild 512x512 matches saved reference") {
    bhr::RenderParams params{};
    params.spin = 0.0f;
    params.camera.width = 512;
    params.camera.height = 512;
    params.camera.r_cam = 50.0f;
    params.camera.theta_cam_deg = 90.0f;
    params.camera.fov_deg = 20.0f;
    params.disk.r_inner = 100.0f;
    params.disk.r_outer = 200.0f;

    bhr::Image img;
    bhr::render(params, img);

    const char* tmp = "/tmp/bhr_golden_candidate.png";
    REQUIRE(bhr::write_png(img, tmp));

    auto got = read_file(tmp);
    auto want = read_file("tests/golden/schwarzschild_512.png");
    REQUIRE(!got.empty());
    REQUIRE(!want.empty());

    CHECK(got.size() == want.size());
    CHECK(got == want);

    std::remove(tmp);
}
