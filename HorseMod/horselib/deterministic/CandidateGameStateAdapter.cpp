#include "CandidateGameStateAdapter.hpp"

#include "FloatingPointEnvironment.hpp"

#include <chrono>

namespace Horse::Deterministic
{
CandidateGameStateAdapter::CandidateGameStateAdapter(
    NativeCandidateRegions& regions, HgCpuStreamShim& hgcpu) noexcept
    : regions_(regions), hgcpu_(hgcpu)
{
}

bool CandidateGameStateAdapter::context_matches(
    const NativeContext& context) const noexcept
{
    return context.generation == binding_.context.generation
        && context.battle_identity == binding_.context.battle_identity
        && context.fighter_identities[0] == binding_.context.fighter_identities[0]
        && context.fighter_identities[1] == binding_.context.fighter_identities[1]
        && context.stage_identity == binding_.context.stage_identity;
}

Status CandidateGameStateAdapter::Configure(
    const CandidateAdapterBinding& binding) noexcept
{
    Reset();
    if (binding.context.generation == 0 || binding.context.battle_identity == 0
        || binding.context.fighter_identities[0] == 0
        || binding.context.fighter_identities[1] == 0
        || binding.context.stage_identity == 0 || binding.hgcpu_writer == nullptr
        || binding.hgcpu_reader == nullptr || !regions_.IsBound()
        || binding.battle_audio_selector == nullptr
        || binding.motion_banks == nullptr
        || binding.move_dispatch == nullptr
        || binding.secondary_events == nullptr
        || binding.chara_animation == nullptr
        || binding.ucrt_broker == nullptr || binding.simulation_thread_id == 0
        || binding.wind_probe == nullptr || binding.wind_transaction == nullptr
        || binding.wind_addresses.generation != binding.context.generation
        || binding.ucrt_broker->owner_thread_id()
            != binding.simulation_thread_id
        || binding.hgcpu_context.schema_id != Schema::snapshot_schema_version
        || binding.hgcpu_context.session_generation
            != binding.context.battle_identity
        || binding.hgcpu_context.round_generation != binding.context.generation
        || binding.hgcpu_context.fighter_generations[0]
            != binding.context.fighter_identities[0]
        || binding.hgcpu_context.fighter_generations[1]
            != binding.context.fighter_identities[1]
        || binding.hgcpu_context.stage_generation
            != binding.context.stage_identity
        || binding.hgcpu_context.camera_generation == 0
        || binding.hgcpu_context.allocation_generation
            != binding.context.generation)
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    binding_ = binding;
    try
    {
        if (transaction_target_scratch_ == nullptr)
            transaction_target_scratch_ =
                std::make_unique<CandidateCheckpointImage>();
        if (transaction_scratch_ == nullptr)
            transaction_scratch_ =
                std::make_unique<CandidateCheckpointImage>();
        Status prepared = PrepareCandidateCheckpointStorage(
            capture_scratch_, true);
        if (prepared.ok()) prepared = PrepareCandidateCheckpointStorage(
            canonical_capture_scratch_, false);
        if (prepared.ok()) prepared = PrepareCandidateCheckpointStorage(
            *transaction_target_scratch_, true);
        if (prepared.ok()) prepared = PrepareCandidateCheckpointStorage(
            *transaction_scratch_, true);
        if (!prepared.ok())
        {
            binding_ = {};
            return prepared;
        }
    }
    catch (...)
    {
        binding_ = {};
        return Status::failure(FailureCode::CapacityExceeded);
    }
    configured_ = true;
    return Status::success();
}

void CandidateGameStateAdapter::Reset() noexcept
{
    binding_ = {};
    total_capture_timing_ = {};
    typed_capture_timing_ = {};
    local_capture_timing_ = {};
    hgcpu_capture_timing_ = {};
    motion_capture_timing_ = {};
    ucrt_capture_timing_ = {};
    wind_capture_timing_ = {};
    encode_timing_ = {};
    local_restore_timing_ = {};
    typed_restore_timing_ = {};
    wind_restore_timing_ = {};
    ucrt_restore_timing_ = {};
    derived_repair_timing_ = {};
    total_restore_timing_ = {};
    capture_scratch_ = {};
    canonical_capture_scratch_ = {};
    scratch_capacity_baseline_bytes_ = 0;
    scratch_capacity_high_water_bytes_ = 0;
    scratch_capacity_growth_events_ = 0;
    scratch_capacity_baseline_by_owner_ = {};
    scratch_capacity_high_water_by_owner_ = {};
    last_capture_phase_ = CandidateCapturePhase::None;
    last_captured_movevm_short25_ = {};
    last_captured_movevm_state_shorts_ = {};
    last_captured_rng_ = {};
    configured_ = false;
    bound_ = false;
}

Status CandidateGameStateAdapter::BindContext(
    const NativeContext& context) noexcept
{
    if (!configured_ || !regions_.IsBound() || !context_matches(context))
        return Status::failure(FailureCode::IdentityMismatch);
    const Status preflight = regions_.PreflightCapture();
    if (!preflight.ok()) return preflight;
    bound_ = true;
    return Status::success();
}

Status CandidateGameStateAdapter::PreflightCapture(
    FrameCoordinate coordinate) noexcept
{
    if (!bound_ || coordinate.generation != binding_.context.generation)
        return Status::failure(FailureCode::GenerationMismatch);
    return regions_.PreflightCapture();
}

Status CandidateGameStateAdapter::capture_image(
    CandidateCheckpointImage& output, bool include_local) noexcept
{
    ScopedFloatingPointEnvironment fp_scope;
    if (include_local)
    {
        try { output.local_images.resize(2); }
        catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    }
    else
    {
        output.local_images.clear();
    }
    const auto typed_begin = std::chrono::steady_clock::now();
    last_capture_phase_ = CandidateCapturePhase::NativeTyped;
    Status status = regions_.Capture(output.native);
    if (status.ok())
    {
        last_capture_phase_ = CandidateCapturePhase::BattleAudioSelector;
        status = binding_.battle_audio_selector->Capture(
            output.battle_audio_selector);
    }
    if (status.ok())
    {
        last_capture_phase_ = CandidateCapturePhase::MoveDispatch;
        status = binding_.move_dispatch->Capture(output.move_dispatch);
    }
    if (status.ok())
    {
        last_capture_phase_ = CandidateCapturePhase::SecondaryEvents;
        status = binding_.secondary_events->Capture(output.secondary_events);
    }
    if (status.ok())
    {
        last_capture_phase_ = CandidateCapturePhase::CharaAnimation;
        status = binding_.chara_animation->Capture(output.chara_animation);
    }
    const auto typed_end = std::chrono::steady_clock::now();
    typed_capture_timing_.Record(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            typed_end - typed_begin).count()));
    if (status.ok() && include_local)
    {
        const auto local_begin = std::chrono::steady_clock::now();
        const auto hgcpu_begin = local_begin;
        last_capture_phase_ = CandidateCapturePhase::HgCpu;
        status = hgcpu_.Capture(
            binding_.hgcpu_writer, binding_.hgcpu_context,
            output.local_images[0]);
        const auto hgcpu_end = std::chrono::steady_clock::now();
        hgcpu_capture_timing_.Record(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                hgcpu_end - hgcpu_begin).count()));
        if (status.ok())
        {
            const auto motion_begin = std::chrono::steady_clock::now();
            last_capture_phase_ = CandidateCapturePhase::MotionBanks;
            status = binding_.motion_banks->Capture(output.local_images[1]);
            const auto motion_end = std::chrono::steady_clock::now();
            motion_capture_timing_.Record(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    motion_end - motion_begin).count()));
        }
        const auto local_end = std::chrono::steady_clock::now();
        local_capture_timing_.Record(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                local_end - local_begin).count()));
    }
    if (status.ok())
    {
        const auto ucrt_begin = std::chrono::steady_clock::now();
        last_capture_phase_ = CandidateCapturePhase::Ucrt;
        status = binding_.ucrt_broker->Capture(
            binding_.simulation_thread_id, output.ucrt);
        const auto ucrt_end = std::chrono::steady_clock::now();
        ucrt_capture_timing_.Record(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                ucrt_end - ucrt_begin).count()));
    }
    if (status.ok())
    {
        const auto wind_begin = std::chrono::steady_clock::now();
        last_capture_phase_ = CandidateCapturePhase::StageWind;
        status = binding_.wind_probe->Capture(output.wind);
        const auto wind_end = std::chrono::steady_clock::now();
        wind_capture_timing_.Record(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                wind_end - wind_begin).count()));
    }
    const Status fp = fp_scope.Finish();
    if (status.ok() && fp.ok()) last_capture_phase_ = CandidateCapturePhase::None;
    return status.ok() ? fp : status;
}

Status CandidateGameStateAdapter::Capture(
    FrameCoordinate coordinate, Snapshot& output) noexcept
{
    const auto total_begin = std::chrono::steady_clock::now();
    const Status preflight = PreflightCapture(coordinate);
    if (!preflight.ok())
    {
        output = {};
        const auto total_end = std::chrono::steady_clock::now();
        total_capture_timing_.Record(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                total_end - total_begin).count()));
        return preflight;
    }
    last_capture_phase_ = CandidateCapturePhase::None;
    const Status captured = capture_image(capture_scratch_);
    if (!captured.ok()) return captured;
    last_captured_movevm_state_shorts_ =
        capture_scratch_.native.movevm_state_shorts;
    last_captured_rng_ = capture_scratch_.native.rng;
    for (std::size_t fighter = 0;
         fighter < last_captured_movevm_short25_.size(); ++fighter)
    {
        last_captured_movevm_short25_[fighter] =
            capture_scratch_.native.movevm_state_shorts.fighters[fighter][25];
    }
    const auto encode_begin = std::chrono::steady_clock::now();
    last_capture_phase_ = CandidateCapturePhase::Encode;
    const Status encoded = CandidateCheckpointCodec::EncodeCaptured(
        coordinate, binding_.context.battle_identity, capture_scratch_, output);
    const auto encode_end = std::chrono::steady_clock::now();
    encode_timing_.Record(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            encode_end - encode_begin).count()));
    total_capture_timing_.Record(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            encode_end - total_begin).count()));
    observe_scratch_capacity();
    if (encoded.ok()) last_capture_phase_ = CandidateCapturePhase::None;
    return encoded;
}

Status CandidateGameStateAdapter::CaptureCanonical(
    FrameCoordinate coordinate, Snapshot& output) noexcept
{
    const auto total_begin = std::chrono::steady_clock::now();
    const Status preflight = PreflightCapture(coordinate);
    if (!preflight.ok())
    {
        output = {};
        const auto total_end = std::chrono::steady_clock::now();
        total_capture_timing_.Record(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                total_end - total_begin).count()));
        return preflight;
    }
    last_capture_phase_ = CandidateCapturePhase::None;
    const Status captured = capture_image(canonical_capture_scratch_, false);
    if (!captured.ok()) return captured;
    last_captured_movevm_state_shorts_ =
        canonical_capture_scratch_.native.movevm_state_shorts;
    last_captured_rng_ = canonical_capture_scratch_.native.rng;
    for (std::size_t fighter = 0;
         fighter < last_captured_movevm_short25_.size(); ++fighter)
    {
        last_captured_movevm_short25_[fighter] = canonical_capture_scratch_
            .native.movevm_state_shorts.fighters[fighter][25];
    }
    const auto encode_begin = std::chrono::steady_clock::now();
    last_capture_phase_ = CandidateCapturePhase::Encode;
    const Status encoded = CandidateCheckpointCodec::EncodeCanonical(
        coordinate, binding_.context.battle_identity,
        canonical_capture_scratch_, output);
    const auto encode_end = std::chrono::steady_clock::now();
    encode_timing_.Record(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            encode_end - encode_begin).count()));
    total_capture_timing_.Record(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            encode_end - total_begin).count()));
    observe_scratch_capacity();
    if (encoded.ok()) last_capture_phase_ = CandidateCapturePhase::None;
    return encoded;
}

CandidateAdapterPerformanceStatus
CandidateGameStateAdapter::performance_status() const noexcept
{
    return {
        total_capture_timing_.Status(),
        typed_capture_timing_.Status(),
        local_capture_timing_.Status(),
        hgcpu_capture_timing_.Status(),
        motion_capture_timing_.Status(),
        ucrt_capture_timing_.Status(),
        wind_capture_timing_.Status(),
        encode_timing_.Status(),
        local_restore_timing_.Status(),
        typed_restore_timing_.Status(),
        wind_restore_timing_.Status(),
        ucrt_restore_timing_.Status(),
        derived_repair_timing_.Status(),
        total_restore_timing_.Status(),
        scratch_capacity_baseline_bytes_,
        scratch_capacity_high_water_bytes_,
        scratch_capacity_growth_events_,
        scratch_capacity_baseline_by_owner_,
        scratch_capacity_high_water_by_owner_,
    };
}

void CandidateGameStateAdapter::ResetCapturePerformanceWindow() noexcept
{
    total_capture_timing_ = {};
    typed_capture_timing_ = {};
    local_capture_timing_ = {};
    hgcpu_capture_timing_ = {};
    motion_capture_timing_ = {};
    ucrt_capture_timing_ = {};
    wind_capture_timing_ = {};
    encode_timing_ = {};
    scratch_capacity_baseline_by_owner_ = scratch_capacity_by_owner();
    scratch_capacity_baseline_bytes_ = 0;
    for (const auto bytes : scratch_capacity_baseline_by_owner_)
        scratch_capacity_baseline_bytes_ += bytes;
    scratch_capacity_high_water_bytes_ = scratch_capacity_baseline_bytes_;
    scratch_capacity_growth_events_ = 0;
    scratch_capacity_high_water_by_owner_ =
        scratch_capacity_baseline_by_owner_;
}

std::size_t CandidateGameStateAdapter::scratch_capacity_bytes() const noexcept
{
    std::size_t bytes = CandidateCheckpointDynamicCapacity(
            capture_scratch_, false)
        + CandidateCheckpointDynamicCapacity(canonical_capture_scratch_)
        + regions_.ScratchCapacityBytes();
    if (transaction_target_scratch_ != nullptr)
        bytes += CandidateCheckpointDynamicCapacity(
            *transaction_target_scratch_);
    if (transaction_scratch_ != nullptr)
        bytes += CandidateCheckpointDynamicCapacity(*transaction_scratch_);
    if (binding_.motion_banks != nullptr)
        bytes += binding_.motion_banks->ScratchCapacityBytes();
    if (binding_.move_dispatch != nullptr)
        bytes += binding_.move_dispatch->ScratchCapacityBytes();
    return bytes;
}

std::size_t CandidateGameStateAdapter::owned_scratch_bytes() const noexcept
{
    return scratch_capacity_bytes()
        + (transaction_target_scratch_ == nullptr
            ? 0 : sizeof(CandidateCheckpointImage))
        + (transaction_scratch_ == nullptr
            ? 0 : sizeof(CandidateCheckpointImage));
}

std::array<std::size_t,
    CandidateAdapterPerformanceStatus::scratch_owner_count>
CandidateGameStateAdapter::scratch_capacity_by_owner() const noexcept
{
    return {
        // Attached local images are outbound Snapshot payload ownership that
        // EncodeCaptured swaps through this object; they are not adapter
        // scratch and may legitimately arrive from a reused store slot.
        CandidateCheckpointDynamicCapacity(capture_scratch_, false),
        CandidateCheckpointDynamicCapacity(canonical_capture_scratch_),
        transaction_target_scratch_ == nullptr ? 0
            : CandidateCheckpointDynamicCapacity(*transaction_target_scratch_),
        transaction_scratch_ == nullptr ? 0
            : CandidateCheckpointDynamicCapacity(*transaction_scratch_),
        regions_.ScratchCapacityBytes(),
        binding_.motion_banks == nullptr ? 0
            : binding_.motion_banks->ScratchCapacityBytes(),
        binding_.move_dispatch == nullptr ? 0
            : binding_.move_dispatch->ScratchCapacityBytes(),
    };
}

void CandidateGameStateAdapter::observe_scratch_capacity() noexcept
{
    const auto by_owner = scratch_capacity_by_owner();
    std::size_t current{};
    for (const auto bytes : by_owner) current += bytes;
    if (scratch_capacity_baseline_bytes_ == 0)
    {
        scratch_capacity_baseline_bytes_ = current;
        scratch_capacity_baseline_by_owner_ = by_owner;
        scratch_capacity_high_water_by_owner_ = by_owner;
    }
    if (current > scratch_capacity_high_water_bytes_)
    {
        if (scratch_capacity_high_water_bytes_ != 0)
            ++scratch_capacity_growth_events_;
        scratch_capacity_high_water_bytes_ = current;
    }
    for (std::size_t index = 0; index < by_owner.size(); ++index)
        scratch_capacity_high_water_by_owner_[index] = std::max(
            scratch_capacity_high_water_by_owner_[index], by_owner[index]);
}

Status CandidateGameStateAdapter::TraceLocalStreamOffset(
    std::size_t stream_offset, HgCpuWriteSpan& output) noexcept
{
    output = {};
    if (!bound_ || stream_offset >= hgcpu_stream_capacity)
        return Status::failure(FailureCode::InvalidConfiguration);
    std::vector<HgCpuWriteSpan> storage;
    try { storage.resize(4096); }
    catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    HgCpuWriteTrace trace{std::span{storage}};
    HgCpuLocalImage image{};
    const Status captured = hgcpu_.Capture(binding_.hgcpu_writer,
        binding_.hgcpu_context, image, &trace);
    if (!captured.ok()) return captured;
    for (std::size_t index = 0; index < trace.count; ++index)
    {
        const auto& span = trace.storage[index];
        if (stream_offset >= span.stream_offset
            && stream_offset - span.stream_offset < span.size)
        {
            output = span;
            return Status::success();
        }
    }
    return Status::failure(trace.truncated
        ? FailureCode::CapacityExceeded : FailureCode::ContextUnavailable);
}

Status CandidateGameStateAdapter::decode_and_preflight(
    const Snapshot& snapshot, CandidateCheckpointImage& output) noexcept
{
    if (!bound_ || snapshot.coordinate.generation != binding_.context.generation
        || snapshot.context_identity != binding_.context.battle_identity)
    {
        return Status::failure(FailureCode::GenerationMismatch);
    }
    const Status decoded = CandidateCheckpointCodec::Decode(snapshot, output);
    if (!decoded.ok()) return decoded;
    if (output.local_images.size() != 2
        || output.local_images[0].context != binding_.hgcpu_context
        || output.local_images[1].context != binding_.hgcpu_context
        || !HgCpuStreamShim::ValidateLocalImage(output.local_images[0])
        || !MotionBankSnapshot::ValidateLocalImage(output.local_images[1]))
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }
    if (binding_.ucrt_broker->mode() != UcrtRandBrokerMode::Owned
        || binding_.ucrt_broker->owner_thread_id()
            != binding_.simulation_thread_id)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    Status status = regions_.PreflightRestore(output.native);
    // Battle-audio alternation is presentation-handler state whose source
    // boundary is a native outer batch, not a coordinate checkpoint. Its
    // independently observed entry value is restored by DeterministicHookSet
    // immediately before the corresponding owned batch executes.
    if (status.ok())
        status = binding_.move_dispatch->PreflightRestore(output.move_dispatch);
    return status;
}

Status CandidateGameStateAdapter::PreflightRestore(
    const Snapshot& snapshot) noexcept
{
    if (transaction_target_scratch_ == nullptr)
        return Status::failure(FailureCode::AdapterUnqualified);
    return decode_and_preflight(snapshot, *transaction_target_scratch_);
}

Status CandidateGameStateAdapter::Restore(const Snapshot& snapshot) noexcept
{
    const auto total_begin = std::chrono::steady_clock::now();
    last_restore_operation_failure_mask_ = 0;
    if (transaction_target_scratch_ == nullptr
        || transaction_scratch_ == nullptr)
    {
        last_restore_operation_failure_mask_ = 1u << 10;
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    const Status preflight = decode_and_preflight(
        snapshot, *transaction_target_scratch_);
    if (!preflight.ok())
    {
        last_restore_operation_failure_mask_ = 1u << 9;
        return preflight;
    }
    ScopedFloatingPointEnvironment fp_scope;
    Status restored = capture_image(*transaction_scratch_);
    const bool undo_captured = restored.ok();
    if (!undo_captured) last_restore_operation_failure_mask_ = 1u << 8;
    if (undo_captured)
    {
        // Camera component internals are presentation-local and the decoded
        // peer image leaves them untouched.  Exclude them from the enclosing
        // undo transaction as well; restoring bytes that this transaction did
        // not write is both unnecessary and unsafe for live camera objects.
        transaction_scratch_->native.camera_components = {};
    }
    if (restored.ok()) restored = restore_image(*transaction_target_scratch_);
    if (!restored.ok() && undo_captured
        && !undo_image(*transaction_scratch_))
    {
        last_restore_operation_failure_mask_ |= 1u << 11;
        restored = Status::failure(FailureCode::UndoFailed);
    }
    const Status fp = fp_scope.Finish();
    const auto total_end = std::chrono::steady_clock::now();
    total_restore_timing_.Record(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            total_end - total_begin).count()));
    observe_scratch_capacity();
    if (restored.ok() && !fp.ok())
        last_restore_operation_failure_mask_ = 1u << 12;
    return restored.ok() ? fp : restored;
}

Status CandidateGameStateAdapter::restore_image(
    const CandidateCheckpointImage& image) noexcept
{
    const auto local_begin = std::chrono::steady_clock::now();
    Status status = hgcpu_.Restore(
        binding_.hgcpu_reader, binding_.hgcpu_context,
        image.local_images[0]);
    if (!status.ok()) last_restore_operation_failure_mask_ = 1u << 0;
    if (status.ok())
    {
        status = binding_.motion_banks->RestoreTransactional(
            image.local_images[1]);
        if (!status.ok()) last_restore_operation_failure_mask_ = 1u << 1;
    }
    const auto local_end = std::chrono::steady_clock::now();
    local_restore_timing_.Record(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            local_end - local_begin).count()));
    if (status.ok())
    {
        const auto typed_begin = std::chrono::steady_clock::now();
        // Do not write the diagnostic battle-audio capture here. A coordinate
        // may own several native batches, so the per-batch journal is the only
        // qualified restore boundary for that selector.
        status = regions_.RestoreTransactional(image.native);
        if (!status.ok()) last_restore_operation_failure_mask_ = 1u << 2;
        if (status.ok()) status = binding_.move_dispatch->RestoreTransactional(
            image.move_dispatch);
        if (!status.ok() && last_restore_operation_failure_mask_ == 0)
            last_restore_operation_failure_mask_ = 1u << 3;
        if (status.ok()) status = binding_.secondary_events->RestoreTransactional(
            image.secondary_events);
        if (!status.ok() && last_restore_operation_failure_mask_ == 0)
            last_restore_operation_failure_mask_ = 1u << 4;
        if (status.ok()) status = binding_.chara_animation->RestoreTransactional(
            image.chara_animation);
        if (!status.ok() && last_restore_operation_failure_mask_ == 0)
            last_restore_operation_failure_mask_ = 1u << 5;
        const auto typed_end = std::chrono::steady_clock::now();
        typed_restore_timing_.Record(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                typed_end - typed_begin).count()));
    }
    if (status.ok())
    {
        const auto wind_begin = std::chrono::steady_clock::now();
        status = binding_.wind_transaction->Restore(
            binding_.wind_addresses, image.wind);
        if (!status.ok()) last_restore_operation_failure_mask_ = 1u << 6;
        const auto wind_end = std::chrono::steady_clock::now();
        wind_restore_timing_.Record(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                wind_end - wind_begin).count()));
    }
    if (status.ok())
    {
        const auto ucrt_begin = std::chrono::steady_clock::now();
        status = binding_.ucrt_broker->Restore(
            binding_.simulation_thread_id, image.ucrt);
        if (!status.ok()) last_restore_operation_failure_mask_ = 1u << 7;
        const auto ucrt_end = std::chrono::steady_clock::now();
        ucrt_restore_timing_.Record(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                ucrt_end - ucrt_begin).count()));
    }
    return status;
}

bool CandidateGameStateAdapter::undo_image(
    const CandidateCheckpointImage& image) noexcept
{
    // Use the same verified consumer order as an ordinary restore. Continue
    // through every lane so a failure cannot leave later undo lanes skipped.
    const Status hgcpu = hgcpu_.Restore(
        binding_.hgcpu_reader, binding_.hgcpu_context,
        image.local_images[0]);
    const Status motion = binding_.motion_banks->RestoreTransactional(
        image.local_images[1]);
    // The selector journal is replayed by the native-batch owner and therefore
    // is intentionally absent from coordinate-level undo as well.
    const Status native = regions_.RestoreTransactional(image.native);
    const Status move_dispatch =
        binding_.move_dispatch->RestoreTransactional(image.move_dispatch);
    const Status secondary = binding_.secondary_events->RestoreTransactional(
        image.secondary_events);
    const Status chara_animation =
        binding_.chara_animation->RestoreTransactional(image.chara_animation);
    const Status wind = binding_.wind_transaction->Restore(
        binding_.wind_addresses, image.wind);
    const Status ucrt = binding_.ucrt_broker->Restore(
        binding_.simulation_thread_id, image.ucrt);
    return hgcpu.ok() && motion.ok() && native.ok()
        && move_dispatch.ok()
        && secondary.ok()
        && chara_animation.ok()
        && wind.ok() && ucrt.ok();
}

Status CandidateGameStateAdapter::RebuildDerivedState() noexcept
{
    if (!bound_) return Status::failure(FailureCode::AdapterUnqualified);
    ScopedFloatingPointEnvironment fp_scope;
    const auto rebuild_begin = std::chrono::steady_clock::now();
    const Status rebuilt = binding_.rebuild != nullptr
        ? binding_.rebuild(binding_.action_user)
        : Status::success();
    const auto rebuild_end = std::chrono::steady_clock::now();
    derived_repair_timing_.Record(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            rebuild_end - rebuild_begin).count()));
    const Status fp = fp_scope.Finish();
    return rebuilt.ok() ? fp : rebuilt;
}

Status CandidateGameStateAdapter::VerifyRestoredState(
    const Snapshot& expected) noexcept
{
    last_restore_difference_mask_ = 0;
    if (transaction_target_scratch_ == nullptr
        || transaction_scratch_ == nullptr)
        return Status::failure(FailureCode::AdapterUnqualified);
    const Status preflight = decode_and_preflight(
        expected, *transaction_target_scratch_);
    if (!preflight.ok()) return preflight;
    const Status captured = capture_image(*transaction_scratch_);
    if (!captured.ok()) return captured;
    observe_scratch_capacity();
    // Opaque native streams are reconstruction inputs, not canonical truth.
    // A reader may rebuild pointer/derived bytes that serialize differently
    // while producing the same typed gameplay state. Capture above still proves
    // that the current serializer is bounded and valid; verification compares
    // only pointer-free typed state and explicitly admitted value supplements.
    // Battle-audio selector identity is verified against each native-batch
    // envelope during replay, rather than against this coordinate capture.
    for (std::size_t index = 0;
         index < transaction_target_scratch_->native.camera_components.size();
         ++index)
    {
        if (transaction_target_scratch_->native.camera_components[index].present
            == 0)
            transaction_scratch_->native.camera_components[index] =
                transaction_target_scratch_->native.camera_components[index];
    }
    if (transaction_scratch_->native != transaction_target_scratch_->native)
        last_restore_difference_mask_ |= 1;
    if (transaction_scratch_->move_dispatch
        != transaction_target_scratch_->move_dispatch)
        last_restore_difference_mask_ |= 2;
    if (transaction_scratch_->secondary_events
        != transaction_target_scratch_->secondary_events)
        last_restore_difference_mask_ |= 4;
    if (transaction_scratch_->chara_animation
        != transaction_target_scratch_->chara_animation)
        last_restore_difference_mask_ |= 8;
    if (transaction_scratch_->ucrt != transaction_target_scratch_->ucrt)
        last_restore_difference_mask_ |= 16;
    if (transaction_scratch_->wind != transaction_target_scratch_->wind)
        last_restore_difference_mask_ |= 32;
    return last_restore_difference_mask_ == 0
        ? Status::success()
        : Status::failure(FailureCode::RestoreVerificationFailed);
}

Status CandidateGameStateAdapter::AdvanceFrame(FrameCoordinate coordinate,
    const InputPair& inputs, bool suppress_ephemeral_presentation) noexcept
{
    if (!bound_ || coordinate.generation != binding_.context.generation)
        return Status::failure(FailureCode::GenerationMismatch);
    ScopedFloatingPointEnvironment fp_scope;
    const Status advanced = binding_.advance != nullptr
        ? binding_.advance(binding_.action_user, coordinate, inputs,
            suppress_ephemeral_presentation)
        : Status::failure(FailureCode::AdapterUnqualified);
    const Status fp = fp_scope.Finish();
    return advanced.ok() ? fp : advanced;
}

Status CandidateGameStateAdapter::ReconcilePresentation(
    FrameCoordinate coordinate) noexcept
{
    if (!bound_ || coordinate.generation != binding_.context.generation)
        return Status::failure(FailureCode::GenerationMismatch);
    ScopedFloatingPointEnvironment fp_scope;
    const Status reconciled = binding_.reconcile != nullptr
        ? binding_.reconcile(binding_.action_user, coordinate)
        : Status::failure(FailureCode::AdapterUnqualified);
    const Status fp = fp_scope.Finish();
    return reconciled.ok() ? fp : reconciled;
}
}
