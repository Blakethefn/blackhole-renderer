#include "doctest.h"
#include "bhr/params.hpp"
#include "bhr/kerr.hpp"
#include <initializer_list>
#include <limits>
#include <type_traits>

TEST_CASE("Parameter validation accepts defaults and existing golden scenes") {
    CHECK(std::is_trivially_copyable<bhr::RenderParams>::value);
    bhr::RenderParams p;
    CHECK(p.validate());
    CHECK(bhr::validation_error(p) == nullptr);
    p.disk.r_inner = 100.0f;
    p.disk.r_outer = 200.0f;
    CHECK(p.validate()); // Camera need not be beyond the disk outer edge.
    p.spin = 0.9f;
    p.disk.r_inner = 2.32f; // Existing rounded ISCO fixture.
    CHECK(p.validate());
    p.spin = 0.999f;
    p.disk.r_inner = bhr::r_isco(p.spin);
    CHECK(p.validate());
    p.disk.brightness = 0.0f;
    CHECK(p.validate());
}

TEST_CASE("Parameter validation rejects every nonfinite scene scalar") {
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()}) {
        for (int field = 0; field < 9; ++field) {
            bhr::RenderParams p;
            float* fields[] = {&p.spin, &p.camera.r_cam, &p.camera.theta_cam_deg,
                &p.camera.phi_cam_deg, &p.camera.fov_deg, &p.disk.r_inner,
                &p.disk.r_outer, &p.disk.peak_temp_K, &p.disk.brightness};
            *fields[field] = invalid;
            CAPTURE(field);
            CHECK_FALSE(p.validate());
            CHECK(bhr::validation_error(p) != nullptr);
        }
    }
}

TEST_CASE("Parameter validation rejects unsupported physical and image ranges") {
    bhr::RenderParams p;
    SUBCASE("negative spin") { p.spin = -0.1f; }
    SUBCASE("extremal spin") { p.spin = 1.0f; }
    SUBCASE("camera on horizon") { p.camera.r_cam = bhr::horizon_radius(p.spin); }
    SUBCASE("camera under horizon") { p.camera.r_cam = 1.0f; }
    SUBCASE("camera beyond integration domain") { p.camera.r_cam = 1001.0f; }
    SUBCASE("north pole") { p.camera.theta_cam_deg = 0.0f; }
    SUBCASE("south pole") { p.camera.theta_cam_deg = 180.0f; }
    SUBCASE("negative inclination") { p.camera.theta_cam_deg = -1.0f; }
    SUBCASE("zero FOV") { p.camera.fov_deg = 0.0f; }
    SUBCASE("large FOV") { p.camera.fov_deg = 179.0f; }
    SUBCASE("negative width") { p.camera.width = -1; }
    SUBCASE("zero height") { p.camera.height = 0; }
    SUBCASE("oversized width") { p.camera.width = std::numeric_limits<int>::max(); }
    SUBCASE("oversized height") { p.camera.height = 16385; }
    SUBCASE("inverted disk") { p.disk.r_inner = p.disk.r_outer + 1.0f; }
    SUBCASE("empty disk") { p.disk.r_inner = p.disk.r_outer; }
    SUBCASE("disk inside ISCO") { p.disk.r_inner = 4.0f; }
    SUBCASE("disk inside horizon") { p.disk.r_inner = 1.0f; }
    SUBCASE("disk beyond integration domain") { p.disk.r_outer = 1001.0f; }
    SUBCASE("negative temperature") { p.disk.peak_temp_K = -1.0f; }
    SUBCASE("zero temperature") { p.disk.peak_temp_K = 0.0f; }
    SUBCASE("negative brightness") { p.disk.brightness = -1.0f; }
    SUBCASE("invalid integrator") { p.integrator = static_cast<bhr::IntegratorKind>(99); }
    CHECK_FALSE(p.validate());
    REQUIRE(bhr::validation_error(p) != nullptr);
    CHECK(bhr::validation_error(p)[0] != '\0');
}
