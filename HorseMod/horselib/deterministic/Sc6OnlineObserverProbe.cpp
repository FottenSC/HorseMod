#include "Sc6OnlineObserverProbe.hpp"
#include "Sc6StageCatalog.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Horse::Deterministic
{
namespace
{
template <std::size_t Size>
bool HasTerminatedText(const std::array<char, Size>& value) noexcept
{
    return value[0] != '\0'
        && std::find(value.begin(), value.end(), '\0') != value.end();
}

template <std::size_t Size>
bool CopyText(std::string_view value, std::array<char, Size>& output) noexcept
{
    if (value.empty() || value.size() >= output.size()) return false;
    std::memcpy(output.data(), value.data(), value.size());
    output[value.size()] = '\0';
    return true;
}

Status ResolveLoadedStage(const OnlineContentContract& content,
    std::span<const std::string_view> packages,
    OnlineObserverProbeReport& output) noexcept
{
    constexpr std::size_t maximum_packages = 64;
    if (packages.empty() || packages.size() > maximum_packages)
        return Status::failure(FailureCode::ContextUnavailable);
    std::array<std::string_view, maximum_packages> unique{};
    std::size_t unique_count{};
    std::string_view stage_root{};
    for (const auto package : packages)
    {
        if (package.empty()) continue;
        if (std::find(unique.begin(), unique.begin() + unique_count, package)
            != unique.begin() + unique_count)
            continue;
        unique[unique_count++] = package;
        const auto marker = package.find("/Stage/");
        if (marker == std::string_view::npos) continue;
        const auto component_start = marker + 7;
        const auto component_end = package.find('/', component_start);
        const auto root = package.substr(0, component_end == std::string_view::npos
            ? package.size() : component_end);
        if (stage_root.empty()) stage_root = root;
        else if (stage_root != root)
            return Status::failure(FailureCode::IdentityMismatch);
    }
    if (unique_count == 0 || stage_root.empty()
        || !CopyText(stage_root, output.stage_package))
        return Status::failure(FailureCode::ContextUnavailable);
    const std::string_view stage_code{content.stage_code.data()};
    const auto* catalog = FindQualifiedStage(stage_code);
    if (catalog == nullptr || stage_root != catalog->package_root
        || std::string_view{content.map_name.data()} != catalog->package_root)
        return Status::failure(FailureCode::IdentityMismatch);
    auto status = HashMapPackageIdentity(
        std::span<const std::string_view>{unique.data(), unique_count},
        output.loaded_package_identity);
    if (!status.ok()) return status;
    if (!CopyText(catalog->display_name, output.stage_display_name))
        return Status::failure(FailureCode::ContextUnavailable);
    return Status::success();
}
}

Status Sc6OnlineObserverReadOnlyAccess::Initialize(
    std::uintptr_t image_base) noexcept
{
    return session_observer_.Initialize(image_base);
}

Status Sc6OnlineObserverReadOnlyAccess::ObserveSession(
    Sc6OnlineSessionIdentity& output) noexcept
{
    return session_observer_.Observe(output);
}

Status Sc6OnlineObserverReadOnlyAccess::ObserveLobby(std::uint64_t lobby_id,
    SteamLobbyIdentity& output) noexcept
{
    return lobby_observer_.Observe(lobby_id, output);
}

Status Sc6OnlineObserverReadOnlyAccess::ObserveBattle(
    const void* battle_sync_object, Sc6BattleSyncIdentity& output) noexcept
{
    return battle_observer_.ObserveDetailed(battle_sync_object, output);
}

bool Sc6OnlineObserverProbe::Arm(const OnlineObserverProbeRequest& request,
    std::uint64_t monotonic_milliseconds) noexcept
{
    const auto current = state_.load(std::memory_order_acquire);
    if (current == OnlineObserverProbeState::Armed
        || request.schema_version != online_observer_probe_schema_version
        || request.timeout_seconds == 0
        || request.timeout_seconds > online_observer_probe_timeout_seconds
        || !HasTerminatedText(request.run_id))
        return false;
    request_ = request;
    report_ = {};
    armed_at_milliseconds_ = monotonic_milliseconds;
    state_.store(OnlineObserverProbeState::Armed, std::memory_order_release);
    return true;
}

void Sc6OnlineObserverProbe::Disarm() noexcept
{
    request_ = {};
    armed_at_milliseconds_ = 0;
    state_.store(OnlineObserverProbeState::Idle, std::memory_order_release);
}

void Sc6OnlineObserverProbe::Tick(const OnlineObserverFrameView& frame,
    std::uint64_t monotonic_milliseconds) noexcept
{
    if (state_.load(std::memory_order_acquire)
        != OnlineObserverProbeState::Armed)
        return;
    const auto elapsed = monotonic_milliseconds >= armed_at_milliseconds_
        ? monotonic_milliseconds - armed_at_milliseconds_ : 0;
    const auto timeout = static_cast<std::uint64_t>(request_.timeout_seconds)
        * 1000;
    if (elapsed >= timeout)
    {
        OnlineObserverProbeReport expired{};
        expired.run_id = request_.run_id;
        expired.elapsed_milliseconds = elapsed;
        expired.failure = FailureCode::Timeout;
        publish(expired, OnlineObserverProbeState::Expired);
        return;
    }
    OnlineObserverProbeReport observed{};
    observed.run_id = request_.run_id;
    observed.elapsed_milliseconds = elapsed;
    const auto status = observe_once(frame, observed);
    if (status.ok())
        publish(observed, OnlineObserverProbeState::Complete);
    else if (status.code != FailureCode::ContextUnavailable
        && status.code != FailureCode::IdentityMismatch)
    {
        observed.failure = status.code;
        publish(observed, OnlineObserverProbeState::Failed);
    }
}

OnlineObserverProbeState Sc6OnlineObserverProbe::state() const noexcept
{
    return state_.load(std::memory_order_acquire);
}

bool Sc6OnlineObserverProbe::CopyReport(
    OnlineObserverProbeReport& output) const noexcept
{
    const auto current = state_.load(std::memory_order_acquire);
    if (current != OnlineObserverProbeState::Complete
        && current != OnlineObserverProbeState::Expired
        && current != OnlineObserverProbeState::Failed)
        return false;
    output = report_;
    return true;
}

Status Sc6OnlineObserverProbe::observe_once(
    const OnlineObserverFrameView& frame,
    OnlineObserverProbeReport& output) noexcept
{
    auto status = access_.ObserveSession(output.session);
    if (!status.ok()) return status;
    status = access_.ObserveLobby(output.session.lobby_id, output.lobby);
    if (!status.ok()) return status;
    status = access_.ObserveBattle(
        frame.battle_sync_object, output.battle);
    if (!status.ok()) return status;
    if (output.session.role < 0 || output.session.role > 1
        || output.session.virtual_session_state != 4
        || output.session.local_player_slot
            != static_cast<std::uint8_t>(output.session.role)
        || output.lobby.member_count != 2
        || !output.lobby.casual_player_match
        || !output.battle.characters_received || !output.battle.stage_received)
        return Status::failure(FailureCode::IdentityMismatch);
    return ResolveLoadedStage(output.battle.content,
        frame.loaded_packages, output);
}

void Sc6OnlineObserverProbe::publish(OnlineObserverProbeReport report,
    OnlineObserverProbeState state) noexcept
{
    report.schema_version = online_observer_probe_schema_version;
    report.state = state;
    report_ = report;
    request_ = {};
    armed_at_milliseconds_ = 0;
    state_.store(state, std::memory_order_release);
}
}
