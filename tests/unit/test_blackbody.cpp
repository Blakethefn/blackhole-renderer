#include "doctest.h"
#include "bhr/blackbody.hpp"

TEST_CASE("Blackbody reference temperatures") {
    float r, g, b;

    // 3000 K: warm red/orange (candle)
    bhr::blackbody_rgb(3000.0f, r, g, b);
    CHECK(r > 0.9f);
    CHECK(g < r);
    CHECK(b < g);

    // 5778 K: sunlight, mostly white
    bhr::blackbody_rgb(5778.0f, r, g, b);
    CHECK(r > 0.95f);
    CHECK(g > 0.85f);
    CHECK(b > 0.85f);

    // 10000 K: blue-white
    bhr::blackbody_rgb(10000.0f, r, g, b);
    CHECK(b > 0.95f);
    CHECK(b > r);
}

TEST_CASE("Blackbody clamping below 1000K and above 40000K") {
    float r1, g1, b1, r2, g2, b2;
    bhr::blackbody_rgb(500.0f, r1, g1, b1);
    bhr::blackbody_rgb(1000.0f, r2, g2, b2);
    CHECK(r1 == doctest::Approx(r2));
    CHECK(g1 == doctest::Approx(g2));
    CHECK(b1 == doctest::Approx(b2));

    bhr::blackbody_rgb(100000.0f, r1, g1, b1);
    bhr::blackbody_rgb(40000.0f, r2, g2, b2);
    CHECK(r1 == doctest::Approx(r2));
    CHECK(g1 == doctest::Approx(g2));
    CHECK(b1 == doctest::Approx(b2));
}
