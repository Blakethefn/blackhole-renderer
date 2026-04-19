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

TEST_CASE("Dual integrator: off-axis ray reaches same hit type") {
    bhr::CameraParams cam{};
    cam.r_cam = 50.0f;
    cam.theta_cam_deg = 85.0f;
    cam.phi_cam_deg = 0.0f;
    cam.fov_deg = 35.0f;
    cam.width = 257; cam.height = 257;

    bhr::IntegratorConfig cfg{};
    cfg.disk_r_inner = 6.0f;
    cfg.disk_r_outer = 20.0f;

    // Sample 9 pixel locations. For each, both integrators should agree on HitType.
    const int samples[9][2] = {
        {64,128}, {128,64}, {128,128}, {128,192}, {192,128},
        {64,64},  {64,192}, {192,64},  {192,192}
    };
    int agree = 0;
    int total = 0;
    for (const auto& px : samples) {
        bhr::GeodesicState s{}; bhr::Conserved c{};
        bhr::camera_ray(px[0], px[1], cam.width, cam.height, cam, 0.5f, s, c);
        const auto h_rk45 = bhr::integrate_rk45(s, 0.5f, c, cfg);
        const auto h_geo  = bhr::integrate_geokerr(s, 0.5f, c, cfg);
        CAPTURE(px[0]); CAPTURE(px[1]);
        MESSAGE("px=(" << px[0] << "," << px[1] << ") RK45=" << (int)h_rk45.type << " Geo=" << (int)h_geo.type);
        if (h_rk45.type == h_geo.type) ++agree;
        ++total;
        CHECK(h_rk45.type == h_geo.type);
    }
    MESSAGE("Agreement: " << agree << "/" << total << " pixels");
}
