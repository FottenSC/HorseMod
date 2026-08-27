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

#include <DynamicOutput/DynamicOutput.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
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
    std::string run_id;
    std::filesystem::path replay_path;
    std::uint32_t watch_frames{1};
    std::vector<std::uint32_t> seek_percentages{};
    std::uint32_t min_resume_tick_rate_milli{58'000};
    std::uint32_t resume_tick_window{120};
};

using RequestReplaySeekFn = bool (*)(std::uint64_t);
using GetReplaySeekStatusFn = std::uint32_t (*)(
    std::uint64_t*, std::uint64_t*, std::uint64_t*, std::uint16_t*);
using GetReplaySeekableRangeFn = bool (*)(
    std::uint64_t*, std::uint64_t*, std::uint64_t*);
using GetReplaySimulationPhaseFn = bool (*)(
    std::int32_t*, std::int32_t*, std::uint32_t*, std::int32_t*);
using GetReplaySeekMetricsFn = bool (*)(std::uint64_t*, std::uint64_t*);
using GetReplayPresentationCoverageFn = bool (*)(std::uint64_t*, std::size_t);

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
    if (!stream.eof() || !ValidRunId(fields["run_id"])
        || (fields["version"] != "2" && fields["version"] != "3"
            && fields["version"] != "4")
        || (fields["version"] == "2" && fields.size() != 4)
        || (fields["version"] == "3" && fields.size() != 5)
        || (fields["version"] == "4" && fields.size() != 7))
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
    if (fields["version"] == "3" || fields["version"] == "4")
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
        if (percentages.empty()) return false;
    }
    std::uint32_t min_resume_tick_rate_milli = 58'000;
    std::uint32_t resume_tick_window = 120;
    if (fields["version"] == "4")
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
    output = {fields["run_id"], std::filesystem::path(replay_path),
        watch_frames, std::move(percentages), min_resume_tick_rate_milli,
        resume_tick_window};
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
        if (!bound_ || state_ == State::Launched || state_ == State::Failed) return;
        if (++poll_divider_ < 15) return;
        poll_divider_ = 0;
        if (state_ == State::Idle) LoadRequest();
        if (state_ == State::Importing) StartRequest();
        if (state_ == State::WaitingForLaunch) PollLaunch();
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
        initial_battle_frame_ = 0;
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
        profile_attempts_ = 0;
        next_profile_attempt_ = {};
        state_ = State::Importing;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] accepted replay request run_id={}\n"),
            RC::to_generic_string(request_.run_id));
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
            "left_character={} right_character={}\n"),
            metadata.stage_index,
            metadata.stage_index > 0xff
                ? metadata.stage_index & 0xff : metadata.stage_index,
            metadata.left_character, metadata.right_character);
        state_ = State::WaitingForLaunch;
        WriteResult("waiting_for_launch", "none");
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
        std::string navigation_detail;
        const Horse::Qualification::NavigationState navigation =
            navigator_.Tick(playback_context_staged_, navigation_detail);
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
        if (navigation == Horse::Qualification::NavigationState::ReplayListReady)
        {
            PollPlayerProfiles();
            return;
        }
        if (navigation != Horse::Qualification::NavigationState::Ready) return;
        std::uint32_t frame = 0;
        if (!ReadLuxBattleFrame(frame))
        {
            Fail("battle_frame_counter_unreadable");
            return;
        }
        RequestReplaySeekFn unused_request{};
        GetReplaySeekStatusFn unused_status{};
        GetReplaySeekableRangeFn unused_range{};
        GetReplaySimulationPhaseFn get_phase{};
        GetReplaySeekMetricsFn unused_metrics{};
        if (!ResolveHorseModSeekApi(unused_request, unused_status,
                unused_range, get_phase, unused_metrics))
        {
            Fail("horsemod_simulation_phase_api_unavailable");
            return;
        }
        std::int32_t native_round{}, native_time{}, unpause_countdown{};
        std::uint32_t round_state_frame{};
        const bool phase_available = get_phase(&native_round, &native_time,
            &round_state_frame, &unpause_countdown);
        if (!phase_available || round_state_frame == 0
            || unpause_countdown != 0)
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
        phase_wait_log_counter_ = 0;
        if (!battle_scene_observed_)
        {
            battle_scene_observed_ = true;
            initial_battle_frame_ = frame;
            importer_.ReleasePlaybackContext();
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] stock replay battle observed "
                "frame={} round={} time={} round_state_frame={}\n"), frame,
                native_round, native_time, round_state_frame);
            return;
        }
        const std::uint32_t advanced = frame - initial_battle_frame_;
        if (advanced < request_.watch_frames) return;
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
        state_ = State::Launched;
        WriteResult("launch_requested", "none");
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] replay simulation frame advanced "
            "initial={} current={} watched={}\n"), initial_battle_frame_, frame,
            request_.watch_frames);
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
            std::int32_t native_round{}, native_time{}, unpause_countdown{};
            std::uint32_t round_state_frame{};
            if (!get_phase_(&native_round, &native_time, &round_state_frame,
                    &unpause_countdown))
            {
                Fail("horsemod_seek_resume_phase_unavailable");
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (seek_resume_last_active_at_.time_since_epoch().count() != 0
                && frame > seek_resume_last_observed_frame_
                && native_round == seek_resume_native_round_
                && round_state_frame >= seek_resume_last_round_state_frame_)
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
            seek_resume_native_round_ = native_round;
            seek_resume_last_round_state_frame_ = round_state_frame;
            std::uint64_t unused_target{}, unused_source{}, unused_verified{};
            std::uint16_t failure{};
            if (get_status_(&unused_target, &unused_source, &unused_verified,
                    &failure) == 3)
            {
                Fail("horsemod_seek_live_resume_failed");
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
        if (seek_range_generation_ == 0)
        {
            seek_range_generation_ = generation;
            seek_range_first_ = first;
            seek_range_last_ = last;
        }
        if (generation != seek_range_generation_)
        {
            Fail("horsemod_seek_generation_changed");
            return;
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
        const auto now = std::chrono::steady_clock::now();
        if (!player_profiles_requested_)
        {
            // Replay qualification does not need live online profile contents.
            // The native request is asynchronous and its successful submission
            // does not mean the two profile slots are ready; staging after a
            // fixed delay can therefore enter ReplayBattleScene with no active
            // replay phase. Populate the same bounded native profile values the
            // existing failure fallback uses and avoid that external race.
            if (!importer_.PopulateFallbackProfiles())
            {
                Fail("player_profile_fallback_failed");
                return;
            }
            player_profiles_requested_ = true;
            player_profiles_requested_at_ = now - std::chrono::seconds(2);
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] staged bounded replay profiles\n"));
            return;
        }
        if (now - player_profiles_requested_at_ < std::chrono::seconds(2))
            return;
        if (!importer_.ApplyPlaybackContext())
        {
            Fail("playback_context_apply_failed");
            return;
        }
        RC::Unreal::UObject* instance = FindGameInstance();
        if (instance == nullptr
            || !CallNoParams(instance, L"ApplyReplayToBattleSetup"))
        {
            Fail("apply_replay_to_battle_setup_failed");
            return;
        }
        playback_context_staged_ = true;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] native replay playback context and "
            "battle setup staged\n"));
    }

    void Fail(std::string_view reason)
    {
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
    static inline std::atomic<ReplayQualificationMod*> s_instance_{nullptr};
    Request request_{};
    std::string last_run_id_{};
    std::string last_navigation_detail_{};
    std::chrono::steady_clock::time_point started_{};
    std::chrono::steady_clock::time_point player_profiles_requested_at_{};
    std::chrono::steady_clock::time_point next_profile_attempt_{};
    State state_{State::Idle};
    std::uint32_t poll_divider_{};
    RC::Unreal::Hook::GlobalCallbackId engine_tick_id_{
        RC::Unreal::Hook::ERROR_ID};
    bool bound_{};
    bool waiting_context_logged_{};
    bool player_profiles_requested_{};
    bool playback_context_staged_{};
    bool battle_scene_observed_{};
    std::uint32_t initial_battle_frame_{};
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
    std::uint32_t seek_resume_start_frame_{};
    std::uint64_t seek_resume_rate_frames_{};
    std::uint64_t seek_resume_rate_elapsed_us_{};
    std::uint32_t seek_resume_last_observed_frame_{};
    std::chrono::steady_clock::time_point seek_resume_last_active_at_{};
    std::int32_t seek_resume_native_round_{};
    std::uint32_t seek_resume_last_round_state_frame_{};
    bool seek_resume_observation_active_{};
    std::uint16_t phase_wait_log_counter_{};
    std::uint8_t profile_attempts_{};
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
