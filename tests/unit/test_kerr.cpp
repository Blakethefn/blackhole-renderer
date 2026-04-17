#include "doctest.h"
#include "bhr/kerr.hpp"
#include <cmath>

TEST_CASE("horizon_radius at known spins") {
    CHECK(bhr::horizon_radius(0.0f) == doctest::Approx(2.0f));
    CHECK(bhr::horizon_radius(0.998f) == doctest::Approx(1.0f + std::sqrt(1.0f - 0.998f * 0.998f)).epsilon(1e-5));
    CHECK(bhr::horizon_radius(1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("r_isco at known spins") {
    CHECK(bhr::r_isco(0.0f) == doctest::Approx(6.0f).epsilon(1e-4));
    CHECK(bhr::r_isco(1.0f) == doctest::Approx(1.0f).epsilon(1e-3));
    CHECK(bhr::r_isco(0.998f) == doctest::Approx(1.237f).epsilon(1e-2));
}

TEST_CASE("Sigma and Delta at sample points") {
    CHECK(bhr::sigma(10.0f, 0.0f, 0.5f) == doctest::Approx(100.0f));
    const float a = 0.5f;
    const float rp = bhr::horizon_radius(a);
    CHECK(bhr::delta(rp, a) == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("Schwarzschild (a=0) collapses correctly") {
    const float r = 10.0f, sth = 1.0f, cth = 0.0f;
    CHECK(bhr::delta(r, 0.0f) == doctest::Approx(100.0f - 20.0f));
    CHECK(bhr::bl_A(r, sth, 0.0f) == doctest::Approx(std::pow(r, 4.0f)));
    CHECK(bhr::omega_drag(r, sth, 0.0f) == doctest::Approx(0.0f));
}
