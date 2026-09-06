#include "doctest.h"
#include "bhr/geodesic.hpp"
#include "bhr/camera.hpp"
#include "bhr/params.hpp"

TEST_CASE("Dual integrator: center ray of Schwarzschild face-on hits horizon for both backends") {
    bhr::CameraParams cam{};
    cam.r_cam = 50.0f;
    cam.theta_cam_deg = 0.0f;      // face-on, looking down the z-axis
    cam.phi_cam_deg = 0.0f;
    cam.fov_deg = 20.0f;
    cam.width = 257; cam.height = 257;
    bhr::GeodesicState s{}; bhr::Conserved c{};
    bhr::camera_ray(128, 128, cam.width, cam.height, cam, 0.0f, s, c);

    bhr::IntegratorConfig cfg{};
    cfg.disk_r_inner = 6.0f;
    cfg.disk_r_outer = 20.0f;
    const auto h_rk45 = bhr::integrate_rk45(s, 0.0f, c, cfg);
    const auto h_geo  = bhr::integrate_geokerr(s, 0.0f, c, cfg);

    MESSAGE("RK45 type=" << (int)h_rk45.type << " r=" << h_rk45.r);
    MESSAGE("Geokerr type=" << (int)h_geo.type << " r=" << h_geo.r);

    CHECK(h_rk45.type == h_geo.type);
    CHECK(h_rk45.type == bhr::HitType::kHorizon);
}

TEST_CASE("Dual integrator: off-axis rays match physical hit classifications") {
    bhr::CameraParams cam{};
    cam.r_cam = 50.0f;
    cam.theta_cam_deg = 85.0f;
    cam.phi_cam_deg = 0.0f;
    cam.fov_deg = 35.0f;
    cam.width = 257; cam.height = 257;

    bhr::IntegratorConfig cfg{};
    cfg.disk_r_inner = 6.0f;
    cfg.disk_r_outer = 20.0f;

    struct Sample { int px; int py; bhr::HitType expected; };
    // Independently propagated with the separated radial/polar equations.
    // This is deliberately stricter than backend agreement.
    const Sample samples[] = {
        {64,128, bhr::HitType::kDisk}, {128,64, bhr::HitType::kDisk},
        {128,128, bhr::HitType::kHorizon}, {128,192, bhr::HitType::kDisk},
        {192,128, bhr::HitType::kDisk}, {64,64, bhr::HitType::kEscape},
        {64,192, bhr::HitType::kEscape}, {192,64, bhr::HitType::kEscape},
        {192,192, bhr::HitType::kEscape},
    };
    for (const auto& sample : samples) {
        bhr::GeodesicState s{}; bhr::Conserved c{};
        bhr::camera_ray(sample.px, sample.py, cam.width, cam.height, cam, 0.5f, s, c);
        const auto h_rk45 = bhr::integrate_rk45(s, 0.5f, c, cfg);
        const auto h_geo  = bhr::integrate_geokerr(s, 0.5f, c, cfg);
        CAPTURE(sample.px); CAPTURE(sample.py); CAPTURE((int)sample.expected);
        MESSAGE("px=(" << sample.px << "," << sample.py << ") expected=" << (int)sample.expected
            << " RK45=" << (int)h_rk45.type << " r=" << h_rk45.r << " lambda=" << h_rk45.lambda
            << " Geo=" << (int)h_geo.type << " r=" << h_geo.r << " lambda=" << h_geo.lambda
            << " roots=" << h_geo.steps);
        CHECK(h_rk45.type == sample.expected);
        CHECK(h_geo.type == sample.expected);
        CHECK(h_rk45.type == h_geo.type);
    }
}
