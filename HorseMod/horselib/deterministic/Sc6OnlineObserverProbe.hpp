#pragma once

#include "Sc6OnlineContractObserver.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Horse::Deterministic
{
inline constexpr std::uint32_t online_observer_probe_schema_version = 1;
inline constexpr std::uint32_t online_observer_probe_timeout_seconds = 900;

enum class OnlineObserverProbeState : std::uint32_t
{
    Idle,
    Armed,
    Complete,
    Expired,
    Failed,
};

struct OnlineObserverProbeRequest final
{
    std::array<char, 64> run_id{};
    std::uint32_t schema_version{online_observer_probe_schema_version};
    std::uint32_t timeout_seconds{online_observer_probe_timeout_seconds};
};

struct OnlineObserverFrameView final
{
    const void* battle_sync_object{};
    std::span<const std::string_view> loaded_packages{};
};

struct OnlineObserverProbeReport final
{
    std::array<char, 64> run_id{};
    std::uint32_t schema_version{online_observer_probe_schema_version};
    OnlineObserverProbeState state{OnlineObserverProbeState::Idle};
    FailureCode failure{FailureCode::None};
    std::uint64_t elapsed_milliseconds{};
    Sc6OnlineSessionIdentity session{};
    SteamLobbyIdentity lobby{};
    Sc6BattleSyncIdentity battle{};
    std::array<char, 128> stage_package{};
    std::array<char, 96> stage_display_name{};
    CanonicalHash loaded_package_identity{};
};

// This type deliberately owns only the three read-only observers and bounded
// value state. It has no coordinator, transport, allowlist, Gekko, input, or
// presentation dependency and therefore cannot cross the takeover boundary.
class IOnlineObserverReadOnlyAccess
{
public:
    virtual ~IOnlineObserverReadOnlyAccess() = default;
    virtual Status ObserveSession(
        Sc6OnlineSessionIdentity& output) noexcept = 0;
    virtual Status ObserveLobby(std::uint64_t lobby_id,
        SteamLobbyIdentity& output) noexcept = 0;
    virtual Status ObserveBattle(const void* battle_sync_object,
        Sc6BattleSyncIdentity& output) noexcept = 0;
};

class Sc6OnlineObserverReadOnlyAccess final
    : public IOnlineObserverReadOnlyAccess
{
public:
    Status Initialize(std::uintptr_t image_base) noexcept;
    Status ObserveSession(
        Sc6OnlineSessionIdentity& output) noexcept override;
    Status ObserveLobby(std::uint64_t lobby_id,
        SteamLobbyIdentity& output) noexcept override;
    Status ObserveBattle(const void* battle_sync_object,
        Sc6BattleSyncIdentity& output) noexcept override;

private:
    Sc6OnlineSessionObserver session_observer_{};
    SteamLobbyObserver lobby_observer_{};
    Sc6BattleSyncObserver battle_observer_{};
};

class Sc6OnlineObserverProbe final
{
public:
    explicit Sc6OnlineObserverProbe(
        IOnlineObserverReadOnlyAccess& access) noexcept : access_(access) {}
    bool Arm(const OnlineObserverProbeRequest& request,
        std::uint64_t monotonic_milliseconds) noexcept;
    void Disarm() noexcept;
    void Tick(const OnlineObserverFrameView& frame,
        std::uint64_t monotonic_milliseconds) noexcept;

    [[nodiscard]] OnlineObserverProbeState state() const noexcept;
    [[nodiscard]] bool CopyReport(OnlineObserverProbeReport& output) const noexcept;

private:
    Status observe_once(const OnlineObserverFrameView& frame,
        OnlineObserverProbeReport& output) noexcept;
    void publish(OnlineObserverProbeReport report,
        OnlineObserverProbeState state) noexcept;

    IOnlineObserverReadOnlyAccess& access_;
    OnlineObserverProbeRequest request_{};
    OnlineObserverProbeReport report_{};
    std::uint64_t armed_at_milliseconds_{};
    std::atomic<OnlineObserverProbeState> state_{OnlineObserverProbeState::Idle};
};
}
