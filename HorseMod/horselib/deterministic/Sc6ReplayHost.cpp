#include "Sc6ReplayParticleObjects.hpp"
#include "Sc6ReplayPhysicsMarkers.hpp"
#include "Sc6ReplayGroundDebrisState.hpp"
#include "ReplayPhysicsStepConsumer.hpp"
#include "Sc6ReplayHost.hpp"
#include "DeterministicHookSet.hpp"
#include "NativeWindConstruction.hpp"
#include "NativeReplayRendering.hpp"
#include "NativeReplayWidgetClock.hpp"
#include "NativeReplayCpuEmitterLifetime.hpp"
#include "Sc6ReplayParticleConfiguration.hpp"
#include "Sc6ReplayParticleComponentOwner.hpp"
#include "NativeReplayCallbackAdmission.hpp"
#include "NativeReplayTraceTaskGuard.hpp"
#include "NativeReplayNiagaraObservation.hpp"
#include "NativeReplayVfxCompletionObservation.hpp"
#include "NativeReplayMaterialTaskGuard.hpp"
#include "NativeReplayPrerequisiteGuard.hpp"
#include "Sc6ReplayInputSource.hpp"
#include "InputProducer.hpp"
#include "Sc6ReplaySourceRegistrationState.hpp"
#include "Sc6ReplayHudState.hpp"
#include "Sc6ReplayTraceState.hpp"
#include "Sc6ReplayVfxHandlerState.hpp"
#include "Sc6ReplayExecutor.hpp"
#include "ReplaySeekOwnership.hpp"
#include "Sc6CandidateCheckpointCapture.hpp"
#include "Sc6ReplayNativeBridge.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/CoreUObject/UObject/FStrProperty.hpp>
#include <Unreal/UEngine.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <polyhook2/Detour/x64Detour.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include "../GameImGui/GameImGui.hpp"
#include <array>
#include <cstring>
#include <cmath>
#include <string>

namespace Horse::Deterministic
{
#include "Sc6ReplayHost.GroundDebris.inl"
struct Sc6ReplayHost::SurfaceSnapshot
{
    Horse::GameImGui::PresentHook::ReplaySurfaceImage image;
    std::uint64_t session{}, tick{}, epoch{};
    // The render event proves callback completion only. GPU completion and
    // correspondence to the target scene are separate transaction checks.
    bool retained{};
};
std::size_t Sc6ReplayHost::Checkpoint::owned_bytes() const noexcept
{
    std::size_t bytes = sizeof(*this) + gameplay.bytes.capacity()
        + gameplay.local_images.capacity() * sizeof(LocalReconstructionImage)
        + world.owned_bytes() + scheduler.owned_bytes() + vfx.owned_bytes();
    for (const auto& image : gameplay.local_images) bytes += image.bytes.capacity();
    if (input_revision) bytes += input_revision->owned_bytes();
    if (hud) bytes += hud->owned_bytes();
    if (traces) bytes += traces->owned_bytes();
    if (vfx_handler) bytes += vfx_handler->owned_bytes();
    if (source_registration) bytes += source_registration->owned_bytes();
    if (ground) bytes += sizeof(Sc6ReplayGroundDebrisState)+ground->owned_bytes();
    bytes+=particle_configurations.capacity()*sizeof(particle_configurations[0]);
    for(const auto& configuration:particle_configurations)if(configuration)bytes+=configuration->owned_bytes();
    if (surface) bytes += sizeof(SurfaceSnapshot) + surface->image.bytes;
    bytes += Sc6ReplayParticleCopy::captured_bytes(gpu);
    return bytes;
}
struct Sc6ReplayHost::HistoricalRestore
{
    // Armed only by Request/direct Prepare, before any owner acquisition.
    // First member means last destruction: complete B, corrected owners and
    // required native/render retirement all remain inside this negative scope.
    // This blocks unexpected Start by terminal containment, not a positive lease.
    NativeReplayMaterialTaskGuard::TraceStartVeto trace_start_veto;
    // Begins before preparation/publication can produce material work; the
    // execution object is allocated too late to establish this baseline.
    NativeReplayMaterialTaskGuard::Receipt material=NativeReplayMaterialTaskGuard::BeginOperation();
    std::atomic<bool> material_command_deferred{};
    std::uint64_t material_wait_started{};
    // The render-command predicate also consumes the sticky ownership veto.
    std::atomic<bool> material_failed{};
    struct FreshParticle {
        Sc6ReplayObjectLease lease;
        Sc6ReplayColdParticleOwner::Witness cold;
        Sc6ReplayParticleComponentOwner::Registration registration;
        Sc6ReplayParticleComponentOwner::RenderPreparation render;
        struct Gpu {
            Sc6ReplayParticleComponentOwner::GpuRegistration registration;
            Sc6ReplayCpuEmitterState::FreshGpuOwner owner;
            std::size_t ordinal{};
            std::uint64_t retirement_serial{};
        };
        std::vector<Gpu> gpus;
        Sc6ReplayCpuEmitterState::ComponentReplacement binding;
        void* slots{};
        std::size_t slot_bytes{},count{},native_allowance{};
        bool slots_installed{},retired{},adopted{},settled{},native_dead{};
        std::size_t owned_bytes() const noexcept {
            auto bytes=sizeof(*this)+lease.owned_bytes()+gpus.capacity()*sizeof(Gpu)+slot_bytes+native_allowance+render.charged_bytes;
            for(const auto& gpu:gpus)bytes+=gpu.owner.charged_bytes;
            return bytes;
        }
    };
    enum class FreshRenderPhase { Empty, Entered, Queued, Ready };
    FreshRenderPhase fresh_render_phase{FreshRenderPhase::Empty};
    FreshRenderPhase fresh_trace_render_phase{FreshRenderPhase::Empty};
    bool fresh_trace_sources_ready{};
    std::uint64_t fresh_trace_render_serial{};
    std::vector<std::unique_ptr<FreshParticle>> fresh_particles;
    std::vector<std::unique_ptr<Sc6ReplayGroundDebrisState::ColdRoot>> fresh_ground_roots;
    bool fresh_ground_roots_prepared{};
    std::vector<std::unique_ptr<Sc6ReplayTraceState::FreshChild>> fresh_trace_children;
    std::unique_ptr<Sc6ReplayTraceState> trace_projection;
    std::unique_ptr<Sc6ReplayTraceState> trace_survivors;
    std::vector<Sc6ReplayVfxState::ReconstructionBinding> particle_bindings;
    std::vector<Sc6ReplaySchedulerState::ComponentBinding> scheduler_bindings;
    Sc6ReplaySchedulerState fresh_scheduler_staging,fresh_scheduler_target,fresh_scheduler_adopted;
    Sc6ReplaySchedulerState::PreparedRestore fresh_scheduler_transfer;
    bool particle_inventory_ready{},fresh_scheduler_staged{},fresh_scheduler_published{},fresh_scheduler_committed{},fresh_scheduler_ready{};
    struct Execution {
        // Process-local abort receipt, not deterministic checkpoint state.
        std::atomic<void*> protected_particle{};
        std::atomic<unsigned> protected_particle_routes{};
        std::unique_ptr<NativeReplayPrerequisiteGuard> prerequisite_guard;
        CompletionOwnerContext finish_context{};
        bool finish_armed{};
        // Fixed storage is charged by sizeof(Execution). Declared after its
        // context so the guard detaches before that context can be destroyed.
        ReplayVfxFinishDispatch finish_guard;
        enum class Participant : std::uint8_t { Retained, HandedOff, Settled };
        enum class Render : std::uint8_t { Retained, CoordinatesPending, CoordinatesComplete, HandedOff, DrainPending, Drained, Settled };
        enum class Quarantine : std::uint8_t { None, Installed, Retired };
        enum class Capture : std::uint8_t { Empty, RetiringOwners, Current };
        Execution(std::uint64_t original_tick,std::uint64_t target_tick) noexcept
            : ownership(original_tick,target_tick) {}
        ReplaySeekOwnership ownership;
        // C is observed only to validate ownership and safely retire its
        // allocations. No C observations are installed as historical inputs.
        Sc6ReplayVfxState current, retiring;
        Sc6ReplaySchedulerState scheduler;
        Sc6ReplayTraceState traces;
        Sc6ReplayHudState hud;
        ReplayRenderState rendering{};
        std::shared_ptr<SurfaceSnapshot> recovered_surface;
        Sc6ReplayVfxState::ParticleBirthSet births;
        Sc6ReplayVfxState::ParticleBirthSet::RecoveryRetirement retirement;
        Sc6ReplayTraceState::Retirement trace_retirement;
        Sc6ReplayTraceState::RenderRecovery trace_render_retirement,trace_render_reconstruction,trace_target_reconstruction;
        // Participant journals record reversible allocation handoffs; seek
        // admission/commit comes exclusively from ownership above.
        std::array<Participant,11> participants{};
        std::optional<unsigned> settlement_failure_after; // Bounded one-shot injection: 0 after GPU drain, 1..N after CPU settlements.
        Render render{Render::Retained};
        Quarantine quarantine{Quarantine::None};
        Capture capture{Capture::Empty};
        std::uint64_t boundary_epoch{};
        std::size_t scratch_reservation{};
        std::size_t owned_bytes() const noexcept {
            const auto actual=sizeof(*this)+(recovered_surface?sizeof(SurfaceSnapshot):0)+current.owned_bytes()+retiring.owned_bytes()
                +scheduler.owned_bytes()+traces.owned_bytes()+hud.owned_bytes()
                +(prerequisite_guard?sizeof(NativeReplayPrerequisiteGuard):0);
            return (std::max)(actual,scratch_reservation);
        }
    };
    std::unique_ptr<Execution> execution;
    CheckpointHandle target;
    const Sc6ReplaySchedulerState& TargetScheduler() const noexcept {
        return scheduler_bindings.empty()?target->scheduler:fresh_scheduler_target;
    }
    const Sc6ReplayTraceState& TargetTraces() const noexcept {
        return trace_projection?*trace_projection:*target->traces;
    }
    const Sc6ReplayTraceState& SurvivingTraces() const noexcept {
        return trace_survivors?*trace_survivors:*target->traces;
    }
    Checkpoint undo;
    Sc6ReplayVfxState::ParticleBirthSet birth;
    Sc6ReplayVfxState::PreparedManager manager;
    Sc6ReplayVfxState::PreparedEmitterSet cpu, gpu;
    Sc6ReplayVfxState::PreparedTilePools pools;
    Sc6ReplaySchedulerState::PreparedRestore scheduler;
    Sc6ReplayWorldState::PreparedRestore world;
    Sc6ReplayTraceState::Prepared traces;
    Sc6ReplayVfxHandlerState::Prepared vfx_handler;
    Sc6ReplaySourceRegistrationState::Prepared source_registration;
    Sc6ReplayHudState::Prepared hud;
    Sc6ReplayPhysicsMarkers physics_markers;
    Sc6ReplayGroundDebrisState ground;
    std::unique_ptr<Sc6ReplayWorldState::PhysicsBoundary> physics_observation;
    Horse::GameImGui::PresentHook::ReplaySurfaceImage undo_surface;
    std::shared_ptr<SurfaceSnapshot> presented_surface;
    RestoreOperationWitness witness;
    UcrtRandBrokerMode prior_mode{};
    bool game_dirty{}, source_dirty{}, execution_dirty{}, surface_dirty{}, rendering_dirty{};
    bool physics_dirty{}, physics_queries_pending{}, hud_dirty{}, traces_dirty{}, vfx_handler_dirty{};
    bool cancel{}, surface_requested{}, gpu_requested{};
    bool preparing{}, preparation_retiring{};
    bool commit_requested{}, native_retired{}, retirement_requested{};
    bool display_started{},display_requested{},display_complete{},display_finish_requested{},display_finished{};
    // Successful irreversible steps are never repeated after a later failure.
    // A failed step remains terminal until its own ownership can be resolved.
    unsigned retirement_cursor{};
    ~HistoricalRestore() {
        // Detach before target/undo/trace images are destroyed. A native call
        // still holding the context makes destruction terminal, never partial.
        if(execution && !execution->finish_guard.Stop())__fastfail(FAST_FAIL_INVALID_ARG);
    }
};
struct Sc6ReplayHost::ConsumerHoldState {
    NativeReplayTraceTaskGuard guard;
    CheckpointHandle checkpoint;
    std::array<NativeReplayTraceTaskGuard::Binding,2> bindings{};
    std::array<std::uint64_t,2> contracts{};
    Sc6ReplayHost* host{};
    std::uint64_t target{}, held_epoch{}, polls{};
    std::uint32_t hold_ms{};
    LONGLONG qpc{};
    double wall_reference{};
    bool acquired{}, suspended{}, completed{}, failed{};
    std::size_t owned_bytes() const noexcept {return sizeof(*this)+guard.owned_bytes()-sizeof(guard);}
};

Sc6ReplayHost::Sc6ReplayHost() = default;
Sc6ReplayHost::~Sc6ReplayHost() { if(!StopForDestruction()) __fastfail(FAST_FAIL_INVALID_ARG); }

#include "Sc6ReplayHost.Destruction.inl"

bool Sc6ReplayHost::ConfigureMemoryBudget(std::size_t bytes, bool diagnostic) noexcept
{
    auto* self=active_;
    if(!self || GetCurrentThreadId()!=self->thread_ || self->depth_
        || !self->simulation_ || self->simulation_->continuation().tick!=0
        || self->historical_restore_ || self->restore_undo_ || self->captured_checkpoint_
        || !self->retained_checkpoints_.empty() || !self->checkpoint_pins_.empty()
        || self->particle_copy_ || self->surface_event_
        || self->particle_command_pending_.load(std::memory_order_acquire)
        || self->capture_operation_.phase!=CapturePhase::Idle || self->SessionExitRequested()
        || bytes<Schema::replay_timeline_memory_limit
        || bytes>(diagnostic?Schema::replay_debug_memory_limit:Schema::replay_timeline_memory_limit))return false;
    self->memory_limit_=bytes;
    return true;
}


bool Sc6ReplayHost::ValidWorld() const noexcept
{
    __try
    {
        const auto* item = RC::Unreal::FUObjectArray::IndexToObject(object_index_);
        return item && item->GetUObject() == world_ && item->IsValid(false)
            && item->GetSerialNumber() == object_serial_;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Sc6ReplayHost::Bind(std::uintptr_t base, void* manager, Sc6ReplayExecutor* simulation, bool yield_each_tick,
    UcrtRandBroker* broker, void* storage_context, std::size_t (*companion_storage)(void*) noexcept) noexcept
{
    if (active_ || application_hook_ || session_exit_hook_ || !base || !manager) return false;
    // A diagnostic allowance belongs to one session, never a later native reload.
    memory_limit_=Schema::replay_timeline_memory_limit;
    session_exit_={};session_exit_scene_=session_exit_manager_=session_exit_function_=nullptr;
    set_engine_body_ = reinterpret_cast<SetEngineBody>(GetProcAddress(
        GetModuleHandleW(L"UE4SS.dll"), "UE4SS_SetEngineTickOverride"));
    if (!set_engine_body_) return false;
    // UE4SS retains the engine entry detour and all callback ordering. Verify
    // both its target and native bytes beyond the entry trampoline's prologue.
    constexpr std::array<unsigned char, 13> signature{
        0x0f, 0x57, 0xc0, 0x0f, 0x28, 0xf1, 0x45, 0x0f, 0xb6, 0xe8, 0x0f, 0x2f, 0xf0};
    if (reinterpret_cast<std::uintptr_t>(RC::Unreal::UEngine::TickInternal.get_function_address()) != base + 0x1e38f70
        || std::memcmp(reinterpret_cast<void*>(base + 0x1e38f90), signature.data(), signature.size())) return false;
    image_base_ = base;
    if (!ResolveWorld(manager)) return false;
    checkpoint_broker_ = broker;
    storage_context_ = storage_context;
    companion_storage_ = companion_storage;
    checkpoint_session_ = ++next_checkpoint_session_;
    checkpoint_capture_.reset();
    restore_undo_.reset();
    const auto vtable = *reinterpret_cast<std::uintptr_t*>(manager);
    if (*reinterpret_cast<std::uintptr_t*>(vtable + 0x3c0) != base + 0x1c2d330
        || *reinterpret_cast<std::uintptr_t*>(vtable + 0x418) != base + 0x3fbf30) return false;
    executor_.BindSimulation(manager, simulation, yield_each_tick);
    simulation_ = simulation;
    yield_each_tick_ = yield_each_tick;
    interior_phase_ = InteriorPhase::Idle;
    interior_observer_ = nullptr;
    interior_updates_ = 0;
    thread_ = GetCurrentThreadId();
    paused_ = failed_ = false;
    ground_update_pending_=false;
    ground_body_inventory_={};ground_body_inventory_session_=0;
    held_updates_ = depth_ = 0;
    completed_worlds_ = 0;
    completed_engines_ = 0;
    initial_groups_ = executor_.task_groups().completed_groups();
    initial_tasks_ = executor_.task_groups().dispatched_tasks();
    active_ = this;
    if (!BindApplication()) { active_ = nullptr; return false; }
    registered_ = set_engine_body_(this, &TickEngine);
    if (registered_) return true;
    if (!UnbindApplication()) __fastfail(FAST_FAIL_INVALID_ARG);
    active_ = nullptr;
    return false;
}

bool Sc6ReplayHost::ResolveWorld(void* manager) noexcept
{
    __try
    {
        auto* object = static_cast<RC::Unreal::UObject*>(manager);
        const auto* manager_item = RC::Unreal::FUObjectArray::IndexToObject(object->GetInternalIndex());
        if (!manager_item || manager_item->GetUObject() != object || !manager_item->IsValid(false)) return false;
        manager_ = manager;
        manager_index_ = object->GetInternalIndex();
        manager_serial_ = manager_item->GetSerialNumber();
        const auto vtable = *reinterpret_cast<std::uintptr_t*>(object);
        world_ = reinterpret_cast<void* (*)(void*)>(*reinterpret_cast<std::uintptr_t*>(vtable + 0x138))(object);
        if (!world_) return false;
        object_index_ = static_cast<RC::Unreal::UObject*>(world_)->GetInternalIndex();
        const auto* item = RC::Unreal::FUObjectArray::IndexToObject(object_index_);
        if (!item) return false;
        object_serial_ = item->GetSerialNumber();
        if (object_serial_ <= 0 || !ValidWorld()) return false;
        engine_ = *reinterpret_cast<void**>(image_base_ + 0x43b3068);
        if (!engine_) return false;
        engine_index_ = static_cast<RC::Unreal::UObject*>(engine_)->GetInternalIndex();
        const auto* engine_item = RC::Unreal::FUObjectArray::IndexToObject(engine_index_);
        if (!engine_item || engine_item->GetUObject() != engine_ || !engine_item->IsValid(false)) return false;
        engine_serial_ = engine_item->GetSerialNumber();
        // Native nullrhi static initialization must already have completed on
        // this game thread; binding happens after an observed native update.
        const auto guard = *reinterpret_cast<int*>(image_base_ + 0x42987ec);
        if (guard == 0 || guard == -1) return false;
        auto* bytes = static_cast<std::byte*>(engine_);
        const auto count = *reinterpret_cast<int*>(bytes + 0xbf0);
        auto** contexts = *reinterpret_cast<std::byte***>(bytes + 0xbe8);
        bound_context_ = nullptr;
        for (int i = 0; i < count; ++i)
            if (*reinterpret_cast<void**>(contexts[i] + 0x298) == world_)
            {
                if (bound_context_) return false;
                bound_context_ = contexts[i];
                bound_context_handle_ = *reinterpret_cast<std::uint64_t*>(contexts[i] + 0xc8);
            }
        return bound_context_ && engine_serial_ > 0 && ValidEngineContext();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Sc6ReplayHost::ValidEngineContext() const noexcept
{
    __try
    {
        const auto* item = RC::Unreal::FUObjectArray::IndexToObject(engine_index_);
        if (!item || item->GetUObject() != engine_ || !item->IsValid(false)
            || item->GetSerialNumber() != engine_serial_
            || *reinterpret_cast<void**>(image_base_ + 0x43b3068) != engine_) return false;
        auto* bytes = static_cast<std::byte*>(engine_);
        const auto count = *reinterpret_cast<int*>(bytes + 0xbf0);
        auto** contexts = *reinterpret_cast<std::byte***>(bytes + 0xbe8);
        for (int i = 0; i < count; ++i)
            if (contexts[i] == bound_context_)
                return *reinterpret_cast<void**>(contexts[i] + 0x298) == world_
                    && *reinterpret_cast<std::uint64_t*>(contexts[i] + 0xc8) == bound_context_handle_;
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Sc6ReplayHost::CheckBinding() noexcept
{
    if (active_ != this || GetCurrentThreadId() != thread_) return false;
    if (!ValidManager() || !ValidWorld() || !ValidEngineContext()
        || !wind_construction_ || wind_construction_->failed()
        || !replay_rendering_ || replay_rendering_->failed()
        || !widget_clock_ || widget_clock_->failed()) failed_ = true;
    return !failed_;
}

bool Sc6ReplayHost::Pause(bool paused) noexcept
{
    if(SeekOwnsExecution() && !seek_driving_) return false;
    if (capture_operation_.phase!=CapturePhase::Idle || historical_restore_ || checkpoint_restoring_ || active_ != this || GetCurrentThreadId() != thread_ || depth_ || failed_
        || !engine_idle() || !CheckBinding()) return false;
    paused_ = paused;
    return true;
}

bool Sc6ReplayHost::Stop() noexcept
{
    if (consumer_hold_) return false;
    if(index_checkpoint_.pending())return false;
    if(SeekOwnsExecution() || seek_retirement_pending_) return false;
    if (thread_ && GetCurrentThreadId() != thread_) return false;
    std::erase_if(retained_checkpoints_, [](const auto& checkpoint) { return checkpoint.expired(); });
    if (!retained_checkpoints_.empty()) return false;
    if (Sc6ReplayParticleCopy::retained_capture_bytes()) return false;
    if (Sc6ReplayObjectLease::quarantined_bytes()
        && (GetCurrentThreadId() != thread_ || !Sc6ReplayObjectLease::RetireQuarantined(image_base_).ok())) return false;
    if (historical_restore_ || capture_operation_.phase!=CapturePhase::Idle) return false;
    if (particle_command_pending_.load() || (particle_copy_ && particle_copy_->witness().phase != Sc6ReplayParticleCopy::Phase::Released)
        || (corrected_particle_copy_ && corrected_particle_copy_->witness().phase != Sc6ReplayParticleCopy::Phase::Released))
        return false;
    if (checkpoint_restoring_ || engine_post_deferred_) return false;
    if (active_ != this) return !registered_;
    if (GetCurrentThreadId() != thread_ || depth_ || surface_event_ || !executor_.idle() || !engine_idle()
        || Horse::GameImGui::PresentHook::instance().replay_surface_ready()) return false;
    if (!UnbindIndex()) return false;
    index_.Cancel();
    if (registered_ && !set_engine_body_(this, nullptr)) return false;
    registered_ = false;
    paused_ = false;
    // EngineTickPost can release the engine owner while FEngineLoop still
    // owes its tail. Keep this owner alive until that tail actually returns.
    if (application_active_)
    {
        application_stop_requested_ = true;
        return true;
    }
    if (!UnbindSessionExit() || !UnbindApplication()) return false;
    checkpoint_capture_.reset();
    restore_undo_.reset();
    particle_copy_.reset();
    corrected_particle_copy_.reset();
    held_surface_.reset();
    particle_birth_ = {};
    particle_birth_render_ = {};
    particle_birth_render_count_ = 0;
    particle_birth_budget_ = 0;
    checkpoint_broker_ = nullptr;
    index_.Clear();
    index_checkpoint_={};
    index_checkpoint_selection_=IndexCheckpointSelection::Explicit;
    index_checkpoint_round_=0;
    index_checkpoint_replacement_attempts_=0;
    queued_indexed_seek_.reset();indexed_seek_failure_=FailureCode::None;
    Horse::GameImGui::PresentHook::instance().publish_replay_timeline(false,0,0);
    Horse::GameImGui::PresentHook::instance().publish_replay_index_progress(0,0,0,false);
    Horse::GameImGui::PresentHook::instance().take_replay_index_cancel();
    Horse::GameImGui::PresentHook::instance().take_replay_seek_request();
    Horse::GameImGui::PresentHook::instance().publish_replay_pause_availability(false,0);
    Horse::GameImGui::PresentHook::instance().take_replay_pause_request();
    tick_advance_ = {};
    tick_advance_context_ = nullptr;
    tick_advance_observer_ = nullptr;
    active_ = nullptr;
    pause_monitor_=nullptr;pause_monitor_context_=nullptr;
    world_ = nullptr;
    engine_ = bound_context_ = nullptr;
    return true;
}

bool Sc6ReplayHost::TickEngine(void* owner, RC::Unreal::UEngine* engine, float delta, bool idle_mode) noexcept
{
    auto* self = static_cast<Sc6ReplayHost*>(owner);
    if (engine != self->engine_ || !(delta >= 0.0f)) return false;
    if (self->depth_ || !self->engine_idle()) __fastfail(FAST_FAIL_INVALID_ARG);
    if (!self->CheckBinding()) return false;
    ++self->depth_;
    self->DrainEngine(delta, idle_mode);
    --self->depth_;
    return true;
}

void Sc6ReplayHost::DiagnoseCheckpointOwner(const Checkpoint& checkpoint) const noexcept
{
    RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint admission diagnostic tick={} target_shape={} hud_limitation={} depth={} application_idle={} engine_idle={} world_idle={} engine_post_deferred={} owned_bytes={} diagnostic_only=true\n"),
        checkpoint.execution.tick,checkpoint.historical_target_shape_supported(),
        RC::to_generic_string(checkpoint.hud ? (checkpoint.hud->historical_target_limitation()?checkpoint.hud->historical_target_limitation():"none") : "missing"),
        depth_,application_phase_==ApplicationPhase::Idle,engine_idle(),world_idle(),engine_post_deferred_,AdmissionBytes());
    checkpoint.vfx.DiagnoseRetainedOwnerFailure(&checkpoint.execution.tick,[](const void* context,const Sc6ReplayObjectLease::FailureWitness& w) {
        const auto* item=w.reference?RC::Unreal::FUObjectArray::IndexToObject(w.index):nullptr;
        const bool same=item && item->GetUObject()==w.original && item->GetSerialNumber()==w.serial;
        // GC is excluded by DiagnoseFailure. Pending-kill naming is only
        // diagnostic; admission and the retained transaction remain strict.
        const bool readable=same && item->IsValid(true);
        if(w.captured_identity)
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint collected identity tick={} reference={} weak={}/{} name={} class={} vtable_rva={:x} diagnostic_only=true\n"),
                *static_cast<const std::uint64_t*>(context),w.ordinal,w.index,w.serial,
                RC::Unreal::FName(static_cast<std::int64_t>(w.object_name)).ToString(),RC::Unreal::FName(static_cast<std::int64_t>(w.class_name)).ToString(),
                w.vtable-reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)));
        if(w.before_particle_state || w.captured_component)
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint particle lifetime tick={} reference={} captured_component={} captured_flags={:x} captured_template_flags={:x} captured_auto_destroy={:x} captured_emitters={} attachment_users={} before_visit_state={} before_visit_weak_live={} before_visit_flags={:x} before_visit_template_flags={:x} before_visit_auto_destroy={:x} diagnostic_only=true\n"),
                *static_cast<const std::uint64_t*>(context),w.ordinal,w.captured_component,w.captured_component_flags,w.captured_template_flags,w.captured_auto_destroy,w.captured_emitters,w.captured_attachment_users,
                w.before_particle_state,w.before_weak_live,w.before_component_flags,w.before_template_flags,w.before_auto_destroy);
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint expired owner role tick={} reference={} captured_component={} manager_slots={} first_slot_id={} first_slot_kind={} cpu_emitters={} gpu_emitters={} attachment_users={} captured_metadata_only=true\n"),
            *static_cast<const std::uint64_t*>(context),w.ordinal,w.captured_component,w.captured_manager_slots,
            w.captured_slot_id,w.captured_slot_kind,w.captured_cpu_emitters,w.captured_gpu_emitters,w.captured_attachment_users);
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint captured VFX references tick={} reference={} inventory_valid={} provider_users={} completion_users={} own_completions={} manager_listeners={} listener_users={} scope=captured_vfx_only lifetime_admission=false\n"),
            *static_cast<const std::uint64_t*>(context),w.ordinal,w.captured_reference_inventory_valid,
            w.captured_provider_users,w.captured_completion_users,w.captured_own_completions,w.captured_manager_listeners,w.captured_listener_users);
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint lease failure tick={} reference={} has_reference={} weak={}/{} collector_changed={} invalidated={} membership={} same_slot={} pending_kill={} unreachable={} owner={}\n"),
            *static_cast<const std::uint64_t*>(context),w.ordinal,w.reference,w.index,w.serial,w.collector_changed,w.invalidated,w.membership,same,
            same && item->IsPendingKill(),same && item->IsUnreachable(),
            readable?static_cast<RC::Unreal::UObject*>(w.original)->GetFullName():L"unavailable");
    });
}

#include "Sc6ReplayHost.Engine.inl"
#include "Sc6ReplayHost.Application.inl"
#include "Sc6ReplayHost.Surface.inl"
#include "Sc6ReplayHost.Interior.inl"
#include "Sc6ReplayHost.Consumer.inl"
Status Sc6ReplayHost::ValidateParticleCompletionOwnership(const Checkpoint& image) const
{
    if(!image.traces) return Status::failure(FailureCode::CapturePreflightFailed);
    return ValidateParticleCompletionOwnership(image.vfx,*image.traces);
}
Status Sc6ReplayHost::ValidateParticleCompletionOwnership(const Sc6ReplayVfxState& image,const Sc6ReplayTraceState& traces) const
{
    const CompletionOwnerContext context{image_base_,&traces};
    const char* failed_check{};
    const auto status=image.ValidateCompletionOwnership(&context,&CheckParticleCompletionReceiver,&failed_check);
    if(!status.ok()) RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] particle completion ownership rejected check={} tick={} code={}\n"),
        RC::to_generic_string(failed_check?failed_check:"unknown"),simulation_?simulation_->continuation().tick:0,static_cast<unsigned>(status.code));
    return status;
}

bool Sc6ReplayHost::CheckParticleCompletionReceiver(const void* opaque,bool manager,
    const std::array<std::int32_t,2>& weak,std::uint64_t name)
{
    const auto& context=*static_cast<const CompletionOwnerContext*>(opaque);
    if(weak[0]<0 || weak[1]<=0) return false;
    auto* item=RC::Unreal::FUObjectArray::IndexToObject(weak[0]);
    if(!item || !item->IsValid(false) || item->GetSerialNumber()!=weak[1]) return false;
    auto* receiver=item->GetUObject();
    if(!receiver || (!manager && !context.traces->OwnsCompletionReceiver(receiver,weak))) return false;
    auto* function=receiver->GetFunctionByNameInChain(manager?L"OnParticleSystemFinished":L"OnVFxFinished");
    if(!function || !(static_cast<unsigned>(function->GetFunctionFlags())&0x400)
        || function->GetScript().Num()!=0) return false;
    const auto fname=function->GetFName();
    // FName::Equals omits Number. Both native delegate words must match.
    const auto expected=std::uint64_t(fname.GetComparisonIndex().ToUnstableInt())
        | (std::uint64_t(static_cast<std::uint32_t>(fname.GetNumber()))<<32);
    if(name!=expected || reinterpret_cast<std::uintptr_t>(function->GetFuncPtr())
        !=context.base+(manager?0x2413250:0xc40570)) return false;
    // The manager thunk dispatches virtually; the trace thunk calls its
    // audited body directly. Verify the manager's actual final consumer.
    return !manager || EngineField<std::uintptr_t>(reinterpret_cast<void*>(
        EngineField<std::uintptr_t>(receiver,0)),0x5f8)==context.base+0x89f870;
}

#include "Sc6ReplayHost.CheckpointDisplay.inl"
#include "Sc6ReplayHost.Checkpoint.inl"
#include "Sc6ReplayHost.CheckpointOwnership.inl"
#include "Sc6ReplayHost.Restore.inl"
#include "Sc6ReplayHost.Seek.inl"
#include "Sc6ReplayHost.Rolling.inl"
#include "Sc6ReplayHost.Index.inl"
#include "Sc6ReplayHost.IndexCheckpoint.inl"
#include "Sc6ReplayHost.IndexedSeek.inl"
#include "Sc6ReplayHost.SessionExit.inl"
}
