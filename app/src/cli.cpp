// blackhole-cli: render a black hole to a PNG file from the command line.
//
// Usage:
//   blackhole-cli [--spin A] [--resolution WxH] [--inclination DEG]
//                 [--fov DEG] [--distance M] [--disk-inner R] [--disk-outer R]
//                 [--starfield PATH] --output FILE

#include "bhr/renderer.hpp"
#include "bhr/image.hpp"
#include "bhr/params.hpp"
#include "bhr/starfield.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>

namespace {

bool parse_resolution(const std::string& s, int& w, int& h) {
    const auto x = s.find('x');
    if (x == std::string::npos) return false;
    try {
        w = std::stoi(s.substr(0, x));
        h = std::stoi(s.substr(x + 1));
    } catch (...) {
        return false;
    }
    return w > 0 && h > 0;
}

void print_usage(const char* argv0) {
    std::fprintf(stderr,
        "Usage: %s [OPTIONS] --output FILE\n"
        "Options:\n"
        "  --spin VALUE         black hole spin a/M (0..0.998), default 0\n"
        "  --resolution WxH     output size, default 1920x1080\n"
        "  --inclination DEG    camera polar angle, default 85\n"
        "  --fov DEG            horizontal field of view, default 35\n"
        "  --distance M         camera radius in M, default 50\n"
        "  --disk-inner R       disk inner radius in M, default 6\n"
        "  --disk-outer R       disk outer radius in M, default 20\n"
        "  --starfield PATH     HDR EXR equirectangular starfield (optional)\n"
        "  --output FILE        output PNG path (required)\n",
        argv0);
}

} // namespace

int main(int argc, char** argv) {
    bhr::RenderParams params{};
    std::string out_path;
    std::string starfield_path;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Missing value for %s\n", name);
                std::exit(1);
            }
            return argv[++i];
        };

        if (arg == "--spin") {
            params.spin = std::strtof(next("--spin"), nullptr);
        } else if (arg == "--resolution") {
            int w = 0, h = 0;
            if (!parse_resolution(next("--resolution"), w, h)) {
                std::fprintf(stderr, "Bad resolution format (expected WxH)\n");
                return 1;
            }
            params.camera.width = w;
            params.camera.height = h;
        } else if (arg == "--inclination") {
            params.camera.theta_cam_deg = std::strtof(next("--inclination"), nullptr);
        } else if (arg == "--fov") {
            params.camera.fov_deg = std::strtof(next("--fov"), nullptr);
        } else if (arg == "--distance") {
            params.camera.r_cam = std::strtof(next("--distance"), nullptr);
        } else if (arg == "--disk-inner") {
            params.disk.r_inner = std::strtof(next("--disk-inner"), nullptr);
        } else if (arg == "--disk-outer") {
            params.disk.r_outer = std::strtof(next("--disk-outer"), nullptr);
        } else if (arg == "--starfield") {
            starfield_path = next("--starfield");
        } else if (arg == "--output") {
            out_path = next("--output");
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            print_usage(argv[0]);
            return 1;
        }
    }

    if (out_path.empty()) {
        std::fprintf(stderr, "--output is required\n");
        print_usage(argv[0]);
        return 1;
    }

    std::fprintf(stderr,
        "Rendering %dx%d, spin=%.3f, incl=%.1f deg, fov=%.1f deg, r_cam=%.1f M -> %s\n",
        params.camera.width, params.camera.height, params.spin,
        params.camera.theta_cam_deg, params.camera.fov_deg, params.camera.r_cam,
        out_path.c_str());

    bhr::Starfield sf;
    if (!starfield_path.empty()) {
        if (!bhr::load_starfield(starfield_path, 16384, sf)) {
            std::fprintf(stderr, "Warning: starfield load failed, continuing without\n");
        }
    }

    bhr::Image img;
    const auto t0 = std::chrono::steady_clock::now();
    bhr::render(params, sf, img);
    const auto t1 = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(t1 - t0).count();

    bhr::destroy_starfield(sf);

    if (img.width == 0) {
        std::fprintf(stderr, "Render failed (image is empty)\n");
        return 2;
    }

    if (!bhr::write_png(img, out_path)) {
        std::fprintf(stderr, "Failed to write PNG to %s\n", out_path.c_str());
        return 3;
    }

    std::fprintf(stderr, "Done in %.2fs -> %s\n", seconds, out_path.c_str());
    return 0;
}
