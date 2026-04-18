#include "bhr/ferrari.hpp"
#include "doctest.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// Sort roots (real part ascending) for deterministic comparison.
std::vector<bhr::Complex> sorted_roots(bhr::QuarticRoots r) {
    std::vector<bhr::Complex> out{r.r0, r.r1, r.r2, r.r3};
    std::sort(out.begin(), out.end(), [](auto a, auto b) {
        if (a.re != b.re) return a.re < b.re;
        return a.im < b.im;
    });
    return out;
}

bool is_real(const bhr::Complex& z, float tol = 1e-4f) {
    return std::fabs(z.im) < tol;
}

} // namespace

TEST_CASE("Ferrari: (u-1)(u-2)(u-3)(u-4)") {
    // Expanded: u^4 - 10 u^3 + 35 u^2 - 50 u + 24
    const auto roots = bhr::ferrari_quartic(1.0f, -10.0f, 35.0f, -50.0f, 24.0f);
    const auto s = sorted_roots(roots);
    for (const auto& r : s) CHECK(is_real(r));
    CHECK(s[0].re == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(s[1].re == doctest::Approx(2.0f).epsilon(1e-4));
    CHECK(s[2].re == doctest::Approx(3.0f).epsilon(1e-4));
    CHECK(s[3].re == doctest::Approx(4.0f).epsilon(1e-4));
}

TEST_CASE("Ferrari: two real, one complex pair (u-1)(u+2)(u^2 + u + 1)") {
    // Correct expansion: u^4 + 2 u^3 + 0 u^2 - u - 2
    // (plan had d=-3 which is a typo; numpy.roots confirms d=-1 gives roots {-2, 1, complex pair})
    const auto roots = bhr::ferrari_quartic(1.0f, 2.0f, 0.0f, -1.0f, -2.0f);
    const auto s = sorted_roots(roots);
    int real_count = 0;
    for (const auto& r : s) if (is_real(r)) ++real_count;
    CHECK(real_count == 2);
    // Real roots should be u = -2 and u = 1.
    std::vector<float> real_parts;
    for (const auto& r : s) if (is_real(r)) real_parts.push_back(r.re);
    std::sort(real_parts.begin(), real_parts.end());
    REQUIRE(real_parts.size() == 2);
    CHECK(real_parts[0] == doctest::Approx(-2.0f).epsilon(1e-3));
    CHECK(real_parts[1] == doctest::Approx(1.0f).epsilon(1e-3));
}

TEST_CASE("Ferrari: biquadratic (u^2 - 1)(u^2 - 4) = u^4 - 5u^2 + 4") {
    const auto roots = bhr::ferrari_quartic(1.0f, 0.0f, -5.0f, 0.0f, 4.0f);
    const auto s = sorted_roots(roots);
    for (const auto& r : s) CHECK(is_real(r));
    CHECK(s[0].re == doctest::Approx(-2.0f).epsilon(1e-4));
    CHECK(s[1].re == doctest::Approx(-1.0f).epsilon(1e-4));
    CHECK(s[2].re == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(s[3].re == doctest::Approx(2.0f).epsilon(1e-4));
}

TEST_CASE("Ferrari: cubic degeneracy (a=0 falls back to cubic)") {
    // 0 u^4 + u^3 - 6 u^2 + 11 u - 6 = (u-1)(u-2)(u-3)
    // Must return 3 valid real roots (the 4th sentinel is unused or NaN).
    const auto roots = bhr::ferrari_quartic(0.0f, 1.0f, -6.0f, 11.0f, -6.0f);
    std::vector<float> real_parts;
    for (const auto& r : {roots.r0, roots.r1, roots.r2, roots.r3}) {
        if (std::isfinite(r.re) && is_real(r)) real_parts.push_back(r.re);
    }
    std::sort(real_parts.begin(), real_parts.end());
    REQUIRE(real_parts.size() >= 3);
    CHECK(real_parts[0] == doctest::Approx(1.0f).epsilon(1e-3));
    CHECK(real_parts[1] == doctest::Approx(2.0f).epsilon(1e-3));
    CHECK(real_parts[2] == doctest::Approx(3.0f).epsilon(1e-3));
}
