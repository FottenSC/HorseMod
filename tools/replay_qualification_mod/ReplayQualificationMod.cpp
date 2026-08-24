#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "ReplayPayloadImporter.hpp"
#include "ReplaySceneNavigator.hpp"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

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
    WaitingForAssets,
    Launched,
    Failed,
};

struct Request
{
    std::string run_id;
    std::filesystem::path replay_path;
};

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
    if (!stream.eof() || fields.size() != 3 || fields["version"] != "1"
        || !ValidRunId(fields["run_id"]))
    {
        return false;
    }
    const std::wstring replay_path = Widen(fields["replay_path"]);
    if (replay_path.empty()) return false;
    output = {fields["run_id"], std::filesystem::path(replay_path)};
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

RC::Unreal::UObject* FindGameInstance() noexcept
{
    RC::Unreal::UObject* instance =
        RC::Unreal::UObjectGlobals::FindFirstOf(L"LuxGameInstance");
    return instance != nullptr && RC::Unreal::UObject::IsReal(instance)
        ? instance : nullptr;
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

bool CallBool(RC::Unreal::UObject* object, const wchar_t* name,
              bool& output) noexcept
{
    output = false;
    if (object == nullptr) return false;
    __try
    {
        auto* function = object->GetFunctionByNameInChain(name);
        if (function == nullptr) return false;
        struct Params { bool result{}; } params{};
        object->ProcessEvent(function, &params);
        output = params.result;
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

bool SetReplayPath(RC::Unreal::UObject* setup,
                   const std::filesystem::path& replay_path) noexcept
{
    if (setup == nullptr) return false;
    auto* replay = setup->GetValuePtrByPropertyNameInChain<std::byte>(
        L"BattleReplay");
    if (replay == nullptr) return false;
    *reinterpret_cast<bool*>(replay + 0x00) = false;
    *reinterpret_cast<bool*>(replay + 0x18) = true;
    *reinterpret_cast<bool*>(replay + 0x30) = false;
    auto* recording = reinterpret_cast<RC::Unreal::FString*>(replay + 0x08);
    auto* playing = reinterpret_cast<RC::Unreal::FString*>(replay + 0x20);
    *recording = RC::Unreal::FString(L"");
    *playing = RC::Unreal::FString(replay_path.c_str());
    return true;
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
        if (state_ == State::WaitingForAssets) PollLaunch();
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
        if (!CallNoParams(instance, L"ApplyReplayToBattleSetup")
            || !SetReplayPath(GetBattleSetup(instance), request_.replay_path)
            || !importer_.QueueStageMap(instance, metadata)
            || !SetReplayPath(GetBattleSetup(instance), request_.replay_path))
        {
            Fail("battle_setup_or_asset_request_failed");
            return;
        }
        state_ = State::WaitingForAssets;
        WriteResult("waiting_for_assets", "none");
    }

    void PollLaunch()
    {
        if (std::chrono::steady_clock::now() - started_ > std::chrono::seconds(140))
        {
            Fail("asset_wait_timeout");
            return;
        }
        RC::Unreal::UObject* instance = FindGameInstance();
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
        if (navigation != Horse::Qualification::NavigationState::Ready)
            return;
        bool pending = true;
        if (!CallBool(instance, L"HasAnyBattleRequest", pending) || pending)
        {
            return;
        }
        bool ready = false;
        if (!CallBool(instance, L"CanLaunchBattleManually", ready) || !ready)
            return;
        if (!SetReplayPath(GetBattleSetup(instance), request_.replay_path)
            || !CallNoParams(instance, L"ManualLaunchBattle"))
        {
            Fail("manual_launch_failed");
            return;
        }
        state_ = State::Launched;
        importer_.ReleasePlaybackContext();
        WriteResult("launch_requested", "none");
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
        playback_context_staged_ = true;
        Output::send<LogLevel::Default>(STR(
            "[ReplayQualification] native replay playback context staged\n"));
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
