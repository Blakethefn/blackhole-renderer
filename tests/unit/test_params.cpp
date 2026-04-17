#include "doctest.h"
#include "bhr/params.hpp"

TEST_CASE("RenderParams defaults match design spec") {
    bhr::RenderParams p{};
    CHECK(p.spin == doctest::Approx(0.0f));
    CHECK(p.camera.r_cam == doctest::Approx(50.0f));
    CHECK(p.camera.theta_cam_deg == doctest::Approx(85.0f));
    CHECK(p.camera.fov_deg == doctest::Approx(35.0f));
    CHECK(p.camera.width == 1920);
    CHECK(p.camera.height == 1080);
    CHECK(p.disk.r_inner == doctest::Approx(6.0f));
    CHECK(p.disk.r_outer == doctest::Approx(20.0f));
    CHECK(p.enable_starfield == false);
    CHECK(p.enable_doppler == true);
}
