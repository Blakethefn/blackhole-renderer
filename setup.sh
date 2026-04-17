#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "==> Initializing git submodules"
git submodule update --init --recursive

# --- vcpkg detection ---------------------------------------------------------
if [[ -z "${VCPKG_ROOT:-}" ]]; then
    if [[ -d "/media/blakethefn/2000HDD1/dev/vcpkg" ]]; then
        export VCPKG_ROOT="/media/blakethefn/2000HDD1/dev/vcpkg"
        echo "==> Using vcpkg at $VCPKG_ROOT"
    else
        echo "ERROR: VCPKG_ROOT is not set and /media/blakethefn/2000HDD1/dev/vcpkg does not exist."
        echo "Install vcpkg and export VCPKG_ROOT before running this script."
        exit 1
    fi
fi

# --- glad check (manual asset, not vendored from git) -----------------------
if [[ ! -f external/glad/src/glad.c ]]; then
    echo "ERROR: external/glad is missing. See plan Task 9 to generate it from https://glad.dav1d.de/."
    exit 1
fi

# --- CUDA check --------------------------------------------------------------
if ! command -v nvcc >/dev/null 2>&1; then
    echo "ERROR: nvcc not found. Install CUDA Toolkit 12.x."
    exit 1
fi
echo "==> CUDA: $(nvcc --version | grep release)"

# --- Build artifact directory -----------------------------------------------
if [[ ! -L build ]]; then
    if [[ -e build ]]; then
        echo "ERROR: build/ exists and is not a symlink. Remove it or adjust setup.sh."
        exit 1
    fi
    echo "==> Creating build/ symlink to HDD"
    mkdir -p /media/blakethefn/2000HDD1/dev/blackhole-renderer-deps/build-cache
    ln -s /media/blakethefn/2000HDD1/dev/blackhole-renderer-deps/build-cache build
fi

# --- Configure + build ------------------------------------------------------
echo "==> Configuring CMake"
cmake --preset default

echo "==> Building"
cmake --build --preset default -j

echo
echo "✓ Build complete."
echo "  Workbench binary: ./build/app/blackhole-workbench"
echo "  Run tests:        ctest --preset default"
