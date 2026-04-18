#include "doctest.h"
#include "bhr/geodesic.hpp"
#include "bhr/kerr.hpp"
#include "bhr/camera.hpp"
#include <cmath>

TEST_CASE("Schwarzschild photon sphere: circular orbit at r=3, L²=27 E²") {
    bhr::Conserved c{};
    c.E = 1.0f;
    c.Lz = std::sqrt(27.0f);
    c.Q = 0.0f;
    const float r = 3.0f;
    const float a = 0.0f;
    CHECK(bhr::potential_R(r, a, c) == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(bhr::dR_dr(r, a, c) == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("RHS smoke test: velocity passes through correctly") {
    bhr::GeodesicState s{};
    s.r = 100.0f;
    s.theta = static_cast<float>(M_PI) / 2.0f;
    s.phi = 0.0f;
    s.t = 0.0f;
    // Arbitrary initial velocity — we only check passthrough and polar symmetry
    s.dr_dlam = -1.0e4f;
    s.dth_dlam = 0.0f;
    bhr::Conserved c{ .E = 1.0f, .Lz = 0.0f, .Q = 0.0f };
    auto d = bhr::rhs(s, 0.0f, c);
    // d.r must equal dr_dlam (first-order passthrough)
    CHECK(d.r == doctest::Approx(-1.0e4f));
    // dth_dlam should be zero (equatorial, no polar kick for Lz=0, Q=0, a=0)
    CHECK(d.theta == doctest::Approx(0.0f));
}

TEST_CASE("Photon dropped radially from r=5, a=0, aimed at horizon → kHorizon") {
    // Start close to horizon so the inward momentum dominates before adaptive
    // step control saturates. Physical IC: dr_dlam = -sqrt(R(r)) = -r^2 for E=1,Lz=0,a=0.
    // Exact Mino-time solution: r(lam)=1/(1/5+lam), reaches horizon at lam≈0.3.
    bhr::GeodesicState s{};
    s.r = 5.0f;
    s.theta = static_cast<float>(M_PI) / 2.0f;
    s.phi = 0.0f;
    s.t = 0.0f;
    s.dr_dlam = -25.0f;   // -sqrt(R(5)) = -5^2
    s.dth_dlam = 0.0f;
    bhr::Conserved c{ .E = 1.0f, .Lz = 0.0f, .Q = 0.0f };
    bhr::IntegratorConfig cfg{};
    cfg.disk_r_inner = 100.0f;
    cfg.disk_r_outer = 200.0f;
    cfg.h_init = 0.01f;
    cfg.h_min  = 1e-6f;
    cfg.max_steps = 100000;
    auto hit = bhr::integrate_rk45(s, 0.0f, c, cfg);
    CHECK(hit.type == bhr::HitType::kHorizon);
}

TEST_CASE("Photon with large impact parameter escapes to infinity") {
    // For Schwarzschild (a=0), critical impact parameter b_crit = 3*sqrt(3).
    // Lz=100 >> b_crit so the photon must escape.  Start outside the radial
    // turning point (r_turn≈99 for Lz=100, E=1) going outward.
    // R(150, Lz=100) > 0, dr_dlam = +sqrt(R(150)) ≈ 16860.
    bhr::GeodesicState s{};
    s.r = 150.0f;
    s.theta = static_cast<float>(M_PI) / 2.0f;
    s.phi = 0.0f;
    s.t = 0.0f;
    s.dr_dlam = 16860.0f;
    s.dth_dlam = 0.0f;
    bhr::Conserved c{ .E = 1.0f, .Lz = 100.0f, .Q = 0.0f };
    bhr::IntegratorConfig cfg{};
    cfg.r_max = 1000.0f;
    cfg.disk_r_inner = 200.0f;
    cfg.disk_r_outer = 300.0f;
    auto hit = bhr::integrate_rk45(s, 0.0f, c, cfg);
    CHECK(hit.type == bhr::HitType::kEscape);
}

TEST_CASE("Kerr a=0.9: camera_ray produces finite conserved quantities") {
    bhr::CameraParams cam{};
    cam.r_cam = 20.0f;
    cam.theta_cam_deg = 90.0f;
    cam.phi_cam_deg = 0.0f;
    cam.fov_deg = 10.0f;
    cam.width = 16;
    cam.height = 16;

    bhr::GeodesicState s{};
    bhr::Conserved c{};
    bhr::camera_ray(8, 8, 16, 16, cam, 0.9f, s, c);

    // All conserved quantities must be finite (no division by zero, no NaN)
    CHECK(std::isfinite(c.E));
    CHECK(std::isfinite(c.Lz));
    CHECK(std::isfinite(c.Q));
    CHECK(std::isfinite(s.dr_dlam));
    CHECK(std::isfinite(s.dth_dlam));

    // E should be positive (future-directed photon)
    CHECK(c.E > 0.0f);

    // dphi/dlam at this state uses frame-dragging — verify it's finite
    const float sin_th = 1.0f;
    const float dphi = bhr::dphi_dlam(s.r, sin_th, 0.9f, c);
    CHECK(std::isfinite(dphi));
}

TEST_CASE("Kerr photon dropped from r=5, a=0.9 still falls to horizon") {
    // Radial infall in Kerr with a=0.9, E=1, Lz=0, Q=0.
    // dr_dlam = -sqrt(R(5)) = -sqrt(653.35) ≈ -25.56 (same setup as Schwarzschild test).
    // Confirms the Kerr integrator (a>0) returns kHorizon for an inward radial null geodesic.
    bhr::GeodesicState s{};
    s.r = 5.0f;
    s.theta = static_cast<float>(M_PI) / 2.0f;
    s.phi = 0.0f;
    s.t = 0.0f;
    s.dr_dlam = -25.56f;   // -sqrt(R(5)) for a=0.9, E=1, Lz=0
    s.dth_dlam = 0.0f;
    bhr::Conserved c{ .E = 1.0f, .Lz = 0.0f, .Q = 0.0f };

    bhr::IntegratorConfig cfg{};
    cfg.disk_r_inner = 100.0f;  // no disk in path
    cfg.disk_r_outer = 200.0f;
    cfg.max_steps = 100000;
    cfg.h_init = 0.01f;
    cfg.h_min = 1e-6f;

    auto hit = bhr::integrate_rk45(s, 0.9f, c, cfg);
    CHECK(hit.type == bhr::HitType::kHorizon);
}
