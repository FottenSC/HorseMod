#pragma once

#include "Interfaces.hpp"

#include <thread>

namespace Horse::Deterministic
{
struct ReplayNativeRoundView
{
    NativeContext context{};
    std::uint64_t replay_player_identity{};
    std::uint64_t round_image_identity{};
    std::uint32_t round_count{};
    std::uint32_t round_capacity{};
    std::uint8_t manager_status{};
    std::uint8_t move_state{};
    std::uint8_t pending_dispatch{};
    std::uint8_t round_image_applied{};
};

class IReplayNativeBridge
{
public:
    virtual ~IReplayNativeBridge() = default;
    virtual Status InspectRound(
        std::uint32_t native_round_index,
        ReplayNativeRoundView& output) noexcept = 0;
    virtual Status RequestRoundReset(
        std::uint32_t native_round_index,
        std::uint64_t round_image_identity) noexcept = 0;
};

class NativeReplayMaterializer final : public IReplayGenerationMaterializer
{
public:
    explicit NativeReplayMaterializer(IReplayNativeBridge& bridge) noexcept;

    Status Preflight(const ReplayGenerationTarget& target) noexcept override;
    Status Request(const ReplayGenerationTarget& target) noexcept override;
    std::optional<ReplayGenerationMaterialized> Poll() noexcept override;
    FailureCode TerminalFailure() const noexcept override { return failure_; }
    void Cancel() noexcept override;

private:
    enum class State : std::uint8_t { Idle, Preflighted, AwaitingFence, Completed, Failed };

    [[nodiscard]] Status require_owner_thread() const noexcept;
    [[nodiscard]] Status validate_view(
        const ReplayGenerationTarget& target,
        const ReplayNativeRoundView& view,
        bool require_idle_manager) const noexcept;
    Status fail(FailureCode code) noexcept;

    IReplayNativeBridge& bridge_;
    std::thread::id owner_thread_{};
    ReplayGenerationTarget target_{};
    State state_{State::Idle};
    FailureCode failure_{FailureCode::None};
};
}
