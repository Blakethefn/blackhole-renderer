#include "doctest.h"
#include "bhr/redshift.hpp"
#include "bhr/disk_physics.hpp"
#include "bhr/geodesic.hpp"

TEST_CASE("Redshift at large radius with zero angular momentum approaches 1") {
    // At r → infinity, disk 4-velocity → (1, 0, 0, 0), so g → 1 for any photon.
    bhr::Conserved c{.E = 1.0f, .Lz = 0.0f, .Q = 0.0f};
    const float g = bhr::redshift_disk_to_infinity(1000.0f, 0.0f, c);
    CHECK(g == doctest::Approx(1.0f).epsilon(0.02));
}

TEST_CASE("Redshift of disk emitter at r=10 (Schwarzschild) is < 1") {
    // Gravitational redshift + disk motion → observer sees lower freq than emitter
    bhr::Conserved c{.E = 1.0f, .Lz = 0.0f, .Q = 0.0f};
    const float g = bhr::redshift_disk_to_infinity(10.0f, 0.0f, c);
    CHECK(g < 1.0f);
    CHECK(g > 0.0f);
}

TEST_CASE("Doppler asymmetry: prograde photon blueshifted vs retrograde") {
    // For a disk orbiting in +phi direction, a photon going with the flow (Lz > 0)
    // should be blueshifted (g > 1) relative to one going against (Lz < 0, g < 1).
    // Both emitted from the same radius on the equatorial plane.
    const float r = 8.0f;   // within ISCO=6 * 1.5 zone, disk moves fast
    const float a = 0.0f;
    // Same energy, opposite Lz
    bhr::Conserved prograde{.E = 1.0f, .Lz = 3.0f, .Q = 0.0f};
    bhr::Conserved retro{.E = 1.0f, .Lz = -3.0f, .Q = 0.0f};
    const float g_pro = bhr::redshift_disk_to_infinity(r, a, prograde);
    const float g_retro = bhr::redshift_disk_to_infinity(r, a, retro);
    CHECK(g_pro > g_retro);  // prograde emitter relative to observer is approaching when photon has +Lz
}

TEST_CASE("Frequency switches preserve total shift and separate diagnostic factors") {
    const float r = 8.0f;
    const float a = 0.5f;
    const bhr::Conserved photon{1.0f, 3.0f, 0.0f};
    const float total = bhr::redshift_disk_to_infinity(r, a, photon);
    const float time_dilation = bhr::disk_frequency_shift(r, a, photon, false, true);
    const float doppler = bhr::disk_frequency_shift(r, a, photon, true, false);
    CHECK(bhr::disk_frequency_shift(r, a, photon, true, true) == total);
    CHECK(bhr::disk_frequency_shift(r, a, photon, false, false) == 1.0f);
    CHECK(time_dilation < 1.0f);
    CHECK(doppler > 1.0f);
    CHECK(time_dilation * doppler == doctest::Approx(total).epsilon(1e-6));

    const bhr::Conserved radial{1.0f, 0.0f, 0.0f};
    CHECK(bhr::disk_frequency_shift(r, a, radial, true, false) == 1.0f);
    CHECK(bhr::disk_frequency_shift(r, a, radial, false, true) == time_dilation);
    const bhr::Conserved retrograde{1.0f, -3.0f, 0.0f};
    CHECK(bhr::disk_frequency_shift(r, a, retrograde, true, false) < 1.0f);
}
