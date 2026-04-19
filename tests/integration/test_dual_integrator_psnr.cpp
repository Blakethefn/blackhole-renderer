#include "doctest.h"
#include "bhr/renderer.hpp"
#include "bhr/image.hpp"
#include "bhr/params.hpp"
#include <cmath>

namespace {

float psnr(const bhr::Image& a, const bhr::Image& b) {
    const int w = a.width, h = a.height;
    REQUIRE(w == b.width);
    REQUIRE(h == b.height);
    double mse = 0.0;
    const size_t n = size_t(w) * size_t(h) * 4;
    for (size_t i = 0; i < n; ++i) {
        const double d = double(a.rgba[i]) - double(b.rgba[i]);
        mse += d * d;
    }
    mse /= double(n);
    if (mse <= 1e-12) return 99.0f;
    return 20.0f * std::log10(255.0f) - 10.0f * std::log10(float(mse));
}

} // namespace

TEST_CASE("Dual integrator: Schwarzschild PSNR >= 35 dB") {
    bhr::RenderParams p;
    p.spin = 0.0f;
    p.camera.r_cam = 50.0f;
    p.camera.theta_cam_deg = 85.0f;
    p.camera.fov_deg = 35.0f;
    p.camera.width = 256;
    p.camera.height = 144;
    p.disk.r_inner = 6.0f;
    p.disk.r_outer = 20.0f;
    p.enable_starfield = false;

    bhr::Image img_rk45;  img_rk45.allocate(p.camera.width, p.camera.height);
    bhr::Image img_geo;   img_geo.allocate(p.camera.width, p.camera.height);

    p.integrator = bhr::IntegratorKind::kRK45;
    bhr::render(p, img_rk45);
    p.integrator = bhr::IntegratorKind::kGeokerr;
    bhr::render(p, img_geo);

    const float dB = psnr(img_rk45, img_geo);
    MESSAGE("Schwarzschild dual-integrator PSNR = " << dB << " dB");
    CHECK(dB >= 35.0f);
}

TEST_CASE("Dual integrator: Kerr a=0.9 PSNR >= 30 dB") {
    bhr::RenderParams p;
    p.spin = 0.9f;
    p.camera.r_cam = 30.0f;
    p.camera.theta_cam_deg = 85.0f;
    p.camera.fov_deg = 35.0f;
    p.camera.width = 256;
    p.camera.height = 144;
    p.disk.r_inner = 2.32f;   // ISCO at a=0.9
    p.disk.r_outer = 20.0f;
    p.enable_starfield = false;

    bhr::Image img_rk45;  img_rk45.allocate(p.camera.width, p.camera.height);
    bhr::Image img_geo;   img_geo.allocate(p.camera.width, p.camera.height);

    p.integrator = bhr::IntegratorKind::kRK45;
    bhr::render(p, img_rk45);
    p.integrator = bhr::IntegratorKind::kGeokerr;
    bhr::render(p, img_geo);

    const float dB = psnr(img_rk45, img_geo);
    MESSAGE("Kerr a=0.9 dual-integrator PSNR = " << dB << " dB");
    CHECK(dB >= 30.0f);
}
