#include "bhr/elliptic.hpp"
#include "doctest.h"
#include <cmath>

TEST_CASE("Carlson R_F reference values") {
    // R_F(0, 1, 2) = K(1/√2) / √2 ≈ 1.3110288  (note: K(1/√2) = R_F(0, 0.5, 1))
    CHECK(bhr::carlson_RF(0.0f, 1.0f, 2.0f) == doctest::Approx(1.3110288f).epsilon(1e-5));

    // R_F(2, 3, 4) ≈ 0.58408284
    CHECK(bhr::carlson_RF(2.0f, 3.0f, 4.0f) == doctest::Approx(0.58408284f).epsilon(1e-5));

    // R_F is symmetric in its arguments — check all permutations of (1, 2, 3)
    const float base = bhr::carlson_RF(1.0f, 2.0f, 3.0f);
    CHECK(bhr::carlson_RF(2.0f, 1.0f, 3.0f) == doctest::Approx(base).epsilon(1e-6));
    CHECK(bhr::carlson_RF(3.0f, 1.0f, 2.0f) == doctest::Approx(base).epsilon(1e-6));
}

TEST_CASE("Carlson R_D reference values") {
    // R_D(0, 2, 1) = 3 * [K(1/√2) − E(1/√2)] / sin²(π/4)
    //              ≈ 1.7972103521
    CHECK(bhr::carlson_RD(0.0f, 2.0f, 1.0f) == doctest::Approx(1.7972103f).epsilon(1e-5));

    // R_D(2, 3, 4) ≈ 0.16510527
    CHECK(bhr::carlson_RD(2.0f, 3.0f, 4.0f) == doctest::Approx(0.16510527f).epsilon(1e-5));

    // R_D is symmetric only in the first two arguments.
    const float a = bhr::carlson_RD(1.0f, 2.0f, 3.0f);
    const float b = bhr::carlson_RD(2.0f, 1.0f, 3.0f);
    CHECK(a == doctest::Approx(b).epsilon(1e-6));
}

TEST_CASE("Carlson R_J reference values") {
    // R_J(2, 3, 4, 5) ≈ 0.14297579
    CHECK(bhr::carlson_RJ(2.0f, 3.0f, 4.0f, 5.0f)
          == doctest::Approx(0.14297579f).epsilon(1e-5));

    // R_J(0, 1, 2, 3) ≈ 0.77688623
    CHECK(bhr::carlson_RJ(0.0f, 1.0f, 2.0f, 3.0f)
          == doctest::Approx(0.77688623f).epsilon(1e-5));

    // R_J is symmetric in the first three arguments.
    const float base = bhr::carlson_RJ(1.0f, 2.0f, 3.0f, 4.0f);
    CHECK(bhr::carlson_RJ(2.0f, 1.0f, 3.0f, 4.0f)
          == doctest::Approx(base).epsilon(1e-6));
}

TEST_CASE("Carlson R_F duplication is numerically stable near zero") {
    // R_F(eps, 1, 1) → arctan-like, continuous as eps → 0
    const float ref = bhr::carlson_RF(0.0f, 1.0f, 1.0f);    // = π/2
    CHECK(ref == doctest::Approx(1.5707963f).epsilon(1e-5));
    const float near_zero = bhr::carlson_RF(1e-20f, 1.0f, 1.0f);
    CHECK(near_zero == doctest::Approx(ref).epsilon(1e-4));
}
