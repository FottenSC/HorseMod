#pragma once

#include "ReplayGpuCompletion.hpp"
#include "ReplayGpuImageEquality.hpp"
#include "ReplayGpuPackedImage.hpp"
#include "ReplayStaticVectorField.hpp"
#include "Sc6ReplayVfxState.hpp"
#include "ReplaySourceState.hpp"
#include "ReplaySparseRegistry.hpp"
#include "ReplayLightingBinding.hpp"
#include "ReplayCreationRenderOwner.hpp"
#include "ReplayStageVisibility.hpp"
#include "ReplayOcclusionHistory.hpp"
#include <span>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <atomic>
#include <algorithm>

namespace Horse::Deterministic
{
class Sc6ReplayTraceState;
class Sc6ReplayGroundDebrisState;
// GPU images and native render histories for the bounded A/B transaction.
// The enclosing CPU/lifecycle owner admits A publication and commit while
// retaining complete B undo. Image verification alone does not establish
// correct target presentation or the subsequent native-rendered continuation.
// The host invokes operations only in its admitted RHI command callback and
// retains this object through Finish(), including cancellation/timeout.
class Sc6ReplayParticleCopy final
{
public:
    static constexpr std::size_t minimum_private_capture_bytes=56ull*1024*1024;
    bool PrepareTraceRenderOwner(const Sc6ReplayTraceState& original) noexcept;
    struct TraceRenderOwnerProof {
        const void* context{};std::size_t count{};
        Status(*binding)(const void*,std::size_t,Sc6ReplayVfxState::ReconstructionBinding&,bool preparation){};
        bool(*dormant)(const void*,std::size_t,unsigned& source_id,std::uintptr_t& mesh_asset){};
    };
    bool PrepareFreshTraceRenderOwners(const TraceRenderOwnerProof& owners,std::size_t budget) noexcept;
    bool PrepareGroundRenderOwner(const Sc6ReplayGroundDebrisState& original) noexcept;
    bool AppendQuarantinedGroundPrimitives(std::span<unsigned> output,std::size_t& count) const noexcept;
    bool PrepareCreationRenderOwners(std::span<const ReplayCreationRenderOwner> target,
        std::span<const ReplayCreationRenderOwner> original, std::size_t budget) noexcept;
    bool PrepareStageRenderOwners(std::span<const ReplayStageVisibility> target,
        std::span<const ReplayStageVisibility> original,std::size_t budget) noexcept;
    bool PrepareParticleRenderOwners(const Sc6ReplayVfxState& target,
        std::span<const Sc6ReplayVfxState::ReconstructionBinding> bindings,std::size_t budget) noexcept;
    // Ordered render boundary after native proxy creation and A publication.
    // Fresh destinations are outside B; partial failure retains the transaction.
    bool BindParticleRenderOwners() noexcept;
    // After render ownership handoff and native component/GPU registration.
    bool CompleteParticleRenderOwners() noexcept;
    bool trace_render_pending() const noexcept;
    bool particle_render_ready() const noexcept { return (!particle_render_owner_count_ && !trace_render_pending()) || particle_render_bound_; }
    enum class NativeRecovery : std::uint8_t { None, CpuPending, CpuReady, PublicationPending, Complete };
    bool needs_cpu_reconstruction() const noexcept { return native_recovery_==NativeRecovery::CpuReady; }
    bool CompleteNativeReconstruction(std::size_t budget) noexcept;
    struct CaptureIdentity {
        std::uint64_t session{}, tick{}, epoch{}, source_revision{}, interval{};
        std::uint8_t execution_phase{};
        ReplaySourceState source;
        friend bool operator==(const CaptureIdentity&, const CaptureIdentity&) = default;
    };
    struct CapturedImage;
    using CaptureHandle = std::shared_ptr<const CapturedImage>;
    bool SealCapture(const CaptureIdentity& identity, std::size_t remaining_budget) noexcept;
    bool BeginFromCapture(std::uintptr_t base, void* world, const CaptureHandle& image,
        std::size_t budget, void* view_state) noexcept;
    CaptureHandle captured_image() const noexcept { return captured_; }
    static const CaptureIdentity* identity(const CaptureHandle& image) noexcept;
    static std::size_t captured_bytes(const CaptureHandle& image) noexcept;
    [[nodiscard]] std::size_t shared_capture_bytes() const noexcept;
    [[nodiscard]] static std::size_t reopen_shared_bytes(const CaptureHandle& image) noexcept;
    static std::size_t retained_capture_bytes() noexcept;
    static bool capture_retirement_pending() noexcept;
    static bool RetireCapturedImages() noexcept;
    enum class Phase : std::uint8_t { Empty, ReadA, ReadyA, ReadRetainedA, ReadB, ReadyB, RetiringFailure, Failed, Released, CancelRetirement, TimeoutRetirement,
        ReadUndo, UndoReady, ReadInstalled, Installed, ReadRecovered, Recovered, Committed, RetiringCommit, RetiredCommit, ReadCoordinates,
        ReadExecutionCoordinates, ReadUndoCoordinates, ReadExecutionDrain, ReadPrivateOwnerRetirement, ReadPackedA };
    struct Witness
    {
        Phase phase{};
        HRESULT error{};
        std::uint64_t bytes{}, source_changes{};
        std::uintptr_t system{}, pool{};
        std::int32_t failed_resource{-1};
        // Native +170 selects the position/velocity ping-pong pair. Texture
        // equality alone cannot admit a coherent CPU/render transaction.
        std::int32_t captured_selection{-1}, subsequent_selection{-1};
        std::array<std::uint32_t, 6> native_formats{};
        bool pending{}, retained_matches{}, deadline_expired{};
        // Completion/binding witnesses are independent from optional CPU image hashes.
        bool diagnostic_readbacks{true}, retained_copy_complete{}, installed_copy_complete{}, recovered_copy_complete{};
        bool cancel_release_blocked{}, cancel_retired{}, timeout_release_blocked{}, timeout_retired{};
        bool undo_ready{}, native_write_uncommitted{}, installed_matches{}, recovered_matches{};
        bool target_prepared{};
        bool execution_work_complete{};
        std::uint64_t private_owner_retirements{};
        bool birth_prepared{}, birth_excluded{}, birth_recovered{};
        bool reconstruction_a{}, reconstruction_b{};
        bool scene_fields_empty{};
        std::uint32_t scene_field_slots{}, scene_field_checks{};
        bool reflection_history{}, reflection_retained{}, reflection_installed{}, reflection_recovered{};
        bool capture_sealed{}, capture_reopened{};
        std::uint64_t reflection_a{}, reflection_b{}, readback_maps{};
        std::uint64_t immutable_image_shared_bytes{};
        unsigned immutable_image_shared_mask{};
        std::array<unsigned,4> immutable_image_nonuniform_tiles{};
        bool immutable_image_compared{};
        std::uintptr_t reconstruction_owner{};
        std::uint32_t reconstruction_issue{};
        std::array<std::uint64_t, 6> original{}, retained{}, subsequent{};
    };
    bool Begin(std::uintptr_t base, void* world, std::size_t budget, void* view_state = nullptr, bool diagnostic_readbacks = true) noexcept;
    // Corrected capture only: the existing transaction owns these hidden B
    // primitives through GPU completion. Copy the already validated render mask
    // before queuing work; never retain a borrowed game-thread span on the RHI.
    bool SetCaptureQuarantine(std::span<const unsigned> ids) noexcept {
        if(witness_.phase!=Phase::Empty || capture_quarantined_count_ || ids.size()>capture_quarantined_.size())return false;
        for(std::size_t i=0;i<ids.size();++i) {
            if(!ids[i])return false;
            for(std::size_t j=0;j<i;++j)if(ids[i]==ids[j])return false;
        }
        std::copy(ids.begin(),ids.end(),capture_quarantined_.begin());capture_quarantined_count_=ids.size();return true;
    }
    bool VerifyAtB(void* world, bool retirement_probe = true) noexcept;
    // Caller must maintain the completed-application producer exclusion and
    // include all component/render owners sharing the pool in its transaction.
    // Deadline is the enclosing request's absolute deadline, including work
    // before this call. Only recovery receives a separate safety deadline.
    bool PrepareUndoAtB(void* world, std::size_t budget, ReplayGpuCompletion::Clock::time_point deadline, bool defer_target = false) noexcept;
    bool PrepareTargetAtB(void* world, std::size_t budget) noexcept;
    bool InstallCaptured(void* world, bool restore_selection = false) noexcept;
    // Called in the ordered RHI callback after game-thread birth admission.
    // The host must retain the corresponding VFX images/leases through Finish.
    bool PrepareBirthRegistration(std::span<const Sc6ReplayVfxState::ParticleBirth::RenderBinding> bindings,
        std::size_t budget) noexcept;
    bool RestoreUndo(void* world) noexcept;
    bool CommitInstalled(bool transfer_birth = false) noexcept;
    bool PrepareCoordinates(std::vector<Sc6ReplayVfxState::CoordinateRebuild>&& rows, std::size_t budget) noexcept;
    // Ordered render-thread preparation only; this does not admit gameplay.
    // The complete B transaction remains pending through both GPU events.
    bool PrepareExecutionCoordinates() noexcept;
    // Game-thread seal before CPU undo; native owners may be unreadable afterwards.
    bool SealFreshCoordinateRetirement(const Sc6ReplayVfxState::PreparedEmitterSet& gpu) noexcept;
    // Completed render-boundary handoff. The host must independently suspend
    // B producers, retain all CPU owners, and reserve the supplied C storage
    // ceiling before admitting any native application work.
    bool BeginExecution(std::size_t budget, std::size_t registry_ceiling) noexcept;
    // Recovery settlement follows C-only teardown and its render completion.
    // It adopts actual C allocations; it does not publish B or authorize play.
    bool SettleExecution(std::size_t budget, RestoreSettlement purpose) noexcept;
    bool ContinueExecutionForUndo() noexcept;
    bool CanRetireAfterExecutionDrain() const noexcept;
    bool DrainExecutionWork() noexcept;
    const ReplayGpuCompletion& retirement_completion() const noexcept { return completion_; }
    // Ordered render/RHI callback after unpublished native owner teardown.
    // Fresh completion serial advances only after this request's GPU event.
    bool DrainPrivateOwnerRetirement() noexcept;
    bool execution_started() const noexcept { return execution_started_; }
    bool settled_for_commit() const noexcept {
        return execution_settled_ && execution_settlement_==RestoreSettlement::CommitCurrent;
    }
    bool ReadQuarantinedPrimitiveIds(const Sc6ReplayVfxState::ParticleBirthSet& births,
        std::span<unsigned> output, std::size_t& count) const noexcept;
    bool AppendQuarantinedTracePrimitive(std::uintptr_t component,
        std::span<unsigned> output, std::size_t& count) const noexcept;
    bool CompleteNativeRetirement() noexcept;
    void Poll() noexcept;
    // Failure retirement only: observes the retained device/context event,
    // never reads world bindings or advances publication into native owners.
    void RetireInFlight() noexcept;
    void Cancel() noexcept;
    bool Finish() noexcept;
    Witness witness() const noexcept;
    bool blocks_resume() const noexcept
    {
        // A prepared B backup must not silently become stale through playback.
        // New publication phases require explicit Finish after commit/recovery.
        return capture_owner_fault_ || !completion_.retired() || witness_.native_write_uncommitted || birth_dirty_
            || HistoriesDirty()
            || visibility_dirty_ || visibility_native_a_refs_
            || (witness_.undo_ready && witness_.phase != Phase::Released);
    }
private:
    Phase private_retirement_return_{Phase::Empty};
    using LocalFields = ReplayStaticVectorFieldSet;
    LocalFields local_fields_a_,local_fields_b_;
    bool CaptureLocalFields(LocalFields& image) noexcept;
    bool PublishLocalFields(const LocalFields& image) noexcept;
    bool LocalFieldOwnersBound() const noexcept;
    using Texture = Microsoft::WRL::ComPtr<ID3D11Texture2D>;
    static constexpr std::array<std::size_t, 6> offsets_{0x30, 0x40, 0x88, 0x98, 0xe0, 0x120};
    static constexpr std::array<DXGI_FORMAT, 6> formats_{
        DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM};
    std::array<void*, 6> wrappers_{};
    std::array<Texture, 6> sources_, images_, staging_, undo_;
    CaptureHandle image_equality_basis_;
    std::unique_ptr<ReplayGpuImageEquality> image_equality_;
    std::array<ReplayGpuPackedImage::Handle,4> packed_images_;
    std::unique_ptr<ReplayGpuImageMaterializer> image_materializer_;
    std::size_t image_capture_budget_{};
    bool PrepareImageEquality(std::size_t budget);
    bool FinishImageEquality() noexcept;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    ReplayGpuCompletion completion_;
    ReplayGpuCompletion::Clock::time_point request_deadline_{};
    std::uintptr_t base_{}, world_{}, scene_{}, system_{}, pool_{};
    Witness witness_{};
    std::array<Sc6ReplayVfxState::ParticleBirth::RenderBinding, Sc6ReplayVfxState::ParticleBirthSet::capacity> births_{};
    std::array<std::int32_t, Sc6ReplayVfxState::ParticleBirthSet::capacity> birth_indices_{};
    std::size_t birth_count_{};
    // Displaced B-only vector fields remain owned by their live render storage.
    // No GPU simulation is admitted between publication and commit/undo.
    // These bytes are an unchanged-ownership witness, never installed into A.
    std::array<std::array<std::byte, 0x110>, Sc6ReplayVfxState::ParticleBirthSet::capacity> birth_fields_{};
    std::array<std::uintptr_t, Sc6ReplayVfxState::ParticleBirthSet::capacity> birth_field_types_{};
    bool birth_fields_retained_{};
    std::array<std::byte, 0x38> birth_registry_{}, birth_excluded_registry_{};
    std::vector<std::uint64_t> birth_slots_, birth_excluded_slots_;
    std::vector<std::uint32_t> birth_flags_, birth_excluded_flags_;
    ReplaySparseRegistryStorage birth_registry_storage_;
    bool birth_registry_published_{};
    bool registry_prepared_{}, registry_executing_{};
    bool execution_started_{}, execution_settled_{};
    RestoreSettlement execution_settlement_{RestoreSettlement::RecoverOriginal};
    std::int32_t execution_selection_{-1};
    std::uint64_t execution_boundary_epoch_{};
    bool PrepareRegistryStorage(std::size_t budget) noexcept;
    bool CaptureExecutionRegistry() noexcept;
    bool birth_dirty_{}, birth_write_complete_{}, birth_undo_started_{};
    bool selection_dirty_{};
    bool retirement_probe_{true};
    std::vector<Sc6ReplayVfxState::CoordinateRebuild> coordinates_;
    std::size_t coordinate_refs_{};
    bool coordinate_commit_started_{};
    bool coordinate_execution_started_{}, coordinate_undo_complete_{};
    bool CoordinatesBound(const Sc6ReplayVfxState::CoordinateRebuild& row, bool installed) noexcept;
    bool RebuildCoordinates(bool target = true, bool reversible = false) noexcept;
    bool BirthBinding() const noexcept;
    bool BirthOwnersBinding() const noexcept;
    bool BirthRegistryMatchesB() const noexcept;
    bool BirthRegistryMatchesExcluded() const noexcept;
    bool ExcludeBirthRegistration() noexcept;
    bool UndoBirthRegistration() noexcept;
    bool Bindings(void* world, bool retain) noexcept;
    bool ReadSelection(std::int32_t& selection) const noexcept;
    bool AdmitRenderReconstruction(bool allow_displaced_fields = false) noexcept;
    bool SelectionMatches(std::int32_t expected) const noexcept;
    bool BeginUnchecked(std::size_t budget);
    bool BindTexturesUnchecked();
    bool LoadCaptureUnchecked(const CaptureHandle& image);
    bool RetainLoadedCapture(const CapturedImage& image) noexcept;
    bool capture_owner_fault_{};
    std::array<unsigned,64> capture_quarantined_{};
    std::size_t capture_quarantined_count_{};
    // Native point-lighting cache transaction. Render bindings are local;
    // allocation contents are retained inputs, never expected GPU observations.
    struct LightingMap {
        std::array<std::byte,0x50> header{};
        std::vector<std::byte> entries,flags,hashes;
        std::array<void*,3> installed{};
    };
    struct LightingAllocation {
        std::uintptr_t address{};
        std::array<std::byte,0x184> values{};
        void* installed{};
    };
    struct LightingPrimitive {
        std::uintptr_t address{},proxy{},component{},allocation{},proxy_type{};
        std::array<std::int32_t,2> weak{};
        std::array<std::uintptr_t,17> slots{};
        ReplayLightingBinding lci_binding{};
        std::array<std::uintptr_t,7> proxy_inputs{}; // read-only skeletal creation/visibility witness
        unsigned id{},slot_count{};
        std::uint8_t dirty{};
        enum class Binding : std::uint8_t { ScenePrimitive, DormantCreation, DormantStage, PendingStageTarget, DormantTrace, PendingTraceTarget, PendingParticleTarget } binding{};
    };
    struct LightingUniform { std::uintptr_t slot{}; void* value{}; std::uintptr_t component{}; unsigned ordinal{}; };
    struct LightingImage {
        std::array<LightingMap,2> maps;
        std::vector<LightingAllocation> allocations;
        std::vector<LightingPrimitive> primitives;
        std::vector<LightingUniform> uniforms;
        std::vector<void*> owners;
        std::uint32_t next_point{};
        std::uint8_t update_all{};
        bool captured{};
    } lighting_a_,lighting_b_,lighting_c_;
    bool lighting_dirty_{},lighting_transferred_{};
    bool lighting_executing_{},lighting_execution_settled_{};
    std::array<ReplayCreationRenderOwner,128> creation_render_owners_{};
    std::vector<ReplayStageRenderOwner> stage_render_owners_;
    struct ParticleRenderOwner {
        Sc6ReplayVfxState::ReconstructionBinding binding;
        std::uintptr_t particle_template{},proxy_type{};
        unsigned source_id{},target_id{};
        std::array<std::size_t,2> emitter_counts{};
    };
    std::array<ParticleRenderOwner,32> particle_render_owners_{};
    std::size_t particle_render_owner_count_{};
    bool particle_render_bound_{};
    bool ReadParticleRenderOwner(ParticleRenderOwner& owner) const noexcept;
    bool PendingParticleTargetBinding(const LightingPrimitive& row) const noexcept;
    NativeRecovery native_recovery_{};
    const Sc6ReplayTraceState* trace_render_owner_{}; // Borrowed from complete B until Finish.
    struct FreshTraceRenderOwner {
        Sc6ReplayVfxState::ReconstructionBinding binding;
        unsigned source_id{},target_id{};
        std::uintptr_t mesh{};
    };
    TraceRenderOwnerProof fresh_trace_render_proof_{};
    std::array<FreshTraceRenderOwner,16> fresh_trace_render_owners_{};
    std::size_t fresh_trace_render_count_{};
    bool FreshTraceRenderBinding(const LightingPrimitive& row) const noexcept;
    const Sc6ReplayGroundDebrisState* ground_render_owner_{}; // Same enclosing B lifetime; no historical proxy reconstruction.
    bool PendingTraceTargetBinding(const LightingPrimitive& row,bool dormant=true) const noexcept;
    bool TraceRenderBinding(const LightingPrimitive& row,bool dormant=true) const noexcept;
    bool StageRenderBinding(const LightingPrimitive& primitive,bool dormant=true) const noexcept;
    bool PendingStageTargetBinding(const LightingPrimitive& primitive) const noexcept;
    bool DormantRenderBinding(const LightingPrimitive& primitive) const noexcept;
    std::size_t creation_render_owner_count_{};
    bool DormantCreationBinding(const LightingPrimitive& primitive, bool dormant=true) const noexcept;
    bool BeginLightingExecution() noexcept;
    bool CanBeginLightingExecution() const noexcept;
    void TransferLightingExecution() noexcept;
    bool SettleLightingExecution(std::size_t budget, RestoreSettlement purpose) noexcept;
    bool LightingPrivateUndoMatches(bool require_bindings=true) const noexcept;
    bool LightingExecutionDisjoint() const noexcept;
    bool CaptureLighting(LightingImage& image,std::size_t budget) noexcept;
    bool CaptureLightingProtected(LightingImage& image,std::size_t budget);
    bool CaptureLightingUnchecked(LightingImage& image,std::size_t budget);
    bool PrepareLighting(std::size_t budget) noexcept;
    bool RebindLighting() noexcept;
    bool RebindLightingImage(LightingImage& image,const LightingImage& live_image) noexcept;
    void DescribeLightingDomainDelta(const LightingImage& image,const LightingImage& live_image) const noexcept;
    bool PublishLighting(bool original,bool preflight_only=false) noexcept;
    bool FinishLighting(bool commit) noexcept;
    bool LightingBindings(const LightingImage& image) const noexcept;
    bool LightingImageMatches(bool original) const noexcept;
    bool LightingMapMatches(const LightingMap& image,std::uintptr_t address,bool installed) const noexcept;
    bool LightingPrimitiveSlots(LightingPrimitive& primitive) const noexcept;
public:
    std::span<void* const> lighting_owners() const noexcept {
        return lighting_b_.captured ? std::span<void* const>(lighting_b_.owners) : std::span<void* const>(lighting_a_.owners);
    }
private:
    std::uintptr_t view_state_{};
    // Bounded hardware-query history participant. Native backing allocations
    // must survive A->B unchanged; other audited history maps must be empty.
    // Leases protect active queries from the native reuse pool. They are
    // process-local render resources, never canonical simulation inputs.
    struct VisibilityImage {
        std::array<std::byte,0x50> header{};
        std::vector<std::byte> entries, flags, hashes;
        std::vector<void*> queries;
        std::array<std::byte,0x36c> previous_matrices{}, current_matrices{};
        std::array<std::byte,0xc> times{}; // +898..8A3
        std::array<std::byte,0x50> camera_epoch{}; // +8B0..8FF
        std::array<std::byte,0x2c> lod{}; // +FF0..101B
        std::int32_t pool_bookkeeping{};
        std::uint32_t query_sampling_cursor{};
        std::uint64_t query_sampling_table{};
        std::size_t leases{};
        std::uintptr_t material_instance{}, material_resource{};
        std::array<std::byte,16> material_guid{};
        void* material_uniform{};
        void* material_scene_uniform{};
        bool captured{};
    } visibility_a_, visibility_b_, visibility_c_;
    std::array<void*,3> visibility_installed_{};
    bool visibility_storage_ready_{},visibility_storage_published_{},visibility_storage_transferred_{};
    bool visibility_executing_{},visibility_execution_settled_{};
    bool PrepareVisibilityStorage(std::size_t budget) noexcept;
    bool PruneOrphanVisibility(std::size_t budget) noexcept;
    bool BeginVisibilityExecution() noexcept;
    bool CanBeginVisibilityExecution() const noexcept;
    void TransferVisibilityExecution() noexcept;
    bool SettleVisibilityExecution(std::size_t budget) noexcept;
    bool ReleaseVisibilityImage(VisibilityImage& image) noexcept;
    bool visibility_dirty_{};
    std::size_t visibility_native_a_refs_{};
    bool CaptureVisibility(VisibilityImage& image,std::size_t budget) noexcept;
    bool CaptureVisibilityProtected(VisibilityImage& image,std::size_t budget);
    bool CaptureVisibilityUnchecked(VisibilityImage& image,std::size_t budget);
    bool VisibilityAdmission() const noexcept;
    bool VisibilityMatches(const VisibilityImage& image) const noexcept;
    bool PublishVisibility(bool original,bool preflight_only=false) noexcept;
    bool FinishVisibility(bool commit) noexcept;
    bool RetainedUniformBytes(void* value,std::size_t& bytes) const noexcept;
    bool ResolveMaterialPublication(std::uintptr_t& instance,std::uintptr_t& resource,std::uintptr_t& scene_slot) const noexcept;
    bool PublishMaterial(bool original,bool preflight_only=false) noexcept;
    bool material_dirty_{};

    // SSR output, previous scene color and in-place eye-adaptation history.
    struct History {
        std::size_t offset{};
        std::uintptr_t a{}, b{}, c{};
        Texture source_a, source_b, source_c, staging, image_a, image_b;
        D3D11_TEXTURE2D_DESC descriptor{};
        unsigned pixel_bytes{};
        std::uint64_t hash_a{}, hash_b{};
        bool dirty{}, image_dirty{}, external{}, copy_pixels{};
    };
    std::array<History, 3> histories_{{{0xb60}, {0xb40}, {0xae0}}};
    CaptureHandle captured_;
    bool CloneCaptureOwners(CapturedImage& output) noexcept;
    bool HistoriesDirty() const noexcept {
        for(const auto& h:histories_) if(h.dirty || h.image_dirty) return true;
        return false;
    }
    bool RetainReflection(bool original, std::size_t budget) noexcept;
    bool RetainExecutionReflection(std::size_t budget) noexcept;
    bool ReflectionBinding(std::uintptr_t target, ID3D11Texture2D* texture, int minimum_refs = 2) const noexcept;
    bool PublishReflection(bool original) noexcept;
    bool ReadReflectionHash() noexcept;
    bool SubmitRead(const std::array<Texture, 6>& source) noexcept;
    bool PrepareTransfer() noexcept;
    bool StartRetirementProbe(bool timeout) noexcept;
    bool ReadHashes(std::array<std::uint64_t, 6>& output) noexcept;
    void Fail(HRESULT error) noexcept;
};
}
