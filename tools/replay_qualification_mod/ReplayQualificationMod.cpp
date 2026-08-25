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
};

using RequestReplaySeekFn = bool (*)(std::uint64_t);
using GetReplaySeekStatusFn = std::uint32_t (*)(
    std::uint64_t*, std::uint64_t*, std::uint64_t*, std::uint16_t*);
using GetReplaySeekableRangeFn = bool (*)(
    std::uint64_t*, std::uint64_t*, std::uint64_t*);
using GetReplaySimulationPhaseFn = bool (*)(
    std::int32_t*, std::int32_t*, std::uint32_t*, std::int32_t*);

bool ResolveHorseModSeekApi(
    RequestReplaySeekFn& request, GetReplaySeekStatusFn& status,
    GetReplaySeekableRangeFn& range,
    GetReplaySimulationPhaseFn& phase) noexcept
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
        if (candidate_request != nullptr && candidate_status != nullptr
            && candidate_range != nullptr && candidate_phase != nullptr)
        {
            request = candidate_request;
            status = candidate_status;
            range = candidate_range;
            phase = candidate_phase;
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
    if (!stream.eof() || !ValidRunId(fields["run_id"])
        || (fields["version"] != "2" && fields["version"] != "3")
        || (fields["version"] == "2" && fields.size() != 4)
        || (fields["version"] == "3" && fields.size() != 5))
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
    if (fields["version"] == "3")
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
    output = {fields["run_id"], std::filesystem::path(replay_path),
        watch_frames, std::move(percentages)};
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
        if (std::chrono::steady_clock::now() - started_ > std::chrono::seconds(140))
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
        if (!ResolveHorseModSeekApi(unused_request, unused_status,
                unused_range, get_phase))
        {
            Fail("horsemod_simulation_phase_api_unavailable");
            return;
        }
        std::int32_t native_round{}, native_time{}, unpause_countdown{};
        std::uint32_t round_state_frame{};
        if (!get_phase(&native_round, &native_time, &round_state_frame,
                &unpause_countdown)
            || round_state_frame == 0 || unpause_countdown != 0)
        {
            return;
        }
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
            PollSeekQualification();
            return;
        }
        state_ = State::Launched;
        WriteResult("launch_requested", "none");
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] replay simulation frame advanced "
            "initial={} current={} watched={}\n"), initial_battle_frame_, frame,
            request_.watch_frames);
    }

    void PollSeekQualification()
    {
        RequestReplaySeekFn request_seek{};
        GetReplaySeekStatusFn get_status{};
        GetReplaySeekableRangeFn get_range{};
        GetReplaySimulationPhaseFn get_phase{};
        if (!ResolveHorseModSeekApi(
                request_seek, get_status, get_range, get_phase))
        {
            Fail("horsemod_seek_api_unavailable");
            return;
        }

        const auto percentage = request_.seek_percentages[seek_index_];
        if (seek_requested_)
        {
            std::uint64_t observed_target{};
            std::uint64_t source_end{};
            std::uint64_t verified{};
            std::uint16_t failure{};
            const auto status = get_status(
                &observed_target, &source_end, &verified, &failure);
            if (status == 3)
            {
                Fail("horsemod_seek_validation_failed");
                return;
            }
            if (status == 1 && observed_target == requested_seek_target_
                && source_end != 0)
            {
                Output::send<LogLevel::Default>(STR(
                    "[ReplayQualification] strict seek passed percent={} "
                    "target={} source_end={} verified={} index={}\n"),
                    percentage, observed_target, source_end, verified,
                    seek_index_);
                ++seek_index_;
                seek_requested_ = false;
                requested_seek_target_ = 0;
            }
            return;
        }

        std::uint64_t generation{}, first{}, last{};
        std::int32_t native_round{}, native_time{}, unpause_countdown{};
        std::uint32_t round_state_frame{};
        if (!get_range(&generation, &first, &last) || first >= last
            || last - first < request_.watch_frames)
        {
            return;
        }
        if (!get_phase(&native_round, &native_time, &round_state_frame,
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
        if (!request_seek(target))
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
            if (now < next_profile_attempt_) return;
            ++profile_attempts_;
            if (!importer_.RequestPlayerProfiles())
            {
                if (profile_attempts_ >= 3)
                {
                    if (!importer_.PopulateFallbackProfiles())
                    {
                        Fail("player_profile_fallback_failed");
                        return;
                    }
                    player_profiles_requested_ = true;
                    player_profiles_requested_at_ =
                        now - std::chrono::seconds(2);
                    Output::send<LogLevel::Default>(STR(
                        "[ReplayQualification] native profile service "
                        "unavailable; staged bounded replay profiles\n"));
                    return;
                }
                next_profile_attempt_ = now + std::chrono::seconds(1);
                return;
            }
            player_profiles_requested_ = true;
            player_profiles_requested_at_ = now;
            Output::send<LogLevel::Default>(STR(
                "[ReplayQualification] replay player profiles requested\n"));
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
