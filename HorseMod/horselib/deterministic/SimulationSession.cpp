#include "SimulationSession.hpp"

namespace Horse::Deterministic
{
SimulationSession::SimulationSession(
    IGameStateAdapter& adapter,
    IInputTimeline& inputs,
    ISnapshotStore& snapshots,
    IPresentationJournal& presentation) noexcept
    : adapter_(adapter),
      inputs_(inputs),
      snapshots_(snapshots),
      presentation_(presentation)
{
}

Status SimulationSession::require_owner_thread() const noexcept
{
    return owner_thread_ == std::this_thread::get_id()
        ? Status::success()
        : Status::failure(FailureCode::WrongThread);
}

Status SimulationSession::fail(FailureCode code) noexcept
{
    state_ = SimulationState::Failed;
    failure_ = code;
    return Status::failure(code);
}

Status SimulationSession::BindAndCaptureBaseline(
    const NativeContext& context,
    FrameCoordinate baseline) noexcept
{
    if (state_ != SimulationState::Idle || context.generation != baseline.generation)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    owner_thread_ = std::this_thread::get_id();
    context_ = context;
    state_ = SimulationState::Binding;
    Status status = adapter_.BindContext(context);
    if (!status.ok())
    {
        return fail(status.code);
    }
    state_ = SimulationState::CapturingBaseline;
    status = CaptureCheckpoint(baseline);
    if (!status.ok())
    {
        return fail(status.code);
    }
    state_ = SimulationState::Running;
    return Status::success();
}

Status SimulationSession::CaptureCheckpoint(FrameCoordinate coordinate) noexcept
{
    const Status thread = require_owner_thread();
    if (!thread.ok())
    {
        return thread;
    }
    if (state_ != SimulationState::Running
        && state_ != SimulationState::CapturingBaseline)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    if (coordinate.generation != context_.generation)
    {
        return Status::failure(FailureCode::GenerationMismatch);
    }
    Status status = adapter_.PreflightCapture(coordinate);
    if (!status.ok())
    {
        return Status::failure(FailureCode::CapturePreflightFailed);
    }
    Snapshot snapshot;
    status = adapter_.Capture(coordinate, snapshot);
    if (!status.ok())
    {
        return Status::failure(FailureCode::CaptureFailed);
    }
    if (snapshot.coordinate != coordinate
        || snapshot.context_identity != context_.battle_identity)
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }
    return snapshots_.Save(std::move(snapshot));
}

Status SimulationSession::Advance(
    FrameCoordinate coordinate,
    const InputPair& inputs) noexcept
{
    const Status thread = require_owner_thread();
    if (!thread.ok())
    {
        return thread;
    }
    if (state_ != SimulationState::Running)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    Status status = inputs_.AppendAuthoritative(coordinate, inputs);
    if (!status.ok())
    {
        return status;
    }
    status = adapter_.AdvanceFrame(coordinate, inputs, false);
    return status.ok() ? status : fail(FailureCode::AdvanceFailed);
}

Status SimulationSession::undo_restore(const Snapshot& undo) noexcept
{
    if (!adapter_.PreflightRestore(undo).ok()
        || !adapter_.Restore(undo).ok()
        || !adapter_.RebuildDerivedState().ok()
        || !adapter_.VerifyRestoredState(undo).ok())
    {
        return fail(FailureCode::UndoFailed);
    }
    return Status::success();
}

Status SimulationSession::transactional_restore(const Snapshot& target) noexcept
{
    if (target.coordinate.generation != context_.generation)
    {
        return Status::failure(FailureCode::GenerationMismatch);
    }
    if (!adapter_.PreflightRestore(target).ok())
    {
        return Status::failure(FailureCode::RestorePreflightFailed);
    }

    Snapshot undo;
    if (!adapter_.PreflightCapture(target.coordinate).ok()
        || !adapter_.Capture(target.coordinate, undo).ok())
    {
        return Status::failure(FailureCode::CaptureFailed);
    }
    if (!adapter_.Restore(target).ok())
    {
        return undo_restore(undo).ok()
            ? Status::failure(FailureCode::RestoreWriteFailed)
            : Status::failure(FailureCode::UndoFailed);
    }
    if (!adapter_.RebuildDerivedState().ok())
    {
        return undo_restore(undo).ok()
            ? Status::failure(FailureCode::DerivedStateRepairFailed)
            : Status::failure(FailureCode::UndoFailed);
    }
    if (!adapter_.VerifyRestoredState(target).ok())
    {
        return undo_restore(undo).ok()
            ? Status::failure(FailureCode::RestoreVerificationFailed)
            : Status::failure(FailureCode::UndoFailed);
    }
    return Status::success();
}

Status SimulationSession::RestoreAndResimulate(
    FrameCoordinate target,
    FrameCoordinate resume_at) noexcept
{
    const Status thread = require_owner_thread();
    if (!thread.ok())
    {
        return thread;
    }
    if (state_ != SimulationState::Running || target.generation != resume_at.generation
        || target.frame > resume_at.frame)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    const auto checkpoint = snapshots_.NearestAtOrBefore(target);
    if (!checkpoint.has_value())
    {
        return Status::failure(FailureCode::MissingSnapshot);
    }

    state_ = SimulationState::Restoring;
    Status status = transactional_restore(*checkpoint);
    if (!status.ok())
    {
        return fail(status.code);
    }
    presentation_.DiscardFrom(checkpoint->coordinate);
    state_ = SimulationState::Resimulating;
    for (std::uint64_t frame = checkpoint->coordinate.frame;
         frame < resume_at.frame;
         ++frame)
    {
        const FrameCoordinate coordinate{resume_at.generation, frame};
        const auto input = inputs_.GetExact(coordinate);
        if (!input.has_value())
        {
            return fail(FailureCode::MissingInput);
        }
        status = adapter_.AdvanceFrame(coordinate, *input, true);
        if (!status.ok())
        {
            return fail(FailureCode::AdvanceFailed);
        }
    }
    status = adapter_.ReconcilePresentation(resume_at);
    if (!status.ok())
    {
        return fail(FailureCode::PresentationFailed);
    }
    state_ = SimulationState::Running;
    return Status::success();
}

Status SimulationSession::Quiesce() noexcept
{
    const Status thread = require_owner_thread();
    if (!thread.ok())
    {
        return thread;
    }
    if (state_ != SimulationState::Running && state_ != SimulationState::Failed)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    state_ = SimulationState::Quiescing;
    return Status::success();
}

void SimulationSession::Reset() noexcept
{
    if (owner_thread_ != std::thread::id{}
        && owner_thread_ != std::this_thread::get_id())
    {
        return;
    }
    if (context_.generation != 0)
    {
        inputs_.InvalidateGeneration(context_.generation);
        snapshots_.InvalidateGeneration(context_.generation);
        presentation_.InvalidateGeneration(context_.generation);
    }
    context_ = {};
    owner_thread_ = {};
    state_ = SimulationState::Idle;
    failure_ = FailureCode::None;
}
}
