#include "doctest.h"
#include "../../app/src/cli_options.hpp"

#include <limits>

TEST_CASE("CLI float options require a whole finite numeric value") {
    float value = 0.0f;
    CHECK(bhr::cli::parse_finite_float("0.9", value));
    CHECK(value == doctest::Approx(0.9f));
    CHECK(bhr::cli::parse_finite_float("-2e1", value));
    CHECK(value == -20.0f);
    for (const char* invalid : {"", "garbage", "0.9junk", " 1", "1 ", "nan", "inf", "1e100", "1e-100"}) {
        CAPTURE(invalid);
        CHECK_FALSE(bhr::cli::parse_finite_float(invalid, value));
    }
}

TEST_CASE("CLI integer options reject truncation overflow and suffixes") {
    int value = 0;
    CHECK(bhr::cli::parse_positive_integer("256", value));
    CHECK(value == 256);
    for (const char* invalid : {"", "0", "-1", "1.5", "1px", " 1", "2147483648"}) {
        CAPTURE(invalid);
        CHECK_FALSE(bhr::cli::parse_positive_integer(invalid, value));
    }
}

TEST_CASE("CLI resolutions require exactly two positive integers") {
    int width = 3;
    int height = 4;
    CHECK(bhr::cli::parse_resolution("256x144", width, height));
    CHECK(width == 256);
    CHECK(height == 144);
    for (const char* invalid : {"256", "x144", "256x", "256x144junk", "256junkx144", "256x144x2", "0x144", "256x-144", "256x144.5"}) {
        CAPTURE(invalid);
        CHECK_FALSE(bhr::cli::parse_resolution(invalid, width, height));
        CHECK(width == 256);
        CHECK(height == 144);
    }
}

TEST_CASE("CLI integrator names are explicit") {
    bhr::IntegratorKind integrator = bhr::IntegratorKind::kRK45;
    CHECK(bhr::cli::parse_integrator("geokerr", integrator));
    CHECK(integrator == bhr::IntegratorKind::kGeokerr);
    CHECK(bhr::cli::parse_integrator("rk45", integrator));
    CHECK(integrator == bhr::IntegratorKind::kRK45);
    CHECK_FALSE(bhr::cli::parse_integrator("RK45", integrator));
    CHECK_FALSE(bhr::cli::parse_integrator("other", integrator));
}
