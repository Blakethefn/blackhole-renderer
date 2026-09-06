#pragma once
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bhr::cli {
struct ShotSelection {
    const std::string file, id;
    const uint64_t frame;
    const std::string output;
};
inline bool is_shot_option(const std::string& option) {
    return option == "--shot-file" || option == "--shot-id" || option == "--frame";
}
/// No selection means ordinary legacy parsing/precedence still applies.
inline std::optional<ShotSelection> shot_selection(
        const std::vector<std::pair<std::string, std::string>>& options) {
    if (std::none_of(options.begin(), options.end(), [](const auto& option) {
            return is_shot_option(option.first);
        })) return std::nullopt;
    const std::map<std::string, std::string> values = [&] {
        std::map<std::string, std::string> result;
        for (const auto& [option, value] : options) {
            if (!is_shot_option(option) && option != "--output")
                throw std::runtime_error(option + " is incompatible with shot mode");
            if (value.empty() || value.rfind("--", 0) == 0)
                throw std::runtime_error("Missing value for " + option);
            if (!result.emplace(option, value).second)
                throw std::runtime_error(option + " can only be specified once in shot mode");
        }
        return result;
    }();
    if (values.size() != 4)
        throw std::runtime_error("--shot-file, --shot-id, --frame and --output are required together");
    const auto& text = values.at("--frame");
    uint64_t frame = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), frame);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::runtime_error("--frame requires a nonnegative integer without trailing junk");
    return ShotSelection{values.at("--shot-file"), values.at("--shot-id"), frame, values.at("--output")};
}
} // namespace bhr::cli
