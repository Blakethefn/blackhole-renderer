#include "bhr/geodesic.hpp"
#include "bhr/kerr.hpp"

#include <cuda_runtime.h>
#include <cmath>

namespace bhr {

namespace {

__host__ __device__ inline void cash_karp_step(
    const GeodesicState& y, float h, float a, const Conserved& c,
    GeodesicState& y5, GeodesicState& err_out)
{
    const float b21 = 1.0f / 5.0f;
    const float b31 = 3.0f / 40.0f;
    const float b32 = 9.0f / 40.0f;
    const float b41 = 3.0f / 10.0f;
    const float b42 = -9.0f / 10.0f;
    const float b43 = 6.0f / 5.0f;
    const float b51 = -11.0f / 54.0f;
    const float b52 = 5.0f / 2.0f;
    const float b53 = -70.0f / 27.0f;
    const float b54 = 35.0f / 27.0f;
    const float b61 = 1631.0f / 55296.0f;
    const float b62 = 175.0f / 512.0f;
    const float b63 = 575.0f / 13824.0f;
    const float b64 = 44275.0f / 110592.0f;
    const float b65 = 253.0f / 4096.0f;

    const float c1 = 37.0f / 378.0f;
    const float c3 = 250.0f / 621.0f;
    const float c4 = 125.0f / 594.0f;
    const float c6 = 512.0f / 1771.0f;

    const float dc1 = c1 - 2825.0f / 27648.0f;
    const float dc3 = c3 - 18575.0f / 48384.0f;
    const float dc4 = c4 - 13525.0f / 55296.0f;
    const float dc5 = -277.0f / 14336.0f;
    const float dc6 = c6 - 0.25f;

    auto axpy = [](const GeodesicState& s, float alpha, const GeodesicState& ds) {
        GeodesicState r;
        r.r        = s.r        + alpha * ds.r;
        r.theta    = s.theta    + alpha * ds.theta;
        r.phi      = s.phi      + alpha * ds.phi;
        r.t        = s.t        + alpha * ds.t;
        r.dr_dlam  = s.dr_dlam  + alpha * ds.dr_dlam;
        r.dth_dlam = s.dth_dlam + alpha * ds.dth_dlam;
        return r;
    };

    const GeodesicState k1 = rhs(y, a, c);
    const GeodesicState k2 = rhs(axpy(y, h * b21, k1), a, c);
    GeodesicState tmp = y;
    tmp = axpy(tmp, h * b31, k1);
    tmp = axpy(tmp, h * b32, k2);
    const GeodesicState k3 = rhs(tmp, a, c);
    tmp = y;
    tmp = axpy(tmp, h * b41, k1);
    tmp = axpy(tmp, h * b42, k2);
    tmp = axpy(tmp, h * b43, k3);
    const GeodesicState k4 = rhs(tmp, a, c);
    tmp = y;
    tmp = axpy(tmp, h * b51, k1);
    tmp = axpy(tmp, h * b52, k2);
    tmp = axpy(tmp, h * b53, k3);
    tmp = axpy(tmp, h * b54, k4);
    const GeodesicState k5 = rhs(tmp, a, c);
    tmp = y;
    tmp = axpy(tmp, h * b61, k1);
    tmp = axpy(tmp, h * b62, k2);
    tmp = axpy(tmp, h * b63, k3);
    tmp = axpy(tmp, h * b64, k4);
    tmp = axpy(tmp, h * b65, k5);
    const GeodesicState k6 = rhs(tmp, a, c);

    // 5th order solution: y5 = y + h * (c1 k1 + c3 k3 + c4 k4 + c6 k6)
    y5 = y;
    y5.r        += h * (c1*k1.r        + c3*k3.r        + c4*k4.r        + c6*k6.r);
    y5.theta    += h * (c1*k1.theta    + c3*k3.theta    + c4*k4.theta    + c6*k6.theta);
    y5.phi      += h * (c1*k1.phi      + c3*k3.phi      + c4*k4.phi      + c6*k6.phi);
    y5.t        += h * (c1*k1.t        + c3*k3.t        + c4*k4.t        + c6*k6.t);
    y5.dr_dlam  += h * (c1*k1.dr_dlam  + c3*k3.dr_dlam  + c4*k4.dr_dlam  + c6*k6.dr_dlam);
    y5.dth_dlam += h * (c1*k1.dth_dlam + c3*k3.dth_dlam + c4*k4.dth_dlam + c6*k6.dth_dlam);

    // Error estimate
    err_out.r        = h * (dc1*k1.r        + dc3*k3.r        + dc4*k4.r        + dc5*k5.r        + dc6*k6.r);
    err_out.theta    = h * (dc1*k1.theta    + dc3*k3.theta    + dc4*k4.theta    + dc5*k5.theta    + dc6*k6.theta);
    err_out.phi      = h * (dc1*k1.phi      + dc3*k3.phi      + dc4*k4.phi      + dc5*k5.phi      + dc6*k6.phi);
    err_out.t        = h * (dc1*k1.t        + dc3*k3.t        + dc4*k4.t        + dc5*k5.t        + dc6*k6.t);
    err_out.dr_dlam  = h * (dc1*k1.dr_dlam  + dc3*k3.dr_dlam  + dc4*k4.dr_dlam  + dc5*k5.dr_dlam  + dc6*k6.dr_dlam);
    err_out.dth_dlam = h * (dc1*k1.dth_dlam + dc3*k3.dth_dlam + dc4*k4.dth_dlam + dc5*k5.dth_dlam + dc6*k6.dth_dlam);
}

__host__ __device__ inline float max_abs_state(const GeodesicState& s) {
    float m = 0.0f;
    m = fmaxf(m, fabsf(s.r));
    m = fmaxf(m, fabsf(s.theta));
    m = fmaxf(m, fabsf(s.phi));
    m = fmaxf(m, fabsf(s.t));
    m = fmaxf(m, fabsf(s.dr_dlam));
    m = fmaxf(m, fabsf(s.dth_dlam));
    return m;
}

} // namespace

__host__ __device__ HitInfo integrate_rk45(GeodesicState s, float a, const Conserved& c,
                                           const IntegratorConfig& cfg)
{
    HitInfo hit{};
    const float rp = horizon_radius(a);
    const float horizon_limit = rp * (1.0f + cfg.horizon_eps);

    float h = cfg.h_init;
    float lam = 0.0f;

    for (int i = 0; i < cfg.max_steps; ++i) {
        if (s.r <= horizon_limit) {
            hit.type = HitType::kHorizon;
            hit.r = s.r; hit.theta = s.theta; hit.phi = s.phi;
            hit.lambda = lam; hit.steps = i;
            return hit;
        }
        if (s.r >= cfg.r_max) {
            hit.type = HitType::kEscape;
            hit.r = s.r; hit.theta = s.theta; hit.phi = s.phi;
            hit.lambda = lam; hit.steps = i;
            return hit;
        }

        GeodesicState y5{}, err{};
        cash_karp_step(s, h, a, c, y5, err);

        const float err_norm = max_abs_state(err);
        const float y_norm = fmaxf(1e-6f, max_abs_state(y5));
        const float ratio = err_norm / (cfg.tol * y_norm);

        if (ratio > 1.0f && h > cfg.h_min) {
            h = fmaxf(cfg.h_min, 0.9f * h * powf(ratio, -0.25f));
            continue;
        }

        const float prev_th = s.theta;
        GeodesicState s_new = y5;
        lam += h;

        const float half_pi = 1.5707963267948966f;
        const float diff_prev = prev_th - half_pi;
        const float diff_new  = s_new.theta - half_pi;
        if (diff_prev * diff_new < 0.0f) {
            const float alpha = diff_prev / (diff_prev - diff_new);
            const float r_cross = s.r + alpha * (s_new.r - s.r);
            if (r_cross >= cfg.disk_r_inner && r_cross <= cfg.disk_r_outer) {
                hit.type = HitType::kDisk;
                hit.r = r_cross;
                hit.theta = half_pi;
                hit.phi = s.phi + alpha * (s_new.phi - s.phi);
                hit.lambda = lam - h + alpha * h;
                hit.steps = i + 1;
                return hit;
            }
        }

        s = s_new;

        if (ratio < 1.0f) {
            h = fminf(cfg.h_max, 0.9f * h * powf(fmaxf(ratio, 1e-4f), -0.2f));
        }
    }

    hit.type = HitType::kUnknown;
    hit.r = s.r; hit.theta = s.theta; hit.phi = s.phi;
    hit.lambda = lam; hit.steps = cfg.max_steps;
    return hit;
}

} // namespace bhr
