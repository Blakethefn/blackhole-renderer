// Smoke test for the gradient kernel that does not require an OpenGL context.
// We allocate a plain cudaArray and wrap it in a surface object, then verify
// the first pixel has the expected red=0, green=0 values at (0,0).

#include "doctest.h"
#include "bhr/hello.hpp"
#include <cuda_runtime.h>
#include <surface_functions.h>
#include <vector>

TEST_CASE("cuda_gradient writes through a surface object") {
    const int W = 32, H = 16;

    cudaChannelFormatDesc desc = cudaCreateChannelDesc<uchar4>();
    cudaArray_t array = nullptr;
    REQUIRE(cudaMallocArray(&array, &desc, W, H, cudaArraySurfaceLoadStore)
            == cudaSuccess);

    cudaResourceDesc res{};
    res.resType = cudaResourceTypeArray;
    res.res.array.array = array;

    cudaSurfaceObject_t surf = 0;
    REQUIRE(cudaCreateSurfaceObject(&surf, &res) == cudaSuccess);

    bhr::cuda_gradient(static_cast<unsigned long long>(surf), W, H, /*t=*/0.0f);
    REQUIRE(cudaDeviceSynchronize() == cudaSuccess);

    std::vector<unsigned char> host(W * H * 4, 0xAA);
    REQUIRE(cudaMemcpy2DFromArray(host.data(), W * 4, array, 0, 0,
                                  W * 4, H, cudaMemcpyDeviceToHost) == cudaSuccess);

    // Pixel (0,0): u=0, v=0, pulse=sin(0)=0 → mapped to 0.5 by formula.
    CHECK(host[0] == 0);  // R
    CHECK(host[1] == 0);  // G
    CHECK(host[3] == 255); // A

    // Bottom-right corner: u=(W-1)/W, v=(H-1)/H — both near 1.
    // With W=32: R = floor(255 * 31/32) = 247.
    // With H=16: G = floor(255 * 15/16) = 239.
    const int last = ((H - 1) * W + (W - 1)) * 4;
    CHECK(host[last + 0] >= 235);
    CHECK(host[last + 1] >= 235);
    CHECK(host[last + 3] == 255);

    cudaDestroySurfaceObject(surf);
    cudaFreeArray(array);
}
