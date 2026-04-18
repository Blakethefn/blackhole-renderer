#pragma once
/// @file bhr/renderer.hpp
/// Public entrypoint for rendering a black hole to an RGBA8 image.

#include "bhr/params.hpp"
#include "bhr/image.hpp"
#include "bhr/starfield.hpp"

namespace bhr {

/// Render with optional starfield. If sf.is_valid() is false, escape uses dim blue.
void render(const RenderParams& params, const Starfield& sf, Image& img);

/// Backwards-compatible overload without a starfield.
inline void render(const RenderParams& params, Image& img) {
    Starfield empty;
    render(params, empty, img);
}

} // namespace bhr
