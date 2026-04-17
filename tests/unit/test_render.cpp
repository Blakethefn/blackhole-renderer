#include "doctest.h"
#include "bhr/renderer.hpp"
#include "bhr/params.hpp"
#include "bhr/image.hpp"

TEST_CASE("render produces a non-empty image at tiny resolution") {
    bhr::RenderParams params{};
    params.spin = 0.0f;
    params.camera.width = 32;
    params.camera.height = 32;
    params.camera.r_cam = 50.0f;
    params.camera.theta_cam_deg = 90.0f;
    params.camera.fov_deg = 20.0f;
    params.disk.r_inner = 100.0f;
    params.disk.r_outer = 200.0f;

    bhr::Image img;
    bhr::render(params, img);

    REQUIRE(img.width == 32);
    REQUIRE(img.height == 32);
    REQUIRE(img.rgba.size() == 32 * 32 * 4);

    // Center pixel should be black (inside shadow)
    const size_t center = (16 * 32 + 16) * 4;
    CHECK(img.rgba[center + 0] == 0);
    CHECK(img.rgba[center + 1] == 0);
    CHECK(img.rgba[center + 2] == 0);
    CHECK(img.rgba[center + 3] == 255);

    // Corner pixel should NOT be black
    const size_t corner = (0 * 32 + 0) * 4;
    const bool corner_nonblack = img.rgba[corner + 0] > 0
                               || img.rgba[corner + 1] > 0
                               || img.rgba[corner + 2] > 0;
    CHECK(corner_nonblack);
}
