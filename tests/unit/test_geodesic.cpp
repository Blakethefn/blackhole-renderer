#include "doctest.h"
#include "bhr/geodesic.hpp"
#include "bhr/kerr.hpp"
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
