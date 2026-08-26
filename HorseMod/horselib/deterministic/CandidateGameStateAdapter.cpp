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
    last_capture_phase_ = CandidateCapturePhase::None;
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
    CandidateCheckpointImage& output) noexcept
{
    ScopedFloatingPointEnvironment fp_scope;
    auto local_images = std::move(output.local_images);
    output = {};
    output.local_images = std::move(local_images);
    try { output.local_images.resize(2); }
    catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
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
    if (status.ok())
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
    output = {};
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
    CandidateCheckpointImage image{};
    return decode_and_preflight(snapshot, image);
}

Status CandidateGameStateAdapter::Restore(const Snapshot& snapshot) noexcept
{
    const auto total_begin = std::chrono::steady_clock::now();
    CandidateCheckpointImage image{};
    const Status preflight = decode_and_preflight(snapshot, image);
    if (!preflight.ok()) return preflight;
    ScopedFloatingPointEnvironment fp_scope;
    CandidateCheckpointImage undo{};
    Status restored = capture_image(undo);
    const bool undo_captured = restored.ok();
    if (undo_captured)
    {
        // Camera component internals are presentation-local and the decoded
        // peer image leaves them untouched.  Exclude them from the enclosing
        // undo transaction as well; restoring bytes that this transaction did
        // not write is both unnecessary and unsafe for live camera objects.
        undo.native.camera_components = {};
    }
    if (restored.ok()) restored = restore_image(image);
    if (!restored.ok() && undo_captured && !undo_image(undo))
        restored = Status::failure(FailureCode::UndoFailed);
    const Status fp = fp_scope.Finish();
    const auto total_end = std::chrono::steady_clock::now();
    total_restore_timing_.Record(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            total_end - total_begin).count()));
    return restored.ok() ? fp : restored;
}

Status CandidateGameStateAdapter::restore_image(
    const CandidateCheckpointImage& image) noexcept
{
    const auto local_begin = std::chrono::steady_clock::now();
    Status status = hgcpu_.Restore(
        binding_.hgcpu_reader, binding_.hgcpu_context,
        image.local_images[0]);
    if (status.ok())
    {
        status = binding_.motion_banks->RestoreTransactional(
            image.local_images[1]);
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
        if (status.ok()) status = binding_.move_dispatch->RestoreTransactional(
            image.move_dispatch);
        if (status.ok()) status = binding_.secondary_events->RestoreTransactional(
            image.secondary_events);
        if (status.ok()) status = binding_.chara_animation->RestoreTransactional(
            image.chara_animation);
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
    CandidateCheckpointImage expected_image{};
    const Status preflight = decode_and_preflight(expected, expected_image);
    if (!preflight.ok()) return preflight;
    CandidateCheckpointImage observed{};
    const Status captured = capture_image(observed);
    if (!captured.ok()) return captured;
    // Opaque native streams are reconstruction inputs, not canonical truth.
    // A reader may rebuild pointer/derived bytes that serialize differently
    // while producing the same typed gameplay state. Capture above still proves
    // that the current serializer is bounded and valid; verification compares
    // only pointer-free typed state and explicitly admitted value supplements.
    // Battle-audio selector identity is verified against each native-batch
    // envelope during replay, rather than against this coordinate capture.
    for (std::size_t index = 0;
         index < expected_image.native.camera_components.size(); ++index)
    {
        if (expected_image.native.camera_components[index].present == 0)
            observed.native.camera_components[index] =
                expected_image.native.camera_components[index];
    }
    return observed.native == expected_image.native
            && observed.move_dispatch == expected_image.move_dispatch
            && observed.secondary_events == expected_image.secondary_events
            && observed.chara_animation == expected_image.chara_animation
            && observed.ucrt == expected_image.ucrt
            && observed.wind == expected_image.wind
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
