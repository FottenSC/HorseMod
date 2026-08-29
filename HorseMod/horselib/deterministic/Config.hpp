#pragma once

#include "Types.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Horse::Deterministic
{
struct Config
{
    static constexpr std::uint32_t current_version = 1;

    std::uint32_t config_version{current_version};
    bool enabled{};
    std::uint32_t rollback_window{12};
    std::uint32_t input_delay{1};
    bool trace{};
    // Baseline-preserving depth 1/6/11 owned-resimulation probe. Diagnostic
    // only; it never substitutes or mutates captured input.
    bool correction_probe{};
    // Qualification-only continuous rollback stress. This deliberately uses
    // the production correction path at the fixed acceptance depth of seven
    // frames; it is never enabled by the production allowlist.
    bool forced_depth7_qualification{};
    std::uint32_t qualification_depth{7};
    // 1 near round start, 2 active combat, 3 presentation-bearing hit,
    // 4 terminal activity. Ignored outside qualification.
    std::uint32_t qualification_location{2};
};

struct ConfigLoadResult
{
    Status status{};
    Config config{};
    std::vector<std::string> diagnostics;
};

[[nodiscard]] ConfigLoadResult LoadConfig(const std::filesystem::path& path);
}
