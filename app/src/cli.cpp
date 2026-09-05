// Headless renderer. Presets provide the base scene; explicit options override it.
#include "bhr/renderer.hpp"
#include "bhr/image.hpp"
#include "bhr/presets.hpp"
#include "bhr/starfield.hpp"
#include "cli_options.hpp"

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void print_usage(const char* executable) {
    std::fprintf(stderr,
        "Usage: %s [OPTIONS] --output FILE\n"
        "  --params FILE        load a JSON preset; other options override it\n"
        "  --spin VALUE         black hole spin a/M (0..0.999), default 0\n"
        "  --resolution WxH     output size, default 1920x1080\n"
        "  --inclination DEG    camera polar angle, default 85\n"
        "  --azimuth DEG        camera azimuth, default 0\n"
        "  --fov DEG            horizontal field of view, default 35\n"
        "  --distance M         camera radius in M, default 50\n"
        "  --disk-inner R       disk inner radius in M, default 6\n"
        "  --disk-outer R       disk outer radius in M, default 20\n"
        "  --disk-temp K        disk peak temperature, default 10000000\n"
        "  --brightness VALUE   disk brightness multiplier, default 1\n"
        "  --starfield PATH     load and enable an HDR EXR starfield\n"
        "  --no-starfield       disable starfield sampling\n"
        "  --no-doppler         disable Doppler shift\n"
        "  --no-redshift        disable gravitational redshift\n"
        "  --no-beaming         disable relativistic intensity beaming\n"
        "  --integrator NAME    rk45 (default) or geokerr (approximate)\n"
        "  --output FILE        output PNG path (required)\n",
        executable);
}

bool value_option(const std::string& option) {
    return option == "--params" || option == "--spin" || option == "--resolution" ||
        option == "--inclination" || option == "--azimuth" || option == "--fov" ||
        option == "--distance" || option == "--disk-inner" || option == "--disk-outer" ||
        option == "--disk-temp" || option == "--brightness" || option == "--starfield" ||
        option == "--integrator" || option == "--output";
}

float parse_float(const std::string& name, const std::string& value) {
    float result = 0.0f;
    if (!bhr::cli::parse_finite_float(value, result)) {
        throw std::runtime_error(name + " requires a finite number, received: " + value);
    }
    return result;
}

struct StarfieldOwner {
    bhr::Starfield value;
    ~StarfieldOwner() { bhr::destroy_starfield(value); }
};

int run(int argc, char** argv) {
    std::vector<std::pair<std::string, std::string>> options;
    std::string preset_path;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--help" || option == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        const bool takes_value = value_option(option);
        if (!takes_value && option != "--no-doppler" && option != "--no-redshift" &&
            option != "--no-beaming" && option != "--no-starfield") {
            throw std::runtime_error("Unknown argument: " + option);
        }
        if (takes_value && i + 1 >= argc) throw std::runtime_error("Missing value for " + option);
        const std::string value = takes_value ? argv[++i] : "";
        if (takes_value && value.empty()) throw std::runtime_error("Empty value for " + option);
        if (option == "--params") {
            if (!preset_path.empty()) throw std::runtime_error("--params can only be specified once");
            preset_path = value;
        }
        options.emplace_back(option, value);
    }

    bhr::RenderParams params;
    std::string error;
    if (!preset_path.empty() && !bhr::load_preset(preset_path, params, error)) {
        throw std::runtime_error("Cannot load preset: " + error);
    }
    std::string output_path;
    std::string starfield_path;
    for (const auto& [option, value] : options) {
        if (option == "--spin") params.spin = parse_float(option, value);
        else if (option == "--inclination") params.camera.theta_cam_deg = parse_float(option, value);
        else if (option == "--azimuth") params.camera.phi_cam_deg = parse_float(option, value);
        else if (option == "--fov") params.camera.fov_deg = parse_float(option, value);
        else if (option == "--distance") params.camera.r_cam = parse_float(option, value);
        else if (option == "--disk-inner") params.disk.r_inner = parse_float(option, value);
        else if (option == "--disk-outer") params.disk.r_outer = parse_float(option, value);
        else if (option == "--disk-temp") params.disk.peak_temp_K = parse_float(option, value);
        else if (option == "--brightness") params.disk.brightness = parse_float(option, value);
        else if (option == "--resolution") {
            if (!bhr::cli::parse_resolution(value, params.camera.width, params.camera.height)) {
                throw std::runtime_error("--resolution requires positive integers in WxH format");
            }
        } else if (option == "--integrator") {
            if (!bhr::cli::parse_integrator(value, params.integrator)) {
                throw std::runtime_error("--integrator requires rk45 or geokerr");
            }
        } else if (option == "--starfield") {
            starfield_path = value;
            params.enable_starfield = true;
        } else if (option == "--no-starfield") params.enable_starfield = false;
        else if (option == "--no-doppler") params.enable_doppler = false;
        else if (option == "--no-redshift") params.enable_redshift = false;
        else if (option == "--no-beaming") params.enable_beaming = false;
        else if (option == "--output") output_path = value;
    }
    if (output_path.empty()) throw std::runtime_error("--output is required");
    if (const char* invalid = bhr::validation_error(params)) throw std::runtime_error(invalid);
    if (params.enable_starfield && starfield_path.empty()) {
        throw std::runtime_error("Preset enables starfield: supply --starfield PATH or --no-starfield");
    }

    StarfieldOwner starfield;
    if (params.enable_starfield && !starfield_path.empty()
        && !bhr::load_starfield(starfield_path, 16384, starfield.value)) {
        throw std::runtime_error("Failed to load starfield: " + starfield_path);
    }
    std::fprintf(stderr,
        "Rendering %dx%d, integrator=%s, spin=%.3f, incl=%.1f deg, fov=%.1f deg, r_cam=%.1f M -> %s\n",
        params.camera.width, params.camera.height,
        params.integrator == bhr::IntegratorKind::kRK45 ? "rk45" : "geokerr (approximate)", params.spin,
        params.camera.theta_cam_deg, params.camera.fov_deg, params.camera.r_cam, output_path.c_str());

    bhr::Image image;
    const auto start = std::chrono::steady_clock::now();
    bhr::render(params, starfield.value, image);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (image.width == 0) throw std::runtime_error("Render failed (image is empty)");
    if (!bhr::write_png(image, output_path)) throw std::runtime_error("Failed to write PNG: " + output_path);
    if (!bhr::destroy_starfield(starfield.value)) throw std::runtime_error("Failed to release the starfield");
    std::fprintf(stderr, "Done in %.3fs -> %s\n", seconds, output_path.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Error: %s\n", error.what());
        return 1;
    }
}
