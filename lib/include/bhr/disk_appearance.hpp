#pragma once
#include "bhr/kerr.hpp"

namespace bhr {
/// Static artistic emissivity only. r is the disk-hit BL radius in M, phi is
/// its BL azimuth in radians; u=(r-inner)/(outer-inner), v=fract(phi/(2*pi)).
/// No time input or alteration of ray geometry, opacity, temperature or g.
BHR_HD inline float disk_appearance(float r, float phi, float inner, float outer,
                                    float detail, float fade) {
    constexpr float tau=6.2831853071795864769f;
    const float u=fminf(1.0f,fmaxf(0.0f,(r-inner)/(outer-inner)));
    const float angle=phi-tau*floorf(phi/tau);
    const float lanes=.65f*cosf(48.0f*u+2.0f*angle+2.0f*sinf(angle))
                      +.35f*cosf(97.0f*u-3.0f*angle);
    const float edge=fade>0?fminf(1.0f,(1.0f-u)/fade):1.0f;
    const float taper=edge*edge*(3.0f-2.0f*edge);
    return (1.0f+detail*lanes)*taper;
}
} // namespace bhr
