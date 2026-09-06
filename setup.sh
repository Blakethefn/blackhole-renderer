#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly DEFAULT_BUILD_ROOT="${XDG_CACHE_HOME:-${HOME}/.cache}/blackhole-renderer"

preset="linux-release"
build_dir=""
cuda_arch="86"
sdl_prefix=""
run_tests=false
make_package=false
init_submodules=true

usage() {
    cat <<'USAGE'
Usage: ./setup.sh [options]

Configure and build without installing system dependencies or changing build/.

Options:
  --headless             Build CUDA CLI/benchmark without SDL2/OpenGL
  --cpu-tests            Build only the CUDA-free CPU contract test suite
  --build-dir PATH       Use an explicit build directory (default: user cache)
  --cuda-arch LIST       CMake CUDA architectures (default: 86)
  --sdl-prefix PATH      Prefix containing an SDL2 CMake package
  --test                 Run CTest after building
  --package              Produce the local TGZ candidate after tests
  --no-submodules        Do not initialize missing pinned submodules
  -h, --help             Show this help
USAGE
}

while (($# > 0)); do
    case "$1" in
        --headless)
            preset="linux-headless"
            ;;
        --cpu-tests)
            preset="cpu-tests"
            ;;
        --build-dir)
            [[ $# -ge 2 ]] || { echo "ERROR: --build-dir requires a path" >&2; exit 2; }
            build_dir="$2"
            shift
            ;;
        --cuda-arch)
            [[ $# -ge 2 ]] || { echo "ERROR: --cuda-arch requires a value" >&2; exit 2; }
            cuda_arch="$2"
            shift
            ;;
        --sdl-prefix)
            [[ $# -ge 2 ]] || { echo "ERROR: --sdl-prefix requires a path" >&2; exit 2; }
            sdl_prefix="$2"
            shift
            ;;
        --test)
            run_tests=true
            ;;
        --package)
            run_tests=true
            make_package=true
            ;;
        --no-submodules)
            init_submodules=false
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "ERROR: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [[ ! "$cuda_arch" =~ ^[0-9]+(-(real|virtual))?(\;[0-9]+(-(real|virtual))?)*$ ]]; then
    echo "ERROR: --cuda-arch must be a semicolon-separated CMake architecture list" >&2
    exit 2
fi

if [[ -z "$build_dir" ]]; then
    build_dir="${DEFAULT_BUILD_ROOT}/${preset}"
fi

for required_command in cmake ninja git; do
    command -v "$required_command" >/dev/null 2>&1 || {
        echo "ERROR: required command not found: ${required_command}" >&2
        exit 1
    }
done

if [[ "$preset" != "cpu-tests" ]]; then
    command -v nvcc >/dev/null 2>&1 || {
        echo "ERROR: nvcc not found; install a supported CUDA 12.x toolkit" >&2
        exit 1
    }
fi

cd "$SCRIPT_DIR"
readonly submodule_status="$(git submodule status --recursive)"
if [[ "$submodule_status" == *$'\n-'* || "$submodule_status" == -* ]]; then
    if [[ "$init_submodules" == true ]]; then
        echo "==> Initializing pinned git submodules"
        git submodule update --init --recursive
    else
        echo "ERROR: submodules are missing; run git submodule update --init --recursive" >&2
        exit 1
    fi
fi

configure_args=(--preset "$preset" -B "$build_dir")
if [[ "$preset" != "cpu-tests" ]]; then
    configure_args+=(-DCMAKE_CUDA_ARCHITECTURES="$cuda_arch")
fi
if [[ -n "$sdl_prefix" ]]; then
    configure_args+=(-DCMAKE_PREFIX_PATH="$sdl_prefix")
fi

echo "==> Configuring ${preset} in ${build_dir}"
cmake "${configure_args[@]}"

echo "==> Building"
cmake --build "$build_dir" -j

if [[ "$run_tests" == true ]]; then
    echo "==> Testing"
    ctest --test-dir "$build_dir" --output-on-failure
fi

if [[ "$make_package" == true ]]; then
    [[ "$preset" != "cpu-tests" ]] || {
        echo "ERROR: --package is unavailable with --cpu-tests" >&2
        exit 2
    }
    echo "==> Packaging"
    cpack --config "$build_dir/CPackConfig.cmake"
fi

echo "==> Complete: ${build_dir}"
