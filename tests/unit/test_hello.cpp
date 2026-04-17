#include "doctest.h"
#include "bhr/hello.hpp"
#include <cstring>
#include <string>

TEST_CASE("cuda_add runs a kernel and returns the sum") {
    const int result = bhr::cuda_add(1, 2);
    CHECK(result == 3);
}

TEST_CASE("cuda_add handles negatives") {
    CHECK(bhr::cuda_add(-5, 10) == 5);
}

TEST_CASE("cuda_device_name reports a non-empty device") {
    const char* name = bhr::cuda_device_name();
    CHECK(std::strlen(name) > 0);
    MESSAGE("Detected GPU: " << std::string(name));
}
