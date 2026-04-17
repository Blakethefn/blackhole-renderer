#pragma once
/// @file bhr/renderer.hpp
/// Public entrypoint for rendering a black hole to an RGBA8 image.

#include "bhr/params.hpp"
#include "bhr/image.hpp"

namespace bhr {

/// Render the scene described by @p params into @p img. Resizes img if needed.
/// Synchronous — returns when the render is complete and the image is on the host.
void render(const RenderParams& params, Image& img);

} // namespace bhr
