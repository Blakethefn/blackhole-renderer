#pragma once

#include "bhr/params.hpp"
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>

namespace bhr::cli {

inline bool parse_finite_float(const std::string& text, float& output) {
    if (text.empty() || std::isspace(static_cast<unsigned char>(text.front()))) return false;
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(text.c_str(), &end);
    if (errno == ERANGE || end != text.c_str() + text.size() || !std::isfinite(parsed)) return false;
    output = parsed;
    return true;
}

inline bool parse_positive_integer(std::string_view text, int& output) {
    int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed <= 0) return false;
    output = parsed;
    return true;
}

inline bool parse_resolution(std::string_view text, int& width, int& height) {
    const auto separator = text.find('x');
    if (separator == std::string_view::npos) return false;
    int parsed_width = 0;
    int parsed_height = 0;
    if (!parse_positive_integer(text.substr(0, separator), parsed_width) ||
        !parse_positive_integer(text.substr(separator + 1), parsed_height)) return false;
    width = parsed_width;
    height = parsed_height;
    return true;
}

inline bool parse_integrator(std::string_view text, IntegratorKind& output) {
    if (text != "rk45" && text != "geokerr") return false;
    output = text == "rk45" ? IntegratorKind::kRK45 : IntegratorKind::kGeokerr;
    return true;
}

} // namespace bhr::cli
