#include "ReplayCoordinator.hpp"

namespace Horse::Deterministic
{
ReplayCoordinator::ReplayCoordinator(SimulationSession& simulation) noexcept
    : simulation_(simulation)
{
}

Status ReplayCoordinator::Begin(
    const NativeContext& context,
    FrameCoordinate baseline) noexcept
{
    if (state_ != ReplayState::Idle)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    const Status status = simulation_.BindAndCaptureBaseline(context, baseline);
    if (!status.ok())
    {
        return Fail(status.code);
    }
    playhead_ = baseline;
    captured_end_ = baseline;
    state_ = ReplayState::Capturing;
    return Status::success();
}

Status ReplayCoordinator::RecordAndAdvance(
    FrameCoordinate coordinate,
    const InputPair& inputs) noexcept
{
    if (state_ != ReplayState::Capturing || coordinate.generation != playhead_.generation)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    if (coordinate != playhead_)
    {
        return Fail(FailureCode::MissingInput);
    }
    if (coordinate.frame != 0 && coordinate.frame % checkpoint_interval == 0)
    {
        const Status checkpoint = simulation_.CaptureCheckpoint(coordinate);
        if (!checkpoint.ok())
        {
            if (checkpoint.code == FailureCode::CapacityExceeded)
            {
                partial_timeline_ = true;
                state_ = ReplayState::Ready;
                return Status::success();
            }
            return Fail(checkpoint.code);
        }
    }
    const Status advanced = simulation_.Advance(coordinate, inputs);
    if (!advanced.ok())
    {
        return Fail(advanced.code);
    }
    ++playhead_.frame;
    if (playhead_ > captured_end_)
    {
        captured_end_ = playhead_;
    }
    return Status::success();
}

Status ReplayCoordinator::MarkGenerationBoundary(FrameCoordinate coordinate) noexcept
{
    if (state_ != ReplayState::Capturing || coordinate != playhead_)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    const Status status = simulation_.CaptureCheckpoint(coordinate);
    return status.ok() ? status : Fail(status.code);
}

Status ReplayCoordinator::FinishCapture() noexcept
{
    if (state_ != ReplayState::Capturing)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    state_ = ReplayState::Ready;
    return Status::success();
}

Status ReplayCoordinator::Seek(FrameCoordinate target) noexcept
{
    if ((state_ != ReplayState::Ready && state_ != ReplayState::Capturing)
        || target.generation != captured_end_.generation
        || target.frame > captured_end_.frame)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    state_ = ReplayState::Seeking;
    const Status status = simulation_.RestoreAndResimulate(target, target);
    if (!status.ok())
    {
        return Fail(status.code);
    }
    playhead_ = target;
    state_ = ReplayState::Resuming;
    return Status::success();
}

Status ReplayCoordinator::Resume() noexcept
{
    if (state_ != ReplayState::Resuming)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    state_ = ReplayState::Capturing;
    return Status::success();
}

Status ReplayCoordinator::Fail(FailureCode code) noexcept
{
    failure_ = code;
    state_ = ReplayState::Failed;
    return Status::failure(code);
}

void ReplayCoordinator::Reset() noexcept
{
    simulation_.Reset();
    state_ = ReplayState::Idle;
    failure_ = FailureCode::None;
    playhead_ = {};
    captured_end_ = {};
    partial_timeline_ = false;
}
}
