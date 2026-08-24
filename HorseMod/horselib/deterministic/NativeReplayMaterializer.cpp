#include "NativeReplayMaterializer.hpp"

namespace Horse::Deterministic
{
NativeReplayMaterializer::NativeReplayMaterializer(IReplayNativeBridge& bridge) noexcept
    : bridge_(bridge)
{
}

Status NativeReplayMaterializer::require_owner_thread() const noexcept
{
    return owner_thread_ == std::this_thread::get_id()
        ? Status::success()
        : Status::failure(FailureCode::WrongThread);
}

Status NativeReplayMaterializer::validate_view(
    const ReplayGenerationTarget& target,
    const ReplayNativeRoundView& view,
    bool require_idle_manager) const noexcept
{
    const NativeContext& expected = target.expected_context;
    if (!view.replay_enabled || view.replay_player_identity == 0
        || view.context.battle_identity == 0
        || view.context.fighter_identities[0] == 0
        || view.context.fighter_identities[1] == 0
        || view.context.stage_identity == 0
        || view.round_image_identity != target.round_image_identity
        || target.native_round_index >= view.round_count
        || view.round_count > view.round_capacity || view.manager_status != 2)
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }
    if (view.context.battle_identity != expected.battle_identity
        || view.context.fighter_identities[0] != expected.fighter_identities[0]
        || view.context.fighter_identities[1] != expected.fighter_identities[1]
        || view.context.stage_identity != expected.stage_identity)
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }
    if (require_idle_manager && view.move_state != 0)
    {
        return Status::failure(FailureCode::NativeGenerationMaterializationFailed);
    }
    return Status::success();
}

Status NativeReplayMaterializer::Preflight(
    const ReplayGenerationTarget& target) noexcept
{
    if (state_ != State::Idle || target.expected_context.generation == 0
        || target.baseline.generation != target.expected_context.generation
        || target.round_image_identity == 0)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    owner_thread_ = std::this_thread::get_id();
    ReplayNativeRoundView view;
    Status status = bridge_.InspectRound(target.native_round_index, view);
    if (!status.ok())
    {
        return fail(status.code);
    }
    status = validate_view(target, view, true);
    if (!status.ok())
    {
        return fail(status.code);
    }
    target_ = target;
    state_ = State::Preflighted;
    return Status::success();
}

Status NativeReplayMaterializer::Request(
    const ReplayGenerationTarget& target) noexcept
{
    const Status thread = require_owner_thread();
    if (!thread.ok())
    {
        return thread;
    }
    if (state_ != State::Preflighted
        || target.expected_context != target_.expected_context
        || target.baseline != target_.baseline
        || target.native_round_index != target_.native_round_index
        || target.round_image_identity != target_.round_image_identity)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    const Status requested = bridge_.RequestRoundReset(
        target.native_round_index,
        target.round_image_identity);
    if (!requested.ok())
    {
        return fail(requested.code);
    }
    ReplayNativeRoundView view;
    Status status = bridge_.InspectRound(target.native_round_index, view);
    if (!status.ok())
    {
        return fail(status.code);
    }
    status = validate_view(target, view, false);
    if (!status.ok() || view.move_state != 4)
    {
        return fail(FailureCode::NativeGenerationMaterializationFailed);
    }
    state_ = State::AwaitingFence;
    return Status::success();
}

std::optional<ReplayGenerationMaterialized> NativeReplayMaterializer::Poll() noexcept
{
    const Status thread = require_owner_thread();
    if (!thread.ok())
    {
        fail(thread.code);
        return std::nullopt;
    }
    if (state_ != State::AwaitingFence)
    {
        fail(FailureCode::IllegalTransition);
        return std::nullopt;
    }
    ReplayNativeRoundView view;
    Status status = bridge_.InspectRound(target_.native_round_index, view);
    if (!status.ok())
    {
        fail(status.code);
        return std::nullopt;
    }
    status = validate_view(target_, view, false);
    if (!status.ok())
    {
        fail(status.code);
        return std::nullopt;
    }
    if (view.move_state == 4)
    {
        return std::nullopt;
    }
    if (view.move_state != 0 || view.pending_dispatch != 1
        || view.round_image_applied != 1)
    {
        fail(FailureCode::NativeGenerationMaterializationFailed);
        return std::nullopt;
    }
    view.context.generation = target_.expected_context.generation;
    state_ = State::Completed;
    return ReplayGenerationMaterialized{
        view.context,
        target_.baseline,
        target_.native_round_index,
        target_.round_image_identity};
}

Status NativeReplayMaterializer::fail(FailureCode code) noexcept
{
    failure_ = code;
    state_ = State::Failed;
    return Status::failure(code);
}

void NativeReplayMaterializer::Cancel() noexcept
{
    target_ = {};
    owner_thread_ = {};
    state_ = State::Idle;
    failure_ = FailureCode::None;
}
}
