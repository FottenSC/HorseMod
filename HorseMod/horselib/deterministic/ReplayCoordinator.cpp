#include "ReplayCoordinator.hpp"

namespace Horse::Deterministic
{
ReplayCoordinator::ReplayCoordinator(
    SimulationSession& simulation,
    IReplayGenerationMaterializer* materializer) noexcept
    : simulation_(simulation), materializer_(materializer)
{
}

Status ReplayCoordinator::Begin(
    const NativeContext& context,
    FrameCoordinate baseline,
    std::uint32_t native_round_index,
    std::uint64_t round_image_identity) noexcept
{
    if (state_ != ReplayState::Idle || context.generation == 0
        || round_image_identity == 0)
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
    generations_.emplace(
        context.generation,
        GenerationRecord{
            {context, baseline, native_round_index, round_image_identity}, baseline});
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
    GenerationRecord& generation = generations_.at(playhead_.generation);
    if (playhead_ > generation.captured_end)
    {
        generation.captured_end = playhead_;
    }
    return Status::success();
}

Status ReplayCoordinator::BeginGeneration(
    const NativeContext& context,
    FrameCoordinate baseline,
    std::uint32_t native_round_index,
    std::uint64_t round_image_identity) noexcept
{
    if (state_ != ReplayState::Capturing
        || context.generation == 0
        || context.generation <= playhead_.generation
        || context.generation != baseline.generation
        || round_image_identity == 0
        || generations_.contains(context.generation))
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    Status status = simulation_.Quiesce();
    if (!status.ok())
    {
        return Fail(status.code);
    }
    status = simulation_.ReleaseBindingPreserveHistory();
    if (!status.ok())
    {
        return Fail(status.code);
    }
    status = simulation_.BindAndCaptureBaseline(context, baseline);
    if (!status.ok())
    {
        return Fail(status.code);
    }
    playhead_ = baseline;
    captured_end_ = baseline;
    generations_[context.generation] = GenerationRecord{
        {context, baseline, native_round_index, round_image_identity}, baseline};
    return Status::success();
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
    const auto generation = generations_.find(target.generation);
    if ((state_ != ReplayState::Ready && state_ != ReplayState::Capturing)
        || generation == generations_.end()
        || target.frame < generation->second.target.baseline.frame
        || target.frame > generation->second.captured_end.frame)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    state_ = ReplayState::Seeking;
    if (target.generation == playhead_.generation)
    {
        return finish_seek(target);
    }
    if (materializer_ == nullptr)
    {
        return Fail(FailureCode::NativeGenerationMaterializationFailed);
    }
    Status status = materializer_->Preflight(generation->second.target);
    if (!status.ok())
    {
        return Fail(status.code);
    }
    status = simulation_.Quiesce();
    if (!status.ok())
    {
        return Fail(status.code);
    }
    status = simulation_.ReleaseBindingPreserveHistory();
    if (!status.ok())
    {
        return Fail(status.code);
    }
    status = materializer_->Request(generation->second.target);
    if (!status.ok())
    {
        return Fail(status.code);
    }
    pending_seek_ = target;
    return Status::success();
}

Status ReplayCoordinator::PollSeek() noexcept
{
    if (state_ != ReplayState::Seeking || !pending_seek_.has_value()
        || materializer_ == nullptr)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    const FailureCode terminal = materializer_->TerminalFailure();
    if (terminal != FailureCode::None)
    {
        return Fail(terminal);
    }
    const auto materialized = materializer_->Poll();
    if (!materialized.has_value())
    {
        return Status::success();
    }
    const auto generation = generations_.find(pending_seek_->generation);
    if (generation == generations_.end()
        || materialized->context != generation->second.target.expected_context
        || materialized->baseline != generation->second.target.baseline
        || materialized->native_round_index != generation->second.target.native_round_index
        || materialized->round_image_identity != generation->second.target.round_image_identity)
    {
        return Fail(FailureCode::IdentityMismatch);
    }
    Status status = simulation_.BindMaterializedGeneration(
        materialized->context,
        materialized->baseline);
    if (!status.ok())
    {
        return Fail(status.code);
    }
    const FrameCoordinate target = *pending_seek_;
    pending_seek_.reset();
    return finish_seek(target);
}

Status ReplayCoordinator::finish_seek(FrameCoordinate target) noexcept
{
    const Status status = simulation_.RestoreAndResimulate(target, target);
    if (!status.ok())
    {
        return Fail(status.code);
    }
    playhead_ = target;
    captured_end_ = generations_.at(target.generation).captured_end;
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
    if (materializer_ != nullptr && state_ == ReplayState::Seeking)
    {
        materializer_->Cancel();
    }
    pending_seek_.reset();
    failure_ = code;
    state_ = ReplayState::Failed;
    return Status::failure(code);
}

void ReplayCoordinator::Reset() noexcept
{
    if (materializer_ != nullptr)
    {
        materializer_->Cancel();
    }
    simulation_.Reset();
    state_ = ReplayState::Idle;
    failure_ = FailureCode::None;
    playhead_ = {};
    captured_end_ = {};
    partial_timeline_ = false;
    generations_.clear();
    pending_seek_.reset();
}
}
