#pragma once

#include "Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse::Deterministic
{
enum class OnlineLifecyclePhase : std::uint8_t
{
    ClearForStock,
    PreOwnership,
    Owned,
    FailClosedAwaitingSceneExit,
    SceneExitCleanup,
};

constexpr std::string_view online_lifecycle_phase_name(
    OnlineLifecyclePhase phase) noexcept
{
    switch (phase)
    {
    case OnlineLifecyclePhase::ClearForStock: return "clear_for_stock";
    case OnlineLifecyclePhase::PreOwnership: return "preownership";
    case OnlineLifecyclePhase::Owned: return "owned";
    case OnlineLifecyclePhase::FailClosedAwaitingSceneExit:
        return "fail_closed_awaiting_scene_exit";
    case OnlineLifecyclePhase::SceneExitCleanup: return "scene_exit_cleanup";
    }
    return "unknown";
}

enum class OnlineCleanupResource : std::uint8_t
{
    Coordinator,
    Transport,
    Gekko,
    PredictedPlayer,
    Presentation,
    Timeline,
    Allowlist,
    Request,
    InputOwner,
    SessionIdentity,
    Count,
};

struct OnlineStockClearance
{
    std::array<bool, static_cast<std::size_t>(
        OnlineCleanupResource::Count)> resources{};

    [[nodiscard]] bool All() const noexcept
    {
        for (const bool clear : resources)
            if (!clear) return false;
        return true;
    }
};

class OnlineLifecycle final
{
public:
    Status ArmPreOwnership() noexcept
    {
        if (phase_ != OnlineLifecyclePhase::ClearForStock)
            return Status::failure(FailureCode::IllegalTransition);
        phase_ = OnlineLifecyclePhase::PreOwnership;
        lobby_request_issued_ = false;
        return Status::success();
    }

    Status MarkOwned() noexcept
    {
        if (phase_ != OnlineLifecyclePhase::PreOwnership
            && phase_ != OnlineLifecyclePhase::Owned)
            return Status::failure(FailureCode::IllegalTransition);
        phase_ = OnlineLifecyclePhase::Owned;
        return Status::success();
    }

    void BeginFailure(bool post_ownership) noexcept
    {
        phase_ = post_ownership || phase_ == OnlineLifecyclePhase::Owned
            ? OnlineLifecyclePhase::FailClosedAwaitingSceneExit
            : OnlineLifecyclePhase::SceneExitCleanup;
    }

    [[nodiscard]] bool TakeLobbyRequest() noexcept
    {
        if (phase_ != OnlineLifecyclePhase::FailClosedAwaitingSceneExit
            || lobby_request_issued_)
            return false;
        lobby_request_issued_ = true;
        return true;
    }

    Status BeginSceneExitCleanup() noexcept
    {
        if (phase_ == OnlineLifecyclePhase::ClearForStock)
            return Status::success();
        phase_ = OnlineLifecyclePhase::SceneExitCleanup;
        return Status::success();
    }

    Status CompleteSceneExitCleanup(
        const OnlineStockClearance& clearance) noexcept
    {
        if (phase_ != OnlineLifecyclePhase::SceneExitCleanup)
            return Status::failure(FailureCode::IllegalTransition);
        if (!clearance.All())
            return Status::failure(FailureCode::RestoreVerificationFailed);
        phase_ = OnlineLifecyclePhase::ClearForStock;
        lobby_request_issued_ = false;
        return Status::success();
    }

    [[nodiscard]] OnlineLifecyclePhase phase() const noexcept
    {
        return phase_;
    }
    [[nodiscard]] bool IsClearForStock() const noexcept
    {
        return phase_ == OnlineLifecyclePhase::ClearForStock;
    }
    [[nodiscard]] bool RequiresOwnedInput() const noexcept
    {
        return phase_ == OnlineLifecyclePhase::Owned
            || phase_ == OnlineLifecyclePhase::FailClosedAwaitingSceneExit;
    }
    [[nodiscard]] bool CanUnloadModule() const noexcept
    {
        return IsClearForStock();
    }

private:
    OnlineLifecyclePhase phase_{OnlineLifecyclePhase::ClearForStock};
    bool lobby_request_issued_{};
};
}
