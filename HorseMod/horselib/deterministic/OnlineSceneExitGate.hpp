#pragma once

#include "OnlineCoordinator.hpp"
#include "OnlineLifecycle.hpp"

#include <cstdint>
#include <optional>

namespace Horse::Deterministic
{
// ALuxBattleGameMode::TerminateBattle is a two-sided native boundary. The
// pre-hook is the last point at which the active coordinator contract is
// guaranteed readable, while the post-hook is the first point after stock
// BattleManager teardown. Retain only the value identity between them.
class OnlineSceneExitGate final
{
public:
    [[nodiscard]] bool ArmBeforeBattleTermination(
        std::uint64_t session_id,
        bool online_requested,
        OnlineLifecyclePhase phase) noexcept
    {
        if (pending_session_id_ != 0 || session_id == 0 || !online_requested
            || (phase != OnlineLifecyclePhase::Owned
                && phase
                    != OnlineLifecyclePhase::FailClosedAwaitingSceneExit))
            return false;
        pending_session_id_ = session_id;
        return true;
    }

    [[nodiscard]] std::optional<OnlineSceneExitEvidence>
    CompleteAfterBattleTermination() noexcept
    {
        if (pending_session_id_ == 0) return std::nullopt;
        const OnlineSceneExitEvidence evidence{
            pending_session_id_,
            OnlineSceneExitBoundary::BattleTerminationCompleted};
        pending_session_id_ = 0;
        return evidence;
    }

    void Clear() noexcept { pending_session_id_ = 0; }
    [[nodiscard]] bool pending() const noexcept
    {
        return pending_session_id_ != 0;
    }

private:
    std::uint64_t pending_session_id_{};
};
}
