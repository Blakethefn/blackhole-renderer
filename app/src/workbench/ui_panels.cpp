#include "ui_panels.hpp"
#include "bhr/kerr.hpp"
#include "bhr/presets.hpp"
#include "imgui.h"
#include <algorithm>
#include <cstdint>

namespace bhr::workbench {
namespace {
constexpr int kPreviewWidths[] = {256, 512, 1024, 1920};
constexpr int kPreviewHeights[] = {144, 288, 576, 1080};
constexpr const char* kPreviewLabels[] = {"256 x 144", "512 x 288", "1024 x 576", "1920 x 1080"};

void timing(const State& state, const Viewport& viewport) {
    if (!state.has_image) {
        ImGui::TextUnformatted("No completed frame yet");
        return;
    }
    const auto& p = state.displayed;
    ImGui::Text("Last image: %d x %d / %s", p.camera.width, p.camera.height,
        p.integrator == IntegratorKind::kRK45 ? "RK45" : "Geokerr (approximate)");
    ImGui::Text("GPU render: %.2f ms", viewport.last_gpu_ms());
    const float frame_ms = viewport.last_frame_ms();
    if (frame_ms > 0.0f) {
        ImGui::Text("Frame to display: %.2f ms / %.2f FPS", frame_ms, 1000.0f / frame_ms);
        ImGui::Text("Preview target: <=333 ms (%s)", frame_ms <= 333.0f ? "met for last frame" : "not met");
    }
}
} // namespace

State draw_controls(const State& state, Controls& controls,
                    const Viewport& viewport, Starfield& starfield,
                    const char* device_name) {
    if (!state.show_controls) return state;
    auto next = state;
    auto params = state.params;
    bool edited = false;
    ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(410.0f, 660.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 240.0f),
        ImVec2(600.0f, std::max(240.0f, ImGui::GetIO().DisplaySize.y - 48.0f)));
    ImGui::SetNextWindowBgAlpha(0.94f);
    if (ImGui::Begin("Renderer Controls", &next.show_controls, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted(device_name);
        ImGui::Text("UI: %.1f FPS | F1 hides controls", ImGui::GetIO().Framerate);
        timing(state, viewport);
        ImGui::Separator();
        if (ImGui::Button("Known-good preset (R)")) {
            params = workbench_preset();
            edited = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Render again")) next = requested(next);
        if (ImGui::Checkbox("Continuous preview", &controls.continuous) && controls.continuous)
            next = requested(next);

        if (ImGui::CollapsingHeader("Camera and black hole", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::SliderFloat("Spin a/M", &params.spin, 0.0f, 0.999f, "%.3f")) {
                params.disk.r_inner = std::max(params.disk.r_inner, r_isco(params.spin));
                edited = true;
            }
            edited |= ImGui::SliderFloat("Distance (M)", &params.camera.r_cam, 3.0f, 200.0f, "%.1f");
            edited |= ImGui::SliderFloat("Inclination", &params.camera.theta_cam_deg, 0.1f, 179.9f, "%.1f deg");
            edited |= ImGui::SliderFloat("Azimuth", &params.camera.phi_cam_deg, -180.0f, 180.0f, "%.1f deg");
            edited |= ImGui::SliderFloat("Field of view", &params.camera.fov_deg, 1.0f, 120.0f, "%.1f deg");
        }
        if (ImGui::CollapsingHeader("Disk and light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("ISCO: %.3f M", r_isco(params.spin));
            edited |= ImGui::SliderFloat("Inner radius (M)", &params.disk.r_inner, 1.0f, 30.0f, "%.3f");
            edited |= ImGui::SliderFloat("Outer radius (M)", &params.disk.r_outer, 2.0f, 200.0f, "%.1f");
            edited |= ImGui::SliderFloat("Temperature (K)", &params.disk.peak_temp_K, 1000.0f, 1.0e7f, "%.0f", ImGuiSliderFlags_Logarithmic);
            edited |= ImGui::SliderFloat("Brightness", &params.disk.brightness, 0.0f, 10.0f, "%.2f");
            edited |= ImGui::Checkbox("Doppler shift", &params.enable_doppler);
            edited |= ImGui::Checkbox("Redshift / time dilation", &params.enable_redshift);
            edited |= ImGui::Checkbox("Relativistic beaming", &params.enable_beaming);
        }
        if (ImGui::CollapsingHeader("Render settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            int selected = -1;
            for (int i = 0; i < 4; ++i) {
                if (params.camera.width == kPreviewWidths[i] && params.camera.height == kPreviewHeights[i]) selected = i;
            }
            if (ImGui::Combo("Preview (1-4)", &selected, kPreviewLabels, 4)) {
                params.camera.width = kPreviewWidths[selected];
                params.camera.height = kPreviewHeights[selected];
                edited = true;
            }
            int integrator = static_cast<int>(params.integrator);
            if (ImGui::Combo("Integrator (Space)", &integrator, "RK45 (reference)\0Geokerr (approximate)\0")) {
                params.integrator = static_cast<IntegratorKind>(integrator);
                edited = true;
            }
            if (params.integrator == IntegratorKind::kGeokerr) {
                ImGui::TextWrapped("Approximate radial integration and escape direction; disk boundaries differ from RK45. Not an exact physics mode.");
            }
        }
        if (ImGui::CollapsingHeader("Starfield")) {
            ImGui::InputText("EXR path", controls.starfield_path, sizeof(controls.starfield_path));
            ImGui::BeginDisabled(viewport.busy());
            if (ImGui::Button("Load starfield")) {
                Starfield candidate{};
                if (load_starfield(controls.starfield_path, 16384, candidate)
                    && destroy_starfield(starfield)) {
                    starfield = candidate;
                    params.enable_starfield = true;
                    edited = true;
                    controls.message = "Starfield loaded.";
                } else {
                    destroy_starfield(candidate);
                    controls.message = "Starfield load or replacement failed; check the EXR format and error log.";
                }
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!starfield.is_valid() && !params.enable_starfield);
            edited |= ImGui::Checkbox("Enable starfield", &params.enable_starfield);
            ImGui::EndDisabled();
            if (!starfield.is_valid()) ImGui::TextWrapped("Load an HDR EXR map to enable lensing. Without a map, escaped rays use the diagnostic blue background.");
        }
        if (ImGui::CollapsingHeader("Presets")) {
            ImGui::InputText("JSON path", controls.preset_path, sizeof(controls.preset_path));
            if (ImGui::Button("Save preset")) {
                if (save_preset(params, controls.preset_path, controls.message)) controls.message = "Preset saved.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Load preset")) {
                auto candidate = params;
                if (load_preset(controls.preset_path, candidate, controls.message)) {
                    params = candidate;
                    edited = true;
                    controls.message = "Preset loaded. Starfield assets are loaded separately.";
                }
            }
        }
        if (const char* error = validation_error(params)) {
            ImGui::TextWrapped("Cannot render: %s", error);
        }
        if (!next.error.empty()) ImGui::TextWrapped("Render error: %s", next.error.c_str());
        if (!controls.message.empty()) ImGui::TextWrapped("%s", controls.message.c_str());
    }
    ImGui::End();
    if (edited) next = changed(next, params);
    return next;
}

void draw_viewport(const State& state, const Viewport& viewport) {
    const auto* main_viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(main_viewport->WorkPos);
    ImGui::SetNextWindowSize(main_viewport->WorkSize);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("Viewport", nullptr, flags);
    if (state.rendering) {
        ImGui::TextUnformatted("Rendering... | Showing last completed image | F1: Renderer Controls");
    } else if (const char* error = validation_error(state.params)) {
        ImGui::TextWrapped("Cannot render: %s | F1: Renderer Controls", error);
    } else if (!state.error.empty()) {
        ImGui::TextWrapped("Render error: %s | F1: Renderer Controls", state.error.c_str());
    } else {
        ImGui::TextUnformatted("Ready | F1: Renderer Controls | R: preset | Space: integrator | 1-4: resolution");
    }
    if (viewport.has_image()) {
        const auto available = ImGui::GetContentRegionAvail();
        const float width = static_cast<float>(viewport.image_width());
        const float height = static_cast<float>(viewport.image_height());
        const float scale = std::max(0.0f, std::min(available.x / width, available.y / height));
        const ImVec2 size(width * scale, height * scale);
        const auto cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(cursor.x + (available.x - size.x) * 0.5f,
                                  cursor.y + (available.y - size.y) * 0.5f));
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(viewport.texture())), size);
    }
    ImGui::End();
}
} // namespace bhr::workbench
