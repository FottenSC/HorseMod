#pragma once

#include "CandidateCheckpoint.hpp"
#include "Interfaces.hpp"
#include "NativeCandidateRegions.hpp"

namespace Horse::Deterministic
{
using CandidateAdvanceFn = Status (*)(
    void* user,
    FrameCoordinate coordinate,
    const InputPair& inputs,
    bool suppress_ephemeral_presentation) noexcept;
using CandidateRebuildFn = Status (*)(void* user) noexcept;
using CandidateReconcileFn = Status (*)(
    void* user, FrameCoordinate coordinate) noexcept;

struct CandidateAdapterBinding
{
    NativeContext context{};
    HgCpuGenerationContext hgcpu_context{};
    HgCpuExecFn hgcpu_writer{};
    HgCpuExecFn hgcpu_reader{};
    void* action_user{};
    CandidateAdvanceFn advance{};
    CandidateRebuildFn rebuild{};
    CandidateReconcileFn reconcile{};
};

class CandidateGameStateAdapter final : public IGameStateAdapter
{
public:
    CandidateGameStateAdapter(
        NativeCandidateRegions& regions, HgCpuStreamShim& hgcpu) noexcept;

    Status Configure(const CandidateAdapterBinding& binding) noexcept;
    void Reset() noexcept;

    Status BindContext(const NativeContext& context) noexcept override;
    Status PreflightCapture(FrameCoordinate coordinate) noexcept override;
    Status Capture(FrameCoordinate coordinate, Snapshot& output) noexcept override;
    Status PreflightRestore(const Snapshot& snapshot) noexcept override;
    Status Restore(const Snapshot& snapshot) noexcept override;
    Status RebuildDerivedState() noexcept override;
    Status VerifyRestoredState(const Snapshot& expected) noexcept override;
    Status AdvanceFrame(
        FrameCoordinate coordinate,
        const InputPair& inputs,
        bool suppress_ephemeral_presentation) noexcept override;
    Status ReconcilePresentation(FrameCoordinate coordinate) noexcept override;

private:
    [[nodiscard]] bool context_matches(const NativeContext& context) const noexcept;
    Status capture_image(CandidateCheckpointImage& output) noexcept;
    Status decode_and_preflight(
        const Snapshot& snapshot, CandidateCheckpointImage& output) noexcept;

    NativeCandidateRegions& regions_;
    HgCpuStreamShim& hgcpu_;
    CandidateAdapterBinding binding_{};
    bool configured_{};
    bool bound_{};
};
}
