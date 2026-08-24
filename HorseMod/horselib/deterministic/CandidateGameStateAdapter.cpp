#include "CandidateGameStateAdapter.hpp"

#include "FloatingPointEnvironment.hpp"

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
        || binding.hgcpu_context.camera_generation
            != binding.context.stage_identity)
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
    output = {};
    Status status = regions_.Capture(output.native);
    if (status.ok())
    {
        status = hgcpu_.Capture(
            binding_.hgcpu_writer, binding_.hgcpu_context, output.hgcpu);
    }
    if (status.ok())
    {
        status = binding_.ucrt_broker->Capture(
            binding_.simulation_thread_id, output.ucrt);
    }
    if (status.ok()) status = binding_.wind_probe->Capture(output.wind);
    const Status fp = fp_scope.Finish();
    return status.ok() ? fp : status;
}

Status CandidateGameStateAdapter::Capture(
    FrameCoordinate coordinate, Snapshot& output) noexcept
{
    output = {};
    const Status preflight = PreflightCapture(coordinate);
    if (!preflight.ok()) return preflight;
    CandidateCheckpointImage image{};
    const Status captured = capture_image(image);
    if (!captured.ok()) return captured;
    return CandidateCheckpointCodec::Encode(
        coordinate, binding_.context.battle_identity, image, output);
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
    if (output.hgcpu.context != binding_.hgcpu_context
        || !HgCpuStreamShim::ValidateLocalImage(output.hgcpu))
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }
    if (binding_.ucrt_broker->mode() != UcrtRandBrokerMode::Owned
        || binding_.ucrt_broker->owner_thread_id()
            != binding_.simulation_thread_id)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    return regions_.PreflightRestore(output.native);
}

Status CandidateGameStateAdapter::PreflightRestore(
    const Snapshot& snapshot) noexcept
{
    CandidateCheckpointImage image{};
    return decode_and_preflight(snapshot, image);
}

Status CandidateGameStateAdapter::Restore(const Snapshot& snapshot) noexcept
{
    CandidateCheckpointImage image{};
    const Status preflight = decode_and_preflight(snapshot, image);
    if (!preflight.ok()) return preflight;
    ScopedFloatingPointEnvironment fp_scope;
    CandidateCheckpointImage undo{};
    Status restored = capture_image(undo);
    const bool undo_captured = restored.ok();
    if (restored.ok()) restored = restore_image(image);
    if (!restored.ok() && undo_captured && !undo_image(undo))
        restored = Status::failure(FailureCode::UndoFailed);
    const Status fp = fp_scope.Finish();
    return restored.ok() ? fp : restored;
}

Status CandidateGameStateAdapter::restore_image(
    const CandidateCheckpointImage& image) noexcept
{
    Status status = hgcpu_.Restore(
        binding_.hgcpu_reader, binding_.hgcpu_context, image.hgcpu);
    if (status.ok()) status = regions_.RestoreTransactional(image.native);
    if (status.ok())
        status = binding_.wind_transaction->Restore(
            binding_.wind_addresses, image.wind);
    if (status.ok())
    {
        status = binding_.ucrt_broker->Restore(
            binding_.simulation_thread_id, image.ucrt);
    }
    return status;
}

bool CandidateGameStateAdapter::undo_image(
    const CandidateCheckpointImage& image) noexcept
{
    const Status ucrt = binding_.ucrt_broker->Restore(
        binding_.simulation_thread_id, image.ucrt);
    const Status native = regions_.RestoreTransactional(image.native);
    const Status wind = binding_.wind_transaction->Restore(
        binding_.wind_addresses, image.wind);
    const Status hgcpu = hgcpu_.Restore(
        binding_.hgcpu_reader, binding_.hgcpu_context, image.hgcpu);
    return ucrt.ok() && native.ok() && wind.ok() && hgcpu.ok();
}

Status CandidateGameStateAdapter::RebuildDerivedState() noexcept
{
    if (!bound_) return Status::failure(FailureCode::AdapterUnqualified);
    ScopedFloatingPointEnvironment fp_scope;
    const Status rebuilt = binding_.rebuild != nullptr
        ? binding_.rebuild(binding_.action_user)
        : Status::success();
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
    return observed.native == expected_image.native
            && observed.hgcpu.context == expected_image.hgcpu.context
            && observed.hgcpu.cursor == expected_image.hgcpu.cursor
            && observed.hgcpu.checksum == expected_image.hgcpu.checksum
            && observed.hgcpu.bytes == expected_image.hgcpu.bytes
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
