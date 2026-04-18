#include "bhr/geodesic.hpp"
#include "doctest.h"

TEST_CASE("integrate_geokerr: signature contract — HitType is one of the enum values") {
    bhr::GeodesicState s{};
    s.r = 50.0f; s.theta = 1.57f; s.phi = 0.0f; s.t = 0.0f;
    s.dr_dlam = -100.0f; s.dth_dlam = 0.0f;
    const bhr::Conserved c{1.0f, 0.0f, 0.0f};
    bhr::IntegratorConfig cfg{};
    cfg.disk_r_inner = 6.0f;
    cfg.disk_r_outer = 20.0f;
    const auto hit = bhr::integrate_geokerr(s, 0.0f, c, cfg);
    CHECK((hit.type == bhr::HitType::kHorizon
           || hit.type == bhr::HitType::kDisk
           || hit.type == bhr::HitType::kEscape
           || hit.type == bhr::HitType::kUnknown));
}
