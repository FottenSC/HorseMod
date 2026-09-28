#include "ReplayStartupLoading.hpp"
#include "ReplayRollingCorrectionRequest.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Psapi.h>
#include "ReplayFailureProtocol.hpp"
#include "ReplayStartupFailure.hpp"
#include "ReplayParticleOwnerCopy.hpp"

#include "ReplayPayloadImporter.hpp"
#include "ReplayQualificationHealth.hpp"
#include "ReplaySceneNavigator.hpp"
#include "ReplaySeekReadiness.hpp"
#include "ReplayResumeRateWindow.hpp"
#include "deterministic/Sc6ReplayObjectLease.hpp"
#include "deterministic/Sc6ReplayParticleObjects.hpp"
#include "deterministic/Sc6ReplayTraceState.hpp"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObjectArray.hpp>

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

#include "ReplayTrajectoryObserver.hpp"
#include "ReplayBoundaryObserver.hpp"
#include "ReplayIndexCompletion.hpp"
#include "ReplayPresentationObserver.hpp"
#include "../../HorseMod/horselib/deterministic/Sc6ReplayHost.hpp"
#include "../../HorseMod/horselib/deterministic/Sc6ReplayInputSource.hpp"
#include "../../HorseMod/horselib/deterministic/Schema.hpp"

using RC::CppUserModBase;
namespace LogLevel = RC::LogLevel;
namespace Output = RC::Output;
using ReplayHost = Horse::Deterministic::Sc6ReplayHost;
using ReplayScheduler = Horse::Deterministic::Sc6ReplaySchedulerState;

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
    bool trajectory_control{};
    bool trajectory_to_end{};
    bool executor_control{};
    bool executor_yield_every_tick{};
    bool include_setup{};
    bool probe_empty_interval{};
    bool probe_move_state{};
    bool probe_world_pause{};
    bool probe_interior_pause{};
    bool probe_application_pause{};
    bool probe_consumer_task{};
    bool c18_diagnostic{};
    bool consumer_mutation{};
    bool probe_particle_copy{};
    bool probe_historical_restore{};
    std::string historical_cancel;
    unsigned historical_advanced_tick{210};
    unsigned historical_anchor_tick{205};
    bool historical_exact_advance{};
    bool historical_single_step{};
    bool completion_repeat{};
    bool host_seek{};
    bool host_seek_repeat{};
    bool checkpoint_pair{};
    bool checkpoint_fallback{};
    bool coherence_images{};
    bool seek_publication_cancel{};
    bool seek_observer_failure{};
    bool seek_advance_failure{};
    bool seek_settlement_failure{};
    bool seek_drained_cancel{};
    bool record_index{};
    bool index_checkpoint{};
    std::uint32_t index_extra_checkpoint{};
    bool index_preparation_fallback{};
    std::string index_recovery;
    bool index_seek{};
    std::uint32_t index_seek_target{208},index_seek_continuation{120},observation_target{};
    unsigned index_sequence{};
    unsigned replay_budget_gib{1};
    unsigned rolling_cycles{};
    bool ground_motion_perturb{};
    std::vector<ReplayRollingCorrectionRow> rolling_corrections;
    bool authored_prefix{};
    bool index_cancel{};
    bool native_session_exit{};
    bool host_session{};
    std::uint32_t host_seek_target{208};
    std::uint32_t host_seek_first_target{};
    bool seek_preparation_failure{};
    bool probe_interactive_controls{};
    bool probe_small_restore{};
    bool skip_intros{};
    bool flush_startup_loading{};
    bool changed_inputs{};
    bool corrected_inputs{};
    bool source_revision{};
    bool source_revision_guard{};
    bool complete_seek_application{};
    bool capture_cancel{};
    bool particle_owner_copy{};
    bool particle_owner_registration{};
    bool particle_owner_gpu{};
    bool restore_reuse{};
    bool pixel_diagnostics{};
    bool pass_diagnostics{};
};

// Independent native task lease witness. Reading a completed/retired task is
// forbidden; the event must remain incomplete throughout the held interval.
bool ReadPendingInteriorTask(void* manager, const ReplayHost::InteriorWitness& witness,
    const void*& event) noexcept
{
    __try
    {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto task = reinterpret_cast<std::uintptr_t>(witness.pending_task);
        if (witness.boundary != ReplayHost::PauseBoundary::SimulationTick
            || !task || *reinterpret_cast<const std::uintptr_t*>(task) != base + 0x39cd9c0
            || !*reinterpret_cast<const std::uint8_t*>(task + 0x38)) return false;
        const auto tick = *reinterpret_cast<const std::uintptr_t*>(task + 0x10);
        if (!tick || *reinterpret_cast<const void* const*>(tick + 0x18) != witness.pending_task) return false;
        event = *reinterpret_cast<const void* const*>(task + 0x40);
        if (!event || (*reinterpret_cast<const std::uint32_t*>(
                static_cast<const std::byte*>(event) + 8) & (1u << 26))) return false;
        const auto get_world = reinterpret_cast<void* (*)(void*)>(
            (*reinterpret_cast<std::uintptr_t**>(manager))[0x138 / 8]);
        const auto world = get_world(manager);
        return world && world == witness.world
            && *reinterpret_cast<const void* const*>(task + 0x28) == world
            && *reinterpret_cast<const std::uint8_t*>(static_cast<const std::byte*>(world) + 0x780)
            && *reinterpret_cast<const std::uint64_t*>(base + 0x4197170) == witness.epoch;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadActiveTimerStorage(const void* world, const void*& storage) noexcept
{
    __try
    {
        const auto* bytes = static_cast<const std::byte*>(world);
        const auto* instance = *reinterpret_cast<const std::byte* const*>(bytes + 0x140);
        const auto* manager = instance ? *reinterpret_cast<const std::byte* const*>(instance + 0xd8)
            : *reinterpret_cast<const std::byte* const*>(bytes + 0x430);
        if (!manager || *reinterpret_cast<const std::uintptr_t*>(manager)
                != reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) + 0x39df498
            || *reinterpret_cast<const int*>(manager + 0x18) <= 0) return false;
        storage = *reinterpret_cast<const void* const*>(manager + 0x10);
        return storage != nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadCompletedApplicationTaskState(void* actor, std::uint64_t epoch) noexcept
{
    __try
    {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto* manager = reinterpret_cast<const std::byte*>(base + 0x43b2fd0);
        const auto* tick = static_cast<RC::Unreal::UObject*>(actor)->GetValuePtrByPropertyNameInChain<std::byte>(L"PrimaryActorTick");
        // Native FinishTickTaskManagerFrame clears its level count, world
        // context and active flag. Actor task completion clears tick+18.
        // Resolve the reflected subobject and verify its independently audited
        // actor wrapper/owner instead of assuming an AActor layout offset.
        return tick && *reinterpret_cast<const std::uintptr_t*>(tick) == base + 0x381c720
            && *reinterpret_cast<const void* const*>(tick + 0x50) == actor
            && !*reinterpret_cast<const void* const*>(tick + 0x18)
            && *reinterpret_cast<const std::uintptr_t*>(manager) == base + 0x39cdb10
            && !*reinterpret_cast<const int*>(manager + 0x18)
            && !*reinterpret_cast<const void* const*>(manager + 0x30)
            && !*reinterpret_cast<const std::uint8_t*>(manager + 0x38)
            && *reinterpret_cast<const std::uint64_t*>(base + 0x4197170) == epoch;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

template<class T>
bool ReadSchedulerValue(std::uintptr_t address, T& value) noexcept
{
    __try { std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T)); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

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
using GetQualificationClockFn = bool (*)(std::uint64_t*, std::size_t);
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
    // v12 keeps the v11 field set and adds the independent-clock/Tira-event
    // runtime semantics. Normalize only after parsing so older harnesses
    // remain readable while new producers bind evidence to the new contract.
    bool c18_diagnostic{};
    if(!ReadReplayC18DiagnosticProtocol(fields,c18_diagnostic))return false;
    if(c18_diagnostic) {fields["version"]="13";fields.erase("c18_selection");}
    const bool authored_protocol=fields["version"]=="17";
    if(authored_protocol) {
        if(fields["capture_mode"]!="executor" && fields["capture_mode"]!="trajectory")return false;
        for(const auto* name:{"seek_observer_failure","seek_advance_failure","seek_settlement_failure",
            "seek_drained_cancel","seek_preparation_failure","host_seek_repeat","checkpoint_pair",
            "checkpoint_fallback","capture_cancel","restore_reuse","record_index","completion_repeat"})
            if(fields.contains(name))return false;
        if(fields["authored_prefix"]!="217:0:0:172:2:0:0;218:1:0:170:2:0:0;219:2:0:168:2:0:0"
            || fields.contains("rolling_cycles") || fields.contains("rolling_corrections")
            || fields.contains("historical_cancel") || fields.contains("consumer_mutation")
            || fields.contains("probe_consumer_task") || fields.contains("changed_inputs")
            || fields.contains("corrected_inputs") || fields.contains("source_revision"))return false;
        // Controls perform native forward execution with the same authored
        // history. Only runtime candidates request the bounded transaction.
        if(fields["capture_mode"]=="executor" && (fields["historical_anchor_tick"]!="617"
            || fields["historical_advanced_tick"]!="624" || fields["host_seek_target"]!="624"
            || fields["host_seek"]!="true" || fields["historical_exact_advance"]!="true"))return false;
        fields["version"]="13";
    }
    if(fields.contains("authored_prefix") && !authored_protocol)return false;
    const bool mutation_protocol=fields["version"]=="16";
    if(mutation_protocol) {
        if(fields["consumer_mutation"]!="true" || fields["historical_anchor_tick"]!="210"
            || fields["historical_advanced_tick"]!="217" || fields["host_seek_target"]!="217"
            || fields["host_seek"]!="true" || fields["historical_exact_advance"]!="true"
            || fields["historical_cancel"]!="after" || fields.contains("probe_consumer_task")
            || fields.contains("seek_advance_failure") || fields.contains("rolling_corrections"))return false;
        fields["version"]="13";
    }
    if(fields.contains("consumer_mutation") && !mutation_protocol)return false;
    const bool consumer_protocol=fields["version"]=="15";
    if(consumer_protocol) {
        if(fields["probe_consumer_task"]!="true" || fields.contains("rolling_corrections"))return false;
        fields["version"]="13";
    }
    if(fields.contains("probe_consumer_task") && !consumer_protocol)return false;
    const bool scheduled_protocol=fields["version"]=="14";
    if(scheduled_protocol) {
        if(!fields.contains("rolling_corrections"))return false;
        fields["version"]="13";
    }
    if(fields.contains("rolling_corrections") && !scheduled_protocol)return false;
    if (fields["version"] == "12") fields["version"] = "11";
    if (stream.eof() && ValidRunId(fields["run_id"])
        && fields["version"] == "13"
        && fields.size() == 5 + fields.contains("c18_diagnostic") + fields.contains("authored_prefix") + fields.contains("executor_yield_every_tick") + fields.contains("include_setup")
            + fields.contains("completion_repeat") + fields.contains("flush_startup_loading") + fields.contains("skip_intros") + fields.contains("changed_inputs") + fields.contains("corrected_inputs") + fields.contains("source_revision") + fields.contains("source_revision_profile") + fields.contains("complete_seek_application") + fields.contains("capture_cancel") + fields.contains("restore_reuse") + fields.contains("pixel_diagnostics") + fields.contains("pass_diagnostics")
            + fields.contains("particle_owner_copy") + fields.contains("particle_owner_registration") + fields.contains("particle_owner_gpu")
            + fields.contains("coherence_images") + fields.contains("probe_empty_interval") + fields.contains("probe_world_pause")
            + fields.contains("probe_interior_pause")
            + fields.contains("probe_application_pause") + fields.contains("probe_consumer_task") + fields.contains("consumer_mutation")
            + fields.contains("probe_particle_copy")
            + fields.contains("probe_historical_restore")
            + fields.contains("historical_cancel")
            + fields.contains("historical_anchor_tick") + fields.contains("historical_advanced_tick") + fields.contains("historical_exact_advance") + fields.contains("historical_single_step") + fields.contains("host_seek") + fields.contains("host_seek_target") + fields.contains("host_seek_first_target") + fields.contains("host_seek_repeat") + fields.contains("checkpoint_pair") + fields.contains("checkpoint_fallback") + fields.contains("record_index") + fields.contains("seek_publication_cancel") + fields.contains("seek_observer_failure") + fields.contains("seek_advance_failure") + fields.contains("seek_settlement_failure") + fields.contains("seek_drained_cancel") + fields.contains("seek_preparation_failure")
            + fields.contains("ground_motion_perturb") + fields.contains("observation_target") + fields.contains("rolling_corrections") + fields.contains("rolling_cycles") + fields.contains("replay_budget_gib") + fields.contains("index_sequence") + fields.contains("index_recovery") + fields.contains("index_preparation_fallback") + fields.contains("index_extra_checkpoint") + fields.contains("index_seek_target") + fields.contains("index_seek_continuation") + fields.contains("index_checkpoint") + fields.contains("index_seek") + fields.contains("index_cancel") + fields.contains("native_session_exit") + fields.contains("host_session") + fields.contains("probe_small_restore")
            + fields.contains("probe_move_state")
            + fields.contains("probe_interactive_controls")
        && (fields["capture_mode"] == "trajectory" || fields["capture_mode"] == "trajectory_match"
            || fields["capture_mode"] == "executor" || fields["capture_mode"] == "executor_match"))
    {
        output = {};
        output.c18_diagnostic=c18_diagnostic;
        output.consumer_mutation=mutation_protocol;
        output.authored_prefix=authored_protocol;
        output.run_id = fields["run_id"];
        output.replay_path = std::filesystem::path(Widen(fields["replay_path"]));
        try
        {
            std::size_t consumed{};
            const auto count = std::stoul(fields["watch_frames"], &consumed);
            if (consumed != fields["watch_frames"].size() || count < 120 || count > 36000)
                return false;
            output.watch_frames = static_cast<std::uint32_t>(count);
        }
        catch (...) { return false; }
        output.trajectory_control = true;
        output.trajectory_to_end = fields["capture_mode"] == "trajectory_match"
            || fields["capture_mode"] == "executor_match";
        output.executor_control = fields["capture_mode"] == "executor"
            || fields["capture_mode"] == "executor_match";
        if (fields.contains("executor_yield_every_tick"))
        {
            if (!output.executor_control || (fields["executor_yield_every_tick"] != "true"
                && fields["executor_yield_every_tick"] != "false"))
                return false;
            output.executor_yield_every_tick = fields["executor_yield_every_tick"] == "true";
        }
        output.stock_round_outcome_control = output.trajectory_to_end;
        if (fields.contains("probe_world_pause"))
        {
            if (!output.executor_control || output.watch_frames < 240 || fields["probe_world_pause"] != "true") return false;
            output.probe_world_pause = true;
        }
        if (fields.contains("include_setup"))
        {
            if (fields["include_setup"] != "true" && fields["include_setup"] != "false") return false;
            output.include_setup = fields["include_setup"] == "true";
        }
        for (const auto* name : {"pixel_diagnostics", "pass_diagnostics", "coherence_images"})
            if (fields.contains(name) && fields[name] != "true") return false;
        if(fields.contains("changed_inputs") && fields["changed_inputs"]!="true") return false;
        output.changed_inputs=fields.contains("changed_inputs");
        if(fields.contains("corrected_inputs") && (fields["corrected_inputs"]!="true" || !output.changed_inputs)) return false;
        output.corrected_inputs=fields.contains("corrected_inputs");
        if(fields.contains("source_revision") && (fields["source_revision"]!="true" || !output.changed_inputs)) return false;
        output.source_revision=fields.contains("source_revision");
        if(fields.contains("source_revision_profile")) {
            if(!output.source_revision || !output.corrected_inputs || fields["source_revision_profile"]!="guard201") return false;
            output.source_revision_guard=true;
        }
        if(fields.contains("capture_cancel") && (fields["capture_cancel"]!="true")) return false;
        output.capture_cancel=fields.contains("capture_cancel");
        if(fields.contains("particle_owner_copy") && fields["particle_owner_copy"]!="true")return false;
        output.particle_owner_copy=fields.contains("particle_owner_copy");
        if(fields.contains("particle_owner_registration") && (fields["particle_owner_registration"]!="true" || !output.particle_owner_copy))return false;
        output.particle_owner_registration=fields.contains("particle_owner_registration");
        if(fields.contains("particle_owner_gpu") && (fields["particle_owner_gpu"]!="true" || !output.particle_owner_registration))return false;
        output.particle_owner_gpu=fields.contains("particle_owner_gpu");
        if(fields.contains("restore_reuse") && fields["restore_reuse"]!="true") return false;
        output.restore_reuse=fields.contains("restore_reuse");
        output.pixel_diagnostics = fields.contains("pixel_diagnostics");
        output.coherence_images = fields.contains("coherence_images");
        if(output.coherence_images && (output.pixel_diagnostics || fields.contains("pass_diagnostics"))) return false;
        output.pass_diagnostics = fields.contains("pass_diagnostics");
        if (output.pass_diagnostics && !output.pixel_diagnostics) return false;
        if (fields.contains("skip_intros"))
        {
            if (fields["skip_intros"] != "true" || !output.include_setup) return false;
            output.skip_intros = true;
        }
        if(fields.contains("flush_startup_loading")) {
            if(fields["flush_startup_loading"]!="true" || !output.skip_intros)return false;
            output.flush_startup_loading=true;
        }
        if(!ReadReplayObservationTarget(fields,output.observation_target))return false;
        if (fields.contains("probe_empty_interval"))
        {
            if (fields["probe_empty_interval"] != "true" && fields["probe_empty_interval"] != "false") return false;
            output.probe_empty_interval = fields["probe_empty_interval"] == "true";
        }
        if (fields.contains("probe_interior_pause"))
        {
            if (fields["probe_interior_pause"] != "true" || !output.executor_control
                || !output.executor_yield_every_tick || !output.include_setup
                || output.probe_empty_interval || output.probe_world_pause) return false;
            output.probe_interior_pause = true;
        }
        if (fields.contains("probe_interactive_controls"))
        {
            if (fields["probe_interactive_controls"] != "true"
                || (!output.probe_interior_pause
                    && !(fields.contains("seek_settlement_failure") && fields.at("seek_settlement_failure")=="true")
                    && !(fields.contains("index_seek") && fields.at("index_seek")=="true"))) return false;
            output.probe_interactive_controls = true;
        }
        if (fields.contains("probe_application_pause"))
        {
            if (fields["probe_application_pause"] != "true" || !output.executor_control
                || !output.executor_yield_every_tick || !output.include_setup || output.probe_interior_pause
                || output.probe_empty_interval || output.probe_world_pause) return false;
            output.probe_application_pause = true;
        }
        if(fields.contains("probe_consumer_task")) {
            if(!output.probe_application_pause || fields["probe_consumer_task"]!="true")return false;
            output.probe_consumer_task=true;
        }
        if (fields.contains("probe_particle_copy"))
        {
            if (fields["probe_particle_copy"] != "true" || !output.probe_application_pause) return false;
            output.probe_particle_copy = true;
        }
        if (fields.contains("probe_historical_restore"))
        {
            if (fields["probe_historical_restore"] != "true" || !output.probe_particle_copy) return false;
            output.probe_historical_restore = true;
        }
        if (fields.contains("historical_exact_advance"))
        {
            if (!output.probe_historical_restore || fields["historical_exact_advance"] != "true"
                || (fields.contains("historical_cancel") && !fields.contains("host_seek_repeat")
                    && !IsBoundedCheckpointWindow(fields))) return false;
            output.historical_exact_advance = true;
        }
        if (fields.contains("historical_single_step")) {
            if (!output.historical_exact_advance || fields["historical_single_step"] != "true") return false;
            output.historical_single_step = true;
        }
        if(fields.contains("host_seek")) {
            if(fields["host_seek"]!="true" || (!output.historical_exact_advance && !fields.contains("historical_cancel"))
                || fields.contains("restore_reuse") || fields.contains("capture_cancel")) return false;
            output.host_seek=true;
        }
        if(fields.contains("seek_publication_cancel")) {
            if(fields["seek_publication_cancel"]!="true" || !output.host_seek
                || fields["historical_cancel"]!="after" || fields["historical_anchor_tick"]!="170"
                || fields["historical_advanced_tick"]!="2504" || fields.contains("seek_advance_failure")
                || fields.contains("seek_observer_failure") || fields.contains("seek_preparation_failure")
                || fields.contains("host_seek_repeat") || fields.contains("changed_inputs") || fields.contains("corrected_inputs")) return false;
            output.seek_publication_cancel=true;
        }
        if(fields.contains("seek_observer_failure")) {
            if(fields["seek_observer_failure"]!="true" || !output.host_seek
                || fields["historical_cancel"]!="after") return false;
            output.seek_observer_failure=true;
        }
        if(fields.contains("seek_advance_failure")) {
            const bool emitter_recovery=IsBoundedEmitterRecovery(fields);
            const bool lifetime_recovery=IsBoundedParticleLifetimeRecovery(fields);
            if(fields["seek_advance_failure"]!="true" || !output.host_seek
                || fields["historical_cancel"]!="after" || (!emitter_recovery && !lifetime_recovery && !IsBoundedPrivateHudDrainedRecovery(fields) && !IsBoundedTraceRenderRecovery(fields) && (fields["historical_anchor_tick"]!="170"
                || (fields["historical_advanced_tick"]!="220" && fields["historical_advanced_tick"]!="2504") || (fields["host_seek_target"]!="214" && !((fields["host_seek_target"]=="5750" || fields["host_seek_target"]=="11000") && fields["historical_advanced_tick"]=="2504" && fields["seek_settlement_failure"]=="true")))) || fields.contains("host_seek_repeat")
                || fields.contains("seek_observer_failure") || fields.contains("seek_preparation_failure")) return false;
            output.seek_advance_failure=true;
        }
        if(fields.contains("seek_settlement_failure")) {
            if(fields["seek_settlement_failure"]!="true" || !output.seek_advance_failure)return false;
            output.seek_settlement_failure=true;
        }
        if(fields.contains("seek_drained_cancel")) {
            if(fields["seek_drained_cancel"]!="true" || !output.seek_settlement_failure || output.probe_interactive_controls)return false;
            output.seek_drained_cancel=true;
        }
        if(fields.contains("record_index")) {
            if(fields["record_index"]!="true" || !output.include_setup || (!output.trajectory_to_end && !fields.contains("index_cancel") && !fields.contains("native_session_exit"))
                || fields.contains("probe_historical_restore") || fields.contains("probe_small_restore")) return false;
            output.record_index=true;
        }
        if(fields.contains("index_checkpoint")) {
            if(fields["index_checkpoint"]!="true" || !output.record_index || !output.executor_control
                || !output.skip_intros || !output.executor_yield_every_tick) return false;
            output.index_checkpoint=true;
        }
        for(const auto key:{"index_seek_target","index_seek_continuation","index_extra_checkpoint"}) if(fields.contains(key)) {
            const auto& text=fields[key];
            if(!output.trajectory_to_end || text.empty() || text.size()>5 || text.find_first_not_of("0123456789")!=std::string::npos)return false;
            const auto value=static_cast<std::uint32_t>(std::stoul(text));
            if(std::string_view(key)=="index_seek_target")output.index_seek_target=value;
            else if(std::string_view(key)=="index_seek_continuation")output.index_seek_continuation=value;
            else output.index_extra_checkpoint=value;
        }
        if(output.index_seek_target<170 || output.index_seek_continuation<120 || output.index_seek_continuation>600
            || output.index_seek_target>36000-output.index_seek_continuation)return false;
        if(fields.contains("index_seek")) {
            if(fields["index_seek"]!="true" || !output.index_checkpoint || !output.trajectory_to_end) return false;
            output.index_seek=true;
        }
        if(output.index_extra_checkpoint && (!output.index_seek || output.index_extra_checkpoint<=170
            || output.index_extra_checkpoint>=output.index_seek_target))return false;
        if(fields.contains("index_preparation_fallback")) {
            if(fields["index_preparation_fallback"]!="true" || !output.index_seek || !output.index_extra_checkpoint)return false;
            output.index_preparation_fallback=true;
        }
        if(fields.contains("index_recovery")) {
            output.index_recovery=fields["index_recovery"];
            if(!output.index_seek || output.index_preparation_fallback
                || (output.index_recovery!="published" && output.index_recovery!="drained" && output.index_recovery!="interior")
                || output.index_seek_target<=std::max(170u,output.index_extra_checkpoint))return false;
        }
        if(fields.contains("index_cancel")) {
            if(fields["index_cancel"]!="true" || !output.record_index || output.trajectory_to_end
                || !output.skip_intros || (output.executor_control && !output.index_checkpoint)) return false;
            output.index_cancel=true;
        }
        if(fields.contains("native_session_exit")) {
            if(fields["native_session_exit"]!="true" || !output.record_index || output.trajectory_to_end || output.index_cancel
                || !output.skip_intros || (output.executor_control && !output.index_checkpoint)) return false;
            output.native_session_exit=true;
        }
        if(fields.contains("host_session")) {
            if(fields["host_session"]!="true" || !output.record_index || !output.skip_intros
                || (output.executor_control && !output.index_checkpoint)
                || (output.index_extra_checkpoint && output.index_extra_checkpoint!=2504)
                || (!output.native_session_exit && (!output.trajectory_to_end || (output.executor_control && !output.index_seek)))) return false;
            output.host_session=true;
        }
        if(authored_protocol && !ParseRollingCorrectionRequest(fields["authored_prefix"],output.rolling_corrections))return false;
        if(scheduled_protocol && !ParseRollingCorrectionRequest(fields["rolling_corrections"],output.rolling_corrections))return false;
        if(scheduled_protocol && output.executor_control && !fields.contains("rolling_cycles"))return false;
        if(fields.contains("rolling_cycles")) {
            if(!(scheduled_protocol && (fields["rolling_cycles"]=="1" || fields["rolling_cycles"]=="2" || fields["rolling_cycles"]=="7")) && fields["rolling_cycles"]!="30" && fields["rolling_cycles"]!="48" && fields["rolling_cycles"]!="54" && fields["rolling_cycles"]!="174" && fields["rolling_cycles"]!="252" && fields["rolling_cycles"]!="407" && fields["rolling_cycles"]!="408" && fields["rolling_cycles"]!="600")return false;
            output.rolling_cycles=fields["rolling_cycles"]=="1"?1:fields["rolling_cycles"]=="2"?2:fields["rolling_cycles"]=="7"?7:fields["rolling_cycles"]=="30"?30:fields["rolling_cycles"]=="48"?48:fields["rolling_cycles"]=="54"?54:fields["rolling_cycles"]=="174"?174:fields["rolling_cycles"]=="252"?252:fields["rolling_cycles"]=="407"?407:fields["rolling_cycles"]=="408"?408:600;
            const bool early=fields["historical_anchor_tick"]=="210" && fields["historical_advanced_tick"]=="217" && fields["host_seek_target"]=="217";
            const bool trace=output.rolling_cycles==30 && fields["historical_anchor_tick"]=="332" && fields["historical_advanced_tick"]=="339" && fields["host_seek_target"]=="339";
            if(!output.executor_control || !output.host_seek || (!early && !trace)
                || fields.contains("historical_cancel") || fields.contains("host_seek_repeat")
                || fields.contains("changed_inputs") || fields.contains("corrected_inputs"))return false;
        }
        if(fields.contains("ground_motion_perturb")) {
            if(fields["ground_motion_perturb"]!="true" || !output.executor_control || output.rolling_cycles!=407)return false;
            output.ground_motion_perturb=true;
        }
        if(fields.contains("replay_budget_gib")) {
            if((!output.host_session && !output.rolling_cycles) || fields["replay_budget_gib"]!="2")return false;
            output.replay_budget_gib=2;
        }
        if(fields.contains("index_sequence")) {
            const auto& sequence=fields["index_sequence"];
            if(!output.host_session || !output.trajectory_to_end || output.index_seek_continuation!=120
                || output.index_preparation_fallback || !output.index_recovery.empty()
                || (output.executor_control && !output.index_seek)
                || (sequence!="early" && sequence!="late"))return false;
            output.index_sequence=sequence=="early"?1u:2u;
            if(output.index_seek_target!=ReplayQualification::IndexSequence(output.index_sequence).front())return false;
        }

        if(fields.contains("host_seek_repeat")) {
            if(fields["host_seek_repeat"]!="true" || !output.host_seek
                || fields.contains("corrected_inputs") || fields.contains("changed_inputs")
                || (fields.contains("pixel_diagnostics") && !fields.contains("checkpoint_pair"))) return false;
            output.host_seek_repeat=true;
        }
        if(fields.contains("checkpoint_pair")) {
            if(fields["checkpoint_pair"]!="true" || !output.host_seek_repeat
                || fields["historical_advanced_tick"]!="211" || fields["host_seek_target"]!="208") return false;
            output.checkpoint_pair=true;
        }
        if(fields.contains("host_seek_target")) {
            const auto& target=fields["host_seek_target"];
            unsigned value{};
            if(!output.host_seek || !ReadCheckpointWindowCoordinate(fields,"host_seek_target",value)
                || (target.size()!=3 && target!="2510" && target!="5750" && target!="11000"
                    && !IsBoundedCheckpointWindow(fields)))return false;
            // Bounded protocol coverage, not a limit in the host seek API.
            // The retained 360-observation control must cover target+120.
            if(value<205 || (value>ReplayPresentationObserver::observation_last_tick-120
                && !IsBoundedCheckpointWindow(fields) && !IsBoundedTraceRenderRecovery(fields) && !IsBoundedGroundCommit(fields)
                && !(value==300 && (output.source_revision_guard || IsBoundedPrivateHudDrainedRecovery(fields)))
                && !((value==5750 || value==11000) && output.seek_settlement_failure && fields["historical_advanced_tick"]=="2504"))) return false;
            output.host_seek_target=value;
        }
        if(fields.contains("host_seek_first_target")) {
            const auto& target=fields["host_seek_first_target"];
            if(!output.host_seek_repeat || output.checkpoint_pair || target.size()!=3
                || !std::all_of(target.begin(),target.end(),[](char c){return c>='0' && c<='9';})) return false;
            const auto value=unsigned(target[0]-'0')*100u+unsigned(target[1]-'0')*10u+unsigned(target[2]-'0');
            if(value<205 || value>ReplayPresentationObserver::observation_last_tick-120 || value==output.host_seek_target) return false;
            output.host_seek_first_target=value;
        }
        if(fields.contains("seek_preparation_failure")) {
            if(fields["seek_preparation_failure"]!="true" || !output.host_seek_repeat || output.checkpoint_pair
                || output.seek_observer_failure || output.host_seek_first_target!=220 || output.host_seek_target!=208
                || fields["historical_cancel"]!="after") return false;
            output.seek_preparation_failure=true;
        }
        if(output.historical_single_step && (output.host_seek_repeat || output.host_seek_target!=208
            || fields.contains("historical_cancel"))) return false;
        if(fields.contains("complete_seek_application")) {
            if(fields["complete_seek_application"]!="true" || !output.host_seek || output.host_seek_repeat
                || output.historical_single_step || !output.historical_exact_advance
                || fields.contains("historical_cancel") || fields.contains("pixel_diagnostics")) return false;
            output.complete_seek_application=true;
        }
        if (fields.contains("historical_anchor_tick")) {
            if(IsBoundedCheckpointWindow(fields)) {
                if(!ReadCheckpointWindowCoordinate(fields,"historical_anchor_tick",output.historical_anchor_tick))return false;
            } else {
            const bool baseline=fields["historical_anchor_tick"]=="0";
            if ((!baseline && fields["historical_anchor_tick"]!="170") || !output.host_seek
                || output.checkpoint_pair || fields.contains("pixel_diagnostics")
                || (output.host_seek_repeat && (output.changed_inputs || fields.contains("historical_cancel")))
                || ((output.changed_inputs || output.corrected_inputs)
                    && (!output.corrected_inputs || !output.source_revision || fields.contains("historical_cancel")))
                || (baseline ? (fields["historical_advanced_tick"]!="210" || output.host_seek_repeat
                    || output.corrected_inputs || output.changed_inputs || fields.contains("historical_cancel")
                    || output.host_seek_target!=208 || output.coherence_images)
                    : (fields["historical_advanced_tick"]!="220" && fields["historical_advanced_tick"]!="2504"
                        && !IsBoundedExecutionFallback(fields) && !IsBoundedSameOriginGuard(fields) && !IsBoundedPrivateHudRecovery(fields) && !IsBoundedPrivateHudDrainedRecovery(fields) && !IsBoundedTraceRenderRecovery(fields) && !IsBoundedGroundCommit(fields)))) return false;
            output.historical_anchor_tick=baseline?0:170;
            }
        }
        if(fields.contains("checkpoint_fallback")) {
            const bool repeated=output.host_seek_repeat && !fields.contains("historical_cancel")
                && output.host_seek_first_target==208 && output.host_seek_target==214;
            const bool cancelled=!output.host_seek_repeat && fields.contains("historical_cancel")
                && !output.host_seek_first_target && output.host_seek_target==208;
            if(fields["checkpoint_fallback"]!="true" || output.historical_anchor_tick!=170
                || output.checkpoint_pair || (!repeated && !cancelled && !IsBoundedExecutionFallback(fields))) return false;
            output.checkpoint_fallback=true;
        }
        if (fields.contains("historical_advanced_tick"))
        {
            if(IsBoundedCheckpointWindow(fields)) {
                if(!output.probe_historical_restore || !ReadCheckpointWindowCoordinate(fields,"historical_advanced_tick",output.historical_advanced_tick))return false;
            } else {
            if (!output.probe_historical_restore || (fields["historical_advanced_tick"] != "209"
                && fields["historical_advanced_tick"] != "210" && !(output.checkpoint_pair && fields["historical_advanced_tick"]=="211")
                && !(output.historical_anchor_tick==170 && (fields["historical_advanced_tick"]=="220" || fields["historical_advanced_tick"]=="2504"))
                && !IsBoundedSameOriginGuard(fields) && !IsBoundedPrivateHudRecovery(fields) && !IsBoundedPrivateHudDrainedRecovery(fields) && !IsBoundedTraceRenderRecovery(fields) && !IsBoundedGroundCommit(fields))) return false;
            output.historical_advanced_tick = fields["historical_advanced_tick"] == "6538" ? 6538 : fields["historical_advanced_tick"] == "5695" ? 5695 : fields["historical_advanced_tick"] == "329" ? 329 : fields["historical_advanced_tick"] == "209" ? 209 : fields["historical_advanced_tick"]=="211" ? 211 : fields["historical_advanced_tick"]=="220" ? 220 : fields["historical_advanced_tick"]=="2504" ? 2504 : fields["historical_advanced_tick"]=="300" ? 300 : 210;
            if(output.historical_advanced_tick==2504 && (output.host_seek_repeat || output.checkpoint_pair
                || (output.changed_inputs && (!output.corrected_inputs || !output.source_revision || fields.contains("historical_cancel")))
                || output.checkpoint_fallback || (output.host_seek_target!=(output.seek_advance_failure?214:208)
                    && !(output.host_seek_target==300 && output.source_revision_guard)
                    && !(output.seek_settlement_failure && (output.host_seek_target==5750 || output.host_seek_target==11000))))) return false;
            }
        }
        if (fields.contains("historical_cancel"))
        {
            if (!output.probe_historical_restore || (fields["historical_cancel"] != "before"
                && fields["historical_cancel"] != "after")) return false;
            output.historical_cancel = fields["historical_cancel"];
        }
        // Validate the assembled protocol only after anchor, B and cancellation are parsed.
        if(output.particle_owner_gpu && output.historical_anchor_tick!=205)return false;
        if(output.particle_owner_copy && (!output.host_seek || !output.probe_historical_restore
            || output.historical_cancel!="before" || !(
                (output.historical_anchor_tick==6415 && output.historical_advanced_tick==6416
                 && output.host_seek_target==6415 && output.historical_exact_advance)
                || (output.historical_anchor_tick==205 && output.historical_advanced_tick==210
                    && output.host_seek_target==208 && !output.historical_exact_advance))
            || output.changed_inputs || output.corrected_inputs
            || output.capture_cancel || output.restore_reuse || output.host_seek_repeat
            || output.checkpoint_pair || output.pixel_diagnostics || output.seek_advance_failure
            || output.seek_observer_failure || output.seek_preparation_failure
            || fields.contains("completion_repeat"))) return false;
        if(fields.contains("completion_repeat")) {
            const bool indexed=output.index_seek && output.index_recovery=="interior" && output.host_session
                && output.index_seek_target==208 && !output.index_extra_checkpoint && !output.index_preparation_fallback;
            if(fields["completion_repeat"]!="true" || !output.skip_intros || output.changed_inputs || output.corrected_inputs
                || (output.executor_control && !indexed && (!output.host_seek || output.host_seek_repeat || output.host_seek_target!=208
                    || output.historical_anchor_tick!=205 || output.historical_advanced_tick!=210
                    || (output.historical_cancel.empty()? !output.historical_single_step
                        : output.historical_cancel!="after" || output.historical_single_step || output.historical_exact_advance)))) return false;
            output.completion_repeat=true;
        }
        if(output.index_recovery=="interior" && !output.completion_repeat)return false;
        if (fields.contains("probe_move_state"))
        {
            if (fields["probe_move_state"] != "true" || output.watch_frames < 240 || output.probe_empty_interval
                || output.probe_world_pause || output.probe_interior_pause || output.probe_application_pause) return false;
            output.probe_move_state = true;
        }
        if (fields.contains("probe_small_restore"))
        {
            if (fields["probe_small_restore"] != "true" || !output.probe_interior_pause) return false;
            output.probe_small_restore = true;
        }
        return output.replay_path.is_absolute();
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
    if (require_authored_outcomes && !percentages.empty()) return false;
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
                    || location < 1 || location > 6) return false;
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
        s_instance_.store(nullptr, std::memory_order_release);
        if (match_finish_hook_registered_)
        {
            try { RC::Unreal::UObjectGlobals::UnregisterHook(match_finish_hook_path_, match_finish_hook_ids_); }
            catch (...) {}
        }
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
        if (engine_tick_pre_id_ != RC::Unreal::Hook::ERROR_ID)
            (void)RC::Unreal::Hook::UnregisterCallback(engine_tick_pre_id_);
        if (viewport_tick_id_ != RC::Unreal::Hook::ERROR_ID)
        {
            (void)RC::Unreal::Hook::UnregisterCallback(viewport_tick_id_);
        }
    }

    void on_unreal_init() override
    {
        Request startup_request{};
        const bool startup_request_valid=ReadRequest(QualificationRoot()/L"replay_request.txt",startup_request);
        if(startup_request_valid && (startup_request.probe_consumer_task || startup_request.consumer_mutation)) {
            using StartFailure=bool(*)(const wchar_t*,const char*);
            const auto start=ResolveHorseModExport<StartFailure>("horsemod_start_consumer_failure_diagnostic");
            if(!start || !start((QualificationRoot()/L"consumer_failure.json").c_str(),startup_request.run_id.c_str())) {
                bound_=false;
                Output::send<LogLevel::Error>(STR("[ReplayQualification] consumer failure diagnostic initialization failed\n"));
                return;
            }
            const auto observe=ResolveHorseModExport<StartFailure>("horsemod_start_niagara_observation");
            if(!observe || !observe((QualificationRoot()/L"niagara_observation.json").c_str(),startup_request.run_id.c_str())) {
                bound_=false;
                Output::send<LogLevel::Error>(STR("[ReplayQualification] Niagara observation initialization failed\n"));return;
            }
            // Begin before replay/level loading, including registration and the
            // first relevant combat effects. No observer activation at rollback
            // admission: that would miss the collection's original binding.
            const auto vfx_observe=ResolveHorseModExport<StartFailure>("horsemod_start_vfx_completion_observation");
            if(!vfx_observe || !vfx_observe((QualificationRoot()/L"vfx_completion_observation.json").c_str(),startup_request.run_id.c_str())) {
                bound_=false;
                Output::send<LogLevel::Error>(STR("[ReplayQualification] VFX completion observation initialization failed\n"));return;
            }
            const auto physics_observe=ResolveHorseModExport<StartFailure>("horsemod_start_physics_callback_observation");
            if(!physics_observe || !physics_observe((QualificationRoot()/L"physics_callback_observation.jsonl").c_str(),startup_request.run_id.c_str())) {
                bound_=false;
                Output::send<LogLevel::Error>(STR("[ReplayQualification] physics callback observation initialization failed\n"));return;
            }
        }
        if(startup_request_valid && startup_request.c18_diagnostic) {
            const auto observe=ResolveHorseModExport<bool(*)(const wchar_t*,const char*)>("horsemod_start_collection18_combat_observation");
            if(!observe || !observe((QualificationRoot()/L"vfx_completion_observation.json").c_str(),startup_request.run_id.c_str())) {
                bound_=false;
                Output::send<LogLevel::Error>(STR("[ReplayQualification] C18 diagnostic initialization failed\n"));return;
            }
        }
        if (ReadRequest(QualificationRoot() / L"replay_request.txt", startup_request) && startup_request.skip_intros) {
            // Owned-process diagnostic, before replay/level loading. Native
            // 141F853C0 selects the complete inline work when this registered
            // reference CVar is zero. Do not alter persistent Engine.ini.
            const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if(startup_request.probe_consumer_task
                && !ReplayStartupFailure::Start(base,startup_request.run_id,QualificationRoot())) {
                bound_=false;
                Output::send<LogLevel::Error>(STR("[ReplayQualification] startup fatal diagnostic installation failed\n"));
                return;
            }
            startup_observation_run_ = startup_request.run_id;
            auto* setting=reinterpret_cast<unsigned*>(base+0x4095698);
            const auto previous=*setting;
            if(previous>1) {bound_=false;return;}
            *setting=0;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] particle scheduling diagnostic run_id={} previous={} inline=true before_replay_load=true\n"),RC::to_generic_string(startup_request.run_id),previous);
        }
        bound_ = importer_.Bind(reinterpret_cast<std::uintptr_t>(
            GetModuleHandleW(nullptr)));
        bound_ = bound_ && navigator_.Bind(reinterpret_cast<std::uintptr_t>(
            GetModuleHandleW(nullptr)));
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
        viewport_tick_id_ =
            RC::Unreal::Hook::RegisterGameViewportClientTickPostCallback(
                [](RC::Unreal::Hook::TCallbackIterationData<void>&,
                   RC::Unreal::UGameViewportClient*, float) {
                    ReplayQualificationMod* self =
                        s_instance_.load(std::memory_order_acquire);
                    if (self != nullptr)
                        self->viewport_frame_count_.fetch_add(
                            1, std::memory_order_relaxed);
                }, options);
        if (viewport_tick_id_ == RC::Unreal::Hook::ERROR_ID)
        {
            bound_ = false;
            Output::send<LogLevel::Error>(STR(
                "[ReplayQualification] viewport-frame clock unavailable\n"));
            return;
        }
        engine_tick_pre_id_ = RC::Unreal::Hook::RegisterEngineTickPreCallback(
            [](RC::Unreal::Hook::TCallbackIterationData<void>&,
               RC::Unreal::UEngine*, float delta, bool) {
                if (auto* self = s_instance_.load(std::memory_order_acquire)) {
                    self->engine_tick_started_ = std::chrono::steady_clock::now();
                    // Install on the native game thread before the post callback
                    // can navigate to the replay. Partial installation cleanup
                    // therefore uses the same owner as normal detachment.
                    if (!self->startup_observation_run_.empty()) {
                        const bool ready = self->presentation_observer_.StartStartup(self->startup_observation_run_);
                        if (!ready) {
                            // Request loading has not run yet; preserve the
                            // startup identity for failure publication/cleanup.
                            self->request_.run_id = self->startup_observation_run_;
                            self->request_.skip_intros = true;
                            self->Fail("startup_observer_install_failed");
                        }
                        self->startup_observation_run_.clear();
                    }
                    self->presentation_observer_.ObserveStartupEngineBefore(delta);
                    self->FlushStartupLoading();
                }
            }, options);
        engine_tick_id_ = RC::Unreal::Hook::RegisterEngineTickPostCallback(
            [](RC::Unreal::Hook::TCallbackIterationData<void>&,
               RC::Unreal::UEngine*, float, bool) {
                ReplayQualificationMod* self =
                    s_instance_.load(std::memory_order_acquire);
                if (self != nullptr)
                {
                    const auto started = std::chrono::steady_clock::now();
                    self->engine_work_us_ = static_cast<std::uint64_t>(std::chrono::duration_cast<
                        std::chrono::microseconds>(started - self->engine_tick_started_).count());
                    self->FlushStartupLoading(true);
                    self->presentation_observer_.ObserveStartupEngineAfter();
                    self->TickGameThread();
                    self->observer_work_us_ = static_cast<std::uint64_t>(std::chrono::duration_cast<
                        std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
                }
            }, options);
        if (engine_tick_pre_id_ == RC::Unreal::Hook::ERROR_ID
            || engine_tick_id_ == RC::Unreal::Hook::ERROR_ID) bound_ = false;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] game-thread replay entry armed id={}\n"),
            engine_tick_id_);
    }

private:
    void FlushStartupLoading(bool after_engine=false)
    {
        if(!request_.flush_startup_loading || !playback_context_staged_ || state_==State::Failed
            || startup_loading_.complete)return;
        struct Native {
            std::uintptr_t base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            bool Owner()const{return (*reinterpret_cast<const bool*>(base+0x419718c))
                && (*reinterpret_cast<const DWORD*>(base+0x419716c))==GetCurrentThreadId();}
            bool Signatures()const {
                constexpr unsigned char flush[]={0x40,0x57,0x48,0x83,0xec,0x40,0x8b,0xf9,0xe8,0x43,0xd3,0xee,0xff,0xff,0x15,0xa5};
                constexpr unsigned char getter[]={0x40,0x53,0x48,0x83,0xec,0x20,0x65,0x48,0x8b,0x04,0x25,0x58,0,0,0,0x8b};
                constexpr unsigned char registration[]={0x40,0x53,0x48,0x83,0xec,0x40,0x49,0x8b,0x80,0xc0,0,0,0,0x32,0xdb,0x0f};
                constexpr unsigned char raw_complete[]={0x48,0x8b,0xc4,0x53,0x41,0x57,0x48,0x83,0xec,0x58,0x48,0x89,0x70,0x18,0x48,0x8d};
                constexpr unsigned char raw_getter[]={0x48,0x83,0xec,0x28,0x65,0x48,0x8b,0x04,0x25,0x58,0,0,0,0x8b,0x0d,0x0d};
                return !std::memcmp(reinterpret_cast<void*>(base+0xe510f0),flush,sizeof(flush))
                    && !std::memcmp(reinterpret_cast<void*>(base+0xe515c0),getter,sizeof(getter))
                    && !std::memcmp(reinterpret_cast<void*>(base+0x21be420),registration,sizeof(registration))
                    && !std::memcmp(reinterpret_cast<void*>(base+0x2ddfb20),raw_complete,sizeof(raw_complete))
                    && !std::memcmp(reinterpret_cast<void*>(base+0x2ddd630),raw_getter,sizeof(raw_getter));
            }
            unsigned Tick()const{return (*reinterpret_cast<const unsigned*>(base+0x470d0c4));}
            bool Ready()const{return (*reinterpret_cast<const int*>(base+0x429f250))<-1;}
            bool Suspended()const{return (*reinterpret_cast<const int*>(base+0x429ef70+0x74))!=0;}
            bool Pending()const{return (*reinterpret_cast<bool(__fastcall**)()>(base+0x40713a8))();}
            float& RegistrationBudget()const{return *reinterpret_cast<float*>(base+0x4093d30);}
            void Flush()const{reinterpret_cast<void(__fastcall*)(int)>(base+0xe510f0)(-1);}
            std::uint64_t Milliseconds()const{return GetTickCount64();}
            void WaitOne()const{Sleep(1);}
            int RawPending()const {
                // Do not construct the service or touch its lock before the
                // native static initializer publishes the completed epoch.
                const auto epoch=*reinterpret_cast<const int*>(base+0x43fd2fc);
                if(epoch==0)return 0;
                if(epoch>=-1)return -2;
                const auto service=*reinterpret_cast<const std::uintptr_t*>(base+0x40e3a28);
                if(!service)return -1;
                auto* lock=reinterpret_cast<CRITICAL_SECTION*>(service+0x60);
                if(!TryEnterCriticalSection(lock))return -2;
                const auto count=*reinterpret_cast<const int*>(service+0x58);
                const auto capacity=*reinterpret_cast<const int*>(service+0x5c);
                const auto data=*reinterpret_cast<const std::uintptr_t*>(service+0x50);
                const bool valid=count>=0 && count<=4096 && capacity>=count && (count==0 || data);
                LeaveCriticalSection(lock);
                return valid?count:-1;
            }
        } native;
        if(after_engine) {
            if(const auto* error=startup_loading_.AfterEngine(native,true))Fail(error);
            return;
        }
        if(const auto* error=startup_loading_.Poll(native,true)) {Fail(error);return;}
        if(startup_loading_.complete) {
            Output::send<LogLevel::Default>(STR("[ReplayQualification] startup loading flush complete run_id={} calls={} first_tick={} native_callbacks=true registration_budget_ms=60000 registration_previous_bits={:08x} registration_restored={} raw_asset_checks={} raw_asset_waits={} raw_asset_completion=true\n"),
                RC::to_generic_string(request_.run_id),startup_loading_.calls,native.Tick(),std::bit_cast<unsigned>(startup_loading_.previous_registration_budget),startup_loading_.registration_restored,startup_loading_.raw_checks,startup_loading_.raw_waits);
        }
    }
    Horse::Qualification::ReplayStartupLoading startup_loading_;

    bool ReadQualificationClock(
        std::array<std::uint64_t, 5>& clock) const noexcept
    {
        if (qualification_clock_ == nullptr
            || !qualification_clock_(clock.data(), clock.size()))
            return false;
        std::uint32_t native_battle_frame{};
        if (!ReadLuxBattleFrame(native_battle_frame)) return false;
        clock[1] = viewport_frame_count_.load(std::memory_order_acquire);
        // The native battle frame is the authored simulation clock. Unlike
        // HorseMod's outer-tick detour it exists in the rollback-disabled
        // stock oracle too, so stock and deterministic runs use the same TPS
        // definition without coupling it to diagnostic hook installation.
        clock[2] = native_battle_frame;
        return true;
    }

    void TickGameThread()
    {
        if (!bound_) return;
        RetryResultPublication();
        if (state_ == State::WaitingForLaunch) ObserveReplayTrajectory();
        if ((!battle_terminate_hook_registered_ || !match_finish_hook_registered_)
            && ++battle_terminate_hook_poll_divider_ >= 60)
        {
            battle_terminate_hook_poll_divider_ = 0;
            TryRegisterBattleTerminateHook();
            TryRegisterMatchFinishHook();
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
        if (state_ == State::Launched || state_ == State::Failed) return;
        if (state_ == State::Idle) LoadRequest();
        if (state_ == State::Importing) StartRequest();
    }

    // Called only after payload/initial-round validation and a real trajectory
    // sample. Native arm/entry independently check bounded frame/phase inputs.
    // Reading a return is metadata-only; it never revisits borrowed C18 pages.
    bool ObserveC18CombatSample(const ReplayTrajectorySample& sample)
    {
        if(!request_.c18_diagnostic || c18_diagnostic_logged_)return true;
        if(!c18_diagnostic_arm_ && sample.frame<170)return true;
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        std::uint64_t epoch{};
        if(!replay_scene_ready_ || battle_terminate_observed_ || boundary_failed_
            || !sample.source_active || sample.round!=0 || sample.manager_phase!=2
            || sample.round_state!=2 || sample.world_mode!=2 || !sample.round_frame
            || !ReadSchedulerValue(base+0x4197170,epoch) || !epoch) {
            Fail("c18_combat_trajectory_failed");return false;
        }
        if(!c18_diagnostic_arm_) {
            if(sample.frame!=170) {Fail("c18_combat_arm_boundary_missed");return false;}
            auto** slot=battle_manager_->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(STR("BattleReplayPlayer"));
            const auto arm=ResolveHorseModExport<std::uint64_t(*)(const char*,std::uintptr_t,std::uintptr_t,
                std::uintptr_t,std::uint32_t,std::uint64_t)>("horsemod_arm_collection18_combat_observation");
            c18_diagnostic_arm_=arm?arm(request_.run_id.c_str(),reinterpret_cast<std::uintptr_t>(battle_manager_),
                reinterpret_cast<std::uintptr_t>(slot),sample.replay_player,sample.frame,epoch):0;
            if(!c18_diagnostic_arm_) {Fail("c18_combat_arm_rejected");return false;}
            c18_diagnostic_epoch_=epoch;c18_diagnostic_player_=sample.replay_player;
        }
        if(sample.frame<170 || sample.frame>221 || epoch<c18_diagnostic_epoch_
            || sample.replay_player!=c18_diagnostic_player_) {Fail("c18_combat_window_missed");return false;}
        const auto read=ResolveHorseModExport<bool(*)(const char*,std::uint64_t*,std::size_t)>("horsemod_read_collection18_combat_return");
        std::uint64_t receipt[13]{};
        if(!read) {Fail("c18_combat_reader_missing");return false;}
        if(read(request_.run_id.c_str(),receipt,13)) {
            if(receipt[5]!=c18_diagnostic_arm_ || receipt[6]!=170 || receipt[7]!=c18_diagnostic_epoch_
                || receipt[8]!=reinterpret_cast<std::uintptr_t>(battle_manager_) || receipt[9]!=sample.replay_player
                || receipt[10]<170 || receipt[10]>220 || receipt[10]>sample.frame
                || receipt[11]<receipt[7] || receipt[11]>epoch || !receipt[12]
                || receipt[5]>=receipt[0] || receipt[0]>=receipt[1] || receipt[2]!=GetCurrentThreadId()) {
                Fail("c18_combat_return_mismatch");return false;
            }
            c18_diagnostic_logged_=true;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] C18 combat diagnostic return run_id={} entry_sequence={} return_sequence={} thread={} collection={} descriptor={} arm_sequence={} arm_frame={} arm_epoch={} manager={} player={} selected_frame={} selected_epoch={} selected_round_frame={} observed_frame={} observed_epoch={} ownership_proven=false completion_proven=false\n"),
                RC::to_generic_string(request_.run_id),receipt[0],receipt[1],receipt[2],receipt[3],receipt[4],receipt[5],receipt[6],receipt[7],
                receipt[8],receipt[9],receipt[10],receipt[11],receipt[12],sample.frame,epoch);
        } else if(sample.frame>220) {Fail("c18_combat_window_missed");return false;}
        return true;
    }
    void ObserveReplayTrajectory()
    {
        if (!request_.trajectory_control || !replay_scene_ready_) {
            if(c18_diagnostic_arm_)Fail("c18_combat_scene_lost");
            return;
        }
        if ((request_.probe_interior_pause || request_.probe_application_pause) && executor_started_ && !interior_completed_)
        {
            ReplayHost::InteriorWitness witness{};
            const auto read = ResolveHorseModExport<bool (*)(ReplayHost::InteriorWitness*)>(
                "horsemod_get_replay_interior_witness");
            if (!read || !read(&witness) || witness.phase == ReplayHost::InteriorPhase::Failed)
            { Fail("interior_pause_failed"); return; }
            if (witness.phase == ReplayHost::InteriorPhase::Resumed)
            {
                if (!interior_release_requested_) { Fail("interior_resume_without_hold"); return; }
                if (request_.probe_application_pause)
                {
                    if (request_.probe_particle_copy) ObserveParticleCopyResume(witness);
                    else
                    {
                    if (!application_checkpoint_
                        || FingerprintCheckpointStorage(*application_checkpoint_) != application_checkpoint_fingerprint_
                        || !application_scheduler_prepared_
                        || application_scheduler_prepared_->storage_fingerprint() != application_scheduler_prepared_fingerprint_)
                    { Fail("application_checkpoint_did_not_survive_resume"); return; }
                    // EngineTickPost precedes the application's epoch increment.
                    // Keep observing the first resumed interval normally; the
                    // following post can witness that its outer tail completed.
                    if (witness.epoch != interior_epoch_)
                    {
                    if (witness.epoch != interior_epoch_ + 1
                        || witness.completed_applications != application_hold_completed_ + 1)
                    { Fail("application_checkpoint_resume_tail_count_mismatch"); return; }
                    if (application_reentry_armed_)
                    {
                        interior_completed_ = true;
                        Output::send<LogLevel::Default>(STR(
                            "[ReplayQualification] application reentry resumed run_id={} held_tick={} native_callbacks={} "
                            "storage_unchanged=true\n"), RC::to_generic_string(request_.run_id), application_reentry_tick_, boundary_samples_ - interior_boundaries_);
                        application_scheduler_prepared_.reset();
                        Output::send<LogLevel::Default>(STR(
                            "[ReplayQualification] scheduler prepared storage released run_id={} unchanged=true never_installed=true\n"),
                            RC::to_generic_string(request_.run_id));
                        if (!application_checkpoint_->vfx.ReleaseOwners().ok())
                        { Fail("application_checkpoint_owner_retirement_failed"); return; }
                        application_checkpoint_.reset();
                        SceneReferenceObservation released{};
                        if (!ReadPhysicsSceneReference(witness.world, released)
                            || released != application_scene_reference_)
                        { Fail("application_scene_weak_lease_not_released"); return; }
                        Output::send<LogLevel::Default>(STR(
                            "[ReplayQualification] scheduler scene lease released run_id={} strong={} weak={}\n"),
                            RC::to_generic_string(request_.run_id), released.strong, released.weak);
                    }
                    else
                    {
                    Output::send<LogLevel::Default>(STR(
                        "[ReplayQualification] application checkpoint retained run_id={} captured_tick={} resumed_tick={} "
                        "epoch_advanced=true storage_unchanged=true borrowed_task=false\n"),
                        RC::to_generic_string(request_.run_id), application_target_tick_, witness.tick);
                    Output::send<LogLevel::Default>(STR(
                        "[ReplayQualification] application pause resumed run_id={} held_tick={} native_callbacks={}\n"),
                        RC::to_generic_string(request_.run_id), application_target_tick_, boundary_samples_ - interior_boundaries_);
                    const auto arm = ResolveHorseModExport<bool (*)(std::uint64_t, void*, ReplayHost::InteriorObserver)>(
                        "horsemod_arm_replay_application_pause");
                    if (!arm || !arm(application_reentry_tick_, this, [](void* context, const ReplayHost::InteriorWitness& held) {
                        return static_cast<ReplayQualificationMod*>(context)->ObserveApplicationPause(held);
                    })) { Fail("application_pause_rearm_rejected"); return; }
                    application_reentry_armed_ = true;
                    interior_started_ = false;
                    interior_release_requested_ = false;
                    }
                    }
                    }
                }
                else
                {
                    interior_completed_ = true;
                    Output::send<LogLevel::Default>(STR(
                        "[ReplayQualification] interior pause resumed run_id={} held_tick={} native_callbacks={}\n"),
                        RC::to_generic_string(request_.run_id), request_.probe_small_restore ? 360 : interior_expected_tick_, boundary_samples_ - interior_boundaries_);
                }
            }
        }
        if (world_pause_active_) { ObserveWorldPause(); return; }
        if(native_exit_phase_==NativeExitPhase::Waiting) { PollNativeSessionExit(); return; }
        if(request_.record_index && executor_started_ && !CheckIndexProgress()) return;
        if(request_.index_seek && index_seek_phase_==IndexSeek::Resuming) {
            ObserveIndexedSuffix();return;
        }
        if(index_endpoint_pending_) {
            if(request_.index_seek && !ReplayQualification::AwaitingIndexEndpoint(index_seek_phase_)) return;
            if(std::chrono::steady_clock::now()>trajectory_finish_deadline_)
                Fail("index_native_finish_timeout");
            else CompleteTrajectory();
            return;
        }
        if (trajectory_capture_complete_)
        {
            if (match_finish_observed_) PublishTrajectoryResult();
            else if (std::chrono::steady_clock::now() > trajectory_finish_deadline_)
                Fail("native_endpoint_missing_scene_finish");
            return;
        }
        if (battle_terminate_observed_) { Fail("replay_terminated_before_native_endpoint"); return; }
        if (boundary_failed_ || !boundary_observer_.healthy()) { Fail("boundary_observation_failed"); return; }
        if (request_.executor_control && executor_started_)
        {
            const auto read = ResolveHorseModExport<bool (*)(std::uint64_t*, std::size_t)>(
                "horsemod_get_replay_executor_status");
            std::array<std::uint64_t, 6> status{};
            if (!read || !read(status.data(), status.size()) || status[0] != 1 || status[5] != 0)
            { Fail("executor_failed"); return; }
        }
        if (battle_manager_ == nullptr) battle_manager_ = FindBattleManager();
        if(IsLiveReplayObject(battle_manager_)) presentation_observer_.ObserveStartupManager(battle_manager_);
        if (request_.include_setup && trajectory_samples_ == 0)
        {
            ReplayTrajectorySample setup{};
            if (!ReadReplayTrajectory(battle_manager_, setup, true)) return;
            if (setup_samples_ == 0)
            {
                if (setup.source_active) { Fail("setup_observer_started_too_late"); return; }
                std::array<std::uint8_t, 32> actual{};
                if (!importer_.VerifyPlaybackRecording(reinterpret_cast<void*>(setup.replay_player), actual)) return;
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] setup started run_id={} native_frame={} source_equal=true\n"),
                    RC::to_generic_string(request_.run_id), setup.frame);
                if (!StartBoundaryObserver(setup.frame)) return;
                if (request_.executor_control && !StartExecutor(setup.frame)) return;
            }
            if (!setup.source_active)
            {
                if (request_.skip_intros && !SkipIntroIfReady(setup)) return;
                if (setup_samples_ == 0 || setup != setup_last_)
                {
                    if (setup_samples_ >= 36000) { Fail("setup_observation_capacity"); return; }
                    setup_last_ = setup;
                    LogTrajectorySample(setup, ++setup_samples_, STR("setup"));
                }
                return;
            }
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] setup completed run_id={} observations={} native_frame={}\n"),
                RC::to_generic_string(request_.run_id), setup_samples_, setup.frame);
        }
        const bool winner_known = request_.trajectory_to_end && PollRoundOutcomeQualification();
        bool match_complete = false;
        if (state_ == State::Failed) return;
        ReplayTrajectorySample sample{};
        if (!ReadReplayTrajectory(battle_manager_, sample, trajectory_samples_ != 0))
        {
            if(c18_diagnostic_arm_)Fail("c18_combat_sample_unreadable");
            return;
        }
        match_complete = winner_known && sample.world_mode == 10 && sample.round_state == 10
            && !sample.source_active && sample.round + 1 == trajectory_source_rounds_;
        if (request_.probe_historical_restore && request_.historical_anchor_tick<=205
            && (request_.historical_cancel.empty() || request_.host_seek_repeat)
            && historical_execution_started_ && !historical_birth_reproduced_ && sample.frame >= 210)
        {
            std::uintptr_t vfx{};
            std::int32_t members{};
            if (sample.frame != 210 || !ReadSchedulerValue(reinterpret_cast<std::uintptr_t>(battle_manager_) + 0x508, vfx)
                || !ReadSchedulerValue(vfx + 0x3f0, members) || members != 8)
            {
                Output::send<LogLevel::Warning>(STR("[ReplayQualification] historical birth witness rejected tick={} members={} enclosing_resume_observed={}\n"),sample.frame,members,interior_completed_);
                Fail("historical_particle_birth_not_reproduced"); return;
            }
            historical_birth_reproduced_ = true;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical particle birth reproduced run_id={} tick=210 combat_particles=8\n"),
                RC::to_generic_string(request_.run_id));
        }
        if (match_finish_observed_ && !match_complete)
        { Fail("scene_finished_before_native_endpoint"); return; }
        if (trajectory_samples_ == 0)
        {
            std::array<std::uint8_t, 32> actual{};
            const bool same = importer_.VerifyPlaybackRecording(
                reinterpret_cast<void*>(sample.replay_player), actual);
            const auto hex = [](const auto& bytes) {
                std::ostringstream text;
                text << std::hex << std::setfill('0');
                for (auto byte : bytes) text << std::setw(2) << static_cast<unsigned>(byte);
                return RC::to_generic_string(text.str());
            };
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] payload handoff run_id={} battle_sha256={} "
                "expected_recording_sha256={} actual_recording_sha256={} "
                "source_equal={} round={} cursor={}\n"),
                RC::to_generic_string(request_.run_id), hex(importer_.battle_identity()),
                hex(importer_.recording_identity()), hex(actual), same,
                sample.round, sample.cursor);
            if (!same) { Fail("playback_recording_payload_mismatch"); return; }
            // Setup verifies the imported payload before tracker activation.
            // Apply the control intervention only after the active handoff is
            // independently verified, before any edited sample is consumed.
            if(request_.source_revision && !request_.executor_control && !ApplyAuthoredSourceRevision(false)) {
                Fail("native_authored_revision_failed");return;
            }
            if(!request_.rolling_corrections.empty() && (!request_.executor_control || request_.authored_prefix) && !ApplyRollingAuthoredControl()) {
                Fail("native_rolling_authored_revision_failed");return;
            }
            ReplaySourceExtent extent{};
            if (!ReadReplaySourceExtent(sample.replay_player, extent))
            { Fail("playback_source_extent_unavailable"); return; }
            trajectory_source_rounds_ = extent.rounds;
            for (std::int32_t round = 0; round < extent.rounds; ++round)
                for (std::int32_t slot = 0; slot < extent.recorder_counts[round]; ++slot)
                    Output::send<LogLevel::Default>(STR(
                        "[ReplayQualification] source extent run_id={} rounds={} round={} slot={} recorded_time={} source_samples={}\n"),
                        RC::to_generic_string(request_.run_id), extent.rounds, round, slot,
                        extent.recorded_times[round][slot], extent.source_samples[round][slot]);
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] initial round snapshot run_id={} version=1 round={} cursor={} "
                "available={} consumed_equal={} raw_equal={} raw_difference={} expected={:08x} actual={:08x} "
                "consumed_difference={} expected_motion_count={} actual_motion_count={} "
                "stage_seed={} applied_round={} restore_mode={}\n"),
                RC::to_generic_string(request_.run_id), sample.round, sample.cursor,
                sample.initial_reset_available, sample.initial_reset_equal, sample.initial_reset_raw_equal,
                sample.initial_reset_difference, sample.initial_reset_expected, sample.initial_reset_actual,
                sample.initial_reset_consumed_difference,
                sample.initial_reset_expected_count, sample.initial_reset_actual_count,
                sample.stage_seed, sample.applied_round, sample.restore_mode);
            if (!sample.initial_reset_equal) { Fail("initial_round_snapshot_mismatch"); return; }
            // Native playback is now verified. The entry deadline must not
            // expire while a full-match observation is already in progress.
            battle_scene_observed_ = true;
            if (!boundary_started_ && !StartBoundaryObserver(sample.frame)) return;
            if (request_.executor_control && !executor_started_ && !StartExecutor(sample.frame)) return;
        }
        if(!ObserveC18CombatSample(sample))return;
        if (trajectory_samples_ != 0 && sample.frame == trajectory_last_.frame
            && sample.round == trajectory_last_.round && sample.cursor == trajectory_last_.cursor)
        {
            if (match_complete) CompleteTrajectory();
            return;
        }
        if(!request_.host_session && request_.index_extra_checkpoint && index_capture_==IndexCapture::Resuming && index_capture_tick_==170
            && sample.frame>=request_.index_extra_checkpoint-1) {
            index_capture_tick_=request_.index_extra_checkpoint;index_capture_=IndexCapture::Waiting;
            index_host_completion_seen_=false;
        }
        if(request_.index_checkpoint && !request_.host_session && executor_started_ && index_capture_==IndexCapture::Waiting && sample.frame>=index_capture_tick_-1) {
            if(!ReplayQualification::CanArmIndexCheckpoint(sample.frame,index_capture_tick_,sample.source_active))
            {Fail("index_checkpoint_admission_boundary_missed");return;}
            if(index_capture_tick_==170) {
                const auto monitor=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
                const auto arm=ResolveHorseModExport<std::uint16_t (*)(std::uint64_t)>("horsemod_request_index_checkpoint");
                if(!arm || arm(170)!=0 || !monitor || !monitor(this,[](void* context,const ReplayHost::InteriorWitness& held) {
                    static_cast<ReplayQualificationMod*>(context)->ObserveHostIndexCheckpoint(held);
                })) {Fail("host_index_checkpoint_arm_failed");return;}
                Output::send<LogLevel::Default>(STR("[ReplayQualification] host index checkpoint armed run_id={} origin={} target={}\n"),RC::to_generic_string(request_.run_id),index_capture_tick_-1,index_capture_tick_);
            } else {
                const auto arm=ResolveHorseModExport<bool (*)(std::uint64_t,void*,ReplayHost::InteriorObserver)>("horsemod_arm_replay_application_pause");
                if(!arm || !arm(index_capture_tick_,this,[](void* context,const ReplayHost::InteriorWitness& held) {
                    return static_cast<ReplayQualificationMod*>(context)->ObserveIndexCheckpoint(held);
                })) {Fail("index_checkpoint_arm_failed");return;}
            }
            index_capture_=IndexCapture::Armed;
        }
        if (!combat_pause_armed_ && !request_.probe_small_restore && executor_started_
            && (request_.probe_interior_pause || request_.probe_application_pause)
            && sample.source_active && (sample.round == 0 || (request_.probe_historical_restore
                && request_.host_seek && request_.historical_exact_advance && request_.historical_anchor_tick>220))
            && sample.round_state == 2 && sample.world_mode == 2
            && (request_.probe_historical_restore ? sample.frame + 1 == request_.historical_anchor_tick
                : request_.probe_consumer_task ? sample.frame + 1 == 210
                : sample.round_frame >= (request_.probe_application_pause ? 41u : 36u)))
        {
            interior_expected_tick_ = sample.frame + 1;
            application_target_tick_ = interior_expected_tick_;
            application_reentry_tick_ = request_.probe_historical_restore ? request_.historical_advanced_tick : request_.probe_consumer_task ? 217 : application_target_tick_ + 5;
            const auto arm = ResolveHorseModExport<bool (*)(std::uint64_t, void*, ReplayHost::InteriorObserver)>(
                request_.probe_application_pause ? "horsemod_arm_replay_application_pause" : "horsemod_arm_replay_interior_pause");
            if (!arm || !arm(interior_expected_tick_, this, [](void* context, const ReplayHost::InteriorWitness& witness) {
                auto* self = static_cast<ReplayQualificationMod*>(context);
                return self->request_.probe_application_pause ? self->ObserveApplicationPause(witness) : self->ObserveInteriorPause(witness);
            })) { Fail("combat_pause_arm_rejected"); return; }
            combat_pause_armed_ = true;
        }
        const auto timing_now = std::chrono::steady_clock::now();
        if (world_pause_completed_ && !world_resume_measured_)
        {
            const auto gap = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                timing_now - world_resume_previous_).count());
            world_resume_previous_ = timing_now;
            world_resume_max_gap_us_ = (std::max)(world_resume_max_gap_us_, gap);
            if (gap > 20'000) ++world_resume_late_gaps_;
            const auto ticks = sample.frame - world_resume_frame_;
            if (ticks >= 120)
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                    timing_now - world_resume_started_).count();
                const auto viewport = viewport_frame_count_.load(std::memory_order_relaxed) - world_resume_viewport_;
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] world resume timing run_id={} native_ticks={} elapsed_us={} viewport_frames={} max_observation_gap_us={} gaps_over_20ms={}\n"),
                    RC::to_generic_string(request_.run_id), ticks, elapsed, viewport,
                    world_resume_max_gap_us_, world_resume_late_gaps_);
                world_resume_measured_ = true;
            }
        }
        if (trajectory_samples_ == 0)
        {
            trajectory_timing_started_ = timing_now;
            trajectory_viewport_start_ = viewport_frame_count_.load(std::memory_order_relaxed);
            trajectory_native_start_ = sample.frame;
            trajectory_max_gap_us_ = trajectory_late_gaps_ = 0;
            trajectory_engine_us_ = trajectory_observer_us_ = 0;
            trajectory_pacing_us_ = 0;
            trajectory_max_engine_us_ = trajectory_max_observer_us_ = 0;
        }
        else
        {
            const auto gap = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                timing_now - trajectory_timing_previous_).count());
            trajectory_max_gap_us_ = (std::max)(trajectory_max_gap_us_, gap);
            if (gap > 20'000) ++trajectory_late_gaps_;
            trajectory_engine_us_ += engine_work_us_;
            // 142189040 publishes actual time spent in its Sleep/yield cap
            // loop here. Observe it; never substitute expected replay timing.
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            trajectory_pacing_us_ += *reinterpret_cast<const double*>(base + 0x418a9e8) * 1'000'000.0;
            trajectory_observer_us_ += observer_work_us_;
            trajectory_max_engine_us_ = (std::max)(trajectory_max_engine_us_, engine_work_us_);
            trajectory_max_observer_us_ = (std::max)(trajectory_max_observer_us_, observer_work_us_);
        }
        trajectory_timing_previous_ = timing_now;
        trajectory_last_ = sample;
        ++trajectory_samples_;
        LogTrajectorySample(sample, trajectory_samples_, STR("trajectory"));
        if (request_.probe_world_pause && !world_pause_completed_ && trajectory_samples_ == 120)
        {
            const auto pause = ResolveHorseModExport<bool (*)(bool)>("horsemod_set_replay_world_paused");
            if (!pause || !pause(true)) { Fail("world_pause_rejected"); return; }
            world_pause_active_ = true;
            world_pause_started_ = std::chrono::steady_clock::now();
            world_pause_viewport_ = viewport_frame_count_.load(std::memory_order_relaxed);
            world_pause_boundaries_ = boundary_samples_;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] world pause started run_id={} native_frame={}\n"),
                RC::to_generic_string(request_.run_id), sample.frame);
            return;
        }
        if (request_.probe_empty_interval && !empty_interval_observed_ && trajectory_samples_ == 120)
        {
            if (!ProbeEmptyInterval(sample)) return;
        }
        if (request_.probe_move_state && !move_state_observed_ && trajectory_samples_ == 120)
        {
            if (!ProbeMoveStateInterval(sample)) return;
        }
        const bool retained_source_stop=request_.host_session && request_.index_seek && request_.executor_control
            && winner_known && sample.world_mode==5 && sample.round_state==5 && !sample.source_active
            && sample.round+1==trajectory_source_rounds_;
        if (match_complete || retained_source_stop || trajectory_samples_ >= request_.watch_frames)
        {
            if (request_.trajectory_to_end && !match_complete && !retained_source_stop)
                Fail("trajectory_capacity_before_match_end");
            else CompleteTrajectory();
        }
    }

    bool ProbeEmptyInterval(const ReplayTrajectorySample& before)
    {
        // Deliberately invoke one engine interval with no new input production.
        // Both native control and executor use the same call and must preserve
        // observed simulation/source state. No expected bytes are written.
        if (before.manager_phase != 2 || before.move_state != 0 || before.round_state != 2)
        { Fail("empty_interval_requires_active_quiet_boundary"); return false; }
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] empty interval started run_id={} native_frame={}\n"),
            RC::to_generic_string(request_.run_id), before.frame);
        reinterpret_cast<void (*)(void*, float)>(base + 0x3fbf30)(battle_manager_, 0.0f);
        ReplayTrajectorySample after{};
        if (!ReadReplayTrajectory(battle_manager_, after) || before != after || boundary_failed_)
        { Fail("empty_interval_changed_native_observation"); return false; }
        empty_interval_observed_ = true;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] empty interval completed run_id={} native_frame={} unchanged=true\n"),
            RC::to_generic_string(request_.run_id), after.frame);
        return true;
    }

    bool ObserveInteriorPause(const ReplayHost::InteriorWitness& witness)
    {
        // A rejected hold never becomes a resume request on a later update.
        // The owned-process runner handles this development failure; normal
        // replay cancellation and recovery require the later lifecycle gate.
        if (state_ == State::Failed) return false;
        ReplayTrajectorySample sample{};
        const void* event{};
        if ((witness.phase != ReplayHost::InteriorPhase::Holding
                && witness.phase != ReplayHost::InteriorPhase::Releasing)
            || witness.tick != interior_expected_tick_ || !ReadReplayTrajectory(battle_manager_, sample, true)
            || sample.frame != interior_expected_tick_ || !ReadPendingInteriorTask(battle_manager_, witness, event))
        { Fail("interior_native_witness_invalid"); return false; }
        if (!interior_started_)
        {
            const bool late_recovery=ReplayQualification::AllowsLateRecoveryHold(
                request_.host_seek && request_.seek_settlement_failure && request_.historical_cancel=="after"
                    && request_.historical_anchor_tick==170 && request_.historical_advanced_tick==2504
                    && request_.host_seek_target==11000 && sample.frame==11000,
                sample.source_active,sample.manager_phase,sample.move_state,sample.round_state,sample.world_mode,sample.cursor);
            if (!request_.probe_small_restore && !late_recovery && (!sample.source_active || sample.manager_phase != 2
                || sample.round_state != 2 || sample.world_mode != 2 || sample.cursor == 0))
            { Fail("interior_pause_requires_active_combat"); return false; }
            if(late_recovery)Output::send<LogLevel::Default>(STR("[ReplayQualification] interior hold scope=late_round_recovery active_combat=false tick={}\n"),sample.frame);
            if (!boundary_last_round_sequence_ || sample != boundary_last_ || !witness.surface_bytes)
            { Fail("interior_not_completed_tick_boundary"); return false; }
            interior_started_ = true;
            interior_sample_ = sample;
            interior_task_ = witness.pending_task;
            interior_event_ = event;
            interior_epoch_ = witness.epoch;
            interior_boundaries_ = boundary_samples_;
            interior_surface_start_ = witness.surface_frames;
            interior_started_at_ = std::chrono::steady_clock::now();
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] interior pause started run_id={} native_frame={} epoch={} pending_event=true\n"),
                RC::to_generic_string(request_.run_id), witness.tick, witness.epoch);
        }
        if (sample != interior_sample_ || boundary_samples_ != interior_boundaries_
            || witness.pending_task != interior_task_ || event != interior_event_ || witness.epoch != interior_epoch_)
        { Fail("interior_state_or_pending_work_advanced"); return false; }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - interior_started_at_).count();
        const auto frames = witness.surface_frames - interior_surface_start_;
        if (request_.probe_interactive_controls && !request_.seek_settlement_failure)
        {
            if (witness.ui_resume_requests > 1) { Fail("interior_duplicate_ui_activation"); return false; }
            if (!witness.ui_resume_requests)
            {
                if (elapsed > 3'000'000) Fail("interior_ui_activation_missing");
                return false;
            }
        }
        if (elapsed < 1'000'000) return false;
        if (frames < 30 || witness.application_updates < 30)
        {
            Output::send<LogLevel::Default>(STR("[ReplayQualification] interior responsiveness rejected run_id={} elapsed_us={} surface_frames={} application_updates={} surface_pending={}\n"),
                RC::to_generic_string(request_.run_id),elapsed,frames,witness.application_updates,witness.surface_pending);
            Fail("interior_ui_or_application_not_responsive"); return false;
        }
        if (request_.probe_small_restore && !small_restore_completed_ && witness.surface_pending) return false;
        if (!interior_release_requested_)
        {
            if (request_.probe_interactive_controls && !request_.seek_settlement_failure)
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] interior control verified run_id={} native_frame={} requests={} "
                    "application_updates={} pending_event=true unchanged=true\n"),
                    RC::to_generic_string(request_.run_id), witness.tick, witness.ui_resume_requests, witness.application_updates);
            interior_release_requested_ = true;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] interior pause held run_id={} native_frame={} epoch={} elapsed_us={} "
                "application_updates={} surface_frames={} surface_bytes={} unchanged=true pending_event=true\n"),
                RC::to_generic_string(request_.run_id), witness.tick, witness.epoch, elapsed,
                witness.application_updates, frames, witness.surface_bytes);
        }
        if (request_.probe_small_restore && !small_restore_completed_ && !ObserveSmallRestore()) return false;
        return true;
    }

    static std::uint64_t FingerprintCheckpointStorage(const ReplayHost::Checkpoint& checkpoint)
    {
        // Diagnostic mutation witness, not a canonical simulation hash.
        std::uint64_t value = 14695981039346656037ull;
        const auto bytes = [&](const auto& storage) {
            for (auto byte : storage) { value ^= std::to_integer<unsigned>(byte); value *= 1099511628211ull; }
        };
        bytes(checkpoint.gameplay.bytes);
        for (const auto& local : checkpoint.gameplay.local_images) bytes(local.bytes);
        value ^= checkpoint.scheduler.storage_fingerprint();
        value *= 1099511628211ull;
        value ^= checkpoint.vfx.storage_fingerprint();
        value *= 1099511628211ull;
        return value;
    }

    struct SceneReferenceObservation
    {
        std::uintptr_t control{};
        int strong{}, weak{};
        friend bool operator==(const SceneReferenceObservation&, const SceneReferenceObservation&) = default;
    };
    static bool ReadPhysicsSceneReference(const void* world, SceneReferenceObservation& output) noexcept
    {
        // Independent native hash lookup, unlike the runtime's sparse scan.
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        __try
        {
            int index = -1;
            reinterpret_cast<void (*)(void*, int*, const void*)>(base + 0x15827a0)(
                reinterpret_cast<void*>(base + 0x4096120), &index, world);
            const auto slots = *reinterpret_cast<const int*>(base + 0x4096128);
            if (index < 0 || index >= slots) return false;
            const auto entry = *reinterpret_cast<const std::uintptr_t*>(base + 0x4096120)
                + static_cast<std::size_t>(index) * 0x20;
            if (*reinterpret_cast<const void* const*>(entry) != world) return false;
            const auto scene = *reinterpret_cast<const std::uintptr_t*>(entry + 8);
            output.control = *reinterpret_cast<const std::uintptr_t*>(entry + 0x10);
            if (!scene || !output.control
                || *reinterpret_cast<const std::uintptr_t*>(output.control) != base + 0x35e56a0
                || *reinterpret_cast<const std::uintptr_t*>(output.control + 0x10) != scene) return false;
            output.strong = *reinterpret_cast<const int*>(output.control + 8);
            output.weak = *reinterpret_cast<const int*>(output.control + 0xc);
            return output.strong > 0 && output.weak > 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool ValidateCapturedSchedulerBindings(const ReplayHost::InteriorWitness& witness)
    {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        auto& scheduler = application_checkpoint_->scheduler;
        SceneReferenceObservation observed{};
        auto expected = application_scene_reference_;
        ++expected.weak;
        Horse::Deterministic::Sc6ReplaySchedulerState::BindingFailure detail{};
        const auto binding=scheduler.ValidateBindings(base,const_cast<void*>(witness.world),&detail);
        const auto foreign_base=scheduler.ValidateBindings(0,const_cast<void*>(witness.world));
        const auto foreign_world=scheduler.ValidateBindings(base,nullptr);
        const bool scene_read=ReadPhysicsSceneReference(witness.world,observed);
        if(!binding.ok() || foreign_base.code!=Horse::Deterministic::FailureCode::GenerationMismatch
            || foreign_world.code!=Horse::Deterministic::FailureCode::GenerationMismatch || !scene_read || observed!=expected) {
            Output::send<LogLevel::Warning>(STR(
                "[ReplayQualification] scheduler lifetime detail run_id={} tick={} binding_code={} check={} owner_index={} owner_generation={} tick_address={:x} owner_address={:x} offset={} expected={:x} actual={:x} foreign_base_code={} foreign_world_code={} scene_read={} expected_control={:x} observed_control={:x} expected_strong={} observed_strong={} expected_weak={} observed_weak={} addresses_process_local=true\n"),
                RC::to_generic_string(request_.run_id),witness.tick,static_cast<unsigned>(binding.code),
                RC::to_generic_string(detail.check?detail.check:"none"),detail.weak[0],detail.weak[1],detail.tick,detail.owner,
                detail.offset,detail.expected,detail.actual,static_cast<unsigned>(foreign_base.code),static_cast<unsigned>(foreign_world.code),
                scene_read,expected.control,observed.control,expected.strong,observed.strong,expected.weak,observed.weak);
            Fail("application_scheduler_lifetime_preflight_failed");return false;
        }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] scheduler bindings validated run_id={} native_frame={} strong={} weak={} foreign_bindings_rejected=true\n"),
            RC::to_generic_string(request_.run_id), witness.tick, observed.strong, observed.weak);
        return true;
    }

    bool PrepareCapturedScheduler(const ReplayHost::InteriorWitness& witness)
    {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        auto& scheduler = application_checkpoint_->scheduler;
        application_scheduler_prepared_ = std::make_unique<ReplayScheduler::PreparedRestore>();
        auto& prepared = *application_scheduler_prepared_;
        const auto status = scheduler.PrepareRestore(scheduler, base, const_cast<void*>(witness.world),
            Horse::Deterministic::Schema::replay_checkpoint_memory_budget, prepared);
        if (!status.ok() || !prepared.ready() || !prepared.allocation_count()
            || prepared.patch_count() != scheduler_inventory_counts_[0] * 4 + scheduler_inventory_counts_[1])
        {
            Output::send<LogLevel::Warning>(STR("[ReplayQualification] scheduler preparation failed code={}\n"),
                static_cast<unsigned>(status.code));
            Fail("application_scheduler_preparation_failed"); return false;
        }
        application_scheduler_prepared_fingerprint_ = prepared.storage_fingerprint();
        ReplayScheduler::PreparedRestore rejected;
        const auto capacity = scheduler.PrepareRestore(scheduler, base, const_cast<void*>(witness.world), 0, rejected);
        ReplayTrajectorySample sample{};
        if (capacity.code != Horse::Deterministic::FailureCode::CapacityExceeded || rejected.ready()
            || rejected.owned_bytes() != sizeof(ReplayScheduler::PreparedRestore)
            || rejected.allocation_count() || rejected.patch_count()
            || prepared.storage_fingerprint() != application_scheduler_prepared_fingerprint_
            || FingerprintCheckpointStorage(*application_checkpoint_) != application_checkpoint_fingerprint_
            || !ReadReplayTrajectory(battle_manager_, sample, true) || sample != interior_sample_
            || boundary_samples_ != interior_boundaries_ || !ReadCompletedApplicationTaskState(battle_manager_, witness.epoch))
        { Fail("application_scheduler_preparation_changed_live_state"); return false; }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] scheduler prepared run_id={} patches={} allocations={} bytes={} "
            "zero_budget_rejected=true live_unchanged=true never_installed=true\n"), RC::to_generic_string(request_.run_id),
            prepared.patch_count(), prepared.allocation_count(), prepared.owned_bytes());
        // A separate transaction leaves the retained preparation above intact.
        // Install only at this exact completed boundary, first cancelling after
        // writes to exercise undo, then committing and observing continuation.
        ReplayScheduler::PreparedRestore transaction;
        const auto budget = Horse::Deterministic::Schema::replay_checkpoint_memory_budget - prepared.owned_bytes();
        auto applied = scheduler.PrepareRestore(scheduler, base, const_cast<void*>(witness.world), budget, transaction);
        if (!applied.ok()) { Fail("application_scheduler_transaction_preparation_failed"); return false; }
        const auto transaction_fingerprint = transaction.storage_fingerprint();
        const auto transaction_bytes = transaction.owned_bytes();
        const auto unchanged = [&]() {
            return FingerprintCheckpointStorage(*application_checkpoint_) == application_checkpoint_fingerprint_
                && prepared.storage_fingerprint() == application_scheduler_prepared_fingerprint_
                && ReadReplayTrajectory(battle_manager_, sample, true) && sample == interior_sample_
                && boundary_samples_ == interior_boundaries_
                && ReadCompletedApplicationTaskState(battle_manager_, witness.epoch);
        };
        applied = scheduler.RestoreBacking(scheduler, base, const_cast<void*>(witness.world), 0, transaction);
        if (applied.code != Horse::Deterministic::FailureCode::CapacityExceeded || !transaction.ready()
            || transaction.storage_fingerprint() != transaction_fingerprint || !unchanged())
        { Fail("application_scheduler_transaction_capacity_rejection_failed"); return false; }
        applied = scheduler.RestoreBacking(scheduler, base, const_cast<void*>(witness.world), budget, transaction,
            [](void*) noexcept { return true; });
        if (applied.code != Horse::Deterministic::FailureCode::Cancelled || !transaction.ready()
            || transaction.storage_fingerprint() != transaction_fingerprint || !unchanged())
        { Fail("application_scheduler_transaction_undo_failed"); return false; }
        const auto started = std::chrono::steady_clock::now();
        applied = scheduler.RestoreBacking(scheduler, base, const_cast<void*>(witness.world), budget, transaction);
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
        if (!applied.ok() || transaction.ready()
            || transaction.owned_bytes() != sizeof(ReplayScheduler::PreparedRestore)
            || transaction.allocation_count() || transaction.patch_count() || !unchanged()
            || !scheduler.ValidateBindings(base, const_cast<void*>(witness.world)).ok())
        {
            Output::send<LogLevel::Warning>(STR("[ReplayQualification] scheduler transaction failed code={}\n"),
                static_cast<unsigned>(applied.code));
            Fail("application_scheduler_transaction_commit_failed"); return false;
        }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] scheduler backing replaced run_id={} native_frame={} epoch={} bytes={} elapsed_us={} "
            "capacity_rejected=true cancellation_undone=true committed=true unchanged=true\n"),
            RC::to_generic_string(request_.run_id), witness.tick, witness.epoch, transaction_bytes, elapsed);
        return true;
    }

    struct SchedulerTickObservation
    {
        std::uintptr_t tick{};
        std::uint32_t cooldown{}, last_time{};
        std::int32_t admitted{}, queued{};
        friend bool operator==(const SchedulerTickObservation&, const SchedulerTickObservation&) = default;
    };

    struct ParticleIdentityObservation
    {
        std::uintptr_t address{};
        std::int32_t index{}, serial{};
    };
    bool ObserveParticleEmitterOwnership(std::uintptr_t component, std::uint64_t tick)
    {
        std::uintptr_t entries{};
        std::int32_t count{}, capacity{};
        if (!ReadSchedulerValue(component + 0xa50, entries) || !ReadSchedulerValue(component + 0xa58, count)
            || !ReadSchedulerValue(component + 0xa5c, capacity) || count < 0 || count > 64
            || capacity < count || (count && !entries)) return false;
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] particle emitter inventory run_id={} native_frame={} component={} count={}\n"),
            RC::to_generic_string(request_.run_id), tick, component, count);
        for (int i = 0; i < count; ++i)
        {
            std::uintptr_t emitter{}, vtable{}, owner{};
            if (!ReadSchedulerValue(entries + static_cast<std::size_t>(i) * 8, emitter)) return false;
            if (emitter && (!ReadSchedulerValue(emitter, vtable) || !ReadSchedulerValue(emitter + 0x18, owner)
                || owner != component || vtable < base)) return false;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] particle emitter binding run_id={} native_frame={} component={} ordinal={} "
                "emitter={} vtable_rva={} owner={}\n"), RC::to_generic_string(request_.run_id), tick, component,
                i, emitter, emitter ? vtable - base : 0, owner);
            if (!emitter) continue;
            if (vtable != base + 0x3949b60 && vtable != base + 0x394c100) return false;
            // Needed to admit the emitter transaction, not another storage image:
            // 141F9BFE0 invokes the asset's +158 module list at virtual +270;
            // 141F8F9E0 owns the listed payloads and attached-object callbacks.
            std::uintptr_t asset{}, modules{}, instance_data{}, attached{};
            std::int32_t module_count{}, instance_bytes{}, attached_count{}, particle_count{}, particle_capacity{}, stride{};
            if (!ReadSchedulerValue(emitter + 0x10, asset) || !asset
                || !ReadSchedulerValue(asset + 0x158, modules) || !ReadSchedulerValue(asset + 0x160, module_count)
                || module_count < 0 || module_count > 128 || (module_count && !modules)
                || !ReadSchedulerValue(emitter + 0x100, instance_data) || !ReadSchedulerValue(emitter + 0x108, instance_bytes)
                || instance_bytes < 0 || instance_bytes > 1024 * 1024 || (instance_bytes && !instance_data)
                || !ReadSchedulerValue(emitter + 0x1c0, attached) || !ReadSchedulerValue(emitter + 0x1c8, attached_count)
                || attached_count < 0 || attached_count > 128 || (attached_count && !attached)
                || !ReadSchedulerValue(emitter + 0x118, particle_count) || !ReadSchedulerValue(emitter + 0x120, particle_capacity)
                || !ReadSchedulerValue(emitter + 0x114, stride)) return false;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] particle payload ownership run_id={} tick={} emitter={} asset={} modules={} instance_bytes={} attached={} active={} capacity={} stride={}\n"),
                RC::to_generic_string(request_.run_id), tick, emitter, asset, module_count, instance_bytes, attached_count,
                particle_count, particle_capacity, stride);
            const auto observe_object = [&](std::uintptr_t address, bool module, int ordinal) {
                std::uintptr_t object{}, table{}, initialize{};
                if (!ReadSchedulerValue(address, object) || !object || !ReadSchedulerValue(object, table)
                    || table < base || (module && !ReadSchedulerValue(table + 0x270, initialize))) return false;
                auto* value = reinterpret_cast<RC::Unreal::UObject*>(object);
                const auto* item = RC::Unreal::FUObjectArray::IndexToObject(value->GetInternalIndex());
                if (!item || item->GetUObject() != value || !item->IsValid(false)) return false;
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] particle payload binding run_id={} tick={} emitter={} module={} ordinal={} vtable_rva={} initialize_rva={} object={}\n"),
                    RC::to_generic_string(request_.run_id), tick, emitter, module, ordinal, table - base,
                    module && initialize >= base ? initialize - base : 0, value->GetFullName());
                return true;
            };
            for (int j = 0; j < module_count; ++j)
                if (!observe_object(modules + static_cast<std::size_t>(j) * 8, true, j)) return false;
            for (int j = 0; j < attached_count; ++j)
                if (!observe_object(attached + static_cast<std::size_t>(j) * 8, false, j)) return false;
            if (vtable != base + 0x394c100) continue;
            // Only this native constructor/initializer variant has the tile
            // pool and render resource fields audited at these offsets.
            std::uintptr_t system{}, render{}, pool{}, tiles{};
            std::int32_t tile_count{}, free_tiles{}, seed{}, current_seed{};
            if (!ReadSchedulerValue(emitter + 0x1d0, system) || !system
                || !ReadSchedulerValue(emitter + 0x1e0, render)
                || !ReadSchedulerValue(system + 0x78, pool) || !pool
                || !ReadSchedulerValue(emitter + 0x1e8, tiles) || !ReadSchedulerValue(emitter + 0x1f0, tile_count)
                || !ReadSchedulerValue(pool + 0x40190, free_tiles)
                || !ReadSchedulerValue(emitter + 0x288, seed) || !ReadSchedulerValue(emitter + 0x28c, current_seed)) return false;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] particle emitter tile ownership run_id={} native_frame={} emitter={} "
                "system={} render={} pool={} tiles={} tile_count={} free_tiles={} initial_seed={} current_seed={}\n"),
                RC::to_generic_string(request_.run_id), tick, emitter, system, render, pool, tiles,
                tile_count, free_tiles, seed, current_seed);
        }
        return true;
    }

    bool CaptureParticleIdentities()
    {
        // Diagnostic inventory once at capture, outside simulation observation
        // loops. This is identity evidence, not a lifetime lease or checkpoint.
        std::vector<RC::Unreal::UObject*> particles;
        RC::Unreal::UObjectGlobals::FindAllOf(L"LuxParticleSystemComponent", particles);
        if (particles.size() > particle_identities_.size()) return false;
        particle_identity_count_ = 0;
        for (auto* particle : particles)
        {
            const auto index = particle->GetInternalIndex();
            const auto* item = RC::Unreal::FUObjectArray::IndexToObject(index);
            if (!item || item->GetUObject() != particle || !item->IsValid(false)) continue;
            particle_identities_[particle_identity_count_++] = {
                reinterpret_cast<std::uintptr_t>(particle), index, item->GetSerialNumber()};
        }
        return true;
    }

    void ObserveNewParticleOwner(std::uintptr_t owner)
    {
        auto* object = reinterpret_cast<RC::Unreal::UObject*>(owner);
        if (object->GetClassPrivate()->GetName() != STR("LuxParticleSystemComponent")) return;
        const auto index = object->GetInternalIndex();
        const auto* item = RC::Unreal::FUObjectArray::IndexToObject(index);
        if (!item || item->GetUObject() != object || !item->IsValid(false)) return;
        const auto previous = std::find_if(particle_identities_.begin(),
            particle_identities_.begin() + particle_identity_count_, [&](const auto& row) {
                return row.address == owner && row.index == index;
            });
        const bool existed = previous != particle_identities_.begin() + particle_identity_count_;
        std::uintptr_t particle_template{}, fence{};
        std::uint8_t busy{};
        if (!ReadSchedulerValue(owner + 0x808, particle_template)
            || !ReadSchedulerValue(owner + 0xa70, fence) || !ReadSchedulerValue(owner + 0xa81, busy)) return;
        Output::send<LogLevel::Warning>(STR(
            "[ReplayQualification] particle lifecycle mismatch run_id={} captured_tick={} held_tick={} "
            "owner={} index={} serial={} present_at_capture={} captured_serial={} template={} async_fence={} async_busy={}\n"),
            RC::to_generic_string(request_.run_id), application_target_tick_, application_reentry_tick_, owner,
            index, item->GetSerialNumber(), existed, existed ? previous->serial : -1, particle_template, fence, busy);
        if (!ObserveParticleEmitterOwnership(owner, application_reentry_tick_))
            Output::send<LogLevel::Warning>(STR("[ReplayQualification] particle emitter inventory unavailable run_id={} owner={}\n"),
                RC::to_generic_string(request_.run_id), owner);
        std::vector<RC::Unreal::UObject*> managers;
        RC::Unreal::UObjectGlobals::FindAllOf(L"LuxVFxInstanceManager", managers);
        for (auto* manager : managers)
        {
            const auto address = reinterpret_cast<std::uintptr_t>(manager);
            std::uintptr_t slots{};
            std::int32_t count{}, capacity{}, sequence{};
            if (!ReadSchedulerValue(address + 0x3e8, slots) || !ReadSchedulerValue(address + 0x3f0, count)
                || !ReadSchedulerValue(address + 0x3f4, capacity) || !ReadSchedulerValue(address + 0x3e0, sequence)
                || count < 0 || count > 4096 || capacity < count || (count && !slots)) continue;
            for (std::int32_t ordinal = 0; ordinal < count; ++ordinal)
            {
                const auto record = slots + static_cast<std::size_t>(ordinal) * 0xc0;
                std::uintptr_t component{};
                std::int32_t slot_id{}, mesh_actor_id{}, kind_arg{}, group{};
                std::uint8_t kind{};
                if (!ReadSchedulerValue(record, component) || component != owner
                    || !ReadSchedulerValue(record + 8, slot_id) || !ReadSchedulerValue(record + 0x10, mesh_actor_id)
                    || !ReadSchedulerValue(record + 0x14, kind) || !ReadSchedulerValue(record + 0x18, kind_arg)
                    || !ReadSchedulerValue(record + 0x1c, group)) continue;
                Output::send<LogLevel::Warning>(STR(
                    "[ReplayQualification] particle lifecycle slot run_id={} owner={} manager={} ordinal={} "
                    "slots={} next_slot={} slot_id={} mesh_actor_id={} kind={} kind_arg={} group={}\n"),
                    RC::to_generic_string(request_.run_id), owner, manager->GetFullName(), ordinal,
                    count, sequence, slot_id, mesh_actor_id, kind, kind_arg, group);
            }
        }
    }
    static bool ReadSchedulerTick(std::uintptr_t tick, SchedulerTickObservation& value) noexcept
    {
        value.tick = tick;
        return tick && ReadSchedulerValue(tick + 0x38, value.cooldown)
            && ReadSchedulerValue(tick + 0x3c, value.last_time)
            && ReadSchedulerValue(tick + 0x10, value.admitted)
            && ReadSchedulerValue(tick + 0x14, value.queued);
    }

    bool ProbeHistoricalScheduler(const ReplayHost::InteriorWitness& witness)
    {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        auto* world = const_cast<void*>(witness.world);
        auto& target = application_checkpoint_->scheduler;
        SchedulerTickObservation before{}, recovered{};
        if (!ReadSchedulerTick(application_scheduler_tick_.tick, before)
            || (before.cooldown == application_scheduler_tick_.cooldown
                && before.last_time == application_scheduler_tick_.last_time))
        { Fail("historical_scheduler_witness_did_not_change"); return false; }
        const auto budget = Horse::Deterministic::Schema::replay_checkpoint_memory_budget
            - application_scheduler_prepared_->owned_bytes();
        ReplayScheduler current;
        ReplayScheduler::PreparedRestore prepared;
        ReplayScheduler::PreflightFailure failure{"capture_current", 0};
        // Retained ownership supplies identities only; Capture reads current native
        // values for dormant ticks as well as registered membership.
        auto status = current.Capture(base, world, budget, {}, &target);
        if (status.ok()) status = target.PrepareRestore(current, base, world, budget, prepared, &failure);
        if (!status.ok())
        {
            Output::send<LogLevel::Warning>(STR("[ReplayQualification] historical scheduler preparation failed code={} check={} address={} "
                "target_levels={} current_levels={} target_ticks={} current_ticks={}\n"),
                static_cast<unsigned>(status.code), RC::to_generic_string(failure.check ? failure.check : "unknown"), failure.address,
                target.level_count(), current.level_count(), target.tick_count(), current.tick_count());
            if (failure.check && std::string_view(failure.check) == "new_tick_registration")
            {
                std::uintptr_t vtable{}, owner{};
                if (ReadSchedulerValue(failure.address, vtable) && ReadSchedulerValue(failure.address + 0x50, owner)
                    && owner && (vtable == base + 0x381c720 || vtable == base + 0x3865f98))
                    Output::send<LogLevel::Warning>(STR("[ReplayQualification] new scheduler tick vtable_rva={} owner={}\n"),
                        vtable - base, reinterpret_cast<RC::Unreal::UObject*>(owner)->GetFullName());
                if (owner && vtable == base + 0x3865f98) ObserveNewParticleOwner(owner);
            }
            Fail("historical_scheduler_preparation_failed"); return false;
        }
        struct Query
        {
            ReplayQualificationMod* owner;
            const ReplayHost::InteriorWitness* witness;
            std::uintptr_t base;
            bool observed{};
        } query{this, &witness, base};
        const auto started = std::chrono::steady_clock::now();
        status = target.RestoreBacking(current, base, world, budget, prepared, [](void* opaque) noexcept {
            auto& q = *static_cast<Query*>(opaque);
            const auto& expected = q.owner->application_scheduler_tick_;
            SchedulerTickObservation observed{};
            ReplayTrajectorySample sample{};
            std::uint64_t epoch{};
            const auto admitted = [](std::int32_t stamp, std::uint64_t epoch) {
                return static_cast<std::uint64_t>(static_cast<std::int64_t>(stamp)) == epoch;
            };
            q.observed = ReadSchedulerTick(expected.tick, observed)
                && observed.cooldown == expected.cooldown && observed.last_time == expected.last_time
                && ReadSchedulerValue(q.base + 0x4197170, epoch) && epoch == q.witness->epoch
                && admitted(observed.admitted, epoch) == admitted(expected.admitted, q.owner->application_checkpoint_->epoch)
                && admitted(observed.queued, epoch) == admitted(expected.queued, q.owner->application_checkpoint_->epoch)
                && ReadReplayTrajectory(q.owner->battle_manager_, sample, true) && sample == q.owner->interior_sample_
                && q.owner->boundary_samples_ == q.owner->interior_boundaries_
                && ReadCompletedApplicationTaskState(q.owner->battle_manager_, epoch);
            // This component-only historical image must always be undone.
            return true;
        }, &query);
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        ReplayScheduler::PreparedRestore verify;
        if (status.code != Horse::Deterministic::FailureCode::Cancelled || !query.observed
            || !ReadSchedulerTick(before.tick, recovered) || recovered != before
            || !current.PrepareRestore(current, base, world, budget - prepared.owned_bytes(), verify).ok())
        { Fail("historical_scheduler_install_or_undo_failed"); return false; }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] scheduler historical install cancelled run_id={} captured_tick={} held_tick={} "
            "old_epoch={} current_epoch={} elapsed_us={} changed_before=true target_observed=true current_recovered=true epoch_preserved=true\n"),
            RC::to_generic_string(request_.run_id), application_target_tick_, application_reentry_tick_, application_checkpoint_->epoch, witness.epoch, elapsed);
        return true;
    }

    bool ObserveSchedulerInventory(const void* world)
    {
        // Once per completed-application diagnostic. This is native-state
        // inventory, not a restorable scheduler image or a hot observation loop.
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto world_address = reinterpret_cast<std::uintptr_t>(world);
        std::uintptr_t loaded{}, root{};
        int loaded_count{};
        if (!ReadSchedulerValue(world_address + 0x110, loaded)
            || !ReadSchedulerValue(world_address + 0x118, loaded_count)
            || loaded_count < 0 || loaded_count > 1024 || (loaded_count && !loaded)
            || !ReadSchedulerValue(world_address + 0x778, root) || !root) return false;
        std::map<std::uintptr_t, bool> levels{{root, true}};
        for (int i = 0; i < loaded_count; ++i)
        {
            std::uintptr_t level{}, tick_level{};
            if (!ReadSchedulerValue(loaded + i * 8, level) || !level
                || !ReadSchedulerValue(level + 0x140, tick_level)) return false;
            if (tick_level) levels.emplace(tick_level, true);
        }
        struct Kind { std::uint64_t ticks{}, intervals{}, prerequisites{}, pending_tasks{}; };
        std::map<std::uintptr_t, Kind> kinds;
        std::map<std::uintptr_t, bool> ticks;
        std::vector<std::uintptr_t> prerequisite_targets;
        std::uint64_t active{}, disabled{}, newly_spawned{}, cooldown{};
        const auto observe_tick = [&](std::uintptr_t tick, std::uintptr_t level) {
            if (!tick) return false;
            if (!ticks.emplace(tick, true).second) return true;
            if (ticks.size() > 65536) return false;
            std::uintptr_t vtable{}, task{}, owner{}, prerequisites{};
            int count{}, capacity{};
            float interval{};
            if (!ReadSchedulerValue(tick, vtable) || vtable < base
                || !ReadSchedulerValue(tick + 0x18, task)
                || !ReadSchedulerValue(tick + 0x20, prerequisites)
                || !ReadSchedulerValue(tick + 0x28, count) || count < 0 || count > 4096
                || !ReadSchedulerValue(tick + 0x2c, capacity) || capacity < count || (count && !prerequisites)
                || !ReadSchedulerValue(tick + 0x40, interval)
                || !ReadSchedulerValue(tick + 0x48, owner) || owner != level) return false;
            auto& kind = kinds[vtable - base];
            ++kind.ticks;
            kind.intervals += interval > 0;
            kind.pending_tasks += task != 0;
            kind.prerequisites += count;
            if (static_cast<std::size_t>(count) > 65536 - prerequisite_targets.size()) return false;
            for (int i = 0; i < count; ++i)
            {
                std::uintptr_t target{};
                if (!ReadSchedulerValue(prerequisites + i * 16 + 8, target)) return false;
                prerequisite_targets.push_back(target);
            }
            return true;
        };
        for (const auto& [level, unused] : levels)
        {
            std::uint8_t context_active{};
            int pending{};
            if (!ReadSchedulerValue(level + 0x128, context_active) || context_active
                || !ReadSchedulerValue(level + 0xb8, pending) || pending) return false;
            for (const auto offset : {0x8u, 0x60u, 0xc0u})
            {
                const auto set = level + offset;
                std::uintptr_t data{}, bits{};
                int slots{}, free{}, bit_count{};
                if (!ReadSchedulerValue(set, data) || !ReadSchedulerValue(set + 8, slots)
                    || slots < 0 || slots > 65536 || (slots && !data)
                    || !ReadSchedulerValue(set + 0x20, bits)
                    || !ReadSchedulerValue(set + 0x28, bit_count) || bit_count != slots
                    || (!bits && bit_count > 128)
                    || !ReadSchedulerValue(set + 0x34, free) || free < 0 || free > slots) return false;
                if (!bits) bits = set + 0x10;
                int occupied = 0;
                for (int i = 0; i < slots; ++i)
                {
                    std::uint32_t flags{};
                    if (!ReadSchedulerValue(bits + (i / 32) * 4, flags)) return false;
                    if (!(flags & (1u << (i % 32)))) continue;
                    std::uintptr_t tick{};
                    if (!ReadSchedulerValue(data + i * 16, tick) || !observe_tick(tick, level)) return false;
                    ++occupied;
                }
                if (occupied != slots - free) return false;
                (offset == 8 ? active : offset == 0x60 ? disabled : newly_spawned) += occupied;
            }
            std::uintptr_t tick{};
            if (!ReadSchedulerValue(level + 0x58, tick)) return false;
            std::map<std::uintptr_t, bool> chain;
            while (tick)
            {
                if (!chain.emplace(tick, true).second || chain.size() > 65536 || !observe_tick(tick, level)) return false;
                if (++cooldown > 65536) return false;
                if (!application_scheduler_tick_.tick && !ReadSchedulerTick(tick, application_scheduler_tick_)) return false;
                if (!ReadSchedulerValue(tick + 0x30, tick)) return false;
            }
        }
        // Capture owns dormant trace-mesh primary ticks in addition to native
        // registration membership. Inventory their live arrays independently;
        // never copy the scheduler image's counts into its expected result.
        const auto membership_ticks = ticks.size();
        if (application_checkpoint_ && application_checkpoint_->traces)
        {
            struct Context { const decltype(observe_tick)* observe; const decltype(levels)* levels; };
            Context context{&observe_tick, &levels};
            const auto status = application_checkpoint_->traces->VisitPrimaryTicks(&context,
                [](void* opaque, std::uintptr_t component) {
                    const auto& context = *static_cast<Context*>(opaque);
                    const auto tick = component + 0x110;
                    std::uintptr_t level{}, owner{};
                    return ReadSchedulerValue(tick + 0x48, level) && context.levels->contains(level)
                        && ReadSchedulerValue(tick + 0x50, owner) && owner == component
                        && (*context.observe)(tick, level);
                });
            if (!status.ok()) return false;
        }
        std::uint64_t outside = 0, pending = 0;
        for (auto target : prerequisite_targets) outside += !ticks.contains(target);
        for (const auto& [rva, kind] : kinds)
        {
            pending += kind.pending_tasks;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] scheduler kind run_id={} vtable_rva={} ticks={} interval_ticks={} prerequisites={} pending_tasks={}\n"),
                RC::to_generic_string(request_.run_id), rva, kind.ticks, kind.intervals, kind.prerequisites, kind.pending_tasks);
        }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] scheduler inventory run_id={} levels={} ticks={} kinds={} active={} disabled={} "
            "cooldown={} newly_spawned={} prerequisites={} outside_prerequisites={} pending_tasks={} retained_only={}\n"),
            RC::to_generic_string(request_.run_id), levels.size(), ticks.size(), kinds.size(), active, disabled,
            cooldown, newly_spawned, prerequisite_targets.size(), outside, pending, ticks.size() - membership_ticks);
        scheduler_inventory_counts_ = {levels.size(), ticks.size(), prerequisite_targets.size()};
        return !ticks.empty();
    }

    bool ObserveApplicationPause(const ReplayHost::InteriorWitness& witness)
    {
        if (state_ == State::Failed) return false;
        if (request_.probe_historical_restore) return ObserveHistoricalCombatRestore(witness);
        const auto target = application_reentry_armed_ ? application_reentry_tick_ : application_target_tick_;
        ReplayTrajectorySample sample{};
        if ((witness.phase != ReplayHost::InteriorPhase::Holding && witness.phase != ReplayHost::InteriorPhase::Releasing)
            || witness.boundary != ReplayHost::PauseBoundary::CompletedApplication || witness.tick != target
            || witness.pending_task || !witness.application_idle || !witness.engine_idle
            || !witness.world_idle || !witness.arena_empty
            || !ReadReplayTrajectory(battle_manager_, sample, true) || sample.frame != target
            || !sample.source_active || sample.manager_phase != 2 || sample.round_state != 2 || sample.world_mode != 2
            || !ReadCompletedApplicationTaskState(battle_manager_, witness.epoch))
        { Fail("application_pause_boundary_invalid"); return false; }
        if (!interior_started_)
        {
            interior_started_ = true;
            interior_sample_ = sample;
            interior_epoch_ = witness.epoch;
            application_hold_completed_ = witness.completed_applications;
            interior_boundaries_ = boundary_samples_;
            interior_surface_start_ = witness.surface_frames;
            interior_started_at_ = std::chrono::steady_clock::now();
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] application pause started run_id={} native_frame={} epoch={} applications={} task_retired=true\n"),
                RC::to_generic_string(request_.run_id), target, witness.epoch, witness.completed_applications);
        }
        if (sample != interior_sample_ || boundary_samples_ != interior_boundaries_
            || witness.epoch != interior_epoch_ || witness.completed_applications != application_hold_completed_)
        { Fail("application_pause_state_advanced"); return false; }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - interior_started_at_).count();
        const auto frames = witness.surface_frames - interior_surface_start_;
        if (elapsed < 1'000'000) return false;
        // Release intentionally frees the surface before the next owner-thread
        // observation. Its retained frame count still proves the held updates;
        // only the pre-release hold requires live surface storage.
        if (frames < 30 || witness.application_updates < 30
            || (!interior_release_requested_ && !witness.surface_bytes))
        { Fail("application_pause_surface_stalled"); return false; }
        if (request_.probe_particle_copy) return ObserveParticleCopyHold(witness, target);
        if (!interior_release_requested_)
        {
            if (witness.surface_pending) return false;
            if (application_reentry_armed_)
            {
                if (!ValidateCapturedSchedulerBindings(witness)) return false;
                const auto restore = ResolveHorseModExport<std::uint16_t (*)(const ReplayHost::Checkpoint*)>(
                    "horsemod_restore_replay_checkpoint");
                if (!application_checkpoint_ || application_checkpoint_->epoch >= witness.epoch
                    || FingerprintCheckpointStorage(*application_checkpoint_) != application_checkpoint_fingerprint_
                    || !restore || restore(application_checkpoint_.get()) != static_cast<std::uint16_t>(
                        Horse::Deterministic::FailureCode::UnsupportedContent)
                    || !ReadReplayTrajectory(battle_manager_, sample, true) || sample != interior_sample_
                    || boundary_samples_ != interior_boundaries_
                    || FingerprintCheckpointStorage(*application_checkpoint_) != application_checkpoint_fingerprint_)
                { Fail("application_historical_rejection_changed_state"); return false; }
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] application historical restore rejected run_id={} captured_tick={} held_tick={} "
                    "old_epoch={} current_epoch={} unchanged=true\n"), RC::to_generic_string(request_.run_id),
                    application_target_tick_, application_reentry_tick_, application_checkpoint_->epoch, witness.epoch);
                if (!ProbeHistoricalScheduler(witness)) return false;
            }
            else
            {
            if (!ReadPhysicsSceneReference(witness.world, application_scene_reference_))
            { Fail("application_scene_reference_unavailable"); return false; }
            if (!CaptureParticleIdentities())
            { Fail("particle_identity_inventory_capacity"); return false; }
            const auto capture = ResolveHorseModExport<std::uint16_t (*)(ReplayHost::Checkpoint*)>("horsemod_capture_replay_checkpoint");
            application_checkpoint_ = std::make_shared<ReplayHost::Checkpoint>();
            const auto code = capture ? capture(application_checkpoint_.get()) : 65535;
            if (code || !application_checkpoint_->valid || application_checkpoint_->task || application_checkpoint_->event
                || application_checkpoint_->execution.phase != Horse::Deterministic::Sc6ReplayExecutor::Phase::Idle
                || application_checkpoint_->execution.tick != application_target_tick_ || application_checkpoint_->gameplay.bytes.empty()
                || !ReadReplayTrajectory(battle_manager_, sample, true) || sample != interior_sample_
                || boundary_samples_ != interior_boundaries_)
            {
                Output::send<LogLevel::Warning>(STR("[ReplayQualification] application checkpoint capture failed code={}\n"), code);
                Fail("application_checkpoint_capture_failed"); return false;
            }
            application_checkpoint_fingerprint_ = FingerprintCheckpointStorage(*application_checkpoint_);
            std::uintptr_t vfx_manager{};
            std::int32_t particles{}, debris{};
            auto& vfx = application_checkpoint_->vfx;
            if (!ReadSchedulerValue(reinterpret_cast<std::uintptr_t>(battle_manager_) + 0x508, vfx_manager)
                || !ReadSchedulerValue(vfx_manager + 0x3f0, particles) || !ReadSchedulerValue(vfx_manager + 0x400, debris)
                || particles < 0 || debris < 0 || vfx.slot_count() != static_cast<std::size_t>(particles) + debris
                || vfx.Capture(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)), battle_manager_, 0).code
                    != Horse::Deterministic::FailureCode::CapacityExceeded
                || FingerprintCheckpointStorage(*application_checkpoint_) != application_checkpoint_fingerprint_)
            { Fail("vfx_request_capture_or_rejection_invalid"); return false; }
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] vfx requests captured run_id={} native_frame={} particles={} debris={} bytes={} "
                "independent_storage=true zero_budget_rejected=true\n"),
                RC::to_generic_string(request_.run_id), target, particles, debris, vfx.owned_bytes());
            auto& scheduler = application_checkpoint_->scheduler;
            const auto scheduler_bytes = scheduler.owned_bytes();
            if (!ObserveSchedulerInventory(witness.world))
            { Fail("application_scheduler_inventory_invalid"); return false; }
            if (scheduler.level_count() != scheduler_inventory_counts_[0]
                || scheduler.tick_count() != scheduler_inventory_counts_[1]
                || scheduler.prerequisite_count() != scheduler_inventory_counts_[2]
                || !scheduler_bytes)
            {
                Output::send<LogLevel::Warning>(STR(
                    "[ReplayQualification] scheduler inventory mismatch actual={}/{}/{} expected={}/{}/{} bytes={}\n"),
                    scheduler.level_count(), scheduler.tick_count(), scheduler.prerequisite_count(),
                    scheduler_inventory_counts_[0], scheduler_inventory_counts_[1], scheduler_inventory_counts_[2], scheduler_bytes);
                Fail("application_scheduler_capture_inventory_mismatch"); return false;
            }
            const auto rejected = scheduler.Capture(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),
                const_cast<void*>(witness.world), 0);
            if (rejected.code != Horse::Deterministic::FailureCode::CapacityExceeded
                || scheduler.owned_bytes() != scheduler_bytes
                || FingerprintCheckpointStorage(*application_checkpoint_) != application_checkpoint_fingerprint_)
            { Fail("application_scheduler_rejected_capture_changed_storage"); return false; }
            if (!ValidateCapturedSchedulerBindings(witness)) return false;
            if (!PrepareCapturedScheduler(witness)) return false;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] scheduler captured run_id={} levels={} ticks={} prerequisites={} bytes={} zero_budget_rejected=true\n"),
                RC::to_generic_string(request_.run_id), scheduler.level_count(), scheduler.tick_count(),
                scheduler.prerequisite_count(), scheduler_bytes);
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] application checkpoint captured run_id={} native_frame={} bytes={} timers={} borrowed_task=false\n"),
                RC::to_generic_string(request_.run_id), application_target_tick_, application_checkpoint_->gameplay.bytes.size(),
                application_checkpoint_->world.timer_count());
            }
            if(request_.probe_consumer_task && !application_reentry_armed_) {
                const auto arm=ResolveHorseModExport<bool (*)(const ReplayHost::Checkpoint*,std::uint64_t,std::uint32_t)>(
                    "horsemod_arm_consumer_task_probe");
                if(!arm || !arm(application_checkpoint_.get(),target+1,500)) {
                    Fail("consumer_task_probe_admission_failed");return false;
                }
            }
            interior_release_requested_ = true;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] application pause held run_id={} native_frame={} epoch={} elapsed_us={} "
                "application_updates={} surface_frames={} surface_bytes={} applications={} unchanged=true task_retired=true\n"),
                RC::to_generic_string(request_.run_id), target, witness.epoch, elapsed, witness.application_updates,
                frames, witness.surface_bytes, witness.completed_applications);
        }
        return true;
    }

    bool UndoPreparedEmitters() noexcept
    {
        if (particle_pool_restore_ && particle_pool_restore_->published()
            && (!particle_vfx_image_ || !particle_vfx_undo_
                || !particle_vfx_image_->UndoTilePools(*particle_vfx_undo_, *particle_pool_restore_).ok())) return false;
        if (particle_gpu_historical_set_ && !particle_gpu_historical_set_->Undo().ok()) return false;
        if (particle_cpu_historical_set_ && !particle_cpu_historical_set_->Undo().ok()) return false;
        if (!particle_cpu_prepared_ || !particle_cpu_prepared_->published()) return true;
        if (!particle_cpu_prepared_->UndoPublication().ok())
            return false;
        particle_cpu_prepared_.reset();
        return true;
    }

    void RetireCpuFailure(std::string_view reason)
    {
        if (!UndoPreparedEmitters()) { Fail(reason); return; }
        // Keep the application hold until the copy owner retires/releases its
        // resources. Only then resume the unchanged native state and fail.
        particle_copy_failed_ = true;
        Output::send<LogLevel::Warning>(STR("[ReplayQualification] cpu replacement rejected reason={} retirement_required=true\n"),
            RC::to_generic_string(std::string(reason)));
    }

    std::size_t ParticleLeaseBytes() const noexcept
    {
        std::size_t bytes{};
        for (const auto& lease : particle_object_leases_) bytes += lease.owned_bytes();
        if (particle_cpu_historical_set_) bytes += particle_cpu_historical_set_->owned_bytes();
        if (particle_gpu_historical_set_) bytes += particle_gpu_historical_set_->owned_bytes();
        if (particle_vfx_undo_) bytes += sizeof(*particle_vfx_undo_) + particle_vfx_undo_->owned_bytes();
        if (particle_pool_restore_) bytes += particle_pool_restore_->owned_bytes();
        return bytes;
    }

    bool RetireParticleObjectLeases()
    {
        const bool retained = (particle_vfx_image_ && particle_vfx_image_->owners_retained())
            || (particle_vfx_undo_ && particle_vfx_undo_->owners_retained())
            || std::any_of(particle_object_leases_.begin(), particle_object_leases_.end(),
                [](const auto& lease) { return lease.registered(); });
        if (!retained) return true;
        // CPU undo and GPU retirement precede object-owner release. Failure
        // is not cancellation of an in-flight native/resource consumer.
        if ((particle_cpu_prepared_ && particle_cpu_prepared_->published())
            || (particle_cpu_historical_set_ && particle_cpu_historical_set_->published())
            || (particle_gpu_historical_set_ && particle_gpu_historical_set_->published())
            || (particle_pool_restore_ && particle_pool_restore_->published())) return false;
        if (particle_copy_started_ || particle_vfx_image_)
        {
            using Copy = Horse::Deterministic::Sc6ReplayParticleCopy;
            using Action = ReplayHost::ParticleCopyAction;
            const auto control = ResolveHorseModExport<bool (*)(Action, Copy::Witness*, bool*)>("horsemod_particle_copy_experiment");
            Copy::Witness copy{}; bool pending{};
            if (!control || !control(Action::Read, &copy, &pending) || pending
                || copy.pending || copy.native_write_uncommitted || copy.phase != Copy::Phase::Released) return false;
        }
        particle_cpu_historical_set_.reset();
        particle_gpu_historical_set_.reset();
        particle_pool_restore_.reset();
        particle_birth_ = {};
        if (particle_vfx_undo_ && !particle_vfx_undo_->ReleaseOwners().ok()) return false;
        particle_vfx_undo_.reset();
        if (particle_vfx_image_ && !particle_vfx_image_->ReleaseOwners().ok()) return false;
        for (auto& lease : particle_object_leases_)
            if (!lease.Release().ok()) return false;
        return true;
    }

    bool ObserveSharedParticleOwners(std::uintptr_t pool, std::uint64_t tick, std::size_t budget)
    {
        // One bounded identity scan at each held boundary, not a tick observer
        // or checkpoint archive. Combat VFX slots do not cover the shared pool.
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        std::vector<RC::Unreal::UObject*> components;
        RC::Unreal::UObjectGlobals::FindAllOf(L"ParticleSystemComponent", components);
        if (components.size() > 4096) return false;
        std::vector<void*> retained_objects;
        retained_objects.reserve(4096);
        const auto retain = [&](std::uintptr_t address) {
            if (!address) return true;
            if (retained_objects.size() == retained_objects.capacity()) return false;
            retained_objects.push_back(reinterpret_cast<void*>(address));
            return true;
        };
        std::array<std::uint64_t, 1024> allocated{};
        std::size_t owned_tiles{}, owners{};
        for (auto* component : components)
        {
            const auto* item = RC::Unreal::FUObjectArray::IndexToObject(component->GetInternalIndex());
            if (!item || item->GetUObject() != component || !item->IsValid(false)) continue;
            const auto address = reinterpret_cast<std::uintptr_t>(component);
            std::uintptr_t emitters{}; int count{}, capacity{};
            if (!ReadSchedulerValue(address + 0xa50, emitters) || !ReadSchedulerValue(address + 0xa58, count)
                || !ReadSchedulerValue(address + 0xa5c, capacity) || count < 0 || capacity < count || count > 128
                || (count && !emitters)) return false;
            std::size_t component_tiles{}, component_emitters{};
            for (int ordinal = 0; ordinal < count; ++ordinal)
            {
                std::uintptr_t emitter{}, vtable{}, system{}, owner_pool{}, tiles{}; int tile_count{};
                if (!ReadSchedulerValue(emitters + ordinal * 8, emitter)) return false;
                if (!emitter) continue;
                if (!ReadSchedulerValue(emitter, vtable)) return false;
                if (vtable != base + 0x394c100) continue;
                if (!ReadSchedulerValue(emitter + 0x1d0, system) || !system
                    || !ReadSchedulerValue(system + 0x78, owner_pool)) return false;
                if (owner_pool != pool) continue;
                if (!ReadSchedulerValue(emitter + 0x1e8, tiles) || !ReadSchedulerValue(emitter + 0x1f0, tile_count)
                    || tile_count < 0 || tile_count > 65536 || (tile_count && !tiles)) return false;
                for (int i = 0; i < tile_count; ++i)
                {
                    std::uint32_t tile{};
                    if (!ReadSchedulerValue(tiles + i * 4, tile) || tile >= 65536) return false;
                    const auto mask = std::uint64_t{1} << (tile % 64);
                    if (allocated[tile / 64] & mask) return false;
                    allocated[tile / 64] |= mask;
                }
                component_tiles += tile_count; ++component_emitters;
            }
            if (!component_emitters) continue;
            // Retain every actual shared-pool component, including stage snow,
            // plus the audited UObject bindings of its live emitters. GPU
            // extension +1D8 is not assumed to be a UObject.
            if (!retain(address)) return false;
            std::uintptr_t particle_template{};
            if (!ReadSchedulerValue(address + 0x808, particle_template) || !retain(particle_template)) return false;
            for (int ordinal = 0; ordinal < count; ++ordinal)
            {
                std::uintptr_t emitter{}, vtable{};
                if (!ReadSchedulerValue(emitters + ordinal * 8, emitter)) return false;
                if (!emitter) continue;
                if (!ReadSchedulerValue(emitter, vtable)
                    || (vtable != base + 0x3949b60 && vtable != base + 0x394c100)) return false;
                for (const auto offset : {0x10, 0x28, 0x1a0})
                {
                    std::uintptr_t binding{};
                    if (!ReadSchedulerValue(emitter + offset, binding) || !retain(binding)) return false;
                }
            }
            owned_tiles += component_tiles; ++owners;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] shared particle owner run_id={} tick={} component={} emitters={} tiles={} object={}\n"),
                RC::to_generic_string(request_.run_id), tick, address, component_emitters, component_tiles, component->GetFullName());
        }
        std::size_t free_count{};
        const auto partition = Horse::Deterministic::Sc6ReplayVfxState::ValidateNativeTilePartition(pool, allocated, free_count);
        Output::send<LogLevel::Default>(STR("[ReplayQualification] shared particle partition run_id={} tick={} owners={} allocated={} free={} complete={} disjoint={}\n"),
            RC::to_generic_string(request_.run_id), tick, owners, owned_tiles, free_count,
            owned_tiles + free_count == 65536, partition.ok());
        // Allocation membership does not prove render registration or retained
        // component state. Those remain separate transaction preconditions.
        if (!partition.ok() || owned_tiles + free_count != 65536) return false;
        const auto used = ParticleLeaseBytes() + (particle_vfx_image_ ? particle_vfx_image_->owned_bytes() : 0)
            + (components.capacity() + retained_objects.capacity()) * sizeof(void*) + 2 * sizeof(allocated);
        if (used > budget) return false;
        auto& lease = particle_object_leases_[tick == 205 ? 0 : 1];
        const auto acquired = lease.Acquire(base, retained_objects, budget - used);
        Output::send<LogLevel::Default>(STR("[ReplayQualification] particle object lease run_id={} tick={} objects={} bytes={} acquired={} code={}\n"),
            RC::to_generic_string(request_.run_id), tick, lease.object_count(), lease.owned_bytes(), acquired.ok(), static_cast<unsigned>(acquired.code));
        if (!acquired.ok()) return false;
        for (const auto& active : particle_object_leases_)
            if (active.registered() && !active.Validate().ok()) return false;
        return true;
    }

    bool ObserveCpuReplacement(const ReplayHost::InteriorWitness& held, std::size_t gpu_bytes)
    {
        using Vfx = Horse::Deterministic::Sc6ReplayVfxState;
        using Prepared = Horse::Deterministic::Sc6ReplayCpuEmitterState::Prepared;
        if (particle_cpu_undone_) return true;
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto total = Horse::Deterministic::Schema::replay_checkpoint_memory_budget;
        if (gpu_bytes > total || held.surface_bytes > total - gpu_bytes
            || ParticleLeaseBytes() > total - gpu_bytes - held.surface_bytes) { RetireCpuFailure("cpu_replacement_budget"); return false; }
        const auto budget = total - gpu_bytes - held.surface_bytes - ParticleLeaseBytes();
        if (!particle_vfx_image_)
        {
            particle_vfx_image_ = std::make_unique<Vfx>();
            auto status = Horse::Deterministic::CaptureReplayVfx(*particle_vfx_image_, base, battle_manager_, budget);
            particle_cpu_prepared_ = std::make_unique<Prepared>();
            void* component{}; std::size_t ordinal{};
            if (status.ok()) status = particle_vfx_image_->PrepareCpuEmitter(0, budget, component, ordinal, *particle_cpu_prepared_);
            if (!status.ok())
            {
                Output::send<LogLevel::Warning>(STR("[ReplayQualification] cpu replacement preparation failed code={} emitters={}\n"),
                    static_cast<unsigned>(status.code), particle_vfx_image_->cpu_emitter_count());
                RetireCpuFailure("cpu_replacement_preparation_failed"); return false;
            }
            std::uintptr_t slots{}; int count{};
            if (!ReadSchedulerValue(reinterpret_cast<std::uintptr_t>(component) + 0xa50, slots)
                || !ReadSchedulerValue(reinterpret_cast<std::uintptr_t>(component) + 0xa58, count)
                || count < 0 || ordinal >= static_cast<std::size_t>(count)
                || !ReadSchedulerValue(slots + ordinal * 8, particle_cpu_original_) || !particle_cpu_original_)
            { RetireCpuFailure("cpu_replacement_slot_invalid"); return false; }
            particle_cpu_component_ = component; particle_cpu_ordinal_ = ordinal;
            particle_cpu_fingerprint_ = particle_vfx_image_->storage_fingerprint();
            if (!particle_cpu_prepared_->Publish(ordinal, particle_cpu_original_).ok())
            { RetireCpuFailure("cpu_replacement_publication_rejected"); return false; }
            particle_cpu_started_ = std::chrono::steady_clock::now();
            particle_cpu_updates_ = held.application_updates; particle_cpu_frames_ = held.surface_frames;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] cpu replacement published run_id={} tick=205 original={} replacement={} component={}\n"),
                RC::to_generic_string(request_.run_id), reinterpret_cast<std::uintptr_t>(particle_cpu_original_),
                reinterpret_cast<std::uintptr_t>(particle_cpu_prepared_->get()), reinterpret_cast<std::uintptr_t>(component));
            return false;
        }
        if (!particle_cpu_prepared_ || !particle_cpu_prepared_->ValidatePublished().ok())
        { Fail("cpu_replacement_held_binding_changed"); return false; }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - particle_cpu_started_).count();
        if (elapsed < 250000 || held.application_updates - particle_cpu_updates_ < 30 || held.surface_frames - particle_cpu_frames_ < 10)
            return false;
        const auto bytes = particle_vfx_image_->owned_bytes() + particle_cpu_prepared_->owned_bytes();
        if (!UndoPreparedEmitters() || !particle_vfx_image_->ValidateHeld(base, battle_manager_).ok()
            || particle_vfx_image_->storage_fingerprint() != particle_cpu_fingerprint_)
        { Fail("cpu_replacement_undo_failed"); return false; }
        particle_cpu_undone_ = true;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] cpu replacement undone run_id={} tick=205 elapsed_us={} updates={} surface_frames={} bytes={} original_restored=true\n"),
            RC::to_generic_string(request_.run_id), elapsed, held.application_updates - particle_cpu_updates_,
            held.surface_frames - particle_cpu_frames_, bytes);
        return true;
    }

    bool PrepareHistoricalEmitters(std::size_t budget)
    {
        if (particle_cpu_historical_prepared_) return true;
        using Set = Horse::Deterministic::Sc6ReplayVfxState::PreparedEmitterSet;
        auto prepared_set = std::make_unique<Set>();
        const auto status = particle_vfx_image_->PrepareCpuEmitters(budget, *prepared_set);
        const auto* prepared = prepared_set->replacement(0);
        void* component = prepared_set->component(0);
        const auto ordinal = prepared_set->ordinal(0);
        void* now{};
        std::uintptr_t current_slots{}; int current_count{};
        if (!status.ok() || component != particle_cpu_component_ || (!prepared || !prepared->get())
            || ordinal != particle_cpu_ordinal_
            || !ReadSchedulerValue(reinterpret_cast<std::uintptr_t>(component) + 0xa50, current_slots) || !current_slots
            || !ReadSchedulerValue(reinterpret_cast<std::uintptr_t>(component) + 0xa58, current_count)
            || current_count < 0 || ordinal >= static_cast<std::size_t>(current_count)
            || !ReadSchedulerValue(current_slots + ordinal * sizeof(void*), now) || now != nullptr
            || particle_vfx_image_->storage_fingerprint() != particle_cpu_fingerprint_)
        {
            Output::send<LogLevel::Warning>(STR("[ReplayQualification] historical cpu preparation failed code={} original_retired={}\n"),
                static_cast<unsigned>(status.code), now == nullptr);
            RetireCpuFailure("historical_cpu_replacement_preparation_failed"); return false;
        }
        // Do not dereference the retired address. Observe reuse through B's
        // currently published new component and emitter array. Reuse is an
        // observation, not a required allocator choice or lifetime predicate.
        std::uintptr_t manager{}, slots{}, added{}, emitters{}; void* live_b_emitter{};
        if (!ReadSchedulerValue(reinterpret_cast<std::uintptr_t>(battle_manager_) + 0x508, manager)
            || !ReadSchedulerValue(manager + 0x3e8, slots) || !ReadSchedulerValue(slots + 7 * 0xc0, added)
            || !ReadSchedulerValue(added + 0xa50, emitters) || !ReadSchedulerValue(emitters, live_b_emitter)
            || !live_b_emitter || prepared->get() == live_b_emitter)
        { RetireCpuFailure("historical_cpu_preparation_live_binding_invalid"); return false; }
        if (prepared_set->owned_bytes() > budget)
        { RetireCpuFailure("historical_emitter_preparation_budget"); return false; }
        auto gpu_set = std::make_unique<Set>();
        const auto gpu_status = particle_vfx_image_->PrepareGpuEmitters(budget - prepared_set->owned_bytes(), *gpu_set);
        if (!gpu_status.ok())
        {
            Output::send<LogLevel::Warning>(STR("[ReplayQualification] historical gpu storage preparation failed code={}\n"),
                static_cast<unsigned>(gpu_status.code));
            RetireCpuFailure("historical_gpu_storage_preparation_failed"); return false;
        }
        using Vfx = Horse::Deterministic::Sc6ReplayVfxState;
        const auto prepared_bytes = prepared_set->owned_bytes() + gpu_set->owned_bytes();
        const auto before_undo = particle_vfx_image_->owned_bytes() + prepared_bytes + sizeof(Vfx);
        if (before_undo > budget)
        { RetireCpuFailure("historical_particle_undo_budget"); return false; }
        auto vfx_undo = std::make_unique<Vfx>();
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        auto pool_status = Horse::Deterministic::CaptureReplayVfx(*vfx_undo, base, battle_manager_, budget - before_undo);
        auto pools = std::make_unique<Vfx::PreparedTilePools>();
        if (pool_status.ok()) pool_status = particle_vfx_image_->PrepareTilePools(
            *vfx_undo, budget - prepared_bytes - sizeof(Vfx), *pools);
        Vfx::ParticleBirth birth;
        if (pool_status.ok()) pool_status = particle_vfx_image_->PrepareParticleBirth(*vfx_undo, battle_manager_, birth);
        if (!pool_status.ok())
        {
            Output::send<LogLevel::Warning>(STR("[ReplayQualification] historical pool preparation failed code={}\n"),
                static_cast<unsigned>(pool_status.code));
            RetireCpuFailure("historical_pool_preparation_failed"); return false;
        }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] historical pool undo prepared run_id={} captured_tick=205 held_tick=210 bytes={} "
            "current_owner_bytes={} publication=false\n"),
            RC::to_generic_string(request_.run_id), pools->owned_bytes(), vfx_undo->owned_bytes());
        particle_vfx_undo_ = std::move(vfx_undo);
        particle_birth_ = birth;
        particle_pool_restore_ = std::move(pools);
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] historical gpu storage prepared run_id={} captured_tick=205 held_tick=210 emitters={} bytes={} render_publication=false\n"),
            RC::to_generic_string(request_.run_id), gpu_set->size(), gpu_set->owned_bytes());
        particle_gpu_historical_set_ = std::move(gpu_set);
        particle_cpu_historical_prepared_ = true;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] historical cpu replacement prepared run_id={} captured_tick=205 held_tick=210 "
            "original_retired=true address_reused={} independent_storage=true historical_publication=false bytes={} emitter_count={} retained_for_transaction=true\n"),
            RC::to_generic_string(request_.run_id), live_b_emitter == particle_cpu_original_, prepared_set->owned_bytes(), prepared_set->size());
        particle_cpu_historical_set_ = std::move(prepared_set);
        return true;
    }

    bool HasSecondCheckpoint() const {return request_.checkpoint_pair || request_.checkpoint_fallback;}
    bool UsesOwnedHistoricalAdvance() const {return HasSecondCheckpoint() || (request_.host_seek
        && request_.historical_exact_advance && request_.historical_anchor_tick>220);}
    std::uint32_t SecondCheckpointTick() const {return request_.checkpoint_fallback ? 205 : 206;}

    bool ObservePairOriginalBoundary(const ReplayHost::InteriorWitness& held)
    {
        if(held.surface_pending) return false;
        if((!HasSecondCheckpoint() || held.tick!=SecondCheckpointTick()) && held.tick!=request_.historical_advanced_tick) {Fail("checkpoint_pair_original_tick_changed");return false;}
        if(held.boundary==ReplayHost::PauseBoundary::SimulationTick) {
            const auto complete=ResolveHorseModExport<bool (*)(void*,ReplayHost::InteriorObserver,ReplayHost::TickAdvanceWitness*)>("horsemod_complete_replay_application");
            ReplayHost::TickAdvanceWitness state{};
            if(!complete || !complete(this,[](void* context,const ReplayHost::InteriorWitness& pause) {
                return static_cast<ReplayQualificationMod*>(context)->ObservePairOriginalBoundary(pause);
            },&state) || state.phase!=ReplayHost::TickAdvancePhase::Settling)
            {Fail("checkpoint_pair_original_completion_rejected");return false;}
            return false;
        }
        if(HasSecondCheckpoint() && held.tick==SecondCheckpointTick()) return ObserveSecondCheckpoint(held);
        if(!pair_B_armed_) {
            if(!held.application_idle || !held.engine_idle || !held.world_idle || held.pending_task)
            {Fail("checkpoint_pair_original_tail_incomplete");return false;}
            pair_B_armed_=true;particle_copy_started_=interior_started_=interior_release_requested_=false;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] checkpoint pair original held run_id={} tick={} application_idle=true pending_task=false\n"),RC::to_generic_string(request_.run_id),request_.historical_advanced_tick);
        }
        return ObserveHistoricalCombatRestore(held);
    }

    bool ObserveSecondCheckpoint(const ReplayHost::InteriorWitness& held)
    {
        if(held.surface_pending) return false;
        ReplayTrajectorySample sample{};
        if(!held.application_idle || !held.engine_idle || !held.world_idle || !held.arena_empty || held.pending_task
            || !combat_checkpoint_ || !ReadReplayTrajectory(battle_manager_,sample,true) || sample.frame!=SecondCheckpointTick()
            || sample.manager_phase!=2 || sample.round_state!=2)
        {Fail("checkpoint_pair_boundary_invalid");return false;}
        const auto capture=ResolveHorseModExport<bool (*)(ReplayHost::CaptureAction,ReplayHost::CaptureWitness*,ReplayHost::CheckpointHandle*)>("horsemod_replay_capture_operation");
        ReplayHost::CaptureWitness state{};
        if(!pair_capture_started_) {
            pair_sample_=sample;pair_capture_fingerprint_=FingerprintCheckpointStorage(*combat_checkpoint_);
            interior_epoch_=held.epoch;application_hold_completed_=held.completed_applications;interior_boundaries_=boundary_samples_;
            if(!capture || !capture(ReplayHost::CaptureAction::Begin,&state,nullptr))
            {Fail("checkpoint_pair_capture_rejected");return false;}
            pair_capture_started_=true;return false;
        }
        if(sample!=pair_sample_ || held.epoch!=interior_epoch_ || held.completed_applications!=application_hold_completed_
            || boundary_samples_!=interior_boundaries_ || FingerprintCheckpointStorage(*combat_checkpoint_)!=pair_capture_fingerprint_)
        {Fail("checkpoint_pair_capture_changed_retained_state");return false;}
        if(pair_checkpoint_) return false; // The owned advance already carries the resume request.
        if(!capture || !capture(ReplayHost::CaptureAction::Read,&state,nullptr)
            || state.phase==ReplayHost::CapturePhase::Failed || state.phase==ReplayHost::CapturePhase::Cancelled)
        {Fail("checkpoint_pair_capture_failed");return false;}
        if(state.phase!=ReplayHost::CapturePhase::Ready) return false;
        const auto ready=state;
        if(!capture(ReplayHost::CaptureAction::Take,&state,&pair_checkpoint_) || !pair_checkpoint_ || !pair_checkpoint_->valid
            || pair_checkpoint_->execution.tick!=SecondCheckpointTick() || pair_checkpoint_->gpu==combat_checkpoint_->gpu)
        {Fail("checkpoint_pair_handle_invalid");return false;}
        LogTrajectorySample(sample,1,L"historical_second_target");
        Output::send<LogLevel::Default>(STR("[ReplayQualification] checkpoint pair captured run_id={} first={} second={} distinct_gpu_owners=true first_storage_unchanged=true elapsed_us={} owned_bytes={}\n"),
            RC::to_generic_string(request_.run_id),request_.historical_anchor_tick,SecondCheckpointTick(),ready.elapsed_us,ready.owned_bytes);
        const auto advance=ResolveHorseModExport<bool (*)(std::uint64_t,void*,ReplayHost::InteriorObserver,ReplayHost::TickAdvanceWitness*)>("horsemod_request_replay_tick");
        ReplayHost::TickAdvanceWitness advancing{};
        if(!advance || !advance(request_.historical_advanced_tick,this,[](void* context,const ReplayHost::InteriorWitness& pause) {
            return static_cast<ReplayQualificationMod*>(context)->ObservePairOriginalBoundary(pause);
        },&advancing) || advancing.phase!=ReplayHost::TickAdvancePhase::Releasing)
        {Fail("checkpoint_pair_owned_advance_rejected");return false;}
        Output::send<LogLevel::Default>(STR("[ReplayQualification] checkpoint pair advance requested run_id={} origin={} target={} host_owned=true\n"),RC::to_generic_string(request_.run_id),SecondCheckpointTick(),request_.historical_advanced_tick);
        interior_release_requested_=true;return false;
    }

    #include "ReplayRolling.inl"

    bool ObserveHistoricalCombatRestore(const ReplayHost::InteriorWitness& held)
    {
        if(HasSecondCheckpoint() && held.tick==SecondCheckpointTick() && !pair_B_armed_) return ObserveSecondCheckpoint(held);
        // A host-owned advance can still deliver the outgoing held callback
        // while its surface release completes. It is not the original B hold.
        if(UsesOwnedHistoricalAdvance() && application_reentry_armed_ && !pair_B_armed_) return false;
        const bool external_gpu_diagnostic=request_.pixel_diagnostics && !request_.host_seek;
        const bool reuse=request_.restore_reuse && !restore_reuse_completed_;
        using Copy = Horse::Deterministic::Sc6ReplayParticleCopy;
        using Action = ReplayHost::ParticleCopyAction;
        using Operation = ReplayHost::RestoreOperationAction;
        using Phase = ReplayHost::RestoreOperationPhase;
        const auto control = ResolveHorseModExport<bool (*)(Action, Copy::Witness*, bool*)>("horsemod_particle_copy_experiment");
        const auto restore = ResolveHorseModExport<bool (*)(Operation, const ReplayHost::Checkpoint*, ReplayHost::RestoreOperationWitness*)>(
            "horsemod_replay_restore_operation");
        const auto capture_control=ResolveHorseModExport<bool (*)(ReplayHost::CaptureAction,ReplayHost::CaptureWitness*,ReplayHost::CheckpointHandle*)>("horsemod_replay_capture_operation");
        ReplayHost::CaptureWitness capture_state{};
        ReplayTrajectorySample sample{};
        if (!control || !restore || held.pending_task || !held.application_idle || !held.engine_idle
            || !held.world_idle || !held.arena_empty || application_target_tick_ != request_.historical_anchor_tick || application_reentry_tick_ != request_.historical_advanced_tick
            || !ReadReplayTrajectory(battle_manager_, sample, true) || sample.manager_phase != 2
            || (request_.historical_anchor_tick==0 && !application_reentry_armed_
                ? (sample.frame!=0 || sample.round_state!=0 || sample.world_mode!=0 || sample.source_active)
                : sample.round_state!=2))
        { Fail("historical_combat_boundary_invalid"); return false; }
        Copy::Witness copy{}; bool pending{};
        if (!control(Action::Read, &copy, &pending)) particle_copy_failed_ = true;
        const auto release_captured_a=[&]() -> bool {
            if(request_.rolling_cycles)return BeginRollingCampaign();
            LogTrajectorySample(sample, 1, L"historical_target");
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical A captured run_id={} tick={} combat_particles={} gpu_bytes={}\n"),
                RC::to_generic_string(request_.run_id),sample.frame,combat_checkpoint_->vfx.slot_count(),copy.bytes);
            interior_release_requested_ = true;
            if(UsesOwnedHistoricalAdvance()) {
                const auto advance=ResolveHorseModExport<bool (*)(std::uint64_t,void*,ReplayHost::InteriorObserver,ReplayHost::TickAdvanceWitness*)>("horsemod_request_replay_tick");
                ReplayHost::TickAdvanceWitness next{};
                if(!advance || !advance(HasSecondCheckpoint()?SecondCheckpointTick():request_.historical_advanced_tick,this,[](void* context,const ReplayHost::InteriorWitness& pause) {
                    return static_cast<ReplayQualificationMod*>(context)->ObservePairOriginalBoundary(pause);
                },&next) || next.phase!=ReplayHost::TickAdvancePhase::Releasing)
                {Fail("checkpoint_pair_first_advance_rejected");return false;}
                application_reentry_armed_=true;
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical owned advance run_id={} origin={} target={} host_owned=true\n"),RC::to_generic_string(request_.run_id),request_.historical_anchor_tick,HasSecondCheckpoint()?SecondCheckpointTick():request_.historical_advanced_tick);
                return false;
            }
            return true;
        };
        // Read deliberately supplies no mutable GPU witness while the render
        // command owns it. Cancellation touches only the GT operation flag.
        if(!particle_copy_failed_ && (pending || copy.pending) && historical_operation_started_
            && !external_gpu_diagnostic && request_.historical_cancel=="before" && !historical_cancel_requested_) {
            ReplayHost::RestoreOperationWitness preparing{};
            if(!restore(Operation::Read,nullptr,&preparing))
            {Fail("historical_preparation_cancel_unreadable");return false;}
            if(preparing.phase==Phase::Preparing) {
                if(!preparing.pending || sample!=interior_sample_
                    || !restore(Operation::Cancel,nullptr,&preparing))
                {Fail("historical_preparation_cancel_rejected");return false;}
                historical_cancel_requested_=true;
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation injected run_id={} point=before tick={} preparation_pending=true render_command_pending={} gpu_completion_pending={}\n"),
                    RC::to_generic_string(request_.run_id),request_.historical_advanced_tick,pending,!pending && copy.pending);
            }
        }
        if (pending || held.surface_pending) return false;
        if (!interior_started_)
        {
            std::uintptr_t vfx{};
            std::int32_t members{};
            if (!ReadSchedulerValue(reinterpret_cast<std::uintptr_t>(battle_manager_) + 0x508, vfx)
                || !ReadSchedulerValue(vfx + 0x3f0, members) || members < 0 || members > 1024
                || (request_.historical_anchor_tick==205 && members != (application_reentry_armed_ && request_.historical_advanced_tick >= 210 ? 8 : 7)))
            { Fail("historical_combat_particle_membership_changed"); return false; }
            if (sample.frame != (application_reentry_armed_ ? request_.historical_advanced_tick : request_.historical_anchor_tick))
            { Fail("historical_combat_target_changed"); return false; }
            if(request_.historical_advanced_tick==2504 && (sample.round!=(application_reentry_armed_?1:0) || !sample.source_active || sample.world_mode!=2))
            {Fail("historical_cross_round_boundary_changed");return false;}
            interior_started_ = true; interior_sample_ = sample;
            interior_epoch_ = held.epoch; application_hold_completed_ = held.completed_applications;
            interior_boundaries_ = boundary_samples_;
            if (!application_reentry_armed_) historical_capture_started_ = std::chrono::steady_clock::now();
            else historical_restore_started_ = std::chrono::steady_clock::now();
            if(!application_reentry_armed_ && !external_gpu_diagnostic) {
                if(!capture_control || !capture_control(ReplayHost::CaptureAction::Begin,&capture_state,nullptr))
                {Fail("historical_capture_request_rejected");return false;}
                if(request_.capture_cancel) {
                    if(!capture_control(ReplayHost::CaptureAction::Cancel,&capture_state,nullptr))
                    {Fail("capture_cancel_before_rejected");return false;}
                    capture_cancel_stage_=1;
                }
            } else if (external_gpu_diagnostic && !control(application_reentry_armed_ ? Action::VerifyState
                : Action::Begin, &copy, &pending))
            { Fail("historical_gpu_request_rejected"); return false; }
            return false;
        }
        if (held.epoch != interior_epoch_ || held.completed_applications != application_hold_completed_
            || boundary_samples_ != interior_boundaries_)
        { Fail("historical_native_work_advanced_during_hold"); return false; }
        if(!application_reentry_armed_ && !external_gpu_diagnostic && !combat_checkpoint_) {
            if(!capture_control || !capture_control(ReplayHost::CaptureAction::Read,&capture_state,nullptr))
            {Fail("historical_capture_status_unavailable");return false;}
            if(request_.capture_cancel && capture_state.phase==ReplayHost::CapturePhase::Cancelled
                && (capture_cancel_stage_==1 || capture_cancel_stage_==3)) {
                if(sample!=interior_sample_ || boundary_samples_!=interior_boundaries_ || capture_state.pending
                    || capture_state.failure!=Horse::Deterministic::FailureCode::None)
                {Fail("capture_cancellation_changed_state");return false;}
                Output::send<LogLevel::Default>(STR("[ReplayQualification] capture cancellation recovered point={} tick=205 native_boundaries_unchanged=true resources_retired=true\n"),
                    capture_cancel_stage_==1?STR("before_submission"):STR("after_ready"));
                if(!capture_control(ReplayHost::CaptureAction::Release,&capture_state,nullptr)
                    || !capture_control(ReplayHost::CaptureAction::Begin,&capture_state,nullptr))
                {Fail("capture_after_cancellation_rejected");return false;}
                ++capture_cancel_stage_;
                return false;
            }
            if(request_.capture_cancel && capture_cancel_stage_==2 && capture_state.phase==ReplayHost::CapturePhase::Ready) {
                if(!capture_control(ReplayHost::CaptureAction::Cancel,&capture_state,nullptr))
                {Fail("capture_cancel_ready_rejected");return false;}
                capture_cancel_stage_=3;return false;
            }
            if(capture_state.phase==ReplayHost::CapturePhase::Failed || capture_state.phase==ReplayHost::CapturePhase::Cancelled) {
                if(!capture_control(ReplayHost::CaptureAction::Release,&capture_state,nullptr))
                {Fail("historical_capture_retirement_unacknowledged");return false;}
                particle_copy_failed_=true;
            } else if(capture_state.phase!=ReplayHost::CapturePhase::Ready) return false;
        }
        if (copy.pending)
        {
            if ((!historical_operation_started_ || external_gpu_diagnostic) && !control(Action::Poll, &copy, &pending)) { Fail("historical_gpu_poll_rejected"); return false; }
            return false;
        }
        if (copy.phase == Copy::Phase::Failed || copy.phase == Copy::Phase::RetiringFailure) particle_copy_failed_ = true;
        if(particle_gpu_retirement_pending_) {
            if(particle_copy_failed_ || copy.phase!=Copy::Phase::ReadyA
                || copy.private_owner_retirements!=particle_gpu_retirement_serial_
                || particle_gpu_owner_.phase!=Horse::Deterministic::Sc6ReplayCpuEmitterState::FreshGpuOwner::Phase::RetirementQueued) {
                Fail("particle_gpu_retirement_not_completed",true);return false;
            }
            Output::send<LogLevel::Default>(STR("[ReplayQualification] particle GPU owner run_id={} tick={} component={:x} root={:x} render={:x} charged_bytes={} completion_serial={} constructed=true native_retirement_returned=true gpu_completed=true graph_published=false rollback_proof=false\n"),
                RC::to_generic_string(request_.run_id),sample.frame,particle_gpu_owner_.component,
                reinterpret_cast<std::uintptr_t>(particle_gpu_owner_.root),particle_gpu_owner_.render,
                particle_gpu_owner_.charged_bytes,copy.private_owner_retirements);
            particle_gpu_retirement_pending_=false;
            particle_gpu_owner_.charged_bytes=0;
            return release_captured_a();
        }

        if (!particle_copy_failed_ && !application_reentry_armed_ && copy.phase == Copy::Phase::ReadyA && !combat_checkpoint_)
        {
            if (external_gpu_diagnostic && !copy.capture_sealed)
            {
                if (!control(Action::SealCapture, &copy, &pending)) { Fail("historical_capture_seal_rejected"); return false; }
                return false;
            }
            std::uint16_t code{};
            if(external_gpu_diagnostic) {
                const auto capture=ResolveHorseModExport<std::uint16_t (*)(ReplayHost::Checkpoint*)>("horsemod_capture_replay_checkpoint");
                auto checkpoint=std::make_shared<ReplayHost::Checkpoint>();
                code=capture?capture(checkpoint.get()):65535;combat_checkpoint_=std::move(checkpoint);
            } else if(!capture_control(ReplayHost::CaptureAction::Take,&capture_state,&combat_checkpoint_)) code=65535;
            if (code || !combat_checkpoint_ || !combat_checkpoint_->valid || !combat_checkpoint_->gpu || !ReadReplayTrajectory(battle_manager_, sample, true)
                || sample != interior_sample_)
            {
                Output::send<LogLevel::Warning>(STR("[ReplayQualification] historical capture failed code={}\n"), code);
                particle_copy_failed_ = true;
            }
            else
            {
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical capture cost run_id={} elapsed_us={} gpu_bytes={} diagnostic_gpu_readbacks={} transaction_readback_maps={} readback_scope=observer_and_transaction\n"),
                    RC::to_generic_string(request_.run_id), std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-historical_capture_started_).count(),
                    copy.bytes, request_.pixel_diagnostics || request_.coherence_images, copy.readback_maps);
                if(request_.restore_reuse) reuse_checkpoint_fingerprint_=FingerprintCheckpointStorage(*combat_checkpoint_);
                historical_target_sample_ = sample;
                if(request_.particle_owner_copy) {
                    ReplayQualification::ParticleOwnerCopy::Witness owner_copy{};
                    const auto okay=ReplayQualification::ParticleOwnerCopy::Run(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),
                        battle_manager_,combat_checkpoint_->vfx,particle_owner_copy_lease_,owner_copy,
                        ResolveHorseModExport<ReplayQualification::ParticleOwnerCopy::SourceValidator>("horsemod_validate_replay_vfx_capture"),request_.particle_owner_registration,
                        request_.particle_owner_gpu?&particle_gpu_owner_:nullptr);
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] particle owner copy run_id={} tick={} check={} source={:x} copy={:x} outer={:x} properties={} arrays={} outer_members={} created={} detached={} destroyed={} lease_released={} source_unchanged={} registration_performed={} activation_performed=false rollback_proof=false\n"),
                        RC::to_generic_string(request_.run_id),sample.frame,RC::to_generic_string(owner_copy.check),owner_copy.source,owner_copy.copy,owner_copy.outer,
                        owner_copy.properties,owner_copy.arrays,owner_copy.outer_members,owner_copy.created,owner_copy.detached,owner_copy.destroyed,owner_copy.lease_released,owner_copy.source_unchanged,owner_copy.registration_performed);
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] particle owner copy allocation scope run_id={} property_offset={:x} property_bytes={} copy_flags={:x} native_allocator_peak=unmeasured budget_qualification=false\n"),
                        RC::to_generic_string(request_.run_id),owner_copy.property_offset,owner_copy.property_bytes,owner_copy.copy_flags);
                    if(!okay && std::string_view(owner_copy.check)=="outer_modify_domain")
                        Output::send<LogLevel::Default>(STR("[ReplayQualification] particle owner copy outer domain run_id={} modify_rva={:x} transaction_buffer={:x} dirty_gate={}\n"),
                            RC::to_generic_string(request_.run_id),owner_copy.modify_rva,owner_copy.transaction_buffer,owner_copy.dirty_gate);
                    if(!okay && owner_copy.differing_property)
                        Output::send<LogLevel::Default>(STR("[ReplayQualification] particle owner copy property difference run_id={} property={} offset={:x} size={} alias={} source_words={:016x},{:016x} copy_words={:016x},{:016x}\n"),
                            RC::to_generic_string(request_.run_id),owner_copy.differing_property->GetFullName(),
                            owner_copy.differing_property->GetOffset_Internal(),owner_copy.differing_property->GetElementSize(),owner_copy.differing_alias,
                            owner_copy.source_value[0],owner_copy.source_value[1],owner_copy.copy_value[0],owner_copy.copy_value[1]);
                    if(!okay && std::string_view(owner_copy.check)=="property_recursive_ownership_unproven" && owner_copy.visited_property) {
                        auto* p=owner_copy.visited_property;
                        const auto kind=p->GetClass().GetFName().ToString();
                        auto type=kind==L"StructProperty"?static_cast<RC::Unreal::FStructProperty*>(p)->GetStruct():nullptr;
                        const auto ops=type?reinterpret_cast<std::uintptr_t>(type->GetCppStructOps()):0;
                        const auto table=ops?ReplayQualification::ParticleOwnerCopy::Read<std::uintptr_t>(ops):0;
                        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
                        Output::send<LogLevel::Default>(STR("[ReplayQualification] particle owner copy nested property run_id={} property={} kind={} offset={:x} size={} dim={} flags={:x} struct_flags={:x} ops_table_rva={:x} serializer_rva={:x}\n"),
                            RC::to_generic_string(request_.run_id),p->GetFullName(),kind,p->GetOffset_Internal(),p->GetElementSize(),p->GetArrayDim(),
                            static_cast<std::uint64_t>(p->GetPropertyFlags()),type?static_cast<unsigned>(type->GetStructFlags()):0,
                            table?table-base:0,table?ReplayQualification::ParticleOwnerCopy::Read<std::uintptr_t>(table,0x38)-base:0);
                    }
                    if(!okay) {Fail("particle_owner_copy_failed",true);return false;}
                    if(request_.particle_owner_gpu) {
                        if(copy.private_owner_retirements==UINT64_MAX) {Fail("particle_gpu_retirement_serial_exhausted",true);return false;}
                        particle_gpu_retirement_serial_=copy.private_owner_retirements+1;
                        particle_gpu_retirement_pending_=true;
                        if(!control(Action::DrainPrivateOwnerRetirement,&copy,&pending))
                            Fail("particle_gpu_retirement_queue_failed",true);
                        return false;
                    }

                }
                return release_captured_a();
            }
        }
        if (external_gpu_diagnostic && !particle_copy_failed_ && application_reentry_armed_ && copy.phase == Copy::Phase::ReadyB && !historical_operation_started_)
        {
            if (!external_gpu_diagnostic && !copy.capture_reopened)
            { Fail("historical_checkpoint_image_was_not_reopened"); return false; }
            if (sample != interior_sample_) { Fail("historical_B_changed_before_preparation"); return false; }
            if (!control(Action::PrepareUndo, &copy, &pending)) particle_copy_failed_ = true;
            return false;
        }
        ReplayHost::RestoreOperationWitness operation{};
        if (!particle_copy_failed_ && application_reentry_armed_ && !historical_operation_started_ && combat_checkpoint_
            && (!external_gpu_diagnostic || copy.phase==Copy::Phase::UndoReady))
        {
            if(request_.source_revision && !request_.corrected_inputs && !source_revision_applied_) {
                if(!ApplyAuthoredSourceRevision(true)) {Fail("B_input_revision_activation_failed");return false;}
                source_revision_applied_=true;
            }

            const auto counters = ResolveHorseModExport<bool (*)(std::uint64_t*, std::size_t)>("horsemod_get_replay_executor_status");
            std::array<std::uint64_t, 12> execution{};
            if (!counters || !counters(execution.data(), execution.size()) || !combat_checkpoint_
                || execution[2] <= combat_checkpoint_->execution.tick
                || execution[3] <= combat_checkpoint_->execution.interval)
            { Fail("historical_execution_coordinates_invalid"); return false; }
            const auto& selected=request_.checkpoint_pair ? pair_checkpoint_ : combat_checkpoint_;
            if(!selected || execution[2]<=selected->execution.tick || execution[3]<=selected->execution.interval)
            {Fail("host_selected_checkpoint_coordinate_invalid");return false;}
            historical_rewind_ticks_ = execution[2] - selected->execution.tick;
            historical_rewind_intervals_ = execution[3] - selected->execution.interval;
            LogTrajectorySample(sample, 1, L"historical_original");
            historical_operation_started_ = true;
            if(request_.host_seek) {
                host_seek_original_sample_=sample; // Independent B witness; interior holds have separate mutable scratch.
                using SeekAction=ReplayHost::SeekAction;
                const auto seek=ResolveHorseModExport<bool (*)(SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,
                    ReplayHost::SeekWitness*,void*,ReplayHost::SeekObserver)>("horsemod_replay_seek_operation");
                ReplayHost::SeekWitness seeking{};
                const auto ownership=ResolveHorseModExport<std::uint16_t (*)(ReplayHost::CheckpointOwnership,
                    const ReplayHost::Checkpoint*,std::size_t*)>("horsemod_replay_checkpoint_ownership");
                auto& caller_checkpoint=request_.checkpoint_pair?pair_checkpoint_:combat_checkpoint_;
                const auto* checkpoint_address=caller_checkpoint.get();
                std::weak_ptr<const ReplayHost::Checkpoint> checkpoint_weak=caller_checkpoint;
                std::size_t pins{};
                if(!ownership || ownership(ReplayHost::CheckpointOwnership::Retain,checkpoint_address,&pins) || pins!=1)
                {Fail("host_checkpoint_retention_rejected");return false;}
                caller_checkpoint.reset();
                const bool host_only=checkpoint_weak.use_count()==1;
                // Returning to B's coordinate is still a real resimulation
                // experiment, including the first leg of a repeated seek.
                // Automatic same-tick UI requests do not select an older A.
                const bool resimulate_to_origin=HostSeekTarget()==execution[2];
                const bool accepted=host_only && seek && seek(SeekAction::Begin,resimulate_to_origin?checkpoint_address:nullptr,HostSeekTarget(),&seeking,this,
                    [](void* context,const ReplayHost::SeekWitness& state,const ReplayHost::InteriorWitness& held) {
                        static_cast<ReplayQualificationMod*>(context)->ObserveHostSeek(state,held);
                    });
                caller_checkpoint=checkpoint_weak.lock();
                const bool released=!ownership(ReplayHost::CheckpointOwnership::Release,checkpoint_address,&pins) && pins==0;
                if(!accepted || !released || !caller_checkpoint || seeking.phase!=ReplayHost::SeekPhase::Preparing || !seeking.pending)
                {Fail("host_seek_request_rejected");return false;}
                if(seeking.automatic_selection==resimulate_to_origin)
                {Fail("host_checkpoint_selection_receipt_invalid");return false;}
                Output::send<LogLevel::Default>(STR("[ReplayQualification] host checkpoint handoff run_id={} checkpoint={} sole_host_owner=true automatic_selection={} pin_released_after_acceptance=true retained_count={} caller_readback_pin_reacquired=true\n"),
                    RC::to_generic_string(request_.run_id),seeking.checkpoint,seeking.automatic_selection,pins);
                host_seek_selected_tick_=seeking.checkpoint;host_seek_original_interval_=execution[3];
                if(ExecutionFallback()) {
                    // Begin resolves the registered owner and validates its GC
                    // lease inside HorseMod. Calling that validator here would
                    // compare its native vtable against this DLL's callback table.
                    // Observe the admitted host receipt and immutable coordinates.
                    if(!seeking.automatic_selection || seeking.checkpoint!=205 || seeking.origin!=210
                        || !pair_checkpoint_ || pair_checkpoint_->execution.tick!=205
                        || !combat_checkpoint_ || combat_checkpoint_->execution.tick!=170)
                    {Fail("execution_fallback_initial_selection_unproven");return false;}
                    historical_rewind_ticks_=execution[2]-pair_checkpoint_->execution.tick;
                    historical_rewind_intervals_=execution[3]-pair_checkpoint_->execution.interval;
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] execution fallback selected run_id={} checkpoint=205 earlier=170 origin=210 target=220 selected_owner_validated_by_host=true earlier_retained=true explicit_checkpoint=false\n"),RC::to_generic_string(request_.run_id));
                } else if(request_.checkpoint_fallback) {
                    if(!pair_checkpoint_ || pair_checkpoint_->execution.tick!=205
                        || !seeking.automatic_selection || !seeking.retained_owner_rejected || seeking.rejected_checkpoint!=205
                        || seeking.checkpoint!=170 || seeking.origin!=220)
                    {Fail("automatic_checkpoint_fallback_not_exercised");return false;}
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] checkpoint fallback selected run_id={} nearest=205 nearest_owners_live=false selected=170 selected_owners_live=true origin=220 target=208 explicit_checkpoint=false\n"),RC::to_generic_string(request_.run_id));
                    // The failed owner is no longer a candidate. Release only
                    // the caller handle at completed B; queued GPU/native
                    // ownership remains charged until host retirement finishes.
                    pair_checkpoint_.reset();
                }
                const auto before=seeking;
                if(seek(SeekAction::Begin,combat_checkpoint_.get(),HostSeekTarget(),&seeking,nullptr,nullptr)
                    || seek(static_cast<SeekAction>(255),nullptr,0,&seeking,nullptr,nullptr)
                    || restore(Operation::Request,combat_checkpoint_.get(),&operation)
                    || control(Action::Finish,&copy,&pending)
                    || !seek(SeekAction::Read,nullptr,0,&seeking,nullptr,nullptr)
                    || seeking.phase!=before.phase || seeking.origin!=before.origin || seeking.target!=before.target
                    || seeking.pending!=before.pending || seeking.commit_decided!=before.commit_decided)
                {Fail("host_seek_ownership_conflict");return false;}
                Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek accepted run_id={} checkpoint={} origin={} target={} aliases_rejected=true\n"),
                    RC::to_generic_string(request_.run_id),seeking.checkpoint,seeking.origin,seeking.target);
                return false;
            }
            if (!restore(external_gpu_diagnostic?Operation::Begin:Operation::Request, combat_checkpoint_.get(), &operation))
            {
                historical_first_failure_ = "historical_preflight_";
                historical_first_failure_ += operation.participant ? operation.participant : "admission";
                Output::send<LogLevel::Warning>(STR("[ReplayQualification] historical assembled preflight failed participant={} code={} bytes={}\n"),
                    RC::to_generic_string(operation.participant ? operation.participant : "admission"),
                    static_cast<unsigned>(operation.failure), operation.owned_bytes);
                if (!ReadReplayTrajectory(battle_manager_, sample, true) || sample != interior_sample_
                    || (operation.phase!=Phase::Empty && !restore(Operation::Release, nullptr, &operation)))
                { Fail("historical_preflight_cleanup_failed"); return false; }
                historical_operation_started_ = false;
                particle_copy_failed_ = true;
            }
            if(historical_operation_started_ && !external_gpu_diagnostic) {
                const auto before=operation;
                if(operation.phase!=Phase::Preparing || !operation.pending
                    || restore(Operation::Begin,combat_checkpoint_.get(),&operation)
                    || restore(Operation::Request,combat_checkpoint_.get(),&operation)
                    || restore(Operation::Commit,nullptr,&operation)
                    || control(Action::Finish,&copy,&pending)
                    || !restore(Operation::Read,nullptr,&operation)
                    || operation.phase!=before.phase || operation.failure!=before.failure
                    || operation.pending!=before.pending || operation.target_tick!=before.target_tick
                    || operation.original_tick!=before.original_tick)
                {Fail("restore_request_ownership_conflict");return false;}
                Output::send<LogLevel::Default>(STR("[ReplayQualification] restore request accepted target=205 original={} pending=true aliases_rejected=true\n"),request_.historical_advanced_tick);
            }
            return false;
        }
        if (historical_operation_started_)
        {
            if (!restore(Operation::Read, nullptr, &operation)) { Fail("historical_operation_unreadable"); return false; }
            if (operation.owned_bytes > Horse::Deterministic::Schema::replay_timeline_memory_limit - presentation_observer_.ReservationBytes())
            {
                Output::send<LogLevel::Warning>(STR("[ReplayQualification] historical observer reservation rejected owned_bytes={} observer_reserved_bytes={} limit={}\n"),
                    operation.owned_bytes, presentation_observer_.ReservationBytes(),
                    Horse::Deterministic::Schema::replay_timeline_memory_limit);
                restore(Operation::Cancel, nullptr, &operation); Fail("historical_presentation_observer_budget"); return false;
            }
            if(operation.phase==Phase::Preparing) {
                if(!operation.pending || sample!=interior_sample_)
                {Fail("historical_async_preparation_changed_B");return false;}
                return false;
            }
            if (operation.phase != Phase::Failed && operation.phase != Phase::Recovered
                && operation.particle_birth != (request_.historical_advanced_tick == 210))
            { Fail("historical_topology_admission_mismatch"); return false; }
            if (operation.pending) return false;
            if (operation.phase == Phase::Prepared)
            {
                if(!external_gpu_diagnostic && !copy.capture_reopened)
                {Fail("historical_requested_image_not_reopened");return false;}
                if (!historical_invalid_actions_checked_)
                {
                    const auto before = operation;
                    if (restore(Operation::Commit, nullptr, &operation)
                        || restore(static_cast<Operation>(255), nullptr, &operation)
                        || !restore(Operation::Read, nullptr, &operation)
                        || operation.phase != before.phase || operation.failure != before.failure
                        || operation.target_tick != before.target_tick || operation.original_tick != before.original_tick
                        || operation.pending != before.pending || sample != interior_sample_)
                    { Fail("historical_invalid_action_mutated_prepared_operation"); return false; }
                    historical_invalid_actions_checked_ = true;
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] historical topology admitted run_id={} target_tick=205 original_tick={} particle_birth={}\n"),
                        RC::to_generic_string(request_.run_id), request_.historical_advanced_tick, operation.particle_birth);
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] historical action admission run_id={} premature_commit_rejected=true unknown_action_rejected=true phase_unchanged=true tick={}\n"),
                        RC::to_generic_string(request_.run_id), request_.historical_advanced_tick);
                }
                if (request_.historical_cancel == "before")
                {
                    if (!historical_cancel_requested_)
                    {
                        historical_cancel_requested_ = restore(Operation::Cancel, nullptr, &operation);
                        if (!historical_cancel_requested_) { Fail("historical_cancel_before_rejected"); return false; }
                        Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation injected run_id={} point=before tick={}\n"),
                            RC::to_generic_string(request_.run_id), request_.historical_advanced_tick);
                    }
                    return false;
                }
                if (!restore(Operation::Publish, nullptr, &operation))
                    Output::send<LogLevel::Warning>(STR("[ReplayQualification] historical publication failed code={} recovering=true\n"),
                        static_cast<unsigned>(operation.failure));
                return false;
            }
            if (operation.phase == Phase::Held)
            {
                if (sample != historical_target_sample_ || held.tick != 205)
                {
                    restore(Operation::Cancel, nullptr, &operation);
                    particle_copy_failed_ = true;
                    return false;
                }
                if (!historical_target_held_)
                {
                    historical_target_held_ = true;
                    if (!copy.scene_fields_empty || !copy.scene_field_checks) { Fail("historical_scene_fields_unadmitted"); return false; }
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] historical scene fields run_id={} empty=true slots={} checks={} policy=reject_nonempty_v1\n"),
                        RC::to_generic_string(request_.run_id), copy.scene_field_slots, copy.scene_field_checks);
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] historical restore cost run_id={} elapsed_us={} owned_bytes={} diagnostic_gpu_readbacks={} transaction_readback_maps={} readback_scope=observer_and_transaction includes_B_undo=true includes_image_reopen=true request_origin=held_B owned_bytes_kind=conservative_reservation\n"),
                        RC::to_generic_string(request_.run_id), std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-historical_restore_started_).count(),
                        operation.owned_bytes, copy.diagnostic_readbacks || request_.coherence_images, copy.readback_maps);
                    historical_hold_started_ = std::chrono::steady_clock::now();
                    historical_hold_frames_ = held.surface_frames;
                    LogTrajectorySample(sample, 1, L"historical_restored");
                }
                const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - historical_hold_started_).count();
                if (elapsed < 500'000 || held.surface_frames - historical_hold_frames_ < 30) return false;
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical target held run_id={} tick=205 elapsed_us={} surface_frames={} native_boundaries_unchanged=true bytes={}\n"),
                    RC::to_generic_string(request_.run_id), elapsed, held.surface_frames - historical_hold_frames_, operation.owned_bytes);
                if (request_.historical_cancel == "after" || reuse)
                {
                    if (!historical_cancel_requested_)
                    {
                        historical_cancel_requested_ = restore(Operation::Cancel, nullptr, &operation);
                        if (!historical_cancel_requested_) { Fail("historical_cancel_after_rejected"); return false; }
                        Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation injected run_id={} point=after tick=205\n"),
                            RC::to_generic_string(request_.run_id));
                    }
                    return false;
                }
                if (!restore(Operation::Commit, nullptr, &operation))
                {
                    historical_first_failure_ = "historical_commit_";
                    historical_first_failure_ += operation.participant ? operation.participant : "admission";
                    restore(Operation::Cancel, nullptr, &operation); particle_copy_failed_ = true;
                }
                return false;
            }
            if (operation.phase == Phase::Committed)
            {
                if (!restore(Operation::Release, nullptr, &operation)) { Fail("historical_execution_started_owner_release_failed"); return false; }
                historical_operation_started_ = false;
                historical_execution_started_ = true;
                if(request_.corrected_inputs && !(request_.source_revision ? ApplyAuthoredSourceRevision(true) : boundary_observer_.ActivateChangedInputs())) {Fail("corrected_input_activation_failed");return false;}
                combat_checkpoint_.reset();
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical execution rewind run_id={} ticks={} intervals={}\n"),
                    RC::to_generic_string(request_.run_id), historical_rewind_ticks_, historical_rewind_intervals_);
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical combat committed run_id={} from_tick={} to_tick=205\n"),
                    RC::to_generic_string(request_.run_id), request_.historical_advanced_tick);
                if (request_.historical_exact_advance)
                {
                    const auto advance = ResolveHorseModExport<bool (*)(std::uint64_t, void*, ReplayHost::InteriorObserver,
                        ReplayHost::TickAdvanceWitness*)>("horsemod_request_replay_tick");
                    ReplayHost::TickAdvanceWitness advancement{};
                    if (!advance || !advance(208, this, [](void* context, const ReplayHost::InteriorWitness& witness) {
                        return static_cast<ReplayQualificationMod*>(context)->ObserveHistoricalExactHold(witness);
                    }, &advancement) || advancement.phase != ReplayHost::TickAdvancePhase::Releasing)
                    { Fail("historical_exact_advance_rejected"); return false; }
                    historical_exact_requested_ = true;
                    interior_expected_tick_ = 208;
                    interior_release_requested_ = true; // Release A; target observer resets this for its own hold.
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] historical exact requested run_id={} origin=205 target=208\n"),
                        RC::to_generic_string(request_.run_id));
                    return true;
                }
                world_pause_completed_ = true; world_resume_measured_ = false;
                world_resume_frame_ = 205; world_resume_started_ = world_resume_previous_ = std::chrono::steady_clock::now();
                world_resume_viewport_ = viewport_frame_count_.load(std::memory_order_relaxed);
                world_resume_max_gap_us_ = world_resume_late_gaps_ = 0;
                interior_release_requested_ = true;
                return true;
            }
            if (operation.phase == Phase::Recovered)
            {
                if (copy.phase != Copy::Phase::Released) return false;
                if (!operation.original_recovered || sample != interior_sample_ || held.tick != request_.historical_advanced_tick
                    || !restore(Operation::Release, nullptr, &operation))
                { Fail("historical_B_undo_observation_failed"); return false; }
                historical_operation_started_ = false;
                if(reuse && historical_cancel_requested_ && !particle_copy_failed_) {
                    if(!combat_checkpoint_ || !combat_checkpoint_->gpu
                        || FingerprintCheckpointStorage(*combat_checkpoint_)!=reuse_checkpoint_fingerprint_)
                    {Fail("restore_reuse_checkpoint_changed");return false;}
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation recovered run_id={} point=after tick={}\n"),
                        RC::to_generic_string(request_.run_id),request_.historical_advanced_tick);
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] restore reuse ready target=205 original={} checkpoint_storage_unchanged=true prior_gpu_operation_released=true\n"),request_.historical_advanced_tick);
                    restore_reuse_completed_=true;historical_cancel_requested_=false;
                    historical_target_held_=false;historical_invalid_actions_checked_=false;
                    historical_restore_started_=std::chrono::steady_clock::now();
                    return false;
                }
                if (historical_cancel_requested_ && !particle_copy_failed_)
                {
                    LogTrajectorySample(sample, 1, L"historical_recovered");
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation recovered run_id={} point={} tick={}\n"),
                        RC::to_generic_string(request_.run_id), RC::to_generic_string(request_.historical_cancel), request_.historical_advanced_tick);
                    combat_checkpoint_.reset();
                    world_pause_completed_ = true; world_resume_measured_ = false;
                    world_resume_frame_ = request_.historical_advanced_tick; world_resume_started_ = world_resume_previous_ = std::chrono::steady_clock::now();
                    world_resume_viewport_ = viewport_frame_count_.load(std::memory_order_relaxed);
                    world_resume_max_gap_us_ = world_resume_late_gaps_ = 0;
                    interior_release_requested_ = true;
                    return true;
                }
                particle_copy_failed_ = true;
            }
            if (operation.phase == Phase::Failed)
            { Fail("historical_transaction_failed_state_retained"); return false; }
            return false;
        }
        if (particle_copy_failed_)
        {
            if (copy.native_write_uncommitted) { Fail("historical_failed_publication_without_owner"); return false; }
            if (copy.phase != Copy::Phase::Released)
            {
                if (!control(Action::Finish, &copy, &pending)) { Fail("historical_resource_retirement_failed"); return false; }
                return false;
            }
            combat_checkpoint_.reset();
            interior_release_requested_ = true;
            return true; // Existing resume observer reports failure after safe native re-entry.
        }
        return false;
    }

    bool ObserveParticleCopyHold(const ReplayHost::InteriorWitness& held, std::uint64_t target)
    {
        using Copy = Horse::Deterministic::Sc6ReplayParticleCopy;
        using Action = ReplayHost::ParticleCopyAction;
        const auto control = ResolveHorseModExport<bool (*)(Action, Copy::Witness*, bool*)>("horsemod_particle_copy_experiment");
        if (!control) { Fail("particle_copy_api_missing"); return false; }
        Copy::Witness copy{};
        bool command_pending{};
        const auto readable = control(Action::Read, &copy, &command_pending);
        if (command_pending || held.surface_pending) return false;
        if (!readable) particle_copy_failed_ = true;
        if (!particle_copy_started_)
        {
            std::uintptr_t manager{};
            std::int32_t count{};
            if (!ReadSchedulerValue(reinterpret_cast<std::uintptr_t>(battle_manager_) + 0x508, manager)
                || !ReadSchedulerValue(manager + 0x3f0, count) || count != (application_reentry_armed_ ? 8 : 7)
                || application_target_tick_ != 205 || application_reentry_tick_ != 210)
            { Fail("particle_copy_combat_membership_selection_changed"); return false; }
            std::uintptr_t slots{};
            if (!ReadSchedulerValue(manager + 0x3e8, slots) || !slots)
            { Fail("particle_transaction_slots_unavailable"); return false; }
            for (int i = 0; i < count; ++i)
            {
                std::uintptr_t component{};
                if (!ReadSchedulerValue(slots + static_cast<std::size_t>(i) * 0xc0, component)
                    || (component && !ObserveParticleEmitterOwnership(component, target)))
                { Fail("particle_transaction_payload_ownership_unavailable"); return false; }
            }
            particle_copy_started_ = true;
            if (!control(application_reentry_armed_ ? Action::VerifyAtB : Action::Begin, &copy, &command_pending))
            { Fail("particle_copy_request_rejected"); return false; }
            return false;
        }
        const bool expected_publication_cancel = particle_publication_cancel_requested_
            && copy.error == E_ABORT && copy.undo_ready && copy.native_write_uncommitted
            && !particle_publication_recovery_requested_;
        if ((copy.phase == Copy::Phase::Failed || copy.phase == Copy::Phase::RetiringFailure)
            && !expected_publication_cancel)
        {
            if (!particle_copy_failure_logged_)
            {
                particle_copy_failed_ = particle_copy_failure_logged_ = true;
                Output::send<LogLevel::Warning>(STR(
                    "[ReplayQualification] particle copy failed run_id={} tick={} phase={} hr={} resource={} formats={},{},{},{},{},{} pending={}\n"),
                    RC::to_generic_string(request_.run_id), target, static_cast<unsigned>(copy.phase), copy.error, copy.failed_resource,
                    copy.native_formats[0], copy.native_formats[1], copy.native_formats[2], copy.native_formats[3], copy.native_formats[4], copy.native_formats[5], copy.pending);
            }
        }
        if (copy.pending)
        {
            if (!control(Action::Poll, &copy, &command_pending)) { Fail("particle_copy_poll_rejected"); return false; }
            return false;
        }
        const auto partition_index = application_reentry_armed_ ? 1u : 0u;
        if (!particle_copy_failed_ && (copy.phase == Copy::Phase::ReadyA || copy.phase == Copy::Phase::ReadyB)
            && !particle_partition_observed_[partition_index])
        {
            particle_partition_observed_[partition_index] = true;
            const auto total = Horse::Deterministic::Schema::replay_checkpoint_memory_budget;
            if (copy.bytes > total || held.surface_bytes > total - copy.bytes
                || !ObserveSharedParticleOwners(copy.pool, target, total - copy.bytes - held.surface_bytes))
            { RetireCpuFailure("shared_particle_partition_unresolved"); return false; }
        }
        // This subexperiment never presents or resumes the temporary A
        // textures with B's CPU state. The surface is held throughout, commit
        // is not exported, and every publication is followed by verified B undo.
        if (copy.native_write_uncommitted
            && (particle_copy_failed_ || copy.phase == Copy::Phase::Failed || copy.phase == Copy::Phase::RetiringFailure))
        {
            if (particle_publication_recovery_requested_)
            { Fail("particle_publication_undo_failed_state_retained"); return false; }
            particle_publication_recovery_requested_ = true;
            if (!control(Action::RestoreUndo, &copy, &command_pending))
            { Fail("particle_publication_undo_rejected_state_retained"); return false; }
            return false;
        }
        if (!particle_copy_failed_ && application_reentry_armed_ && copy.phase == Copy::Phase::ReadyB)
        {
            const auto total = Horse::Deterministic::Schema::replay_checkpoint_memory_budget;
            const auto owned = copy.bytes + held.surface_bytes + ParticleLeaseBytes()
                + (particle_vfx_image_ ? particle_vfx_image_->owned_bytes() : 0)
                + (particle_cpu_prepared_ ? particle_cpu_prepared_->owned_bytes() : 0);
            // B undo adds six images. This check includes the observer's
            // existing CPU image/lease storage before asking the host to allocate.
            if (owned > total || 56ull * 1024 * 1024 > total - owned)
            { RetireCpuFailure("particle_publication_budget"); return false; }
            if (!PrepareHistoricalEmitters(total - copy.bytes - held.surface_bytes - ParticleLeaseBytes()
                - 56ull * 1024 * 1024)) return false;
            if (!control(Action::PrepareUndo, &copy, &command_pending))
            { RetireCpuFailure("particle_publication_prepare_rejected"); return false; }
            return false;
        }
        if (!particle_copy_failed_ && copy.phase == Copy::Phase::UndoReady)
        {
            if (!control(Action::InstallCaptured, &copy, &command_pending))
            { RetireCpuFailure("particle_publication_install_rejected"); return false; }
            return false;
        }
        if (!particle_copy_failed_ && copy.phase == Copy::Phase::Installed)
        {
            if (!particle_publication_release_checked_)
            {
                particle_publication_release_checked_ = true;
                if (!control(Action::CheckUncommittedRelease, &copy, &command_pending))
                { Fail("particle_publication_release_guard_rejected"); return false; }
                return false;
            }
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] particle textures installed run_id={} tick=210 captured_tick=205 "
                "images_match={} gpu_complete=true release_blocked=true resume_blocked=true cpu_state=B target_presented=false\n"),
                RC::to_generic_string(request_.run_id), copy.installed_matches);
            particle_publication_cancel_requested_ = true;
            if (!control(Action::Cancel, &copy, &command_pending))
            { Fail("particle_publication_cancel_rejected_state_retained"); return false; }
            return false;
        }
        if (particle_copy_failed_ || (application_reentry_armed_ && copy.phase == Copy::Phase::Recovered))
        {
            if (!particle_copy_finish_requested_)
            {
                particle_copy_finish_requested_ = true;
                if (!particle_copy_failed_)
                {
                    particle_copy_failed_ = !copy.retained_matches || copy.source_changes == 0
                        || !copy.cancel_release_blocked || !copy.cancel_retired
                        || !copy.timeout_release_blocked || !copy.timeout_retired
                        || !copy.installed_matches || !copy.recovered_matches || copy.native_write_uncommitted
                        || !particle_publication_cancel_requested_ || !particle_publication_release_checked_
                        || copy.error != E_ABORT;
                    Output::send<LogLevel::Default>(STR(
                        "[ReplayQualification] particle textures recovered run_id={} tick=210 images_match={} "
                        "gpu_complete=true cancelled_after_publication={} uncommitted={} selection={}\n"),
                        RC::to_generic_string(request_.run_id), copy.recovered_matches,
                        particle_publication_cancel_requested_, copy.native_write_uncommitted, copy.subsequent_selection);
                    Output::send<LogLevel::Default>(STR(
                        "[ReplayQualification] particle copy retirement run_id={} tick=210 cancel_release_blocked={} cancel_retired={} timeout_release_blocked={} timeout_retired={}\n"),
                        RC::to_generic_string(request_.run_id), copy.cancel_release_blocked, copy.cancel_retired,
                        copy.timeout_release_blocked, copy.timeout_retired);
                    Output::send<LogLevel::Default>(STR(
                        "[ReplayQualification] particle copy survived run_id={} captured_tick=205 held_tick=210 system={} pool={} "
                        "retained_matches={} changed_sources={} reserved_payload_and_metadata_bytes={} gpu_complete=true\n"),
                        RC::to_generic_string(request_.run_id), copy.system, copy.pool, copy.retained_matches, copy.source_changes, copy.bytes);
                }
                if (!control(Action::Finish, &copy, &command_pending)) { Fail("particle_copy_finish_rejected"); return false; }
                return false;
            }
        }
        if (copy.phase == Copy::Phase::ReadyA && !application_reentry_armed_)
        {
            if (!ObserveCpuReplacement(held, copy.bytes)) return false;
            if (!interior_release_requested_)
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] particle copy captured run_id={} tick=205 system={} pool={} resources=6 "
                    "gpu_complete=true reserved_payload_and_metadata_bytes={}\n"),
                    RC::to_generic_string(request_.run_id), copy.system, copy.pool, copy.bytes);
            interior_release_requested_ = true;
            return true;
        }
        if (copy.phase == Copy::Phase::Released)
        {
            if (!RetireParticleObjectLeases()) { Fail("particle_object_lease_retirement_failed"); return false; }
            if (!interior_release_requested_)
            {
                Output::send<LogLevel::Default>(STR("[ReplayQualification] particle object leases retired run_id={} tick={} registered=0\n"),
                    RC::to_generic_string(request_.run_id), target);
                Output::send<LogLevel::Default>(STR("[ReplayQualification] particle copy released run_id={} tick={} in_flight=false\n"),
                    RC::to_generic_string(request_.run_id), target);
            }
            interior_release_requested_ = true;
            return true;
        }
        return false;
    }

    bool ObserveHistoricalExactHold(const ReplayHost::InteriorWitness& held)
    {
        if(request_.completion_repeat && held.tick==208 && completion_repeat_phase_>0 && completion_repeat_phase_<3
            && !ObserveCompletionRepeat(held)) return false;
        // A tick number does not identify a checkpoint boundary: e.g.205
        // reached by executing from170 still owns unfinished native work.
        const bool checkpoint_target=request_.host_seek && HostSeekCheckpoint()
            && held.tick==HostSeekCheckpoint()->execution.tick && HostSeekTarget()==held.tick;
        if (!historical_exact_requested_ || (checkpoint_target
            ? held.boundary!=ReplayHost::PauseBoundary::CompletedApplication || held.pending_task
                || !held.engine_idle || !held.world_idle || !held.arena_empty || !held.application_idle
            : held.boundary!=ReplayHost::PauseBoundary::SimulationTick
                || held.engine_idle || held.world_idle || held.arena_empty || held.application_idle))
        { Fail("historical_exact_pending_ownership_missing"); return false; }
        if (!historical_exact_started_) {
            historical_exact_started_ = true;
            interior_started_ = false;
            interior_release_requested_ = false;
        }
        const auto read = ResolveHorseModExport<bool (*)(ReplayHost::TickAdvanceWitness*)>("horsemod_read_replay_tick");
        ReplayHost::TickAdvanceWitness status{};
        if (!read || !read(&status) || status.target != held.tick
            || (held.phase == ReplayHost::InteriorPhase::Holding && status.phase != ReplayHost::TickAdvancePhase::Held
                && !(request_.completion_repeat && held.tick==208 && completion_repeat_phase_==3
                    && status.phase==ReplayHost::TickAdvancePhase::CompletionBlocked))
            || (held.phase == ReplayHost::InteriorPhase::Releasing && status.phase != ReplayHost::TickAdvancePhase::Idle))
        { Fail("historical_exact_status_not_held"); return false; }
        if(checkpoint_target) {
            if(held.surface_pending) return false;
            ReplayTrajectorySample sample{};
            if(!ReadReplayTrajectory(battle_manager_,sample,true) || sample.frame!=held.tick)
            {Fail("checkpoint_target_unreadable");return false;}
            if(!interior_started_) {
                interior_started_=true;interior_sample_=sample;interior_epoch_=held.epoch;
                interior_boundaries_=boundary_samples_;interior_surface_start_=held.surface_frames;
                interior_started_at_=std::chrono::steady_clock::now();
                LogTrajectorySample(sample,1,L"historical_checkpoint_held");
                return false;
            }
            if(sample!=interior_sample_ || boundary_samples_!=interior_boundaries_ || held.epoch!=interior_epoch_)
            {Fail("checkpoint_target_hold_advanced");return false;}
            const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-interior_started_at_).count();
            const auto frames=held.surface_frames-interior_surface_start_;
            if(elapsed<1'000'000 || frames<30 || held.application_updates<30) return false;
            if(!interior_release_requested_) {
                interior_release_requested_=true;
                Output::send<LogLevel::Default>(STR("[ReplayQualification] checkpoint pause held run_id={} native_frame={} epoch={} elapsed_us={} application_updates={} surface_frames={} surface_bytes={} unchanged=true pending_event=false application_idle=true\n"),
                    RC::to_generic_string(request_.run_id),held.tick,held.epoch,elapsed,held.application_updates,frames,held.surface_bytes);
            }
        } else if (!ObserveInteriorPause(held)) return false;
        if(request_.completion_repeat && held.tick==208 && !ObserveCompletionRepeat(held)) return false;
        if(request_.completion_repeat && request_.historical_cancel=="after") {
            if(!historical_cancel_requested_ && CancelHostSeek())
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation injected run_id={} point=after tick=208 completion_deferred=true dispatch=host_api\n"),RC::to_generic_string(request_.run_id));
            return false;
        }
        if (request_.historical_single_step && held.tick == 208) {
            if(request_.completion_repeat) {
                if(!historical_step_control_waiting_) {
                    historical_step_control_waiting_=true;
                    const auto seek=ResolveHorseModExport<bool (*)(ReplayHost::SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,ReplayHost::SeekWitness*,void*,ReplayHost::SeekObserver)>("horsemod_replay_seek_operation");
                    ReplayHost::SeekWitness step{};
                    if(!seek || !seek(ReplayHost::SeekAction::Step,nullptr,0,&step,nullptr,nullptr)
                        || step.target!=209 || step.phase!=ReplayHost::SeekPhase::Advancing || !step.pending || step.commit_decided)
                        Fail("completion_repeat_explicit_step_rejected");
                    else {
                        // The API's accepted witness is the dispatch event.
                        // A later application callback may already be at209.
                        // Deliver this returned value before native execution,
                        // exactly as the host UI path does for its observer.
                        ObserveHostSeek(step,held);
                    }
                }
                return false;
            }
            if(!historical_step_control_waiting_) {
                historical_step_control_waiting_=true;
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical step control waiting run_id={} tick=208 pending_event=true unchanged=true\n"),RC::to_generic_string(request_.run_id));
                return false;
            }
            if(!held.ui_step_requests) {
                if(std::chrono::steady_clock::now()-interior_started_at_>std::chrono::seconds(5)) Fail("historical_step_ui_activation_missing");
                return false;
            }
            if(request_.host_seek) return false; // The host owns Step and preserves B.
            if(held.ui_step_requests!=1) {Fail("historical_step_ui_duplicate_activation");return false;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical step control verified run_id={} tick=208 requests=1 pending_event=true unchanged=true\n"),RC::to_generic_string(request_.run_id));
            const auto advance = ResolveHorseModExport<bool (*)(std::uint64_t, void*, ReplayHost::InteriorObserver,
                ReplayHost::TickAdvanceWitness*)>("horsemod_request_replay_tick");
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical exact held run_id={} tick=208 pending_world=true pending_task=true\n"),
                RC::to_generic_string(request_.run_id));
            if (!advance || !advance(209, this, [](void* context, const ReplayHost::InteriorWitness& witness) {
                return static_cast<ReplayQualificationMod*>(context)->ObserveHistoricalExactHold(witness);
            }, &status) || status.phase != ReplayHost::TickAdvancePhase::Stepping)
            { Fail("historical_single_step_rejected"); return false; }
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical single step requested run_id={} origin=208 target=209\n"),
                RC::to_generic_string(request_.run_id));
            interior_expected_tick_=209;historical_exact_started_=false;
            interior_release_requested_=false;interior_started_=false;
            return false;
        }
        if (!historical_exact_held_)
        {
            historical_exact_held_ = true;
            application_hold_completed_ = held.completed_applications;
            world_pause_completed_ = true; world_resume_measured_ = false;
            world_resume_frame_ = held.tick;
            world_resume_started_ = world_resume_previous_ = std::chrono::steady_clock::now();
            world_resume_viewport_ = viewport_frame_count_.load(std::memory_order_relaxed);
            world_resume_max_gap_us_ = world_resume_late_gaps_ = 0;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical exact held run_id={} tick={} pending_world={} pending_task={}\n"),
                RC::to_generic_string(request_.run_id), held.tick,!checkpoint_target,!checkpoint_target);
        }
        if(request_.historical_single_step && !request_.host_seek) {
            const auto complete=ResolveHorseModExport<bool (*)(void*,ReplayHost::InteriorObserver,ReplayHost::TickAdvanceWitness*)>("horsemod_complete_replay_application");
            if(!complete || !complete(this,[](void* context,const ReplayHost::InteriorWitness& witness) {
                return static_cast<ReplayQualificationMod*>(context)->ObserveCompletedInteriorApplication(witness);
            },&status) || status.phase!=ReplayHost::TickAdvancePhase::Settling || status.origin!=209 || status.target!=0)
            {Fail("historical_complete_application_rejected");return false;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical application completion requested run_id={} origin=209\n"),RC::to_generic_string(request_.run_id));
            interior_started_=false;interior_release_requested_=false;
            return false;
        }
        return true;
    }

    bool ObserveCompletedInteriorApplication(const ReplayHost::InteriorWitness& held)
    {
        if(held.boundary!=ReplayHost::PauseBoundary::CompletedApplication || held.pending_task
            || !held.application_idle || !held.engine_idle || !held.world_idle || !held.arena_empty || held.tick!=209)
        {Fail("historical_completed_application_ownership_invalid");return false;}
        if(held.surface_pending) return false;
        if(interior_release_requested_) return true;
        const auto read=ResolveHorseModExport<bool (*)(ReplayHost::TickAdvanceWitness*)>("horsemod_read_replay_tick");
        ReplayHost::TickAdvanceWitness status{};ReplayTrajectorySample sample{};
        if(!read || !read(&status) || status.phase!=ReplayHost::TickAdvancePhase::Settled
            || status.origin!=209 || status.target!=held.tick || !ReadReplayTrajectory(battle_manager_,sample,true))
        {Fail("historical_completed_application_status_invalid");return false;}
        if(!interior_started_) {
            interior_started_=true;interior_sample_=sample;interior_epoch_=held.epoch;
            interior_boundaries_=boundary_samples_;interior_started_at_=std::chrono::steady_clock::now();
            historical_hold_frames_=held.surface_frames;
            LogTrajectorySample(sample,1,L"historical_settled");
            return false;
        }
        if(sample!=interior_sample_ || boundary_samples_!=interior_boundaries_ || held.epoch!=interior_epoch_)
        {Fail("historical_completed_application_hold_changed");return false;}
        const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-interior_started_at_).count();
        if(elapsed<500000 || held.surface_frames-historical_hold_frames_<30) return false;
        Output::send<LogLevel::Default>(STR("[ReplayQualification] historical application completed held run_id={} origin=209 tick={} elapsed_us={} surface_frames={} pending_task=false application_idle=true unchanged=true\n"),
            RC::to_generic_string(request_.run_id),held.tick,elapsed,held.surface_frames-historical_hold_frames_);
        application_hold_completed_=held.completed_applications;
        world_resume_started_=world_resume_previous_=std::chrono::steady_clock::now();
        world_resume_viewport_=viewport_frame_count_.load(std::memory_order_relaxed);
        interior_release_requested_=true;
        return true;
    }

    bool ParticleLifetimeRecovery() const {return request_.seek_advance_failure && !request_.seek_settlement_failure
        && request_.historical_anchor_tick==205 && request_.historical_advanced_tick==210 && request_.host_seek_target==220;}
    bool ExecutionFallback() const {return request_.checkpoint_fallback && request_.historical_advanced_tick==210;}
    std::uint32_t HostSeekFirstTarget() const {return request_.host_seek_first_target?request_.host_seek_first_target:request_.checkpoint_pair?210:request_.host_seek_target==208?209:208;}
    const ReplayHost::CheckpointHandle& HostSeekCheckpoint() const {return (request_.checkpoint_pair && !host_seek_repeated_) || (ExecutionFallback() && host_seek_selected_tick_==205) ? pair_checkpoint_ : combat_checkpoint_;}
    const ReplayTrajectorySample& HostSeekSample() const {return (request_.checkpoint_pair && !host_seek_repeated_) || (ExecutionFallback() && host_seek_selected_tick_==205) ? pair_sample_ : historical_target_sample_;}
    std::uint32_t HostSeekTarget() const {return request_.historical_single_step && interior_expected_tick_==209 ? 209 : request_.host_seek_repeat && !host_seek_repeated_ ? HostSeekFirstTarget() : request_.host_seek_target;}

    std::uint64_t HostSeekExpectedCheckpoint() const {
        if(host_seek_release_checkpoint_)return *host_seek_release_checkpoint_;
        const auto& checkpoint=HostSeekCheckpoint();
        return checkpoint?checkpoint->execution.tick:UINT64_MAX;
    }

    bool PrepareSeekReleaseReadback() {
        if(!host_seek_release_checkpoint_) {
            const auto& checkpoint=HostSeekCheckpoint();
            if(!checkpoint)return false;
            // Freeze our immutable checkpoint coordinate before dropping the
            // readback pins. Never derive this expectation from a later host
            // progress observation that it is meant to validate.
            host_seek_release_checkpoint_=checkpoint->execution.tick;
        }
        combat_checkpoint_.reset();pair_checkpoint_.reset();
        return true;
    }

    void ObserveHostSeek(const ReplayHost::SeekWitness& state,const ReplayHost::InteriorWitness& held)
    {
        // Keep the production owner intact for cleanup, but do not re-enter a
        // terminal observer failure or repeat its diagnostic on every update.
        if(state_==State::Failed)return;
        using Phase=ReplayHost::SeekPhase;
        host_seek_selected_tick_=state.checkpoint;
        if(ExecutionFallback() && state.execution_fallbacks && !host_seek_execution_fallback_seen_) {
            ReplayTrajectorySample recovered{};
            if(state.execution_fallbacks!=1 || state.checkpoint!=170 || state.undo_tick!=210
                || state.commit_decided || state.phase!=Phase::Preparing || held.tick!=210
                || !held.application_idle || held.pending_task || state.fallback_execution_ticks<=5
                || state.fallback_execution_ticks>15 || !state.fallback_execution_intervals
                || !ReadReplayTrajectory(battle_manager_,recovered,true) || recovered!=host_seek_original_sample_)
            {Fail("execution_fallback_complete_B_unproven");return;}
            host_seek_execution_fallback_seen_=true;
            historical_rewind_ticks_=210-combat_checkpoint_->execution.tick+state.fallback_execution_ticks;
            historical_rewind_intervals_=host_seek_original_interval_-combat_checkpoint_->execution.interval+state.fallback_execution_intervals;
            LogTrajectorySample(recovered,1,L"execution_fallback_recovered_B");
            Output::send<LogLevel::Default>(STR("[ReplayQualification] execution fallback B verified run_id={} tick=210 checkpoint=170 target=220 discarded_ticks={} discarded_intervals={} independent_original_equal=true commit_decided=false\n"),
                RC::to_generic_string(request_.run_id),state.fallback_execution_ticks,state.fallback_execution_intervals);
            pair_checkpoint_.reset(); // Host still owns deferred retirement of the displaced image.
        }
        if(request_.seek_preparation_failure && host_seek_repeated_ && state.phase==Phase::Failed) {
            const auto read=ResolveHorseModExport<bool (*)(ReplayHost::RestoreOperationAction,const ReplayHost::Checkpoint*,ReplayHost::RestoreOperationWitness*)>("horsemod_replay_restore_operation");
            ReplayHost::RestoreOperationWitness restore{};ReplayTrajectorySample original{};
            if(!read || !read(ReplayHost::RestoreOperationAction::Read,nullptr,&restore)
                || state.commit_decided || held.tick!=220 || restore.original_tick!=220 || restore.target_tick!=205
                || !ReadReplayTrajectory(battle_manager_,original,true) || original!=interior_sample_)
            {Fail("failed_preparation_changed_original");return;}
            if(held.surface_pending || restore.pending) return;
            if(restore.phase!=ReplayHost::RestoreOperationPhase::Failed || historical_cancel_requested_)
            {Fail("failed_preparation_not_retired");return;}
            if(host_seek_failure_observed_) return;
            host_seek_failure_observed_=true;
            host_seek_phase_=Phase::Failed;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek phase run_id={} phase=11 pending=true commit_decided=false\n"),RC::to_generic_string(request_.run_id));
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek preparation failure observed run_id={} tick=220 unchanged_B=true before_publication=true\n"),RC::to_generic_string(request_.run_id));
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancel control waiting run_id={} tick=220\n"),RC::to_generic_string(request_.run_id));
            return;
        }
        if(request_.seek_preparation_failure && state.phase==Phase::Recovering && !historical_cancel_requested_) {
            if(!host_seek_failure_observed_ || held.tick!=220 || held.ui_cancel_requests!=1)
            {Fail("failed_preparation_cancel_control_unproven");return;}
            historical_cancel_requested_=true;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation injected run_id={} point=after tick=220 preparation_failed=true dispatch=host_ui\n"),RC::to_generic_string(request_.run_id));
        }
        if(request_.seek_settlement_failure && state.phase==Phase::Failed) {
            const auto read=ResolveHorseModExport<bool (*)(ReplayHost::RestoreOperationAction,const ReplayHost::Checkpoint*,ReplayHost::RestoreOperationWitness*)>("horsemod_replay_restore_operation");
            ReplayHost::RestoreOperationWitness restore{};ReplayTrajectorySample sample{};
            if(host_seek_failure_observed_) {
                if(state_==State::Failed)return;
                if(request_.probe_interactive_controls && !historical_cancel_requested_)return;
                if(read && read(ReplayHost::RestoreOperationAction::Read,nullptr,&restore))
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] late recovery failed participant={} code={} B_retained={}\n"),
                        RC::to_generic_string(restore.participant?restore.participant:"unknown"),static_cast<unsigned>(restore.failure),!restore.commit_decided);
                Fail("late_settlement_recovery_failed");return;
            }
            if(!host_seek_failure_injected_ || host_seek_failure_observed_ || !state.pending || state.commit_decided
                || state.failure!=Horse::Deterministic::FailureCode::AdvanceFailed || held.tick!=HostSeekTarget() || held.pending_task
                || held.boundary!=ReplayHost::PauseBoundary::CompletedApplication || !held.application_idle
                || !read || !read(ReplayHost::RestoreOperationAction::Read,nullptr,&restore)
                || restore.phase!=ReplayHost::RestoreOperationPhase::Failed || restore.pending || restore.original_tick!=request_.historical_advanced_tick
                || restore.ownership_phase!=Horse::Deterministic::ReplaySeekOwnership::Phase::FailedRecoverable
                || (request_.seek_drained_cancel && (!restore.participant || std::string_view(restore.participant)!="injected_render_drained"))
                || !ReadReplayTrajectory(battle_manager_,sample,true))
            {Fail("settlement_failure_complete_B_or_quiescence_unproven");return;}
            host_seek_failure_observed_=true;host_seek_phase_=state.phase;
            LogTrajectorySample(sample,1,L"historical_settlement_failure");
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek phase run_id={} phase=11 pending=true commit_decided=false\n"),RC::to_generic_string(request_.run_id));
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek settlement failure observed run_id={} tick={} undo_tick={} pending_task=false complete_B=true\n"),RC::to_generic_string(request_.run_id),HostSeekTarget(),request_.historical_advanced_tick);
            if(request_.probe_interactive_controls) {
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancel control waiting run_id={} tick={}\n"),RC::to_generic_string(request_.run_id),HostSeekTarget());
                return;
            }
            if(!CancelHostSeek())return;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation injected run_id={} point=after tick={} settlement_failed=true\n"),RC::to_generic_string(request_.run_id),HostSeekTarget());
            return;
        }
        if(request_.seek_settlement_failure && request_.probe_interactive_controls
            && state.phase==Phase::Recovering && !historical_cancel_requested_) {
            if(!host_seek_failure_observed_ || held.tick!=214 || held.ui_cancel_requests!=1 || state.commit_decided)
            {Fail("late_recovery_cancel_control_unproven");return;}
            historical_cancel_requested_=true;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation injected run_id={} point=after tick=214 settlement_failed=true dispatch=host_ui\n"),RC::to_generic_string(request_.run_id));
        }
        if(request_.consumer_mutation && state.phase==Phase::Failed) {
            const auto read=ResolveHorseModExport<bool (*)(ReplayHost::RestoreOperationAction,const ReplayHost::Checkpoint*,ReplayHost::RestoreOperationWitness*)>("horsemod_replay_restore_operation");
            ReplayHost::RestoreOperationWitness restore{};
            if(host_seek_failure_observed_) {
                // A second Failed state is a recovery failure. Read its actual
                // witness before stopping; it cannot repeat mutation admission
                // or issue another cancellation, and is never B recovery proof.
                const bool available=read && read(ReplayHost::RestoreOperationAction::Read,nullptr,&restore);
                Output::send<LogLevel::Warning>(STR("[ReplayQualification] consumer recovery failed run_id={} seek_failure={} seek_pending={} seek_commit_decided={} undo_tick={} current_tick={} pending_task={} application_idle={} restore_available={} restore_phase={} restore_failure={} restore_pending={} original_recovered={} restore_commit_decided={} original_tick={} target_tick={} ownership_phase={} executed_ticks={} executed_intervals={} participant={}\n"),
                    RC::to_generic_string(request_.run_id),static_cast<unsigned>(state.failure),state.pending,state.commit_decided,
                    state.undo_tick,held.tick,held.pending_task,held.application_idle,available,
                    static_cast<unsigned>(restore.phase),static_cast<unsigned>(restore.failure),restore.pending,restore.original_recovered,
                    restore.commit_decided,restore.original_tick,restore.target_tick,
                    restore.ownership_phase?static_cast<int>(*restore.ownership_phase):-1,restore.executed_ticks,restore.executed_intervals,
                    RC::to_generic_string(available && restore.participant?restore.participant:"unavailable"));
                Fail("consumer_mutation_recovery_failed");return;
            }
            if(!boundary_observer_.ConsumerMutationInjected() || !state.pending || state.commit_decided
                || state.failure!=Horse::Deterministic::FailureCode::UnsupportedContent || held.tick<=210 || held.tick>217
                || held.pending_task || !held.application_idle || held.boundary!=ReplayHost::PauseBoundary::CompletedApplication
                || !read || !read(ReplayHost::RestoreOperationAction::Read,nullptr,&restore)
                || restore.phase!=ReplayHost::RestoreOperationPhase::Failed || restore.pending || restore.original_tick!=217
                || restore.ownership_phase!=Horse::Deterministic::ReplaySeekOwnership::Phase::FailedRecoverable
                || !restore.participant || std::string_view(restore.participant)!="execution_consumer_prerequisite_changed")
            {Fail("consumer_mutation_complete_application_or_B_unproven");return;}
            host_seek_failure_observed_=true;host_seek_phase_=state.phase;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek phase run_id={} phase=11 pending=true commit_decided=false\n"),RC::to_generic_string(request_.run_id));
            Output::send<LogLevel::Default>(STR("[ReplayQualification] consumer mutation failure observed run_id={} tick={} undo_tick=217 pending_task=false complete_B=true\n"),RC::to_generic_string(request_.run_id),held.tick);
            if(!CancelHostSeek())return;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation injected run_id={} point=after tick={} consumer_failed=true\n"),RC::to_generic_string(request_.run_id),held.tick);
            return;
        }
        if(ParticleLifetimeRecovery() && state.phase==Phase::Failed) {
            const auto read=ResolveHorseModExport<bool (*)(ReplayHost::RestoreOperationAction,const ReplayHost::Checkpoint*,ReplayHost::RestoreOperationWitness*)>("horsemod_replay_restore_operation");
            ReplayHost::RestoreOperationWitness restore{};
            if(host_seek_failure_observed_ || host_seek_failure_injected_ || !state.pending || state.commit_decided
                || state.failure!=Horse::Deterministic::FailureCode::UnsupportedContent || held.tick<=205 || held.tick>220
                || held.pending_task || !held.application_idle || held.boundary!=ReplayHost::PauseBoundary::CompletedApplication
                || !read || !read(ReplayHost::RestoreOperationAction::Read,nullptr,&restore)
                || restore.phase!=ReplayHost::RestoreOperationPhase::Failed || restore.pending || restore.original_tick!=210
                || restore.ownership_phase!=Horse::Deterministic::ReplaySeekOwnership::Phase::FailedRecoverable
                || !restore.participant || std::string_view(restore.participant)!="execution_protected_particle_lifetime")
            {Fail("particle_lifetime_abort_or_complete_B_unproven");return;}
            host_seek_failure_observed_=true;host_seek_phase_=state.phase;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek phase run_id={} phase=11 pending=true commit_decided=false\n"),RC::to_generic_string(request_.run_id));
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek lifetime failure observed run_id={} tick={} undo_tick=210 pending_task=false complete_B=true injected=false\n"),RC::to_generic_string(request_.run_id),held.tick);
            if(!CancelHostSeek())return;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation injected run_id={} point=after tick={} lifetime_failed=true\n"),RC::to_generic_string(request_.run_id),held.tick);
            return;
        }
        if(request_.seek_advance_failure && !request_.seek_settlement_failure && !ParticleLifetimeRecovery() && state.phase==Phase::Failed) {
            if(host_seek_failure_observed_) {
                if(state_!=State::Failed) Output::send<LogLevel::Warning>(STR("[ReplayQualification] post-advance recovery rejected failure={} pending={} commit_decided={} undo_tick={} current={}\n"),
                    static_cast<unsigned>(state.failure),state.pending,state.commit_decided,state.undo_tick,held.tick);
                Fail("post_advance_recovery_failed");return;
            }
            const auto read=ResolveHorseModExport<bool (*)(ReplayHost::RestoreOperationAction,const ReplayHost::Checkpoint*,ReplayHost::RestoreOperationWitness*)>("horsemod_replay_restore_operation");
            ReplayHost::RestoreOperationWitness restore{};ReplayTrajectorySample sample{};
            if(!host_seek_failure_injected_ || host_seek_failure_observed_ || !state.pending || state.commit_decided
                || state.failure!=Horse::Deterministic::FailureCode::AdvanceFailed || held.tick!=208 || !held.pending_task
                || held.boundary!=ReplayHost::PauseBoundary::SimulationTick || held.application_idle
                || !read || !read(ReplayHost::RestoreOperationAction::Read,nullptr,&restore)
                || restore.phase!=ReplayHost::RestoreOperationPhase::Held || restore.pending || restore.original_tick!=request_.historical_advanced_tick
                || restore.ownership_phase!=Horse::Deterministic::ReplaySeekOwnership::Phase::ExecutionActive
                || !ReadReplayTrajectory(battle_manager_,sample,true))
            {Fail("advance_failure_complete_B_or_external_C_unproven");return;}
            host_seek_failure_observed_=true;host_seek_phase_=state.phase;
            LogTrajectorySample(sample,1,L"historical_advance_failure");
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek phase run_id={} phase=11 pending=true commit_decided=false\n"),RC::to_generic_string(request_.run_id));
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek advance failure observed run_id={} tick=208 undo_tick={} pending_task=true complete_B=true\n"),RC::to_generic_string(request_.run_id),request_.historical_advanced_tick);
            if(!CancelHostSeek()) return;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation injected run_id={} point=after tick=208 advance_failed=true\n"),RC::to_generic_string(request_.run_id));
            return;
        }
        if(request_.seek_observer_failure && state.phase==Phase::Failed) {
            const auto read=ResolveHorseModExport<bool (*)(ReplayHost::RestoreOperationAction,const ReplayHost::Checkpoint*,ReplayHost::RestoreOperationWitness*)>("horsemod_replay_restore_operation");
            ReplayHost::RestoreOperationWitness restore{};
            if(!host_seek_failure_injected_ || host_seek_failure_observed_ || !state.pending || state.commit_decided
                || state.failure!=Horse::Deterministic::FailureCode::AdvanceFailed || held.tick!=205
                || !read || !read(ReplayHost::RestoreOperationAction::Read,nullptr,&restore)
                || restore.phase!=ReplayHost::RestoreOperationPhase::Held || restore.pending)
            {Fail("failed_seek_transaction_not_recoverable");return;}
            host_seek_failure_observed_=true;host_seek_phase_=state.phase;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek phase run_id={} phase=11 pending=true commit_decided=false\n"),RC::to_generic_string(request_.run_id));
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek failed wrapper observed run_id={} target=205 undo_tick={} underlying_held=true complete_B=true\n"),RC::to_generic_string(request_.run_id),state.undo_tick);
            if(!CancelHostSeek()) return;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation injected run_id={} point=after tick=205 failed_wrapper=true\n"),RC::to_generic_string(request_.run_id));
            return;
        }
        if(request_.historical_single_step && state.target==209 && interior_expected_tick_==208) {
            if(state.phase!=Phase::Advancing || held.tick!=208 || held.ui_step_requests!=(request_.completion_repeat?0:1) || !historical_step_control_waiting_)
            {Fail("host_retained_step_dispatch_invalid");return;}
            if(!request_.completion_repeat) Output::send<LogLevel::Default>(STR("[ReplayQualification] historical step control verified run_id={} tick=208 requests=1 pending_event=true unchanged=true\n"),RC::to_generic_string(request_.run_id));
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical exact held run_id={} tick=208 pending_world=true pending_task=true\n"),RC::to_generic_string(request_.run_id));
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical single step requested run_id={} origin=208 target=209 dispatch={} B_retained=true\n"),RC::to_generic_string(request_.run_id),request_.completion_repeat?STR("host_api"):STR("host_ui"));
            historical_restore_started_=std::chrono::steady_clock::now(); // Step request latency excludes the prior deliberate hold.
            interior_expected_tick_=209;historical_exact_started_=false;historical_exact_held_=false;
            interior_release_requested_=false;interior_started_=false;
        }
        if(state.phase==Phase::Failed || state.target!=HostSeekTarget() || HostSeekExpectedCheckpoint()==UINT64_MAX || state.checkpoint!=HostSeekExpectedCheckpoint()
            || state.owned_bytes>Horse::Deterministic::Schema::replay_timeline_memory_limit-presentation_observer_.ReservationBytes())
        {
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek rejection phase={} failure={} target={} expected_target={} checkpoint={} expected_checkpoint={} owned_bytes={} observer_bytes={} pending={} commit_decided={}\n"),
                static_cast<unsigned>(state.phase),static_cast<unsigned>(state.failure),state.target,HostSeekTarget(),
                state.checkpoint,HostSeekExpectedCheckpoint(),
                state.owned_bytes,presentation_observer_.ReservationBytes(),state.pending,state.commit_decided);
            ReplayHost::RestoreOperationWitness recovery{};
            const auto read_recovery=ResolveHorseModExport<bool (*)(ReplayHost::RestoreOperationAction,const ReplayHost::Checkpoint*,ReplayHost::RestoreOperationWitness*)>("horsemod_replay_restore_operation");
            if(read_recovery && read_recovery(ReplayHost::RestoreOperationAction::Read,nullptr,&recovery))
                Output::send<LogLevel::Default>(STR("[ReplayQualification] host restore rejection phase={} failure={} participant={} pending={} recovered={} ownership={} original={} target={}\n"),
                    static_cast<unsigned>(recovery.phase),static_cast<unsigned>(recovery.failure),
                    RC::to_generic_string(recovery.participant?recovery.participant:"none"),recovery.pending,recovery.original_recovered,
                    recovery.ownership_phase?static_cast<unsigned>(*recovery.ownership_phase):UINT_MAX,recovery.original_tick,recovery.target_tick);
            Fail("host_seek_failed");return;
        }
        const bool changed=state.phase!=host_seek_phase_;
        if(changed) {
            host_seek_phase_=state.phase;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek phase run_id={} phase={} pending={} commit_decided={}\n"),
                RC::to_generic_string(request_.run_id),static_cast<unsigned>(state.phase),state.pending,state.commit_decided);
        }
        if(changed && host_seek_repeated_ && state.phase==Phase::Preparing) {
            const auto counters=ResolveHorseModExport<bool (*)(std::uint64_t*,std::size_t)>("horsemod_get_replay_executor_status");
            std::array<std::uint64_t,12> execution{};ReplayTrajectorySample original{};
            if(state.undo_tick!=HostSeekFirstTarget() || !counters || !counters(execution.data(),execution.size())
                || execution[2]!=HostSeekFirstTarget() || execution[3]<combat_checkpoint_->execution.interval
                || !ReadReplayTrajectory(battle_manager_,original,true) || original.frame!=HostSeekFirstTarget())
            {Fail("host_seek_repeat_completed_origin_invalid");return;}
            host_seek_rewind_ticks_=execution[2]-combat_checkpoint_->execution.tick;
            host_seek_rewind_intervals_=execution[3]-combat_checkpoint_->execution.interval;
            LogTrajectorySample(original,1,L"historical_repeat_original");
            interior_sample_=original;
            host_seek_original_sample_=original;
        }
        if(request_.historical_cancel=="before" && (!request_.host_seek_repeat || host_seek_repeated_) && state.phase==Phase::Preparing && !historical_cancel_requested_) {
            using Copy=Horse::Deterministic::Sc6ReplayParticleCopy;
            const auto image=ResolveHorseModExport<bool (*)(ReplayHost::ParticleCopyAction,Copy::Witness*,bool*)>("horsemod_particle_copy_experiment");
            Copy::Witness copy{};bool pending{};
            if(!image || !image(ReplayHost::ParticleCopyAction::Read,&copy,&pending)) {Fail("host_cancel_preparation_unreadable");return;}
            if(pending || copy.pending) {
                if(!CancelHostSeek()) return;
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation injected run_id={} point=before tick={} preparation_pending=true render_command_pending={} gpu_completion_pending={}\n"),
                    RC::to_generic_string(request_.run_id),held.tick,pending,!pending && copy.pending);
            }
        }
        if(state.phase==Phase::Cancelled) {
            ReplayTrajectorySample sample{};
            if(!historical_cancel_requested_ || state.pending || !state.original_recovered
                || state.undo_tick!=(request_.host_seek_repeat?HostSeekFirstTarget():request_.historical_advanced_tick) || held.tick!=state.undo_tick
                || !host_seek_original_sample_ || !ReadReplayTrajectory(battle_manager_,sample,true) || sample!=*host_seek_original_sample_)
            {
                if(sample.frame)LogTrajectorySample(sample,1,L"historical_recovery_mismatch");
                Fail("host_cancel_B_recovery_mismatch");return;
            }
            if(changed) {
                if(request_.seek_advance_failure || request_.completion_repeat || request_.consumer_mutation) {
                    if(((request_.seek_advance_failure || request_.consumer_mutation) && !host_seek_failure_observed_) || state.recovered_execution_ticks<(request_.consumer_mutation?211:request_.seek_settlement_failure?HostSeekTarget():208)-request_.historical_anchor_tick || !state.recovered_execution_intervals)
                    {Fail("advance_failure_discarded_C_accounting_missing");return;}
                    historical_rewind_ticks_=state.recovered_execution_ticks;
                    historical_rewind_intervals_=state.recovered_execution_intervals;
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] historical execution rewind run_id={} ticks={} intervals={}\n"),RC::to_generic_string(request_.run_id),historical_rewind_ticks_,historical_rewind_intervals_);
                }

                LogTrajectorySample(sample,1,L"historical_recovered");
                // C completed real application updates before B was restored.
                // The next update is relative to this recovered physical
                // boundary, not the old pre-seek epoch. Gameplay tick stays B.
                interior_epoch_=held.epoch;
                application_hold_completed_=held.completed_applications;
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation recovered run_id={} point={} tick={}\n"),
                    RC::to_generic_string(request_.run_id),RC::to_generic_string(request_.historical_cancel),held.tick);
                world_pause_completed_=true;world_resume_measured_=false;world_resume_frame_=held.tick;
                world_resume_started_=world_resume_previous_=std::chrono::steady_clock::now();
                world_resume_viewport_=viewport_frame_count_.load(std::memory_order_relaxed);
                world_resume_max_gap_us_=world_resume_late_gaps_=0;interior_release_requested_=true;
            }
            if(request_.seek_settlement_failure && request_.probe_interactive_controls) {
                if(changed) {
                    const auto monitor=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
                    if(!monitor || !monitor(this,[](void* context,const ReplayHost::InteriorWitness& pause) {
                        static_cast<ReplayQualificationMod*>(context)->ObserveRecoveredUiResume(pause);
                    })) {Fail("recovered_resume_monitor_rejected");return;}
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] historical resume control waiting run_id={} tick=220 recovered_B=true\n"),RC::to_generic_string(request_.run_id));
                }
                return;
            }
            ReleaseHostSeekAndResume(held);return;
        }
        if(changed && state.phase==Phase::AwaitingCommit) {
            ReplayTrajectorySample sample{};
            if(!ReadReplayTrajectory(battle_manager_,sample,true) || sample!=HostSeekSample() || held.tick!=state.checkpoint)
            {Fail("host_seek_published_target_mismatch");return;}
            const auto restore=ResolveHorseModExport<bool (*)(ReplayHost::RestoreOperationAction,const ReplayHost::Checkpoint*,ReplayHost::RestoreOperationWitness*)>("horsemod_replay_restore_operation");
            ReplayHost::RestoreOperationWitness native_restore{};
            if(!restore || !restore(ReplayHost::RestoreOperationAction::Read,nullptr,&native_restore)
                || native_restore.phase!=ReplayHost::RestoreOperationPhase::Held || native_restore.pending
                || (request_.historical_anchor_tick<=205 && request_.historical_advanced_tick!=2504 && native_restore.particle_birth!=(state.undo_tick>=(request_.historical_anchor_tick==170?177:210))))
            {Fail("host_seek_topology_witness_invalid");return;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical topology admitted run_id={} target_tick={} original_tick={} particle_birth={}\n"),
                RC::to_generic_string(request_.run_id),state.checkpoint,state.undo_tick,native_restore.particle_birth);
            if(state.checkpoint==170) {
                if(request_.historical_advanced_tick!=2504 && native_restore.particle_birth_count<2) {Fail("anchor_multiple_births_not_exercised");return;}
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical anchor topology run_id={} target={} original={} births={} all_owners_validated=true\n"),
                    RC::to_generic_string(request_.run_id),state.checkpoint,state.undo_tick,native_restore.particle_birth_count);
            }
            using Copy=Horse::Deterministic::Sc6ReplayParticleCopy;
            const auto image=ResolveHorseModExport<bool (*)(ReplayHost::ParticleCopyAction,Copy::Witness*,bool*)>("horsemod_particle_copy_experiment");
            Copy::Witness copy{};bool pending{};
            if(!image || !image(ReplayHost::ParticleCopyAction::Read,&copy,&pending) || pending || !copy.scene_fields_empty || !copy.scene_field_checks)
            {Fail("host_seek_publication_witness_unavailable");return;}
            historical_target_held_=true;
            LogTrajectorySample(sample,1,L"historical_restored");
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical scene fields run_id={} empty=true slots={} checks={} policy=reject_nonempty_v1\n"),
                RC::to_generic_string(request_.run_id),copy.scene_field_slots,copy.scene_field_checks);
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical restore cost run_id={} elapsed_us={} owned_bytes={} diagnostic_gpu_readbacks={} transaction_readback_maps={} readback_scope=observer_and_transaction includes_B_undo=true includes_image_reopen=true request_origin=held_B owned_bytes_kind=conservative_reservation\n"),
                RC::to_generic_string(request_.run_id),std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-historical_restore_started_).count(),state.owned_bytes,request_.pixel_diagnostics || request_.coherence_images,copy.readback_maps);
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek publication observed run_id={} tick={}\n"),RC::to_generic_string(request_.run_id),state.checkpoint);
            if(request_.historical_cancel=="after" && !request_.seek_advance_failure && !request_.consumer_mutation && !request_.seek_publication_cancel && !request_.completion_repeat && (!request_.host_seek_repeat || host_seek_repeated_)) {
                if(request_.seek_observer_failure) {
                    if(host_seek_failure_injected_) {Fail("seek_observer_failure_duplicated");return;}
                    host_seek_failure_injected_=true;
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek observer failure injected run_id={} target=205 after_publication=true\n"),RC::to_generic_string(request_.run_id));
                    throw std::uint32_t{0x5345454b}; // Caught by the host's existing observer boundary.
                }
                if(!CancelHostSeek()) return;
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation injected run_id={} point=after tick={}\n"),RC::to_generic_string(request_.run_id),state.checkpoint);
                return;
            }
        }
        if(changed && state.phase==Phase::Restored) {
            if(request_.consumer_mutation) {
                const auto query=ResolveHorseModExport<ReplayBoundaryObserver::ConsumerMutationQuery>("horsemod_read_consumer_mutation_candidate");
                if(!boundary_observer_.ArmConsumerMutation(query)) {Fail("consumer_mutation_arm_failed");return;}
                Output::send<LogLevel::Default>(STR("[ReplayQualification] consumer mutation armed run_id={} checkpoint=210 original=217 target=217\n"),RC::to_generic_string(request_.run_id));
            }
            if(request_.seek_publication_cancel) {
                if(state.commit_decided || held.tick!=state.checkpoint || !held.application_idle || held.pending_task
                    || !CancelHostSeek()) {Fail("publication_handoff_cancel_rejected");return;}
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical cancellation injected run_id={} point=after tick={} execution_handed_off=true simulation_ticks=0\n"),
                    RC::to_generic_string(request_.run_id),state.checkpoint);
                return;
            }
            if(host_seek_repeated_) {
                historical_birth_reproduced_=false;
                historical_rewind_ticks_+=host_seek_rewind_ticks_;
                historical_rewind_intervals_+=host_seek_rewind_intervals_;
            }
            historical_execution_started_=true;
            if(request_.corrected_inputs && !(request_.source_revision?ApplyAuthoredSourceRevision(true):boundary_observer_.ActivateChangedInputs()))
            {Fail("host_seek_source_revision_rejected");return;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical combat execution admitted run_id={} from_tick={} to_tick={} B_retained=true\n"),RC::to_generic_string(request_.run_id),state.undo_tick,state.checkpoint);
            if(ParticleLifetimeRecovery()) Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek lifetime failure watching run_id={} checkpoint=205 target=220 injection=false\n"),RC::to_generic_string(request_.run_id));
            if(request_.seek_advance_failure && !ParticleLifetimeRecovery()) {
                const auto seek=ResolveHorseModExport<bool (*)(ReplayHost::SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,ReplayHost::SeekWitness*,void*,ReplayHost::SeekObserver)>("horsemod_replay_seek_operation");
                ReplayHost::SeekWitness armed{};
                if(host_seek_failure_injected_ || !seek || !seek(request_.seek_settlement_failure?ReplayHost::SeekAction::InjectSettlementFailure:ReplayHost::SeekAction::InjectAdvanceFailure,nullptr,request_.seek_settlement_failure?(request_.seek_drained_cancel?0:11):208,&armed,nullptr,nullptr))
                {Fail("advance_failure_arm_rejected");return;}
                host_seek_failure_injected_=true;
                if(request_.seek_settlement_failure) Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek settlement failure armed run_id={} participants={} target={}\n"),RC::to_generic_string(request_.run_id),request_.seek_drained_cancel?0:11,HostSeekTarget());
                else Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek advance failure armed run_id={} tick=208 target={}\n"),RC::to_generic_string(request_.run_id),state.target);
            }
            historical_exact_requested_=true;interior_expected_tick_=HostSeekTarget();interior_release_requested_=true;
            if(state.target==state.checkpoint)
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical checkpoint landing requested run_id={} origin={} target={} resimulated_ticks=0\n"),RC::to_generic_string(request_.run_id),state.checkpoint,state.target);
        }
        if(changed && state.phase==Phase::Advancing)
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical exact requested run_id={} origin={} target={}\n"),RC::to_generic_string(request_.run_id),state.checkpoint,state.target);
        if(state.phase!=Phase::Held) return;
        if(request_.consumer_mutation) {Fail("consumer_mutation_not_enforced_before_target");return;}
        if(ExecutionFallback() && (!host_seek_execution_fallback_seen_ || state.execution_fallbacks!=1 || state.checkpoint!=170))
        {Fail("execution_fallback_not_exercised");return;}
        if(host_seek_release_waiting_) {
            if(state.pending) return;
            if(!request_.complete_seek_application && !(request_.host_seek_repeat && !host_seek_repeated_)) {ReleaseHostSeekAndResume(held);return;}
            if(!state.commit_decided || held.boundary!=ReplayHost::PauseBoundary::CompletedApplication || held.pending_task)
            {Fail("repeat_previous_commit_or_tails_incomplete");return;}
            host_seek_release_waiting_=false;
        }
        if(changed) {
            const auto seek=ResolveHorseModExport<bool (*)(ReplayHost::SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,ReplayHost::SeekWitness*,void*,ReplayHost::SeekObserver)>("horsemod_replay_seek_operation");
            ReplayHost::SeekWitness repeated{};ReplayTrajectorySample before{},after{};
            if(!ReadReplayTrajectory(battle_manager_,before,true) || !seek
                || !seek(ReplayHost::SeekAction::Begin,nullptr,state.target,&repeated,nullptr,nullptr)
                || repeated.phase!=Phase::Held || repeated.pending || repeated.target!=held.tick
                || repeated.origin!=state.origin || repeated.undo_tick!=state.undo_tick
                || !ReadReplayTrajectory(battle_manager_,after,true) || before!=after)
            {Fail("host_seek_identical_request_changed_hold");return;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek identical request run_id={} target={} unchanged=true checkpoint_argument=null\n"),
                RC::to_generic_string(request_.run_id),held.tick);
        }
        if(changed) Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek ready run_id={} target={} elapsed_us={} owned_bytes={} paused=true\n"),
            RC::to_generic_string(request_.run_id),held.tick,std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-historical_restore_started_).count(),state.owned_bytes);
        if(changed) Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek application cost run_id={} target={} prefix_us={} engine_us={} tail_us={} frame_sync_us={} sync_subset_of_tail=true includes_observers=true\n"),
            RC::to_generic_string(request_.run_id),held.tick,state.prefix_us,state.engine_us,state.tail_us,state.frame_sync_us);
        if(request_.complete_seek_application) {
            if(held.surface_pending) return;
            const auto seek=ResolveHorseModExport<bool (*)(ReplayHost::SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,ReplayHost::SeekWitness*,void*,ReplayHost::SeekObserver)>("horsemod_replay_seek_operation");
            const auto monitor=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
            ReplayHost::SeekWitness released{};
            if(!state.commit_decided) {
                // CompleteTarget is accepted intent. B stays owned through
                // native tails and commit. This cost protocol requests tails
                // explicitly, then waits for Release's completed retirement.
                if(!seek || !seek(ReplayHost::SeekAction::CompleteTarget,nullptr,0,&released,nullptr,nullptr)
                    || !released.pending || released.commit_decided)
                {Fail("cost_seek_target_completion_rejected");return;}
                host_seek_release_waiting_=true;
                Output::send<LogLevel::Default>(STR("[ReplayQualification] seek cost completion requested run_id={} origin={} deliberate_target_dwell=false\n"),RC::to_generic_string(request_.run_id),held.tick);
                return;
            }
            if(held.boundary!=ReplayHost::PauseBoundary::CompletedApplication || held.pending_task
                || !held.application_idle || !held.engine_idle || !held.world_idle || !held.arena_empty)
            {Fail("cost_seek_commit_precedes_native_tails");return;}
            if(!PrepareSeekReleaseReadback()){Fail("seek_release_readback_missing");return;}
            if(!seek || !monitor || !seek(ReplayHost::SeekAction::Release,nullptr,0,&released,nullptr,nullptr)
                || released.release_result!=ReplayHost::SeekReleaseResult::Completed) {host_seek_release_waiting_=true;return;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek released run_id={} tick={} surface_pending=false\n"),RC::to_generic_string(request_.run_id),held.tick);
            historical_operation_started_=false;combat_checkpoint_.reset();interior_started_=false;
            interior_release_requested_=false;
            if(!monitor(this,[](void* context,const ReplayHost::InteriorWitness& witness) {
                auto* self=static_cast<ReplayQualificationMod*>(context);
                if(!self->ObserveSeekApplicationCost(witness)) return;
                const auto clear=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
                const auto resume=ResolveHorseModExport<std::uint16_t (*)()>("horsemod_resume_replay_execution");
                if(!clear || !clear(nullptr,nullptr) || !resume || resume()) {self->Fail("cost_seek_resume_rejected");return;}
                Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek resume accepted run_id={} tick={}\n"),RC::to_generic_string(self->request_.run_id),witness.tick);
            })) {Fail("cost_seek_retirement_monitor_rejected");return;}
            return;
        }
        if(!historical_exact_held_ && !ObserveHistoricalExactHold(held)) return;
        if(request_.host_seek_repeat && !host_seek_repeated_) {
            const auto seek=ResolveHorseModExport<bool (*)(ReplayHost::SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,ReplayHost::SeekWitness*,void*,ReplayHost::SeekObserver)>("horsemod_replay_seek_operation");
            ReplayHost::SeekWitness next{};
            if(!state.commit_decided) {
                if(!seek || !seek(ReplayHost::SeekAction::CompleteTarget,nullptr,0,&next,nullptr,nullptr)) return;
                host_seek_release_waiting_=true;return;
            }
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical execution rewind run_id={} ticks={} intervals={}\n"),
                RC::to_generic_string(request_.run_id),historical_rewind_ticks_,historical_rewind_intervals_);
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical combat committed run_id={} from_tick={} to_tick={} target_tick={} tails_completed=true\n"),
                RC::to_generic_string(request_.run_id),state.undo_tick,state.checkpoint,held.tick);
            if(!seek || !seek(ReplayHost::SeekAction::Begin,request_.checkpoint_pair?combat_checkpoint_.get():nullptr,request_.host_seek_target,&next,this,
                [](void* context,const ReplayHost::SeekWitness& progress,const ReplayHost::InteriorWitness& pause) {
                    static_cast<ReplayQualificationMod*>(context)->ObserveHostSeek(progress,pause);
                }) || next.phase!=Phase::Preparing || next.origin!=HostSeekFirstTarget() || next.target!=request_.host_seek_target)
            {Fail("host_seek_repeat_request_rejected");return;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek repeated run_id={} checkpoint={} origin={} target={} pending_world=false pending_task=false previous_commit_complete=true\n"),RC::to_generic_string(request_.run_id),next.checkpoint,next.origin,next.target);
            host_seek_repeated_=true;
            host_seek_phase_=Phase::Idle; // Observe preparation of the fresh B separately.
            if(request_.checkpoint_pair) {
                // Begin transferred the host's old pin to completed-application
                // retirement. Drop only the caller's handle at this interior.
                pair_checkpoint_.reset();
                Output::send<LogLevel::Default>(STR("[ReplayQualification] checkpoint pair displaced run_id={} old=206 selected=205 caller_released=true host_retirement_required=true\n"),RC::to_generic_string(request_.run_id));
            }
            historical_restore_started_=std::chrono::steady_clock::now();
            historical_exact_started_=historical_exact_held_=interior_started_=interior_release_requested_=false;
            world_pause_completed_=world_resume_measured_=false;
            return;
        }
        ReleaseHostSeekAndResume(held);
    }

    bool ObserveSeekApplicationCost(const ReplayHost::InteriorWitness& held)
    {
        if(held.boundary!=ReplayHost::PauseBoundary::CompletedApplication || held.tick!=request_.host_seek_target
            || held.pending_task || !held.application_idle || !held.engine_idle || !held.world_idle || !held.arena_empty)
        {Fail("cost_seek_completion_changed_target_or_has_pending_work");return false;}
        if(held.surface_pending) return false;
        if(interior_release_requested_) return true;
        const auto read=ResolveHorseModExport<bool (*)(ReplayHost::TickAdvanceWitness*)>("horsemod_read_replay_tick");
        ReplayHost::TickAdvanceWitness progress{};ReplayTrajectorySample sample{};
        if(!read || !read(&progress) || progress.phase!=ReplayHost::TickAdvancePhase::Settled
            || progress.origin!=held.tick || progress.target!=held.tick || !ReadReplayTrajectory(battle_manager_,sample,true))
        {Fail("cost_seek_completed_state_unreadable");return false;}
        if(!interior_started_) {
            interior_started_=true;interior_sample_=sample;interior_epoch_=held.epoch;
            interior_boundaries_=boundary_samples_;interior_started_at_=std::chrono::steady_clock::now();
            historical_hold_frames_=held.surface_frames;
            LogTrajectorySample(sample,1,L"historical_cost_completed");
            Output::send<LogLevel::Default>(STR("[ReplayQualification] seek completed application cost run_id={} checkpoint={} target={} elapsed_us={} pending_task=false application_idle=true deliberate_target_dwell=false\n"),
                RC::to_generic_string(request_.run_id),request_.historical_anchor_tick,held.tick,
                std::chrono::duration_cast<std::chrono::microseconds>(interior_started_at_-historical_restore_started_).count());
            return false;
        }
        if(sample!=interior_sample_ || held.epoch!=interior_epoch_ || boundary_samples_!=interior_boundaries_)
        {Fail("cost_seek_completed_hold_advanced");return false;}
        const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-interior_started_at_).count();
        if(elapsed<500000 || held.surface_frames-historical_hold_frames_<30) return false;
        Output::send<LogLevel::Default>(STR("[ReplayQualification] seek cost completed hold run_id={} tick={} elapsed_us={} surface_frames={} unchanged=true\n"),
            RC::to_generic_string(request_.run_id),held.tick,elapsed,held.surface_frames-historical_hold_frames_);
        Output::send<LogLevel::Default>(STR("[ReplayQualification] historical exact held run_id={} tick={} pending_world=false pending_task=false\n"),RC::to_generic_string(request_.run_id),held.tick);
        historical_exact_held_=true;application_hold_completed_=held.completed_applications;
        world_pause_completed_=true;world_resume_measured_=false;world_resume_frame_=held.tick;
        world_resume_started_=world_resume_previous_=std::chrono::steady_clock::now();
        world_resume_viewport_=viewport_frame_count_.load(std::memory_order_relaxed);
        world_resume_max_gap_us_=world_resume_late_gaps_=0;interior_release_requested_=true;
        return true;
    }

    bool CancelHostSeek()
    {
        const auto seek=ResolveHorseModExport<bool (*)(ReplayHost::SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,ReplayHost::SeekWitness*,void*,ReplayHost::SeekObserver)>("horsemod_replay_seek_operation");
        ReplayHost::SeekWitness cancelling{};
        if(!seek || !seek(ReplayHost::SeekAction::Cancel,nullptr,0,&cancelling,nullptr,nullptr)
            || cancelling.commit_decided || !cancelling.pending)
        {Fail("host_seek_cancel_rejected");return false;}
        historical_cancel_requested_=true;return true;
    }

    void ObserveRecoveredUiResume(const ReplayHost::InteriorWitness& held)
    {
        if(state_==State::Failed)return;
        ReplayTrajectorySample sample{};ReplayHost::SeekWitness seek_state{};
        const auto seek=ResolveHorseModExport<bool (*)(ReplayHost::SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,ReplayHost::SeekWitness*,void*,ReplayHost::SeekObserver)>("horsemod_replay_seek_operation");
        if(!seek || !seek(ReplayHost::SeekAction::Read,nullptr,0,&seek_state,nullptr,nullptr)
            || seek_state.phase!=ReplayHost::SeekPhase::Cancelled || seek_state.pending
            || held.tick!=220 || held.pending_task || !held.application_idle || held.ui_resume_requests>1
            || !host_seek_original_sample_ || !ReadReplayTrajectory(battle_manager_,sample,true)
            || sample!=*host_seek_original_sample_)
        {Fail("recovered_UI_hold_changed");return;}
        if(!held.ui_resume_requests)return;
        Output::send<LogLevel::Default>(STR("[ReplayQualification] historical resume control verified run_id={} tick=220 requests=1 recovered_B=true\n"),RC::to_generic_string(request_.run_id));
        interior_epoch_=held.epoch;application_hold_completed_=held.completed_applications;
        world_resume_frame_=held.tick;world_resume_measured_=false;
        world_resume_started_=world_resume_previous_=std::chrono::steady_clock::now();
        world_resume_viewport_=viewport_frame_count_.load(std::memory_order_relaxed);
        world_resume_max_gap_us_=world_resume_late_gaps_=0;
        historical_operation_started_=false;combat_checkpoint_.reset();
        const auto monitor=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
        if(!monitor || !monitor(nullptr,nullptr)) {Fail("recovered_resume_monitor_retirement_failed");return;}
        // Only observe the click. The host dispatches Release/Resume after this
        // monitor returns, under the same admission used without an observer.
    }

    void ReleaseHostSeekAndResume(const ReplayHost::InteriorWitness& held)
    {
        // All readback comparisons have finished. The host still owns its
        // selected checkpoint and any B recovery transaction; releasing this
        // observer pin lets final host retirement include cold configurations.
        if(!PrepareSeekReleaseReadback()){Fail("seek_release_readback_missing");return;}
        const auto seek=ResolveHorseModExport<bool (*)(ReplayHost::SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,ReplayHost::SeekWitness*,void*,ReplayHost::SeekObserver)>("horsemod_replay_seek_operation");
        ReplayHost::SeekWitness released{};
        if(!seek || !seek(ReplayHost::SeekAction::Release,nullptr,0,&released,nullptr,nullptr)
            || released.release_result!=ReplayHost::SeekReleaseResult::Completed) {
            if(!host_seek_release_waiting_) Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek release waiting run_id={} tick={} surface_pending={} phase={} pending={} release_result={}\n"),
                RC::to_generic_string(request_.run_id),held.tick,held.surface_pending,static_cast<unsigned>(released.phase),released.pending,static_cast<unsigned>(released.release_result));
            host_seek_release_waiting_=true;
            return;
        }
        Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek released run_id={} tick={} surface_pending={}\n"),RC::to_generic_string(request_.run_id),held.tick,held.surface_pending);
        if(request_.host_seek && request_.historical_single_step) {
            ReplayTrajectorySample sample{};
            if(held.tick!=209 || held.boundary!=ReplayHost::PauseBoundary::CompletedApplication || held.pending_task
                || !ReadReplayTrajectory(battle_manager_,sample,true)) {Fail("host_step_completed_boundary_invalid");return;}
            LogTrajectorySample(sample,1,L"historical_settled");
            Output::send<LogLevel::Default>(STR("[ReplayQualification] host step completed run_id={} tick=209 native_tails_complete=true\n"),RC::to_generic_string(request_.run_id));
        }
        if(request_.historical_cancel.empty()) {
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical execution rewind run_id={} ticks={} intervals={}\n"),
                RC::to_generic_string(request_.run_id),host_seek_repeated_?host_seek_rewind_ticks_:historical_rewind_ticks_,host_seek_repeated_?host_seek_rewind_intervals_:historical_rewind_intervals_);
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical combat committed run_id={} from_tick={} to_tick={} target_tick={} tails_completed=true\n"),
                RC::to_generic_string(request_.run_id),released.undo_tick,released.checkpoint,held.tick);
        }
        interior_epoch_=held.epoch;application_hold_completed_=held.completed_applications;
        // Completion/commit is seek cost. Start playback measurement only
        // after its native tails and retirement, at the actual Resume boundary.
        world_resume_frame_=held.tick;world_resume_measured_=false;
        world_resume_started_=world_resume_previous_=std::chrono::steady_clock::now();
        world_resume_viewport_=viewport_frame_count_.load(std::memory_order_relaxed);
        world_resume_max_gap_us_=world_resume_late_gaps_=0;
        historical_operation_started_=false;combat_checkpoint_.reset();
        const auto resume=ResolveHorseModExport<std::uint16_t (*)()>("horsemod_resume_replay_execution");
        if(!resume || resume()) {Fail("host_seek_resume_rejected");return;}
        Output::send<LogLevel::Default>(STR("[ReplayQualification] host seek resume accepted run_id={} tick={}\n"),RC::to_generic_string(request_.run_id),held.tick);
    }

    void ObserveParticleCopyResume(const ReplayHost::InteriorWitness& held)
    {
        if (held.epoch == interior_epoch_) return;
        if (held.epoch != interior_epoch_ + 1 || held.completed_applications != application_hold_completed_ + 1)
        { Fail("particle_copy_resume_tail_mismatch"); return; }
        if (particle_copy_failed_) { Fail(historical_first_failure_.empty()
            ? std::string_view("particle_copy_failed_after_retirement_and_resume") : std::string_view(historical_first_failure_)); return; }
        // A second captured checkpoint advances through the host's own tick
        // request. EngineTickPost is too late to arm the adjacent application.
        if(UsesOwnedHistoricalAdvance() && application_reentry_armed_ && !pair_B_armed_) return;
        if (application_reentry_armed_)
        {
            interior_completed_ = true;
            if (request_.probe_historical_restore)
            {
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical combat resumed run_id={} held_tick={}\n"),
                    RC::to_generic_string(request_.run_id), request_.historical_cancel.empty() ? (request_.historical_exact_advance ? (request_.historical_single_step ? 209 : request_.host_seek ? request_.host_seek_target : 208) : 205) : request_.historical_advanced_tick);
                return;
            }
            Output::send<LogLevel::Default>(STR("[ReplayQualification] particle copy native playback resumed run_id={} held_tick=210\n"),
                RC::to_generic_string(request_.run_id));
            return;
        }
        const auto arm = ResolveHorseModExport<bool (*)(std::uint64_t, void*, ReplayHost::InteriorObserver)>("horsemod_arm_replay_application_pause");
        if (!arm || !arm(HasSecondCheckpoint()?SecondCheckpointTick():application_reentry_tick_, this, [](void* context, const ReplayHost::InteriorWitness& witness) {
            return static_cast<ReplayQualificationMod*>(context)->ObserveApplicationPause(witness);
        })) { Fail("particle_copy_second_hold_rejected"); return; }
        application_reentry_armed_ = true;
        particle_copy_started_ = interior_started_ = interior_release_requested_ = false;
    }

    bool ProbeMoveStateInterval(const ReplayTrajectorySample& before)
    {
        // A labelled native intervention, identical in control and executor.
        // Exercise the worker's pre-input traversal through the native setter;
        // never manufacture expected state or claim authored replay coverage.
        if (before.manager_phase != 2 || before.move_state != 0 || before.round_state != 2)
        { Fail("move_state_probe_requires_active_boundary"); return false; }
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] move state interval started run_id={} native_frame={} requested_state=3\n"),
            RC::to_generic_string(request_.run_id), before.frame);
        reinterpret_cast<void (*)(void*, std::uint8_t)>(base + 0x3f8370)(battle_manager_, 3);
        reinterpret_cast<void (*)(void*, float)>(base + 0x3fbf30)(battle_manager_, 0.0f);
        ReplayTrajectorySample after{};
        if (!ReadReplayTrajectory(battle_manager_, after) || after.frame != before.frame + 1
            || after.move_state != 0 || after.round != before.round || after.cursor != before.cursor
            || boundary_failed_)
        { Fail("move_state_probe_did_not_complete_one_traversal"); return false; }
        move_state_observed_ = true;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] move state interval completed run_id={} native_frame={} source_unchanged=true state=0\n"),
            RC::to_generic_string(request_.run_id), after.frame);
        return true;
    }

    bool ObserveSmallRestore()
    {
        using CaptureFn = std::uint16_t (*)(ReplayHost::Checkpoint*);
        using RestoreFn = std::uint16_t (*)(const ReplayHost::Checkpoint*);
        const auto capture = ResolveHorseModExport<CaptureFn>("horsemod_capture_replay_checkpoint");
        const auto restore = ResolveHorseModExport<RestoreFn>("horsemod_restore_replay_checkpoint");
        const auto restore_cancellable = ResolveHorseModExport<std::uint16_t (*)(
            const ReplayHost::Checkpoint*, const ReplayHost::RestoreControl*)>("horsemod_restore_replay_checkpoint_cancellable");
        const auto advance = ResolveHorseModExport<std::uint16_t (*)(std::uint64_t)>("horsemod_advance_replay_to_tick");
        if (!capture || !restore || !restore_cancellable || !advance) { Fail("small_restore_api_missing"); return false; }
        const auto started = std::chrono::steady_clock::now();
        const auto require = [&](std::uint16_t code, const char* phase) {
            if (!code) return true;
            Output::send<LogLevel::Warning>(STR("[ReplayQualification] small restore failed phase={} code={}\n"),
                RC::to_generic_string(phase), code);
            Fail("small_restore_operation_failed");
            return false;
        };
        auto checkpoint = std::make_unique<ReplayHost::Checkpoint>();
        auto trial = std::make_unique<ReplayHost::Checkpoint>();
        auto reproduced = std::make_unique<ReplayHost::Checkpoint>();
        ReplayTrajectorySample before{}, after{}, replayed{}, restored{};
        if (!ReadReplayTrajectory(battle_manager_, before, true) || before != interior_sample_)
        { Fail("small_restore_initial_observation_invalid"); return false; }
        Output::send<LogLevel::Default>(STR("[ReplayQualification] small restore started run_id={} from_tick=360 to_tick=361\n"),
            RC::to_generic_string(request_.run_id));
        if (!require(capture(checkpoint.get()), "capture")) return false;
        if (!ReadReplayTrajectory(battle_manager_, restored, true) || restored != before)
        { Fail("small_restore_capture_changed_gameplay"); return false; }
        ReplayHost::InteriorWitness world_witness{};
        const auto read_witness = ResolveHorseModExport<bool (*)(ReplayHost::InteriorWitness*)>("horsemod_get_replay_interior_witness");
        const auto image_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto world_bytes = checkpoint->world.owned_bytes();
        const auto timers = checkpoint->world.timer_count();
        const void* original_timer_storage{};
        const void* observed_timer_storage{};
        if (!read_witness || !read_witness(&world_witness) || !world_witness.world || !timers || !world_bytes
            || !ReadActiveTimerStorage(world_witness.world, original_timer_storage)
            || checkpoint->world.Capture(image_base, const_cast<void*>(world_witness.world), 0).code
                != Horse::Deterministic::FailureCode::CapacityExceeded
            || checkpoint->world.Restore(image_base, const_cast<void*>(world_witness.world), 0).code
                != Horse::Deterministic::FailureCode::CapacityExceeded
            || !ReadActiveTimerStorage(world_witness.world, observed_timer_storage)
            || observed_timer_storage != original_timer_storage
            || !checkpoint->world.ValidateHeld(image_base, const_cast<void*>(world_witness.world)).ok()
            || checkpoint->world.owned_bytes() != world_bytes || checkpoint->world.timer_count() != timers)
        { Fail("small_restore_world_ownership_or_capacity_failed"); return false; }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] small restore world captured run_id={} timers={} owned_bytes={} "
            "independent_callbacks=true capacity_rejected=true retained=true\n"),
            RC::to_generic_string(request_.run_id), timers, world_bytes);
        // Exercise a native CRT caller outside HorseMod's two diagnostic
        // callsites. Values are only compared, never fed into gameplay. Both
        // draws are undone from our checkpoint before the authored suffix.
        const auto native_rand = reinterpret_cast<int (*)()>(GetProcAddress(GetModuleHandleW(L"ucrtbase.dll"), "rand"));
        if (!native_rand) { Fail("small_restore_native_crt_missing"); return false; }
        const int first_draw = native_rand();
        if (!require(capture(reproduced.get()), "capture_native_crt_draw")) return false;
        if (reproduced->ucrt.state == checkpoint->ucrt.state)
        { Fail("small_restore_native_crt_not_observed"); return false; }
        if (!require(restore(checkpoint.get()), "restore_native_crt_draw")) return false;
        if (!ReadActiveTimerStorage(world_witness.world, observed_timer_storage)
            || observed_timer_storage == original_timer_storage)
        { Fail("small_restore_native_timer_storage_not_reconstructed"); return false; }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] small restore world rebuilt run_id={} old_storage={} new_storage={} "
            "restore_capacity_rejected=true\n"), RC::to_generic_string(request_.run_id),
            reinterpret_cast<std::uintptr_t>(original_timer_storage), reinterpret_cast<std::uintptr_t>(observed_timer_storage));
        const int second_draw = native_rand();
        if (first_draw != second_draw || !require(restore(checkpoint.get()), "restore_native_crt_reproduction")
            || !require(capture(reproduced.get()), "capture_native_crt_reproduction")
            || reproduced->ucrt != checkpoint->ucrt
            || reproduced->gameplay.canonical_hash != checkpoint->gameplay.canonical_hash
            || reproduced->gameplay.canonical_components != checkpoint->gameplay.canonical_components
            || !ReadReplayTrajectory(battle_manager_, restored, true) || restored != before
            || boundary_samples_ != interior_boundaries_)
        { Fail("small_restore_native_crt_reproduction_failed"); return false; }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] small restore native CRT verified run_id={} draws=2 native_equal=true restored=true\n"),
            RC::to_generic_string(request_.run_id));
        // Corrupt only checkpoint metadata, never live simulation or expected
        // trajectory data. Every rejection must leave the captured paused
        // state, execution coordinate and original pending task unchanged.
        const auto rejected = [&](std::uint16_t actual, Horse::Deterministic::FailureCode expected, const char* field) {
            if (actual != static_cast<std::uint16_t>(expected)
                || !require(capture(reproduced.get()), "capture_after_rejection")
                || reproduced->execution != checkpoint->execution
                || reproduced->task != checkpoint->task || reproduced->event != checkpoint->event
                || reproduced->ucrt != checkpoint->ucrt || reproduced->source != checkpoint->source
                || reproduced->gameplay.canonical_hash != checkpoint->gameplay.canonical_hash
                || reproduced->gameplay.canonical_components != checkpoint->gameplay.canonical_components
                || !ReadReplayTrajectory(battle_manager_, restored, true) || restored != before
                || boundary_samples_ != interior_boundaries_)
            { Fail("small_restore_rejection_changed_paused_state"); return false; }
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] small restore metadata rejected run_id={} field={} code={} unchanged=true pending_event=true\n"),
                RC::to_generic_string(request_.run_id), RC::to_generic_string(field), actual);
            return true;
        };
        ++checkpoint->execution.tick;
        auto rejection = restore(checkpoint.get());
        --checkpoint->execution.tick;
        if (!rejected(rejection, Horse::Deterministic::FailureCode::RestorePreflightFailed, "coordinate")) return false;
        ++checkpoint->ucrt.draws;
        rejection = restore(checkpoint.get());
        --checkpoint->ucrt.draws;
        if (!rejected(rejection, Horse::Deterministic::FailureCode::RestorePreflightFailed, "ucrt")) return false;
        const auto* original_task = checkpoint->task;
        checkpoint->task = nullptr;
        rejection = restore(checkpoint.get());
        checkpoint->task = original_task;
        if (!rejected(rejection, Horse::Deterministic::FailureCode::GenerationMismatch, "task")) return false;
        restore_trial_active_ = true;
        const auto advanced = advance(361);
        restore_trial_active_ = false;
        if (!require(advanced, "advance_trial")) return false;
        if (!ReadReplayTrajectory(battle_manager_, after, true) || after.frame != 361
            || restore_trial_boundaries_ != 2 || boundary_failed_)
        { Fail("small_restore_trial_observation_invalid"); return false; }
        restore_trial_ticks_ = 1;
        if (!require(capture(trial.get()), "capture_trial")) return false;
        struct CancellationObservation
        {
            RC::Unreal::UObject* manager{};
            ReplayHost::RestorePhase requested{};
            std::uint16_t (*advance)(std::uint64_t){};
            std::array<ReplayTrajectorySample, 2> samples{};
            unsigned calls{};
            bool readable{true}, nested_rejected{true};
        } cancellation;
        ReplayHost::RestoreControl control{};
        control.context = &cancellation;
        control.cancel_requested = [](void* context, ReplayHost::RestorePhase phase) noexcept {
            auto& observation = *static_cast<CancellationObservation*>(context);
            const auto index = static_cast<unsigned>(phase);
            ++observation.calls;
            observation.readable &= ReadReplayTrajectory(observation.manager, observation.samples[index], true);
            observation.nested_rejected &= observation.advance(361)
                == static_cast<std::uint16_t>(Horse::Deterministic::FailureCode::IllegalTransition);
            // The request depends only on phase. Observed/expected state never
            // chooses a repair or becomes input to the running simulation.
            return phase == observation.requested;
        };
        for (const auto phase : {ReplayHost::RestorePhase::BeforeWrites, ReplayHost::RestorePhase::BeforeCommit})
        {
            cancellation = {};
            cancellation.manager = battle_manager_;
            cancellation.requested = phase;
            cancellation.advance = advance;
            const auto cancelled = restore_cancellable(checkpoint.get(), &control);
            const bool after_writes = phase == ReplayHost::RestorePhase::BeforeCommit;
            if (cancelled != static_cast<std::uint16_t>(Horse::Deterministic::FailureCode::Cancelled)
                || cancellation.calls != (after_writes ? 2u : 1u)
                || !cancellation.readable || !cancellation.nested_rejected
                || cancellation.samples[0] != after || (after_writes && cancellation.samples[1] != before)
                || !require(capture(reproduced.get()), "capture_after_cancellation")
                || reproduced->execution != trial->execution || reproduced->source != trial->source
                || reproduced->ucrt != trial->ucrt || reproduced->task != trial->task || reproduced->event != trial->event
                || reproduced->gameplay.canonical_hash != trial->gameplay.canonical_hash
                || reproduced->gameplay.canonical_components != trial->gameplay.canonical_components
                || !ReadReplayTrajectory(battle_manager_, restored, true) || restored != after
                || boundary_samples_ != interior_boundaries_ || boundary_failed_)
            { Fail("small_restore_cancellation_did_not_recover_original_pause"); return false; }
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] small restore cancelled run_id={} phase={} original_tick=361 "
                "target_tick=360 queries={} target_observed={} original_restored=true nested_rejected=true pending_event=true\n"),
                RC::to_generic_string(request_.run_id), after_writes ? STR("before_commit") : STR("before_writes"),
                cancellation.calls, after_writes);
        }
        if (!require(restore(checkpoint.get()), "restore")) return false;
        if (!ReadReplayTrajectory(battle_manager_, restored, true) || restored != before
            || boundary_samples_ != interior_boundaries_)
        { Fail("small_restore_landing_diverged"); return false; }
        if (!require(advance(361), "reproduce") || !require(capture(reproduced.get()), "capture_reproduced")) return false;
        if (!ReadReplayTrajectory(battle_manager_, replayed, true) || replayed != after
            || trial->gameplay.canonical_hash != reproduced->gameplay.canonical_hash
            || trial->gameplay.canonical_components != reproduced->gameplay.canonical_components
            || boundary_samples_ != interior_boundaries_ + 2 || boundary_failed_)
        { Fail("small_restore_suffix_diverged"); return false; }
        // Only observer expectations change here. The simulation was restored
        // exclusively from its own captured checkpoint, never from these rows.
        interior_expected_tick_ = 361;
        interior_sample_ = replayed;
        interior_boundaries_ = boundary_samples_;
        small_restore_completed_ = true;
        std::ostringstream hash;
        hash << std::hex << std::setfill('0');
        for (auto byte : trial->gameplay.canonical_hash) hash << std::setw(2) << std::to_integer<unsigned>(byte);
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] small restore completed run_id={} from_tick=360 to_tick=361 trial_ticks=1 "
            "trial_boundaries=2 native_equal=true hash_equal=true components_equal=true canonical_hash={} elapsed_us={}\n"),
            RC::to_generic_string(request_.run_id), RC::to_generic_string(hash.str()), elapsed);
        return true;
    }

    void ObserveWorldPause()
    {
        ReplayTrajectorySample sample{};
        std::array<std::uint64_t, 3> status{};
        const auto read = ResolveHorseModExport<bool (*)(std::uint64_t*, std::size_t)>("horsemod_get_replay_host_status");
        if (!read || !read(status.data(), status.size()) || status[0] != 1 || status[2]
            || !ReadReplayTrajectory(battle_manager_, sample, true) || sample != trajectory_last_
            || boundary_samples_ != world_pause_boundaries_)
        { Fail("world_pause_state_advanced"); return; }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - world_pause_started_).count();
        if (elapsed < 1'000'000) return;
        const auto viewport = viewport_frame_count_.load(std::memory_order_relaxed) - world_pause_viewport_;
        const auto pause = ResolveHorseModExport<bool (*)(bool)>("horsemod_set_replay_world_paused");
        if (viewport < 30 || status[1] < 30 || !pause || !pause(false))
        { Fail("world_pause_resume_failed"); return; }
        world_pause_active_ = false;
        world_pause_completed_ = true;
        world_resume_started_ = world_resume_previous_ = std::chrono::steady_clock::now();
        world_resume_frame_ = sample.frame;
        world_resume_viewport_ = viewport_frame_count_.load(std::memory_order_relaxed);
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] world pause completed run_id={} native_frame={} elapsed_us={} viewport_frames={} held_updates={} unchanged=true resumed=true\n"),
            RC::to_generic_string(request_.run_id), sample.frame, elapsed, viewport, status[1]);
    }

    bool ApplyRollingAuthoredControl()
    {
        auto** field=battle_manager_->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(L"BattleReplayPlayer");
        if(!field || !IsLiveReplayObject(*field))return false;
        std::vector<ReplayRollingAuthoredWrite> writes;
        const auto read=[](std::uintptr_t address,auto& value){return ReadSchedulerValue(address,value);};
        if(!PrepareRollingAuthoredControl(request_.rolling_corrections,reinterpret_cast<std::uintptr_t>(*field),
            reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),read,writes))return false;
        // This independent native control is installed once after payload
        // verification, before any edited input is published or consumed.
        for(const auto& write:writes)*reinterpret_cast<std::uint32_t*>(write.address)=write.value;
        // Multiple revisions may intentionally replace the same sample.
        for(std::size_t i=0;i<writes.size();++i) {
            bool superseded=false;
            for(std::size_t j=i+1;j<writes.size();++j)if(writes[i].address==writes[j].address)superseded=true;
            std::uint32_t value{};
            if(!superseded && (!read(writes[i].address,value) || value!=writes[i].value))return false;
        }
        for(const auto& row:request_.rolling_corrections)
            Output::send<LogLevel::Default>(STR("[ReplayQualification] rolling authored control run_id={} arrival={} expected_revision={} round={} sample={} players={} raw0={} raw1={} native_source=true\n"),
                RC::to_generic_string(request_.run_id),row.arrival_tick,row.expected_revision,row.edit.round,row.edit.sample,
                row.edit.players,row.edit.raw[0],row.edit.raw[1]);
        return true;
    }

    bool ApplyAuthoredSourceRevision(bool runtime)
    {
        using Edit=Horse::Deterministic::ReplayInputOverride;
        const unsigned first=request_.source_revision_guard?201u:request_.corrected_inputs?161u:166u;
        const auto* policy=request_.source_revision_guard?L"authored_guard_201_230_v1":request_.corrected_inputs?L"authored_samples_161_190_v1":L"authored_samples_166_195_v1";
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        auto** field=battle_manager_->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(L"BattleReplayPlayer");
        if(!field || !IsLiveReplayObject(*field)) return false;
        const auto owner=reinterpret_cast<std::uintptr_t>(*field);
        std::uintptr_t tracker_vtable{},rounds{},recorders{},object{},vtable{},data{};
        int round_count{},recorder_count{},size{},capacity{},cursor{};unsigned recorded{};
        if(!ReadSchedulerValue(owner+0x390,tracker_vtable) || tracker_vtable!=base+0x3290d20
            || !ReadSchedulerValue(owner+0x3b8,rounds) || !rounds
            || !ReadSchedulerValue(owner+0x3c0,round_count) || round_count<1
            || !ReadSchedulerValue(owner+0x3a0,cursor) || cursor<0 || cursor>static_cast<int>(first)
            || !ReadSchedulerValue(rounds,recorders) || !recorders
            || !ReadSchedulerValue(rounds+8,recorder_count) || recorder_count!=2
            || !ReadSchedulerValue(recorders+4,recorded) || recorded<first+30
            || !ReadSchedulerValue(recorders+16,object) || !object
            || !ReadSchedulerValue(object,vtable) || vtable!=base+0x328e948
            || !ReadSchedulerValue(object+8,data) || !data
            || !ReadSchedulerValue(object+16,size) || size<static_cast<int>((first+30)*4) || (size&3)
            || !ReadSchedulerValue(object+20,capacity) || capacity<size) return false;
        std::array<Edit,30> edits{};std::array<unsigned,30> originals{};
        for(unsigned i=0;i<30;++i) {
            if(!ReadSchedulerValue(data+(first+i)*4,originals[i])) return false;
            edits[i].round=0;edits[i].sample=first+i;edits[i].players=1;
            // Native14036C4D0 publishes0x1008 for crouch guard. This spans
            // the recorded attack's next move-admission boundary around252.
            edits[i].raw[0]=(originals[i]&~0x3c0fu)|(request_.source_revision_guard?0x1008u:(i<15?0x401u:0u));
        }
        std::uint64_t revision{};
        if(runtime) {
            const auto revise=ResolveHorseModExport<std::uint16_t(*)(const Edit*,std::size_t,std::uint64_t,std::uint64_t*)>("horsemod_revise_replay_inputs");
            if(!revise || cursor<1 || cursor>static_cast<int>(first)) return false;
            // Rejection is mutation-free: the next accepted edit still requires
            // revision0. No historical cache slot is cleared or overwritten.
            auto late=edits[0];late.sample=static_cast<unsigned>(cursor-1);
            if(revise(&late,1,0,&revision)!=static_cast<std::uint16_t>(Horse::Deterministic::FailureCode::IllegalTransition)
                || revision!=0 || revise(edits.data(),edits.size(),0,&revision)!=0 || revision!=1) return false;
            for(unsigned i=0;i<30;++i) {
                unsigned original{};
                if(!ReadSchedulerValue(data+(first+i)*4,original) || original!=originals[i]) return false;
            }
        } else {
            // Independent control: alter authored samples before their first
            // native read. It has no runtime override and uses native caches.
            for(unsigned i=0;i<30;++i) *reinterpret_cast<unsigned*>(data+(first+i)*4)=edits[i].raw[0];
            for(unsigned i=0;i<30;++i) {
                unsigned value{};
                if(!ReadSchedulerValue(data+(first+i)*4,value) || value!=edits[i].raw[0]) return false;
            }
        }
        Output::send<LogLevel::Default>(STR("[ReplayQualification] source revision activated owner={} policy={} cursor={} revision={} late_edit_rejected={} native_recording_unchanged={}\n"),
            runtime?STR("runtime"):STR("control"),policy,cursor,revision,runtime,runtime);
        for(unsigned i=0;i<30;++i) Output::send<LogLevel::Default>(STR("[ReplayQualification] source revision sample policy={} round=0 sample={} slot=0 original={:08x} input={:08x}\n"),
            policy,edits[i].sample,originals[i],edits[i].raw[0]);
        return true;
    }

    bool ObserveCompletionRepeat(const ReplayHost::InteriorWitness& held)
    {
        const auto seek=ResolveHorseModExport<bool (*)(ReplayHost::SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,ReplayHost::SeekWitness*,void*,ReplayHost::SeekObserver)>("horsemod_replay_seek_operation");
        ReplayHost::SeekWitness state{};
        if(!seek || !seek(ReplayHost::SeekAction::Read,nullptr,0,&state,nullptr,nullptr)) {Fail("completion_repeat_read");return false;}
        if(completion_repeat_phase_==0) {
            std::uint8_t repeat{};
            if(completion_repeat_count_!=2 || !ReadSchedulerValue(reinterpret_cast<std::uintptr_t>(battle_manager_)+0x1462,repeat)
                || repeat!=1 || !held.pending_task || state.commit_decided
                || !seek(ReplayHost::SeekAction::CompleteTarget,nullptr,0,&state,nullptr,nullptr) || !state.pending)
            {Fail("completion_repeat_request");return false;}
            completion_repeat_phase_=1;return false;
        }
        if(completion_repeat_phase_==1) {
            if(state.pending) return false;
            ReplayTrajectorySample current{};const void* event{};
            if(!ReadPendingInteriorTask(battle_manager_,held,event) || state.release_result!=ReplayHost::SeekReleaseResult::RequiresResume || state.commit_decided || held.tick!=208
                || !held.pending_task || held.application_idle || !ReadReplayTrajectory(battle_manager_,current,true))
            {Fail("completion_repeat_not_deferred");return false;}
            completion_repeat_sample_=current;completion_repeat_epoch_=held.epoch;completion_repeat_event_=event;completion_repeat_task_=held.pending_task;
            completion_repeat_callbacks_=boundary_samples_;completion_repeat_updates_=held.application_updates;
            completion_repeat_frames_=held.surface_frames;completion_repeat_started_=std::chrono::steady_clock::now();
            completion_repeat_phase_=2;return false;
        }
        if(completion_repeat_phase_==2) {
            ReplayTrajectorySample current{};const void* event{};
            if(!ReadPendingInteriorTask(battle_manager_,held,event) || event!=completion_repeat_event_ || held.pending_task!=completion_repeat_task_
                || state.commit_decided || state.pending || held.tick!=208 || !held.pending_task || held.epoch!=completion_repeat_epoch_
                || !ReadReplayTrajectory(battle_manager_,current,true) || current!=completion_repeat_sample_
                || boundary_samples_!=completion_repeat_callbacks_) {Fail("completion_repeat_hold_advanced");return false;}
            const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-completion_repeat_started_).count();
            if(elapsed<500000 || held.application_updates-completion_repeat_updates_<30 || held.surface_frames-completion_repeat_frames_<20) return false;
            ReplayHost::SeekWitness rejected{};
            if(seek(ReplayHost::SeekAction::Release,nullptr,0,&rejected,nullptr,nullptr)
                || rejected.release_result!=ReplayHost::SeekReleaseResult::RequiresResume) {Fail("completion_repeat_release_not_rejected");return false;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] completion repeat held run_id={} tick=208 elapsed_us={} application_updates={} surface_frames={} unchanged=true pending_task=true B_retained=true callbacks_unchanged=true release_requires_resume=true\n"),
                RC::to_generic_string(request_.run_id),elapsed,held.application_updates-completion_repeat_updates_,held.surface_frames-completion_repeat_frames_);
            completion_repeat_phase_=3;
        }
        return true;
    }

    bool StartBoundaryObserver(std::uint32_t native_frame)
    {
        if (request_.skip_intros && !presentation_observer_.Start(this, [](void* context, std::uint32_t& tick) {
            auto* self=static_cast<ReplayQualificationMod*>(context);
            return IsLiveReplayObject(self->battle_manager_)
                && ReadSchedulerValue(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))+0x470d0c4,tick);
        },request_.run_id,ResolveHorseModExport<void (*)()>("horsemod_get_replay_host_status")==nullptr, request_.pixel_diagnostics, request_.pass_diagnostics,battle_manager_->GetWorld(),
            request_.coherence_images,QualificationRoot(),ResolveHorseModExport<ReplayPresentationObserver::SetOutputObserver>("horsemod_set_replay_output_observer"),(request_.observation_target?request_.observation_target:request_.source_revision_guard?300:request_.index_seek_target),request_.observation_target?120:request_.index_seek_continuation,request_.index_sequence!=0,request_.rolling_cycles!=0)) { Fail("native_presentation_start_failed"); return false; }
        if (!boundary_observer_.Start(battle_manager_, this, [](void* context, const wchar_t* event) {
            static_cast<ReplayQualificationMod*>(context)->ObserveNativeBoundary(event);
        }, [](void* context, void* actor, std::uintptr_t caller) {
            static_cast<ReplayQualificationMod*>(context)->ObserveActorFamily(actor, caller);
        }, request_.changed_inputs && !request_.source_revision, request_.corrected_inputs && !request_.source_revision, request_.corrected_inputs && request_.executor_control && !request_.source_revision, request_.skip_intros, request_.observation_target?request_.observation_target:request_.trajectory_to_end?request_.index_seek_target:0,request_.index_sequence!=0)) { Fail(boundary_observer_.error()); return false; }
        boundary_started_ = true;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] boundaries started run_id={} native_frame={}\n"),
            RC::to_generic_string(request_.run_id), native_frame);
        return true;
    }

    void ObserveActorFamily(void* actor, std::uintptr_t caller)
    {
        auto* object = static_cast<RC::Unreal::UObject*>(actor);
        if (!IsLiveReplayObject(object)) { boundary_failed_ = true; return; }
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto vtable = *reinterpret_cast<std::uintptr_t*>(actor) - base;
        const auto caller_rva = caller >= base && caller - base < 0x6000000 ? caller - base : UINTPTR_MAX;
        for (std::size_t i = 0; i < actor_family_count_; ++i)
        {
            auto& family = actor_families_[i];
            if (family.vtable == vtable && family.caller == caller_rva) { ++family.calls; return; }
        }
        if (actor_family_count_ == actor_families_.size()) { boundary_failed_ = true; return; }
        actor_families_[actor_family_count_++] = {vtable, caller_rva, 1};
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] actor family class={} vtable_rva={:x} caller_rva={:x}\n"),
            object->GetClassPrivate()->GetName(), vtable, caller_rva);
    }

    int consumed_sample_{-1},consumed_round_{-1};
    bool consumption_pending_{};
    void ObserveNativeBoundary(const wchar_t* event)
    {
        if (boundary_failed_) return;
        ReplayTrajectorySample sample{};
        if (boundary_samples_ >= 200000 || !ReadReplayTrajectory(battle_manager_, sample, true))
        { boundary_failed_ = true; return; }
        if (restore_trial_active_)
        {
            LogTrajectorySample(sample, ++restore_trial_boundaries_, STR("restore_trial"), event);
            return;
        }
        // Join the actual 1403FCD10 frames_back argument to the next native
        // A30 return. Repeated ticks without a publication are not arrivals.
        if(std::wcscmp(event,L"input_cache_publication")==0) {
            consumed_sample_=sample.input_time-boundary_observer_.LastInputFramesBack()-1;
            consumed_round_=sample.input_round;consumption_pending_=sample.source_active && consumed_sample_>=0;
        } else if(std::wcscmp(event,L"callback_a30")==0 && consumption_pending_) {
            consumption_pending_=false;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] input consumption protocol=1 run_id={} tick={} round={} sample={} p2={:08x} native_return=true\n"),
                RC::to_generic_string(request_.run_id),sample.frame,consumed_round_,consumed_sample_,sample.inputs[1]);
        }
        if(request_.completion_repeat && sample.frame==208 && std::wcscmp(event,L"round_sequence")==0) {
            std::uint8_t pending{};
            if(sample.manager_phase!=2 || sample.round_state!=2 || sample.move_state!=0
                || !ReadSchedulerValue(reinterpret_cast<std::uintptr_t>(battle_manager_)+0x1462,pending) || pending) {
                boundary_failed_=true;return;
            }
            // Native3FE520 consumes this byte after3FCE80/3FCA60. This is a
            // labelled identical control intervention, never expected state.
            *reinterpret_cast<std::uint8_t*>(reinterpret_cast<std::uintptr_t>(battle_manager_)+0x1462)=1;
            ++completion_repeat_count_;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] completion repeat intervention run_id={} tick=208 ordinal={} native_repeat=1\n"),
                RC::to_generic_string(request_.run_id),completion_repeat_count_);
        }
        LogTrajectorySample(sample, ++boundary_samples_, STR("boundary"), event);
        boundary_last_ = sample;
        boundary_last_round_sequence_ = std::wcscmp(event, L"round_sequence") == 0;
    }

    bool SkipIntroIfReady(const ReplayTrajectorySample& sample)
    {
        if (sample.world_mode != 6 && sample.world_mode != 7) return true;
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        std::uintptr_t world_mode{}, vtable{}, ready_fn{}, notify_fn{};
        if (!ReadSchedulerValue(reinterpret_cast<std::uintptr_t>(battle_manager_) + 0x1450, world_mode)
            || !world_mode || !ReadSchedulerValue(world_mode, vtable)
            || !ReadSchedulerValue(vtable + 0x80, ready_fn) || ready_fn != base + 0x3c3b80
            || !ReadSchedulerValue(vtable + 0x88, notify_fn) || notify_fn != base + 0x3cfe70)
        { Fail("intro_skip_provider_binding_invalid"); return false; }
        // The same native readiness/notification pair used for an admitted
        // input edge. Leave mode counters, initialization and post-tick cleanup
        // to native execution; authored replay inputs remain untouched.
        const bool ready = reinterpret_cast<bool (*)(void*)>(ready_fn)(reinterpret_cast<void*>(world_mode));
        if (intro_skip_mode_ != sample.world_mode || !ready) intro_skip_sent_ = false;
        intro_skip_mode_ = sample.world_mode;
        if (ready && !intro_skip_sent_)
        {
            reinterpret_cast<void (*)(void*)>(notify_fn)(reinterpret_cast<void*>(world_mode));
            intro_skip_sent_ = true;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] intro skip request run_id={} native_frame={} world_mode={} source_active=false ready=true\n"),
                RC::to_generic_string(request_.run_id), sample.frame, sample.world_mode);
        }
        return true;
    }

    bool StartExecutor(std::uint32_t native_frame)
    {
        const auto enable = ResolveHorseModExport<bool (*)(bool, bool, void*)>(
            "horsemod_set_replay_executor_enabled");
        const auto read=ResolveHorseModExport<bool (*)(std::uint64_t*,std::size_t)>("horsemod_get_replay_executor_status");
        std::array<std::uint64_t,6> existing{};
        const bool admitted=request_.host_session
            ? native_frame==0 && read && read(existing.data(),existing.size()) && existing[0] && !existing[1] && !existing[5]
            : enable && enable(true, request_.executor_yield_every_tick, battle_manager_);
        if (!admitted)
        { Fail("executor_enable_failed"); return false; }
        if(request_.replay_budget_gib==2) {
            const auto configure=ResolveHorseModExport<bool (*)(std::size_t,bool)>("horsemod_configure_replay_memory_budget");
            if(!configure || !configure(2ull*1024*1024*1024,true)) {Fail("replay_debug_budget_admission_failed");return false;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] replay memory budget run_id={} bytes=2147483648 diagnostic=true native_tick=0\n"),RC::to_generic_string(request_.run_id));
        }
        executor_started_ = true;
        if(request_.probe_historical_restore && request_.historical_anchor_tick==0) {
            const auto arm=ResolveHorseModExport<bool (*)(std::uint64_t,void*,ReplayHost::InteriorObserver)>("horsemod_arm_replay_application_pause");
            application_target_tick_=interior_expected_tick_=0;
            application_reentry_tick_=request_.historical_advanced_tick;
            if(native_frame!=0 || !arm || !arm(0,this,[](void* context,const ReplayHost::InteriorWitness& held) {
                return static_cast<ReplayQualificationMod*>(context)->ObserveApplicationPause(held);
            })) {Fail("initial_baseline_arm_rejected");return false;}
            combat_pause_armed_=true;
        }
        if (request_.record_index) {
            const auto index = ResolveHorseModExport<bool (*)(ReplayHost::IndexAction, Horse::Deterministic::ReplayTickIndex::Witness*, std::size_t)>("horsemod_replay_index_operation");
            Horse::Deterministic::ReplayTickIndex::Witness state{};
            if(native_frame || !index || !index(request_.host_session ? ReplayHost::IndexAction::Read : ReplayHost::IndexAction::Begin,&state,4*1024*1024)
                || state.phase!=Horse::Deterministic::ReplayTickIndex::Phase::Recording || state.entries!=1)
            {Fail("replay_index_begin_failed");return false;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] index started run_id={} entries={} bytes={} native_tick=0\n"),RC::to_generic_string(request_.run_id),state.entries,state.bytes);
        }
        if (request_.probe_small_restore)
        {
            const auto arm = ResolveHorseModExport<bool (*)(std::uint64_t, void*, ReplayHost::InteriorObserver)>(
                request_.probe_application_pause ? "horsemod_arm_replay_application_pause" : "horsemod_arm_replay_interior_pause");
            interior_expected_tick_ = 360;
            if (native_frame != 0 || !arm || !arm(interior_expected_tick_, this,
                [](void* context, const ReplayHost::InteriorWitness& witness) {
                    auto* self = static_cast<ReplayQualificationMod*>(context);
                    return self->request_.probe_application_pause ? self->ObserveApplicationPause(witness)
                        : self->ObserveInteriorPause(witness);
                }))
            { Fail("interior_pause_arm_rejected"); return false; }
        }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] executor started run_id={} native_frame={}\n"),
            RC::to_generic_string(request_.run_id), native_frame);
        return true;
    }

    void LogTrajectorySample(const ReplayTrajectorySample& sample, std::uint32_t ordinal,
        const wchar_t* label, const wchar_t* phase = L"engine_post")
    {
        const auto& p = sample.positions;
        const auto hex_words = [](const auto& values) {
            std::ostringstream text;
            text << std::hex << std::setfill('0');
            bool first = true;
            for (auto value : values)
            {
                if (!first) text << ',';
                first = false;
                text << std::setw(8) << value;
            }
            return RC::to_generic_string(text.str());
        };
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] {} ordinal={} phase={} sample_version=5 "
            "frame={} round={} cursor={} input_round={} input_time={} round_frame={} "
            "inputs={:08x},{:08x} p0_sim={:08x},{:08x},{:08x} "
            "p0_step={:08x},{:08x},{:08x} p0_render={:08x},{:08x},{:08x} "
            "p1_sim={:08x},{:08x},{:08x} p1_step={:08x},{:08x},{:08x} "
            "p1_render={:08x},{:08x},{:08x} p0_vital={} p1_vital={} p0_moves={} p1_moves={} "
            "source_active={} manager_phase={} move_state={} round_state={} published_count={} published_pairs={} world_mode={} mt_cursor={} mt_hash={:016x} crt_state={:08x} ground_roots={} ground_next_id={} ground_lifecycle={:016x}\n"),
            label, ordinal, phase, sample.frame, sample.round, sample.cursor,
            sample.input_round, sample.input_time, sample.round_frame,
            sample.inputs[0], sample.inputs[1],
            p[0][0], p[0][1], p[0][2], p[0][3], p[0][4], p[0][5],
            p[0][6], p[0][7], p[0][8], p[1][0], p[1][1], p[1][2],
            p[1][3], p[1][4], p[1][5], p[1][6], p[1][7], p[1][8],
            hex_words(sample.vital[0]), hex_words(sample.vital[1]),
            hex_words(sample.moves[0]), hex_words(sample.moves[1]), sample.source_active,
            sample.manager_phase, sample.move_state, sample.round_state,
            sample.published_count, hex_words(sample.published_pairs), sample.world_mode, sample.mt_cursor, sample.mt_hash, sample.crt_state, sample.ground.roots, sample.ground.next_id, sample.ground.hash);
    }

    bool BeginHostIndexCheckpoint(const ReplayHost::InteriorWitness& held)
    {
        if(state_==State::Failed)return false;
        if(held.phase==ReplayHost::InteriorPhase::Failed){Fail("index_checkpoint_pause_failed");return false;}
        if(held.phase!=ReplayHost::InteriorPhase::Holding || held.surface_pending)return false;
        ReplayTrajectorySample sample{};
        if(index_capture_!=IndexCapture::Armed || held.tick!=index_capture_tick_
            || held.boundary!=ReplayHost::PauseBoundary::CompletedApplication || !held.application_idle
            || !held.engine_idle || !held.world_idle || !held.arena_empty || held.pending_task
            || !ReadReplayTrajectory(battle_manager_,sample,true)
            || !ReplayQualification::IsCombatIndexCheckpoint(sample.frame,index_capture_tick_,sample.source_active,sample.round_state,sample.world_mode))
        {Fail("host_index_checkpoint_boundary_failed");return false;}
        const auto counters=ResolveHorseModExport<bool (*)(std::uint64_t*,std::size_t)>("horsemod_get_replay_executor_status");
        std::array<std::uint64_t,12> execution{};
        if(!counters || !counters(execution.data(),execution.size())){Fail("indexed_capture_coordinates_unavailable");return false;}
        index_capture_interval_=execution[3];
        if(index_capture_tick_==170)index_first_interval_=execution[3];
        index_capture_sample_=sample;index_capture_callbacks_=boundary_samples_;
        index_capture_epoch_=held.epoch;index_capture_applications_=held.completed_applications;
        const auto read=ResolveHorseModExport<bool (*)(ReplayHost::IndexCheckpointWitness*)>("horsemod_read_index_checkpoint");
        ReplayHost::IndexCheckpointWitness work{};
        if(!read || !read(&work) || work.phase!=ReplayHost::IndexCheckpointPhase::AwaitingBoundary
            || work.requested_tick!=index_capture_tick_ || work.captured_tick || work.failure!=Horse::Deterministic::FailureCode::None)
        {Fail("host_index_checkpoint_request_failed");return false;}
        index_capture_=IndexCapture::Capturing;
        Output::send<LogLevel::Default>(STR("[ReplayQualification] host index checkpoint adopted run_id={} tick={} capture_retention_and_retirement=host observer=read_only\n"),RC::to_generic_string(request_.run_id),index_capture_tick_);
        return false;
    }

    void ObserveHostIndexCheckpoint(const ReplayHost::InteriorWitness& held)
    {
        if(state_==State::Failed)return;
        if(index_capture_==IndexCapture::Armed){BeginHostIndexCheckpoint(held);return;}
        using Phase=ReplayHost::IndexCheckpointPhase;
        using Index=Horse::Deterministic::ReplayTickIndex;
        const auto read=ResolveHorseModExport<bool (*)(ReplayHost::IndexCheckpointWitness*)>("horsemod_read_index_checkpoint");
        const auto index=ResolveHorseModExport<bool (*)(ReplayHost::IndexAction,Index::Witness*,std::size_t)>("horsemod_replay_index_operation");
        ReplayHost::IndexCheckpointWitness work{};Index::Witness indexed{};ReplayTrajectorySample sample{};
        if(!read || !read(&work) || !index || !index(ReplayHost::IndexAction::Read,&indexed,0)
            || work.phase==Phase::Failed || work.requested_tick!=index_capture_tick_ || work.captured_tick!=index_capture_tick_
            || held.tick!=index_capture_tick_ || held.pending_task || !held.application_idle || !held.engine_idle || !held.world_idle
            || !ReadReplayTrajectory(battle_manager_,sample,true) || sample!=index_capture_sample_
            || boundary_samples_!=index_capture_callbacks_ || held.epoch!=index_capture_epoch_
            || held.completed_applications!=index_capture_applications_)
        {Fail("host_index_checkpoint_hold_changed");return;}
        if(work.failure!=Horse::Deterministic::FailureCode::None
            && !(request_.index_cancel && work.failure==Horse::Deterministic::FailureCode::Cancelled)) {
            // The host's eligibility-only rejection created no image or GPU
            // work. Observe its safe resume without claiming retention. Every
            // capture/ownership failure still terminates this experiment.
            if(work.failure!=Horse::Deterministic::FailureCode::UnsupportedContent
                || request_.index_cancel || !index_checkpoint_count_
                || index_capture_!=IndexCapture::Capturing || work.phase!=Phase::Resuming
                || work.capture_elapsed_us || work.owned_bytes
                || indexed.phase!=Index::Phase::Recording || indexed.entries!=index_capture_tick_+1)
            {Fail("host_index_checkpoint_failed");return;}
            const auto monitor=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
            if(!monitor || !monitor(nullptr,nullptr)){Fail("host_index_monitor_release_failed");return;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] optional index checkpoint skipped run_id={} tick={} capture=false retained=false callbacks_unchanged=true application_idle=true monitor_released=true\n"),
                RC::to_generic_string(request_.run_id),index_capture_tick_);
            index_capture_=IndexCapture::Resuming;
            return;
        }
        if(index_capture_==IndexCapture::Capturing && work.phase==Phase::RetiringScratch) {
            if(indexed.phase!=Index::Phase::Recording || indexed.entries!=index_capture_tick_+1 || !work.capture_elapsed_us || !work.owned_bytes)
            {Fail("host_index_checkpoint_retention_missing");return;}
            index_capture_=IndexCapture::Retained;
            if(index_checkpoint_count_>=index_checkpoint_coordinates_.size()
                || (index_checkpoint_count_ && index_checkpoint_coordinates_[index_checkpoint_count_-1].first>=index_capture_tick_))
            {Fail("host_index_checkpoint_inventory_overflow_or_order");return;}
            index_checkpoint_coordinates_[index_checkpoint_count_++]={index_capture_tick_,index_capture_interval_};
            Output::send<LogLevel::Default>(STR("[ReplayQualification] index checkpoint retained run_id={} tick={} entries={} elapsed_us={} owned_bytes={} callbacks_unchanged=true application_idle=true\n"),
                RC::to_generic_string(request_.run_id),index_capture_tick_,indexed.entries,work.capture_elapsed_us,work.owned_bytes);
            if(request_.index_cancel) {
                if(!index(ReplayHost::IndexAction::Cancel,&indexed,0) || indexed.phase!=Index::Phase::Cancelled
                    || indexed.entries!=index_capture_tick_+1 || indexed.native_finish || indexed.final_tail)
                {Fail("host_index_cancel_rejected");return;}
                index_capture_=IndexCapture::Cancelling;
                Output::send<LogLevel::Default>(STR("[ReplayQualification] index held cancellation queued run_id={} tick={} entries={} bytes={} complete=false cleanup_owner=host\n"),
                    RC::to_generic_string(request_.run_id),index_capture_tick_,indexed.entries,indexed.bytes);
                return; // The observer issues intent, never Capture/Finish/Release.
            }
        }
        if(index_capture_==IndexCapture::Cancelling) {
            if(indexed.phase==Index::Phase::Cancelled || indexed.phase==Index::Phase::Releasing)return;
            if(indexed.phase!=Index::Phase::Empty || indexed.entries || indexed.bytes)
            {Fail("host_index_cancel_retirement_failed");return;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] index held cancellation completed run_id={} tick={} entries=0 bytes=0 callbacks_unchanged=true epoch_unchanged=true application_unchanged=true\n"),RC::to_generic_string(request_.run_id),index_capture_tick_);
        } else {
            if(work.phase!=Phase::Resuming)return;
            if(index_capture_!=IndexCapture::Retained || indexed.phase!=Index::Phase::Recording)
            {Fail("host_index_checkpoint_resume_without_retention");return;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] index checkpoint capture retired run_id={} tick={} immutable_owner_retained=true\n"),RC::to_generic_string(request_.run_id),index_capture_tick_);
        }
        index_capture_=IndexCapture::Resuming;
        const auto monitor=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
        if(!monitor || !monitor(nullptr,nullptr))Fail("host_index_monitor_release_failed");
    }

    bool ObserveIndexCheckpoint(const ReplayHost::InteriorWitness& held)
    {
        // The application can keep pumping this hold while owned-process
        // cleanup runs. Preserve the first terminal result and the hold;
        // do not repeat eligibility/capture work or publish a new request.
        if(state_==State::Failed) return false;
        if(held.phase==ReplayHost::InteriorPhase::Failed)
        {Fail("index_checkpoint_pause_failed");return false;}
        if(held.phase!=ReplayHost::InteriorPhase::Holding) return false;
        if(held.surface_pending) return false;
        ReplayTrajectorySample sample{};
        if(held.tick!=index_capture_tick_ || held.boundary!=ReplayHost::PauseBoundary::CompletedApplication
            || !held.application_idle || !held.engine_idle || !held.world_idle || !held.arena_empty || held.pending_task
            || !ReadReplayTrajectory(battle_manager_,sample,true)
            || !ReplayQualification::IsCombatIndexCheckpoint(sample.frame,index_capture_tick_,sample.source_active,sample.round_state,sample.world_mode))
        {Fail("index_checkpoint_boundary_failed");return false;}
        const auto capture=ResolveHorseModExport<bool (*)(ReplayHost::CaptureAction,ReplayHost::CaptureWitness*,ReplayHost::CheckpointHandle*)>("horsemod_replay_capture_operation");
        ReplayHost::CaptureWitness captured{};
        if(index_capture_==IndexCapture::Armed) {
            if(request_.index_extra_checkpoint && index_capture_tick_!=170) {
                if(!capture || !capture(ReplayHost::CaptureAction::CheckTargetEligibility,&captured,nullptr))
                {Fail("index_checkpoint_eligibility_query_failed");return false;}
                if(captured.failure==Horse::Deterministic::FailureCode::UnsupportedContent) {
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed checkpoint deferred run_id={} requested={} tick={} reason=active_HUD_players captured=false\n"),
                        RC::to_generic_string(request_.run_id),request_.index_extra_checkpoint,index_capture_tick_);
                    if(index_capture_tick_+30>request_.index_extra_checkpoint+240 || index_capture_tick_+30>=request_.index_seek_target)
                    {Fail("index_checkpoint_eligibility_window_exhausted");return false;}
                    index_capture_tick_+=30;index_capture_=IndexCapture::Waiting;return true;
                }
                if(captured.failure!=Horse::Deterministic::FailureCode::None)
                {Fail("index_checkpoint_eligibility_binding_failed");return false;}
            }
            const auto counters=ResolveHorseModExport<bool (*)(std::uint64_t*,std::size_t)>("horsemod_get_replay_executor_status");
            std::array<std::uint64_t,12> execution{};
            if(request_.index_seek && (!counters || !counters(execution.data(),execution.size())))
            {Fail("indexed_capture_coordinates_unavailable");return false;}
            index_capture_interval_=execution[3];
            index_capture_sample_=sample;index_capture_callbacks_=boundary_samples_;
            index_capture_epoch_=held.epoch;index_capture_applications_=held.completed_applications;
            if(!capture || !capture(ReplayHost::CaptureAction::Begin,&captured,nullptr))
            {Fail("index_checkpoint_capture_rejected");return false;}
            index_capture_=IndexCapture::Capturing;return false;
        }
        if(sample!=index_capture_sample_ || boundary_samples_!=index_capture_callbacks_
            || held.epoch!=index_capture_epoch_ || held.completed_applications!=index_capture_applications_)
        {Fail("index_checkpoint_hold_changed");return false;}
        if(index_capture_==IndexCapture::Retained) {
            using Copy=Horse::Deterministic::Sc6ReplayParticleCopy;
            const auto operation=ResolveHorseModExport<bool (*)(ReplayHost::ParticleCopyAction,Copy::Witness*,bool*)>("horsemod_particle_copy_experiment");
            Copy::Witness copied{};bool pending{};
            if(!operation || !operation(ReplayHost::ParticleCopyAction::Read,&copied,&pending))
            {Fail("index_checkpoint_scratch_read_failed");return false;}
            if(pending) return false;
            if(copied.phase!=Copy::Phase::Released) {
                if(!operation(ReplayHost::ParticleCopyAction::Finish,&copied,&pending))
                    Fail("index_checkpoint_scratch_retirement_failed");
                return false;
            }
            Output::send<LogLevel::Default>(STR("[ReplayQualification] index checkpoint capture retired run_id={} tick={} immutable_owner_retained=true\n"),RC::to_generic_string(request_.run_id),index_capture_tick_);
            index_capture_=IndexCapture::Resuming;
            return true;
        }
        if(index_capture_==IndexCapture::Resuming) return true;
        if(!capture || !capture(ReplayHost::CaptureAction::Read,&captured,nullptr))
        {Fail("index_checkpoint_capture_failed");return false;}
        if(captured.phase==ReplayHost::CapturePhase::Failed && request_.index_extra_checkpoint && index_capture_tick_!=170
            && captured.failure==Horse::Deterministic::FailureCode::UnsupportedContent) {
            // Failed is terminal only after AdvanceCaptureOperation retires all
            // partial CPU/GPU capture owners. Never treat a pending failure or
            // timeout as cancellation, and never drop the retained A170 pin.
            if(captured.pending || !capture(ReplayHost::CaptureAction::Release,&captured,nullptr)
                || captured.phase!=ReplayHost::CapturePhase::Idle || captured.pending)
            {Fail("index_optional_capture_retirement_failed");return false;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed checkpoint capture rejected run_id={} tick={} code=5 resources_retired=true operation_idle=true\n"),
                RC::to_generic_string(request_.run_id),index_capture_tick_);
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed checkpoint deferred run_id={} requested={} tick={} reason=unsupported_capture captured=true\n"),
                RC::to_generic_string(request_.run_id),request_.index_extra_checkpoint,index_capture_tick_);
            if(index_capture_tick_+30>request_.index_extra_checkpoint+240 || index_capture_tick_+30>=request_.index_seek_target)
            {Fail("index_checkpoint_eligibility_window_exhausted");return false;}
            index_capture_tick_+=30;index_capture_=IndexCapture::Waiting;return true;
        }
        if(captured.phase==ReplayHost::CapturePhase::Failed || captured.phase==ReplayHost::CapturePhase::Cancelled)
        {Fail("index_checkpoint_capture_failed");return false;}
        if(captured.phase!=ReplayHost::CapturePhase::Ready) return false;
        using Index=Horse::Deterministic::ReplayTickIndex;
        const auto operation=ResolveHorseModExport<bool (*)(ReplayHost::IndexAction,Index::Witness*,std::size_t)>("horsemod_replay_index_operation");
        Index::Witness state{};
        if(!operation || !operation(ReplayHost::IndexAction::RetainCapture,&state,0)
            || state.phase!=Index::Phase::Recording || state.entries!=index_capture_tick_+1)
        {Fail("index_checkpoint_retention_failed");return false;}
        index_capture_=IndexCapture::Retained;
        Output::send<LogLevel::Default>(STR("[ReplayQualification] index checkpoint retained run_id={} tick={} entries={} elapsed_us={} owned_bytes={} callbacks_unchanged=true application_idle=true\n"),
            RC::to_generic_string(request_.run_id),index_capture_tick_,state.entries,captured.elapsed_us,captured.owned_bytes);
        return false; // Retire operation scratch before resuming the indexed replay.
    }

    bool CheckIndexProgress()
    {
        if(request_.host_session && (index_capture_==IndexCapture::Waiting
            || (index_capture_==IndexCapture::Resuming && index_host_completion_seen_))) {
            const auto read=ResolveHorseModExport<bool (*)(ReplayHost::IndexCheckpointWitness*)>("horsemod_read_index_checkpoint");
            ReplayHost::IndexCheckpointWitness work{};
            if(!read || !read(&work)){Fail("managed_index_checkpoint_unavailable");return false;}
            const bool initial=index_checkpoint_count_==0;
            if(work.phase==ReplayHost::IndexCheckpointPhase::AwaitingBoundary
                && (initial ? work.requested_tick==170 : work.requested_tick>index_capture_tick_)) {
                index_capture_tick_=work.requested_tick; // Observe native placement; never move the host's request.
                index_host_completion_seen_=false;
                const auto monitor=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
                if(work.phase!=ReplayHost::IndexCheckpointPhase::AwaitingBoundary || work.requested_tick!=index_capture_tick_ || work.captured_tick
                    || !monitor || !monitor(this,[](void* context,const ReplayHost::InteriorWitness& held) {
                        static_cast<ReplayQualificationMod*>(context)->ObserveHostIndexCheckpoint(held);
                    })) {Fail("managed_index_checkpoint_observer_admission_failed");return false;}
                index_capture_=IndexCapture::Armed;
                Output::send<LogLevel::Default>(STR("[ReplayQualification] host index checkpoint armed run_id={} origin={} target={}\n"),RC::to_generic_string(request_.run_id),index_capture_tick_-1,index_capture_tick_);
            }
        }
        if(request_.index_checkpoint && index_capture_==IndexCapture::Resuming && !index_host_completion_seen_) {
            const auto read=ResolveHorseModExport<bool (*)(ReplayHost::IndexCheckpointWitness*)>("horsemod_read_index_checkpoint");
            ReplayHost::IndexCheckpointWitness work{};
            if(!read || !read(&work) || (work.phase!=ReplayHost::IndexCheckpointPhase::Resuming && work.phase!=ReplayHost::IndexCheckpointPhase::Complete))
            {Fail("host_index_completion_missing");return false;}
            if(work.phase==ReplayHost::IndexCheckpointPhase::Complete) {
                index_host_completion_seen_=true;
                Output::send<LogLevel::Default>(STR("[ReplayQualification] host index checkpoint completed run_id={} tick={} pending=false cancelled={}\n"),RC::to_generic_string(request_.run_id),index_capture_tick_,request_.index_cancel);
            }
        }
        using Index=Horse::Deterministic::ReplayTickIndex;
        const auto operation=ResolveHorseModExport<bool (*)(ReplayHost::IndexAction,Index::Witness*,std::size_t)>("horsemod_replay_index_operation");
        Index::Witness state{};
        if(request_.index_cancel && index_capture_==IndexCapture::Resuming) {
            if(!operation || !operation(ReplayHost::IndexAction::Read,&state,0) || state.phase!=Index::Phase::Empty || state.bytes)
            {Fail("index_cancelled_state_changed");return false;}
            return true;
        }
        if(index_publication_==IndexPublication::Retiring) return true; // PublishIndex verifies asynchronous retirement.
        if(!operation || !operation(ReplayHost::IndexAction::Read,&state,0)
            || state.phase==Index::Phase::Failed || state.phase==Index::Phase::Cancelled || !state.entries) {
            Output::send<LogLevel::Warning>(STR("[ReplayQualification] index progress failure phase={} failure={} entries={} intervals={} final_tail={} native_finish={}\n"),
                static_cast<unsigned>(state.phase),static_cast<unsigned>(state.failure),state.entries,state.completed_intervals,state.final_tail,state.native_finish);
            Fail("replay_index_progress_failed");return false;
        }
        return true;
    }

    #include "ReplayIndexedRecovery.inl"
    #include "ReplayPlaybackControls.inl"

    std::size_t index_sequence_position_{};
    std::uint32_t IndexedTarget() const noexcept {
        return request_.index_sequence?ReplayQualification::IndexSequence(request_.index_sequence)[index_sequence_position_]:request_.index_seek_target;
    }

    void LogIndexedSequenceCase(const ReplayHost::InteriorWitness& held,std::uint64_t interval) {
        if(!request_.index_sequence)return;
        Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed sequence case run_id={} sequence={} case={} target={} B={} callbacks={} interval={} previous_release_completed={}\n"),
            RC::to_generic_string(request_.run_id),request_.index_sequence,index_sequence_position_,IndexedTarget(),held.tick,boundary_samples_,interval,index_sequence_position_!=0);
    }

    bool ObserveIndexedSequenceBoundary(const ReplayHost::InteriorWitness& held) {
        if(state_==State::Failed || index_seek_phase_!=IndexSeek::ArmingNext)return false;
        const auto targets=ReplayQualification::IndexSequence(request_.index_sequence);
        if(targets.empty() || index_sequence_position_>=targets.size()-1) {
            Fail("indexed_sequence_position_invalid");return false;
        }
        if(held.phase==ReplayHost::InteriorPhase::Failed){Fail("indexed_sequence_hold_failed");return false;}
        if(held.phase!=ReplayHost::InteriorPhase::Holding || held.surface_pending)return false;
        const auto counters=ResolveHorseModExport<bool (*)(std::uint64_t*,std::size_t)>("horsemod_get_replay_executor_status");
        const auto operation=ResolveHorseModExport<bool (*)(ReplayHost::SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,ReplayHost::SeekWitness*,void*,ReplayHost::SeekObserver)>("horsemod_replay_seek_operation");
        const auto monitor=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
        const auto request=ResolveHorseModExport<std::uint16_t (*)(std::uint64_t)>("horsemod_request_indexed_replay_seek");
        std::array<std::uint64_t,12> execution{};ReplayHost::SeekWitness seek{};
        if(held.tick!=IndexedTarget()+request_.index_seek_continuation+1 || !held.application_idle || !held.engine_idle
            || !held.world_idle || !held.arena_empty || held.pending_task || held.boundary!=ReplayHost::PauseBoundary::CompletedApplication
            || !counters || !counters(execution.data(),execution.size()) || execution[2]!=held.tick
            || !operation || !operation(ReplayHost::SeekAction::Read,nullptr,0,&seek,nullptr,nullptr)
            || seek.pending || seek.phase!=ReplayHost::SeekPhase::Idle
            || seek.release_result!=ReplayHost::SeekReleaseResult::Completed || !monitor || !request)
        {Fail("indexed_sequence_previous_transaction_unreleased");return false;}
        ++index_sequence_position_;index_seek_samples_=0;index_origin_interval_=execution[3];
        if(IndexedTarget()+request_.index_seek_continuation>trajectory_last_.frame
            || !monitor(nullptr,nullptr) || !monitor(this,[](void* context,const ReplayHost::InteriorWitness& value) {
                static_cast<ReplayQualificationMod*>(context)->ObserveIndexedSeek(value);
            })) {Fail("indexed_sequence_monitor_failed");return false;}
        LogIndexedSequenceCase(held,execution[3]);
        Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed seek begin run_id={} origin={} target={} supported_index=true host_checkpoint=true callbacks={} interval={}\n"),
            RC::to_generic_string(request_.run_id),held.tick,IndexedTarget(),boundary_samples_,execution[3]);
        index_seek_requested_=std::chrono::steady_clock::now();
        if(request(207)!=0 || request(IndexedTarget())!=0){Fail("indexed_sequence_request_rejected");return false;}
        index_seek_phase_=IndexSeek::Seeking;
        return false;
    }

    bool BeginIndexedSeek(const ReplayHost::InteriorWitness& held)
    {
        if(held.phase==ReplayHost::InteriorPhase::Failed) {Fail("index_completed_pause_failed");return false;}
        if(held.phase!=ReplayHost::InteriorPhase::Holding || held.surface_pending
            || held.application_updates<30 || held.surface_frames<10) return false;
        const auto resume=ResolveHorseModExport<std::uint16_t (*)()>("horsemod_resume_replay_execution");
        const auto advance=ResolveHorseModExport<bool (*)(std::uint64_t,void*,ReplayHost::InteriorObserver,ReplayHost::TickAdvanceWitness*)>("horsemod_request_replay_tick");
        const auto invalid=static_cast<std::uint16_t>(Horse::Deterministic::FailureCode::InvalidConfiguration);
        if(!resume || !advance || held.tick==UINT64_MAX) {Fail("index_end_escape_exports_missing");return false;}
        const auto resume_result=resume();ReplayHost::TickAdvanceWitness rejected{};
        const bool accepted=advance(held.tick+1,nullptr,nullptr,&rejected);
        Output::send<LogLevel::Default>(STR("[ReplayQualification] index endpoint command results run_id={} tick={} resume_code={} step_accepted={} step_phase={} step_code={}\n"),
            RC::to_generic_string(request_.run_id),held.tick,resume_result,accepted,static_cast<unsigned>(rejected.phase),static_cast<unsigned>(rejected.failure));
        if(resume_result!=invalid || accepted || rejected.phase!=ReplayHost::TickAdvancePhase::Failed
            || rejected.failure!=Horse::Deterministic::FailureCode::InvalidConfiguration)
        {Fail("index_end_escape_not_rejected");return false;}
        Output::send<LogLevel::Default>(STR("[ReplayQualification] index endpoint escape rejected run_id={} tick={} resume=true step=true backward_seek_available=true\n"),
            RC::to_generic_string(request_.run_id),held.tick);
        const auto counters=ResolveHorseModExport<bool (*)(std::uint64_t*,std::size_t)>("horsemod_get_replay_executor_status");
        const auto monitor=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
        const auto request=ResolveHorseModExport<std::uint16_t (*)(std::uint64_t)>("horsemod_request_indexed_replay_seek");
        std::array<std::uint64_t,12> execution{};
        if(index_seek_phase_!=IndexSeek::Arming || !held.application_idle || !held.engine_idle || !held.world_idle
            || !held.arena_empty || held.pending_task || held.boundary!=ReplayHost::PauseBoundary::CompletedApplication
            || !counters || !counters(execution.data(),execution.size()) || execution[2]!=held.tick
            || IndexedTarget()+request_.index_seek_continuation>trajectory_last_.frame
            || held.tick<=trajectory_last_.frame || execution[3]<=index_capture_interval_ || !request || !monitor
            || !monitor(nullptr,nullptr)
            || !monitor(this,[](void* context,const ReplayHost::InteriorWitness& value) {
                static_cast<ReplayQualificationMod*>(context)->ObserveIndexedSeek(value);
            })) {Fail("index_completed_hold_invalid");return false;}
        if(!BeginIndexedRecoveryObservation()) return false;
        index_origin_interval_=execution[3];
        LogIndexedSequenceCase(held,execution[3]);
        historical_execution_started_=true;
        Output::send<LogLevel::Default>(STR("[ReplayQualification] index closure run_id={} replay_tick={} native_tick={} callbacks_retained=true\n"),
            RC::to_generic_string(request_.run_id),trajectory_last_.frame,held.tick);
        Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed seek begin run_id={} origin={} target={} {}=true host_checkpoint=true callbacks={} interval={}\n"),
            RC::to_generic_string(request_.run_id),held.tick,IndexedTarget(),
            request_.host_session?STR("supported_index"):STR("full_index"),boundary_samples_,execution[3]);
        // Two requests before the application owner runs demonstrate latest
        // intent coalescing. Neither request installs a state or an observation.
        index_seek_requested_=std::chrono::steady_clock::now();
        if(request_.probe_interactive_controls && request_.index_recovery.empty()) {
            // Windows input must submit this request through the real overlay.
            // The observer remains read-only; entry time is included in this
            // control experiment's cost, not a production performance claim.
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed numeric entry waiting run_id={} origin={} target={}\n"),
                RC::to_generic_string(request_.run_id),held.tick,IndexedTarget());
            index_seek_phase_=IndexSeek::AwaitingUiRequest;
            return false;
        }
        if(request(207)!=0 || request(IndexedTarget())!=0) {Fail("indexed_seek_queue_rejected");return false;}
        index_seek_phase_=IndexSeek::Seeking;
        return false;
    }

    void ObserveIndexedSeek(const ReplayHost::InteriorWitness& held)
    {
        if(state_==State::Failed) return; // Retain one bounded first-failure record.
        const auto operation=ResolveHorseModExport<bool (*)(ReplayHost::SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,
            ReplayHost::SeekWitness*,void*,ReplayHost::SeekObserver)>("horsemod_replay_seek_operation");
        ReplayHost::SeekWitness seek{};
        const bool readable=operation && operation(ReplayHost::SeekAction::Read,nullptr,0,&seek,nullptr,nullptr);
        if(readable && index_seek_phase_==IndexSeek::AwaitingUiRequest) {
            if(seek.phase==ReplayHost::SeekPhase::Idle)return;
            if(seek.phase!=ReplayHost::SeekPhase::Preparing || seek.target!=IndexedTarget()
                || seek.undo_tick!=held.tick || !held.application_idle || held.pending_task)
            {Fail("indexed_numeric_entry_wrong_request");return;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed numeric entry accepted run_id={} origin={} target={} production_seek=true\n"),
                RC::to_generic_string(request_.run_id),held.tick,seek.target);
            index_seek_phase_=IndexSeek::Seeking;
        }
        if(readable && ObserveIndexedRecovery(held,seek)) return;
        if(!readable || held.phase==ReplayHost::InteriorPhase::Failed || seek.phase==ReplayHost::SeekPhase::Failed) {
            const auto read=ResolveHorseModExport<bool (*)(ReplayHost::RestoreOperationAction,const ReplayHost::Checkpoint*,ReplayHost::RestoreOperationWitness*)>("horsemod_replay_restore_operation");
            ReplayHost::RestoreOperationWitness restore{};
            const bool restore_readable=read && read(ReplayHost::RestoreOperationAction::Read,nullptr,&restore);
            Output::send<LogLevel::Warning>(STR("[ReplayQualification] indexed seek failed participant={} failure={} commit_decided={} run_id={} target={} checkpoint={} held_tick={} B={} seek_phase={} restore_phase={} ownership_phase={} restore_readable={} pending={} B_recovery_complete={}\n"),
                RC::to_generic_string(restore_readable && restore.participant?restore.participant:"unknown"),
                static_cast<unsigned>(restore_readable?restore.failure:seek.failure),restore.commit_decided,
                RC::to_generic_string(request_.run_id),seek.target,seek.checkpoint,held.tick,seek.undo_tick,
                static_cast<unsigned>(seek.phase),static_cast<unsigned>(restore.phase),
                restore.ownership_phase?static_cast<int>(*restore.ownership_phase):-1,restore_readable,
                restore_readable?restore.pending:seek.pending,restore_readable && restore.original_recovered && !restore.pending);
            Fail("indexed_seek_failed");return;
        }
        if(request_.index_preparation_fallback && (index_preparation_trial_==IndexPreparationTrial::Idle
                || index_preparation_trial_==IndexPreparationTrial::Renewed)
            && index_seek_phase_==IndexSeek::Seeking && seek.phase==ReplayHost::SeekPhase::Preparing) {
            if(index_preparation_trial_==IndexPreparationTrial::Idle) {
                if(!ReadReplayTrajectory(battle_manager_,index_preparation_B_,true)) {Fail("indexed_retry_B_observation_failed");return;}
                index_preparation_callbacks_=boundary_samples_;index_preparation_epoch_=held.epoch;
                LogTrajectorySample(index_preparation_B_,1,L"index_preparation_B_before");
            }
            if(seek.checkpoint!=index_capture_tick_ || !operation(ReplayHost::SeekAction::InjectPreparationFailure,nullptr,
                seek.checkpoint,&seek,nullptr,nullptr)) {Fail("indexed_preparation_fault_arm_rejected");return;}
            index_preparation_trial_=index_preparation_trial_==IndexPreparationTrial::Idle
                ?IndexPreparationTrial::FirstArmed:IndexPreparationTrial::FinalArmed;
        }
        if(index_preparation_trial_==IndexPreparationTrial::FirstArmed || index_preparation_trial_==IndexPreparationTrial::Cancelling) {
            ReplayTrajectorySample sample{};
            if(!ReadReplayTrajectory(battle_manager_,sample,true) || sample!=index_preparation_B_
                || boundary_samples_!=index_preparation_callbacks_ || held.epoch!=index_preparation_epoch_)
            {Fail("indexed_retry_B_advanced");return;}
            if(index_preparation_trial_==IndexPreparationTrial::FirstArmed && seek.phase==ReplayHost::SeekPhase::RetryingPreparation) {
                if(seek.preparation_fallbacks || seek.commit_decided || !seek.pending
                    || !operation(ReplayHost::SeekAction::Cancel,nullptr,0,&seek,nullptr,nullptr))
                {Fail("indexed_retry_cancellation_rejected");return;}
                index_preparation_trial_=IndexPreparationTrial::Cancelling;
            }
            if(index_preparation_trial_==IndexPreparationTrial::Cancelling && seek.phase==ReplayHost::SeekPhase::Cancelled) {
                if(seek.pending || !seek.original_recovered || seek.preparation_fallbacks || held.tick!=seek.undo_tick
                    || !held.application_idle || held.pending_task) {Fail("indexed_retry_cancellation_B_unproven");return;}
                LogTrajectorySample(sample,1,L"index_preparation_B_after");
                const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-index_seek_requested_).count();
                Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed preparation retry cancellation run_id={} checkpoint={} B={} elapsed_us={} unchanged=true callbacks_unchanged=true epoch_unchanged=true fallback_started=false native_released=true\n"),
                    RC::to_generic_string(request_.run_id),index_capture_tick_,held.tick,elapsed);
                const auto request=ResolveHorseModExport<std::uint16_t (*)(std::uint64_t)>("horsemod_request_indexed_replay_seek");
                if(!request || request(IndexedTarget())!=0) {Fail("indexed_retry_renewal_rejected");return;}
                index_seek_requested_=std::chrono::steady_clock::now();
                Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed preparation retry renewed run_id={} target={} B={} new_request_clock=true\n"),
                    RC::to_generic_string(request_.run_id),IndexedTarget(),held.tick);
                index_preparation_trial_=IndexPreparationTrial::Renewed;
            }
            return;
        }
        if(index_seek_phase_==IndexSeek::Seeking && seek.phase==ReplayHost::SeekPhase::Restored) {
            const auto coordinate=std::find_if(index_checkpoint_coordinates_.begin(),
                index_checkpoint_coordinates_.begin()+index_checkpoint_count_,
                [&](const auto& row){return row.first==seek.checkpoint;});
            if(request_.host_session ? coordinate==index_checkpoint_coordinates_.begin()+index_checkpoint_count_
                : seek.checkpoint!=170 && (!request_.index_extra_checkpoint || seek.checkpoint!=index_capture_tick_))
            {Fail("indexed_selected_checkpoint_unknown");return;}
            index_selected_checkpoint_=seek.checkpoint;
            std::uint64_t nearest=170,preceding=170;
            if(request_.host_session) {
                for(std::size_t i=0;i<index_checkpoint_count_;++i) {
                    const auto tick=index_checkpoint_coordinates_[i].first;
                    if(tick<=IndexedTarget() && tick<=seek.undo_tick && tick>nearest) {preceding=nearest;nearest=tick;}
                }
            } else if(request_.index_extra_checkpoint)nearest=index_capture_tick_;
            if(seek.checkpoint<nearest) {
                if(request_.index_preparation_fallback) {
                    if(index_preparation_trial_!=IndexPreparationTrial::FinalArmed || !seek.automatic_selection || seek.preparation_fallbacks!=1
                        || seek.checkpoint!=preceding || seek.last_preparation_failure!=Horse::Deterministic::FailureCode::RestorePreflightFailed)
                    {Fail("indexed_preparation_fallback_witness_missing");return;}
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed checkpoint fallback run_id={} rejected={} selected={} reason=retired_preparation B_recovered=true\n"),
                        RC::to_generic_string(request_.run_id),nearest,seek.checkpoint);
                } else if(seek.preparation_fallbacks) {
                    // Natural preparation failures have their own complete-B
                    // recovery journal. They are not expired-owner failures.
                    // The offline parser verifies every native recovery/release
                    // link before independent continuation can pass.
                    if(!seek.automatic_selection || seek.execution_fallbacks
                        || seek.preparation_fallbacks>=index_checkpoint_count_
                        || seek.last_preparation_failure==Horse::Deterministic::FailureCode::None)
                    {Fail("indexed_natural_preparation_fallback_witness_missing");return;}
                    Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed natural preparation fallback run_id={} selected={} attempts={} last_failure={} B={} automatic=true\n"),
                        RC::to_generic_string(request_.run_id),seek.checkpoint,seek.preparation_fallbacks,
                        static_cast<unsigned>(seek.last_preparation_failure),seek.undo_tick);
                } else {
                if(!seek.automatic_selection || !seek.retained_owner_rejected || seek.rejected_checkpoint!=nearest)
                {Fail("indexed_fallback_lifetime_witness_missing");return;}
                Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed checkpoint fallback run_id={} rejected={} selected={} reason=retained_owner_lifetime\n"),
                    RC::to_generic_string(request_.run_id),seek.rejected_checkpoint,seek.checkpoint);
                }
            }
            if(request_.index_preparation_fallback && seek.checkpoint!=preceding) {Fail("indexed_preparation_fallback_not_exercised");return;}
            const auto rewind_ticks=seek.undo_tick-seek.checkpoint;
            const auto rewind_intervals=index_origin_interval_-(request_.host_session?coordinate->second:
                (seek.checkpoint==170?index_first_interval_:index_capture_interval_));
            historical_rewind_ticks_+=rewind_ticks;
            historical_rewind_intervals_+=rewind_intervals;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed checkpoint selected run_id={} tick={} extra={} automatic=true\n"),
                RC::to_generic_string(request_.run_id),seek.checkpoint,request_.index_extra_checkpoint?index_capture_tick_:0);
            if(!ArmIndexedRecovery(seek)) return;
            if(request_.index_recovery.empty()) Output::send<LogLevel::Default>(STR("[ReplayQualification] historical execution rewind run_id={} ticks={} intervals={}\n"),
                RC::to_generic_string(request_.run_id),rewind_ticks,rewind_intervals);
            index_seek_prepared_us_=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-index_seek_requested_).count();
            Output::send<LogLevel::Default>(STR("[ReplayQualification] historical combat execution admitted run_id={} from_tick={} to_tick={} B_retained=true\n"),
                RC::to_generic_string(request_.run_id),seek.undo_tick,seek.checkpoint);
            index_seek_phase_=IndexSeek::Executing;
        }
        if(index_seek_phase_==IndexSeek::Seeking || index_seek_phase_==IndexSeek::Executing) {
            if(seek.phase!=ReplayHost::SeekPhase::Held || seek.pending) return;
            if(seek.target!=IndexedTarget() || seek.checkpoint!=index_selected_checkpoint_ || !seek.automatic_selection || held.tick!=IndexedTarget()
                || (seek.target!=seek.checkpoint && (held.boundary!=ReplayHost::PauseBoundary::SimulationTick || !held.pending_task))
                || (seek.target==seek.checkpoint && (held.boundary!=ReplayHost::PauseBoundary::CompletedApplication || held.pending_task
                    || !held.application_idle || !held.engine_idle || !held.world_idle || !held.arena_empty))
                || !ReadReplayTrajectory(battle_manager_,index_seek_held_sample_,true))
            {Fail("indexed_seek_target_invalid");return;}
            const auto ready_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-index_seek_requested_).count();
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed seek ready run_id={} target={} elapsed_us={} owned_bytes={} diagnostic_gpu_readbacks=false\n"),
                RC::to_generic_string(request_.run_id),IndexedTarget(),ready_us,seek.owned_bytes);
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed seek application cost run_id={} target={} preparation_us={} prefix_us={} engine_us={} tail_us={} frame_sync_us={} includes_observers=true\n"),
                RC::to_generic_string(request_.run_id),IndexedTarget(),index_seek_prepared_us_,seek.prefix_us,seek.engine_us,seek.tail_us,seek.frame_sync_us);
            index_seek_held_callbacks_=boundary_samples_;index_seek_held_epoch_=held.epoch;
            index_seek_held_frames_=held.surface_frames;index_seek_held_started_=std::chrono::steady_clock::now();
            index_seek_phase_=IndexSeek::Holding;
            LogTrajectorySample(index_seek_held_sample_,1,L"indexed_seek_target");
        }
        if(index_seek_phase_==IndexSeek::Holding) {
            ReplayTrajectorySample sample{};
            if(!ReadReplayTrajectory(battle_manager_,sample,true) || sample!=index_seek_held_sample_
                || boundary_samples_!=index_seek_held_callbacks_ || held.epoch!=index_seek_held_epoch_
                || (held.pending_task!=nullptr)!=(IndexedTarget()!=index_selected_checkpoint_))
            {Fail("indexed_seek_hold_advanced");return;}
            const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-index_seek_held_started_).count();
            if(elapsed<500000 || held.surface_frames-index_seek_held_frames_<30) return;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed seek held run_id={} tick={} elapsed_us={} frames={} pending_task={} unchanged=true\n"),
                RC::to_generic_string(request_.run_id),IndexedTarget(),elapsed,held.surface_frames-index_seek_held_frames_,held.pending_task!=nullptr);
            index_seek_hold_us_=elapsed;index_seek_completion_started_=std::chrono::steady_clock::now();
            if(!operation(ReplayHost::SeekAction::CompleteTarget,nullptr,0,&seek,nullptr,nullptr))
            {Fail("indexed_seek_target_completion_rejected");return;}
            index_seek_phase_=IndexSeek::Tails;return;
        }
        if(index_seek_phase_==IndexSeek::Tails) {
            if(seek.pending || !seek.commit_decided || held.surface_pending) return;
            if(held.tick!=IndexedTarget() || !held.application_idle || held.pending_task
                || !operation(ReplayHost::SeekAction::Release,nullptr,0,&seek,nullptr,nullptr))
            {Fail("indexed_seek_release_rejected");return;}
            if(seek.release_result!=ReplayHost::SeekReleaseResult::Completed) return;
            const auto now=std::chrono::steady_clock::now();
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed seek completion cost run_id={} target={} request_to_completed_us={} validation_hold_us={} completion_us={} application_idle=true pending_task=false release_completed=true diagnostic_gpu_readbacks=false\n"),
                RC::to_generic_string(request_.run_id),IndexedTarget(),
                std::chrono::duration_cast<std::chrono::microseconds>(now-index_seek_requested_).count(),index_seek_hold_us_,
                std::chrono::duration_cast<std::chrono::microseconds>(now-index_seek_completion_started_).count());
            index_seek_phase_=IndexSeek::Releasing;return;
        }
        if(index_seek_phase_==IndexSeek::Releasing) {
            if(held.surface_pending) return;
            const auto resume=ResolveHorseModExport<std::uint16_t (*)()>("horsemod_resume_replay_execution");
            const auto monitor=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
            if(!resume || !monitor || !monitor(nullptr,nullptr)
                || (request_.probe_interactive_controls && !monitor(this,[](void* context,const ReplayHost::InteriorWitness& value) {
                    static_cast<ReplayQualificationMod*>(context)->ObservePlaybackControls(value);
                })) || resume()!=0)
            {Fail("indexed_seek_resume_rejected");return;}
            index_seek_resume_started_=std::chrono::steady_clock::now();
            index_seek_phase_=IndexSeek::Resuming;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed seek resumed run_id={} tick={} target_tails_complete=true\n"),RC::to_generic_string(request_.run_id),IndexedTarget());
        }
    }

    void ObserveIndexedSuffix()
    {
        ReplayTrajectorySample sample{};
        if(!ReadReplayTrajectory(battle_manager_,sample,true)) {Fail("indexed_seek_suffix_unreadable");return;}
        if(sample.frame==IndexedTarget()+index_seek_samples_) return;
        if(sample.frame!=IndexedTarget()+1+index_seek_samples_ || boundary_failed_ || !boundary_observer_.healthy())
        {Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed suffix rejected target={} samples={} observed={} boundary_failed={} observer_healthy={}\n"),
            IndexedTarget(),index_seek_samples_,sample.frame,boundary_failed_,boundary_observer_.healthy());
            Fail("indexed_seek_suffix_gap");return;}
        LogTrajectorySample(sample,++index_seek_samples_,L"indexed_seek_trajectory");
        if((!request_.probe_interactive_controls && index_seek_samples_==120)
            || (request_.probe_interactive_controls && playback_controls_.completed && sample.frame==playback_controls_.sample.frame+120)) {
            const auto start=request_.probe_interactive_controls?playback_controls_.resumed_at:index_seek_resume_started_;
            const auto origin=request_.probe_interactive_controls?playback_controls_.sample.frame:IndexedTarget();
            const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed seek resume rate run_id={} target={} ticks=120 elapsed_us={} origin={}\n"),RC::to_generic_string(request_.run_id),IndexedTarget(),elapsed,origin);
        }
        if(index_seek_samples_==request_.index_seek_continuation) {
            if(request_.probe_interactive_controls && !playback_controls_.completed) {Fail("playback_controls_unexercised");return;}
            index_seek_phase_=IndexSeek::Done;
            trajectory_finish_deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed seek continuation run_id={} first={} last={} ticks={}\n"),RC::to_generic_string(request_.run_id),IndexedTarget()+1,IndexedTarget()+request_.index_seek_continuation,request_.index_seek_continuation);
            if(request_.index_sequence && index_sequence_position_+1<ReplayQualification::IndexSequence(request_.index_sequence).size()) {
                const auto arm=ResolveHorseModExport<bool (*)(std::uint64_t,void*,ReplayHost::InteriorObserver)>("horsemod_arm_replay_application_pause");
                index_seek_phase_=IndexSeek::ArmingNext;
                if(!arm || !arm(sample.frame+1,this,[](void* context,const ReplayHost::InteriorWitness& held) {
                    return static_cast<ReplayQualificationMod*>(context)->ObserveIndexedSequenceBoundary(held);
                }))Fail("indexed_sequence_hold_arm_failed");
                return;
            }
            if(request_.index_sequence)Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed sequence completed run_id={} sequence={} cases={} continuation_ticks_each=120\n"),
                RC::to_generic_string(request_.run_id),request_.index_sequence,index_sequence_position_+1);
            CompleteTrajectory();
        }
    }

    void ObserveIndexEndHold(const ReplayHost::InteriorWitness& held)
    {
        if(state_==State::Failed || held.phase!=ReplayHost::InteriorPhase::Holding
            || held.surface_pending || held.application_updates<30 || held.surface_frames<10) return;
        const bool released=PublishIndex();
        if(request_.index_seek) {
            if(index_seek_phase_==IndexSeek::Arming) BeginIndexedSeek(held);
            return;
        }
        if(!released) return;
        const auto clear=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
        const auto resume=ResolveHorseModExport<std::uint16_t (*)()>("horsemod_resume_replay_execution");
        if(!clear || !clear(nullptr,nullptr) || !resume || resume()!=0) Fail("index_end_hold_release_failed");
    }

    bool PublishIndex()
    {
        using Index=Horse::Deterministic::ReplayTickIndex;
        const auto operation=ResolveHorseModExport<bool (*)(ReplayHost::IndexAction,Index::Witness*,std::size_t)>("horsemod_replay_index_operation");
        const auto read=ResolveHorseModExport<bool (*)(std::uint64_t,Index::Entry*)>("horsemod_replay_index_entry");
        Index::Witness state{};
        if(index_publication_==IndexPublication::Complete) return true;
        if(request_.index_checkpoint && index_capture_!=IndexCapture::Resuming)
        {Fail("index_checkpoint_missing_at_completion");return false;}
        if(index_publication_==IndexPublication::Retiring) {
            if(!operation || !operation(ReplayHost::IndexAction::Read,&state,0))
            {Fail("replay_index_retirement_read_failed");return false;}
            if(state.phase==Index::Phase::Releasing) return false;
            if(state.phase!=Index::Phase::Empty || state.bytes)
            {Fail("replay_index_retirement_failed");return false;}
            index_publication_=IndexPublication::Complete;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] index released run_id={} bytes=0 hooks_removed=true\n"),RC::to_generic_string(request_.run_id));
            return true;
        }
        if(request_.index_seek && index_seek_phase_!=IndexSeek::Idle && index_seek_phase_!=IndexSeek::EndHold)
            return index_seek_phase_==IndexSeek::Done?ReleaseObservedIndex():false;
        if(!operation || !read || !operation(ReplayHost::IndexAction::Read,&state,0)) {
            Fail("replay_index_completion_read_failed");return false;
        }
        const bool retained_stop=request_.host_session && request_.index_seek
            && state.end_policy==Index::EndPolicy::RetainedSourceStop;
        // The source-stop observation runs in EnginePost, before the host's
        // application owner arms its end hold. Attach only after that owner
        // records the stop and arms the following completed application.
        if(retained_stop && state.phase==Index::Phase::Recording && !state.source_stopped)return false;
        if(state.phase==Index::Phase::Recording && ((state.native_finish && state.final_tail) || retained_stop)) {
            if(index_seek_phase_!=IndexSeek::EndHold) {
                const auto monitor=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
                if(!monitor || !monitor(this,[](void* context,const ReplayHost::InteriorWitness& held) {
                    static_cast<ReplayQualificationMod*>(context)->ObserveIndexEndHold(held);
                })) {Fail("index_automatic_end_hold_missing");return false;}
                index_seek_phase_=IndexSeek::EndHold;
                trajectory_finish_deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            }
            return false; // Still recording native tail work; no seek admission.
        }
        ReplayHost::InteriorWitness held{};
        const auto read_hold=ResolveHorseModExport<bool (*)(ReplayHost::InteriorWitness*)>("horsemod_get_replay_interior_witness");
        const bool endpoint_valid=retained_stop
            ?state.source_stopped && state.unsupported_native_tail && !state.native_finish && !state.final_tail && !match_finish_observed_
            :state.native_finish && state.final_tail;
        if(state.phase!=Index::Phase::Complete || !endpoint_valid
            || !read_hold || !read_hold(&held) || held.phase!=ReplayHost::InteriorPhase::Holding
            || held.surface_pending || !held.application_idle || !held.engine_idle || !held.world_idle
            || state.entries!=held.tick+1 || held.tick<=trajectory_last_.frame
            || held.tick-trajectory_last_.frame>300 || state.bytes>4*1024*1024) {
            Output::send<LogLevel::Warning>(STR("[ReplayQualification] index completion failure phase={} failure={} entries={} source_tick={} held_tick={} final_tail={} native_finish={}\n"),
                static_cast<unsigned>(state.phase),static_cast<unsigned>(state.failure),state.entries,trajectory_last_.frame,held.tick,state.final_tail,state.native_finish);
            Fail("replay_index_completion_failed");return false;
        }
        for(std::uint64_t tick=0;tick<state.entries;++tick) {
            Index::Entry row{};
            if(!read(tick,&row) || row.tick!=tick || row.native_tick!=tick)
            {Fail("replay_index_entry_failed");return false;}
            Output::send<LogLevel::Default>(STR("[ReplayQualification] index row tick={} native_tick={} interval={} publications={} round={} round_tick={} cursor={} cache_offset={} remaining_inputs={} phase={} round_state={} source_active={} move_state={}\n"),
                row.tick,row.native_tick,row.interval,row.publications,row.round,row.round_tick,row.source_cursor,
                row.cache_offset,row.remaining_inputs,row.execution_phase,row.round_state,row.source_active,row.move_state);
        }
        if(retained_stop) {
            Output::send<LogLevel::Default>(STR("[ReplayQualification] index supported complete run_id={} entries={} bytes={} intervals={} empty={} multi={} native_finish=false unsupported_native_tail=true\n"),
                RC::to_generic_string(request_.run_id),state.entries,state.bytes,state.completed_intervals,state.zero_tick_intervals,state.multi_tick_intervals);
            Output::send<LogLevel::Default>(STR("[ReplayQualification] index supported range run_id={} source_tick={} last_tick={} tail_ticks={} all_retained_ticks_indexed=true unsupported_native_tail=true\n"),
                RC::to_generic_string(request_.run_id),trajectory_last_.frame,held.tick,held.tick-trajectory_last_.frame);
        } else {
            Output::send<LogLevel::Default>(STR("[ReplayQualification] index complete run_id={} entries={} bytes={} intervals={} empty={} multi={} native_finish=true final_tail=true\n"),
                RC::to_generic_string(request_.run_id),state.entries,state.bytes,state.completed_intervals,state.zero_tick_intervals,state.multi_tick_intervals);
            Output::send<LogLevel::Default>(STR("[ReplayQualification] index retained range run_id={} source_tick={} last_tick={} tail_ticks={} all_native_ticks_indexed=true\n"),
                RC::to_generic_string(request_.run_id),trajectory_last_.frame,held.tick,held.tick-trajectory_last_.frame);
        }
        if(request_.index_seek) {index_seek_phase_=IndexSeek::Arming;return false;}
        return ReleaseObservedIndex();
    }

    bool ReleaseObservedIndex()
    {
        using Index=Horse::Deterministic::ReplayTickIndex;
        const auto operation=ResolveHorseModExport<bool (*)(ReplayHost::IndexAction,Index::Witness*,std::size_t)>("horsemod_replay_index_operation");
        Index::Witness state{};
        if(!operation) {Fail("index_release_export_missing");return false;}
        if(!operation(ReplayHost::IndexAction::Release,&state,0)
            || (state.phase!=Index::Phase::Empty && state.phase!=Index::Phase::Releasing))
        {Fail("replay_index_release_failed");return false;}
        index_publication_=IndexPublication::Retiring;
        if(state.phase==Index::Phase::Releasing) return false;
        index_publication_=IndexPublication::Complete;
        Output::send<LogLevel::Default>(STR("[ReplayQualification] index released run_id={} bytes=0 hooks_removed=true\n"),RC::to_generic_string(request_.run_id));
        return true;
    }

    #include "ReplayNativeSessionExit.inl"

    void CompleteTrajectory()
    {
        if(request_.native_session_exit && request_.executor_control && native_exit_phase_==NativeExitPhase::Idle) {
            BeginNativeSessionExit();return;
        }
        if(request_.record_index && !request_.index_cancel && !request_.native_session_exit && native_exit_phase_!=NativeExitPhase::Complete) {
            if(!index_endpoint_pending_) {
                index_endpoint_pending_=true;
                trajectory_finish_deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                Output::send<LogLevel::Default>(STR("[ReplayQualification] index terminal observed run_id={} replay_tick={} frozen=true\n"),RC::to_generic_string(request_.run_id),trajectory_last_.frame);
            }
            // Freeze authored trajectory coordinates; the index continues
            // through the retained native application boundary.
            // Continue observing every native callback through scene completion
            // and the following application tail in BOTH independent runs.
            // An independent control observes extra native finish updates so
            // the candidate's retained tail is independently covered. Its stop
            // condition never supplies inputs or state to native execution.
            const bool retained_stop=request_.host_session && request_.index_seek && request_.executor_control;
            if(!retained_stop && (!match_finish_observed_ || ++index_finish_updates_<(request_.executor_control?2u:16u))) return;
            std::uint32_t physical_tick{};
            if(!ReadSchedulerValue(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))+0x470d0c4,physical_tick)
                || (physical_tick<trajectory_last_.frame && !request_.index_seek)) {Fail("index_closure_clock_failed");return;}
            if(request_.executor_control && !PublishIndex()) return;
            if(!request_.index_seek) Output::send<LogLevel::Default>(STR("[ReplayQualification] index closure run_id={} replay_tick={} native_tick={} callbacks_retained=true\n"),
                RC::to_generic_string(request_.run_id),trajectory_last_.frame,physical_tick);
            index_endpoint_pending_=false;
        }
        if(request_.probe_consumer_task || request_.consumer_mutation) {
            const auto read=ResolveHorseModExport<bool(*)(std::uint64_t*,std::size_t)>("horsemod_read_niagara_observation_status");
            std::uint64_t observation[2]{};
            const bool valid=read && read(observation,2);
            Output::send<LogLevel::Default>(STR("[ReplayQualification] Niagara observation status run_id={} available={} records={} persistence_failed={} admission_proof=false\n"),
                RC::to_generic_string(request_.run_id),valid,observation[0],observation[1]);
            if(!valid || observation[1]) {Fail("niagara_observation_persistence_failed");return;}
            // Close the diagnostic interval at this status boundary. Later
            // completion checks still decide the trajectory result; neither
            // phase completeness nor termination establishes B recovery.
            const auto vfx_close=ResolveHorseModExport<bool(*)(std::uint64_t)>("horsemod_close_vfx_completion_observation_phase");
            const bool vfx_phase_complete=vfx_close && vfx_close(trajectory_last_.frame);
            Output::send<LogLevel::Default>(STR("[ReplayQualification] VFX completion observation phase run_id={} end_tick={} complete={} scope=startup_to_trajectory_completion whole_process_coverage=false native_quiescence_proven=false\n"),
                RC::to_generic_string(request_.run_id),trajectory_last_.frame,vfx_phase_complete);
            const auto vfx_read=ResolveHorseModExport<bool(*)(std::uint64_t*,std::size_t)>("horsemod_read_vfx_completion_observation_status");
            std::uint64_t vfx[6]{};
            const bool vfx_available=vfx_read && vfx_read(vfx,6);
            Output::send<LogLevel::Default>(STR("[ReplayQualification] VFX completion observation status run_id={} available={} records={} dropped={} persistence_failed={} pending_pairs={} unstable_records={} truncated_records={} admission_proof=false writer_coverage_proven=false\n"),
                RC::to_generic_string(request_.run_id),vfx_available,vfx[0],vfx[1],vfx[2],vfx[3],vfx[4],vfx[5]);
            // Observation validity is separate from native execution/recovery.
            // Missing/overflowed/failed records grant no guard or recovery claim.
        }
        if(request_.probe_consumer_task || request_.consumer_mutation) {
            const auto physics_close=ResolveHorseModExport<bool(*)(std::uint64_t)>("horsemod_close_physics_callback_observation_phase");
            const bool physics_complete=physics_close && physics_close(trajectory_last_.frame);
            const auto physics_read=ResolveHorseModExport<bool(*)(std::uint64_t*,std::size_t)>("horsemod_read_physics_callback_observation_status");
            std::uint64_t physics[4]{};
            const bool physics_available=physics_read && physics_read(physics,4);
            Output::send<LogLevel::Default>(STR("[ReplayQualification] physics callback observation run_id={} end_tick={} complete={} available={} records={} dropped={} persistence_failed={} pending_pairs={} lifetime_proven=false recovery_proven=false\n"),
                RC::to_generic_string(request_.run_id),trajectory_last_.frame,physics_complete,physics_available,physics[0],physics[1],physics[2],physics[3]);
            // End physics callback observation. No enforcement or recovery decision.
        }
        const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            (native_exit_phase_==NativeExitPhase::Complete?native_exit_started_:std::chrono::steady_clock::now()) - trajectory_timing_started_).count();
        const auto viewport_frames = (native_exit_phase_==NativeExitPhase::Complete?native_exit_viewport_:viewport_frame_count_.load(std::memory_order_relaxed)) - trajectory_viewport_start_;
        const auto ticks = trajectory_last_.frame - trajectory_native_start_;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] trajectory timing run_id={} viewport_frames={} native_ticks={} elapsed_us={} viewport_fps_milli={} tick_rate_milli={} max_observation_gap_us={} gaps_over_20ms={}\n"),
            RC::to_generic_string(request_.run_id), viewport_frames, ticks, elapsed_us,
            elapsed_us > 0 ? viewport_frames * 1'000'000'000ull / elapsed_us : 0,
            elapsed_us > 0 ? ticks * 1'000'000'000ull / elapsed_us : 0,
            trajectory_max_gap_us_, trajectory_late_gaps_);
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] trajectory work timing run_id={} intervals={} engine_us={} "
            "max_engine_us={} observer_us={} max_observer_us={} pacing_us={}\n"),
            RC::to_generic_string(request_.run_id), trajectory_samples_ - 1,
            trajectory_engine_us_, trajectory_max_engine_us_, trajectory_observer_us_, trajectory_max_observer_us_,
            trajectory_pacing_us_);

        if ((!boundary_started_ && native_exit_phase_!=NativeExitPhase::Complete) || boundary_failed_ || boundary_samples_ == 0
            || (request_.probe_empty_interval && !empty_interval_observed_)
            || (request_.probe_move_state && !move_state_observed_)
            || (request_.probe_world_pause && (!world_pause_completed_ || !world_resume_measured_))
            || ((request_.probe_interior_pause || request_.probe_application_pause) && !interior_completed_)
            || (request_.probe_small_restore && !small_restore_completed_)
            || (request_.probe_historical_restore && ((!request_.historical_cancel.empty()
                ? !historical_cancel_requested_ : (request_.historical_anchor_tick<=205 && !historical_birth_reproduced_)) || !world_resume_measured_))
            || (native_exit_phase_!=NativeExitPhase::Complete && !boundary_observer_.Stop()))
        { Fail("boundary_observer_completion_failed"); return; }
        boundary_started_ = false;
        if (request_.skip_intros && native_exit_phase_!=NativeExitPhase::Complete && !presentation_observer_.Stop())
        { Fail("native_presentation_completion_failed"); return; }
        for (std::size_t i = 0; i < actor_family_count_; ++i)
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] actor family calls vtable_rva={:x} caller_rva={:x} count={}\n"),
                actor_families_[i].vtable, actor_families_[i].caller, actor_families_[i].calls);
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] boundaries completed run_id={} observations={} detached=true\n"),
            RC::to_generic_string(request_.run_id), boundary_samples_);
        if (request_.executor_control)
        {
            const auto read = ResolveHorseModExport<bool (*)(std::uint64_t*, std::size_t)>(
                "horsemod_get_replay_executor_status");
            const auto enable = ResolveHorseModExport<bool (*)(bool, bool, void*)>(
                "horsemod_set_replay_executor_enabled");
            std::array<std::uint64_t, 12> status{};
            const auto read_world = ResolveHorseModExport<bool (*)(std::uint64_t*, std::size_t)>(
                "horsemod_get_replay_host_status");
            std::array<std::uint64_t, 15> world_status{};
            if(native_exit_phase_==NativeExitPhase::Complete) world_status=native_exit_world_;
            if (!read_world || (native_exit_phase_!=NativeExitPhase::Complete && !read_world(world_status.data(), world_status.size()))
                || world_status[0] || world_status[2] || !world_status[3] || world_status[4] != 1
                || !world_status[5] || !world_status[6] || !world_status[7]
                || world_status[9] < world_status[6] || world_status[11] != 1
                || !world_status[13] || world_status[14] != 1
                || (request_.executor_yield_every_tick && (world_status[12] < 2 || world_status[10] < 64)))
            { Fail("world_executor_completion_failed"); return; }
            if(native_exit_phase_==NativeExitPhase::Complete) status=native_exit_executor_;
            if (!read || (native_exit_phase_!=NativeExitPhase::Complete && !read(status.data(), status.size())) || status[0] != 1
                || status[1] != 0 || status[2] == 0 || status[5] != 0
                || status[6] != status[3] + (historical_execution_started_ ? historical_rewind_intervals_ : 0)
                || world_status[7] + (empty_interval_observed_ ? 1 : 0)
                    + (move_state_observed_ ? 1 : 0) != status[6]
                || world_status[8] + (move_state_observed_ && request_.executor_yield_every_tick ? 1 : 0) != status[11]
                || (request_.executor_yield_every_tick && status[11] != status[2] + restore_trial_ticks_
                    + (historical_execution_started_ ? historical_rewind_ticks_ : 0))
                || !enable || !enable(false, false, nullptr))
            {
                Output::send<LogLevel::Warning>(STR(
                    "[ReplayQualification] executor completion mismatch enabled={} phase={} ticks={} intervals={} "
                    "failure={} completed={} world_managers={} manager_yields={} api_yields={} direct_move_interval={}\n"),
                    status[0], status[1], status[2], status[3], status[5], status[6],
                    world_status[7], world_status[8], status[11], move_state_observed_);
                Fail("executor_completion_failed"); return;
            }
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] world executor completed run_id={} worlds={} groups={} tasks={} "
                "manager_tasks={} manager_yields={} idle=true disabled=true\n"),
                RC::to_generic_string(request_.run_id), world_status[3], world_status[5], world_status[6],
                world_status[7], world_status[8]);
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] engine executor completed run_id={} intervals={} idle=true disabled=true\n"),
                RC::to_generic_string(request_.run_id), world_status[13]);
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] world arena completed run_id={} scopes={} max_retained_bytes={} probe_checks={} empty=true\n"),
                RC::to_generic_string(request_.run_id), world_status[9], world_status[10], world_status[12]);
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] executor boundaries run_id={} completed_intervals={} "
                "zero_intervals={} multi_intervals={} repeat_requests={} move_state_ticks={} "
                "yielded_boundaries={} yield_every_tick={}\n"),
                RC::to_generic_string(request_.run_id), status[6], status[7], status[8],
                status[9], status[10], status[11], request_.executor_yield_every_tick);
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] executor completed run_id={} ticks={} intervals={} "
                "publications={} native_frame={} disabled=true\n"),
                RC::to_generic_string(request_.run_id), status[2], status[3], status[4],
                trajectory_last_.frame);
        }
        if (request_.trajectory_to_end)
        {
            if(request_.host_session && request_.index_seek && request_.executor_control) {
                if(trajectory_last_.world_mode!=5 || trajectory_last_.round_state!=5
                    || trajectory_last_.source_active || !round_outcomes_verified_ || match_finish_observed_) {
                    Fail("retained_source_endpoint_invalid");return;
                }
                LogOrderedRoundOutcomes();
                Output::send<LogLevel::Default>(STR("[ReplayQualification] retained replay endpoint run_id={} native_frame={} round={} cursor={} world_mode=5 round_state=5 source_active=false native_finish=false unsupported_native_tail=true\n"),
                    RC::to_generic_string(request_.run_id),trajectory_last_.frame,trajectory_last_.round,trajectory_last_.cursor);
                PublishTrajectoryResult();return;
            }
            if (trajectory_last_.world_mode != 10 || trajectory_last_.round_state != 10
                || trajectory_last_.source_active || !round_outcomes_verified_)
            { Fail("native_endpoint_invalid"); return; }
            LogOrderedRoundOutcomes();
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] native replay endpoint run_id={} native_frame={} round={} cursor={} world_mode=10 round_state=10 source_active=false outer_complete=true\n"),
                RC::to_generic_string(request_.run_id), trajectory_last_.frame,
                trajectory_last_.round, trajectory_last_.cursor);
            trajectory_capture_complete_ = true;
            trajectory_finish_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            if (!match_finish_observed_) return;
        }
        PublishTrajectoryResult();
    }

    void PublishTrajectoryResult()
    {
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] trajectory completed run_id={} observations={} full_match={}\n"),
            RC::to_generic_string(request_.run_id), trajectory_samples_, request_.trajectory_to_end
                && !(request_.host_session && request_.index_seek && request_.executor_control));
        importer_.ReleasePlaybackContext();
        WriteResult("launch_requested", "trajectory_control_complete");
        state_ = State::Launched;
    }

    void TryRegisterBattleTerminateHook()
    {
        if (battle_terminate_hook_registered_) return;
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

    void TryRegisterMatchFinishHook()
    {
        using namespace RC::Unreal;
        if (match_finish_hook_registered_ || !UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, match_finish_hook_path_)) return;
        try
        {
            match_finish_hook_ids_ = UObjectGlobals::RegisterHook(match_finish_hook_path_,
                [](UnrealScriptFunctionCallableContext&, void*) {},
                [](UnrealScriptFunctionCallableContext&, void*) {
                    auto* self = s_instance_.load(std::memory_order_acquire);
                    if (!self || self->state_ != State::WaitingForLaunch
                        || !self->request_.trajectory_to_end || self->match_finish_observed_) return;
                    self->match_finish_observed_ = true;
                    Output::send<LogLevel::Default>(STR(
                        "[ReplayQualification] native match finished run_id={} boundary=ReplayBattleScene.OnFinishMatch.post\n"),
                        RC::to_generic_string(self->request_.run_id));
                }, nullptr);
            match_finish_hook_registered_ = match_finish_hook_ids_.first != 0 || match_finish_hook_ids_.second != 0;
        }
        catch (...) {}
    }

    void LoadRequest()
    {
        Request request{};
        if (!ReadRequest(QualificationRoot() / L"replay_request.txt", request)) {
            // A recognized request with an invalid option combination is not
            // an idle replay. Report once without admitting its partial data.
            if(ValidRunId(request.run_id) && request.run_id!=rejected_run_id_) {
                rejected_run_id_=request.run_id;
                Output::send<LogLevel::Warning>(STR("[ReplayQualification] replay request rejected run_id={} reason=invalid_protocol_options\n"),RC::to_generic_string(request.run_id));
            }
            return;
        }
        if (request.run_id == last_run_id_) return;
        last_run_id_ = request.run_id;
        request_ = std::move(request);
        started_ = std::chrono::steady_clock::now();
        player_profiles_requested_ = false;
        playback_context_staged_ = false;
        battle_scene_observed_ = false;
        battle_terminate_observed_ = false;
        match_finish_observed_ = false;
        terminal_snapshot_captured_ = false;
        replay_scene_ready_ = false;
        executor_started_ = false;
        native_exit_phase_=NativeExitPhase::Idle;
        boundary_started_ = boundary_failed_ = false;
        boundary_samples_ = 0;
        empty_interval_observed_ = false;
        world_pause_active_ = world_pause_completed_ = false;
        interior_started_ = interior_release_requested_ = interior_completed_ = false;
        interior_expected_tick_ = 360;
        combat_pause_armed_ = intro_skip_sent_ = false;
        intro_skip_mode_ = 0;
        application_target_tick_ = application_reentry_tick_ = 0;
        small_restore_completed_ = restore_trial_active_ = false;
        restore_trial_ticks_ = restore_trial_boundaries_ = 0;
        boundary_last_round_sequence_ = false;
        completion_repeat_count_=completion_repeat_phase_=0;
        completion_repeat_event_=completion_repeat_task_=nullptr;
        completion_repeat_sample_={};completion_repeat_epoch_=completion_repeat_callbacks_=0;
        completion_repeat_updates_=completion_repeat_frames_=0;completion_repeat_started_={};
        world_resume_measured_ = false;
        world_resume_max_gap_us_ = world_resume_late_gaps_ = 0;
        actor_family_count_ = 0;
        setup_samples_ = 0;
        setup_last_ = {};
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
        qualification_clock_ = nullptr;
        battle_clock_start_ = {};
        active_clock_start_ = {};
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
        seek_resume_rate_ = {};
        seek_resume_last_round_state_frame_ = 0;
        seek_resume_observation_active_ = false;
        seek_wait_log_counter_ = 0;
        phase_wait_log_counter_ = 0;
        outcome_wait_log_counter_ = 0;
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
        qualification_performance_window_started_ = false;
        qualification_recovery_window_started_ = false;
        qualification_recovery_clock_start_ = {};
        qualification_recovery_rate_started_at_ = {};
        qualification_group_run_id_.clear();
        arm_qualification_group_ = nullptr;
        get_qualification_group_row_report_ = nullptr;
        disarm_qualification_cycle_ = nullptr;
        state_ = State::Importing;
        trajectory_samples_ = 0;
        c18_diagnostic_logged_=false;
        c18_diagnostic_arm_=c18_diagnostic_epoch_=c18_diagnostic_player_=0;
        trajectory_capture_complete_ = false;
        trajectory_source_rounds_ = 0;
        trajectory_last_ = {};
        historical_target_sample_ = {};
        host_seek_original_sample_.reset();
        historical_execution_started_ = false;
        host_seek_phase_=ReplayHost::SeekPhase::Idle;
        index_finish_updates_=0;index_endpoint_pending_=false;index_publication_=IndexPublication::Pending;
        indexed_recovery_=IndexedRecovery::Idle;index_sequence_position_=0;index_seek_phase_=IndexSeek::Idle;index_seek_samples_=index_capture_interval_=0;index_preparation_trial_=IndexPreparationTrial::Idle;
        index_capture_=IndexCapture::Waiting;index_capture_sample_={};index_host_completion_seen_=false;
        index_capture_tick_=170;index_selected_checkpoint_=index_first_interval_=index_origin_interval_=0;
        index_checkpoint_count_=0;index_checkpoint_coordinates_={};
        index_capture_callbacks_=index_capture_epoch_=index_capture_applications_=0;
        host_seek_failure_injected_=host_seek_failure_observed_=false;
        host_seek_release_waiting_=false;host_seek_release_checkpoint_.reset();host_seek_repeated_=false;host_seek_rewind_ticks_=host_seek_rewind_intervals_=0;
        host_seek_selected_tick_=host_seek_original_interval_=0;host_seek_execution_fallback_seen_=false;
        historical_rewind_ticks_ = historical_rewind_intervals_ = 0;
        combat_checkpoint_.reset();pair_checkpoint_.reset();pair_B_armed_=pair_capture_started_=false;pair_sample_={};pair_capture_fingerprint_=0;capture_cancel_stage_=0;restore_reuse_completed_=false;reuse_checkpoint_fingerprint_=0;
        source_revision_applied_=false;
        historical_operation_started_ = historical_target_held_ = historical_birth_reproduced_ = false;
        historical_first_failure_.clear();
        historical_cancel_requested_ = false;
        historical_hold_started_ = {};
        historical_hold_frames_ = 0;
        if (request_.trajectory_control)
        {
            // When present, exercise the runtime's capture path. The absent
            // control has no dependency on this export or on HorseMod clocks.
            const auto set_history = ResolveHorseModExport<SetReplayHistoryCaptureRequiredFn>(
                "horsemod_set_replay_history_capture_required");
            if (set_history != nullptr && !set_history(true))
            {
                Fail("trajectory_runtime_history_unavailable");
                return;
            }
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] accepted passive trajectory run_id={} samples={}\n"),
                RC::to_generic_string(request_.run_id), request_.watch_frames);
            return;
        }
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
            Output::send<LogLevel::Error>(STR("[ReplayQualification] import failed status={} identity_phase={}\n"),
                RC::to_generic_string(std::string(Horse::Qualification::import_failure_name(imported))),
                RC::to_generic_string(std::string(importer_.identity_phase())));
            Fail(Horse::Qualification::import_failure_name(imported));
            return;
        }
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] replay metadata stage={} map={} "
            "left_character={} right_character={} state_reset_records={} recorded_match_winner={}\n"),
            metadata.stage_index,
            metadata.stage_index > 0xff
                ? metadata.stage_index & 0xff : metadata.stage_index,
            metadata.left_character, metadata.right_character,
            metadata.state_reset_record_count, metadata.recorded_match_winner);
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
        const bool terminal_failure =
            Horse::Qualification::IsTerminalReplayQualificationHealth(health);
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

    bool LogFinalQualificationRates()
    {
        std::array<std::uint64_t, 5> clock{};
        if (!ReadQualificationClock(clock)
            || clock[1] < battle_clock_start_[1]
            || clock[2] < battle_clock_start_[2]
            || clock[3] < battle_clock_start_[3]
            || clock[1] < active_clock_start_[1]
            || clock[2] < active_clock_start_[2]
            || clock[3] < active_clock_start_[3])
        {
            Fail("horsemod_final_qualification_clock_invalid");
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto log_window = [&](const TCHAR* label,
                                    const auto& start,
                                    const auto& started_at) noexcept {
            const auto elapsed_us = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    now - started_at).count());
            const auto viewport_frames = clock[1] - start[1];
            const auto ticks = clock[2] - start[2];
            const auto owned = clock[3] - start[3];
            const auto fps_milli = elapsed_us == 0 ? 0
                : viewport_frames * 1'000'000'000ull / elapsed_us;
            const auto tick_rate_milli = elapsed_us == 0 ? 0
                : ticks * 1'000'000'000ull / elapsed_us;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] normal-render {} rate "
                "viewport_frames={} native_ticks={} owned_ticks={} "
                "elapsed_us={} fps_milli={} tick_rate_milli={}\n"), label,
                viewport_frames,
                ticks, owned, elapsed_us, fps_milli, tick_rate_milli);
            return fps_milli >= request_.min_resume_tick_rate_milli
                && tick_rate_milli >= request_.min_resume_tick_rate_milli;
        };
        const bool overall = log_window(STR("battle"), battle_clock_start_,
            battle_rate_started_at_);
        const bool active = log_window(STR("active battle"),
            active_clock_start_, battle_active_rate_started_at_);
        if (!overall || !active)
        {
            Fail("normal_render_qualification_rate_below_minimum");
            return false;
        }
        return true;
    }

    void LogQualificationStressRate(
        const std::array<std::uint64_t, 5>& clock,
        const std::chrono::steady_clock::time_point now) const noexcept
    {
        if (clock[1] < battle_clock_start_[1]
            || clock[2] < battle_clock_start_[2]
            || clock[3] < battle_clock_start_[3])
            return;
        const auto elapsed_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                now - battle_rate_started_at_).count());
        const auto viewport_frames = clock[1] - battle_clock_start_[1];
        const auto ticks = clock[2] - battle_clock_start_[2];
        const auto owned = clock[3] - battle_clock_start_[3];
        const auto fps_milli = elapsed_us == 0 ? 0
            : viewport_frames * 1'000'000'000ull / elapsed_us;
        const auto tick_rate_milli = elapsed_us == 0 ? 0
            : ticks * 1'000'000'000ull / elapsed_us;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] normal-render qualification stress rate "
            "viewport_frames={} native_ticks={} owned_ticks={} elapsed_us={} "
            "fps_milli={} tick_rate_milli={}\n"), viewport_frames, ticks,
            owned, elapsed_us, fps_milli, tick_rate_milli);
    }

    bool PollQualificationCycles(bool allow_completion = true)
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
            if (!ReadQualificationClock(battle_clock_start_))
            {
                Fail("horsemod_qualification_clock_unavailable_after_arm");
                return true;
            }
            active_clock_start_ = battle_clock_start_;
            battle_rate_started_at_ = std::chrono::steady_clock::now();
            battle_active_rate_started_at_ = battle_rate_started_at_;
            qualification_performance_window_started_ = true;
            qualification_recovery_window_started_ = false;
            qualification_recovery_clock_start_ = {};
            qualification_recovery_rate_started_at_ = {};
            return true;
        }
        std::array<std::uint64_t, 51> first_report{};
        const auto status = get_qualification_group_row_report_(
            qualification_group_run_id_.data(),
            qualification_group_run_id_.size(), 0, first_report.data(),
            first_report.size());
        if (status == 0) return true;
        if (status != 3 && status != 4) return true;
        if (!allow_completion) return true;
        if (!qualification_performance_window_started_)
        {
            Fail("horsemod_qualification_performance_window_missing");
            return true;
        }
        std::array<std::uint64_t, 5> performance_clock{};
        if (!ReadQualificationClock(performance_clock)
            || performance_clock[1] < battle_clock_start_[1]
            || performance_clock[2] < battle_clock_start_[2])
        {
            Fail("horsemod_qualification_performance_clock_invalid");
            return true;
        }
        // Preserve the actual synthetic stress throughput, then prove that
        // normal rendering recovers after the terminal group releases its
        // presentation ownership. The correction p99/max budgets above still
        // gate every depth independently; this window detects persistent lag.
        if (!qualification_recovery_window_started_)
        {
            const auto now = std::chrono::steady_clock::now();
            LogQualificationStressRate(performance_clock, now);
            qualification_recovery_clock_start_ = performance_clock;
            qualification_recovery_rate_started_at_ = now;
            qualification_recovery_window_started_ = true;
            return true;
        }
        if (performance_clock[1] - qualification_recovery_clock_start_[1]
                < request_.resume_tick_window
            || performance_clock[2] - qualification_recovery_clock_start_[2]
                < request_.resume_tick_window)
            return true;
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        const bool memory_valid = K32GetProcessMemoryInfo(
            GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(
                &memory), sizeof(memory)) != FALSE;
        std::array<std::array<std::uint64_t, 51>, 3> reports{};
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
                "failure_anchor={} failure_repeat={} arm_ms={}\n"),
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
                report[46], report[47], report[48], report[49], report[50]);
        }
        const auto group_location = request_.qualification_cycles[
            qualification_cycle_index_].location;
        const bool anchor_identity_valid = reports[0][47] != 0
            && reports[1][47] != 0 && reports[2][47] != 0
            // Production cadence assigns successive authored outer ticks to
            // depths 11, 1, and 6, so each row intentionally hashes a
            // different coordinate sequence. Other locations restore every
            // depth at the same anchors and must remain exactly equal.
            && (group_location == 6
                || (reports[0][47] == reports[1][47]
                    && reports[0][47] == reports[2][47]));
        const bool disarmed = disarm_qualification_cycle_(
            qualification_group_run_id_.data(),
            qualification_group_run_id_.size());
        qualification_cycle_armed_ = false;
        std::array<std::uint64_t, 51> cleanup{};
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
            battle_clock_start_ = qualification_recovery_clock_start_;
            active_clock_start_ = qualification_recovery_clock_start_;
            battle_rate_started_at_ = qualification_recovery_rate_started_at_;
            battle_active_rate_started_at_ =
                qualification_recovery_rate_started_at_;
            if (!LogFinalQualificationRates()) return true;
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
        // The passive control uses only native reads and the shared importer.
        // It must remain operable with HorseMod absent from the process.
        if (request_.trajectory_control) return;
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
            qualification_clock_ = ResolveHorseModExport<
                GetQualificationClockFn>(
                    "horsemod_get_qualification_clock_v1");
            if (qualification_clock_ == nullptr)
            {
                Fail("horsemod_qualification_clock_unavailable");
                return;
            }
            if (!ReadQualificationClock(battle_clock_start_))
            {
                Fail("horsemod_qualification_clock_unavailable");
                return;
            }
            active_clock_start_ = battle_clock_start_;
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
            std::array<std::uint64_t, 5> clock{};
            if (!ReadQualificationClock(clock)
                || clock[1] < battle_clock_start_[1]
                || clock[2] < battle_clock_start_[2]
                || clock[3] < battle_clock_start_[3])
            {
                Fail("horsemod_qualification_clock_invalid");
                return;
            }
            const auto measured_frames = clock[1] - battle_clock_start_[1];
            const auto measured_ticks = clock[2] - battle_clock_start_[2];
            const auto owned_ticks = clock[3] - battle_clock_start_[3];
            const auto fps_milli = elapsed_us == 0 ? 0
                : measured_frames * 1'000'000'000ull / elapsed_us;
            const auto tick_rate_milli = elapsed_us == 0 ? 0
                : measured_ticks * 1'000'000'000ull / elapsed_us;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] normal-render battle rate "
                "viewport_frames={} native_ticks={} owned_ticks={} elapsed_us={} "
                "fps_milli={} tick_rate_milli={}\n"),
                measured_frames, measured_ticks, owned_ticks, elapsed_us,
                fps_milli, tick_rate_milli);
            battle_rate_logged_ = true;
            if (fps_milli < request_.min_resume_tick_rate_milli
                || tick_rate_milli < request_.min_resume_tick_rate_milli)
            {
                Output::send<LogLevel::Error>(STR(
                    "[ReplayQualification] normal-render battle rate failed "
                    "viewport_frames={} native_ticks={} elapsed_us={} fps_milli={} "
                    "tick_rate_milli={} minimum={}\n"),
                    measured_frames, measured_ticks, elapsed_us, fps_milli,
                    tick_rate_milli,
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
                if (!ReadQualificationClock(active_clock_start_))
                {
                    Fail("horsemod_active_qualification_clock_reset_failed");
                    return;
                }
            }
            else if (frame - battle_active_rate_start_frame_
                >= (request_.qualification_cycles.empty()
                    ? request_.resume_tick_window : request_.watch_frames))
            {
                const auto elapsed_us = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        now - battle_active_rate_started_at_).count());
                std::array<std::uint64_t, 5> clock{};
                if (!ReadQualificationClock(clock)
                    || clock[1] < active_clock_start_[1]
                    || clock[2] < active_clock_start_[2]
                    || clock[3] < active_clock_start_[3])
                {
                    Fail("horsemod_active_qualification_clock_invalid");
                    return;
                }
                const auto measured_frames = clock[1] - active_clock_start_[1];
                const auto measured_ticks = clock[2] - active_clock_start_[2];
                const auto owned_ticks = clock[3] - active_clock_start_[3];
                const auto fps_milli = elapsed_us == 0 ? 0
                    : measured_frames * 1'000'000'000ull / elapsed_us;
                const auto tick_rate_milli = elapsed_us == 0 ? 0
                    : measured_ticks * 1'000'000'000ull / elapsed_us;
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] normal-render active battle rate "
                    "viewport_frames={} native_ticks={} owned_ticks={} elapsed_us={} "
                    "fps_milli={} tick_rate_milli={}\n"),
                    measured_frames, measured_ticks, owned_ticks, elapsed_us,
                    fps_milli, tick_rate_milli);
                battle_active_rate_logged_ = true;
                if (fps_milli < request_.min_resume_tick_rate_milli
                    || tick_rate_milli < request_.min_resume_tick_rate_milli)
                {
                    Output::send<LogLevel::Error>(STR(
                        "[ReplayQualification] normal-render active battle "
                        "rate failed viewport_frames={} native_ticks={} "
                        "elapsed_us={} fps_milli={} tick_rate_milli={} "
                        "minimum={}\n"),
                        measured_frames, measured_ticks, elapsed_us,
                        fps_milli, tick_rate_milli,
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
        std::array<std::uint64_t, 56> rng_coverage{};
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
            "tira_last_writer_move=0x{:04x} tira_helper_attempts={} "
            "tira_helper_exact_draws={} tira_helper_writes={} "
            "tira_helper_no_write={} tira_helper_no_change={} "
            "tira_helper_signature_failures={} "
            "tira_helper_last_enclosing_move=0x{:04x} "
            "tira_helper_last_chance={} tira_helper_last_result={} "
            "tira_helper_last_rejection_mask=0x{:x}\n"),
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
            rng_coverage[45], rng_coverage[46], rng_coverage[47],
            rng_coverage[48], rng_coverage[49], rng_coverage[50],
            rng_coverage[51], rng_coverage[52], rng_coverage[53],
            static_cast<std::int64_t>(rng_coverage[54]), rng_coverage[55]);
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
        const bool result_available = TryReadBattleResult(
            battle_manager_, result);
        if (++outcome_wait_log_counter_ >= 600)
        {
            outcome_wait_log_counter_ = 0;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] authored outcome progress "
                "available={} timer={} result_type={} round_winner={} "
                "match_winner={} observed_rounds={} expected_rounds={}\n"),
                result_available ? STR("yes") : STR("no"),
                result.timer_seconds, result.result_type,
                result.round_winner_index, result.match_winner_index,
                observed_round_winner_count_,
                request_.expected_round_winners.size());
        }
        if (!result_available) return false;
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
            outcome_wait_log_counter_ = 0;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] authored round outcome observed "
                "ordinal={} winner={} result_type={} timer={} "
                "match_winner={}\n"), observed_round_winner_count_,
                observed, result.result_type, result.timer_seconds,
                result.match_winner_index);
        }
        if (result.match_winner_index < 0) return false;
        const bool recorded_winner_matches = replay_metadata_.recorded_match_winner >= 0
            && replay_metadata_.recorded_match_winner <= 1
            && replay_metadata_.recorded_match_winner == result.match_winner_index;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] recorded match outcome run_id={} recorded={} simulated={} equal={}\n"),
            RC::to_generic_string(request_.run_id), replay_metadata_.recorded_match_winner,
            result.match_winner_index, recorded_winner_matches);
        if (!recorded_winner_matches)
        {
            Fail("recorded_match_winner_mismatch");
            return false;
        }
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
            if (!seek_resume_rate_.Sample(frame,static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count())))
            {
                Fail("horsemod_seek_resume_clock_or_tick_regression");
                return;
            }
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
            const auto elapsed_us = seek_resume_rate_.elapsed_us();
            if (seek_resume_rate_.ticks() < request_.resume_tick_window
                || elapsed_us == 0)
            {
                Fail("horsemod_seek_resume_clock_invalid");
                return;
            }
            const auto tick_rate_milli = seek_resume_rate_.ticks()
                * 1'000'000'000ull
                / static_cast<std::uint64_t>(elapsed_us);
            if (tick_rate_milli < request_.min_resume_tick_rate_milli)
            {
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] strict seek live rate failed "
                    "percent={} live_resumed={} wall_elapsed_us={} "
                    "resume_window={} resume_tick_rate_milli={} minimum={}\n"),
                    percentage, live_frames, elapsed_us,
                    seek_resume_rate_.ticks(), tick_rate_milli,
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
                seek_resume_rate_.ticks(), elapsed_us, tick_rate_milli,
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
                std::int32_t native_round{}, native_time{}, unpause_countdown{};
                std::uint32_t round_state_frame{};
                if (!get_phase_(&native_round, &native_time,
                        &round_state_frame, &unpause_countdown))
                {
                    Fail("horsemod_seek_resume_phase_unavailable");
                    return;
                }
                seek_resume_rate_.Begin(frame,static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count()),request_.resume_tick_window);
                seek_resume_native_round_ = native_round;
                seek_resume_last_round_state_frame_ = round_state_frame;
                seek_resume_observation_active_ = true;
            }
            return;
        }

        std::uint64_t generation{}, first{}, last{};
        std::int32_t native_round{}, native_time{}, unpause_countdown{};
        std::uint32_t round_state_frame{};
        const bool range_available = get_range_(&generation, &first, &last);
        const bool phase_available = get_phase_(&native_round, &native_time,
            &round_state_frame, &unpause_countdown);
        const auto readiness =
            Horse::Qualification::ClassifyReplaySeekReadiness(
                range_available, first, last, request_.watch_frames,
                phase_available, round_state_frame);
        if (readiness != Horse::Qualification::ReplaySeekReadiness::Ready)
        {
            if (++seek_wait_log_counter_ >= 120)
            {
                seek_wait_log_counter_ = 0;
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] strict seek waiting reason={} "
                    "range_available={} generation={} range={}-{} "
                    "required_span={} phase_available={} round={} time={} "
                    "round_state_frame={} unpause={} index={}\n"),
                    RC::to_generic_string(std::string(
                        Horse::Qualification::ReplaySeekReadinessName(
                            readiness))),
                    range_available ? 1 : 0, generation, first, last,
                    request_.watch_frames, phase_available ? 1 : 0,
                    native_round, native_time, round_state_frame,
                    unpause_countdown, seek_index_);
            }
            return;
        }
        seek_wait_log_counter_ = 0;
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

    void Fail(std::string_view reason,bool preserve_native_hold=false)
    {
        if(request_.c18_diagnostic) {
            const auto invalidate=ResolveHorseModExport<void(*)(const char*)>("horsemod_invalidate_collection18_combat_observation");
            if(invalidate)invalidate(request_.run_id.c_str());
        }
        const bool first=state_!=State::Failed;
        // A second diagnostic callback must not turn a constructor failure's
        // retained native hold into executor teardown. The runner owns process
        // cleanup for this bounded probe once it has failed.
        if(!first && request_.particle_owner_copy)return;
        state_=State::Failed;
        if(first) Output::send<LogLevel::Warning>(STR("[ReplayQualification] run failed run_id={} reason={}\n"),RC::to_generic_string(request_.run_id),RC::to_generic_string(reason));
        if(preserve_native_hold) {
            Output::send<LogLevel::Warning>(STR("[ReplayQualification] native owner construction failure retains application hold and leases for owned-process cleanup; in-session recovery unproven\n"));
            return;
        }
        if (!UndoPreparedEmitters() && first)
        {
            // Development failure containment: keep the published graph alive
            // until the runner terminates the owned process. This is not a
            // successful recoverable replay lifecycle result.
            Output::send<LogLevel::Warning>(STR("[ReplayQualification] cpu replacement undo unavailable; retaining published allocation for process cleanup\n"));
        }
        if (!RetireParticleObjectLeases() && first)
            Output::send<LogLevel::Warning>(STR("[ReplayQualification] particle object leases retained until CPU undo and GPU retirement; lifecycle cleanup unproven\n"));
        if (executor_started_)
        {
            const auto disable = ResolveHorseModExport<bool (*)(bool, bool, void*)>("horsemod_set_replay_executor_enabled");
            const bool detached = disable && disable(false, false, nullptr);
            if(first || detached) Output::send<LogLevel::Warning>(STR("[ReplayQualification] executor failure cleanup detached={}\n"), detached);
            if (detached) executor_started_ = world_pause_active_ = false;
        }
        if (boundary_started_)
        {
            const bool detached = boundary_observer_.Stop();
            if(first || detached) Output::send<LogLevel::Warning>(STR(
                "[ReplayQualification] boundary failure cleanup detached={}\n"), detached);
            boundary_started_ = !detached;
        }
        if (request_.skip_intros && !presentation_observer_.Stop() && first)
            Output::send<LogLevel::Warning>(STR("[ReplayQualification] native presentation failure retained until owned-process cleanup\n"));
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
        if(first) WriteResult("failed", reason);
        else RetryResultPublication(); // Preserve the first cause while cleanup can make progress.
    }

    void WriteResult(std::string_view result, std::string_view reason)
    {
        const std::filesystem::path root = QualificationRoot();
        std::error_code error;
        std::filesystem::create_directories(root, error);
        const auto temporary = root / L"replay_result.tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << "version=1\nrun_id=" << request_.run_id
               << "\nresult=" << result << "\nreason=" << reason << '\n';
        stream.close();
        if (error || !stream)
        {
            Output::send<LogLevel::Error>(STR(
                "[ReplayQualification] result publication failed run_id={} phase=write error={}\n"),
                RC::to_generic_string(request_.run_id), error.value());
            state_ = State::Failed;
            return;
        }
        result_publication_pending_ = true;
        result_publication_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        result_publication_attempts_ = 0;
        RetryResultPublication();
    }

    void RetryResultPublication()
    {
        if (!result_publication_pending_) return;
        const auto root = QualificationRoot();
        ++result_publication_attempts_;
        if (MoveFileExW((root / L"replay_result.tmp").c_str(),
            (root / L"replay_result.txt").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            result_publication_pending_ = false;
            if (result_publication_attempts_ > 1)
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] result publication recovered run_id={} attempts={}\n"),
                    RC::to_generic_string(request_.run_id), result_publication_attempts_);
            return;
        }
        const auto error = GetLastError();
        // The Python reader can briefly deny replacement on Windows. Retain
        // the completed file and retry on later game-thread ticks, without
        // blocking simulation or publishing a partial destination.
        if ((error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION)
            && std::chrono::steady_clock::now() < result_publication_deadline_) return;
        result_publication_pending_ = false;
        state_ = State::Failed;
        Output::send<LogLevel::Error>(STR(
            "[ReplayQualification] result publication failed run_id={} phase=replace error={} attempts={}\n"),
            RC::to_generic_string(request_.run_id), error, result_publication_attempts_);
    }

    Horse::Qualification::ReplayPayloadImporter importer_{};
    Horse::Qualification::ReplaySceneNavigator navigator_{};
    Horse::Qualification::ReplayMetadata replay_metadata_{};
    static inline std::atomic<ReplayQualificationMod*> s_instance_{nullptr};
    Request request_{};
    ReplayTrajectorySample trajectory_last_{};
    std::uint32_t trajectory_samples_{};
    bool c18_diagnostic_logged_{};
    std::uint64_t c18_diagnostic_arm_{},c18_diagnostic_epoch_{};
    std::uintptr_t c18_diagnostic_player_{};
    bool executor_started_{};
    ReplayBoundaryObserver boundary_observer_{};
    bool boundary_started_{}, boundary_failed_{};
    bool empty_interval_observed_{};
    bool move_state_observed_{};
    bool world_pause_active_{}, world_pause_completed_{};
    bool interior_started_{}, interior_release_requested_{}, interior_completed_{};
    bool small_restore_completed_{}, restore_trial_active_{};
    std::uint32_t interior_expected_tick_{360}, restore_trial_ticks_{}, restore_trial_boundaries_{};
    int intro_skip_mode_{};
    bool intro_skip_sent_{}, combat_pause_armed_{};
    std::uint32_t application_target_tick_{}, application_reentry_tick_{};
    std::array<ParticleIdentityObservation, 4096> particle_identities_{};
    std::size_t particle_identity_count_{};
    bool boundary_last_round_sequence_{};
    ReplayTrajectorySample boundary_last_{}, interior_sample_{};
    const void* interior_task_{};
    const void* interior_event_{};
    std::uint64_t interior_epoch_{}, interior_boundaries_{}, interior_surface_start_{};
    std::uint64_t application_hold_completed_{};
    unsigned capture_cancel_stage_{};
    bool restore_reuse_completed_{};
    std::uint64_t reuse_checkpoint_fingerprint_{};
    ReplayHost::CheckpointHandle combat_checkpoint_;
    ReplayHost::CheckpointHandle pair_checkpoint_;
    ReplayTrajectorySample pair_sample_{};
    bool pair_B_armed_{},pair_capture_started_{};
    std::uint64_t pair_capture_fingerprint_{};

    std::shared_ptr<ReplayHost::Checkpoint> application_checkpoint_;
    bool historical_invalid_actions_checked_{};
    std::uint64_t application_checkpoint_fingerprint_{};
    std::array<std::size_t, 3> scheduler_inventory_counts_{};
    SchedulerTickObservation application_scheduler_tick_{};
    bool application_reentry_armed_{};
    bool historical_exact_requested_{}, historical_exact_started_{}, historical_exact_held_{};
    bool historical_step_control_waiting_{};
    unsigned completion_repeat_count_{},completion_repeat_phase_{};
    const void* completion_repeat_event_{};const void* completion_repeat_task_{};
    ReplayTrajectorySample completion_repeat_sample_{};
    std::uint64_t completion_repeat_epoch_{},completion_repeat_callbacks_{},completion_repeat_updates_{},completion_repeat_frames_{};
    std::chrono::steady_clock::time_point completion_repeat_started_{};
    ReplayHost::SeekPhase host_seek_phase_{ReplayHost::SeekPhase::Idle};
    unsigned index_finish_updates_{};
    using IndexSeek=ReplayQualification::IndexSeek;
    IndexSeek index_seek_phase_{};
    enum class IndexPreparationTrial { Idle, FirstArmed, Cancelling, Renewed, FinalArmed };
    IndexPreparationTrial index_preparation_trial_{};
    ReplayTrajectorySample index_preparation_B_{};
    std::uint64_t index_preparation_callbacks_{},index_preparation_epoch_{};
    std::uint64_t index_capture_interval_{},index_seek_samples_{};
    std::uint64_t index_capture_tick_{170},index_selected_checkpoint_{},index_first_interval_{},index_origin_interval_{};
    std::array<std::pair<std::uint64_t,std::uint64_t>,16> index_checkpoint_coordinates_{};
    std::size_t index_checkpoint_count_{};
    ReplayTrajectorySample index_seek_held_sample_{};
    std::uint64_t index_seek_held_callbacks_{},index_seek_held_epoch_{},index_seek_held_frames_{};
    std::chrono::steady_clock::time_point index_seek_held_started_{},index_seek_requested_{},index_seek_resume_started_{};
    std::chrono::steady_clock::time_point index_seek_completion_started_{};
    std::int64_t index_seek_prepared_us_{},index_seek_hold_us_{};
    enum class IndexPublication { Pending, Retiring, Complete };
    IndexPublication index_publication_{};
    enum class IndexCapture { Waiting, Armed, Capturing, Retained, Resuming, Cancelling };
    IndexCapture index_capture_{};
    bool index_host_completion_seen_{};
    ReplayTrajectorySample index_capture_sample_{};
    std::uint64_t index_capture_callbacks_{},index_capture_epoch_{},index_capture_applications_{};
    bool index_endpoint_pending_{};
    bool host_seek_release_waiting_{},host_seek_repeated_{};
    std::optional<std::uint64_t> host_seek_release_checkpoint_;
    bool host_seek_failure_injected_{},host_seek_failure_observed_{};
    std::uint64_t host_seek_rewind_ticks_{},host_seek_rewind_intervals_{};
    std::uint64_t host_seek_selected_tick_{},host_seek_original_interval_{};
    bool host_seek_execution_fallback_seen_{};
    bool particle_copy_started_{}, particle_copy_failed_{}, particle_copy_failure_logged_{}, particle_copy_finish_requested_{};
    bool particle_publication_release_checked_{}, particle_publication_cancel_requested_{}, particle_publication_recovery_requested_{};
    std::array<bool, 2> particle_partition_observed_{};
    std::array<Horse::Deterministic::Sc6ReplayObjectLease, 2> particle_object_leases_;
    Horse::Deterministic::Sc6ReplayObjectLease particle_owner_copy_lease_;
    Horse::Deterministic::Sc6ReplayCpuEmitterState::FreshGpuOwner particle_gpu_owner_;
    bool particle_gpu_retirement_pending_{};
    std::uint64_t particle_gpu_retirement_serial_{};
    std::unique_ptr<Horse::Deterministic::Sc6ReplayVfxState> particle_vfx_image_;
    std::unique_ptr<Horse::Deterministic::Sc6ReplayVfxState> particle_vfx_undo_;
    std::unique_ptr<Horse::Deterministic::Sc6ReplayVfxState::PreparedTilePools> particle_pool_restore_;
    std::unique_ptr<Horse::Deterministic::Sc6ReplayVfxState::PreparedEmitterSet> particle_cpu_historical_set_;
    ReplayTrajectorySample historical_target_sample_{};
    std::optional<ReplayTrajectorySample> host_seek_original_sample_;
    std::chrono::steady_clock::time_point historical_capture_started_{}, historical_restore_started_{};
    ReplayPresentationObserver presentation_observer_;
    std::string startup_observation_run_;
    bool historical_execution_started_{};
    std::uint64_t historical_rewind_ticks_{}, historical_rewind_intervals_{};
    bool source_revision_applied_{};
    bool historical_operation_started_{}, historical_target_held_{};
    std::string historical_first_failure_;
    bool historical_birth_reproduced_{};
    bool historical_cancel_requested_{};
    std::chrono::steady_clock::time_point historical_hold_started_{};
    std::uint64_t historical_hold_frames_{};
    std::unique_ptr<Horse::Deterministic::Sc6ReplayVfxState::PreparedEmitterSet> particle_gpu_historical_set_;
    Horse::Deterministic::Sc6ReplayVfxState::ParticleBirth particle_birth_;
    std::unique_ptr<Horse::Deterministic::Sc6ReplayCpuEmitterState::Prepared> particle_cpu_prepared_;
    void* particle_cpu_original_{}; void* particle_cpu_component_{};
    std::size_t particle_cpu_ordinal_{};
    std::uint64_t particle_cpu_fingerprint_{}, particle_cpu_updates_{}, particle_cpu_frames_{};
    std::chrono::steady_clock::time_point particle_cpu_started_{};
    bool particle_cpu_undone_{}, particle_cpu_historical_prepared_{};
    SceneReferenceObservation application_scene_reference_{};
    std::unique_ptr<ReplayScheduler::PreparedRestore> application_scheduler_prepared_;
    std::uint64_t application_scheduler_prepared_fingerprint_{};
    std::chrono::steady_clock::time_point interior_started_at_{};
    std::chrono::steady_clock::time_point world_pause_started_{};
    std::uint64_t world_pause_viewport_{}, world_pause_boundaries_{};
    bool world_resume_measured_{};
    std::chrono::steady_clock::time_point world_resume_started_{}, world_resume_previous_{};
    std::uint32_t world_resume_frame_{};
    std::uint64_t world_resume_viewport_{}, world_resume_max_gap_us_{}, world_resume_late_gaps_{};
    struct ActorFamily { std::uintptr_t vtable{}, caller{}; std::uint64_t calls{}; };
    std::array<ActorFamily, 128> actor_families_{};
    std::size_t actor_family_count_{};
    std::uint32_t boundary_samples_{};
    std::uint32_t setup_samples_{};
    ReplayTrajectorySample setup_last_{};
    bool trajectory_capture_complete_{};
    std::int32_t trajectory_source_rounds_{};
    std::chrono::steady_clock::time_point trajectory_finish_deadline_{};
    std::chrono::steady_clock::time_point trajectory_timing_started_{};
    std::chrono::steady_clock::time_point trajectory_timing_previous_{};
    std::uint64_t trajectory_max_gap_us_{}, trajectory_late_gaps_{};
    RC::Unreal::Hook::GlobalCallbackId engine_tick_pre_id_{RC::Unreal::Hook::ERROR_ID};
    std::chrono::steady_clock::time_point engine_tick_started_{};
    std::uint64_t engine_work_us_{}, observer_work_us_{};
    std::uint64_t trajectory_engine_us_{}, trajectory_observer_us_{};
    std::uint64_t trajectory_max_engine_us_{}, trajectory_max_observer_us_{};
    double trajectory_pacing_us_{};
    std::uint64_t trajectory_viewport_start_{};
    std::uint32_t trajectory_native_start_{};
    bool result_publication_pending_{};
    std::uint32_t result_publication_attempts_{};
    std::chrono::steady_clock::time_point result_publication_deadline_{};
    std::string last_run_id_{};
    std::string rejected_run_id_{};
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
    RC::StringType match_finish_hook_path_{
        STR("/Game/UI/GameFlow/GameScenes/Battle/ReplayBattleScene.ReplayBattleScene_C:OnFinishMatch")};
    std::pair<int, int> match_finish_hook_ids_{};
    bool match_finish_hook_registered_{}, match_finish_observed_{};
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
    GetQualificationClockFn qualification_clock_{};
    std::array<std::uint64_t, 5> battle_clock_start_{};
    std::array<std::uint64_t, 5> active_clock_start_{};
    RC::Unreal::Hook::GlobalCallbackId viewport_tick_id_{
        RC::Unreal::Hook::ERROR_ID};
    std::atomic<std::uint64_t> viewport_frame_count_{};
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
    bool qualification_performance_window_started_{};
    bool qualification_recovery_window_started_{};
    std::array<std::uint64_t, 5> qualification_recovery_clock_start_{};
    std::chrono::steady_clock::time_point
        qualification_recovery_rate_started_at_{};
    std::string qualification_group_run_id_{};
    bool stage_terminal_requested_{};
    bool stage_terminal_completed_{};
    std::uint32_t stage_terminal_operation_{};
    std::uint32_t seek_resume_start_frame_{};
    Horse::Qualification::ReplayResumeRateWindow seek_resume_rate_{};
    std::int32_t seek_resume_native_round_{};
    std::uint32_t seek_resume_last_round_state_frame_{};
    bool seek_resume_observation_active_{};
    std::uint16_t phase_wait_log_counter_{};
    std::uint16_t outcome_wait_log_counter_{};
    std::uint16_t seek_wait_log_counter_{};
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
