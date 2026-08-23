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
};

struct ConfigLoadResult
{
    Status status{};
    Config config{};
    std::vector<std::string> diagnostics;
};

[[nodiscard]] ConfigLoadResult LoadConfig(const std::filesystem::path& path);
}
