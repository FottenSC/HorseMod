// A one-shot native function task, directed to the same named queue as native
// render commands. Completion is polled; waiting never pumps game-thread tasks.
// This event covers the surface callback only. It does not join RHI dispatch,
// RHI execution, deferred resource retirement or GPU completion, and must not
// be used as the historical particle restore publication barrier.
bool Sc6ReplayHost::QueueSurface(SurfaceCommand command)
{
    if (surface_event_ || GetCurrentThreadId()!=thread_) return false;
    if(command==SurfaceCommand::Arm) {
        const auto current=Horse::GameImGui::PresentHook::instance().replay_surface_bytes();
        surface_arm_budget_=(std::min)(std::size_t{32ull*1024*1024},AdmissionRemaining()+static_cast<std::size_t>(current));
        if(!surface_arm_budget_) return false;
    }
    auto* image = reinterpret_cast<void*>(image_base_);
    const auto destination = EngineField<std::uint32_t>(image, 0x406f778);
    // Native1415ED070 also executes render callbacks on GT when threaded
    // rendering is disabled. False flags alone do not prove retirement:
    // StopRenderingThread clears flags before joining the worker wrappers.
    std::byte* inline_thread{};
    if ((destination & 0xff)==2) {
        if(!EngineField<unsigned char>(image,0x419718c)
            || EngineField<DWORD>(image,0x419716c)!=GetCurrentThreadId())return false;
        for(const auto offset:{0x4351840u,0x4351841u,0x434459cu,0x434459du,
            0x434459eu,0x434459fu,0x43445a0u})
            if(EngineField<unsigned char>(image,offset))return false;
        for(const auto offset:{0x4167818u,0x4351848u,0x4168028u})
            if(EngineField<void*>(image,offset))return false;
        if(EngineField<unsigned>(image,0x4168024))return false;
        auto* graph=EngineField<void*>(image,0x4166720);
        if(!graph)return false;
        inline_thread=EngineField<std::byte*>(graph,8+2*0x18);
        if(!inline_thread || EngineField<unsigned>(inline_thread,0x10)!=2
            || EngineField<int>(inline_thread,0x28)!=0)return false;
    } else if ((destination & 0xff)!=3 || !EngineField<bool>(image,0x4351840)) return false;
    surface_command_ = command;
    surface_result_.store(false);
    std::uintptr_t storage[3]{};
    auto* builder = EngineNative<std::uintptr_t*>(image_base_, 0x39a610, storage, 0, 2);
    auto* task = reinterpret_cast<void*>(builder[0]);
    if (!task || EngineField<std::uintptr_t>(task, 0) != image_base_ + 0x325ba68
        || EngineField<int>(task, 0xc) != 1) __fastfail(FAST_FAIL_INVALID_ARG);
    EngineField<std::uintptr_t>(task, 0x10) = reinterpret_cast<std::uintptr_t>(&RunSurfaceTask);
    EngineField<void*>(task, 0x18) = this;
    surface_event_ = EngineField<void*>(task, 0x28);
    if (!surface_event_) __fastfail(FAST_FAIL_INVALID_ARG);
    InterlockedIncrement(&EngineField<LONG>(surface_event_, 0x48));
    // 1403A1EC0's no-prerequisite submission, with explicit render affinity.
    // Its normal body hardcodes AnyThread and cannot be used for DXGI work.
    EngineField<std::uint8_t>(task, 0x20) = 1;
    EngineField<std::uint32_t>(task, 8) = destination;
    if (InterlockedDecrement(&EngineField<LONG>(task, 0xc)) != 0)
        __fastfail(FAST_FAIL_INVALID_ARG);
    if(inline_thread) {
        // Execute the same native wrapper once with GT's actual subsequent
        // array. It owns callback/event/refcount/TLS retirement; do not enqueue
        // as well, inspect recycled task storage, or infer GPU completion.
        EngineVirtual(task,8,inline_thread+0x20,2u);
    } else EngineVirtual(EngineNative<void*>(image_base_, 0xd24400), 0, task, destination, 2u);
    return true;
}

bool Sc6ReplayHost::PollSurface(bool& complete)
{
    complete = false;
    if (!surface_event_) return false;
    const auto flags = InterlockedCompareExchange64(&EngineField<LONG64>(surface_event_, 8), 0, 0);
    if (!(flags & (1ll << 26))) return true;
    complete = true;
    auto* event = surface_event_;
    surface_event_ = nullptr;
    if (InterlockedDecrement(&EngineField<LONG>(event, 0x48)) == 0)
        EngineNative(image_base_, 0xd2ebe0, event);
    const bool success = surface_result_.load();
    if (!success)
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] replay surface command rejected command={} tick={} B_retained={} arm_budget={} surface_bytes={} owned_bytes={}\n"),
            static_cast<unsigned>(surface_command_), simulation_->continuation().tick,
            historical_restore_ && historical_restore_->undo.valid,surface_arm_budget_,
            Horse::GameImGui::PresentHook::instance().replay_surface_bytes(),AdmissionBytes());
    return success;
}

void Sc6ReplayHost::RunSurfaceTask(void* owner)
{
    auto* self = static_cast<Sc6ReplayHost*>(owner);
    auto& surface = Horse::GameImGui::PresentHook::instance();
    bool result = false;
    switch (self->surface_command_)
    {
    case SurfaceCommand::Arm:
        // One bounded presentation allocation. The full replay budget also
        // accounts for checkpoints/world scratch; this is only its UI lease.
        result = surface.arm_replay_surface(self->surface_arm_budget_);
        break;
    case SurfaceCommand::Draw: result = surface.draw_replay_surface(); break;
    case SurfaceCommand::Release: result = surface.release_replay_surface(); break;
    case SurfaceCommand::ReleaseForAdvance: result = surface.release_replay_surface(true); break;
    case SurfaceCommand::RetireHeldDisplay:
        surface.retire_replay_surface();
        result = true; // Submission of a poll is not successful restoration.
        break;
#include "Sc6ReplayHost.SurfacePublication.inl"
    case SurfaceCommand::ParticleCopy:
        self->QueueParticleCopyCommand();
        result = true;
        break;
    }
    self->surface_result_.store(result);
    // Native 14039B210 still owns event completion, subsequent dispatch,
    // reference release and task recycling after this callback returns.
}

bool Sc6ReplayHost::ParticleCopyExperiment(ParticleCopyAction action,
    Sc6ReplayParticleCopy::Witness* output, bool* command_pending) noexcept
{
    auto* self = active_;
    if (!self || !output || !command_pending || GetCurrentThreadId() != self->thread_) return false;
    const bool corrected=self->capture_driving_ && self->corrected_capture_;
    auto& copy=corrected?self->corrected_particle_copy_:self->particle_copy_;
    auto& command_failed=corrected?self->corrected_particle_command_failed_:self->particle_command_failed_;
    if(corrected && action!=ParticleCopyAction::Read && action!=ParticleCopyAction::BeginWithoutReadbacks
        && action!=ParticleCopyAction::Poll && action!=ParticleCopyAction::SealCapture
        && action!=ParticleCopyAction::Finish && action!=ParticleCopyAction::RetireInFlight
        && action!=ParticleCopyAction::RetireCaptures)return false;
    if(self->SeekOwnsExecution() && !self->seek_driving_ && !corrected && action!=ParticleCopyAction::Read) return false;
    if(self->capture_operation_.phase!=CapturePhase::Idle && !self->capture_driving_ && action!=ParticleCopyAction::Read) return false;
    if(self->historical_restore_ && self->historical_restore_->preparing && !self->restore_preparation_driving_ && action!=ParticleCopyAction::Read) return false;
    *command_pending = self->particle_command_pending_.load(std::memory_order_acquire);
    if (*command_pending) return action == ParticleCopyAction::Read;
    if (copy) *output = copy->witness();
    if (!corrected && output->phase == Sc6ReplayParticleCopy::Phase::Released)
    {
        self->particle_birth_ = {};
        self->particle_birth_render_ = {};
        self->particle_birth_render_count_ = 0;
        self->particle_birth_budget_ = 0;
    }
    if (action == ParticleCopyAction::Read) return !command_failed.load();
    const bool retire_only = action == ParticleCopyAction::RetireInFlight || action == ParticleCopyAction::RetireCaptures
        || (action==ParticleCopyAction::Finish && ((self->capture_driving_ && (self->capture_operation_.phase==CapturePhase::Retiring || self->capture_operation_.phase==CapturePhase::Preparing))
            || (self->historical_restore_ && self->historical_restore_->preparing && self->historical_restore_->preparation_retiring)));
    const bool completed_seek_retirement=action==ParticleCopyAction::RetireCaptures
        && self->seek_retirement_pending_ && !self->engine_post_deferred_
        && self->application_phase_==ApplicationPhase::Idle && self->engine_idle() && self->world_idle();
    if (self->surface_event_ || (!completed_seek_retirement && (self->interior_phase_ != InteriorPhase::Holding
            && !(retire_only && self->interior_phase_ == InteriorPhase::Failed))
        ) || (!completed_seek_retirement && self->pause_boundary_ != PauseBoundary::CompletedApplication)
        || self->application_phase_ != ApplicationPhase::Idle || !self->engine_idle()
        || !self->world_idle() || (!retire_only && !self->CheckBinding())) return false;
    if (action == ParticleCopyAction::Begin || action == ParticleCopyAction::BeginWithoutReadbacks)
    {
        if (copy) return false;
        if(self->simulation_->continuation().tick==0 && self->replay_rendering_->constructed()==0
            && !self->replay_rendering_->BindInitialViewState(self->world_)) {
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] initial replay view binding rejected before_capture=true\n"));
            return false;
        }
        const auto available = self->AdmissionRemaining();
        if (sizeof(Sc6ReplayParticleCopy) > available) return false;
        self->particle_capture_budget_ = (std::min)(self->memory_limit_ / 2, available);
        try { copy = std::make_unique<Sc6ReplayParticleCopy>(); }
        catch (...) { return false; }
        if(corrected) {
            std::array<unsigned,64> ids{};std::size_t count{};
            if(!self->HistoricalExecutionAdmitted()
                || !self->replay_rendering_->ReadQuarantinedPrimitives(ids,count)
                || !copy->SetCaptureQuarantine(std::span<const unsigned>(ids.data(),count)))return false;
        }
    }
    if (!copy) return false;
    if(action==ParticleCopyAction::DrainPrivateOwnerRetirement) {
        const auto* recovery=self->historical_restore_.get();
        const bool unexecuted_recovery=recovery && recovery->execution && recovery->undo.valid
            && recovery->witness.phase==RestoreOperationPhase::Recovered
            && recovery->execution->ownership.phase()==ReplaySeekOwnership::Phase::Recovering
            && !copy->execution_started() && !copy->witness().native_write_uncommitted
            && copy->witness().phase==Sc6ReplayParticleCopy::Phase::Recovered
            && self->simulation_->continuation().tick==recovery->witness.original_tick
            && !recovery->game_dirty && !recovery->source_dirty && !recovery->execution_dirty
            && !recovery->surface_dirty && !recovery->rendering_dirty;
        if(self->interior_phase_!=InteriorPhase::Holding || self->surface_event_
            || self->particle_command_pending_.load()
            || (self->checkpoint_restoring_ && !unexecuted_recovery))return false;
    }
    if(action==ParticleCopyAction::CompleteParticleRenderOwners || action==ParticleCopyAction::PrepareExecutionCoordinates || action==ParticleCopyAction::BeginExecution
        || action==ParticleCopyAction::DrainExecutionWork || action==ParticleCopyAction::SettleExecution
        || action==ParticleCopyAction::ContinueExecutionForUndo) {
        if(!self->historical_restore_ || !self->historical_restore_->execution || !self->checkpoint_restoring_) return false;
        const auto phase=self->historical_restore_->execution->ownership.phase();
        if(action==ParticleCopyAction::CompleteParticleRenderOwners || action==ParticleCopyAction::PrepareExecutionCoordinates || action==ParticleCopyAction::BeginExecution) {
            if(phase!=ReplaySeekOwnership::Phase::APublished) return false;
        } else if(phase!=ReplaySeekOwnership::Phase::RecoveryQuiescing
            && phase!=ReplaySeekOwnership::Phase::CompletingTargetTails && phase!=ReplaySeekOwnership::Phase::CompletingResumedTails) return false;
        self->particle_capture_budget_=copy->witness().bytes+self->AdmissionRemaining();
    }
    if(action==ParticleCopyAction::CompleteNativeReconstruction) {
        if(!self->historical_restore_ || !self->historical_restore_->execution || !self->checkpoint_restoring_
            || self->historical_restore_->witness.phase!=RestoreOperationPhase::Recovered
            || self->historical_restore_->execution->ownership.phase()!=ReplaySeekOwnership::Phase::Recovering
            || !copy->needs_cpu_reconstruction())return false;
        self->particle_capture_budget_=copy->witness().bytes+self->AdmissionRemaining();
    }
    if (action == ParticleCopyAction::ReopenCaptureAtB) {
        const bool requested=self->historical_restore_ && self->historical_restore_->preparing;
        const auto phase=copy->witness().phase;
        if(self->checkpoint_restoring_ || (self->historical_restore_ && !requested)
            || (phase!=Sc6ReplayParticleCopy::Phase::ReadyA && !(requested && phase==Sc6ReplayParticleCopy::Phase::Released))
            || !(requested?self->historical_restore_->target->gpu:copy->captured_image())) return false;
    }
    if (action == ParticleCopyAction::SealCapture)
    {
        auto& identity = self->particle_capture_identity_;
        identity = {};
        identity.session = self->checkpoint_session_;
        identity.tick = self->simulation_->continuation().tick;
        identity.interval = self->simulation_->continuation().interval;
        identity.execution_phase = static_cast<std::uint8_t>(self->simulation_->continuation().phase);
        identity.epoch = EngineField<std::uint64_t>(reinterpret_cast<void*>(self->image_base_), 0x4197170);
        if(!self->input_source_) return false;
        identity.source_revision=self->input_source_->revision_id();
        if (!self->ReadCheckpointSource(identity.source).ok() || !self->companion_storage_) return false;
        self->particle_capture_budget_ = self->AdmissionRemaining();
        if (!self->particle_capture_budget_) return false;
    }
    if (action == ParticleCopyAction::InstallState || action == ParticleCopyAction::CommitState
        || action == ParticleCopyAction::CompleteRetirement)
    {
        if (!self->historical_restore_ || !self->checkpoint_restoring_) return false;
        const auto& transaction = *self->historical_restore_;
        if (action == ParticleCopyAction::InstallState
            ? transaction.witness.phase != RestoreOperationPhase::Publishing
            : transaction.witness.phase != RestoreOperationPhase::Committing) return false;
        if (action == ParticleCopyAction::CompleteRetirement && !transaction.native_retired) return false;
    }
    if (action == ParticleCopyAction::PrepareBirthRegistration
        && (!self->particle_birth_.ValidateCurrentB().ok() || self->particle_birth_.empty())) return false;
    if (action == ParticleCopyAction::PrepareUndo)
    {
        self->particle_publication_deadline_ = ReplayGpuCompletion::Clock::now() + std::chrono::milliseconds(500);
        self->particle_capture_budget_ = (std::min)(self->memory_limit_ / 2,
            self->AdmissionRemaining() + copy->witness().bytes);
    }
    if(action==ParticleCopyAction::PrepareRenderTarget) {
        if(!self->historical_restore_ || !self->historical_restore_->preparing
            || !self->historical_restore_->undo.valid || self->checkpoint_restoring_) return false;
        self->particle_capture_budget_=(std::min)(self->memory_limit_/2,
            self->AdmissionRemaining()+copy->witness().bytes);
    }
    if (action == ParticleCopyAction::ReopenCaptureAtB)
    {
        // Admission reads game-thread codec/capture ownership. The render
        // command consumes this allowance; it must not recalculate a partial
        // ledger from a different thread's thread-local scratch.
        if (sizeof(Sc6ReplayParticleCopy) > self->AdmissionRemaining()) return false;
        const auto state=copy->witness();
        const auto credit=state.phase==Sc6ReplayParticleCopy::Phase::Released?0:
            state.bytes-copy->shared_capture_bytes();
        self->particle_capture_budget_ = (std::min)(self->memory_limit_ / 2,
            self->AdmissionRemaining() + credit);
    }
    self->particle_command_corrected_=corrected;
    self->particle_copy_action_ = action;
    command_failed.store(false);
    self->particle_command_pending_.store(true, std::memory_order_release);
    if (!self->QueueSurface(SurfaceCommand::ParticleCopy))
    { self->particle_command_pending_.store(false); return false; }
    *command_pending = true;
    return true;
}

bool Sc6ReplayHost::BindParticleBirth(const Sc6ReplayVfxState::ParticleBirth& birth, std::size_t budget) noexcept
{
    return BindParticleBirths(Sc6ReplayVfxState::ParticleBirthSet::Single(birth), budget);
}
bool Sc6ReplayHost::BindParticleBirths(const Sc6ReplayVfxState::ParticleBirthSet& births, std::size_t budget) noexcept
{
    auto* self = active_;
    if (!self || GetCurrentThreadId() != self->thread_ || self->depth_ || self->surface_event_
        || self->particle_command_pending_.load(std::memory_order_acquire) || !self->particle_copy_
        || self->interior_phase_ != InteriorPhase::Holding
        || self->pause_boundary_ != PauseBoundary::CompletedApplication
        || self->application_phase_ != ApplicationPhase::Idle || !self->engine_idle()
        || !self->world_idle() || !self->CheckBinding() || !self->particle_birth_.empty() || births.empty()
        || budget > self->memory_limit_) return false;
    const auto witness = self->particle_copy_->witness();
    if (witness.pending || witness.phase != Sc6ReplayParticleCopy::Phase::UndoReady) return false;
    std::array<Sc6ReplayVfxState::ParticleBirth::RenderBinding, Sc6ReplayVfxState::ParticleBirthSet::capacity> bindings{};
    std::size_t count{};
    if (!births.ReadRenderBindings(bindings, count).ok()) return false;
    for (std::size_t i = 0; i < count; ++i)
        if (bindings[i].base != self->image_base_ || bindings[i].system != witness.system || bindings[i].pool != witness.pool) return false;
    self->particle_birth_ = births;
    self->particle_birth_render_ = bindings;
    self->particle_birth_render_count_ = count;
    self->particle_birth_budget_ = budget;
    return true;
}

void Sc6ReplayHost::QueueParticleCopyCommand()
{
    // Same named render queue as 1414EC8E0's render/cleanup task. This marker
    // follows its submitted particle passes; ExecuteInner preserves pending
    // sublist/dispatch/execution prerequisites. No global deletion drain.
    auto* list = EngineNative<void*>(image_base_, 0x15edaf0);
    if (!EngineField<bool>(reinterpret_cast<void*>(image_base_), 0x434459e)
        && (particle_copy_action_ == ParticleCopyAction::PrepareExecutionCoordinates
            || particle_copy_action_ == ParticleCopyAction::RestoreUndo
            || particle_copy_action_ == ParticleCopyAction::CommitState)) {
        // These actions can call the native coordinate uploader, whose RHI
        // buffer lock flushes the immediate list. ExecuteInner retains its
        // list head until callbacks return: placing our callback in that list
        // lets the upload flush re-enter it. Drain preceding native commands
        // first, then run from the enclosing render task with no queued node.
        // The action's GPU completion query still owns transfer retirement.
        EngineNative(image_base_, 0x15db1e0, reinterpret_cast<void*>(image_base_ + 0x4344cc8), list);
        std::uintptr_t command[3]{0, 0, reinterpret_cast<std::uintptr_t>(this)};
        ExecuteParticleCopyCommand(list, command);
        return;
    }
    auto top = (EngineField<std::uintptr_t>(list, 0x30) + 7) & ~std::uintptr_t{7};
    if (EngineField<std::uintptr_t>(list, 0x38) < top + 24)
    {
        EngineNative(image_base_, 0xdbfbc0, static_cast<std::byte*>(list) + 0x30, 32);
        top = (EngineField<std::uintptr_t>(list, 0x30) + 7) & ~std::uintptr_t{7};
    }
    EngineField<std::uintptr_t>(list, 0x30) = top + 24;
    auto* command = reinterpret_cast<std::uintptr_t*>(top);
    command[0] = 0;
    command[1] = reinterpret_cast<std::uintptr_t>(&ExecuteParticleCopyCommand);
    command[2] = reinterpret_cast<std::uintptr_t>(this);
    ++EngineField<int>(list, 0x14);
    *EngineField<std::uintptr_t*>(list, 8) = top;
    EngineField<std::uintptr_t>(list, 8) = top;
    EngineNative(image_base_, 0x15db1e0, reinterpret_cast<void*>(image_base_ + 0x4344cc8), list);
}

bool Sc6ReplayHost::ReopenCapturedImageAtB() noexcept
{
    try {
        auto image = historical_restore_ && historical_restore_->preparing
            ? historical_restore_->target->gpu : particle_copy_->captured_image();
        const auto* identity = Sc6ReplayParticleCopy::identity(image);
        if (!identity || identity->session != checkpoint_session_
            || identity->epoch >= interior_epoch_ || identity->tick > interior_target_
            || particle_copy_->witness().pending) return false;
        // Construct only an empty replacement before releasing the old owner.
        // A survives solely through the immutable handle and CPU checkpoint.
        auto replacement = std::make_unique<Sc6ReplayParticleCopy>();
        if (!particle_copy_->Finish()) return false;
        particle_copy_.swap(replacement);
        replacement.reset();
        particle_birth_ = {};
        particle_birth_render_ = {};
        particle_birth_render_count_ = 0;
        particle_birth_budget_ = 0;
        return particle_copy_->BeginFromCapture(image_base_, world_, image, particle_capture_budget_, replay_rendering_->HeldViewState())
            && particle_copy_->VerifyAtB(world_, false);
    } catch (...) { return false; }
}

void Sc6ReplayHost::ExecuteParticleCopyCommand(void*, void* command) noexcept
{
    auto* self = EngineField<Sc6ReplayHost*>(command, 16);
    auto* copy=self->particle_command_corrected_?self->corrected_particle_copy_.get():self->particle_copy_.get();
    bool success = true;
    switch (self->particle_copy_action_)
    {
    // Bounded transaction: cap GPU/surface ownership at half the total.
    // The legacy checkpoint budget reserves 224 MiB for retired batch tables;
    // those tables are not allocated by this executor. Historical preparation
    // separately accounts every A/B/CPU/render participant against the total.
    case ParticleCopyAction::Begin:
    case ParticleCopyAction::BeginWithoutReadbacks:
        success = copy->Begin(self->image_base_, self->world_, self->particle_capture_budget_,
            self->replay_rendering_->HeldViewState(), self->particle_copy_action_ != ParticleCopyAction::BeginWithoutReadbacks);
        break;
    case ParticleCopyAction::VerifyAtB: success = copy->VerifyAtB(self->world_); break;
    case ParticleCopyAction::VerifyState: success = copy->VerifyAtB(self->world_, false); break;
    case ParticleCopyAction::Poll: copy->Poll(); break;
    case ParticleCopyAction::RetireInFlight: copy->RetireInFlight(); break;
    case ParticleCopyAction::SealCapture:
        success = copy->SealCapture(self->particle_capture_identity_, self->particle_capture_budget_); break;
    case ParticleCopyAction::RetireCaptures: success = Sc6ReplayParticleCopy::RetireCapturedImages(); break;
    case ParticleCopyAction::ReopenCaptureAtB: success = self->ReopenCapturedImageAtB(); break;
    case ParticleCopyAction::Cancel: copy->Cancel(); break;
    case ParticleCopyAction::Finish:
        if(self->historical_restore_ && !self->MaterialCopyTransitionAllowed())break;
        success = copy->Finish(); break;
    // State installation/commit is admitted only by the enclosing historical
    // transaction. The texture-only diagnostic still requires B recovery.
    case ParticleCopyAction::PrepareUndo:
        success = copy->PrepareUndoAtB(self->world_, self->particle_capture_budget_, self->particle_publication_deadline_,
            self->historical_restore_ && self->historical_restore_->preparing);
        break;
    case ParticleCopyAction::PrepareRenderTarget:
        success = self->historical_restore_ && self->historical_restore_->undo.valid
            && copy->PrepareTargetAtB(self->world_,self->particle_capture_budget_);
        break;
    case ParticleCopyAction::InstallCaptured:
        if(self->historical_restore_ && !self->MaterialCopyTransitionAllowed())break;
        success = copy->InstallCaptured(self->world_); break;
    case ParticleCopyAction::InstallState:
        if(!self->MaterialCopyTransitionAllowed())break;
        success = copy->InstallCaptured(self->world_, true); break;
    case ParticleCopyAction::PrepareExecutionCoordinates:
        success=copy->PrepareExecutionCoordinates();break;
    case ParticleCopyAction::BeginExecution:
        success=copy->BeginExecution(self->particle_capture_budget_,1024*1024);
        if(copy->execution_started())
            self->historical_restore_->execution->render=HistoricalRestore::Execution::Render::HandedOff;
        break;
    case ParticleCopyAction::CompleteParticleRenderOwners:
        success=copy->CompleteParticleRenderOwners();break;
    case ParticleCopyAction::DrainExecutionWork:
        success=copy->DrainExecutionWork();break;
    case ParticleCopyAction::DrainPrivateOwnerRetirement:
        success=copy->DrainPrivateOwnerRetirement();break;
    case ParticleCopyAction::SettleExecution:
        if(!self->MaterialCopyTransitionAllowed())break;
        success=copy->SettleExecution(self->particle_capture_budget_,
            (self->historical_restore_->execution->ownership.phase()==ReplaySeekOwnership::Phase::CompletingTargetTails
                || self->historical_restore_->execution->ownership.phase()==ReplaySeekOwnership::Phase::CompletingResumedTails)
                ?RestoreSettlement::CommitCurrent:RestoreSettlement::RecoverOriginal);
        if(success) self->historical_restore_->execution->render=HistoricalRestore::Execution::Render::Settled;
        break;
    case ParticleCopyAction::ContinueExecutionForUndo:
        if(!self->MaterialCopyTransitionAllowed())break;
        success=self->historical_restore_->execution->ownership.phase()==ReplaySeekOwnership::Phase::RecoveryQuiescing
            && copy->ContinueExecutionForUndo();
        if(success) self->historical_restore_->execution->render=HistoricalRestore::Execution::Render::HandedOff;
        break;
    case ParticleCopyAction::CommitState:
        if(!self->MaterialCopyTransitionAllowed())break;
        success = self->historical_restore_ && copy->CommitInstalled(self->historical_restore_->witness.particle_birth); break;
    case ParticleCopyAction::CompleteRetirement:
        if(!self->MaterialCopyTransitionAllowed())break;
        success = copy->CompleteNativeRetirement(); break;
    case ParticleCopyAction::CompleteNativeReconstruction:
        if(!self->MaterialCopyTransitionAllowed())break;
        success=copy->CompleteNativeReconstruction(self->particle_capture_budget_);break;
    case ParticleCopyAction::RestoreUndo:
        if(!self->MaterialCopyTransitionAllowed())break;
        success = copy->RestoreUndo(self->world_); break;
    case ParticleCopyAction::PrepareBirthRegistration:
        success = copy->PrepareBirthRegistration({self->particle_birth_render_.data(), self->particle_birth_render_count_}, self->particle_birth_budget_);
        break;
    case ParticleCopyAction::CheckUncommittedRelease:
        success = copy->witness().native_write_uncommitted
            && copy->blocks_resume() && !copy->Finish();
        break;
    default: success = false; break;
    }
    if (self->particle_copy_action_ != ParticleCopyAction::Poll)
        RC::Output::send<RC::LogLevel::Default>(STR(
            "[HorseMod] particle copy command action={} thread={} rhi_threaded={} gpu_pending={} success={} material_mutation_deferred={}\n"),
            static_cast<unsigned>(self->particle_copy_action_), GetCurrentThreadId(),
            EngineField<bool>(reinterpret_cast<void*>(self->image_base_), 0x434459e),
            copy->witness().pending, success,
            self->historical_restore_ && self->historical_restore_->material_command_deferred.load(std::memory_order_acquire));
    (self->particle_command_corrected_?self->corrected_particle_command_failed_:self->particle_command_failed_).store(!success);
    if(!success && self->particle_copy_action_==ParticleCopyAction::PrepareExecutionCoordinates) {
        const auto& observed=copy->witness();
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] execution coordinate failure owner={:x} check={} phase={} pending={} installed_complete={} B_retained=true\n"),
            observed.reconstruction_owner,observed.reconstruction_issue,static_cast<unsigned>(observed.phase),observed.pending,observed.installed_copy_complete);
    }
    self->particle_command_pending_.store(false, std::memory_order_release);
}
