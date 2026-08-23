#include "Config.hpp"

#include <charconv>
#include <fstream>
#include <string_view>

namespace Horse::Deterministic
{
namespace
{
std::string_view trim(std::string_view value) noexcept
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool parse_bool(std::string_view value, bool& output) noexcept
{
    if (value == "true")
    {
        output = true;
        return true;
    }
    if (value == "false")
    {
        output = false;
        return true;
    }
    return false;
}

bool parse_u32(std::string_view value, std::uint32_t& output) noexcept
{
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, output);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}
}

ConfigLoadResult LoadConfig(const std::filesystem::path& path)
{
    ConfigLoadResult result;
    std::ifstream stream(path);
    if (!stream)
    {
        result.diagnostics.emplace_back("configuration file missing; rollback disabled");
        return result;
    }

    std::string line;
    while (std::getline(stream, line))
    {
        const std::string_view text = trim(line);
        if (text.empty() || text.front() == '#' || text.front() == ';')
        {
            continue;
        }
        const auto separator = text.find('=');
        if (separator == std::string_view::npos)
        {
            result.status = Status::failure(FailureCode::InvalidConfiguration);
            return result;
        }
        const std::string_view key = trim(text.substr(0, separator));
        const std::string_view value = trim(text.substr(separator + 1));
        bool valid = true;
        if (key == "config_version") valid = parse_u32(value, result.config.config_version);
        else if (key == "enabled") valid = parse_bool(value, result.config.enabled);
        else if (key == "rollback_window") valid = parse_u32(value, result.config.rollback_window);
        else if (key == "input_delay") valid = parse_u32(value, result.config.input_delay);
        else if (key == "trace") valid = parse_bool(value, result.config.trace);
        else
        {
            result.diagnostics.emplace_back("unsupported legacy or unknown option ignored: " + std::string(key));
            continue;
        }
        if (!valid)
        {
            result.status = Status::failure(FailureCode::InvalidConfiguration);
            return result;
        }
    }

    if (result.config.config_version != Config::current_version
        || result.config.rollback_window == 0
        || result.config.rollback_window > 30
        || result.config.input_delay > 8)
    {
        result.status = Status::failure(FailureCode::InvalidConfiguration);
    }
    return result;
}
}
