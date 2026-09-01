#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Psapi.h>

#include "ReplayPayloadImporter.hpp"
#include "ReplaySceneNavigator.hpp"
#include "OnlineRoomAutomation.hpp"
#include "deterministic/Sc6OnlineObserverProbe.hpp"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using RC::CppUserModBase;
namespace LogLevel = RC::LogLevel;
namespace Output = RC::Output;

#define HORSE_WIDEN_IMPL(value) L##value
#define HORSE_WIDEN(value) HORSE_WIDEN_IMPL(value)

namespace
{
enum class State : std::uint8_t
{
    Idle,
    Importing,
    WaitingForLaunch,
    Launched,
    Failed,
};

struct Request
{
    struct QualificationCycle
    {
        std::string run_id;
        std::uint32_t depth{};
        std::uint32_t location{};
    };
    std::string run_id;
    std::filesystem::path replay_path;
    std::uint32_t watch_frames{1};
    std::vector<std::uint32_t> seek_percentages{};
    std::uint32_t min_resume_tick_rate_milli{58'000};
    std::uint32_t resume_tick_window{120};
    std::uint32_t stage_terminal{};
    bool stock_round_outcome_control{};
    bool require_authored_outcomes{};
    std::vector<std::int8_t> expected_round_winners{};
    std::int32_t expected_match_winner{-1};
    bool development_smoke{};
    std::vector<QualificationCycle> qualification_cycles{};
    std::uint32_t qualification_anchors{40};
    std::uint32_t qualification_repeats{15};
};

struct BattleResult
{
    std::int32_t timer_seconds{};
    std::int32_t result_type{};
    std::int32_t round_winner_index{-1};
    std::int32_t match_winner_index{-1};
};

using RequestReplaySeekFn = bool (*)(std::uint64_t);
using SetReplayHistoryCaptureRequiredFn = bool (*)(bool);
using CaptureReplayQualificationTerminalEvidenceFn = bool (*)();
using GetReplaySeekStatusFn = std::uint32_t (*)(
    std::uint64_t*, std::uint64_t*, std::uint64_t*, std::uint16_t*);
using GetReplaySeekableRangeFn = bool (*)(
    std::uint64_t*, std::uint64_t*, std::uint64_t*);
using GetReplaySimulationPhaseFn = bool (*)(
    std::int32_t*, std::int32_t*, std::uint32_t*, std::int32_t*);
using GetReplaySeekMetricsFn = bool (*)(std::uint64_t*, std::uint64_t*);
using GetReplayCanonicalStateFn = bool (*)(
    std::uint64_t*, std::uint64_t*, std::byte*, std::size_t);
using GetReplayPresentationCoverageFn = bool (*)(std::uint64_t*, std::size_t);
using GetReplayPresentationIdentityFn = bool (*)(std::uint64_t*, std::size_t);
using GetReplayQualificationHealthFn = bool (*)(std::uint64_t*, std::size_t);
using ResetReplayQualificationHealthFn = bool (*)();
using GetReplayGameplayRngCoverageFn = bool (*)(std::uint64_t*, std::size_t);
using RequestStageTerminalFn = bool (*)(std::uint32_t);
using GetStageTerminalStatusFn = std::uint32_t (*)(std::uint32_t*);
using GetForcedQualificationStatusFn = std::uint32_t (*)();
using ArmReplayQualificationCycleFn = bool (*)(
    const char*, std::size_t, std::uint32_t, std::uint32_t);
using GetReplayQualificationCycleReportFn = std::uint32_t (*)(
    const char*, std::size_t, std::uint64_t*, std::size_t);
using DisarmReplayQualificationCycleFn = bool (*)(const char*, std::size_t);
using ArmReplayQualificationGroupFn = bool (*)(
    const char*, std::size_t, std::uint32_t, std::uint32_t, std::uint32_t);
using GetReplayQualificationGroupRowReportFn = std::uint32_t (*)(
    const char*, std::size_t, std::uint32_t, std::uint64_t*, std::size_t);
using ArmOnlineQualificationFn = bool (*)(
    const char*, std::size_t, std::uint32_t);
using GetOnlineQualificationStatusFn = std::uint32_t (*)();
using ArmOnlineObserverProbeFn = bool (*)(
    const Horse::Deterministic::OnlineObserverProbeRequest*);
using GetOnlineObserverProbeReportFn = std::uint32_t (*)(
    Horse::Deterministic::OnlineObserverProbeReport*);
using DisarmOnlineObserverProbeFn = void (*)();

bool ResolveHorseModSeekApi(
    RequestReplaySeekFn& request, GetReplaySeekStatusFn& status,
    GetReplaySeekableRangeFn& range,
    GetReplaySimulationPhaseFn& phase,
    GetReplaySeekMetricsFn& metrics) noexcept
{
    std::array<HMODULE, 512> modules{};
    DWORD required{};
    if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(),
            static_cast<DWORD>(sizeof(modules)), &required))
    {
        return false;
    }
    const auto count = (std::min)(modules.size(),
        static_cast<std::size_t>(required / sizeof(HMODULE)));
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto candidate_request = reinterpret_cast<RequestReplaySeekFn>(
            GetProcAddress(modules[index], "horsemod_request_replay_seek"));
        const auto candidate_status = reinterpret_cast<GetReplaySeekStatusFn>(
            GetProcAddress(modules[index], "horsemod_get_replay_seek_status"));
        const auto candidate_range = reinterpret_cast<GetReplaySeekableRangeFn>(
            GetProcAddress(modules[index],
                "horsemod_get_replay_seekable_range"));
        const auto candidate_phase = reinterpret_cast<GetReplaySimulationPhaseFn>(
            GetProcAddress(modules[index],
                "horsemod_get_replay_simulation_phase"));
        const auto candidate_metrics = reinterpret_cast<GetReplaySeekMetricsFn>(
            GetProcAddress(modules[index],
                "horsemod_get_replay_seek_metrics"));
        if (candidate_request != nullptr && candidate_status != nullptr
            && candidate_range != nullptr && candidate_phase != nullptr
            && candidate_metrics != nullptr)
        {
            request = candidate_request;
            status = candidate_status;
            range = candidate_range;
            phase = candidate_phase;
            metrics = candidate_metrics;
            return true;
        }
    }
    return false;
}

GetReplayPresentationCoverageFn ResolveHorseModPresentationCoverageApi() noexcept
{
    std::array<HMODULE, 512> modules{};
    DWORD required{};
    if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(),
            static_cast<DWORD>(sizeof(modules)), &required))
    {
        return nullptr;
    }
    const auto count = (std::min)(modules.size(),
        static_cast<std::size_t>(required / sizeof(HMODULE)));
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto candidate = reinterpret_cast<
            GetReplayPresentationCoverageFn>(GetProcAddress(modules[index],
                "horsemod_get_replay_presentation_coverage"));
        if (candidate != nullptr) return candidate;
    }
    return nullptr;
}

GetReplayPresentationIdentityFn ResolveHorseModPresentationIdentityApi() noexcept
{
    std::array<HMODULE, 512> modules{};
    DWORD required{};
    if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(),
            static_cast<DWORD>(sizeof(modules)), &required))
        return nullptr;
    const auto count = (std::min)(modules.size(),
        static_cast<std::size_t>(required / sizeof(HMODULE)));
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto candidate = reinterpret_cast<
            GetReplayPresentationIdentityFn>(GetProcAddress(modules[index],
                "horsemod_get_replay_presentation_identity"));
        if (candidate != nullptr) return candidate;
    }
    return nullptr;
}

template <typename Function>
Function ResolveHorseModExport(const char* name) noexcept
{
    std::array<HMODULE, 512> modules{};
    DWORD required{};
    if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(),
            static_cast<DWORD>(sizeof(modules)), &required))
        return nullptr;
    const auto count = (std::min)(modules.size(),
        static_cast<std::size_t>(required / sizeof(HMODULE)));
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto candidate = reinterpret_cast<Function>(
            GetProcAddress(modules[index], name));
        if (candidate != nullptr) return candidate;
    }
    return nullptr;
}

GetReplayQualificationHealthFn ResolveHorseModQualificationHealthApi() noexcept
{
    std::array<HMODULE, 512> modules{};
    DWORD required{};
    if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(),
            static_cast<DWORD>(sizeof(modules)), &required))
        return nullptr;
    const auto count = (std::min)(modules.size(),
        static_cast<std::size_t>(required / sizeof(HMODULE)));
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto candidate = reinterpret_cast<GetReplayQualificationHealthFn>(
            GetProcAddress(modules[index],
                "horsemod_get_replay_qualification_health"));
        if (candidate != nullptr) return candidate;
    }
    return nullptr;
}

ResetReplayQualificationHealthFn ResolveHorseModQualificationHealthResetApi() noexcept
{
    std::array<HMODULE, 512> modules{};
    DWORD required{};
    if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(),
            static_cast<DWORD>(sizeof(modules)), &required))
        return nullptr;
    const auto count = (std::min)(modules.size(),
        static_cast<std::size_t>(required / sizeof(HMODULE)));
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto candidate = reinterpret_cast<ResetReplayQualificationHealthFn>(
            GetProcAddress(modules[index],
                "horsemod_reset_replay_qualification_health"));
        if (candidate != nullptr) return candidate;
    }
    return nullptr;
}

GetReplayCanonicalStateFn ResolveHorseModCanonicalStateApi() noexcept
{
    std::array<HMODULE, 512> modules{};
    DWORD required{};
    if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(),
            static_cast<DWORD>(sizeof(modules)), &required))
        return nullptr;
    const auto count = (std::min)(modules.size(),
        static_cast<std::size_t>(required / sizeof(HMODULE)));
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto candidate = reinterpret_cast<GetReplayCanonicalStateFn>(
            GetProcAddress(modules[index],
                "horsemod_get_replay_canonical_state"));
        if (candidate != nullptr) return candidate;
    }
    return nullptr;
}

GetReplayGameplayRngCoverageFn ResolveHorseModGameplayRngCoverageApi() noexcept
{
    std::array<HMODULE, 512> modules{};
    DWORD required{};
    if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(),
            static_cast<DWORD>(sizeof(modules)), &required))
    {
        return nullptr;
    }
    const auto count = (std::min)(modules.size(),
        static_cast<std::size_t>(required / sizeof(HMODULE)));
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto candidate = reinterpret_cast<
            GetReplayGameplayRngCoverageFn>(GetProcAddress(modules[index],
                "horsemod_get_replay_gameplay_rng_coverage"));
        if (candidate != nullptr) return candidate;
    }
    return nullptr;
}

bool ResolveHorseModStageTerminalApi(RequestStageTerminalFn& request,
    GetStageTerminalStatusFn& status,
    GetForcedQualificationStatusFn& forced_status) noexcept
{
    std::array<HMODULE, 512> modules{};
    DWORD required{};
    if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(),
            static_cast<DWORD>(sizeof(modules)), &required))
        return false;
    const auto count = (std::min)(modules.size(),
        static_cast<std::size_t>(required / sizeof(HMODULE)));
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto candidate_request = reinterpret_cast<RequestStageTerminalFn>(
            GetProcAddress(modules[index],
                "horsemod_request_qualification_stage_terminal"));
        const auto candidate_status = reinterpret_cast<GetStageTerminalStatusFn>(
            GetProcAddress(modules[index],
                "horsemod_get_qualification_stage_terminal_status"));
        const auto candidate_forced = reinterpret_cast<GetForcedQualificationStatusFn>(
            GetProcAddress(modules[index],
                "horsemod_get_forced_qualification_status"));
        if (candidate_request != nullptr && candidate_status != nullptr)
        {
            request = candidate_request;
            status = candidate_status;
            forced_status = candidate_forced;
            return true;
        }
    }
    return false;
}

bool ResolveHorseModOnlineQualificationApi(
    ArmOnlineQualificationFn& arm,
    GetOnlineQualificationStatusFn& status) noexcept
{
    std::array<HMODULE, 512> modules{};
    DWORD required{};
    if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(),
            static_cast<DWORD>(sizeof(modules)), &required))
        return false;
    const auto count = (std::min)(modules.size(),
        static_cast<std::size_t>(required / sizeof(HMODULE)));
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto candidate_arm = reinterpret_cast<ArmOnlineQualificationFn>(
            GetProcAddress(modules[index],
                "horsemod_arm_online_qualification_v3"));
        const auto candidate_status = reinterpret_cast<
            GetOnlineQualificationStatusFn>(GetProcAddress(modules[index],
                "horsemod_get_online_qualification_status"));
        if (candidate_arm != nullptr && candidate_status != nullptr)
        {
            arm = candidate_arm;
            status = candidate_status;
            return true;
        }
    }
    return false;
}

bool ResolveHorseModOnlineObserverProbeApi(ArmOnlineObserverProbeFn& arm,
    GetOnlineObserverProbeReportFn& report,
    DisarmOnlineObserverProbeFn& disarm) noexcept
{
    std::array<HMODULE, 512> modules{};
    DWORD required{};
    if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(),
            static_cast<DWORD>(sizeof(modules)), &required))
        return false;
    const auto count = (std::min)(modules.size(),
        static_cast<std::size_t>(required / sizeof(HMODULE)));
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto candidate_arm = reinterpret_cast<ArmOnlineObserverProbeFn>(
            GetProcAddress(modules[index],
                "horsemod_arm_online_observer_probe"));
        const auto candidate_report = reinterpret_cast<
            GetOnlineObserverProbeReportFn>(GetProcAddress(modules[index],
                "horsemod_get_online_observer_probe_report"));
        const auto candidate_disarm = reinterpret_cast<
            DisarmOnlineObserverProbeFn>(GetProcAddress(modules[index],
                "horsemod_disarm_online_observer_probe"));
        if (candidate_arm != nullptr && candidate_report != nullptr
            && candidate_disarm != nullptr)
        {
            arm = candidate_arm;
            report = candidate_report;
            disarm = candidate_disarm;
            return true;
        }
    }
    return false;
}

std::filesystem::path QualificationRoot()
{
    std::wstring value(32768, L'\0');
    const DWORD count = GetEnvironmentVariableW(
        L"LOCALAPPDATA", value.data(), static_cast<DWORD>(value.size()));
    if (count == 0 || count >= value.size()) return {};
    value.resize(count);
    return std::filesystem::path(value) / L"HorseMod" / L"Qualification";
}

std::wstring Widen(std::string_view value)
{
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), output.data(), count);
    return output;
}

bool ValidRunId(std::string_view value) noexcept
{
    if (value.empty() || value.size() > 96) return false;
    for (const char character : value)
    {
        if (!((character >= 'a' && character <= 'z')
              || (character >= 'A' && character <= 'Z')
              || (character >= '0' && character <= '9')
              || character == '-' || character == '_'))
        {
            return false;
        }
    }
    return true;
}

bool ReadOnlineRequest(const std::filesystem::path& path,
    std::string& run_id, std::uint32_t& fault)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    if (size <= 0 || size > 4096) return false;
    stream.seekg(0, std::ios::beg);
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!stream.read(text.data(), size)) return false;
    bool arm{};
    std::uint32_t version{1};
    std::uint64_t not_before_unix_ms{};
    std::size_t begin{};
    while (begin < text.size())
    {
        const auto end = text.find_first_of("\r\n", begin);
        const auto line = std::string_view(text).substr(begin,
            (end == std::string::npos ? text.size() : end) - begin);
        if (!line.empty())
        {
            const auto equals = line.find('=');
            if (equals == std::string_view::npos) return false;
            const auto key = line.substr(0, equals);
            const auto value = line.substr(equals + 1);
            if (key == "version")
                version = value == "2" ? 2u : 0u;
            else if (key == "run_id") run_id.assign(value);
            else if (key == "arm") arm = value == "true";
            else if (key == "not_before_unix_ms")
            {
                const auto parsed = std::from_chars(value.data(),
                    value.data() + value.size(), not_before_unix_ms);
                if (parsed.ec != std::errc{}
                    || parsed.ptr != value.data() + value.size()) return false;
            }
            else if (key == "qualification_fault")
            {
                const auto parsed = std::from_chars(value.data(),
                    value.data() + value.size(), fault);
                if (parsed.ec != std::errc{}
                    || parsed.ptr != value.data() + value.size()
                    || fault > 6) return false;
            }
            else return false;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
        if (begin < text.size() && text[end] == '\r' && text[begin] == '\n')
            ++begin;
    }
    if (!arm || !ValidRunId(run_id) || version != 2
        || not_before_unix_ms == 0) return false;
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    return now >= not_before_unix_ms;
}

bool ReadObserverOnlyRequest(const std::filesystem::path& path,
    Horse::Deterministic::OnlineObserverProbeRequest& request)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    if (size <= 0 || size > 4096) return false;
    stream.seekg(0, std::ios::beg);
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!stream.read(text.data(), size)) return false;
    std::string run_id;
    bool arm{};
    bool observer_only{};
    std::uint32_t version{};
    std::uint32_t timeout_seconds{};
    std::size_t begin{};
    while (begin < text.size())
    {
        const auto end = text.find_first_of("\r\n", begin);
        const auto line = std::string_view(text).substr(begin,
            (end == std::string::npos ? text.size() : end) - begin);
        if (!line.empty())
        {
            const auto equals = line.find('=');
            if (equals == std::string_view::npos) return false;
            const auto key = line.substr(0, equals);
            const auto value = line.substr(equals + 1);
            if (key == "version") version = value == "1" ? 1u : 0u;
            else if (key == "request_type")
                observer_only = value == "observer_only";
            else if (key == "run_id") run_id.assign(value);
            else if (key == "arm") arm = value == "true";
            else if (key == "timeout_seconds")
            {
                const auto parsed = std::from_chars(
                    value.data(), value.data() + value.size(), timeout_seconds);
                if (parsed.ec != std::errc{}
                    || parsed.ptr != value.data() + value.size())
                    return false;
            }
            else return false;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
        if (begin < text.size() && text[end] == '\r' && text[begin] == '\n')
            ++begin;
    }
    if (!arm || !observer_only || version != 1 || !ValidRunId(run_id)
        || run_id.size() >= request.run_id.size()
        || timeout_seconds == 0
        || timeout_seconds >
            Horse::Deterministic::online_observer_probe_timeout_seconds)
        return false;
    std::memcpy(request.run_id.data(), run_id.data(), run_id.size());
    request.run_id[run_id.size()] = '\0';
    request.timeout_seconds = timeout_seconds;
    return true;
}

bool ReadRoomAutomationRequest(const std::filesystem::path& path,
                               std::string& run_id)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    std::map<std::string, std::string> fields;
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto equals = line.find('=');
        if (equals == std::string::npos
            || !fields.emplace(line.substr(0, equals),
                               line.substr(equals + 1)).second)
            return false;
    }
    if (!stream.eof() || fields.size() != 4
        || fields["version"] != "1"
        || fields["request_type"] != "host_room_create"
        || fields["arm"] != "true"
        || !ValidRunId(fields["run_id"]))
        return false;
    run_id = fields["run_id"];
    return true;
}

void WriteRoomAutomationReport(const std::string& run_id,
                               Horse::Qualification::OnlineRoomState state,
                               const std::string& detail)
{
    const auto root = QualificationRoot();
    std::error_code error;
    std::filesystem::create_directories(root, error);
    const auto temporary = root / L"online_room_report.tmp";
    const auto destination = root / L"online_room_report.json";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream << "{\n"
        << "  \"schema_version\": 1,\n"
        << "  \"kind\": \"host_room_create\",\n"
        << "  \"run_id\": \"" << run_id << "\",\n"
        << "  \"state\": \""
        << (state == Horse::Qualification::OnlineRoomState::Complete
                ? "complete" : "failed") << "\",\n"
        << "  \"detail\": \"" << detail << "\"\n"
        << "}\n";
    stream.close();
    MoveFileExW(temporary.c_str(), destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

template <std::size_t Size>
std::string HexBytes(const std::array<std::byte, Size>& bytes)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const auto value : bytes)
        stream << std::setw(2) << std::to_integer<unsigned int>(value);
    return stream.str();
}

void WriteObserverOnlyReport(
    const Horse::Deterministic::OnlineObserverProbeReport& report)
{
    using Horse::Deterministic::failure_code_name;
    const auto root = QualificationRoot();
    std::error_code error;
    std::filesystem::create_directories(root, error);
    const auto temporary = root / L"online_observer_report.tmp";
    const auto destination = root / L"online_observer_report.json";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream << "{\n"
        << "  \"schema_version\": " << report.schema_version << ",\n"
        << "  \"kind\": \"online_observer_only\",\n"
        << "  \"run_id\": \"" << report.run_id.data() << "\",\n"
        << "  \"state\": " << static_cast<std::uint32_t>(report.state) << ",\n"
        << "  \"failure\": \"" << failure_code_name(report.failure) << "\",\n"
        << "  \"elapsed_milliseconds\": " << report.elapsed_milliseconds << ",\n"
        << "  \"session\": {\n"
        << "    \"role\": " << static_cast<int>(report.session.role) << ",\n"
        << "    \"virtual_state\": "
        << static_cast<unsigned>(report.session.virtual_session_state) << ",\n"
        << "    \"local_slot\": "
        << static_cast<unsigned>(report.session.local_player_slot) << ",\n"
        << "    \"lobby_id\": " << report.session.lobby_id << ",\n"
        << "    \"session_name\": " << report.session.session_name << ",\n"
        << "    \"session_interface\": " << report.session.session_interface << ",\n"
        << "    \"active_connect\": " << report.session.active_connect << ",\n"
        << "    \"online_session\": " << report.session.online_session << ",\n"
        << "    \"named_session\": " << report.session.named_session << ",\n"
        << "    \"session_info\": " << report.session.session_info << "\n"
        << "  },\n"
        << "  \"lobby\": {\n"
        << "    \"local_steam_id\": " << report.lobby.local_steam_id << ",\n"
        << "    \"members\": [" << report.lobby.members[0] << ", "
        << report.lobby.members[1] << "],\n"
        << "    \"member_count\": "
        << static_cast<unsigned>(report.lobby.member_count) << ",\n"
        << "    \"casual_player_match\": "
        << (report.lobby.casual_player_match ? "true" : "false") << "\n"
        << "  },\n"
        << "  \"content\": {\n"
        << "    \"fighters\": [\""
        << report.battle.content.fighter_codes[0].data() << "\", \""
        << report.battle.content.fighter_codes[1].data() << "\"],\n"
        << "    \"stage_code\": \""
        << report.battle.content.stage_code.data() << "\",\n"
        << "    \"stage_package\": \"" << report.stage_package.data() << "\",\n"
        << "    \"stage_display_name\": \""
        << report.stage_display_name.data() << "\",\n"
        << "    \"loaded_package_identity\": \""
        << HexBytes(report.loaded_package_identity) << "\",\n"
        << "    \"battle_sync_object\": "
        << report.battle.battle_sync_object << ",\n"
        << "    \"characters_received\": "
        << (report.battle.characters_received ? "true" : "false") << ",\n"
        << "    \"stage_received\": "
        << (report.battle.stage_received ? "true" : "false") << "\n"
        << "  }\n"
        << "}\n";
    stream.close();
    MoveFileExW(temporary.c_str(), destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

bool ReadRequest(const std::filesystem::path& path, Request& output)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    std::map<std::string, std::string> fields;
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) return false;
        if (!fields.emplace(line.substr(0, separator),
                            line.substr(separator + 1)).second)
        {
            return false;
        }
    }
    if (!stream.eof() || !ValidRunId(fields["run_id"])
        || (fields["version"] != "2" && fields["version"] != "3"
            && fields["version"] != "4" && fields["version"] != "5"
            && fields["version"] != "6" && fields["version"] != "7"
            && fields["version"] != "8" && fields["version"] != "9"
            && fields["version"] != "10" && fields["version"] != "11")
        || (fields["version"] == "2" && fields.size() != 4)
        || (fields["version"] == "3" && fields.size() != 5)
        || (fields["version"] == "4" && fields.size() != 7)
        || (fields["version"] == "5" && fields.size() != 8)
        || (fields["version"] == "6" && fields.size() != 9)
        || (fields["version"] == "7" && fields.size() != 10)
        || (fields["version"] == "8" && fields.size() != 12)
        || (fields["version"] == "9" && fields.size() != 13)
        || (fields["version"] == "10" && fields.size() != 14)
        || (fields["version"] == "11" && fields.size() != 16))
    {
        return false;
    }
    const std::wstring replay_path = Widen(fields["replay_path"]);
    if (replay_path.empty()) return false;
    std::uint32_t watch_frames = 0;
    try
    {
        const unsigned long parsed = std::stoul(fields["watch_frames"]);
        if (parsed == 0 || parsed > 36000) return false;
        watch_frames = static_cast<std::uint32_t>(parsed);
    }
    catch (...) { return false; }
    std::vector<std::uint32_t> percentages;
    if (fields["version"] == "3" || fields["version"] == "4"
        || fields["version"] == "5" || fields["version"] == "6"
        || fields["version"] == "7" || fields["version"] == "8"
        || fields["version"] == "9" || fields["version"] == "10"
        || fields["version"] == "11")
    {
        std::string_view remaining = fields["seek_percentages"];
        while (!remaining.empty())
        {
            const auto comma = remaining.find(',');
            const auto token = remaining.substr(0, comma);
            try
            {
                const auto value = std::stoul(std::string(token));
                if (value == 0 || value >= 100 || percentages.size() >= 16)
                    return false;
                percentages.push_back(static_cast<std::uint32_t>(value));
            }
            catch (...) { return false; }
            if (comma == std::string_view::npos) break;
            remaining.remove_prefix(comma + 1);
        }
        if (percentages.empty() && fields["version"] != "5"
            && fields["version"] != "6" && fields["version"] != "7"
            && fields["version"] != "8" && fields["version"] != "9"
            && fields["version"] != "10" && fields["version"] != "11")
            return false;
    }
    std::uint32_t min_resume_tick_rate_milli = 58'000;
    std::uint32_t resume_tick_window = 120;
    if (fields["version"] == "4" || fields["version"] == "5"
        || fields["version"] == "6" || fields["version"] == "7"
        || fields["version"] == "8" || fields["version"] == "9"
        || fields["version"] == "10" || fields["version"] == "11")
    {
        try
        {
            const auto rate = std::stoul(fields["min_resume_tick_rate_milli"]);
            const auto window = std::stoul(fields["resume_tick_window"]);
            if (rate < 1'000 || rate > 1'000'000
                || window == 0 || window > 36'000)
            {
                return false;
            }
            min_resume_tick_rate_milli = static_cast<std::uint32_t>(rate);
            resume_tick_window = static_cast<std::uint32_t>(window);
        }
        catch (...) { return false; }
    }
    std::uint32_t stage_terminal{};
    if (fields["version"] == "5"
        || ((fields["version"] == "6" || fields["version"] == "7"
                || fields["version"] == "8")
                || fields["version"] == "9" || fields["version"] == "10"
                || fields["version"] == "11")
            && !fields["stage_terminal"].empty())
    {
        if (fields["stage_terminal"] == "wall") stage_terminal = 1;
        else if (fields["stage_terminal"] == "barrier") stage_terminal = 2;
        else if (fields["stage_terminal"] == "both") stage_terminal = 3;
        else return false;
    }
    const bool stock_round_outcome_control =
        (fields["version"] == "6" || fields["version"] == "7"
            || fields["version"] == "8" || fields["version"] == "9"
            || fields["version"] == "10" || fields["version"] == "11")
        ? fields["stock_round_outcome_control"] == "true"
        : percentages.empty() && stage_terminal == 0;
    if ((fields["version"] == "6" || fields["version"] == "7"
            || fields["version"] == "8" || fields["version"] == "9"
            || fields["version"] == "10" || fields["version"] == "11")
        && fields["stock_round_outcome_control"] != "true"
        && fields["stock_round_outcome_control"] != "false")
        return false;
    const bool require_authored_outcomes =
        (fields["version"] == "7" || fields["version"] == "8"
            || fields["version"] == "9" || fields["version"] == "10"
            || fields["version"] == "11")
        && fields["require_authored_outcomes"] == "true";
    if ((fields["version"] == "7" || fields["version"] == "8"
            || fields["version"] == "9" || fields["version"] == "10"
            || fields["version"] == "11")
        && fields["require_authored_outcomes"] != "true"
        && fields["require_authored_outcomes"] != "false")
        return false;
    std::vector<std::int8_t> expected_round_winners;
    std::int32_t expected_match_winner = -1;
    if (fields["version"] == "8" || fields["version"] == "9"
        || fields["version"] == "10" || fields["version"] == "11")
    {
        std::string_view remaining = fields["expected_round_winners"];
        while (!remaining.empty())
        {
            const auto comma = remaining.find(',');
            const auto token = remaining.substr(0, comma);
            if (token.size() != 1 || token[0] < '0' || token[0] > '2'
                || expected_round_winners.size()
                    >= Horse::Qualification::ReplayMetadata::kMaximumRoundStarts)
                return false;
            expected_round_winners.push_back(
                static_cast<std::int8_t>(token[0] - '0'));
            if (comma == std::string_view::npos) break;
            remaining.remove_prefix(comma + 1);
        }
        if (!fields["expected_match_winner"].empty())
        {
            if (fields["expected_match_winner"] != "0"
                && fields["expected_match_winner"] != "1") return false;
            expected_match_winner = fields["expected_match_winner"][0] - '0';
        }
        if (require_authored_outcomes && !stock_round_outcome_control
            && (expected_round_winners.empty()
                || expected_match_winner < 0)) return false;
    }
    const bool development_smoke = (fields["version"] == "9"
            || fields["version"] == "10" || fields["version"] == "11")
        && fields["development_smoke"] == "true";
    if ((fields["version"] == "9" || fields["version"] == "10"
            || fields["version"] == "11")
        && fields["development_smoke"] != "true"
        && fields["development_smoke"] != "false") return false;
    if (development_smoke
        && (watch_frames < 60 || watch_frames > 120
            || stock_round_outcome_control || require_authored_outcomes
            || stage_terminal != 0 || !percentages.empty())) return false;
    std::vector<Request::QualificationCycle> qualification_cycles;
    if (fields["version"] == "10" || fields["version"] == "11")
    {
        std::string_view remaining = fields["qualification_cycles"];
        while (!remaining.empty())
        {
            const auto comma = remaining.find(',');
            const auto token = remaining.substr(0, comma);
            const auto first = token.find(':');
            const auto second = first == std::string_view::npos
                ? first : token.find(':', first + 1);
            if (first == std::string_view::npos
                || second == std::string_view::npos
                || qualification_cycles.size() >= 128) return false;
            const std::string cycle_id(token.substr(0, first));
            if (!ValidRunId(cycle_id)) return false;
            try
            {
                const auto depth = std::stoul(std::string(
                    token.substr(first + 1, second - first - 1)));
                const auto location = std::stoul(std::string(
                    token.substr(second + 1)));
                if ((depth != 1 && depth != 6 && depth != 11)
                    || location < 1 || location > 4) return false;
                if (std::any_of(qualification_cycles.begin(),
                        qualification_cycles.end(), [&](const auto& cycle) {
                            return cycle.run_id == cycle_id;
                        })) return false;
                qualification_cycles.push_back({cycle_id,
                    static_cast<std::uint32_t>(depth),
                    static_cast<std::uint32_t>(location)});
            }
            catch (...) { return false; }
            if (comma == std::string_view::npos) break;
            remaining.remove_prefix(comma + 1);
        }
        if (qualification_cycles.empty() || qualification_cycles.size() % 3 != 0
            || development_smoke
            || stock_round_outcome_control || require_authored_outcomes
            || stage_terminal != 0 || !percentages.empty()) return false;
        for (std::size_t index = 0; index < qualification_cycles.size();
             index += 3)
        {
            const auto location = qualification_cycles[index].location;
            if (qualification_cycles[index].depth != 11
                || qualification_cycles[index + 1].depth != 1
                || qualification_cycles[index + 2].depth != 6
                || qualification_cycles[index + 1].location != location
                || qualification_cycles[index + 2].location != location)
                return false;
        }
    }
    std::uint32_t qualification_anchors = 40;
    std::uint32_t qualification_repeats = 15;
    if (fields["version"] == "11")
    {
        try
        {
            qualification_anchors = static_cast<std::uint32_t>(
                std::stoul(fields["qualification_anchors"]));
            qualification_repeats = static_cast<std::uint32_t>(
                std::stoul(fields["qualification_repeats"]));
        }
        catch (...) { return false; }
        if (qualification_anchors == 0 || qualification_anchors > 40
            || qualification_repeats == 0 || qualification_repeats > 15)
            return false;
    }
    output = {fields["run_id"], std::filesystem::path(replay_path),
        watch_frames, std::move(percentages), min_resume_tick_rate_milli,
        resume_tick_window, stage_terminal, stock_round_outcome_control,
        require_authored_outcomes, std::move(expected_round_winners),
        expected_match_winner, development_smoke,
        std::move(qualification_cycles), qualification_anchors,
        qualification_repeats};
    return output.replay_path.is_absolute();
}

bool ReadPayload(const std::filesystem::path& path,
                 std::vector<std::byte>& output)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return false;
    const std::streamsize size = stream.tellg();
    if (size < 8 || size > 64 * 1024 * 1024) return false;
    output.resize(static_cast<std::size_t>(size));
    stream.seekg(0);
    return static_cast<bool>(stream.read(
        reinterpret_cast<char*>(output.data()), size));
}

RC::Unreal::UObject* TryResolveOwningGameInstance(
    RC::Unreal::UObject* manager) noexcept
{
    if (manager == nullptr || !RC::Unreal::UObject::IsReal(manager)) return nullptr;
    __try
    {
        auto** scene = manager->GetValuePtrByPropertyNameInChain<
            RC::Unreal::UObject*>(L"CurrentScene");
        if (scene == nullptr || *scene == nullptr
            || !RC::Unreal::UObject::IsReal(*scene))
        {
            return nullptr;
        }
        auto* world = reinterpret_cast<RC::Unreal::UObject*>(manager->GetWorld());
        if (world == nullptr || !RC::Unreal::UObject::IsReal(world)) return nullptr;
        auto** instance = world->GetValuePtrByPropertyNameInChain<
            RC::Unreal::UObject*>(L"OwningGameInstance");
        if (instance == nullptr || *instance == nullptr
            || !RC::Unreal::UObject::IsReal(*instance)
            || (*instance)->GetFunctionByNameInChain(L"GetBattleSetup") == nullptr)
        {
            return nullptr;
        }
        return *instance;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

bool LogAuthoredMapPackages(RC::Unreal::UObject* manager) noexcept
{
    struct ArrayHeader
    {
        RC::Unreal::UObject** data{};
        std::int32_t count{};
        std::int32_t capacity{};
    };
    if (manager == nullptr || !RC::Unreal::UObject::IsReal(manager)) return false;
    {
        auto* world = reinterpret_cast<RC::Unreal::UObject*>(manager->GetWorld());
        if (world == nullptr || !RC::Unreal::UObject::IsReal(world)) return false;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] authored map world={}\n"),
            world->GetFullName());
        auto* streaming = world->GetValuePtrByPropertyNameInChain<ArrayHeader>(
            L"StreamingLevels");
        if (streaming == nullptr || streaming->data == nullptr
            || streaming->count <= 0 || streaming->count > 256)
            return true;
        for (std::int32_t index = 0; index < streaming->count; ++index)
        {
            auto* level = streaming->data[index];
            if (level == nullptr || !RC::Unreal::UObject::IsReal(level)) continue;
            auto* package = level->GetValuePtrByPropertyNameInChain<
                RC::Unreal::FName>(L"PackageNameToLoad");
            if (package == nullptr) continue;
            const auto name = package->ToString();
            if (!name.empty())
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] authored map package[{}]={}\n"),
                    index, name);
        }
    }
    return true;
}

RC::Unreal::UObject* FindBattleManager() noexcept
{
    std::vector<RC::Unreal::UObject*> managers;
    RC::Unreal::UObjectGlobals::FindAllOf(L"LuxBattleManager", managers);
    for (auto* manager : managers)
        if (manager != nullptr && RC::Unreal::UObject::IsReal(manager))
            return manager;
    return nullptr;
}

RC::Unreal::UObject* FindGameInstance() noexcept
{
    std::vector<RC::Unreal::UObject*> managers;
    RC::Unreal::UObjectGlobals::FindAllOf(L"LuxUIGameFlowManager", managers);
    for (RC::Unreal::UObject* manager : managers)
    {
        if (RC::Unreal::UObject* instance =
                TryResolveOwningGameInstance(manager))
        {
            return instance;
        }
    }
    return nullptr;
}

bool TryReadBattleResult(
    RC::Unreal::UObject* manager, BattleResult& output) noexcept
{
    if (manager == nullptr || !RC::Unreal::UObject::IsReal(manager))
        return false;
    __try
    {
        BattleResult* result =
            manager->GetValuePtrByPropertyNameInChain<BattleResult>(
                L"BattleResult");
        if (result == nullptr) return false;
        output = *result;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

RC::Unreal::UObject* GetBattleSetup(RC::Unreal::UObject* instance) noexcept
{
    if (instance == nullptr) return nullptr;
    __try
    {
        auto* function = instance->GetFunctionByNameInChain(L"GetBattleSetup");
        if (function == nullptr) return nullptr;
        struct Params { RC::Unreal::UObject* result{}; } params{};
        instance->ProcessEvent(function, &params);
        return params.result;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

bool CallNoParams(RC::Unreal::UObject* object, const wchar_t* name) noexcept
{
    if (object == nullptr) return false;
    __try
    {
        auto* function = object->GetFunctionByNameInChain(name);
        if (function == nullptr) return false;
        std::byte params{};
        object->ProcessEvent(function, &params);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadLuxBattleFrame(std::uint32_t& output) noexcept
{
    constexpr std::uintptr_t kFrameCounterRva = 0x470d0c4;
    const auto image_base = reinterpret_cast<std::uintptr_t>(
        GetModuleHandleW(nullptr));
    if (image_base == 0) return false;
    __try
    {
        output = *reinterpret_cast<const std::uint32_t*>(
            image_base + kFrameCounterRva);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

}

class ReplayQualificationMod final : public CppUserModBase
{
public:
    ReplayQualificationMod()
    {
        ModName = STR("ReplayQualificationMod");
        ModVersion = STR("1.0.0");
        ModDescription = STR("Test-only SC6 replay entry bridge");
        ModAuthors = STR("HorseMod qualification");
    }

    ~ReplayQualificationMod() override
    {
        if (disarm_online_observer_ != nullptr)
            disarm_online_observer_();
        s_instance_.store(nullptr, std::memory_order_release);
        if (battle_terminate_hook_registered_)
        {
            try
            {
                RC::Unreal::UObjectGlobals::UnregisterHook(
                    battle_terminate_hook_path_, battle_terminate_hook_ids_);
            }
            catch (...) {}
        }
        if (engine_tick_id_ != RC::Unreal::Hook::ERROR_ID)
        {
            (void)RC::Unreal::Hook::UnregisterCallback(engine_tick_id_);
        }
    }

    void on_unreal_init() override
    {
        bound_ = importer_.Bind(reinterpret_cast<std::uintptr_t>(
            GetModuleHandleW(nullptr)));
        bound_ = bound_ && navigator_.Bind(reinterpret_cast<std::uintptr_t>(
            GetModuleHandleW(nullptr)));
        bound_ = bound_ && room_automation_.Bind(
            reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)));
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] source={} native_import={}\n"),
            HORSE_WIDEN(REPLAY_QUALIFICATION_SOURCE_COMMIT),
            bound_ ? STR("ready") : STR("blocked"));
        if (!bound_) return;
        s_instance_.store(this, std::memory_order_release);
        RC::Unreal::Hook::FCallbackOptions options{};
        options.bReadonly = true;
        options.OwnerModName = STR("ReplayQualificationMod");
        options.HookName = STR("ReplayEntry");
        engine_tick_id_ = RC::Unreal::Hook::RegisterEngineTickPostCallback(
            [](RC::Unreal::Hook::TCallbackIterationData<void>&,
               RC::Unreal::UEngine*, float, bool) {
                ReplayQualificationMod* self =
                    s_instance_.load(std::memory_order_acquire);
                if (self != nullptr) self->TickGameThread();
            }, options);
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] game-thread replay entry armed id={}\n"),
            engine_tick_id_);
    }

private:
    void TickGameThread()
    {
        if (!bound_) return;
        if (!battle_terminate_hook_registered_
            && ++battle_terminate_hook_poll_divider_ >= 60)
        {
            battle_terminate_hook_poll_divider_ = 0;
            TryRegisterBattleTerminateHook();
        }
        if (state_ == State::WaitingForLaunch)
        {
            PollLaunch();
            return;
        }
        if (++poll_divider_ < 15)
        {
            return;
        }
        poll_divider_ = 0;
        PollOnlineRoomAutomation();
        const bool observer_only_request = PollOnlineObserverOnly();
        if (!observer_only_request) PollOnlineQualification();
        if (state_ == State::Launched || state_ == State::Failed) return;
        if (state_ == State::Idle) LoadRequest();
        if (state_ == State::Importing) StartRequest();
    }

    void TryRegisterBattleTerminateHook()
    {
        using namespace RC::Unreal;
        UFunction* function = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, battle_terminate_hook_path_);
        if (function == nullptr) return;
        UnrealScriptFunctionCallable pre_callback =
            [](UnrealScriptFunctionCallableContext&, void*) {
                ReplayQualificationMod* self =
                    s_instance_.load(std::memory_order_acquire);
                if (self == nullptr
                    || self->state_ != State::WaitingForLaunch
                    || !self->battle_scene_observed_)
                    return;
                // Capture the final authored result while BattleManager is
                // still valid. HorseMod independently freezes its value-only
                // terminal evidence in the same pre-native boundary, so hook
                // callback ordering cannot extend native object lifetime.
                if (self->request_.require_authored_outcomes
                    || self->request_.stock_round_outcome_control)
                    (void)self->PollRoundOutcomeQualification();
                self->terminal_snapshot_captured_ =
                    self->capture_terminal_evidence_ != nullptr
                    && self->capture_terminal_evidence_();
                self->battle_terminate_observed_ = true;
            };
        UnrealScriptFunctionCallable post_callback =
            [](UnrealScriptFunctionCallableContext&, void*) {};
        try
        {
            battle_terminate_hook_ids_ = UObjectGlobals::RegisterHook(
                battle_terminate_hook_path_, pre_callback,
                post_callback, nullptr);
        }
        catch (...) { return; }
        battle_terminate_hook_registered_ =
            battle_terminate_hook_ids_.first != 0
            || battle_terminate_hook_ids_.second != 0;
    }

    void PollOnlineQualification()
    {
        std::string run_id;
        std::uint32_t fault{};
        if (!ReadOnlineRequest(
                QualificationRoot() / L"online_request.txt", run_id, fault))
            return;
        if (run_id != online_last_run_id_)
        {
            if ((arm_online_ == nullptr || get_online_status_ == nullptr)
                && !ResolveHorseModOnlineQualificationApi(
                    arm_online_, get_online_status_))
                return;
            if (!arm_online_(run_id.data(), run_id.size(), fault)) return;
            online_last_run_id_ = run_id;
            online_last_status_ = UINT32_MAX;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] armed online qualification "
                "run_id={} fault={} without menu navigation\n"),
                RC::to_generic_string(run_id), fault);
        }
        if (get_online_status_ != nullptr)
        {
            const auto status = get_online_status_();
            if (status != online_last_status_)
            {
                online_last_status_ = status;
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] online qualification "
                    "run_id={} status={}\n"),
                    RC::to_generic_string(online_last_run_id_), status);
            }
        }
    }

    void PollOnlineRoomAutomation()
    {
        std::string run_id;
        const bool present = ReadRoomAutomationRequest(
            QualificationRoot() / L"online_room_request.txt", run_id);
        if (!present)
        {
            if (!online_room_run_id_.empty()) room_automation_.Reset();
            online_room_run_id_.clear();
            online_room_terminal_ = false;
            return;
        }
        if (run_id != online_room_run_id_)
        {
            room_automation_.Reset();
            online_room_run_id_ = run_id;
            online_room_terminal_ = false;
            online_room_last_detail_.clear();
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] armed stock host room creation "
                "run_id={}\n"), RC::to_generic_string(run_id));
        }
        if (online_room_terminal_) return;
        std::string detail;
        const auto state = room_automation_.Tick(detail);
        if (detail != online_room_last_detail_)
        {
            online_room_last_detail_ = detail;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] stock host room creation "
                "run_id={} detail={}\n"),
                RC::to_generic_string(run_id),
                RC::to_generic_string(detail));
        }
        if (state == Horse::Qualification::OnlineRoomState::Waiting) return;
        WriteRoomAutomationReport(run_id, state, detail);
        online_room_terminal_ = true;
    }

    bool PollOnlineObserverOnly()
    {
        using namespace Horse::Deterministic;
        OnlineObserverProbeRequest request{};
        const bool present = ReadObserverOnlyRequest(
            QualificationRoot() / L"online_observer_request.txt", request);
        const std::string run_id = present
            ? std::string(request.run_id.data()) : std::string{};
        if (!present)
        {
            if (online_observer_active_ && disarm_online_observer_ != nullptr)
                disarm_online_observer_();
            online_observer_active_ = false;
            return false;
        }
        if (run_id != online_observer_last_run_id_)
        {
            if ((arm_online_observer_ == nullptr
                    || get_online_observer_report_ == nullptr
                    || disarm_online_observer_ == nullptr)
                && !ResolveHorseModOnlineObserverProbeApi(
                    arm_online_observer_, get_online_observer_report_,
                    disarm_online_observer_))
                return true;
            if (!arm_online_observer_(&request)) return true;
            online_observer_last_run_id_ = run_id;
            online_observer_active_ = true;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] armed observer-only online probe "
                "run_id={}\n"), RC::to_generic_string(run_id));
        }
        if (!online_observer_active_ || get_online_observer_report_ == nullptr)
            return true;
        OnlineObserverProbeReport report{};
        const auto state = static_cast<OnlineObserverProbeState>(
            get_online_observer_report_(&report));
        if (state != OnlineObserverProbeState::Complete
            && state != OnlineObserverProbeState::Expired
            && state != OnlineObserverProbeState::Failed)
            return true;
        if (std::string_view(report.run_id.data()) != run_id) return true;
        WriteObserverOnlyReport(report);
        disarm_online_observer_();
        online_observer_active_ = false;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] observer-only online probe completed "
            "run_id={} map={} state={}\n"),
            RC::to_generic_string(run_id),
            RC::to_generic_string(std::string(
                report.stage_display_name.data())),
            static_cast<std::uint32_t>(state));
        return true;
    }

    void LoadRequest()
    {
        Request request{};
        if (!ReadRequest(QualificationRoot() / L"replay_request.txt", request)) return;
        if (request.run_id == last_run_id_) return;
        last_run_id_ = request.run_id;
        request_ = std::move(request);
        started_ = std::chrono::steady_clock::now();
        player_profiles_requested_ = false;
        playback_context_staged_ = false;
        battle_scene_observed_ = false;
        battle_terminate_observed_ = false;
        terminal_snapshot_captured_ = false;
        replay_scene_ready_ = false;
        authored_map_logged_ = false;
        battle_manager_ = nullptr;
        initial_battle_frame_ = 0;
        observed_battle_frame_ = 0;
        battle_rate_started_at_ = {};
        battle_rate_logged_ = false;
        battle_active_rate_started_at_ = {};
        battle_active_rate_start_frame_ = 0;
        battle_active_rate_round_ = 0;
        battle_active_rate_logged_ = false;
        seek_index_ = 0;
        seek_requested_ = false;
        requested_seek_target_ = 0;
        seek_range_generation_ = 0;
        seek_range_first_ = 0;
        seek_range_last_ = 0;
        seek_history_verified_ = 0;
        seek_completed_source_ = 0;
        seek_validation_ns_ = 0;
        seek_resimulation_coordinates_ = 0;
        seek_resume_start_frame_ = 0;
        seek_resume_rate_frames_ = 0;
        seek_resume_rate_elapsed_us_ = 0;
        seek_resume_last_observed_frame_ = 0;
        seek_resume_last_round_state_frame_ = 0;
        seek_resume_observation_active_ = false;
        phase_wait_log_counter_ = 0;
        replay_metadata_ = {};
        observed_round_winner_count_ = 0;
        observed_round_winners_.clear();
        have_last_round_result_ = false;
        round_result_armed_ = true;
        last_round_result_ = {};
        round_outcomes_verified_ = false;
        stage_terminal_requested_ = false;
        stage_terminal_completed_ = false;
        stage_terminal_operation_ = request_.stage_terminal == 3
            ? 1 : request_.stage_terminal;
        qualification_cycle_index_ = 0;
        qualification_cycle_armed_ = false;
        qualification_group_run_id_.clear();
        arm_qualification_group_ = nullptr;
        get_qualification_group_row_report_ = nullptr;
        disarm_qualification_cycle_ = nullptr;
        state_ = State::Importing;
        const bool history_required = !request_.seek_percentages.empty();
        const auto set_history = ResolveHorseModExport<
            SetReplayHistoryCaptureRequiredFn>(
                "horsemod_set_replay_history_capture_required");
        capture_terminal_evidence_ = ResolveHorseModExport<
            CaptureReplayQualificationTerminalEvidenceFn>(
                "horsemod_capture_replay_qualification_terminal_evidence");
        if (set_history == nullptr || !set_history(history_required)
            || capture_terminal_evidence_ == nullptr)
        {
            Fail("horsemod_replay_history_mode_unavailable");
            return;
        }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] accepted replay request run_id={} "
            "history_required={}\n"),
            RC::to_generic_string(request_.run_id),
            history_required ? 1 : 0);
    }

    void StartRequest()
    {
        RC::Unreal::UObject* instance = FindGameInstance();
        RC::Unreal::UObject* setup = GetBattleSetup(instance);
        if (instance == nullptr || setup == nullptr)
        {
            if (!waiting_context_logged_)
            {
                waiting_context_logged_ = true;
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] waiting for game instance/setup\n"));
            }
            return;
        }
        waiting_context_logged_ = false;
        std::vector<std::byte> payload;
        if (!ReadPayload(request_.replay_path, payload))
        {
            Fail("replay_file_unreadable");
            return;
        }
        Horse::Qualification::ReplayMetadata metadata{};
        const Horse::Qualification::ImportFailure imported =
            importer_.Import(payload, metadata);
        if (imported == Horse::Qualification::ImportFailure::SaveManagerUnavailable)
            return;
        if (imported != Horse::Qualification::ImportFailure::None)
        {
            Fail(Horse::Qualification::import_failure_name(imported));
            return;
        }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] replay metadata stage={} map={} "
            "left_character={} right_character={} state_reset_records={}\n"),
            metadata.stage_index,
            metadata.stage_index > 0xff
                ? metadata.stage_index & 0xff : metadata.stage_index,
            metadata.left_character, metadata.right_character,
            metadata.state_reset_record_count);
        replay_metadata_ = metadata;
        state_ = State::WaitingForLaunch;
        WriteResult("waiting_for_launch", "none");
    }

    bool PollFailFastHealth(std::uint32_t frame)
    {
        const auto get_health = ResolveHorseModQualificationHealthApi();
        if (get_health == nullptr)
        {
            Fail("horsemod_qualification_health_api_unavailable");
            return false;
        }
        std::array<std::uint64_t, 54> health{};
        // A canonical frame may not exist on the first active replay tick.
        // That is not a failure; poll again on the next engine tick.
        if (!get_health(health.data(), health.size())) return true;
        // health[30] intentionally remains diagnostic-only: it includes masked
        // x87/MXCSR exception-status changes that normal simulation produces on
        // every outer tick. Control-environment correctness remains part of the
        // native final timeline evidence and any real failure is represented by
        // health[21].
        const bool terminal_failure = health[0] != 0 || health[1] != 0
            || health[2] != 0 || health[5] != 0 || health[6] != 0
            || health[21] != 0 || health[26] != 0 || health[27] != 0
            || health[31] != 0 || health[48] != 0;
        if (!terminal_failure) return true;
        Output::send<LogLevel::Error>(STR(
            "[ReplayQualification] fail-fast health frame={} "
            "timeline_failure={} last_coordinate={}:{} "
            "canonical_failure={}:{} identity_issue={} "
            "identity_expected=0x{:016x} identity_observed=0x{:016x} "
            "presentation_failure={} event_kind={} event_identity=0x{:016x} "
            "capacity_failures={} growth_events={} accounting_failures={} "
            "cursor_mismatches={} batch_accounting_mismatches={} "
            "round_transition_barriers={} "
            "cursor_failure_coordinate={}:{} cursor_input={}:{} "
            "cursor_manager={}:{} pending_dispatch={} "
            "round_image_applied={} round_state={} "
            "timeline_partial={} partial_reason={} "
            "partial_coordinate={}:{} checkpoint_failure={} "
            "batch_entry_checkpoint_failure={} "
            "duplicates={} publish_failures={} fp_mismatches={} "
            "unknown_rng_callers={} audio_sources={} audio_terminals={}\n"),
            frame, health[21], health[22], health[23], health[24], health[25],
            health[26], health[32], health[33], health[27], health[28],
            health[29], health[0], health[1], health[2], health[36], health[37],
            health[38], health[39], health[40],
            static_cast<std::int64_t>(health[41]),
            static_cast<std::int64_t>(health[42]),
            static_cast<std::int64_t>(health[43]), health[44], health[45],
            health[46], health[47], health[48], health[49], health[50],
            health[51], health[52], health[53], health[5], health[6],
            health[30], health[31], health[34], health[35]);
        Fail("horsemod_fail_fast_health");
        return false;
    }

    bool PollQualificationCycles()
    {
        if (request_.qualification_cycles.empty()) return false;
        if (qualification_cycle_index_ >= request_.qualification_cycles.size())
        {
            WriteResult("launch_requested", "qualification_groups_passed");
            require_replay_list_before_ready_ = true;
            state_ = State::Idle;
            return true;
        }
        if (arm_qualification_group_ == nullptr)
        {
            arm_qualification_group_ = ResolveHorseModExport<
                ArmReplayQualificationGroupFn>(
                    "horsemod_arm_replay_qualification_group_v1");
            get_qualification_group_row_report_ = ResolveHorseModExport<
                GetReplayQualificationGroupRowReportFn>(
                    "horsemod_get_replay_qualification_group_row_report_v1");
            disarm_qualification_cycle_ = ResolveHorseModExport<
                DisarmReplayQualificationCycleFn>(
                    "horsemod_disarm_replay_qualification_cycle_v1");
            if (arm_qualification_group_ == nullptr
                || get_qualification_group_row_report_ == nullptr
                || disarm_qualification_cycle_ == nullptr)
            {
                Fail("horsemod_qualification_group_api_unavailable");
                return true;
            }
        }
        const auto& first = request_.qualification_cycles[
            qualification_cycle_index_];
        if (!qualification_cycle_armed_)
        {
            qualification_group_run_id_ = first.run_id;
            if (!arm_qualification_group_(qualification_group_run_id_.data(),
                    qualification_group_run_id_.size(), first.location,
                    request_.qualification_anchors,
                    request_.qualification_repeats))
            {
                qualification_group_run_id_.clear();
                Fail("horsemod_qualification_group_arm_rejected");
                return true;
            }
            qualification_cycle_armed_ = true;
            return true;
        }
        std::array<std::uint64_t, 50> first_report{};
        const auto status = get_qualification_group_row_report_(
            qualification_group_run_id_.data(),
            qualification_group_run_id_.size(), 0, first_report.data(),
            first_report.size());
        if (status == 0) return true;
        if (status != 3 && status != 4) return true;
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        const bool memory_valid = K32GetProcessMemoryInfo(
            GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(
                &memory), sizeof(memory)) != FALSE;
        std::array<std::array<std::uint64_t, 50>, 3> reports{};
        bool reports_valid = true;
        for (std::uint32_t row = 0; row < 3; ++row)
        {
            const auto row_status = get_qualification_group_row_report_(
                qualification_group_run_id_.data(),
                qualification_group_run_id_.size(), row,
                reports[row].data(), reports[row].size());
            reports_valid = reports_valid && row_status == status;
            const auto& report = reports[row];
            const auto& cycle = request_.qualification_cycles[
                qualification_cycle_index_ + row];
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] qualification cycle terminal "
                "run_id={} ordinal={} depth={} location={} status={} "
                "completed={}/{} failure={} generations={}-{} transitions={} "
                "frames={}-{} cycle_p99_us={} cycle_max_us={} "
                "capture_p99_us={} capture_max_us={} capacity_growth={} "
                "owned_bytes={}->{} timeline_bytes={} forced_bytes={} "
                "presentation_bytes={} scratch_bytes={} pending={}/{} "
                "duplicates={} publish_failures={} elapsed_ms={} drift_ms={} "
                "working_set_bytes={} private_bytes={} "
                "presentation_activity={}/{}/{} terminal_coverage={} "
                "anchors={}/{} repeats={} anchor_hash=0x{:016x} "
                "failure_anchor={} failure_repeat={}\n"),
                RC::to_generic_string(cycle.run_id), report[4], report[2],
                report[3], status, report[5], report[6], report[7], report[8],
                report[9], report[10], report[11], report[12],
                report[13] / 1000, report[14] / 1000, report[15] / 1000,
                report[16] / 1000, report[17], report[18], report[19],
                report[20], report[21], report[22], report[23], report[24],
                report[25], report[27], report[28], report[29], report[30],
                memory_valid ? memory.WorkingSetSize : 0,
                memory_valid ? memory.PrivateUsage : 0, report[40],
                report[41], report[42], report[43], report[44], report[45],
                report[46], report[47], report[48], report[49]);
        }
        const bool anchor_identity_valid = reports[0][47] != 0
            && reports[0][47] == reports[1][47]
            && reports[0][47] == reports[2][47];
        const bool disarmed = disarm_qualification_cycle_(
            qualification_group_run_id_.data(),
            qualification_group_run_id_.size());
        qualification_cycle_armed_ = false;
        std::array<std::uint64_t, 50> cleanup{};
        const auto cleanup_status = get_qualification_group_row_report_(
            qualification_group_run_id_.data(),
            qualification_group_run_id_.size(), 0, cleanup.data(),
            cleanup.size());
        if (!disarmed || cleanup_status != 5 || cleanup[31] != 0
            || cleanup[32] != 1 || cleanup[36] != 0 || cleanup[37] != 0
            || !reports_valid || !anchor_identity_valid)
        {
            Output::send<LogLevel::Error>(STR(
                "[ReplayQualification] qualification group cleanup failed "
                "run_id={} status={} stale_mask=0x{:x} verified={} "
                "owned_bytes={} timeline_bytes={} forced_bytes={} "
                "pending={}/{} reports_valid={} anchor_identity={}\n"),
                RC::to_generic_string(qualification_group_run_id_),
                cleanup_status, cleanup[31], cleanup[32], cleanup[33],
                cleanup[34], cleanup[35], cleanup[36], cleanup[37],
                reports_valid ? 1 : 0, anchor_identity_valid ? 1 : 0);
            qualification_group_run_id_.clear();
            Fail("horsemod_qualification_group_cleanup_failed");
            return true;
        }
        for (std::uint32_t row = 0; row < 3; ++row)
        {
            const auto& cycle = request_.qualification_cycles[
                qualification_cycle_index_ + row];
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] qualification cycle cleanup passed "
                "run_id={} ordinal={} stale_mask=0x{:x} owned_bytes={} "
                "timeline_bytes={} forced_bytes={} pending={}/{}\n"),
                RC::to_generic_string(cycle.run_id), reports[row][4],
                cleanup[31], cleanup[33], cleanup[34], cleanup[35],
                cleanup[36], cleanup[37]);
        }
        if (status == 4)
        {
            qualification_group_run_id_.clear();
            Fail("horsemod_qualification_group_failed");
            return true;
        }
        qualification_group_run_id_.clear();
        qualification_cycle_index_ += 3;
        if (qualification_cycle_index_ == request_.qualification_cycles.size())
        {
            WriteResult("launch_requested", "qualification_groups_passed");
            // Persistent qualification owns multiple independent replay
            // entries without restarting SC6.  Reuse the navigator's proven
            // smoke-campaign teardown path before accepting the next request.
            require_replay_list_before_ready_ = true;
            state_ = State::Idle;
        }
        return true;
    }

    bool CompleteDevelopmentSmoke(std::uint32_t frame)
    {
        const auto get_coverage = ResolveHorseModPresentationCoverageApi();
        const auto get_identity = ResolveHorseModPresentationIdentityApi();
        const auto get_health = ResolveHorseModQualificationHealthApi();
        std::array<std::uint64_t, 10> coverage{};
        std::array<std::uint64_t, 9> identity{};
        std::array<std::uint64_t, 54> health{};
        if (get_coverage == nullptr || get_identity == nullptr
            || get_health == nullptr
            || !get_coverage(coverage.data(), coverage.size())
            || !get_identity(identity.data(), identity.size())
            || !get_health(health.data(), health.size()))
        {
            Fail("development_smoke_diagnostics_unavailable");
            return false;
        }
        if (coverage[6] == 0 || identity[1] == 0 || identity[3] == 0
            || health[34] == 0 || health[35] == 0)
        {
            Output::send<LogLevel::Error>(STR(
                "[ReplayQualification] development smoke missing early "
                "audio/ownership activity frame={} audio_source_coverage={} "
                "audio_events={} order_events={} audio_sources={} "
                "audio_terminals={}\n"), frame, coverage[6], identity[1],
                identity[3], health[34], health[35]);
            Fail("development_smoke_audio_ownership_missing");
            return false;
        }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] development smoke passed frame={} "
            "audio_source_coverage={} audio_events={} order_events={} "
            "audio_sources={} audio_terminals={}\n"), frame, coverage[6],
            identity[1], identity[3], health[34], health[35]);
        WriteResult("launch_requested", "development_smoke_passed");
        // A persistent development campaign publishes the next request only
        // after observing this result. Return to request polling, but require
        // the existing navigator to leave the current ReplayBattleScene before
        // another payload can be staged.
        require_replay_list_before_ready_ = true;
        state_ = State::Idle;
        return true;
    }

    void PollLaunch()
    {
        if (!battle_scene_observed_
            && std::chrono::steady_clock::now() - started_
                > std::chrono::seconds(140))
        {
            Fail("asset_wait_timeout");
            return;
        }
        if (!replay_scene_ready_)
        {
            std::string navigation_detail;
            const Horse::Qualification::NavigationState navigation =
                navigator_.Tick(playback_context_staged_,
                    require_replay_list_before_ready_, navigation_detail);
            if (navigation_detail != last_navigation_detail_)
            {
                last_navigation_detail_ = navigation_detail;
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] navigation={}\n"),
                    RC::to_generic_string(navigation_detail));
            }
            if (navigation == Horse::Qualification::NavigationState::Failed)
            {
                Fail(navigation_detail);
                return;
            }
            if (navigation
                == Horse::Qualification::NavigationState::ReplayListReady)
            {
                require_replay_list_before_ready_ = false;
                PollPlayerProfiles();
                return;
            }
            if (navigation != Horse::Qualification::NavigationState::Ready)
                return;
            replay_scene_ready_ = true;
        }
        std::uint32_t frame = 0;
        if (!ReadLuxBattleFrame(frame))
        {
            Fail("battle_frame_counter_unreadable");
            return;
        }
        const bool stock_round_outcome_control =
            request_.stock_round_outcome_control;
        if ((request_seek_ == nullptr || get_status_ == nullptr
                || get_range_ == nullptr || get_phase_ == nullptr
                || get_metrics_ == nullptr)
            && !ResolveHorseModSeekApi(request_seek_, get_status_, get_range_,
                get_phase_, get_metrics_))
        {
            Fail("horsemod_simulation_phase_api_unavailable");
            return;
        }
        std::int32_t native_round{}, native_time{}, unpause_countdown{};
        std::uint32_t round_state_frame{};
        const bool phase_available = get_phase_(
            &native_round, &native_time, &round_state_frame,
            &unpause_countdown);
        const bool inactive_phase = !phase_available || round_state_frame == 0
            || unpause_countdown != 0;
        if (inactive_phase && !battle_terminate_observed_)
        {
            if (++phase_wait_log_counter_ >= 120)
            {
                phase_wait_log_counter_ = 0;
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] waiting for active replay phase "
                    "available={} frame={} round={} time={} "
                    "round_state_frame={} unpause={}\n"),
                    phase_available ? STR("yes") : STR("no"), frame,
                    native_round, native_time, round_state_frame,
                    unpause_countdown);
            }
            return;
        }
        if (battle_terminate_observed_
            && (request_.require_authored_outcomes
                || request_.stock_round_outcome_control)
            && !round_outcomes_verified_)
        {
            Fail("authored_outcome_missing_at_battle_terminate");
            return;
        }
        if (battle_terminate_observed_ && !stock_round_outcome_control
            && !terminal_snapshot_captured_)
        {
            Fail("horsemod_terminal_snapshot_incomplete");
            return;
        }
        phase_wait_log_counter_ = 0;
        if (battle_manager_ == nullptr)
            battle_manager_ = FindBattleManager();
        if (battle_manager_ == nullptr) return;
        if (!authored_map_logged_)
            authored_map_logged_ = LogAuthoredMapPackages(battle_manager_);
        if (!battle_scene_observed_)
        {
            if (!stock_round_outcome_control)
            {
                const auto reset_health =
                    ResolveHorseModQualificationHealthResetApi();
                if (reset_health == nullptr || !reset_health())
                {
                    Fail("horsemod_qualification_health_reset_unavailable");
                    return;
                }
            }
            battle_scene_observed_ = true;
            observed_battle_frame_ = frame;
            initial_battle_frame_ = round_state_frame <= frame + 1
                ? frame - (round_state_frame - 1) : frame;
            battle_rate_started_at_ = std::chrono::steady_clock::now();
            if (request_.seek_percentages.empty())
            {
                battle_active_rate_started_at_ = battle_rate_started_at_;
                battle_active_rate_start_frame_ = frame;
                battle_active_rate_round_ = native_round;
            }
            importer_.ReleasePlaybackContext();
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] stock replay battle observed "
                "frame={} round={} time={} round_state_frame={}\n"), frame,
                native_round, native_time, round_state_frame);
            return;
        }
        // Stock outcome control is the rollback-disabled authored oracle. It
        // intentionally does not reset the deterministic qualification-health
        // window above, and must not classify pre-active observer diagnostics
        // as a stock-game outcome failure. Every deterministic replay path
        // retains the fail-fast contract after resetting its active window.
        if (!stock_round_outcome_control && !PollFailFastHealth(frame)) return;
        const std::uint32_t advanced = frame - initial_battle_frame_;
        if (!battle_rate_logged_ && advanced >= request_.watch_frames)
        {
            const auto elapsed_us = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now()
                        - battle_rate_started_at_).count());
            const auto measured_frames = static_cast<std::uint64_t>(
                frame - observed_battle_frame_);
            const auto tick_rate_milli = elapsed_us == 0 ? 0
                : measured_frames * 1'000'000'000ull
                    / elapsed_us;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] normal-render battle rate "
                "frames={} elapsed_us={} tick_rate_milli={}\n"),
                measured_frames, elapsed_us, tick_rate_milli);
            battle_rate_logged_ = true;
            if (tick_rate_milli < request_.min_resume_tick_rate_milli)
            {
                Output::send<LogLevel::Error>(STR(
                    "[ReplayQualification] normal-render battle rate failed "
                    "frames={} elapsed_us={} tick_rate_milli={} minimum={}\n"),
                    measured_frames, elapsed_us, tick_rate_milli,
                    request_.min_resume_tick_rate_milli);
                Fail("normal_render_battle_rate_below_minimum");
                return;
            }
        }
        // Strict seek qualification owns its own fixed live-frame timing
        // window.  Emitting this diagnostic at the same 120-frame boundary
        // perturbs the interval it is intended to qualify.
        if (request_.seek_percentages.empty()
            && !battle_active_rate_logged_)
        {
            const auto now = std::chrono::steady_clock::now();
            if (battle_active_rate_started_at_.time_since_epoch().count() == 0
                || native_round != battle_active_rate_round_
                || frame < battle_active_rate_start_frame_)
            {
                battle_active_rate_started_at_ = now;
                battle_active_rate_start_frame_ = frame;
                battle_active_rate_round_ = native_round;
            }
            else if (frame - battle_active_rate_start_frame_
                >= request_.resume_tick_window)
            {
                const auto measured_frames = static_cast<std::uint64_t>(
                    frame - battle_active_rate_start_frame_);
                const auto elapsed_us = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        now - battle_active_rate_started_at_).count());
                const auto tick_rate_milli = elapsed_us == 0 ? 0
                    : measured_frames * 1'000'000'000ull / elapsed_us;
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] normal-render active battle rate "
                    "frames={} elapsed_us={} tick_rate_milli={}\n"),
                    measured_frames, elapsed_us, tick_rate_milli);
                battle_active_rate_logged_ = true;
                if (tick_rate_milli < request_.min_resume_tick_rate_milli)
                {
                    Output::send<LogLevel::Error>(STR(
                        "[ReplayQualification] normal-render active battle "
                        "rate failed frames={} elapsed_us={} "
                        "tick_rate_milli={} minimum={}\n"),
                        measured_frames, elapsed_us, tick_rate_milli,
                        request_.min_resume_tick_rate_milli);
                    Fail("normal_render_active_battle_rate_below_minimum");
                    return;
                }
            }
        }
        if (!request_.qualification_cycles.empty())
        {
            // The same 120-frame normal-render canary used elsewhere must
            // pass before the first expensive correction cycle is armed.
            if (!battle_rate_logged_ || !battle_active_rate_logged_) return;
            PollQualificationCycles();
            return;
        }
        if (stock_round_outcome_control)
        {
            if (!PollRoundOutcomeQualification()) return;
            LogOrderedRoundOutcomes();
            state_ = State::Launched;
            WriteResult("launch_requested", "none");
            std::ostringstream winners;
            for (std::size_t index = 0;
                 index < observed_round_winners_.size(); ++index)
            {
                if (index != 0) winners << ',';
                winners << static_cast<int>(observed_round_winners_[index]);
            }
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] stock round outcome qualification "
                "passed rounds={} match_winner={} winners={}\n"),
                observed_round_winner_count_,
                last_round_result_.match_winner_index,
                RC::to_generic_string(winners.str()));
            return;
        }
        // Terminal injection must run while the authored battle identity and
        // actor lists are still live. Outcome verification intentionally
        // continues afterward through the final round and match result.
        if (request_.stage_terminal != 0 && !PollStageTerminal()) return;
        if (request_.require_authored_outcomes
            && !PollRoundOutcomeQualification()) return;
        if (request_.require_authored_outcomes
            && round_outcomes_verified_ && !terminal_snapshot_captured_)
        {
            terminal_snapshot_captured_ =
                capture_terminal_evidence_ != nullptr
                && capture_terminal_evidence_();
            if (!terminal_snapshot_captured_)
            {
                Fail("horsemod_terminal_snapshot_incomplete");
                return;
            }
        }
        if (advanced < request_.watch_frames) return;
        if (request_.development_smoke)
        {
            CompleteDevelopmentSmoke(frame);
            return;
        }
        if (seek_index_ < request_.seek_percentages.size())
        {
            PollSeekQualification(frame);
            return;
        }
        const auto get_coverage = ResolveHorseModPresentationCoverageApi();
        std::array<std::uint64_t, 10> coverage{};
        if (get_coverage == nullptr
            || !get_coverage(coverage.data(), coverage.size()))
        {
            Fail("horsemod_presentation_coverage_api_unavailable");
            return;
        }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] presentation source coverage "
            "stage_wall={} stage_barrier={} stage_dispatch={} "
            "audio={} audio_direct={} audio_remap={} audio_source={} "
            "audio_stop_all={} audio_blueprint={} particle_spawn={}\n"),
            coverage[0], coverage[1], coverage[2], coverage[3], coverage[4],
            coverage[5], coverage[6], coverage[7], coverage[8], coverage[9]);
        const auto get_presentation_identity =
            ResolveHorseModPresentationIdentityApi();
        std::array<std::uint64_t, 9> presentation_identity{};
        if (get_presentation_identity == nullptr
            || !get_presentation_identity(
                presentation_identity.data(), presentation_identity.size()))
        {
            Fail("horsemod_presentation_identity_api_unavailable");
            return;
        }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] presentation identity batches={} "
            "audio_events={} audio_identity=0x{:016x} order_events={} "
            "order_identity=0x{:016x} camera_identity=0x{:016x} "
            "camera_batches={} failures={} journal_committed={}\n"),
            presentation_identity[0], presentation_identity[1],
            presentation_identity[2], presentation_identity[3],
            presentation_identity[4], presentation_identity[5],
            presentation_identity[8], presentation_identity[6],
            presentation_identity[7]);
        const auto get_capacity_health =
            ResolveHorseModQualificationHealthApi();
        std::array<std::uint64_t, 21> capacity_health{};
        if (get_capacity_health == nullptr
            || !get_capacity_health(
                capacity_health.data(), capacity_health.size()))
        {
            Fail("horsemod_qualification_health_api_unavailable");
            return;
        }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] scratch capacity owners "
            "capture={}->{} canonical={}->{} target={}->{} "
            "transaction={}->{} regions={}->{} motion={}->{} "
            "dispatch={}->{} growth_events={}\n"),
            capacity_health[7], capacity_health[14],
            capacity_health[8], capacity_health[15],
            capacity_health[9], capacity_health[16],
            capacity_health[10], capacity_health[17],
            capacity_health[11], capacity_health[18],
            capacity_health[12], capacity_health[19],
            capacity_health[13], capacity_health[20], capacity_health[1]);
        // The aggregate identity above already includes every ordered audio
        // dispatch payload and terminal hash.  Do not synchronously enumerate
        // thousands of diagnostic records from EngineTick after the authored
        // match ends: UE4SS output can block this callback and prevent clean
        // teardown/re-entry.  Terminal failures retain their bounded native
        // failure ledger; successful runs publish only the exact aggregate.
        const auto get_health = ResolveHorseModQualificationHealthApi();
        std::array<std::uint64_t, 54> health{};
        if (get_health == nullptr
            || !get_health(health.data(), health.size()))
        {
            Fail("horsemod_qualification_health_api_unavailable");
            return;
        }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] qualification health "
            "capacity_failures={} capacity_growth_events={} "
            "timeline_accounting_failures={} aggregate_owned_bytes={} "
            "presentation_owned_bytes={} presentation_duplicate_failures={} "
            "presentation_publish_failures={} cursor_mismatches={} "
            "batch_accounting_mismatches={} round_transition_barriers={} "
            "timeline_partial={} partial_reason={} "
            "partial_coordinate={}:{} checkpoint_failure={} "
            "batch_entry_checkpoint_failure={} "
            "scratch_owner_capture={}->{} scratch_owner_canonical={}->{} "
            "scratch_owner_target={}->{} scratch_owner_transaction={}->{} "
            "scratch_owner_regions={}->{} scratch_owner_motion={}->{} "
            "scratch_owner_dispatch={}->{}\n"),
            health[0], health[1], health[2], health[3], health[4],
            health[5], health[6], health[36], health[37], health[38],
            health[48], health[49], health[50], health[51], health[52],
            health[53], health[7], health[14], health[8], health[15],
            health[9], health[16], health[10], health[17], health[11],
            health[18], health[12], health[19], health[13], health[20]);
        const auto get_rng_coverage =
            ResolveHorseModGameplayRngCoverageApi();
        std::array<std::uint64_t, 46> rng_coverage{};
        if (get_rng_coverage == nullptr
            || !get_rng_coverage(rng_coverage.data(), rng_coverage.size()))
        {
            Fail("horsemod_gameplay_rng_coverage_api_unavailable");
            return;
        }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] gameplay rng coverage "
            "xorshift_draws={} known_callers=0x{:x} unknown_callers={} "
            "weighted_draws={} if_draws={} short25_p0={} short25_p1={} "
            "probability_transition_batches={} state_changes_p0={} "
            "state_changes_p1={} probability_state_mask_p0="
            "{:016x}{:016x}{:016x}{:016x} probability_state_mask_p1="
            "{:016x}{:016x}{:016x}{:016x} transition07_calls={} "
            "tira_random_transitions={} tira_probability_batches={} "
            "tira_targets=0x{:x} xorshift_sequence=0x{:016x} "
            "transition07_sequence=0x{:016x} tira_sequence=0x{:016x} "
            "tira_stance_batches={} tira_slot_mask=0x{:x} "
            "state19_sequence_p0=0x{:016x} state19_sequence_p1=0x{:016x} "
            "state19_initial_p0={} state19_initial_p1={} "
            "state19_final_p0={} state19_final_p1={} "
            "xorshift_landing=0x{:08x},0x{:08x},0x{:08x} "
            "state19_at_tira_transition_p0={} "
            "state19_at_tira_transition_p1={} state19_initial_valid={} "
            "tira_last_target=0x{:04x} resolved_hit_calls={} "
            "resolved_hit_sequence=0x{:016x} tira_writer_calls={} "
            "tira_writer_sequence=0x{:016x} tira_writer_slot_mask=0x{:x} "
            "tira_last_writer_move=0x{:04x}\n"),
            rng_coverage[0], rng_coverage[1], rng_coverage[2],
            rng_coverage[3], rng_coverage[4], rng_coverage[5],
            rng_coverage[6], rng_coverage[7], rng_coverage[8],
            rng_coverage[9], rng_coverage[13], rng_coverage[12],
            rng_coverage[11], rng_coverage[10], rng_coverage[17],
            rng_coverage[16], rng_coverage[15], rng_coverage[14],
            rng_coverage[18], rng_coverage[19], rng_coverage[20],
            rng_coverage[21], rng_coverage[22], rng_coverage[23],
            rng_coverage[24], rng_coverage[25], rng_coverage[26],
            rng_coverage[27], rng_coverage[28], rng_coverage[29],
            rng_coverage[30], rng_coverage[31], rng_coverage[32],
            rng_coverage[33], rng_coverage[34], rng_coverage[35],
            rng_coverage[36], rng_coverage[37], rng_coverage[38],
            rng_coverage[39], rng_coverage[40], rng_coverage[41],
            rng_coverage[42], rng_coverage[43], rng_coverage[44],
            rng_coverage[45]);
        const auto get_canonical = ResolveHorseModCanonicalStateApi();
        std::uint64_t canonical_generation{}, canonical_frame{};
        std::array<std::byte, 32> canonical_hash{};
        if (get_canonical == nullptr
            || !get_canonical(&canonical_generation, &canonical_frame,
                canonical_hash.data(), canonical_hash.size()))
        {
            Fail("horsemod_canonical_state_api_unavailable");
            return;
        }
        std::ostringstream canonical_hex;
        canonical_hex << std::hex << std::setfill('0');
        for (const auto item : canonical_hash)
            canonical_hex << std::setw(2) << std::to_integer<unsigned>(item);
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] final canonical state generation={} "
            "frame={} sha256={}\n"), canonical_generation, canonical_frame,
            RC::to_generic_string(canonical_hex.str()));
        if (request_.require_authored_outcomes) LogOrderedRoundOutcomes();
        state_ = State::Launched;
        WriteResult("launch_requested", "none");
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] replay simulation frame advanced "
            "initial={} current={} watched={}\n"), initial_battle_frame_, frame,
            request_.watch_frames);
    }

    bool PollRoundOutcomeQualification()
    {
        if (round_outcomes_verified_) return true;
        BattleResult result{};
        if (!TryReadBattleResult(battle_manager_, result)) return false;
        const bool round_result_valid = result.result_type != 0
            && result.round_winner_index >= 0
            && result.round_winner_index
                <= Horse::Qualification::ReplayMetadata::
                    kSimultaneousRoundWinners;
        if (!round_result_valid)
        {
            if (have_last_round_result_) round_result_armed_ = true;
            return false;
        }
        const bool new_round_result = round_result_armed_
            || !have_last_round_result_
            || result.timer_seconds != last_round_result_.timer_seconds
            || result.result_type != last_round_result_.result_type
            || result.round_winner_index
                != last_round_result_.round_winner_index;
        if (new_round_result)
        {
            have_last_round_result_ = true;
            round_result_armed_ = false;
            last_round_result_ = result;
            const std::int8_t observed = static_cast<std::int8_t>(
                result.round_winner_index);
            if (!request_.stock_round_outcome_control)
            {
                if (observed_round_winner_count_
                    >= request_.expected_round_winners.size())
                {
                    Fail("simulated_round_winner_overflow");
                    return false;
                }
                const std::int8_t expected =
                    request_.expected_round_winners[observed_round_winner_count_];
                if (observed != expected)
                {
                    Output::send<LogLevel::Error>(STR(
                        "[ReplayQualification] round outcome mismatch "
                        "ordinal={} control={} simulated={} result_type={}\n"),
                        observed_round_winner_count_ + 1,
                        expected, observed, result.result_type);
                    Fail("simulated_round_winner_mismatch");
                    return false;
                }
            }
            observed_round_winners_.push_back(observed);
            ++observed_round_winner_count_;
        }
        if (result.match_winner_index < 0) return false;
        if (!request_.stock_round_outcome_control
            && result.match_winner_index != request_.expected_match_winner)
        {
            Fail("simulated_match_winner_mismatch");
            return false;
        }
        if (!request_.stock_round_outcome_control
            && observed_round_winner_count_
                != request_.expected_round_winners.size())
        {
            Fail("simulated_round_winner_count_mismatch");
            return false;
        }
        round_outcomes_verified_ = true;
        return true;
    }

    void LogOrderedRoundOutcomes() const
    {
        std::ostringstream winners;
        for (std::size_t index = 0;
             index < observed_round_winners_.size(); ++index)
        {
            if (index != 0) winners << ',';
            winners << static_cast<int>(observed_round_winners_[index]);
        }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] ordered round outcomes verified "
            "rounds={} match_winner={} winners={}\n"),
            observed_round_winner_count_, last_round_result_.match_winner_index,
            RC::to_generic_string(winners.str()));
    }

    bool PollStageTerminal()
    {
        if (stage_terminal_completed_) return true;
        if (request_stage_terminal_ == nullptr
            && !ResolveHorseModStageTerminalApi(
                request_stage_terminal_, get_stage_terminal_status_,
                get_forced_qualification_status_))
        {
            Fail("horsemod_stage_terminal_api_unavailable");
            return false;
        }
        if (!stage_terminal_requested_
            && get_forced_qualification_status_ != nullptr
            && get_forced_qualification_status_() == 0)
            return false;
        if (!stage_terminal_requested_)
        {
            if (!request_stage_terminal_(stage_terminal_operation_)) return false;
            stage_terminal_requested_ = true;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] requested source-frame stage "
                "terminal operation={}\n"), stage_terminal_operation_);
            return false;
        }
        std::uint32_t source_frame{};
        const auto status = get_stage_terminal_status_(&source_frame);
        if (status == 3)
        {
            Fail("horsemod_stage_terminal_failed");
            return false;
        }
        if (status != 2) return false;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] source-frame stage terminal "
            "completed operation={} frame={}\n"),
            stage_terminal_operation_, source_frame);
        if (request_.stage_terminal == 3 && stage_terminal_operation_ == 1)
        {
            stage_terminal_operation_ = 2;
            stage_terminal_requested_ = false;
            return false;
        }
        stage_terminal_completed_ = true;
        return true;
    }

    void PollSeekQualification(std::uint32_t frame)
    {
        if (request_seek_ == nullptr
            && !ResolveHorseModSeekApi(request_seek_, get_status_, get_range_,
                get_phase_, get_metrics_))
        {
            Fail("horsemod_seek_api_unavailable");
            return;
        }

        const auto percentage = request_.seek_percentages[seek_index_];
        if (seek_resume_observation_active_)
        {
            const auto now = std::chrono::steady_clock::now();
            if (seek_resume_last_active_at_.time_since_epoch().count() != 0
                && frame > seek_resume_last_observed_frame_)
            {
                const auto elapsed_us = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        now - seek_resume_last_active_at_).count());
                const auto advanced_frames = static_cast<std::uint64_t>(
                    frame - seek_resume_last_observed_frame_);
                if (seek_resume_rate_frames_ < request_.resume_tick_window)
                {
                    const auto accepted_frames = (std::min)(advanced_frames,
                        static_cast<std::uint64_t>(request_.resume_tick_window)
                            - seek_resume_rate_frames_);
                    seek_resume_rate_frames_ += accepted_frames;
                    seek_resume_rate_elapsed_us_ += elapsed_us
                        * accepted_frames / advanced_frames;
                }
            }
            seek_resume_last_active_at_ = now;
            seek_resume_last_observed_frame_ = frame;
            if (frame < seek_resume_start_frame_)
            {
                Fail("horsemod_seek_live_resume_generation_changed");
                return;
            }
            const std::uint64_t live_frames =
                frame - seek_resume_start_frame_;
            const auto required_live_frames = (std::max)(
                static_cast<std::uint64_t>(request_.resume_tick_window),
                static_cast<std::uint64_t>(request_.watch_frames));
            if (live_frames < required_live_frames)
            {
                return;
            }
            std::int32_t native_round{}, native_time{}, unpause_countdown{};
            std::uint32_t round_state_frame{};
            if (!get_phase_(&native_round, &native_time, &round_state_frame,
                    &unpause_countdown))
            {
                Fail("horsemod_seek_resume_phase_unavailable");
                return;
            }
            if (native_round != seek_resume_native_round_
                || round_state_frame < seek_resume_last_round_state_frame_)
            {
                Fail("horsemod_seek_resume_phase_changed");
                return;
            }
            // The seek status is terminal once validation completes.  Poll it
            // at the end of the live-resume interval instead of performing an
            // exported status call inside every sample of the timing window.
            // This preserves fail-closed validation without measuring the
            // qualification observer as simulation work.
            std::uint64_t unused_target{}, unused_source{}, unused_verified{};
            std::uint16_t failure{};
            if (get_status_(&unused_target, &unused_source, &unused_verified,
                    &failure) == 3)
            {
                Fail("horsemod_seek_live_resume_failed");
                return;
            }
            const auto elapsed_us = seek_resume_rate_elapsed_us_;
            if (seek_resume_rate_frames_ < request_.resume_tick_window
                || elapsed_us == 0)
            {
                Fail("horsemod_seek_resume_clock_invalid");
                return;
            }
            const auto tick_rate_milli = seek_resume_rate_frames_
                * 1'000'000'000ull
                / static_cast<std::uint64_t>(elapsed_us);
            if (tick_rate_milli < request_.min_resume_tick_rate_milli)
            {
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] strict seek live rate failed "
                    "percent={} live_resumed={} active_elapsed_us={} "
                    "resume_window={} resume_tick_rate_milli={} minimum={}\n"),
                    percentage, live_frames, elapsed_us,
                    seek_resume_rate_frames_, tick_rate_milli,
                    request_.min_resume_tick_rate_milli);
                Fail("horsemod_seek_live_resume_too_slow");
                return;
            }
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] strict seek passed percent={} "
                "target={} source_end={} history_verified={} "
                "live_resumed={} resume_total={} resim={} "
                "validation_us={} resume_window={} resume_elapsed_us={} "
                "resume_tick_rate_milli={} index={}\n"),
                percentage, requested_seek_target_, seek_completed_source_,
                seek_history_verified_, live_frames,
                seek_history_verified_ + live_frames,
                seek_resimulation_coordinates_, seek_validation_ns_ / 1000,
                request_.resume_tick_window, elapsed_us, tick_rate_milli,
                seek_index_);
            ++seek_index_;
            seek_requested_ = false;
            seek_resume_observation_active_ = false;
            requested_seek_target_ = 0;
            return;
        }
        if (seek_requested_)
        {
            std::uint64_t observed_target{};
            std::uint64_t source_end{};
            std::uint64_t verified{};
            std::uint16_t failure{};
            const auto status = get_status_(
                &observed_target, &source_end, &verified, &failure);
            if (status == 3)
            {
                Fail("horsemod_seek_validation_failed");
                return;
            }
            if (status == 1 && observed_target == requested_seek_target_
                && source_end != 0)
            {
                std::uint64_t validation_ns{}, resimulation_coordinates{};
                if (!get_metrics_(&validation_ns, &resimulation_coordinates))
                {
                    Fail("horsemod_seek_metrics_unavailable");
                    return;
                }
                if (validation_ns > 500'000'000ull)
                {
                    Fail("horsemod_seek_validation_too_slow");
                    return;
                }
                if (resimulation_coordinates > 29)
                {
                    Fail("horsemod_seek_resimulation_too_long");
                    return;
                }
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] strict seek historical prefix "
                    "passed percent={} target={} source_end={} verified={} "
                    "awaiting_live_frames={} resume_rate_window={}\n"),
                    percentage, observed_target, source_end, verified,
                    (std::max)(
                        static_cast<std::uint64_t>(request_.resume_tick_window),
                        static_cast<std::uint64_t>(request_.watch_frames)),
                    request_.resume_tick_window);
                seek_history_verified_ = verified;
                seek_completed_source_ = source_end;
                seek_validation_ns_ = validation_ns;
                seek_resimulation_coordinates_ = resimulation_coordinates;
                seek_resume_start_frame_ = frame;
                seek_resume_rate_frames_ = 0;
                seek_resume_rate_elapsed_us_ = 0;
                std::int32_t native_round{}, native_time{}, unpause_countdown{};
                std::uint32_t round_state_frame{};
                if (!get_phase_(&native_round, &native_time,
                        &round_state_frame, &unpause_countdown))
                {
                    Fail("horsemod_seek_resume_phase_unavailable");
                    return;
                }
                seek_resume_last_active_at_ = std::chrono::steady_clock::now();
                seek_resume_last_observed_frame_ = frame;
                seek_resume_native_round_ = native_round;
                seek_resume_last_round_state_frame_ = round_state_frame;
                seek_resume_observation_active_ = true;
            }
            return;
        }

        std::uint64_t generation{}, first{}, last{};
        std::int32_t native_round{}, native_time{}, unpause_countdown{};
        std::uint32_t round_state_frame{};
        if (!get_range_(&generation, &first, &last) || first >= last
            || last - first < request_.watch_frames)
        {
            return;
        }
        if (!get_phase_(&native_round, &native_time, &round_state_frame,
                &unpause_countdown) || round_state_frame == 0)
        {
            return;
        }
        // A completed seek resumes live simulation for the full qualification
        // window.  That window may legitimately cross a round barrier and
        // advance the native timeline generation before the next percentage
        // is requested.  The native range API guarantees that each returned
        // range is internally single-generation, so anchor the next seek to
        // that new domain.  Generation changes remain terminal while a seek
        // request or its live-resume validation is active in the branches
        // above.
        if (seek_range_generation_ == 0
            || generation != seek_range_generation_)
        {
            if (seek_range_generation_ != 0)
            {
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] strict seek range re-anchored "
                    "previous_generation={} generation={} range={}-{} "
                    "next_index={}\n"), seek_range_generation_, generation,
                    first, last, seek_index_);
            }
            seek_range_generation_ = generation;
            seek_range_first_ = first;
            seek_range_last_ = last;
        }
        const std::uint64_t target = seek_range_first_
            + (seek_range_last_ - seek_range_first_) * percentage / 100;
        if (!request_seek_(target))
        {
            Fail("horsemod_seek_request_rejected");
            return;
        }
        seek_requested_ = true;
        requested_seek_target_ = target;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] requested strict seek percent={} "
            "generation={} range={}-{} target={} index={} round={} "
            "time={} round_state_frame={} unpause={}\n"), percentage,
            seek_range_generation_, seek_range_first_, seek_range_last_, target,
            seek_index_, native_round,
            native_time, round_state_frame, unpause_countdown);
    }

    void PollPlayerProfiles()
    {
        if (!player_profiles_requested_)
        {
            if (!importer_.PopulateFallbackProfiles())
            {
                Fail("player_profile_fallback_failed");
                return;
            }
            player_profiles_requested_ = true;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] staged bounded replay profiles\n"));
            return;
        }
        if (!importer_.RequestReadyPlayback())
        {
            Fail("request_ready_replay_failed");
            return;
        }
        playback_context_staged_ = true;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] stock RequestReadyReplay ownership "
            "transfer completed; ReplaySetupScene owns setup application\n"));
    }

    void Fail(std::string_view reason)
    {
        if (qualification_cycle_armed_ && !qualification_group_run_id_.empty()
            && disarm_qualification_cycle_ != nullptr)
        {
            const bool cleaned = disarm_qualification_cycle_(
                qualification_group_run_id_.data(),
                qualification_group_run_id_.size());
            Output::send<LogLevel::Warning>(STR(
                "[ReplayQualification] failure-path qualification cleanup "
                "run_id={} accepted={}\n"),
                RC::to_generic_string(qualification_group_run_id_),
                cleaned ? 1 : 0);
            qualification_cycle_armed_ = false;
            qualification_group_run_id_.clear();
        }
        importer_.ReleasePlaybackContext();
        state_ = State::Failed;
        WriteResult("failed", reason);
    }

    void WriteResult(std::string_view result, std::string_view reason)
    {
        const std::filesystem::path root = QualificationRoot();
        std::error_code error;
        std::filesystem::create_directories(root, error);
        const auto temporary = root / L"replay_result.tmp";
        const auto destination = root / L"replay_result.txt";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << "version=1\nrun_id=" << request_.run_id
               << "\nresult=" << result << "\nreason=" << reason << '\n';
        stream.close();
        MoveFileExW(temporary.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }

    Horse::Qualification::ReplayPayloadImporter importer_{};
    Horse::Qualification::ReplaySceneNavigator navigator_{};
    Horse::Qualification::OnlineRoomAutomation room_automation_{};
    Horse::Qualification::ReplayMetadata replay_metadata_{};
    static inline std::atomic<ReplayQualificationMod*> s_instance_{nullptr};
    Request request_{};
    std::string last_run_id_{};
    std::string online_last_run_id_{};
    std::string online_observer_last_run_id_{};
    std::string online_room_run_id_{};
    std::string online_room_last_detail_{};
    std::string last_navigation_detail_{};
    std::chrono::steady_clock::time_point started_{};
    State state_{State::Idle};
    std::uint32_t poll_divider_{};
    RC::Unreal::Hook::GlobalCallbackId engine_tick_id_{
        RC::Unreal::Hook::ERROR_ID};
    RC::StringType battle_terminate_hook_path_{
        STR("/Script/LuxorGame.LuxBattleGameMode:TerminateBattle")};
    std::pair<int, int> battle_terminate_hook_ids_{};
    std::uint32_t battle_terminate_hook_poll_divider_{};
    bool battle_terminate_hook_registered_{};
    bool battle_terminate_observed_{};
    bool terminal_snapshot_captured_{};
    CaptureReplayQualificationTerminalEvidenceFn capture_terminal_evidence_{};
    bool bound_{};
    bool waiting_context_logged_{};
    bool player_profiles_requested_{};
    bool playback_context_staged_{};
    bool battle_scene_observed_{};
    bool replay_scene_ready_{};
    bool require_replay_list_before_ready_{};
    bool authored_map_logged_{};
    std::uint32_t initial_battle_frame_{};
    std::uint32_t observed_battle_frame_{};
    std::chrono::steady_clock::time_point battle_rate_started_at_{};
    bool battle_rate_logged_{};
    std::chrono::steady_clock::time_point battle_active_rate_started_at_{};
    std::uint32_t battle_active_rate_start_frame_{};
    std::int32_t battle_active_rate_round_{};
    bool battle_active_rate_logged_{};
    std::size_t seek_index_{};
    bool seek_requested_{};
    std::uint64_t requested_seek_target_{};
    std::uint64_t seek_range_generation_{};
    std::uint64_t seek_range_first_{};
    std::uint64_t seek_range_last_{};
    std::uint64_t seek_history_verified_{};
    std::uint64_t seek_completed_source_{};
    std::uint64_t seek_validation_ns_{};
    std::uint64_t seek_resimulation_coordinates_{};
    RequestReplaySeekFn request_seek_{};
    GetReplaySeekStatusFn get_status_{};
    GetReplaySeekableRangeFn get_range_{};
    GetReplaySimulationPhaseFn get_phase_{};
    GetReplaySeekMetricsFn get_metrics_{};
    RequestStageTerminalFn request_stage_terminal_{};
    GetStageTerminalStatusFn get_stage_terminal_status_{};
    GetForcedQualificationStatusFn get_forced_qualification_status_{};
    ArmReplayQualificationGroupFn arm_qualification_group_{};
    GetReplayQualificationGroupRowReportFn
        get_qualification_group_row_report_{};
    DisarmReplayQualificationCycleFn disarm_qualification_cycle_{};
    std::size_t qualification_cycle_index_{};
    bool qualification_cycle_armed_{};
    std::string qualification_group_run_id_{};
    ArmOnlineQualificationFn arm_online_{};
    GetOnlineQualificationStatusFn get_online_status_{};
    std::uint32_t online_last_status_{UINT32_MAX};
    ArmOnlineObserverProbeFn arm_online_observer_{};
    GetOnlineObserverProbeReportFn get_online_observer_report_{};
    DisarmOnlineObserverProbeFn disarm_online_observer_{};
    bool online_observer_active_{};
    bool online_room_terminal_{};
    bool stage_terminal_requested_{};
    bool stage_terminal_completed_{};
    std::uint32_t stage_terminal_operation_{};
    std::uint32_t seek_resume_start_frame_{};
    std::uint64_t seek_resume_rate_frames_{};
    std::uint64_t seek_resume_rate_elapsed_us_{};
    std::uint32_t seek_resume_last_observed_frame_{};
    std::chrono::steady_clock::time_point seek_resume_last_active_at_{};
    std::int32_t seek_resume_native_round_{};
    std::uint32_t seek_resume_last_round_state_frame_{};
    bool seek_resume_observation_active_{};
    std::uint16_t phase_wait_log_counter_{};
    std::uint32_t observed_round_winner_count_{};
    std::vector<std::int8_t> observed_round_winners_{};
    BattleResult last_round_result_{};
    bool have_last_round_result_{};
    bool round_result_armed_{true};
    bool round_outcomes_verified_{};
    RC::Unreal::UObject* battle_manager_{};
};

#define REPLAY_QUALIFICATION_API __declspec(dllexport)
extern "C"
{
REPLAY_QUALIFICATION_API CppUserModBase* start_mod()
{
    return new ReplayQualificationMod();
}

REPLAY_QUALIFICATION_API void uninstall_mod(CppUserModBase* mod)
{
    delete mod;
}
}
