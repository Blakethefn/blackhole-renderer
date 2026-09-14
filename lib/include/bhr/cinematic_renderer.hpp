#pragma once
#include "bhr/appearance.hpp"
#include "bhr/display_transform.hpp"
#include "bhr/renderer.hpp"

namespace bhr {
/// One contiguous caller-owned CUDA workspace, 16-byte aligned. Includes radiance,
/// status, two quarter-dimension bloom buffers and diagnostic counters. Packed rows.
struct CinematicDeviceTarget { void* data; size_t capacity_bytes; int width, height; };
size_t cinematic_workspace_bytes(int width, int height);
RadiancePixel* radiance_data(CinematicDeviceTarget);
PixelStatus* status_data(CinematicDeviceTarget);
FrameDiagnostics* diagnostics_data(CinematicDeviceTarget);
/// Async, allocation-free calls. Targets/output/starfield/stream must remain alive
/// until completion. Stages execute in order on the same stream. No GL dependency.
cudaError_t render_radiance_device(const CinematicRequest&, const Starfield&, CinematicDeviceTarget, cudaStream_t = 0);
cudaError_t bloom_device(const AppearanceV1&, CinematicDeviceTarget, cudaStream_t = 0);
cudaError_t display_device(const AppearanceV1&, CinematicDeviceTarget, uchar4*, size_t capacity_bytes, cudaStream_t = 0);
cudaError_t render_cinematic_device(const CinematicRequest&, const Starfield&, CinematicDeviceTarget, uchar4*, size_t, cudaStream_t = 0);
/// Reusable owned workspace plus pinned asynchronous diagnostics. Not an image
/// cache. Construct/close only with no work in flight; values are never shared mutably.
class CinematicBuffer {
public:
    CinematicBuffer(int width, int height);
    ~CinematicBuffer();
    CinematicBuffer(const CinematicBuffer&) = delete;
    CinematicBuffer& operator=(const CinematicBuffer&) = delete;
    CinematicDeviceTarget target() const { return target_; }
    cudaError_t read_diagnostics_async(cudaStream_t stream);
    FrameDiagnostics diagnostics() const { return *host_; } // only after stream/event completes
    bool close();
private:
    CinematicDeviceTarget target_{nullptr,0,0,0};
    FrameDiagnostics* host_ = nullptr;
};
struct CinematicResult { Image image; FrameDiagnostics diagnostics; };
/// Blocking wrapper. Throws actionable errors; never returns a partial/invalid image.
CinematicResult render_cinematic(const CinematicRequest&, const Starfield&);
} // namespace bhr
