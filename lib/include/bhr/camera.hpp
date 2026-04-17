#pragma once
/// @file bhr/camera.hpp
/// Generate initial photon state in Boyer-Lindquist coordinates from a camera pixel.

#include "bhr/kerr.hpp"
#include "bhr/geodesic.hpp"
#include "bhr/params.hpp"

namespace bhr {

/// Build the initial GeodesicState and conserved quantities for a
/// ray launched from camera pixel (px, py) in a width×height image.
/// Uses the ZAMO tetrad at the camera position.
BHR_HD inline void camera_ray(
    int px, int py, int width, int height,
    const CameraParams& cam, float a,
    GeodesicState& state_out, Conserved& conserved_out)
{
    const float PI = 3.14159265358979323846f;

    // Screen-space normalized direction. Origin top-left; y points down in pixel
    // space; we flip so screen-up maps to +y in world.
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float fov_rad = cam.fov_deg * (PI / 180.0f);
    const float tan_half = std::tan(0.5f * fov_rad);
    const float sx = (2.0f * (px + 0.5f) / width - 1.0f) * tan_half * aspect;
    const float sy = (1.0f - 2.0f * (py + 0.5f) / height) * tan_half;
    const float sz = -1.0f; // forward

    const float norm = std::sqrt(sx*sx + sy*sy + sz*sz);
    const float nx = sx / norm;
    const float ny = sy / norm;
    const float nz = sz / norm;

    // Observer position
    const float theta_cam = cam.theta_cam_deg * (PI / 180.0f);
    const float sth = std::sin(theta_cam);
    const float cth = std::cos(theta_cam);
    const float r = cam.r_cam;

    // Metric pieces at camera position
    const float S = sigma(r, cth, a);
    const float D = delta(r, a);
    const float A_bl = bl_A(r, sth, a);

    // Guard against theta_cam exactly 0 or pi (poles)
    const float sth_safe = std::fmax(sth, 1e-6f);

    // ZAMO 4-velocity
    const float alpha_lapse = std::sqrt(S * D / A_bl);
    const float omega = 2.0f * a * r / A_bl;
    const float ut = 1.0f / alpha_lapse;
    const float uphi = omega * ut;

    // Tetrad basis (contravariant components of e_r̂, e_θ̂, e_φ̂)
    const float e_r_r = std::sqrt(D / S);
    const float e_th_th = 1.0f / std::sqrt(S);
    // e_φ̂^φ: scale so that g_ab (e_φ̂)^a (e_φ̂)^b = 1 in the ZAMO frame
    // = 1 / sqrt( (A sin²θ) / Σ )
    const float e_phi_phi = 1.0f / std::sqrt(A_bl * sth_safe * sth_safe / S);

    // Photon 4-momentum in coordinate basis, with E_local = 1 local-frame energy.
    // Camera convention: forward = -z (nz=-1), right = +x, up = +y.
    // The camera looks radially inward: forward maps to -r̂, right maps to +φ̂.
    // nz=-1 at center pixel → p_r_con < 0 (inward) — correct for a camera aimed at the hole.
    const float p_t_con   = ut;
    const float p_r_con   = nz * e_r_r;       // forward (-z) → radial inward
    const float p_th_con  = -ny * e_th_th;    // screen-up corresponds to decreasing θ
    const float p_phi_con = uphi + nx * e_phi_phi; // screen-right (+x) → azimuthal

    // Covariant components via metric
    const float g_tt = -(1.0f - 2.0f * r / S);
    const float g_tphi = -2.0f * a * r * sth * sth / S;
    const float g_thth = S;
    const float g_phiphi = (r*r + a*a + 2.0f * a * a * r * sth * sth / S) * sth * sth;

    const float p_t_cov   = g_tt * p_t_con   + g_tphi * p_phi_con;
    const float p_phi_cov = g_tphi * p_t_con + g_phiphi * p_phi_con;
    const float p_th_cov  = g_thth * p_th_con;

    conserved_out.E = -p_t_cov;
    conserved_out.Lz = p_phi_cov;
    conserved_out.Q = p_th_cov * p_th_cov
                    + cth * cth * (-a * a * conserved_out.E * conserved_out.E
                                   + conserved_out.Lz * conserved_out.Lz / (sth_safe * sth_safe));

    // Mino-time derivatives
    state_out.r = r;
    state_out.theta = theta_cam;
    state_out.phi = cam.phi_cam_deg * (PI / 180.0f);
    state_out.t = 0.0f;
    state_out.dr_dlam = S * p_r_con;
    state_out.dth_dlam = S * p_th_con;
}

} // namespace bhr
