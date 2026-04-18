#include "doctest.h"
#include "bhr/disk_physics.hpp"
#include "bhr/kerr.hpp"
#include <cmath>

TEST_CASE("Disk temperature goes to 0 at inner edge and peaks near r_in * 1.36") {
    const float r_in = 6.0f;
    const float T_peak = 1e7f;

    CHECK(bhr::disk_temperature(r_in, r_in, T_peak) == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(bhr::disk_temperature(1.361f * r_in, r_in, T_peak) == doctest::Approx(T_peak).epsilon(0.05));

    CHECK(bhr::disk_temperature(10.0f * r_in, r_in, T_peak) < T_peak);
}

TEST_CASE("Keplerian angular velocity at large r matches Newtonian limit") {
    const float r = 100.0f;
    CHECK(bhr::kepler_omega(r, 0.0f) == doctest::Approx(powf(r, -1.5f)).epsilon(1e-5));
    CHECK(bhr::kepler_omega(r, 0.9f) == doctest::Approx(powf(r, -1.5f)).epsilon(1e-3));
}

TEST_CASE("Disk 4-velocity is timelike (u^t > 0) at valid radii") {
    float ut, up;
    bhr::disk_4velocity(10.0f, 0.0f, ut, up);
    CHECK(ut > 0.0f);
    CHECK(up > 0.0f);

    bhr::disk_4velocity(6.0f, 0.0f, ut, up);
    CHECK(ut > 0.0f);
}
