// One enclosing transaction owns all displaced B graphs. Native application
// work stays held until completion or verified B recovery; timeout never
// releases queued render work or partially installed storage.
namespace {
Status DiagnosePhysicsInstallation(const Sc6ReplayWorldState::PhysicsBoundary& desired,
    const Sc6ReplayWorldState::PhysicsBoundary& observed, const wchar_t* direction, bool owned_render_work = false)
{
    Sc6ReplayWorldState::PhysicsProjectionFailure failure{};
    auto status = Sc6ReplayWorldState::PreparePhysicsProjection(desired, observed, &failure, owned_render_work);
    if (!status.ok()) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical physics installation direction={} check={} scene={} actor={}\n"),
            direction, RC::to_generic_string(failure.check ? failure.check : "unknown"), failure.scene, failure.actor);
        if (!desired.valid_counts() || !observed.valid_counts()
            || failure.scene >= desired.actors.size() || failure.actor >= desired.actors[failure.scene].size()) return status;
        const auto& a = desired.actors[failure.scene][failure.actor];
        const auto& b = observed.actors[failure.scene][failure.actor];
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical physics installation flags desired={:x}/{:x} observed={:x}/{:x}\n"),
            a.component_flags, a.component_transform_flags, b.component_flags, b.component_transform_flags);
        const auto bytes = [&](const char* kind, const auto& x, const auto& y) {
            unsigned shown{};
            const auto* ax = reinterpret_cast<const unsigned char*>(x.data());
            const auto* bx = reinterpret_cast<const unsigned char*>(y.data());
            for (std::size_t i = 0; i < sizeof(x) && shown < 16; ++i) if (ax[i] != bx[i]) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical physics installation diff kind={} offset={:x} desired={:x} observed={:x}\n"),
                    RC::to_generic_string(kind), i, ax[i], bx[i]);
                ++shown;
            }
        };
        if (!failure.check) return status;
        if (std::string_view(failure.check) == "actor_unaudited_field") bytes("actor", a.dynamic_storage, b.dynamic_storage);
        if (std::string_view(failure.check) == "actor_property_changed") bytes("properties", a.properties, b.properties);
        if (std::string_view(failure.check) == "body_sim_changed") bytes("body_sim", a.simulation_storage, b.simulation_storage);
        if (std::string_view(failure.check) == "shape_inputs_changed") bytes("shapes", a.query_shapes, b.query_shapes);
    }
    return status;
}
}
#include "Sc6ReplayHost.ParticleOwners.inl"

bool Sc6ReplayHost::CheckHistoricalMaterialBoundary(bool c_only_retirement) noexcept
{
    if(!historical_restore_)return false;
    auto& transaction=*historical_restore_;
    if(transaction.material_failed)return false;
    const auto material=c_only_retirement
        ? NativeReplayMaterialTaskGuard::InspectCOnlyRetirement(transaction.material)
        : NativeReplayMaterialTaskGuard::Inspect(transaction.material);
    if(material.state==NativeReplayMaterialTaskGuard::State::Clear)return true;
    auto& operation=transaction.witness;
    const auto now=GetTickCount64();
    if(!transaction.material_wait_started)transaction.material_wait_started=now;
    if(material.state==NativeReplayMaterialTaskGuard::State::CpuPending
        && now-transaction.material_wait_started<5000) {
        operation.pending=true;return false;
    }
    // CPU zero cannot cover a raw producer borrow or complete observed material
    // work. Missing producer ownership is a terminal veto, even at zero. A timeout
    // also fails the session: it neither cancels tasks nor permits owner release.
    // Existing seek/qualification failure handling retains the graph for owned
    // process termination. Menu exit/recovery cannot resume this partial state.
    transaction.material_failed=true;
    operation.failure=FailureCode::UnsupportedContent;
    operation.participant=material.state==NativeReplayMaterialTaskGuard::State::CoverageLost
        ? "material_cpu_coverage_lost" : material.state==NativeReplayMaterialTaskGuard::State::ProducerUncovered
        ? "material_C_only_producer_uncovered" : material.state==NativeReplayMaterialTaskGuard::State::CpuPending
        ? "material_cpu_timeout" : "material_resource_completion_unsupported";
    operation.phase=RestoreOperationPhase::Failed;operation.pending=false;
    operation.original_recovered=false;
    if(transaction.execution) {transaction.execution->ownership.Fail();PublishHistoricalOwnership();}
    // A provisional corrected capture shares this unresolved native lifetime.
    // Publish failure without resetting its checkpoint/copy or routing it
    // through ordinary cancellation/Finish. Complete B and all C owners stay
    // pinned even after both CPU callbacks return.
    if(corrected_capture_) {
        capture_operation_.failure=FailureCode::UnsupportedContent;
        capture_operation_.phase=CapturePhase::Failed;capture_operation_.pending=false;
    }
    if(seek_.witness.phase!=SeekPhase::Idle) {
        seek_.witness.failure=FailureCode::UnsupportedContent;
        seek_.witness.phase=SeekPhase::Failed;seek_.witness.pending=false;
        seek_.witness.original_recovered=false;
        seek_.witness.release_result=SeekReleaseResult::Rejected;
    }
    interior_phase_=InteriorPhase::Failed;
    RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] material boundary failed state={} builders={} tasks={} owners_retained=true recovery=false resource_completion_proven=false session_termination_required=true\n"),
        static_cast<unsigned>(material.state),material.builders,material.tasks);
    return false;
}

bool Sc6ReplayHost::MaterialCopyTransitionAllowed() noexcept
{
    if(!historical_restore_)return false;
    const bool allowed=!historical_restore_->material_failed
        && NativeReplayMaterialTaskGuard::Inspect(historical_restore_->material).state
        ==NativeReplayMaterialTaskGuard::State::Clear;
    // Render command completion is distinct from mutation completion. The host
    // retries the boundary or publishes its explicit failed-session outcome.
    historical_restore_->material_command_deferred.store(!allowed,std::memory_order_release);
    return allowed;
}

Status Sc6ReplayHost::RequestHistoricalRestore(const Checkpoint& target)
{
    if(!NativeReplayMaterialTaskGuard::HistoricalRestoreSupported())
        return Status::failure(FailureCode::UnsupportedContent);
    if(index_.witness().phase==ReplayTickIndex::Phase::Recording
        || index_.witness().phase==ReplayTickIndex::Phase::Releasing)
        return Status::failure(FailureCode::IllegalTransition);
    if(capture_operation_.phase!=CapturePhase::Idle || historical_restore_ || checkpoint_restoring_ || depth_
        || !CheckBinding() || interior_phase_!=InteriorPhase::Holding || surface_event_
        || particle_command_pending_.load() || pause_boundary_!=PauseBoundary::CompletedApplication
        || application_phase_!=ApplicationPhase::Idle || !engine_idle() || !executor_.idle()
        || !executor_.arena_empty() || !simulation_->interval_complete()
        || !target.valid || target.session!=checkpoint_session_ || !target.gpu || !RestoreCheckpointDisplay(target)
        || !target.hud || !target.traces || !target.vfx_handler || target.gameplay.bytes.empty()
        || target.boundary!=PauseBoundary::CompletedApplication || target.task || target.event
        || target.execution.phase!=Sc6ReplayExecutor::Phase::Idle
        || target.execution.tick>simulation_->continuation().tick || !particle_copy_)
        return Status::failure(FailureCode::RestorePreflightFailed);
    const auto phase=particle_copy_->witness().phase;
    if(phase!=Sc6ReplayParticleCopy::Phase::ReadyA && phase!=Sc6ReplayParticleCopy::Phase::Released)
        return Status::failure(FailureCode::IllegalTransition);
    const auto* identity=Sc6ReplayParticleCopy::identity(target.gpu);
    if(!identity || identity->session!=target.session || identity->tick!=target.execution.tick
        || identity->epoch!=target.epoch || identity->interval!=target.execution.interval
        || identity->execution_phase!=static_cast<std::uint8_t>(target.execution.phase)
        || identity->source!=target.source || identity->source_revision!=(target.input_revision?target.input_revision->id:0))
        return Status::failure(FailureCode::IdentityMismatch);
    auto pinned=target.weak_from_this().lock();
    if(!pinned || pinned.get()!=&target) return Status::failure(FailureCode::RestorePreflightFailed);
    if(sizeof(HistoricalRestore)>AdmissionRemaining()) return Status::failure(FailureCode::CapacityExceeded);
    auto operation=std::make_unique<HistoricalRestore>();
    if(!operation->trace_start_veto.Block()
        || NativeReplayMaterialTaskGuard::Inspect(operation->material).state!=NativeReplayMaterialTaskGuard::State::Clear)
        return Status::failure(FailureCode::UnsupportedContent);
    operation->target=std::move(pinned);operation->preparing=true;
    operation->witness.phase=RestoreOperationPhase::Preparing;
    operation->witness.pending=true;operation->witness.target_tick=target.execution.tick;
    operation->witness.original_tick=simulation_->continuation().tick;
    historical_restore_=std::move(operation);
    return Status::success();
}

void Sc6ReplayHost::AdvanceRestorePreparation() noexcept
{
    auto& transaction=*historical_restore_;
    auto& operation=transaction.witness;
    if(surface_event_ || particle_command_pending_.load()) return;
    struct Driving {bool& value;Driving(bool& v):value(v){value=true;}~Driving(){value=false;}} driving(restore_preparation_driving_);
    const auto fail=[&](FailureCode code,const char* participant) {
        if(operation.failure==FailureCode::None) {
            operation.failure=code;operation.participant=participant;
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] restore preparation failed participant={} code={} target={} original={} before_publication=true\n"),
                RC::to_generic_string(participant),static_cast<unsigned>(code),operation.target_tick,operation.original_tick);
        }
        transaction.preparation_retiring=true;operation.pending=true;
    };
    const auto queue=[&](ParticleCopyAction action) {
        Sc6ReplayParticleCopy::Witness state{};bool pending{};
        if(!ParticleCopyExperiment(action,&state,&pending)) fail(FailureCode::PresentationFailed,"image_preparation");
    };
    if(!transaction.preparation_retiring && (!CheckBinding() || interior_phase_!=InteriorPhase::Holding
        || simulation_->continuation().tick!=operation.original_tick || particle_command_failed_.load()))
        fail(FailureCode::GenerationMismatch,"preparation_domain");
    // Accepted cancellation stops new acquisition. Submitted render work and
    // prepared backing still own resources and must complete retirement;
    // cancellation is never permission to discard an in-flight operation.
    if(!transaction.preparation_retiring && transaction.cancel)
        fail(FailureCode::Cancelled,"preparation_cancelled");
    const auto copy=particle_copy_->witness();
    if(transaction.preparation_retiring) {
        if(copy.pending) {
            // Cleanup's own retirement fence must complete successfully.
            // RetireInFlight cancels the request and permanently destroys that
            // completion receipt, even after the GPU has actually signaled.
            queue(copy.phase==Sc6ReplayParticleCopy::Phase::ReadPrivateOwnerRetirement
                ?ParticleCopyAction::Poll:ParticleCopyAction::RetireInFlight);
            return;
        }
        bool owners_pending{};
        const auto owners=RetirePreparedFreshParticles(owners_pending);
        if(!owners.ok()) {operation.failure=owners.code;operation.participant="preparation_particle_retirement";return;}
        if(owners_pending)return;
        if(copy.phase!=Sc6ReplayParticleCopy::Phase::Released) {queue(ParticleCopyAction::Finish);return;}
        // Preparation has not published A. Retire prepared wind backing using
        // its own phase-aware cleanup; never free it while an undo is pending.
        if(checkpoint_capture_ && checkpoint_capture_->PendingEnclosingWind()) {
            auto status=checkpoint_capture_->UndoEnclosingWind();
            if(status.ok()) status=checkpoint_capture_->FinishEnclosingWind();
            if(!status.ok()) {operation.failure=status.code;operation.participant="preparation_wind_retirement";return;}
        }
        // B bodies may already have left native scene membership before the
        // complete core image is acquired. Render retirement alone cannot
        // release that undo owner or report preparation cancellation complete.
        const auto ground=transaction.ground.RecoverBodies();
        if(ground!=Sc6ReplayGroundDebrisState::Progress::Complete) {
            if(ground!=Sc6ReplayGroundDebrisState::Progress::Pending) {
                operation.failure=FailureCode::GenerationMismatch;
                operation.participant=transaction.ground.failed_check();
            }
            return;
        }
        const auto ground_release=transaction.ground.ReleaseRecoveredOwners();
        if(!ground_release.ok()) {operation.failure=ground_release.code;operation.participant="preparation_ground_retirement";return;}
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] restore preparation recovery completed original={} observed={} ground_released=true render_released=true before_A_publication=true\n"),
            operation.original_tick,simulation_->continuation().tick);
        operation.phase=RestoreOperationPhase::Failed;operation.pending=false;
        transaction.preparing=false;return;
    }
    operation.owned_bytes=AdmissionBytes();
    if(operation.owned_bytes>memory_limit_) {fail(FailureCode::CapacityExceeded,"preparation_budget");return;}
    if(copy.pending) {queue(ParticleCopyAction::Poll);return;}
    // Reopening the selected image can release a caller-discarded, obsolete
    // checkpoint. Its deleter queues native references; account and retire
    // those on the render owner before allocating complete B storage.
    if(Sc6ReplayParticleCopy::capture_retirement_pending()) {
        queue(ParticleCopyAction::RetireCaptures);return;
    }
    if(copy.phase==Sc6ReplayParticleCopy::Phase::ReadyA || copy.phase==Sc6ReplayParticleCopy::Phase::Released) {
        queue(ParticleCopyAction::ReopenCaptureAtB);return;
    }
    if(copy.phase==Sc6ReplayParticleCopy::Phase::ReadyB) {queue(ParticleCopyAction::PrepareUndo);return;}
    if(copy.phase!=Sc6ReplayParticleCopy::Phase::UndoReady) {fail(FailureCode::PresentationFailed,"preparation_phase");return;}
    try {
        // Private B lifecycle is retained before its bodies leave membership.
        // The core B snapshot then owns the remaining scene; ground owns the
        // original excluded NpActors and their unchanged root/controller state.
        // This acquisition is reversible even when complete_B capture fails.
        if(!transaction.ground.prepared()) {
            auto status=transaction.ground.Prepare(image_base_,manager_,world_,0,AdmissionRemaining());
            if(status.ok())status=transaction.ground.PreparePhysics(AdmissionRemaining()+transaction.ground.owned_bytes());
            if(!status.ok()) {fail(status.code,transaction.ground.failed_check());return;}
        }
        const auto removed=transaction.ground.AdvanceRemoval();
        if(removed!=Sc6ReplayGroundDebrisState::Progress::Complete) {
            if(removed!=Sc6ReplayGroundDebrisState::Progress::Pending)
                fail(FailureCode::RestorePreflightFailed,transaction.ground.failed_check());
            return;
        }
        // Complete B's CPU/world/source ownership only after its GPU copy has
        // completed, and before any target-owner preparation. Rematerializing
        // an expired A owner must never begin with an incomplete undo image.
        if(!transaction.undo.valid) {
            const auto captured=CaptureCheckpointUnchecked(transaction.undo);
            if(!captured.ok()) {fail(captured.code,"complete_B");return;}
            if(transaction.undo.execution.tick!=operation.original_tick) {fail(FailureCode::GenerationMismatch,"complete_B_coordinate");return;}
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] restore undo captured target={} original={} gpu_complete=true before_target_preparation=true owned_bytes={}\n"),
                operation.target_tick,operation.original_tick,AdmissionBytes());
        }
        if(!copy.target_prepared) {
            const auto owners=PrepareFreshParticleOwners();
            if(!owners.ok()) {fail(owners.code,"fresh_particle_owners");return;}
            if(!transaction.particle_inventory_ready)return; // Constructor work/GPU completion still owns preparation.
            if(!transaction.particle_bindings.empty()
                && !particle_copy_->PrepareParticleRenderOwners(transaction.target->vfx,transaction.particle_bindings,
                    AdmissionRemaining()+particle_copy_->witness().bytes)) {
                fail(FailureCode::GenerationMismatch,"fresh_particle_render_binding");return;
            }
            if(!particle_copy_->PrepareGroundRenderOwner(transaction.ground)) {
                fail(FailureCode::GenerationMismatch,"ground_render_membership");return;
            }
            if(!particle_copy_->PrepareTraceRenderOwner(*transaction.undo.traces)) {
                fail(FailureCode::GenerationMismatch,"trace_render_membership");return;
            }
            if(!transaction.fresh_trace_children.empty()) {
                const Sc6ReplayParticleCopy::TraceRenderOwnerProof owners{&transaction,transaction.fresh_trace_children.size(),
                    [](const void* context,std::size_t i,Sc6ReplayVfxState::ReconstructionBinding& output,bool preparation) {
                        const auto& owner=*static_cast<const HistoricalRestore*>(context);
                        if(!owner.target || !owner.target->traces || i>=owner.fresh_trace_children.size() || !owner.fresh_trace_children[i])
                            return Status::failure(FailureCode::GenerationMismatch);
                        const auto& child=*owner.fresh_trace_children[i];
                        return preparation?owner.target->traces->FreshChildMeshBinding(child,output)
                            :owner.target->traces->FreshChildRenderBinding(child,output);
                    },[](const void* context,std::size_t i,unsigned& source_id,std::uintptr_t& mesh_asset) {
                        const auto& owner=*static_cast<const HistoricalRestore*>(context);
                        return owner.target && owner.target->traces && i<owner.fresh_trace_children.size()
                            && owner.fresh_trace_children[i] && owner.target->traces->FreshChildDormantRenderSource(
                                *owner.fresh_trace_children[i],source_id,mesh_asset);
                    }};
                if(!particle_copy_->PrepareFreshTraceRenderOwners(owners,AdmissionRemaining()+particle_copy_->witness().bytes)) {
                    fail(FailureCode::GenerationMismatch,"fresh_trace_render_binding");return;
                }
            }
            const auto& a=transaction.target->physics;const auto& b=transaction.undo.physics;
            if(!particle_copy_->PrepareCreationRenderOwners(
                {a.creation_owners.data(),a.creation_owner_count},{b.creation_owners.data(),b.creation_owner_count},
                AdmissionRemaining()+particle_copy_->witness().bytes)) {
                fail(FailureCode::GenerationMismatch,"creation_render_membership");return;
            }
            if(!particle_copy_->PrepareStageRenderOwners(transaction.target->world.stage_visibility(),transaction.undo.world.stage_visibility(),
                AdmissionRemaining()+particle_copy_->witness().bytes)) {
                fail(FailureCode::GenerationMismatch,"stage_render_membership");return;
            }
            queue(ParticleCopyAction::PrepareRenderTarget);return;
        }
        if(seek_.preparation_failure_checkpoint==transaction.target->execution.tick) {
            seek_.preparation_failure_checkpoint.reset();
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] seek preparation fault checkpoint={} B={} complete_B=true target_prepared=true publication=false\n"),
                transaction.target->execution.tick,transaction.undo.execution.tick);
            fail(FailureCode::RestorePreflightFailed,"injected_checkpoint_preparation");return;
        }
        const auto status=PrepareHistoricalRestore(*transaction.target);
        if(!status.ok()) {fail(status.code,operation.participant?operation.participant:"cpu_preparation");return;}
        transaction.preparing=false;operation.pending=false;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] restore request prepared target={} original={} owned_bytes={} complete_B=true\n"),
            operation.target_tick,operation.original_tick,operation.owned_bytes);
    } catch(...) {fail(FailureCode::RestorePreflightFailed,"preparation_exception");}
}

Status Sc6ReplayHost::PrepareHistoricalRestore(const Checkpoint& target)
{
    // The direct Begin path must have the same before-acquisition admission
    // contract as Request; idle counters cannot make either path safe.
    if(!NativeReplayMaterialTaskGuard::HistoricalRestoreSupported())
        return Status::failure(FailureCode::UnsupportedContent);
    auto pinned = target.weak_from_this().lock();
    if (!pinned || pinned.get() != &target) return Status::failure(FailureCode::RestorePreflightFailed);
    const bool requested=historical_restore_ && historical_restore_->preparing && historical_restore_->target.get()==&target;
    if (capture_operation_.phase!=CapturePhase::Idle || (historical_restore_ && !requested) || checkpoint_restoring_ || !CheckBinding() || depth_ || surface_event_
        || particle_command_pending_.load() || !particle_copy_
        || !particle_copy_->witness().reflection_retained
        || !particle_copy_->witness().target_prepared
        || particle_copy_->witness().phase != Sc6ReplayParticleCopy::Phase::UndoReady
        || interior_phase_ != InteriorPhase::Holding || pause_boundary_ != PauseBoundary::CompletedApplication
        || application_phase_ != ApplicationPhase::Idle || !engine_idle() || !executor_.idle()
        || !executor_.arena_empty() || !simulation_->interval_complete()
        || !target.valid || target.session != checkpoint_session_ || target.task || target.event
        || !target.gpu || target.gpu != particle_copy_->captured_image()
        || target.boundary != PauseBoundary::CompletedApplication || !RestoreCheckpointDisplay(target)
        || target.execution.phase != Sc6ReplayExecutor::Phase::Idle)
        return Status::failure(FailureCode::RestorePreflightFailed);
    const auto* source_identity=Sc6ReplayParticleCopy::identity(target.gpu);
    if(!source_identity || source_identity->source_revision!=(target.input_revision?target.input_revision->id:0))
        return Status::failure(FailureCode::GenerationMismatch);
    if (!input_source_) return Status::failure(FailureCode::ContextUnavailable);
    const auto revision_status=input_source_->Validate(target.source,target.input_revision);
    if(!revision_status.ok()) return revision_status;
    if (!callback_admission_) return Status::failure(FailureCode::ContextUnavailable);
    const auto callback_status=callback_admission_->Validate(target.callback_stamp);
    if (!callback_status.ok()) {
        const auto current_stamp=callback_admission_->stamp();const auto detail=callback_admission_->diagnostic();
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical callback domain rejected target={} original={} latent={}/{} streamable={}/{} check={} owner={:x} count={}\n"),
            target.execution.tick,simulation_->continuation().tick,target.callback_stamp[0],current_stamp[0],
            target.callback_stamp[1],current_stamp[1],detail.check,detail.owner,detail.count);
        return callback_status;
    }
    if(!requested) {
        if (sizeof(HistoricalRestore) > AdmissionRemaining()) return Status::failure(FailureCode::CapacityExceeded);
        auto operation=std::make_unique<HistoricalRestore>();
        if(!operation->trace_start_veto.Block()
            || NativeReplayMaterialTaskGuard::Inspect(operation->material).state!=NativeReplayMaterialTaskGuard::State::Clear)
            return Status::failure(FailureCode::UnsupportedContent);
        historical_restore_=std::move(operation);
    }
    auto& transaction = *historical_restore_;
    transaction.target = std::move(pinned);
    if(!CheckHistoricalMaterialBoundary())return Status::failure(FailureCode::UnsupportedContent);
    transaction.prior_mode = checkpoint_broker_->mode();
    auto& witness = transaction.witness;
    witness.target_tick = target.execution.tick;
    witness.original_tick = simulation_->continuation().tick;
    const auto step = [&](Status result, const char* participant) {
        if (!result.ok())
        {
            // Requested preparation may already own detached B bodies and GPU
            // work. Only its outer recovery owner may publish terminal failure.
            witness.phase = requested ? RestoreOperationPhase::Preparing : RestoreOperationPhase::Failed;
            witness.failure = result.code;
            witness.participant = participant;
            RC::Output::send<RC::LogLevel::Warning>(STR(
                "[HorseMod] historical restore preflight participant={} code={} target={} original={} bytes={}\n"),
                RC::to_generic_string(participant), static_cast<unsigned>(result.code),
                witness.target_tick, witness.original_tick, witness.owned_bytes);
        }
        return result;
    };
    auto status = step(requested && transaction.undo.valid ? Status::success() : CaptureCheckpointUnchecked(transaction.undo), "complete_B");
    if (!status.ok()) return status;
    const auto& current = transaction.undo;
    if(current.execution.tick!=simulation_->continuation().tick || current.epoch!=interior_epoch_)
        return step(Status::failure(FailureCode::GenerationMismatch),"complete_B_coordinate");
    Sc6ReplayResolvers source_resolvers{};
    source_resolvers.user=this;source_resolvers.image_base=image_base_;source_resolvers.replay_player=&ResolveCheckpointSource;
    const auto source_status=Sc6ReplayNativeBridge(source_resolvers).ValidatePlaybackSourceTransition(
        current.source,target.source,true,Sc6ReplayNativeBridge::SourceRestoreScope::RetainedReplay);
    if(!source_status.ok()) return step(source_status,"source_continuation");
    // Restore routing through its private A / retained B transaction. Unknown
    // registry shapes still reject before any historical publication.
    if(!target.source_registration || !current.source_registration)
        return step(Status::failure(FailureCode::UnsupportedContent),"source_registration");
    if (sizeof(Sc6ReplayWorldState::PhysicsBoundary) + sizeof(SurfaceSnapshot) + 128 > AdmissionRemaining())
        return step(Status::failure(FailureCode::CapacityExceeded), "observation_storage");
    transaction.physics_observation = std::make_unique<Sc6ReplayWorldState::PhysicsBoundary>();
    const auto display = RestoreCheckpointDisplay(target);
    if (!display || (target.rolling_display_omitted && display != current.surface))
        return step(Status::failure(FailureCode::RestorePreflightFailed), "complete_B_display");
    transaction.presented_surface = std::make_shared<SurfaceSnapshot>(*display);
    transaction.presented_surface->epoch = current.epoch;
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] input revision transaction prepared A={} B={}\n"),target.input_revision?target.input_revision->id:0,current.input_revision?current.input_revision->id:0);
    const auto limit = memory_limit_;
    if (!companion_storage_) return step(Status::failure(FailureCode::ContextUnavailable), "companion_storage_accounting");
    const auto account = [&]() {
        witness.owned_bytes = AdmissionBytes();
        return witness.owned_bytes <= limit;
    };
    const auto remaining = [&]() -> std::size_t { return account() ? limit - witness.owned_bytes : 0; };
    if (!account()) return step(Status::failure(FailureCode::CapacityExceeded), "owned_budget");
    // Logical ticks may be equal after a prior seek. Physical execution must
    // still be newer than A; all participant preflights and private B capture
    // remain necessary even when the checkpoint has the same replay tick.
    if (target.epoch >= current.epoch || target.execution.tick > current.execution.tick
        || target.gameplay.coordinate != FrameCoordinate{target.session, target.execution.tick})
        return step(Status::failure(FailureCode::GenerationMismatch), "historical_coordinates");
    status = step(checkpoint_capture_->ValidateSnapshotUcrt(target.gameplay, target.ucrt), "encoded_ucrt");
    Sc6ReplayVfxState::Topology topology{};
    const Sc6ReplayVfxState::RetainedSecondaryOwners private_ground{&transaction.ground,
        [](const void* owner,void* context,bool(*accept)(void*,std::uintptr_t)) {
            const auto& ground=*static_cast<const Sc6ReplayGroundDebrisState*>(owner);
            return ground.prepared()?ground.VisitRoots(context,accept):Status::success();
        }};
    if (status.ok()) status = step(target.vfx.PrepareTopology(current.vfx, manager_, topology, transaction.birth,private_ground,transaction.particle_bindings), "particle_topology");
    witness.particle_birth = status.ok() && topology != Sc6ReplayVfxState::Topology::Unchanged;
    witness.particle_birth_count = status.ok() ? transaction.birth.members().size() : 0;
    if (status.ok()) {
        std::array<Sc6ReplayTraceState::FreshChild*,16> children{};
        if(transaction.fresh_trace_children.size()>children.size())status=step(Status::failure(FailureCode::CapacityExceeded),"trace_child_inventory");
        else {
            for(std::size_t i=0;i<transaction.fresh_trace_children.size();++i)children[i]=transaction.fresh_trace_children[i].get();
            status = step(target.traces && current.traces ? transaction.TargetTraces().PrepareStorage(*current.traces,remaining(),transaction.traces,&transaction.birth,
                transaction.trace_survivors.get(),std::span(children).first(transaction.fresh_trace_children.size())) : Status::failure(FailureCode::UnsupportedContent), "traces");
        }
        if(!status.ok() && target.traces && current.traces) target.traces->DescribeOwnershipDelta(*current.traces);
    }
    const Sc6ReplaySchedulerState::RetainedPrimaryTicks private_trace_ticks{&transaction,
        [](const void* owner,void* context,bool(*accept)(void*,std::uintptr_t)) {
            const auto& transaction=*static_cast<const HistoricalRestore*>(owner);
            auto status=transaction.traces.VisitPrivateMeshOwners(context,accept);
            if(status.ok() && transaction.ground.prepared())status=transaction.ground.VisitRoots(context,accept);
            return status;
        }};
    Sc6ReplaySchedulerState::PreflightFailure scheduler_failure{};
    const auto& target_scheduler=transaction.TargetScheduler();
    if (status.ok()) status = step(target_scheduler.PrepareRestore(current.scheduler, image_base_, world_,
        remaining(), transaction.scheduler, &scheduler_failure, witness.particle_birth ? &transaction.birth : nullptr,private_trace_ticks,transaction.scheduler_bindings), "scheduler");
    if (!status.ok() && scheduler_failure.check) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical scheduler check={} address={:x}\n"),
            RC::to_generic_string(scheduler_failure.check), scheduler_failure.address);
        if (std::string_view(scheduler_failure.check)=="membership_counts") {
            if(target.traces && current.traces) target.traces->DescribeOwnershipDelta(*current.traces);
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical scheduler counts A={} B={} births={}\n"),
                scheduler_failure.target_ticks,scheduler_failure.current_ticks,scheduler_failure.births);
            for(std::size_t i=0;i<scheduler_failure.difference_count;++i) {
                const auto& row=scheduler_failure.differences[i];
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical scheduler delta current={} tick={:x} owner={:x} name={}\n"),
                    row.current,row.tick,row.owner,reinterpret_cast<RC::Unreal::UObject*>(row.owner)->GetFullName());
            }
        }
    }
    if (!status.ok()) return status;
    status = step(target.vfx.PrepareManager(current.vfx, manager_, remaining(), transaction.manager,transaction.particle_bindings), "vfx_manager");
    if(!status.ok()) {
        const auto detail=target.vfx.HistoricalMaterialAdmission();
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical material admission target={} original={} component={:x} array={} count={} non_null={} material={:x} parent={:x} captured_metadata_only=true policy=null_only_exact_membership_v1\n"),
            target.execution.tick,current.execution.tick,detail.component,detail.array,detail.count,detail.non_null,detail.first_material,detail.first_parent);
    }
    if (status.ok()) status = step(target.vfx.PrepareCpuEmitters(remaining(), transaction.cpu,transaction.particle_bindings), "cpu_emitters");
    if (status.ok()) status = step(target.vfx.PrepareGpuEmitters(remaining(), transaction.gpu,transaction.particle_bindings), "gpu_emitters");
    if (status.ok()) status = step(target.vfx.PrepareTilePools(current.vfx, remaining(), transaction.pools,transaction.particle_bindings), "tile_pools");
    if (status.ok()) status = step(target.world.PrepareRestore(image_base_, world_, remaining(), transaction.world), "world");
    if (status.ok()) status = step(target.hud && current.hud ? target.hud->Prepare(*current.hud,transaction.hud) : Status::failure(FailureCode::UnsupportedContent), "hud");
    if (status.ok()) status = step(target.vfx_handler && current.vfx_handler ? target.vfx_handler->Prepare(*current.vfx_handler,remaining(),transaction.vfx_handler) : Status::failure(FailureCode::UnsupportedContent), "vfx_handler");
    if(status.ok()) status=step(target.source_registration->Prepare(*current.source_registration,remaining(),transaction.source_registration),"source_registration");
    if (status.ok()) status = step(transaction.world.PrepareRenderWork(target.physics, current.physics, remaining()), "world_render_work");
    if (status.ok() && witness.particle_birth) {
        status = step(transaction.birth.ValidateRetirement(), "birth_retirement");
        if(!status.ok()) for(const auto& birth:transaction.birth.members()) {
            const char* check{};
            const auto result=birth.ValidateRetirement(&check);
            if(!result.ok()) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical birth retirement check={} component={:x} name={} code={} before_A_publication=true B_retained=true\n"),
                    RC::to_generic_string(check?check:"unknown"),birth.component(),
                    reinterpret_cast<RC::Unreal::UObject*>(birth.component())->GetFullName(),static_cast<unsigned>(result.code));
                break;
            }
        }
    }
    if (status.ok()) {
        Sc6ReplayVfxState::CoordinateFailure failure{};
        std::vector<Sc6ReplayVfxState::CoordinateRebuild> coordinates;
        status = step(target.vfx.PrepareCoordinateReconstruction(current.vfx, coordinates, remaining(), &failure, transaction.particle_bindings), "gpu_coordinates");
        if (!status.ok()) RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical coordinate rejection component={:x} ordinal={} found={} system_equal={} pool_equal={} target_tiles={} original_tiles={} first={} target_tile={} original_tile={} render_A={:x} render_B={:x}\n"),
            failure.component, failure.ordinal, failure.found, failure.system_equal, failure.pool_equal,
            failure.target_count, failure.original_count, failure.first, failure.target_tile, failure.original_tile,
            failure.render_a, failure.render_b);
        if (status.ok()) {
            const auto count = coordinates.size();
            status = step(particle_copy_->PrepareCoordinates(std::move(coordinates), remaining())
                ? Status::success() : Status::failure(FailureCode::RestorePreflightFailed), "gpu_coordinate_owners");
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] historical coordinate preparation rows={} success={} owner={:x} check={}\n"),
                count, status.ok(), particle_copy_->witness().reconstruction_owner, particle_copy_->witness().reconstruction_issue);
        }
    }
    // A must be fully reconstructible now. A B-only field or scalar packet can be
    // admitted later by PrepareBirthRegistration on the render thread, after
    // exact owner/membership and retained-B checks. No publication can bypass it.
    const auto reconstruction = particle_copy_->witness();
    const bool pending_birth_fields = witness.particle_birth
        && (reconstruction.reconstruction_issue == 5 || reconstruction.reconstruction_issue == 6);
    if (status.ok() && (!reconstruction.reconstruction_a || (!reconstruction.reconstruction_b && !pending_birth_fields)))
        status = step(Status::failure(FailureCode::UnsupportedContent), "render_reconstruction");
    if (!status.ok()) return status;
    if (!account()) return step(Status::failure(FailureCode::CapacityExceeded), "prepared_budget");
    Sc6ReplayWorldState::PhysicsProjectionFailure physics_failure{};
    status=step(transaction.physics_markers.Prepare(image_base_,target.physics,current.physics,remaining()),"physics_markers");
    if(!status.ok()) {
        const auto diagnostic=transaction.physics_markers.diagnostic();
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] physics marker preparation check={} owner={:x} row={} captured_side={} expected={:x} observed={:x} A_published=false B_retained=true\n"),
            RC::to_generic_string(diagnostic.check),diagnostic.owner,diagnostic.row,diagnostic.source,diagnostic.expected,diagnostic.observed);
        if(diagnostic.check && std::string_view(diagnostic.check)=="marker_live_notifications")
            for(unsigned scene=0;scene<target.physics.node_domains.size();++scene) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] physics notification capture scene={} A={} B={} A_idle={} B_idle={} A_valid={} B_valid={} captured_only=true\n"),
                    scene,target.execution.tick,current.execution.tick,target.physics.node_domains[scene].notifications_quiescent,
                    current.physics.node_domains[scene].notifications_quiescent,target.physics.node_domains[scene].valid,current.physics.node_domains[scene].valid);
                for(unsigned actor=0;actor<current.physics.observed_actors[scene];++actor) {
                    const auto& row=current.physics.actors[scene][actor];
                    unsigned short flags{};std::memcpy(&flags,row.simulation_storage.data()+0xb4,2);
                    if(!(flags&0xf0))continue;
                    std::uintptr_t native_scene{},core{};
                    std::memcpy(&native_scene,row.simulation_storage.data()+0x40,8);
                    std::memcpy(&core,row.simulation_storage.data()+0x48,8);
                    RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] physics notification body scene={} actor={} kind={} native={:x} sim={:x} core={:x} sc={:x} flags={:x} isolated={} interactions={} mutable={} captured_only=true\n"),
                        scene,actor,row.kind,row.actor,row.simulation,core,native_scene,flags,row.isolated_kinematic,row.interaction_count,row.mutable_projection());
                }
            }
        return status;
    }
    status = Sc6ReplayWorldState::PreparePhysicsProjection(target.physics, current.physics, &physics_failure,false,&transaction.physics_markers,true);
    for(unsigned scene=0;scene<target.physics.scenes.size();++scene)
        for(unsigned actor=0;actor<target.physics.observed_actors[scene];++actor) {
            const auto& a=target.physics.actors[scene][actor];const auto& b=current.physics.actors[scene][actor];
            if(a.component_transform_flags!=b.component_transform_flags)
                RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] physics transform-cache admission scene={} actor={} component={:x} A={:x} B={:x} only_current_bit={}\n"),
                    scene,actor,a.component,a.component_transform_flags,b.component_transform_flags,
                    ((a.component_transform_flags^b.component_transform_flags)&~1u)==0);
        }
    if (!status.ok()) RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical physics check={} scene={} actor={}\n"),
        RC::to_generic_string(physics_failure.check ? physics_failure.check : "unknown"), physics_failure.scene, physics_failure.actor);
    if(!status.ok() && physics_failure.check && std::string_view(physics_failure.check)=="physics_node_ownership") {
        const auto scene=physics_failure.scene;
        const auto& a=target.physics.node_domains[scene];const auto& b=current.physics.node_domains[scene];
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] physics node domain A={} B={} valid={}/{} owner={:x}/{:x} next={}/{} free={}/{} pending={}/{} counts={},{} / {},{} captured_only=true\n"),
            target.execution.tick,current.execution.tick,a.valid,b.valid,a.owner,b.owner,a.next_id,b.next_id,a.free_count,b.free_count,a.pending_count,b.pending_count,a.counts[0],a.counts[1],b.counts[0],b.counts[1]);
        unsigned printed{};
        for(unsigned slot=0;slot<2;++slot)for(unsigned id=0;id<(std::min)(64u,(std::max)(a.counts[slot],b.counts[slot]));++id) {
            if(a.nodes[slot][id]==b.nodes[slot][id] && a.indices[slot][id]==b.indices[slot][id])continue;
            if(printed++>=32)continue;
            std::array<std::uint64_t,4> aw{},bw{};std::memcpy(aw.data(),a.nodes[slot][id].data(),32);std::memcpy(bw.data(),b.nodes[slot][id].data(),32);
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] physics node delta slot={} id={} index={:x}/{:x} A={:x},{:x},{:x},{:x} B={:x},{:x},{:x},{:x} captured_only=true\n"),
                slot,id,a.indices[slot][id],b.indices[slot][id],aw[0],aw[1],aw[2],aw[3],bw[0],bw[1],bw[2],bw[3]);
        }
    }
    if(!status.ok() && physics_failure.check && std::string_view(physics_failure.check).starts_with("physics_order_")) {
        const auto s=physics_failure.scene;
        for(unsigned side=0;side<2;++side) {
            const auto& image=side?current.physics:target.physics;
            for(unsigned i=0;i<image.observed_actors[s];++i) {
                const auto& row=image.actors[s][i];const auto& admission=row.kinematic_admission;
                if(!admission.valid)continue;
                const auto word=[](const auto& bytes,std::size_t offset){unsigned v{};std::memcpy(&v,bytes.data()+offset,4);return v;};
                const auto pointer=[](const auto& bytes,std::size_t offset){std::uintptr_t v{};std::memcpy(&v,bytes.data()+offset,8);return v;};
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] physics order image side={} actor={} mutable={} active_count={} capacity={} prefix={} body_index={} node={:x} sim_vtable_rva={:x} core_offset={:x}\n"),
                    side,i,row.mutable_projection(),word(admission.active_header,8),word(admission.active_header,12)&0x7fffffff,
                    word(admission.active_header,16),word(row.simulation_storage,0xb8),word(row.simulation_storage,0xb0),
                    pointer(row.simulation_storage,0)-image.module,pointer(row.simulation_storage,0x48)-row.actor);
                const auto& other=(side?target.physics:current.physics).actors[s][i].kinematic_admission;
                for(unsigned slot=0;slot<2;++slot) {
                    const auto& island=admission.islands[slot];const auto& compared=other.islands[slot];
                    RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] physics order island side={} actor={} slot={} count={} capacity={} pending={} index={} entry={:x} flags={:x} same_active_header={} same_storage={} same_list_headers={}\n"),
                        side,i,slot,word(island.list_headers[0],8),word(island.list_headers[0],12)&0x7fffffff,
                        word(island.list_headers[1],8),island.index,island.index<64?island.lists[0][island.index]:0xffffffffu,
                        std::to_integer<unsigned>(island.node[4]),admission.active_header==other.active_header,
                        island.storage_header==compared.storage_header,island.list_headers==compared.list_headers);
                    if(!island.storage_header.empty() && island.storage_header!=compared.storage_header) {
                        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] physics order storage side={} actor={} slot={} nodes={:x} nodes_count={} nodes_capacity={} indices={:x} indices_count={} indices_capacity={}\n"),
                            side,i,slot,pointer(island.storage_header,0),word(island.storage_header,8),word(island.storage_header,12),
                            pointer(island.storage_header,16),word(island.storage_header,24),word(island.storage_header,28));
                    }
                }
            }
        }
    }
    if (!status.ok() && physics_failure.check && std::string_view(physics_failure.check) == "shape_scale_changed") {
        const auto& a = target.physics.actors[physics_failure.scene][physics_failure.actor];
        const auto& b = current.physics.actors[physics_failure.scene][physics_failure.actor];
        for (unsigned axis = 0; axis < 4; ++axis) {
            unsigned av{}, bv{}, ar{}, br{};
            std::memcpy(&av, a.component_transform.data() + 32 + axis * 4, 4);
            std::memcpy(&bv, b.component_transform.data() + 32 + axis * 4, 4);
            if (axis < 3) {
                std::memcpy(&ar, a.component_transform_auxiliary.data() + 108 + axis * 4, 4);
                std::memcpy(&br, b.component_transform_auxiliary.data() + 108 + axis * 4, 4);
            }
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical physics scale axis={} world_A={:x} world_B={:x} relative_A={:x} relative_B={:x} body_A={:x} body_B={:x}\n"),
                axis, av, bv, ar, br, axis < 3 ? a.body_scale[axis] : 0, axis < 3 ? b.body_scale[axis] : 0);
        }
    }
    // Preserve the bounded first-failure inventory until this projection has
    // independently matched native combat continuation and cancellation.
    for (std::size_t scene = 0; scene < current.physics.scenes.size(); ++scene)
    {
        const auto& a = target.physics.actor_counts[scene];
        const auto& b = current.physics.actor_counts[scene];
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] historical physics scene_flags scene={} A={:x} B={:x}\n"),
            scene, target.physics.scene_flags[scene], current.physics.scene_flags[scene]);
        if(!status.ok()) {
            const auto& af=target.physics.node_domains[scene];
            const auto& bf=current.physics.node_domains[scene];
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] physics filter binding scene={} A_shader={:x} B_shader={:x} A_callback={:x} B_callback={:x} A_data={:x} B_data={:x} A_size={} B_size={}\n"),
                scene,af.filter_shader,bf.filter_shader,af.filter_callback,bf.filter_callback,
                af.filter_data,bf.filter_data,af.filter_data_size,bf.filter_data_size);
        }
        for (unsigned pruner = 0; current.physics.scenes[scene] && pruner < 2; ++pruner) {
            unsigned a_count{}, b_count{};
            std::memcpy(&a_count, target.physics.query_pruners[scene].data() + pruner * 0x30 + 0x20, 4);
            std::memcpy(&b_count, current.physics.query_pruners[scene].data() + pruner * 0x30 + 0x20, 4);
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] historical physics query scene={} pruner={} A_dirty={} B_dirty={} same_header={}\n"),
                scene, pruner, a_count, b_count, !std::memcmp(target.physics.query_pruners[scene].data() + pruner * 0x30,
                    current.physics.query_pruners[scene].data() + pruner * 0x30, 0x30));
        }
        RC::Output::send<RC::LogLevel::Default>(STR(
            "[HorseMod] historical physics scene={} A_static={} A_dynamic={} A_particles={} A_fluid={} A_cloth={} A_all={} A_articulations={} "
            "B_static={} B_dynamic={} B_particles={} B_fluid={} B_cloth={} B_all={} B_articulations={}\n"),
            scene, a[0], a[1], a[2], a[3], a[4], a[5], target.physics.articulations[scene],
            b[0], b[1], b[2], b[3], b[4], b[5], current.physics.articulations[scene]);
        if (a[5] || b[5] || target.physics.articulations[scene] || current.physics.articulations[scene])
        {
            for (unsigned i = 0; i < current.physics.observed_actors[scene]; ++i) {
                const auto& actor = current.physics.actors[scene][i];
                if (actor.kind != 2) continue;
                const Sc6ReplayWorldState::PhysicsBoundary::ActorObservation* prior = nullptr;
                for (unsigned j = 0; j < target.physics.observed_actors[scene]; ++j)
                    if (target.physics.actors[scene][j].actor == actor.actor) prior = &target.physics.actors[scene][j];
                auto* p = const_cast<std::byte*>(actor.properties.data());
                RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] historical physics actor={} vtable_rva={:x} body={} component={} actor_flags={:x} body_flags={:x} sleeping={} wake_bits={:x} same_pose={} same_velocity={} same_properties={}\n"),
                    actor.actor, actor.vtable - current.physics.module, actor.body, actor.component,
                    EngineField<unsigned char>(p, 0x10), EngineField<unsigned char>(p, 0x9c), EngineField<unsigned char>(p, 0xbc),
                    EngineField<unsigned>(p, 0xcc), prior && !std::memcmp(prior->properties.data() + 0x28, p + 0x28, 28),
                    prior && !std::memcmp(prior->properties.data() + 0x84, p + 0x84, 24), prior && prior->properties == actor.properties);
                if (prior) {
                    if (!status.ok() && prior->interaction_count != actor.interaction_count) {
                        for(unsigned side=0;side<2;++side) {
                            const auto& row=side?actor:*prior;
                            for(unsigned entry=0;entry<row.interaction_count;++entry) {
                                const auto& item=row.interactions[entry];
                                std::array<std::uintptr_t,2> owners{};std::array<unsigned,3> indices{};
                                std::memcpy(owners.data(),item.header.data()+8,16);
                                std::memcpy(indices.data(),item.header.data()+0x18,12);
                                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] physics marker graph side={} actor={} entry={} address={} type={} flags={:x} dirty={:x} elements={},{} filter_pair={:x} registered={} owners={},{} indices={},{},{}\n"),
                                    side,row.actor,entry,item.address,std::to_integer<unsigned>(item.header[0x24]),
                                    std::to_integer<unsigned>(item.header[0x25]),std::to_integer<unsigned>(item.header[0x26]),
                                    item.marker_elements[0],item.marker_elements[1],item.marker_filter_pair,item.marker_registered,
                                    owners[0],owners[1],indices[0],indices[1],indices[2]);
                                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] physics marker inputs side={} address={} scene_count={} scene_active={} map_count={} actor_counts={},{} valid={} attributes={:x},{:x} cores={:x},{:x} filters={:x},{:x},{:x},{:x}/{:x},{:x},{:x},{:x}\n"),
                                    side,item.address,item.marker_scene_count,item.marker_scene_active,item.marker_map_count,
                                    item.marker_actor_counts[0],item.marker_actor_counts[1],item.marker_filter_valid,
                                    item.marker_attributes[0],item.marker_attributes[1],item.marker_shape_cores[0],item.marker_shape_cores[1],
                                    item.marker_filter_data[0][0],item.marker_filter_data[0][1],item.marker_filter_data[0][2],item.marker_filter_data[0][3],
                                    item.marker_filter_data[1][0],item.marker_filter_data[1][1],item.marker_filter_data[1][2],item.marker_filter_data[1][3]);
                            }
                        }
                    }
                    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] historical physics component actor={} component={} vtable_rva={:x} same_transform={}\n"),
                        actor.actor, actor.component, actor.component_vtable ? actor.component_vtable - image_base_ : 0,
                        prior->component_transform == actor.component_transform);
                    for (unsigned entry = 0; entry < actor.interaction_count; ++entry) {
                        const auto& interaction = actor.interactions[entry];
                        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] historical physics interaction actor={} entry={} address={} type={} flags={:x} dirty={:x} same_header={}\n"),
                            actor.actor, entry, interaction.address, std::to_integer<unsigned>(interaction.header[0x24]),
                            std::to_integer<unsigned>(interaction.header[0x25]), std::to_integer<unsigned>(interaction.header[0x26]),
                            entry < prior->interaction_count && interaction == prior->interactions[entry]);
                    }
                    for (unsigned slot = 0; slot < 2; ++slot) {
                        const auto& pa = prior->kinematic_admission;
                        const auto& pb = actor.kinematic_admission;
                        RC::Output::send<RC::LogLevel::Default>(STR(
                            "[HorseMod] historical physics wake actor={} island={} A_valid={} B_valid={} controller_rva={:x} same_admission={} same_active={} same_node={} A_flags={:x} B_flags={:x} A_index={} B_index={} same_lists={}\n"),
                            actor.actor, slot, pa.valid, pb.valid, pa.controller_vtable ? pa.controller_vtable - target.physics.module : 0,
                            pa == pb, pa.active_header == pb.active_header && pa.active_bodies == pb.active_bodies,
                            pa.islands[slot].node == pb.islands[slot].node,
                            std::to_integer<unsigned>(pa.islands[slot].node[4]), std::to_integer<unsigned>(pb.islands[slot].node[4]),
                            pa.islands[slot].index, pb.islands[slot].index,
                            pa.islands[slot].list_headers == pb.islands[slot].list_headers && pa.islands[slot].lists == pb.islands[slot].lists);
                    }
                    const auto changed_words = [](const auto& first, const auto& second, unsigned start) {
                        std::uint64_t bits{};
                        for (unsigned word = start; word < first.size() / 4 && word < start + 64; ++word)
                            if (std::memcmp(first.data() + word * 4, second.data() + word * 4, 4)) bits |= std::uint64_t{1} << (word - start);
                        return bits;
                    };
                    const auto read_word = [](const auto& bytes, unsigned offset) {
                        unsigned value{}; std::memcpy(&value, bytes.data() + offset, sizeof(value)); return value;
                    };
                    RC::Output::send<RC::LogLevel::Default>(STR(
                        "[HorseMod] historical physics dependency actor={} same_sim={} same_kine={} actor_diff_lo={:x} actor_diff_hi={:x} sim_diff={:x} kine_diff={:x} "
                        "A_control={:x} B_control={:x} A_buffered={:x} B_buffered={:x} A_interactions={} B_interactions={} A_sim_flags={:x} B_sim_flags={:x} A_isolated={} B_isolated={} A_notifications_idle={} B_notifications_idle={} "
                        "A_active={:x} B_active={:x} A_target_flags={:x} B_target_flags={:x}\n"), actor.actor,
                        prior->simulation == actor.simulation, prior->kinematic == actor.kinematic,
                        changed_words(prior->dynamic_storage, actor.dynamic_storage, 0), changed_words(prior->dynamic_storage, actor.dynamic_storage, 64),
                        changed_words(prior->simulation_storage, actor.simulation_storage, 0), changed_words(prior->kinematic_storage, actor.kinematic_storage, 0),
                        read_word(prior->dynamic_storage, 0x68), read_word(actor.dynamic_storage, 0x68),
                        read_word(prior->dynamic_storage, 0x17c), read_word(actor.dynamic_storage, 0x17c),
                        read_word(prior->simulation_storage, 0x34), read_word(actor.simulation_storage, 0x34),
                        read_word(prior->simulation_storage, 0xb4), read_word(actor.simulation_storage, 0xb4),
                        prior->isolated_kinematic, actor.isolated_kinematic,
                        target.physics.node_domains[scene].notifications_quiescent, current.physics.node_domains[scene].notifications_quiescent,
                        read_word(prior->simulation_storage, 0xb8), read_word(actor.simulation_storage, 0xb8),
                        read_word(prior->kinematic_storage, 0x1c), read_word(actor.kinematic_storage, 0x1c));
                }
            }
        }
    }
    if (!status.ok()) return step(status, "physics_projection");
    if (witness.particle_birth && !BindParticleBirths(transaction.birth, remaining() + particle_copy_->witness().bytes))
        return step(Status::failure(FailureCode::RestorePreflightFailed), "render_birth_binding");
    status = step(checkpoint_capture_->PrepareEnclosingWind(target.gameplay, remaining()), "wind_graphs");
    if (!status.ok()) return status;
    if (!account()) {
        auto recovery = checkpoint_capture_->UndoEnclosingWind();
        if (recovery.ok()) recovery = checkpoint_capture_->FinishEnclosingWind();
        return step(recovery.ok() ? Status::failure(FailureCode::CapacityExceeded) : recovery, "wind_owned_budget");
    }
    if(SeekOwnsExecution()) {
        constexpr std::size_t envelope=32*1024*1024;
        if(envelope>remaining()) return step(Status::failure(FailureCode::CapacityExceeded),"seek_execution_undo_budget");
        transaction.execution=std::make_unique<HistoricalRestore::Execution>(transaction.undo.execution.tick,seek_.witness.target);
        transaction.execution->scratch_reservation=envelope;
        transaction.execution->recovered_surface=std::make_shared<SurfaceSnapshot>(*transaction.undo.surface);
        PublishHistoricalOwnership();
    }
    witness.phase = RestoreOperationPhase::Prepared;
    checkpoint_restoring_ = true;
    return Status::success();
}

Status Sc6ReplayHost::PublishHistoricalRestore() noexcept
{
    if (!historical_restore_ || historical_restore_->witness.phase != RestoreOperationPhase::Prepared
        || surface_event_ || particle_command_pending_.load()
        || !particle_copy_->witness().reconstruction_a || !particle_copy_->witness().reconstruction_b
        || historical_restore_->witness.particle_birth != particle_copy_->witness().birth_prepared)
        return Status::failure(FailureCode::IllegalTransition);
    if(!CheckHistoricalMaterialBoundary())return Status::failure(FailureCode::UnsupportedContent);
    auto& transaction = *historical_restore_;
    const auto& target = *transaction.target;
    auto& current = transaction.undo;
    transaction.prior_mode = checkpoint_broker_->mode();
    const auto publication_started=ReplayGpuCompletion::Clock::now();
    auto publication_segment=publication_started;
    struct PublicationTiming {const char* participant{};std::int64_t microseconds{};};
    std::array<PublicationTiming,40> publication_timings{};
    std::size_t publication_timing_count{};
    const auto publish = [&](Status result, const char* participant) {
        const auto now=ReplayGpuCompletion::Clock::now();
        if(publication_timing_count<publication_timings.size())
            publication_timings[publication_timing_count++]={participant,
                std::chrono::duration_cast<std::chrono::microseconds>(now-publication_segment).count()};
        publication_segment=now;
        if (!result.ok()) {
            transaction.witness.participant = participant;
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical publication participant={} code={}\n"),
                RC::to_generic_string(participant), static_cast<unsigned>(result.code));
            const auto render = transaction.world.render_work_diagnostic();
            if (render.check) RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical render work check={} component={:x} observed={:x} expected={:x}\n"),
                RC::to_generic_string(render.check), render.component, render.observed, render.expected);
            if (render.check && std::string_view(render.check) == "unretained_component") {
                auto* lock = reinterpret_cast<CRITICAL_SECTION*>(static_cast<std::byte*>(world_) + 0x270);
                EnterCriticalSection(lock);
                unsigned shown{};
                for (unsigned set = 0; set < 2; ++set) {
                    auto* header = static_cast<std::byte*>(world_) + 0x1d0 + set * 0x50;
                    const auto slots = EngineField<int>(header, 8);
                    if (slots < 0 || slots > 128 || EngineField<void*>(header, 0x20)) continue;
                    for (int slot = 0; slot < slots && shown < 16; ++slot) {
                        if (!((EngineField<unsigned>(header, 0x10 + (slot / 32) * 4) >> (slot % 32)) & 1)) continue;
                        auto* weak = EngineField<std::byte*>(header, 0) + slot * 16;
                        auto* object = EngineNative<RC::Unreal::UObject*>(image_base_, 0xf823f0, weak);
                        if (!object) continue;
                        bool retained{};
                        for (unsigned scene = 0; scene < current.physics.scenes.size(); ++scene)
                            for (unsigned i = 0; i < current.physics.observed_actors[scene]; ++i)
                                retained |= current.physics.actors[scene][i].component == reinterpret_cast<std::uintptr_t>(object);
                        if (retained) continue;
                        ++shown;
                        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical render owner component={:x} flags={:x} parent={:x} name={}\n"),
                            reinterpret_cast<std::uintptr_t>(object), EngineField<unsigned>(object, 0x188),
                            EngineField<std::uintptr_t>(object, 0x1d0), object->GetFullName());
                    }
                }
                LeaveCriticalSection(lock);
            }
        }
        return result;
    };
    auto status = publish(callback_admission_ ? callback_admission_->Validate(target.callback_stamp)
        : Status::failure(FailureCode::ContextUnavailable), "callback_admission");
    if (status.ok()) status = publish(Sc6ReplayWorldState::ReadPhysicsBoundary(image_base_, world_, *transaction.physics_observation), "read_B_physics");
    if (status.ok()) status = publish(Sc6ReplayWorldState::ValidatePhysicsProjection(current.physics, *transaction.physics_observation, true), "validate_B_physics");
    if (status.ok()) status = publish(transaction.physics_markers.Publish(), "physics_markers");
    if (status.ok()) status = publish(checkpoint_capture_->EnsureRestoreOwnership(thread_), "ucrt_ownership");
    if (status.ok()) status = publish(transaction.world.PublishRenderWork(), "world_render_work");
    // Scheduler publication must see the intact B manager while validating
    // the birth proof. Its original membership survives through cancellation.
    if (status.ok()) status = publish(transaction.TargetScheduler().PublishBacking(current.scheduler, image_base_, world_,
        memory_limit_, transaction.scheduler), "scheduler");
    if (status.ok()) status = publish(transaction.manager.Publish(), "vfx_manager");
    if (status.ok()) status = publish(transaction.cpu.Publish(), "cpu_emitters");
    if (status.ok()) status = publish(transaction.gpu.PublishGpuStorage(), "gpu_emitters");
    if (status.ok()) status = publish(target.vfx.PublishTilePools(current.vfx, transaction.pools), "tile_pools");
    if (status.ok())
    {
        transaction.game_dirty = true;
        transaction.physics_dirty = true;
        status = publish(checkpoint_capture_->RestoreAndVerify(target.gameplay), "gameplay");
    }
    if (status.ok()) status = publish(transaction.world.Publish(), "world");
    if (status.ok()) status = publish(transaction.world.PublishCreationVisibility(target.physics,false), "creation_visibility");
    if (status.ok()) {
        transaction.hud_dirty = true;
        status = publish(target.hud->Install(*current.hud,transaction.hud), "hud");
    }
    if (status.ok()) {
        transaction.vfx_handler_dirty = true;
        status = publish(transaction.vfx_handler.Publish(), "vfx_handler");
    }
    if (status.ok()) {
        transaction.traces_dirty = true;
        status = publish(transaction.traces.Publish(), "traces");
    }
    if (status.ok()) {
        status = publish(replay_rendering_->Replace(current.rendering, target.rendering) ? Status::success() : Status::failure(FailureCode::RestorePreflightFailed), "rendering");
        transaction.rendering_dirty = status.ok();
    }
    Sc6ReplayResolvers resolvers{};
    resolvers.user = this; resolvers.image_base = image_base_; resolvers.replay_player = &ResolveCheckpointSource;
    Sc6ReplayNativeBridge bridge(resolvers);
    if (status.ok())
    {
        transaction.source_dirty = true;
        status=publish(transaction.source_registration.Publish(),"source_registration");
        if(status.ok()) status = publish(bridge.RestorePlaybackSource(current.source, target.source, true,
            Sc6ReplayNativeBridge::SourceRestoreScope::RetainedReplay), "source");
        if(status.ok()) status=publish(input_source_->Install(target.source,current.input_revision,target.input_revision),"input_revision");
        if(status.ok()) index_.SuspendSourceRevision(input_source_->revision_id());
        if(status.ok()) RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] input revision installed A={} B={}\n"),input_source_->revision_id(),current.input_revision?current.input_revision->id:0);
    }
    if (status.ok())
    {
        transaction.execution_dirty = true;
        status = publish(simulation_->RestoreIdleContinuation(current.execution, target.execution), "execution");
    }
    if (status.ok()) {
        Sc6ReplaySchedulerState::PublishedFailure failure{};
        status = publish(transaction.TargetScheduler().FinalizeContainerOrder(image_base_, world_, transaction.scheduler, &failure), "scheduler_final_order");
        if (!status.ok()) RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical scheduler finalization check={} address={:x} owner={:x} offset={:x} expected={:x} observed={:x}\n"),
            RC::to_generic_string(failure.check ? failure.check : "unknown"), failure.address, failure.owner, failure.offset, failure.expected, failure.observed);
    }
    if (status.ok()) status = publish(Sc6ReplayWorldState::ReadPhysicsBoundary(image_base_, world_, *transaction.physics_observation), "read_physics_after_gameplay");
    if (status.ok()) status = publish(transaction.world.ValidateRenderWork(), "validate_A_render_work");
    if (status.ok()) status = publish(DiagnosePhysicsInstallation(target.physics, *transaction.physics_observation, L"A", true), "preflight_A_physics");
    if (status.ok()) status = publish(Sc6ReplayWorldState::InstallPhysicsProjection(*transaction.physics_observation, target.physics, true), "install_A_physics");
    if (status.ok()) status = publish(Sc6ReplayWorldState::ReadPhysicsBoundary(image_base_, world_, *transaction.physics_observation), "read_A_physics");
    if (status.ok()) status = publish(Sc6ReplayWorldState::ValidatePhysicsProjection(target.physics, *transaction.physics_observation, false, true), "validate_A_physics");
    transaction.physics_queries_pending = status.ok();
    const auto publication_finished=ReplayGpuCompletion::Clock::now();
    const auto publication_us=std::chrono::duration_cast<std::chrono::microseconds>(publication_finished-publication_started).count();
    if(publication_us>=100000 || !status.ok()) {
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] historical publication timing A={} B={} elapsed_us={} deadline_start_remaining_us={} deadline_end_remaining_us={} participants={} success={}\n"),
            target.execution.tick,current.execution.tick,publication_us,
            std::chrono::duration_cast<std::chrono::microseconds>(particle_publication_deadline_-publication_started).count(),
            std::chrono::duration_cast<std::chrono::microseconds>(particle_publication_deadline_-publication_finished).count(),
            publication_timing_count,status.ok());
        for(std::size_t i=0;i<publication_timing_count;++i) {
            const auto& row=publication_timings[i];
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] historical publication cost participant={} elapsed_us={}\n"),
                RC::to_generic_string(row.participant),row.microseconds);
        }
    }
    transaction.witness.phase = status.ok() ? RestoreOperationPhase::Publishing : RestoreOperationPhase::Recovering;
    transaction.witness.failure = status.code;
    return status;
}

Status Sc6ReplayHost::UndoHistoricalRestore() noexcept
{
    if(!CheckHistoricalMaterialBoundary())return Status::failure(FailureCode::UnsupportedContent);
    auto& transaction = *historical_restore_;
    const auto& target = *transaction.target;
    auto& current = transaction.undo;
    Status result = Status::success();
    const auto undo = [&](Status status, const char* participant) {
        if (!status.ok()) {
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical undo participant={} code={}\n"),
                RC::to_generic_string(participant), static_cast<unsigned>(status.code));
            if (result.ok()) result = status;
        }
        return status.ok();
    };
    // Repair mixed headers before any native gameplay repair can enqueue work.
    if (transaction.world.render_repair_pending()
        && !undo(transaction.world.UndoRenderWork(), "partial_render_B")) {
        transaction.witness.failure = FailureCode::UndoFailed;
        transaction.witness.phase = RestoreOperationPhase::Failed;
        interior_phase_ = InteriorPhase::Failed;
        return result;
    }
    // Scheduler's C witness must be checked before world/component undo
    // changes any tick admission fields it observes. The application remains
    // held; restoring B registrations here cannot dispatch a task.
    if(transaction.execution)
        undo(transaction.TargetScheduler().UndoBacking(current.scheduler, image_base_, world_, transaction.scheduler), "scheduler");
    if (transaction.rendering_dirty)
        transaction.rendering_dirty = !undo(replay_rendering_->Replace(
            transaction.execution && transaction.execution->capture==HistoricalRestore::Execution::Capture::Current
                ? transaction.execution->rendering : target.rendering,current.rendering)
            ? Status::success() : Status::failure(FailureCode::UndoFailed), "rendering");
    // Recover every attempted participant even if an earlier undo failed.
    // Its owners remain alive and gameplay stays held until all validate.
    if (transaction.execution_dirty)
        transaction.execution_dirty = !undo(simulation_->RestoreIdleContinuation(simulation_->continuation(), current.execution), "execution");
    if (transaction.source_dirty)
    {
        ReplaySourceState observed{};
        auto status = ReadCheckpointSource(observed);
        Sc6ReplayResolvers resolvers{};
        resolvers.user = this; resolvers.image_base = image_base_; resolvers.replay_player = &ResolveCheckpointSource;
        Sc6ReplayNativeBridge bridge(resolvers);
        if (status.ok()) status = bridge.RestorePlaybackSource(observed, current.source, true,
            Sc6ReplayNativeBridge::SourceRestoreScope::RetainedReplay);
        if(status.ok()) status=input_source_->Install(current.source,input_source_->revision(),current.input_revision);
        if(status.ok()) RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] input revision recovered B={}\n"),input_source_->revision_id());
        transaction.source_dirty = !undo(status, "source");
    }
    transaction.source_dirty = !undo(transaction.source_registration.Undo(),"source_registration") || transaction.source_dirty;
    // Preparation already owns weak-reference increments and private arrays.
    // Cancellation before publication must retire those as well.
    transaction.vfx_handler_dirty = !undo(transaction.vfx_handler.Undo(), "vfx_handler");
    transaction.traces_dirty = !undo(transaction.traces.Undo(), "traces");
    if (transaction.hud_dirty) transaction.hud_dirty = !undo(current.hud->Install(
        transaction.execution && transaction.execution->capture==HistoricalRestore::Execution::Capture::Current
            ? transaction.execution->hud : *current.hud,transaction.hud,true), "hud");
    undo(transaction.world.Undo(), "world");
    undo(checkpoint_capture_->UndoEnclosingWind(), "wind_B_graph");
    if (transaction.game_dirty) transaction.game_dirty = !undo(checkpoint_capture_->RestoreAndVerify(current.gameplay), "gameplay");
    undo(target.vfx.UndoTilePools(current.vfx, transaction.pools), "tile_pools");
    undo(transaction.gpu.Undo(), "gpu_emitters");
    undo(transaction.cpu.Undo(), "cpu_emitters");
    undo(transaction.manager.Undo(), "vfx_manager");
    if(!transaction.execution)
        undo(transaction.TargetScheduler().UndoBacking(current.scheduler, image_base_, world_, transaction.scheduler), "scheduler");
    // Recreate B rendering only after all CPU/particle/manager owners are B.
    // No native end-frame consumer may observe a mixed A/B emitter graph.
    if(result.ok() && transaction.world.render_published()) {
        const auto visibility=transaction.world.PublishCreationVisibility(current.physics,true);
        if(!visibility.ok()) {
            const auto diagnostic=transaction.world.render_work_diagnostic();
            if(diagnostic.check) RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical B visibility check={} component={:x} observed={:x} expected={:x}\n"),
                RC::to_generic_string(diagnostic.check),diagnostic.component,diagnostic.observed,diagnostic.expected);
        }
        undo(visibility,"creation_visibility_B");
    }
    if(result.ok() && transaction.execution)
        undo(current.traces->ReconstructVisibleRenderingForUndo(transaction.execution->trace_render_reconstruction),"trace_visible_render_B");
    auto physics_status = transaction.physics_markers.Undo();
    if(physics_status.ok())physics_status = Sc6ReplayWorldState::ReadPhysicsBoundary(image_base_, world_, *transaction.physics_observation);
    if (physics_status.ok() && transaction.physics_dirty && transaction.world.render_published())
        physics_status = transaction.world.ValidateRenderWork();
    if (physics_status.ok() && transaction.physics_dirty)
        physics_status = DiagnosePhysicsInstallation(current.physics, *transaction.physics_observation, L"B", true);
    if (physics_status.ok() && transaction.physics_dirty)
        physics_status = Sc6ReplayWorldState::InstallPhysicsProjection(*transaction.physics_observation, current.physics, true);
    if (physics_status.ok()) physics_status = transaction.world.UndoRenderWork();
    if(physics_status.ok() && transaction.execution && !transaction.physics_queries_pending)
        physics_status=Sc6ReplayWorldState::ReconstructPhysicsQueries(current.physics);
    if (physics_status.ok()) physics_status = Sc6ReplayWorldState::ReadPhysicsBoundary(image_base_, world_, *transaction.physics_observation);
    if (physics_status.ok()) physics_status = Sc6ReplayWorldState::ValidatePhysicsProjection(current.physics, *transaction.physics_observation, true);
    // Reconstructed native query caches advance their invalidation epochs.
    // Use the audited projection validator, including every storage binding,
    // shape and pose, instead of comparing those physical epochs to old B.
    if (physics_status.ok()) physics_status=Sc6ReplayWorldState::ValidatePhysicsProjection(
        current.physics,*transaction.physics_observation,true);
    undo(physics_status, "physics");
    if (result.ok()) undo(current.vfx_handler->ValidateValues(), "vfx_handler_final_B");
    if (result.ok()) undo(current.source_registration->ValidateValues(),"source_registration_final_B");
    if (result.ok()) undo(current.traces->ValidateValues(), "traces_final_B");
    if (result.ok()) undo(ValidateCheckpointLease(current), "checkpoint_lease");
    if (result.ok()) undo(checkpoint_capture_->FinishEnclosingWind(), "wind_A_retirement");
    if (result.ok() && transaction.prior_mode == UcrtRandBrokerMode::Observing)
        undo(checkpoint_broker_->ReleaseOwnership(thread_), "ucrt_ownership");
    if (!result.ok())
    {
        transaction.witness.failure = FailureCode::UndoFailed;
        transaction.witness.phase = RestoreOperationPhase::Failed;
        interior_phase_ = InteriorPhase::Failed;
        return result;
    }
    if(transaction.execution) {
        auto& execution=*transaction.execution;
        if(execution.ownership.phase()!=ReplaySeekOwnership::Phase::Recovering) {
            transaction.witness.failure=FailureCode::UndoFailed;transaction.witness.phase=RestoreOperationPhase::Failed;
            interior_phase_=InteriorPhase::Failed;return Status::failure(FailureCode::UndoFailed);
        }
        execution.recovered_surface->epoch=execution.boundary_epoch;
        held_surface_=execution.recovered_surface;
        PublishHistoricalOwnership();
    }
    // CPU B is installed, but native reconstruction can still have queued
    // render work. Complete recovery only after the admitted Finish retires it.
    transaction.witness.original_recovered = false;
    transaction.witness.phase = RestoreOperationPhase::Recovered;
    transaction.witness.pending = true;
    transaction.game_dirty = transaction.source_dirty = transaction.execution_dirty = false;
    transaction.physics_dirty = transaction.physics_queries_pending = false;
    transaction.rendering_dirty = transaction.hud_dirty = transaction.traces_dirty = transaction.vfx_handler_dirty = false;
    return Status::success();
}

void Sc6ReplayHost::PublishHistoricalOwnership() noexcept
{
    if(!historical_restore_ || !historical_restore_->execution) return;
    auto& witness=historical_restore_->witness;
    const auto& owner=historical_restore_->execution->ownership;
    ReplayNiagaraObservation::Transaction(owner.phase());
    if(witness.ownership_phase==owner.phase()) return;
    witness.ownership_phase=owner.phase();witness.commit_decided=owner.commit_decided();
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] seek ownership state={} B_tick={} target={} current_tick={} completed_tail_tick={} B_retained={} commit_decided={} executed_ticks={} executed_intervals={}\n"),
        RC::to_generic_string(ReplaySeekOwnership::Name(owner.phase())),owner.original_tick(),owner.target_tick(),
        simulation_?simulation_->continuation().tick:0,owner.completed_tail_tick(),owner.retains_undo(),owner.commit_decided(),
        witness.executed_ticks,witness.executed_intervals);
}

Status Sc6ReplayHost::RequestHistoricalExecution()
{
    if(!historical_restore_ || !historical_restore_->execution || !SeekOwnsExecution()
        || !seek_driving_ || !CheckBinding() || depth_ || surface_event_ || particle_command_pending_.load()
        || !checkpoint_restoring_ || !particle_copy_ || !replay_rendering_
        || interior_phase_!=InteriorPhase::Holding || pause_boundary_!=PauseBoundary::CompletedApplication
        || application_phase_!=ApplicationPhase::Idle || !engine_idle() || !executor_.idle()
        || !executor_.arena_empty() || !simulation_->interval_complete())
        return Status::failure(FailureCode::IllegalTransition);
    if(!CheckHistoricalMaterialBoundary())return Status::failure(FailureCode::UnsupportedContent);
    auto& transaction=*historical_restore_;
    const auto copy=particle_copy_->witness();
    if(transaction.witness.phase!=RestoreOperationPhase::Held || transaction.cancel || transaction.witness.commit_decided
        || !transaction.undo.valid || !transaction.target || !transaction.undo.surface
        || !transaction.surface_dirty || copy.pending || !copy.native_write_uncommitted || !copy.installed_copy_complete
        || copy.phase!=Sc6ReplayParticleCopy::Phase::Installed
        || simulation_->continuation().tick!=transaction.target->execution.tick)
        return Status::failure(FailureCode::RestoreVerificationFailed);
    auto& execution=*transaction.execution;
    if(execution.ownership.phase()!=ReplaySeekOwnership::Phase::APublished
        || execution.render!=HistoricalRestore::Execution::Render::Retained)
        return Status::failure(FailureCode::IllegalTransition);
    auto status=PrepareHistoricalPhysicsForExecution();
    if(!status.ok()) return status;
    std::array<unsigned,64> ids{};std::size_t count{};
    if(!particle_copy_->ReadQuarantinedPrimitiveIds(transaction.birth,ids,count))
        return Status::failure(FailureCode::RestorePreflightFailed);
    struct TraceMask {Sc6ReplayParticleCopy& copy;std::span<unsigned> ids;std::size_t& count;} mask{*particle_copy_,ids,count};
    status=transaction.traces.VisitPrivateMeshOwners(&mask,[](void* context,std::uintptr_t component) {
        auto& mask=*static_cast<TraceMask*>(context);
        return mask.copy.AppendQuarantinedTracePrimitive(component,mask.ids,mask.count);
    });
    if(!status.ok() || !particle_copy_->AppendQuarantinedGroundPrimitives(ids,count)
        || !replay_rendering_->SetQuarantinedPrimitives({ids.data(),count},AdmissionRemaining()))
        return Status::failure(FailureCode::RestorePreflightFailed);
    execution.quarantine=HistoricalRestore::Execution::Quarantine::Installed;
    Sc6ReplayParticleCopy::Witness copy_witness{};bool pending{};
    if(!ParticleCopyExperiment(ParticleCopyAction::PrepareExecutionCoordinates,&copy_witness,&pending))
        return Status::failure(FailureCode::PresentationFailed);
    execution.render=HistoricalRestore::Execution::Render::CoordinatesPending;
    transaction.witness.pending=pending;
    return Status::success();
}

bool Sc6ReplayHost::HistoricalExecutionAdmitted(bool allow_settled) const noexcept
{
    if(!historical_restore_ || !historical_restore_->execution || !checkpoint_restoring_ || !particle_copy_)
        return false;
    const auto& execution=*historical_restore_->execution;
    const auto phase=execution.ownership.phase();
    return (phase==ReplaySeekOwnership::Phase::ExecutionActive
            || (allow_settled && phase==ReplaySeekOwnership::Phase::CSettled)
            || phase==ReplaySeekOwnership::Phase::CompletingTargetTails
            || phase==ReplaySeekOwnership::Phase::CompletingResumedTails
            || phase==ReplaySeekOwnership::Phase::RecoveryQuiescing)
        && (historical_restore_->fresh_particles.empty()
            || historical_restore_->fresh_render_phase==HistoricalRestore::FreshRenderPhase::Ready)
        && particle_copy_->particle_render_ready()
        // A detached scope cannot silently authorize another corrected step.
        // Re-admission must retain the original contract; live rows are never
        // promoted to a new baseline by this entry-comparison containment.
        && execution.finish_armed
        && execution.ownership.retains_undo()
        && execution.render==HistoricalRestore::Execution::Render::HandedOff
        && execution.quarantine==HistoricalRestore::Execution::Quarantine::Installed
        && std::all_of(execution.participants.begin(),execution.participants.end(),[](auto p) {
            return p==HistoricalRestore::Execution::Participant::HandedOff;
        }) && particle_copy_->execution_started() && !particle_copy_->witness().pending;
}

Status Sc6ReplayHost::StepHistoricalExecution()
{
    if(!historical_restore_ || !historical_restore_->execution || !CheckBinding() || depth_
        || !seek_driving_ || historical_restore_->cancel || historical_restore_->witness.pending
        || historical_restore_->witness.phase!=RestoreOperationPhase::Held
        || interior_phase_!=InteriorPhase::Holding)
        return Status::failure(FailureCode::IllegalTransition);
    auto& owner=historical_restore_->execution->ownership;
    const auto previous=owner;
    if(!owner.StepFromSettled(simulation_->continuation().tick))
        return Status::failure(FailureCode::IllegalTransition);
    if(!HistoricalExecutionAdmitted()) {owner=previous;return Status::failure(FailureCode::IllegalTransition);}
    const auto advanced=AdvanceToTick(owner.target_tick(),this,&ObserveSeekHold);
    if(advanced.phase==TickAdvancePhase::Failed) {owner=previous;return Status::failure(advanced.failure);}
    PublishHistoricalOwnership();
    return Status::success();
}

#include "Sc6ReplayHost.ParticleLifetime.inl"

Status Sc6ReplayHost::CaptureHistoricalExecution(bool retiring)
{
    if(!historical_restore_ || !historical_restore_->execution || !CheckBinding() || depth_
        || surface_event_ || particle_command_pending_.load()
        || interior_phase_!=InteriorPhase::Holding || pause_boundary_!=PauseBoundary::CompletedApplication
        || application_phase_!=ApplicationPhase::Idle || !engine_idle() || !executor_.idle()
        || !executor_.arena_empty() || !simulation_->interval_complete())
        return Status::failure(FailureCode::IllegalTransition);
    auto& transaction=*historical_restore_;auto& execution=*transaction.execution;
    const auto phase=execution.ownership.phase();
    if(phase!=ReplaySeekOwnership::Phase::RecoveryQuiescing && phase!=ReplaySeekOwnership::Phase::CompletingTargetTails && phase!=ReplaySeekOwnership::Phase::CompletingResumedTails)
        return Status::failure(FailureCode::IllegalTransition);
    const auto remaining=[&] {
        // The reserved envelope already participates in AdmissionBytes. Each
        // capture additionally counts its old image while building its next.
        const auto actual=sizeof(execution)+(execution.recovered_surface?sizeof(SurfaceSnapshot):0)
            +execution.current.owned_bytes()+execution.retiring.owned_bytes()
            +execution.scheduler.owned_bytes()+execution.traces.owned_bytes()+execution.hud.owned_bytes();
        return AdmissionRemaining()+(execution.scratch_reservation>actual?execution.scratch_reservation-actual:0);
    };
    auto measured_at=std::chrono::steady_clock::now();
    const auto measure=[&](const char* component) {
        const auto now=std::chrono::steady_clock::now();
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] seek C capture cost target={} current={} retiring={} component={} elapsed_us={}\n"),
            execution.ownership.target_tick(),simulation_->continuation().tick,retiring,RC::to_generic_string(component),
            std::chrono::duration_cast<std::chrono::microseconds>(now-measured_at).count());
        measured_at=std::chrono::steady_clock::now();
    };
    auto& image=retiring?execution.retiring:execution.current;
    {
        // Reopened participants no longer refer to C trace/HUD snapshots.
        // Retire their GC registrations explicitly at this admitted boundary,
        // before reusing the capture objects; immutable A and complete B stay.
        if(std::any_of(execution.participants.begin(),execution.participants.end(),[](auto p) {
            return p==HistoricalRestore::Execution::Participant::Settled;
        }))return Status::failure(FailureCode::IllegalTransition);
        auto retired=execution.traces.ReleaseCapture();
        if(retired.ok())retired=execution.hud.ReleaseCapture();
        if(!retired.ok())return retired;
    }
    auto status=CaptureReplayVfx(image,image_base_,manager_,remaining(),{},&transaction.birth);
    measure("vfx");
    if(!status.ok()) {
        // Failure-only census of the existing immutable A/private B leases.
        // Never dereference expired objects or relax quarantine on this path.
        if(!transaction.target->vfx.retained_owners_live())DiagnoseCheckpointOwner(*transaction.target);
        if(!transaction.undo.vfx.retained_owners_live())DiagnoseCheckpointOwner(transaction.undo);
        return status;
    }
    status=execution.traces.Capture(image_base_,world_,remaining());
    measure("traces");
    if(status.ok()) status=ValidateParticleCompletionOwnership(image,execution.traces);
    if(!status.ok() || retiring) return status;
    status=execution.hud.Capture(image_base_,world_,manager_,remaining());
    measure("hud");
    if(!status.ok()) return status;
    struct TickOwners {const Sc6ReplayTraceState* traces;const Sc6ReplayVfxState::ParticleBirthSet* births;};
    const TickOwners owners{&execution.traces,&transaction.birth};
    status=execution.scheduler.Capture(image_base_,world_,remaining(),{&owners,
        [](const void* opaque,void* context,bool(*visit)(void*,std::uintptr_t)) {
            const auto& owners=*static_cast<const TickOwners*>(opaque);
            auto status=owners.traces->VisitPrimaryTicks(context,visit);
            if(!status.ok()) return status;
            // B's disabled, registered tick prefixes remain owned while C
            // executes. Capture their actual admission/membership, not a
            // fabricated registration or an old scheduling epoch.
            for(const auto& birth:owners.births->members()) {
                status=birth.ValidateQuarantined();if(!status.ok()) return status;
                if(!visit(context,birth.component())) return Status::failure(FailureCode::CapturePreflightFailed);
            }
            return Status::success();
        }},&transaction.undo.scheduler);
    measure("scheduler");
    if(status.ok()) status=Sc6ReplayWorldState::ReadPhysicsBoundary(image_base_,world_,*transaction.physics_observation);
    measure("physics");
    if(status.ok() && !replay_rendering_->Capture(execution.rendering))
        status=Status::failure(FailureCode::PresentationFailed);
    measure("render_bindings");
    if(status.ok()) {
        execution.boundary_epoch=EngineField<std::uint64_t>(reinterpret_cast<void*>(image_base_),0x4197170);
        execution.capture=HistoricalRestore::Execution::Capture::Current;
    }
    return status;
}

void Sc6ReplayHost::AdvanceHistoricalExecution()
{
    if(!historical_restore_ || !historical_restore_->execution || surface_event_ || particle_command_pending_.load()) return;
    auto& transaction=*historical_restore_;auto& execution=*transaction.execution;
    auto& operation=transaction.witness;
    using Ownership=ReplaySeekOwnership::Phase;
    using Render=HistoricalRestore::Execution::Render;
    const auto fail=[&](Status status,const char* participant) {
        operation.failure=status.code;operation.participant=participant;
        execution.ownership.Fail();PublishHistoricalOwnership();
        operation.phase=RestoreOperationPhase::Failed;
        operation.pending=false;
    };
    if(HistoricalConsumerAbortPending() && interior_phase_==InteriorPhase::Holding
        && pause_boundary_==PauseBoundary::CompletedApplication && application_phase_==ApplicationPhase::Idle
        && engine_idle() && executor_.idle() && executor_.arena_empty() && simulation_->interval_complete()) {
        const auto& guard=*execution.prerequisite_guard;
        const auto& witness=guard.witness();
        RC::Output::send<RC::LogLevel::Warning>(STR(
            "[HorseMod] consumer prerequisite changed tick={} epoch={} owner_index={} owner_generation={} native_completed={} application_complete=true B_retained=true recovery=false\n"),
            simulation_->continuation().tick,witness.task().epoch,witness.contract().owner.index,
            witness.contract().owner.serial,guard.completed());
        if(!guard.completed())__fastfail(FAST_FAIL_INVALID_ARG);
        fail(Status::failure(FailureCode::UnsupportedContent),"execution_consumer_prerequisite_changed");return;
    }
    if(HistoricalParticleAbortPending() && interior_phase_==InteriorPhase::Holding
        && pause_boundary_==PauseBoundary::CompletedApplication && application_phase_==ApplicationPhase::Idle
        && engine_idle() && executor_.idle() && executor_.arena_empty() && simulation_->interval_complete()) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] seek protected particle lifetime abort component={:x} routes={} tick={} target={} B={} before_native_lifecycle=true B_retained=true commit_decided=false\n"),
            reinterpret_cast<std::uintptr_t>(execution.protected_particle.load()),execution.protected_particle_routes.load(),
            simulation_->continuation().tick,execution.ownership.target_tick(),execution.ownership.original_tick());
        fail(Status::failure(FailureCode::UnsupportedContent),"execution_protected_particle_lifetime");return;
    }
    auto copy=particle_copy_->witness();
    const auto queue=[&](ParticleCopyAction action) {
        bool pending{};
        const bool accepted=ParticleCopyExperiment(action,&copy,&pending);
        operation.pending=pending || copy.pending;
        if(!accepted) fail(Status::failure(FailureCode::PresentationFailed),"execution_render_command");
        return accepted;
    };
    if(copy.pending) {queue(ParticleCopyAction::Poll);return;}
    if(particle_command_failed_.load())
    {fail(Status::failure(FailureCode::PresentationFailed),"execution_render_completion");return;}
    if(execution.ownership.phase()==Ownership::APublished) {
        if(execution.render==Render::Retained) return; // BeginExecution has not been requested.
        if(execution.render==Render::CoordinatesPending) execution.render=Render::CoordinatesComplete;
        if(execution.render==Render::CoordinatesComplete) {queue(ParticleCopyAction::BeginExecution);return;}
        if(execution.render!=Render::HandedOff) {fail(Status::failure(FailureCode::IllegalTransition),"execution_render_handoff");return;}
        if(transaction.fresh_render_phase==HistoricalRestore::FreshRenderPhase::Empty) {
            auto status=BeginHistoricalCpuExecution();
            if(!status.ok()) {fail(status,"execution_cpu_handoff");return;}
            if(!transaction.fresh_particles.empty() || particle_copy_->trace_render_pending()) {
                status=BeginFreshParticleRenderOwners();
                if(!status.ok())fail(status,"fresh_particle_render_registration");
                return;
            }
        } else if(transaction.fresh_render_phase==HistoricalRestore::FreshRenderPhase::Queued) {
            if(!copy.execution_work_complete || !particle_copy_->particle_render_ready()) {
                fail(Status::failure(FailureCode::PresentationFailed),"fresh_particle_render_completion");return;
            }
            transaction.fresh_render_phase=HistoricalRestore::FreshRenderPhase::Ready;
        } else if(transaction.fresh_render_phase!=HistoricalRestore::FreshRenderPhase::Ready) {
            fail(Status::failure(FailureCode::IllegalTransition),"fresh_particle_render_partial");return;
        }
        if(AdmissionBytes()>memory_limit_) {fail(Status::failure(FailureCode::CapacityExceeded),"execution_budget");return;}
        { // Native reconstruction must preserve the restored simulation RNG.
            UcrtRandBrokerImage observed{};
            const auto read=checkpoint_broker_->Capture(thread_,observed);
            if((transaction.target->execution.tick>=248 && transaction.target->execution.tick<=260)
                || (transaction.target->execution.tick>=416 && transaction.target->execution.tick<=426))
                RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] execution RNG admission A={} expected={:08x} observed={:08x} combat_expected={:08x} combat_observed={:08x} valid={} read_only=true\n"),
                    transaction.target->execution.tick,transaction.target->ucrt.state,observed.state,
                    transaction.target->ucrt.combat_state,observed.combat_state,read.ok());
            if(!read.ok() || observed!=transaction.target->ucrt) {
                fail(Status::failure(FailureCode::RestoreVerificationFailed),"execution_A_rng");return;
            }
        }
        const auto finish=ArmHistoricalFinishGuard();
        if(!finish.ok()) {fail(finish,"execution_finish_dispatch_admission");return;}
        const auto consumer=ArmHistoricalConsumerGuard();
        if(!consumer.ok()) {fail(consumer,"execution_consumer_admission");return;}
        if(!execution.ownership.ActivateExecution()) {fail(Status::failure(FailureCode::IllegalTransition),"execution_admission");return;}
        PublishHistoricalOwnership();operation.pending=false;
        return;
    }
    const auto phase=execution.ownership.phase();
    if(phase==Ownership::TargetTailsCompleted) {
        if(!transaction.display_requested) {
            transaction.display_requested=QueueSurface(SurfaceCommand::PublishCheckpoint);
            operation.pending=transaction.display_requested;
            if(!transaction.display_requested)fail(Status::failure(FailureCode::PresentationFailed),"execution_viewport_publication");
            return;
        }
        if(!transaction.display_complete) {fail(Status::failure(FailureCode::PresentationFailed),"execution_viewport_completion");return;}
        RestoreOperationWitness result{};
        if(!RestoreOperation(RestoreOperationAction::Commit,nullptr,&result)) {
            fail(Status::failure(result.failure==FailureCode::None?FailureCode::RestoreVerificationFailed:result.failure),"execution_commit_admission");
        }
        return;
    }
    if(phase!=Ownership::RecoveryQuiescing && phase!=Ownership::CompletingTargetTails
        && phase!=Ownership::CompletingResumedTails) return;
    if(phase==Ownership::CompletingTargetTails && tick_advance_.phase==TickAdvancePhase::CompletionBlocked) {
        if(simulation_->continuation().tick!=execution.ownership.target_tick()
            || !execution.ownership.DeferTargetTails()) {fail(Status::failure(FailureCode::AdvanceFailed),"execution_exact_tail");return;}
        operation.pending=false;PublishHistoricalOwnership();
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] seek exact completion deferred tick={} simulation_phase={} pending_task=true B_retained=true resume_required=true\n"),
            simulation_->continuation().tick,static_cast<unsigned>(simulation_->continuation().phase));
        return;
    }
    if(interior_phase_!=InteriorPhase::Holding) return;
    if(pause_boundary_==PauseBoundary::SimulationTick) {
        const auto tail=CompleteApplicationToHold(this,&ObserveSeekHold);
        if(tail.phase!=TickAdvancePhase::Settling) {fail(Status::failure(tail.failure),"execution_application_tail");return;}
        operation.pending=true;return;
    }
    if(pause_boundary_!=PauseBoundary::CompletedApplication || application_phase_!=ApplicationPhase::Idle
        || !engine_idle() || !executor_.idle() || !executor_.arena_empty() || !simulation_->interval_complete()) return;
    // First finish the actual application, including an exact-target tail.
    // Only then release entry enforcement for native recovery/retirement.
    if(!StopHistoricalConsumerGuard()) {operation.pending=true;return;}
    const bool recovery=phase==Ownership::RecoveryQuiescing;
    if(!CheckHistoricalMaterialBoundary())return;
    // No handoff means cancellation still uses the original publication undo.
    if(!particle_copy_->execution_started()) {
        if(!recovery || std::any_of(execution.participants.begin(),execution.participants.end(),[](auto p) {
            return p!=HistoricalRestore::Execution::Participant::Retained;
        })) {fail(Status::failure(FailureCode::IllegalTransition),"execution_unhanded_boundary");return;}
        execution.boundary_epoch=EngineField<std::uint64_t>(reinterpret_cast<void*>(image_base_),0x4197170);
        if(!replay_rendering_->SetQuarantinedPrimitives({},0)
            || !execution.ownership.CompleteRecoveryTails(simulation_->continuation().tick)) {
            fail(Status::failure(FailureCode::UndoFailed),"execution_unhanded_recovery");return;
        }
        execution.quarantine=HistoricalRestore::Execution::Quarantine::Retired;
        PublishHistoricalOwnership();operation.phase=RestoreOperationPhase::Recovering;
        return;
    }
    if(recovery && execution.render==Render::Settled && particle_copy_->settled_for_commit()) {
        // A cancel arriving after render settlement reopens native C backing
        // before C-only teardown. B is untouched and still owns all its refs.
        const auto reopened=ReopenHistoricalCpuForUndo();
        if(!reopened.ok()) {fail(reopened,"execution_cpu_reopen_for_undo");return;}
        execution.capture=HistoricalRestore::Execution::Capture::Empty;
        queue(ParticleCopyAction::ContinueExecutionForUndo);return;
    }
    if(recovery && execution.render==Render::Drained && !execution.retirement.admitted) {
        if(!particle_copy_->CanRetireAfterExecutionDrain()
            || std::any_of(execution.participants.begin(),execution.participants.end(),[](auto p) {
                return p!=HistoricalRestore::Execution::Participant::HandedOff;
            })) {fail(Status::failure(FailureCode::IllegalTransition),"execution_drained_recovery_admission");return;}
        // The completed fence precedes C-only teardown, so it cannot certify
        // that teardown. Discard only the capture role, retain its leases until
        // CaptureHistoricalExecution retires them, then use the existing native
        // retirement route and submit a fresh ordered render/GPU drain.
        execution.capture=HistoricalRestore::Execution::Capture::Empty;
        execution.render=Render::HandedOff;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] seek drained recovery routed tick={} gpu_complete=true render_settled=false cpu_settled=0 B_retained=true fresh_retirement_drain_required=true\n"),simulation_->continuation().tick);
    }
    if(execution.render==Render::HandedOff) {
        const auto& current=simulation_->continuation();
        if(current.tick==transaction.target->execution.tick && current.interval==transaction.target->execution.interval
            && execution.capture==HistoricalRestore::Execution::Capture::Empty) {
            const auto completed=transaction.world.CompleteUnexecutedPublicationRenderWork();
            if(!completed.ok()) {
                const auto d=transaction.world.render_work_diagnostic();
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] publication render tail rejected check={} observed={} expected={} B_retained=true\n"),
                    RC::to_generic_string(d.check?d.check:"admission"),d.observed,d.expected);
                fail(completed,"execution_publication_render_tail");return;
            }
            const auto d=transaction.world.render_work_diagnostic();
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] publication render tail completed tick={} interval={} components={} gameplay_traversals=0 native_jobs_joined=true GPU_drain_pending=true B_retained=true\n"),current.tick,current.interval,d.observed);
        }
        if(recovery) {
            if(!CheckHistoricalMaterialBoundary())return;
            const auto private_b=transaction.traces.ValidateRetirementB();
            if(!private_b.ok()) {fail(private_b,"execution_trace_private_B");return;}
            if(execution.capture!=HistoricalRestore::Execution::Capture::RetiringOwners) {
                const auto status=CaptureHistoricalExecution(true);
                if(!status.ok()) {fail(status,"execution_C_retirement_capture");return;}
                execution.capture=HistoricalRestore::Execution::Capture::RetiringOwners;
            }
            if(!execution.retirement.admitted && execution.births.empty()) {
                Sc6ReplayVfxState::Topology topology{};
                const auto status=transaction.particle_bindings.empty()
                    ? transaction.target->vfx.PrepareTopology(execution.retiring,manager_,topology,execution.births)
                    : transaction.undo.vfx.PrepareRecoveryRetirement(execution.retiring,manager_,execution.births);
                if(!status.ok()) {fail(status,"execution_C_only_topology");return;}
            }
            if(execution.trace_retirement.step==Sc6ReplayTraceState::Retirement::Step::Empty) {
                const auto planned=execution.traces.PrepareAddedRetirement(transaction.SurvivingTraces(),*transaction.undo.traces,
                    execution.births,execution.trace_retirement,transaction.traces.PrivateChildren());
                if(!planned.ok()) {fail(planned,"execution_C_trace_retirement_preflight");return;}
            }
            if(!execution.retirement.admitted) {
                // Idle builders do not cover a raw provider-to-MID borrow.
                // Keep the native destructive call sites behind the separate
                // ownership veto even if reached below Request/Prepare.
                if(!CheckHistoricalMaterialBoundary(true))return;
                const auto trace_retired=transaction.undo.traces->RetireHiddenRenderingForUndo(execution.traces,transaction.SurvivingTraces(),execution.trace_render_retirement,transaction.traces.PrivateChildren());
                if(!trace_retired.ok()) {fail(trace_retired,"execution_C_trace_render_retirement");return;}
            }
            if(!CheckHistoricalMaterialBoundary(true))return;
            const auto retired=execution.births.RetireCurrentForUndo(transaction.undo.vfx,execution.retirement);
            if(!retired.ok()) {fail(retired,"execution_C_only_retirement");return;}
            if(!CheckHistoricalMaterialBoundary(true))return;
            const auto trace_destroyed=execution.traces.RetireAddedForUndo(*transaction.undo.traces,execution.trace_retirement);
            if(!trace_destroyed.ok()) {fail(trace_destroyed,"execution_C_trace_retirement");return;}
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] seek C-only retirement owners={} completed={} B_retained=true native_render_drain_required=true\n"),
                execution.births.members().size(),execution.retirement.completed);
        }
        if(queue(ParticleCopyAction::DrainExecutionWork)) execution.render=Render::DrainPending;
        return;
    }
    if(execution.render==Render::DrainPending) {
        if(!copy.execution_work_complete) return;
        execution.render=Render::Drained;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] seek render drain completed tick={} recovery={} gpu_complete=true render_settled=false\n"),simulation_->continuation().tick,recovery);
    }
    if(execution.render==Render::Drained) {
        if(!CheckHistoricalMaterialBoundary())return;
        if(execution.capture!=HistoricalRestore::Execution::Capture::Current) {
            const auto captured=CaptureHistoricalExecution(false);
            if(!captured.ok()) {fail(captured,"execution_C_settlement_capture");return;}
        }
        if(!recovery && execution.settlement_failure_after==0) {
            if(!particle_copy_->CanRetireAfterExecutionDrain()
                || std::any_of(execution.participants.begin(),execution.participants.end(),[](auto phase){return phase!=HistoricalRestore::Execution::Participant::HandedOff;})) {
                fail(Status::failure(FailureCode::IllegalTransition),"drained_injection_admission");return;
            }
            execution.settlement_failure_after.reset();
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] seek drained cancellation boundary tick={} gpu_complete=true C_captured=true render_settled=false cpu_settled=0 B_retained=true\n"),simulation_->continuation().tick);
            fail(Status::failure(FailureCode::AdvanceFailed),"injected_render_drained");return;
        }
        if(!CheckHistoricalMaterialBoundary())return;
        queue(ParticleCopyAction::SettleExecution);return;
    }
    if(execution.render!=Render::Settled) {fail(Status::failure(FailureCode::IllegalTransition),"execution_render_settlement");return;}
    if(!CheckHistoricalMaterialBoundary())return;
    const auto settled=SettleHistoricalCpuExecution();
    if(!settled.ok()) {fail(settled,transaction.witness.participant?transaction.witness.participant:"execution_C_cpu_settlement");return;}
    const auto& continuation=simulation_->continuation();
    if(continuation.tick<transaction.target->execution.tick || continuation.interval<transaction.target->execution.interval) {
        fail(Status::failure(FailureCode::RestoreVerificationFailed),"execution_coordinate_accounting");return;
    }
    operation.executed_ticks=continuation.tick-transaction.target->execution.tick;
    operation.executed_intervals=continuation.interval-transaction.target->execution.interval;
    if(recovery) {
        if(!replay_rendering_->SetQuarantinedPrimitives({},0)
            || !execution.ownership.CompleteRecoveryTails(simulation_->continuation().tick)) {
            fail(Status::failure(FailureCode::UndoFailed),"execution_recovery_tail");return;
        }
        execution.quarantine=HistoricalRestore::Execution::Quarantine::Retired;
        operation.phase=RestoreOperationPhase::Recovering;
    } else {
        if(!execution.ownership.CompleteTargetTails(simulation_->continuation().tick)) {
            fail(Status::failure(FailureCode::IllegalTransition),"execution_target_tail");return;
        }
        PublishHistoricalOwnership();
        // The next held drive publishes the validated C image on the original
        // viewport and waits for its restoration-copy completion before commit.
        operation.pending=true;
    }
    PublishHistoricalOwnership();
}

Status Sc6ReplayHost::BeginHistoricalCpuExecution()
{
    if(!historical_restore_ || !historical_restore_->execution || !checkpoint_capture_
        || historical_restore_->witness.phase!=RestoreOperationPhase::Held
        || historical_restore_->witness.commit_decided || historical_restore_->cancel
        || !CheckBinding() || depth_ || surface_event_ || particle_command_pending_.load()
        || interior_phase_!=InteriorPhase::Holding || pause_boundary_!=PauseBoundary::CompletedApplication
        || !engine_idle() || !executor_.idle() || !executor_.arena_empty()
        || application_phase_!=ApplicationPhase::Idle || !simulation_->interval_complete())
        return Status::failure(FailureCode::IllegalTransition);
    auto& transaction=*historical_restore_;
    auto& execution=*transaction.execution;
    if(execution.render!=HistoricalRestore::Execution::Render::HandedOff
        || execution.quarantine!=HistoricalRestore::Execution::Quarantine::Installed || transaction.physics_queries_pending
        || execution.ownership.phase()!=ReplaySeekOwnership::Phase::APublished
        || std::any_of(execution.participants.begin(),execution.participants.end(),[](auto v){return v!=HistoricalRestore::Execution::Participant::Retained;}))
        return Status::failure(FailureCode::IllegalTransition);
    const auto& a=*transaction.target;const auto& b=transaction.undo;
    for(const auto& birth:transaction.birth.members()) {
        const auto status=birth.ValidateQuarantined();if(!status.ok()) return status;
    }
    // Bounded retirement policy, charged before the first handoff. A failed
    // admission never authorizes a native tick. The extra capacity permits
    // growth; each participant independently validates its actual C graph.
    constexpr std::size_t growth=1024*1024;
    const std::array<std::size_t,11> budgets{
        transaction.manager.owned_bytes()+growth,transaction.cpu.owned_bytes()+growth,
        transaction.gpu.owned_bytes()+growth,transaction.pools.owned_bytes()+growth,
        transaction.world.owned_bytes()+growth,transaction.world.owned_bytes()+growth,
        transaction.scheduler.owned_bytes()+growth,transaction.traces.owned_bytes()+growth,
        transaction.vfx_handler.owned_bytes()+2*sizeof(ReplayVfxHandlerStorage::Graph)+growth,
        checkpoint_capture_->EnclosingWindExecutionBudget(),transaction.source_registration.owned_bytes()};
    auto available=AdmissionRemaining();
    for(const auto bytes:budgets) {
        if(bytes>available) return Status::failure(FailureCode::CapacityExceeded);
        available-=bytes;
    }
    const auto handoff=[&](unsigned index,const char* name,auto action) {
        const auto status=action();
        execution.participants[index]=status.ok()?HistoricalRestore::Execution::Participant::HandedOff:HistoricalRestore::Execution::Participant::Retained;
        if(!status.ok()) {
            transaction.witness.participant=name;transaction.witness.failure=status.code;
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] seek execution handoff failed participant={} code={} B_retained=true\n"),
                RC::to_generic_string(name),static_cast<unsigned>(status.code));
        }
        return status;
    };
    auto status=handoff(0,"manager",[&]{return transaction.manager.BeginExecution(budgets[0]);});
    if(status.ok()) {
        status=handoff(1,"cpu",[&]{return transaction.cpu.BeginExecution(b.vfx,budgets[1],&particle_copy_->retirement_completion());});
        execution.participants[1]=transaction.cpu.execution_started()?HistoricalRestore::Execution::Participant::HandedOff:HistoricalRestore::Execution::Participant::Retained;
    }
    if(status.ok()) {
        status=handoff(2,"gpu",[&]{return transaction.gpu.BeginExecution(b.vfx,budgets[2],&particle_copy_->retirement_completion());});
        execution.participants[2]=transaction.gpu.execution_started()?HistoricalRestore::Execution::Participant::HandedOff:HistoricalRestore::Execution::Participant::Retained;
    }
    if(status.ok()) status=handoff(3,"tile_pools",[&]{return a.vfx.BeginTilePoolExecution(b.vfx,transaction.pools,budgets[3]);});
    if(status.ok()) status=handoff(4,"world",[&]{return transaction.world.BeginExecution(budgets[4],&transaction.undo.scheduler,&transaction.scheduler);});
    if(status.ok()) status=handoff(5,"render_work",[&]{return transaction.world.BeginRenderWorkExecution(budgets[5]);});
    if(status.ok()) status=handoff(6,"scheduler",[&]{return transaction.TargetScheduler().BeginExecution(image_base_,world_,transaction.scheduler,budgets[6]);});
    if(status.ok()) status=handoff(7,"traces",[&]{return transaction.traces.BeginExecution(budgets[7],&particle_copy_->retirement_completion());});
    if(status.ok()) status=handoff(8,"handler",[&]{return transaction.vfx_handler.BeginExecution(budgets[8]);});
    if(status.ok()) status=handoff(9,"wind",[&]{return checkpoint_capture_->BeginEnclosingWindExecution(budgets[9]);});
    if(status.ok()) status=handoff(10,"source_registration",[&]{return transaction.source_registration.BeginExecution(budgets[10]);});
    if(status.ok()) status=transaction.physics_markers.BeginExecution();
    // This is CPU ownership only. The caller still validates the complete
    // render handoff and all admission budgets before marking admitted.
    return status;
}

bool Sc6ReplayHost::ArmSettlementFailure(std::uint64_t participants) noexcept
{
    if(!historical_restore_ || !historical_restore_->execution || !HistoricalExecutionAdmitted()
        || participants>historical_restore_->execution->participants.size() || historical_restore_->execution->settlement_failure_after
        || historical_restore_->witness.commit_decided || !CheckBinding())return false;
    historical_restore_->execution->settlement_failure_after=static_cast<unsigned>(participants);
    return true;
}

Status Sc6ReplayHost::SettleHistoricalCpuExecution()
{
    if(!historical_restore_ || !historical_restore_->execution || !historical_restore_->physics_observation
        || !CheckBinding() || depth_ || !engine_idle() || !executor_.idle() || !executor_.arena_empty()
        || application_phase_!=ApplicationPhase::Idle || !simulation_->interval_complete())
        return Status::failure(FailureCode::IllegalTransition);
    auto& transaction=*historical_restore_;auto& execution=*transaction.execution;
    if(execution.capture!=HistoricalRestore::Execution::Capture::Current
        || execution.render!=HistoricalRestore::Execution::Render::Settled) return Status::failure(FailureCode::IllegalTransition);
    const auto phase=execution.ownership.phase();
    if(phase!=ReplaySeekOwnership::Phase::RecoveryQuiescing && phase!=ReplaySeekOwnership::Phase::CompletingTargetTails && phase!=ReplaySeekOwnership::Phase::CompletingResumedTails)
        return Status::failure(FailureCode::IllegalTransition);
    const auto purpose=(phase==ReplaySeekOwnership::Phase::CompletingTargetTails || phase==ReplaySeekOwnership::Phase::CompletingResumedTails)
        ? RestoreSettlement::CommitCurrent : RestoreSettlement::RecoverOriginal;
    const auto& a=*transaction.target;const auto& b=transaction.undo;
    const auto settle=[&](unsigned index,const char* name,auto action) {
        if(execution.participants[index]!=HistoricalRestore::Execution::Participant::HandedOff) return Status::success();
        const auto status=action();if(status.ok()) execution.participants[index]=HistoricalRestore::Execution::Participant::Settled;
        if(!status.ok()) {
            transaction.witness.participant=name;transaction.witness.failure=status.code;
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] seek CPU settlement rejected participant={} code={} recovery={} B_retained=true\n"),
                RC::to_generic_string(name),static_cast<unsigned>(status.code),purpose==RestoreSettlement::RecoverOriginal);
        }
        if(status.ok() && purpose==RestoreSettlement::CommitCurrent && execution.settlement_failure_after
            && std::count(execution.participants.begin(),execution.participants.end(),HistoricalRestore::Execution::Participant::Settled)
                ==*execution.settlement_failure_after) {
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] seek settlement failure injected tick={} participants={} B_retained=true before_commit=true\n"),
                simulation_->continuation().tick,*execution.settlement_failure_after);
            execution.settlement_failure_after.reset();return Status::failure(FailureCode::AdvanceFailed);
        }
        return status;
    };
    // World settlement establishes the actual physical epoch before render
    // queue settlement. No global clock or historical image is rewritten.
    auto status=settle(4,"world_C",[&]{
        const auto result=transaction.world.SettleExecution();
        if(!result.ok()) {
            const auto d=transaction.world.binding_diagnostic();
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] world settlement binding check={} actor={:x} component={} offset={} expected={} observed={} registration_rva={:x} primary_flags={:x} base_registration={} secondary_flags={:x} expected_primary={:x} expected_secondary={:x}\n"),
                RC::to_generic_string(d.check?d.check:"unknown"),d.actor,d.component,d.offset,d.expected,d.observed,d.registration_entry,d.primary_tick_flags,d.base_registration,d.secondary_tick_flags,d.expected_primary_flags,d.expected_secondary_flags);
        }
        return result;
    });
    if(status.ok()) status=settle(5,"render_work_C",[&]{
        const auto result=transaction.world.SettleRenderWorkExecution(*transaction.physics_observation);
        if(!result.ok()) {
            const auto d=transaction.world.render_work_diagnostic();
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] render work settlement check={} component={:x} observed={} expected={} counts={}/{} tick={} checkpoint={} interval={} checkpoint_interval={} B_retained=true\n"),
                RC::to_generic_string(d.check?d.check:"unknown"),d.component,d.observed,d.expected,
                transaction.physics_observation->render_work_counts[0],transaction.physics_observation->render_work_counts[1],
                simulation_->continuation().tick,a.execution.tick,simulation_->continuation().interval,a.execution.interval);
        }
        return result;
    });
    const auto settle_emitters=[&](auto& owner,const char* kind) {
        Sc6ReplayVfxState::PreparedEmitterSet::SettlementFailure failure{};
        const auto result=owner.SettleExecution(execution.current,&failure);
        if(!result.ok()) {
            const auto& detail=failure.detail;
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] emitter settlement rejected kind={} component={:x} ordinal={} phase={} check={} offset={:x} expected={:x} actual={:x} code={} observed_member_flags={:x} B_retained=true\n"),
                RC::to_generic_string(kind),reinterpret_cast<std::uintptr_t>(failure.component),failure.ordinal,
                RC::to_generic_string(detail.phase?detail.phase:"set_admission"),RC::to_generic_string(detail.check?detail.check:"none"),
                detail.offset,detail.expected,detail.actual,static_cast<unsigned>(result.code),failure.observed_member_flags);
        } else for(std::size_t i=0;i<owner.size();++i) {
            if(owner.replacement(i)->native_destroyed())
                RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] emitter native retirement settled kind={} component={:x} ordinal={} destructor_completed=true slot_null=true B_retained=true C_freed_by_native=true\n"),
                    RC::to_generic_string(kind),reinterpret_cast<std::uintptr_t>(owner.component(i)),owner.ordinal(i));
        }
        return result;
    };
    if(status.ok()) status=settle(0,"manager_C",[&]{
        const auto result=transaction.manager.SettleExecution(execution.current,purpose,&transaction.gpu,&transaction.cpu);
        if(!result.ok())for(const auto& binding:transaction.particle_bindings) {
            if(binding.identity.source==binding.identity.target)continue;
            const auto counts=transaction.target->vfx.ParticleEmitterCounts(binding.identity.source);
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] manager settlement owner source={:x} target={:x} weak={}/{} CPU={} GPU={} observed={} gpu_death={} cpu_death={} captured_metadata_only=true B_retained=true\n"),
                binding.identity.source,binding.identity.target,binding.identity.target_weak[0],binding.identity.target_weak[1],counts[0],counts[1],
                execution.current.RetainsComponent(reinterpret_cast<void*>(binding.identity.target)),
                transaction.gpu.FreshComponentDeath(binding.identity.target,binding.identity.target_weak),
                transaction.cpu.FreshComponentDeath(binding.identity.target,binding.identity.target_weak));
        }
        return result;
    });
    if(status.ok()) status=settle(1,"cpu_C",[&]{return settle_emitters(transaction.cpu,"cpu");});
    if(status.ok()) status=settle(2,"gpu_C",[&]{return settle_emitters(transaction.gpu,"gpu");});
    if(status.ok()) status=SettleFreshParticleOwners();
    if(status.ok()) status=settle(3,"pools_C",[&]{return a.vfx.SettleTilePoolExecution(b.vfx,execution.current,transaction.pools);});
    if(status.ok()) status=settle(6,"scheduler_C",[&]{
        Sc6ReplaySchedulerState::PublishedFailure failure{};
        const auto result=transaction.TargetScheduler().SettleExecution(execution.scheduler,image_base_,world_,transaction.scheduler,purpose,&failure);
        if(!result.ok()) RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] scheduler settlement rejected check={} address={:x} owner={:x} code={} B_retained=true\n"),
            RC::to_generic_string(failure.check?failure.check:"unknown"),failure.address,failure.owner,static_cast<unsigned>(result.code));
        return result;
    });
    if(status.ok()) status=settle(7,"traces_C",[&] {
        Sc6ReplayVfxState::ParticleBirthSet particles;
        if(execution.traces.HasAddedStates(*transaction.undo.traces)) {
            Sc6ReplayVfxState::Topology topology{};
            const auto prepared=transaction.particle_bindings.empty()
                ? transaction.target->vfx.PrepareTopology(execution.current,manager_,topology,particles)
                : transaction.undo.vfx.PrepareExecutionParticles(execution.current,manager_,particles);
            if(!prepared.ok())return prepared;
        }
        return transaction.traces.SettleExecution(execution.traces,&particles);
    });
    if(status.ok()) status=settle(8,"handler_C",[&]{return transaction.vfx_handler.SettleExecution();});
    if(status.ok()) status=settle(9,"wind_C",[&]{return checkpoint_capture_->SettleEnclosingWindExecution();});
    if(status.ok()) status=settle(10,"source_registration_C",[&]{return transaction.source_registration.SettleExecution();});
    return status;
}

Status Sc6ReplayHost::ReopenHistoricalCpuForUndo()
{
    if(!historical_restore_ || !historical_restore_->execution || !checkpoint_capture_
        || !CheckBinding() || depth_ || surface_event_ || particle_command_pending_.load()
        || !engine_idle() || !executor_.idle() || !executor_.arena_empty()
        || application_phase_!=ApplicationPhase::Idle || !simulation_->interval_complete())
        return Status::failure(FailureCode::IllegalTransition);
    auto& transaction=*historical_restore_;auto& execution=*transaction.execution;
    if(execution.ownership.phase()!=ReplaySeekOwnership::Phase::RecoveryQuiescing
        || execution.render!=HistoricalRestore::Execution::Render::Settled)
        return Status::failure(FailureCode::IllegalTransition);
    const auto& a=*transaction.target;const auto& b=transaction.undo;
    const auto reopen=[&](unsigned index,const char* name,auto action) {
        if(execution.participants[index]==HistoricalRestore::Execution::Participant::Retained)
            return Status::success();
        // A participant can be internally settled even when its final check
        // failed. Every begun owner handles this idempotently; the existing
        // journal prevents gameplay admission during a partial reopening.
        auto status=action();
        if(status.ok())execution.participants[index]=HistoricalRestore::Execution::Participant::HandedOff;
        else {transaction.witness.participant=name;transaction.witness.failure=status.code;}
        return status;
    };
    auto status=reopen(5,"render_work_reopen",[&]{return transaction.world.ReopenRenderWorkForUndo();});
    if(status.ok())status=reopen(4,"world_reopen",[&]{return transaction.world.ReopenExecutionForUndo();});
    if(status.ok())status=reopen(0,"manager_reopen",[&]{return transaction.manager.ReopenExecutionForUndo(a.vfx);});
    if(status.ok())status=reopen(1,"cpu_reopen",[&]{return transaction.cpu.ReopenExecutionForUndo();});
    if(status.ok())status=reopen(2,"gpu_reopen",[&]{return transaction.gpu.ReopenExecutionForUndo();});
    if(status.ok())status=reopen(3,"pools_reopen",[&]{return a.vfx.ReopenTilePoolsForUndo(b.vfx,transaction.pools);});
    if(status.ok())status=reopen(6,"scheduler_reopen",[&]{return transaction.scheduler.ReopenExecutionForUndo(transaction.TargetScheduler());});
    if(status.ok())status=reopen(7,"traces_reopen",[&]{return a.traces?transaction.traces.ReopenExecutionForUndo(transaction.TargetTraces()):Status::failure(FailureCode::ContextUnavailable);});
    if(status.ok())status=reopen(8,"handler_reopen",[&]{return transaction.vfx_handler.ReopenExecutionForUndo();});
    if(status.ok())status=reopen(10,"source_registration_reopen",[&]{return transaction.source_registration.ReopenExecution();});
    if(status.ok())status=reopen(9,"wind_reopen",[&]{return checkpoint_capture_->ReopenEnclosingWindForUndo();});
    if(status.ok())RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] seek CPU settlement reopened B_retained=true native_allocations_freed=0\n"));
    return status;
}

Status Sc6ReplayHost::PrepareHistoricalPhysicsForExecution() noexcept
{
    if (!historical_restore_ || !historical_restore_->target || !historical_restore_->physics_observation)
        return Status::failure(FailureCode::IllegalTransition);
    auto& transaction=*historical_restore_;
    if (!transaction.physics_queries_pending) return Status::success();
    const auto& target=*transaction.target;
    auto status=transaction.world.ValidateRenderWork();
    if (status.ok()) status=Sc6ReplayWorldState::ReconstructPhysicsQueries(target.physics);
    if (status.ok()) status=Sc6ReplayWorldState::ReadPhysicsBoundary(image_base_,world_,*transaction.physics_observation);
    if (status.ok()) status=Sc6ReplayWorldState::ValidatePhysicsProjection(target.physics,*transaction.physics_observation,true,true);
    if (!status.ok()) return status;
    // Reversible preparation: B's query projection and end-frame backing
    // remain owned. Publishing transforms does not retire any B participant.
    __try {
        for (unsigned scene=0;scene<target.physics.scenes.size();++scene)
            for (unsigned i=0;i<target.physics.observed_actors[scene];++i)
                if (const auto& actor=target.physics.actors[scene][i];actor.mutable_projection() && actor.component)
                    reinterpret_cast<void(*)(void*)>(image_base_+0x1d4e960)(reinterpret_cast<void*>(actor.component));
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::ContextUnavailable);}
    transaction.physics_queries_pending=false;
    return Status::success();
}

void Sc6ReplayHost::AdvanceHistoricalRestore() noexcept
{
    // Render publication is driven by the same admitted hold owner. Do not
    // drain an engine call to fake asynchronous completion.
    if (!historical_restore_ || surface_event_ || particle_command_pending_.load()) return;
    if(!CheckHistoricalMaterialBoundary())return;
    // Corrected capture owns command scheduling, but never exempts the
    // transaction from material classification or sticky terminal failure.
    if(corrected_capture_)return;
    if(historical_restore_->preparing) {
        AdvanceRestorePreparation();return;
    }
    auto& transaction = *historical_restore_;
    auto& operation = transaction.witness;
    if (operation.phase == RestoreOperationPhase::Empty) return;
    auto copy = particle_copy_->witness();
    if (operation.phase != RestoreOperationPhase::Failed && operation.phase != RestoreOperationPhase::Committed
        && transaction.undo.valid && callback_admission_) {
        const auto admission=callback_admission_->Validate(transaction.undo.callback_stamp);
        if (!admission.ok()) {
            operation.failure=admission.code; operation.participant="callback_admission";
            operation.phase=RestoreOperationPhase::Failed; interior_phase_=InteriorPhase::Failed;
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical callback lease invalidated code={} owners_retained=true\n"),static_cast<unsigned>(admission.code));
        }
    }
    if (operation.phase == RestoreOperationPhase::Failed || interior_phase_ == InteriorPhase::Failed)
    {
        // A failed binding forbids CPU recovery writes. It does not cancel
        // submitted GPU work or authorize freeing its retained owners.
        if (copy.pending) {
            bool pending{};
            ParticleCopyExperiment(ParticleCopyAction::RetireInFlight, &copy, &pending);
            operation.pending = pending || copy.pending;
        }
        else operation.pending = false;
        return;
    }
    if (particle_command_failed_.load() && operation.phase == RestoreOperationPhase::Committing)
    { operation.failure = FailureCode::PresentationFailed; operation.phase = RestoreOperationPhase::Failed; interior_phase_ = InteriorPhase::Failed; return; }
    if (particle_command_failed_.load() && operation.phase != RestoreOperationPhase::Recovering
        && operation.phase != RestoreOperationPhase::Recovered)
    { operation.failure = FailureCode::PresentationFailed; operation.phase = RestoreOperationPhase::Recovering; }
    const auto queue = [&](ParticleCopyAction action) {
        bool pending{};
        const bool accepted = ParticleCopyExperiment(action, &copy, &pending);
        operation.pending = pending;
        if (!accepted)
        {
            operation.failure = FailureCode::PresentationFailed;
            if (operation.phase == RestoreOperationPhase::Recovering || operation.phase == RestoreOperationPhase::Committing)
            { operation.phase = RestoreOperationPhase::Failed; interior_phase_ = InteriorPhase::Failed; }
            else operation.phase = RestoreOperationPhase::Recovering;
        }
        return accepted;
    };
    operation.pending = copy.pending;
    if (copy.pending) { queue(ParticleCopyAction::Poll); return; }
    if (Horse::GameImGui::PresentHook::instance().replay_retirement_pending()) {
        operation.pending = QueueSurface(SurfaceCommand::RetireHeldDisplay);
        return;
    }
    if(transaction.execution && operation.phase==RestoreOperationPhase::Recovering
        && transaction.execution->ownership.phase()!=ReplaySeekOwnership::Phase::Recovering) {
        if(!transaction.execution->ownership.BeginRecovery()) {
            operation.failure=FailureCode::UndoFailed;operation.phase=RestoreOperationPhase::Failed;return;
        }
        operation.phase=RestoreOperationPhase::Held;
    }
    if(transaction.execution && operation.phase==RestoreOperationPhase::Held) {
        if(transaction.cancel && !transaction.execution->ownership.BeginRecovery()) {
            operation.failure=FailureCode::IllegalTransition;operation.phase=RestoreOperationPhase::Failed;return;
        }
        AdvanceHistoricalExecution();
        PublishHistoricalOwnership();
        if(operation.phase==RestoreOperationPhase::Held || operation.phase==RestoreOperationPhase::Failed) return;
    }
    if (transaction.cancel && operation.phase != RestoreOperationPhase::Recovered)
    {
        if (operation.failure == FailureCode::None) operation.failure = FailureCode::Cancelled;
        operation.phase = RestoreOperationPhase::Recovering;
    }
    if (operation.phase == RestoreOperationPhase::Prepared)
    {
        if (operation.particle_birth && !copy.birth_prepared) queue(ParticleCopyAction::PrepareBirthRegistration);
        return;
    }
    if (operation.phase == RestoreOperationPhase::Publishing)
    {
        if (!transaction.gpu_requested)
        { transaction.gpu_requested = queue(ParticleCopyAction::InstallState); return; }
        if (copy.phase != Sc6ReplayParticleCopy::Phase::Installed || !copy.installed_copy_complete
            || operation.particle_birth != copy.birth_excluded)
        { operation.failure = FailureCode::PresentationFailed; operation.phase = RestoreOperationPhase::Recovering; return; }
        if (!transaction.surface_requested)
        {
            transaction.surface_requested = QueueSurface(SurfaceCommand::InstallCheckpoint);
            operation.pending = transaction.surface_requested;
            if (!transaction.surface_requested)
            { operation.failure = FailureCode::PresentationFailed; operation.phase = RestoreOperationPhase::Recovering; }
            return;
        }
        if (!transaction.surface_dirty)
        { operation.failure = FailureCode::PresentationFailed; operation.phase = RestoreOperationPhase::Recovering; return; }
        operation.phase = RestoreOperationPhase::Held;
        if(transaction.execution && !transaction.execution->ownership.PublishA()) {
            operation.failure=FailureCode::IllegalTransition;
            operation.phase=RestoreOperationPhase::Recovering;
            return;
        }
        if(transaction.execution) PublishHistoricalOwnership();
        operation.pending = false;
        return;
    }
    if (operation.phase == RestoreOperationPhase::Recovering)
    {
        if (copy.native_write_uncommitted && !particle_copy_->needs_cpu_reconstruction())
        {
            // Seal before GPU undo consumes coordinate retirement receipts.
            // After that copy completes its phase is Recovered, not Installed;
            // CPU undo must not attempt to seal live or already-restored state.
            if(transaction.execution && !transaction.particle_bindings.empty()
                && !particle_copy_->SealFreshCoordinateRetirement(transaction.gpu)) {
                operation.failure=FailureCode::UndoFailed;operation.participant="fresh_coordinate_retirement";
                operation.phase=RestoreOperationPhase::Failed;interior_phase_=InteriorPhase::Failed;return;
            }
            queue(ParticleCopyAction::RestoreUndo);return;
        }
        if (transaction.surface_dirty)
        {
            operation.pending = QueueSurface(SurfaceCommand::UndoCheckpoint);
            if (!operation.pending)
            { operation.failure = FailureCode::UndoFailed; operation.phase = RestoreOperationPhase::Failed; interior_phase_ = InteriorPhase::Failed; }
            return;
        }
        UndoHistoricalRestore();
        return;
    }
    if (operation.phase == RestoreOperationPhase::Recovered)
    {
        if(particle_copy_->needs_cpu_reconstruction()) {
            queue(ParticleCopyAction::CompleteNativeReconstruction);return;
        }
        // Publication can create private A owners without handing them to
        // native execution. CPU/GPU undo restored B, but these allocations
        // still require their original inactive-owner retirement and a fresh
        // GPU completion receipt before Finish releases the copy owner.
        // Finish clears the copy's transient execution flag. Once Released,
        // executed owners must continue through their retained settlement and
        // native-death receipts below, never private allocation teardown.
        if(transaction.execution && copy.phase!=Sc6ReplayParticleCopy::Phase::Released
            && !particle_copy_->execution_started()) {
            bool owners_pending{};
            const auto owners=RetirePreparedFreshParticles(owners_pending);
            if(!owners.ok()) {
                operation.failure=owners.code;operation.participant="undo_unexecuted_owner_retirement";
                operation.phase=RestoreOperationPhase::Failed;interior_phase_=InteriorPhase::Failed;return;
            }
            if(owners_pending) {operation.pending=true;return;}
        }
        if (copy.phase != Sc6ReplayParticleCopy::Phase::Released)
        { queue(ParticleCopyAction::Finish); return; }
        if(transaction.execution && transaction.display_started && !transaction.display_finished) {
            if(!transaction.display_finish_requested) {
                transaction.display_finish_requested=QueueSurface(SurfaceCommand::FinishDisplayRecovery);
                operation.pending=transaction.display_finish_requested;
            }
            if(!transaction.display_finish_requested) {
                operation.failure=FailureCode::UndoFailed;operation.phase=RestoreOperationPhase::Failed;
            }
            return;
        }
        const auto ground=transaction.ground.RecoverBodies();
        if(ground!=Sc6ReplayGroundDebrisState::Progress::Complete) {
            operation.pending=true;
            if(ground!=Sc6ReplayGroundDebrisState::Progress::Pending) {
                operation.failure=FailureCode::UndoFailed;operation.participant=transaction.ground.failed_check();
                operation.phase=RestoreOperationPhase::Failed;interior_phase_=InteriorPhase::Failed;
            }
            return;
        }
        const auto fresh_release=ReleaseFreshParticleOwnership(false);
        if(!fresh_release.ok()) {operation.failure=fresh_release.code;operation.participant="undo_fresh_owner_retirement";return;}
        const auto ground_release=transaction.ground.ReleaseRecoveredOwners();
        if(!ground_release.ok()) {operation.failure=ground_release.code;operation.participant="undo_ground_retirement";return;}
        if(transaction.execution && (transaction.execution->protected_particle_routes.load()&4u)) {
            // Native B reconstruction unexpectedly attempted completion or
            // destruction of a protected owner. Keep B/resources retained;
            // suppressed callbacks cannot count as successful recovery.
            operation.failure=FailureCode::UndoFailed;operation.participant="undo_protected_particle_lifetime";
            operation.phase=RestoreOperationPhase::Failed;interior_phase_=InteriorPhase::Failed;return;
        }
        if(transaction.execution && transaction.execution->ownership.phase()!=ReplaySeekOwnership::Phase::Recovered) {
            if(!transaction.execution->ownership.CompleteRecovery(simulation_->continuation().tick)) {
                operation.failure=FailureCode::UndoFailed;operation.phase=RestoreOperationPhase::Failed;
                interior_phase_=InteriorPhase::Failed;return;
            }
            PublishHistoricalOwnership();
        }
        operation.original_recovered=simulation_->continuation().tick==operation.original_tick;
        if(!operation.original_recovered) {
            operation.failure=FailureCode::UndoFailed;operation.phase=RestoreOperationPhase::Failed;
            interior_phase_=InteriorPhase::Failed;return;
        }
        index_.SettleSourceRevision(input_source_->revision_id());
        checkpoint_restoring_ = false;
        operation.pending = false;
    }
    if (operation.phase == RestoreOperationPhase::Committing)
    {
        if (!transaction.commit_requested)
        { transaction.commit_requested = queue(ParticleCopyAction::CommitState); return; }
        if (!transaction.native_retired)
        {
            if (copy.phase != Sc6ReplayParticleCopy::Phase::Committed)
            { operation.failure = FailureCode::PresentationFailed; operation.phase = RestoreOperationPhase::Failed; interior_phase_ = InteriorPhase::Failed; return; }
            const auto& target = *transaction.target;
            auto& current = transaction.undo;
            auto status = Status::success();
            const auto retire = [&](unsigned cursor, auto action) {
                if (!status.ok() || transaction.retirement_cursor > cursor) return;
                if(!CheckHistoricalMaterialBoundary()) {status=Status::failure(FailureCode::UnsupportedContent);return;}
                if (transaction.retirement_cursor != cursor) {
                    status = Status::failure(FailureCode::IllegalTransition); return;
                }
                status = action();
                if (status.ok()) ++transaction.retirement_cursor;
            };
            status = PrepareHistoricalPhysicsForExecution();
            retire(0, [&] { return transaction.cpu.Commit(current.vfx); });
            retire(1, [&] { return transaction.gpu.Commit(current.vfx); });
            retire(2, [&] { return target.vfx.CommitTilePools(current.vfx, transaction.pools); });
            retire(3, [&] { return transaction.world.CommitRenderWork(); });
            retire(4, [&] { return transaction.world.Commit(); });
            retire(5, [&] { return transaction.TargetScheduler().CommitBacking(image_base_, world_, transaction.scheduler); });
            retire(6, [&] { return transaction.manager.Commit(); });
            // A displaced particle may still reference the B trace attachment.
            // Match C-undo ownership: retire the particle consumer before native
            // trace actor/attachment destruction. The final ordered GPU drain
            // still precedes releasing either image's UObject/RHI leases.
            retire(7, [&] { return transaction.vfx_handler.Commit(); });
            retire(8, [&] { return !operation.particle_birth ? Status::success()
                : transaction.execution ? transaction.birth.RetireQuarantinedAfterCommit() : transaction.birth.RetireAfterCommit(); });
            retire(9, [&] { return transaction.traces.Commit(); });
            retire(10, [&] { return checkpoint_capture_->FinishEnclosingWind(); });
            retire(11, [&] { return transaction.physics_markers.Commit(); });
            retire(12, [&] { return transaction.source_registration.Commit(); });
            retire(13, [&] { return transaction.hud.Commit()?Status::success():Status::failure(FailureCode::RestorePreflightFailed); });
            retire(14, [&] { return transaction.ground.CommitNativeOwners(); });
            if (!status.ok())
            { operation.failure = status.code; operation.phase = RestoreOperationPhase::Failed; interior_phase_ = InteriorPhase::Failed;
                RC::Output::send<RC::LogLevel::Error>(STR("[HorseMod] historical commit retirement failed cursor={} code={} B_recovery_available=false\n"),
                    transaction.retirement_cursor, static_cast<unsigned>(status.code)); return; }
            transaction.native_retired = true;
        }
        if (!transaction.retirement_requested)
        { transaction.retirement_requested = queue(ParticleCopyAction::CompleteRetirement); return; }
        if (copy.phase == Sc6ReplayParticleCopy::Phase::RetiredCommit)
        { queue(ParticleCopyAction::Finish); return; }
        if (copy.phase != Sc6ReplayParticleCopy::Phase::Released)
        { operation.failure = FailureCode::PresentationFailed; operation.phase = RestoreOperationPhase::Failed; interior_phase_ = InteriorPhase::Failed; return; }
        if(transaction.execution && !transaction.display_finished) {
            if(!transaction.display_finish_requested) {
                transaction.display_finish_requested=QueueSurface(SurfaceCommand::FinishDisplayCommit);
                operation.pending=transaction.display_finish_requested;
            }
            if(!transaction.display_finish_requested) {
                operation.failure=FailureCode::PresentationFailed;operation.phase=RestoreOperationPhase::Failed;
            }
            return;
        }
        // Native debris destruction can enqueue proxy/RHI work. Its GC leases
        // outlive the ordered CompleteRetirement drain and render-thread Finish.
        const auto fresh_release=ReleaseFreshParticleOwnership(true);
        if(!fresh_release.ok()) {
            if(fresh_release.code==FailureCode::ContextUnavailable)return;
            operation.failure=fresh_release.code;operation.participant="commit_fresh_owner_adoption";
            operation.phase=RestoreOperationPhase::Failed;interior_phase_=InteriorPhase::Failed;return;
        }
        const auto ground_release=transaction.ground.ReleaseCommittedOwners();
        if(!ground_release.ok()) {
            if(ground_release.code==FailureCode::ContextUnavailable)return;
            operation.failure=ground_release.code;operation.phase=RestoreOperationPhase::Failed;interior_phase_=InteriorPhase::Failed;return;
        }
        if (transaction.prior_mode == UcrtRandBrokerMode::Observing && !checkpoint_broker_->ReleaseOwnership(thread_).ok())
        { operation.failure = FailureCode::UndoFailed; operation.phase = RestoreOperationPhase::Failed; interior_phase_ = InteriorPhase::Failed; return; }
        if(transaction.execution && (!replay_rendering_->SetQuarantinedPrimitives({},0)
            || !transaction.execution->ownership.CompleteCommit())) {
            operation.failure=FailureCode::PresentationFailed;operation.phase=RestoreOperationPhase::Failed;
            interior_phase_=InteriorPhase::Failed;return;
        }
        // Render-thread publication tracks the actual retained A or C image.
        // A zero-traversal commit has an execution owner but no fresh C frame;
        // retaining the old B metadata would reject the next complete B capture.
        held_surface_ = transaction.presented_surface;
        transaction.undo_surface = {};
        transaction.game_dirty = transaction.source_dirty = transaction.execution_dirty = transaction.surface_dirty = false;
        transaction.physics_dirty = false;
        transaction.rendering_dirty = transaction.hud_dirty = transaction.traces_dirty = transaction.vfx_handler_dirty = false;
        // B recovery leaves the original index intact. Invalidate completion
        // only when a different authored history actually commits.
        index_.SettleSourceRevision(input_source_->revision_id());
        operation.phase = RestoreOperationPhase::Committed;
        if(transaction.execution) PublishHistoricalOwnership();
        operation.pending = false;
        checkpoint_restoring_ = false;
    }
}

bool Sc6ReplayHost::CanReleaseHistoricalRestore() const noexcept
{
    if(!historical_restore_) return false;
    const auto& operation=*historical_restore_;
    if(operation.material_failed || NativeReplayMaterialTaskGuard::Inspect(operation.material).state
        !=NativeReplayMaterialTaskGuard::State::Clear)return false;
    return HistoricalNativeOwnersReleased();
}

bool Sc6ReplayHost::HistoricalNativeOwnersReleased() const noexcept
{
    if(!historical_restore_)return false;
    const auto& operation=*historical_restore_;
    return !checkpoint_restoring_ && !operation.witness.pending && !surface_event_
        && !Horse::GameImGui::PresentHook::instance().replay_retirement_pending()
        && !particle_command_pending_.load() && !operation.game_dirty && !operation.physics_dirty
        && !operation.source_dirty && !operation.execution_dirty && !operation.surface_dirty
        && !operation.rendering_dirty && !operation.hud_dirty && !operation.traces_dirty && !operation.vfx_handler_dirty
        && !operation.manager.published() && !operation.cpu.published() && !operation.gpu.published()
        && !operation.pools.published() && !operation.scheduler.published() && !operation.world.published()
        && !operation.world.render_published()
        && operation.physics_markers.released()
        && operation.ground.empty()
        && !operation.fresh_scheduler_transfer.published()
        && std::all_of(operation.fresh_particles.begin(),operation.fresh_particles.end(),[](const auto& owner){return owner && (owner->retired || owner->adopted);})
        && std::all_of(operation.fresh_trace_children.begin(),operation.fresh_trace_children.end(),[](const auto& owner){return owner && (owner->retired || owner->adopted);})
        && (!checkpoint_capture_ || !checkpoint_capture_->PendingEnclosingWind())
        && (!particle_copy_ || (!particle_copy_->witness().pending
            && !particle_copy_->witness().native_write_uncommitted && !particle_copy_->blocks_resume()));
}

bool Sc6ReplayHost::RestoreOperation(RestoreOperationAction action, const Checkpoint* target,
    RestoreOperationWitness* output) noexcept
{
    auto* self = active_;
    if (!self || !output || GetCurrentThreadId() != self->thread_) return false;
    *output = self->historical_restore_ ? self->historical_restore_->witness : RestoreOperationWitness{};
    if(self->SeekOwnsExecution() && !self->seek_driving_ && action!=RestoreOperationAction::Read) return false;
    switch (action) {
    case RestoreOperationAction::Begin:
    case RestoreOperationAction::Read:
    case RestoreOperationAction::Publish:
    case RestoreOperationAction::Cancel:
    case RestoreOperationAction::Commit:
    case RestoreOperationAction::Release:
    case RestoreOperationAction::Request:
    case RestoreOperationAction::BeginExecution:
    case RestoreOperationAction::SettleTarget:
    case RestoreOperationAction::CompleteTarget:
    case RestoreOperationAction::ResumeTarget: break;
    default: return false;
    }
    bool success = true;
    try
    {
        // Resolve public addresses through the accounted registry before
        // either asynchronous entry point reads target members. Keep the pin
        // until preparation has taken ownership; a catch cannot repair an
        // invalid native pointer dereference.
        CheckpointHandle admitted;
        if(action==RestoreOperationAction::Request || action==RestoreOperationAction::Begin) {
            if(self->index_.witness().phase==ReplayTickIndex::Phase::Releasing) return false;
            admitted=self->ResolveOwnedCheckpoint(target);
            if(!admitted) {output->failure=FailureCode::RestorePreflightFailed;return false;}
            target=admitted.get();
        }
        if (action == RestoreOperationAction::Request) {
            const auto status=target?self->RequestHistoricalRestore(*target):Status::failure(FailureCode::RestorePreflightFailed);
            success=status.ok();if(!success && !self->historical_restore_) output->failure=status.code;
        }
        else if (action == RestoreOperationAction::Begin) {
            if(self->historical_restore_ || !target) return false;
            const auto status=self->PrepareHistoricalRestore(*target);
            success=status.ok();if(!success && !self->historical_restore_) output->failure=status.code;
        }
        else if (!self->historical_restore_) return false;
        else if (action == RestoreOperationAction::Publish)
            success = self->PublishHistoricalRestore().ok();
        else if(action==RestoreOperationAction::BeginExecution)
            success=self->RequestHistoricalExecution().ok();
        else if(action==RestoreOperationAction::SettleTarget || action==RestoreOperationAction::CompleteTarget
            || action==RestoreOperationAction::ResumeTarget) {
            auto& transaction=*self->historical_restore_;
            if(!transaction.execution || !self->SeekOwnsExecution() || !self->seek_driving_
                || self->depth_ || !self->CheckBinding() || transaction.cancel
                || self->interior_phase_!=InteriorPhase::Holding
                || transaction.witness.phase!=RestoreOperationPhase::Held) return false;
            auto& owner=transaction.execution->ownership;
            if(action==RestoreOperationAction::SettleTarget) {
                if(self->tick_advance_.phase!=TickAdvancePhase::Held || self->tick_advance_.target!=owner.target_tick()) return false;
                success=owner.SettleC(self->simulation_->continuation().tick);
            } else {
                if(action==RestoreOperationAction::CompleteTarget && self->tick_advance_.phase==TickAdvancePhase::CompletionBlocked) return false;
                success=action==RestoreOperationAction::ResumeTarget?owner.BeginResumedTails():owner.BeginTargetTails();
            }
            if(success) {
                self->PublishHistoricalOwnership();
                transaction.witness.pending=action!=RestoreOperationAction::SettleTarget;
            }
        }
        else if (action == RestoreOperationAction::Cancel)
        {
            auto& transaction = *self->historical_restore_;
            if(!self->CheckHistoricalMaterialBoundary()) {*output=transaction.witness;return false;}
            const auto phase = transaction.witness.phase;
            if (transaction.witness.commit_decided) return false;
            if(transaction.execution) {
                if(!self->CheckBinding() || !transaction.undo.valid) return false;
                using Cancellation=ReplaySeekOwnership::Cancellation;
                const auto cancellation=transaction.execution->ownership.RequestCancellation();
                if(cancellation==Cancellation::Rejected) return false;
                if(cancellation==Cancellation::Recovered || cancellation==Cancellation::Undo) {
                    if(!transaction.witness.AcceptsIdempotentCancellation(cancellation)) return false;
                    *output=transaction.witness;return true;
                }
                // A new explicit recovery request may acknowledge an old,
                // completed command failure. A later recovery-command failure
                // must stop, not be automatically submitted on every update.
                if(!self->particle_command_pending_.load()) self->particle_command_failed_.store(false);
                transaction.cancel=true;
                self->PublishHistoricalOwnership();
                // The execution owner first quiesces C and settles its backing.
                // Publication-only Undo is not permitted to race native C.
                if(transaction.execution->ownership.phase()==ReplaySeekOwnership::Phase::RecoveryQuiescing)
                    transaction.witness.phase=RestoreOperationPhase::Held;
                *output=transaction.witness;return true;
            }
            if(transaction.preparing) {
                transaction.cancel=true;*output=transaction.witness;return true;
            }
            if (phase == RestoreOperationPhase::Failed) {
                // Request preparation has its own retirement phase and has
                // never published A. The completed original remains B even
                // when the requested checkpoint's owners expired. Do not
                // mistake that for permission to resume a partial installation.
                if(transaction.preparation_retiring && !transaction.preparing
                    && self->CanReleaseHistoricalRestore() && !self->depth_ && self->CheckBinding()
                    && self->interior_phase_==InteriorPhase::Holding
                    && self->pause_boundary_==PauseBoundary::CompletedApplication
                    && self->application_phase_==ApplicationPhase::Idle && self->engine_idle()
                    && self->executor_.idle() && self->simulation_->interval_complete()
                    && self->simulation_->continuation().tick==transaction.witness.original_tick) {
                    transaction.cancel=true;transaction.witness.phase=RestoreOperationPhase::Recovered;
                    transaction.witness.original_recovered=true;*output=transaction.witness;
                    return true;
                }
                // Only a reversible fault with the complete original image
                // and its unchanged native domain can retry B recovery.
                auto* image = reinterpret_cast<void*>(self->image_base_);
                if (!self->checkpoint_restoring_ || !transaction.target || !transaction.undo.valid
                    || !transaction.physics_observation || !transaction.undo.hud || !transaction.undo.traces || !transaction.undo.vfx_handler
                    || !self->CheckBinding() || !self->engine_idle() || !self->world_idle()
                    || self->application_phase_ != ApplicationPhase::Idle
                    || EngineField<std::uint64_t>(image, 0x4197170) != self->interior_epoch_
                    || EngineField<double>(image, 0x43b3778) != self->interior_wall_reference_) return false;
                transaction.cancel = true;
                transaction.witness.phase = RestoreOperationPhase::Recovering;
                self->interior_phase_ = InteriorPhase::Holding;
                *output = transaction.witness;
                return true;
            }
            if (phase != RestoreOperationPhase::Prepared && phase != RestoreOperationPhase::Publishing
                && phase != RestoreOperationPhase::Held && phase != RestoreOperationPhase::Recovering
                && phase != RestoreOperationPhase::Recovered) return false;
            self->historical_restore_->cancel = true;
        }
        else if (action == RestoreOperationAction::Release)
        {
            const auto& operation = *self->historical_restore_;
            const auto phase = operation.witness.phase;
            if ((phase != RestoreOperationPhase::Recovered && phase != RestoreOperationPhase::Committed
                    && phase != RestoreOperationPhase::Failed)
                || !self->CanReleaseHistoricalRestore()) return false;
            *output = operation.witness;
            self->historical_restore_.reset();
            return true;
        }
        else if (action == RestoreOperationAction::Commit)
        {
            auto& transaction = *self->historical_restore_;
            if(!self->CheckHistoricalMaterialBoundary()) {*output=transaction.witness;return false;}
            // Read-only native lifecycle admission precedes the irreversible
            // seek-wide decision. Rejection still permits complete B recovery.
            const auto markers_commit=transaction.physics_markers.ValidateCommit();
            if(!markers_commit.ok()) {
                const auto d=transaction.physics_markers.diagnostic();
                transaction.witness.failure=markers_commit.code;
                transaction.witness.participant="physics_markers_commit";
                RC::Output::send<RC::LogLevel::Error>(STR("[HorseMod] physics marker commit preflight rejected check={} code={} owner={:x} row={} source={} expected={:x} observed={:x} B_retained=true commit_decided=false\n"),
                    RC::to_generic_string(d.check),static_cast<unsigned>(markers_commit.code),d.owner,d.row,d.source,d.expected,d.observed);
                *output=transaction.witness;return false;
            }
            const auto ground_commit=transaction.ground.PrepareCommit();
            if(!ground_commit.ok()) {
                transaction.witness.failure=ground_commit.code;
                transaction.witness.participant=transaction.ground.failed_check();
                RC::Output::send<RC::LogLevel::Error>(STR("[HorseMod] ground commit preflight rejected check={} code={} target={} current_tick={} B_retained=true commit_decided=false native_disposal_started=false\n"),
                    RC::to_generic_string(transaction.ground.failed_check()),static_cast<unsigned>(ground_commit.code),
                    transaction.execution?transaction.execution->ownership.target_tick():transaction.witness.target_tick,self->simulation_?self->simulation_->continuation().tick:0);
                *output=transaction.witness;return false;
            }
            // A host seek owns B through execution and target tail completion.
            // The older direct restore diagnostic has no seek-wide executor.
            // Its commit contract cannot authorize an early host commit.
            if(transaction.execution
                && (transaction.execution->ownership.phase()!=ReplaySeekOwnership::Phase::TargetTailsCompleted
                    || !transaction.display_complete))
                return false;
            if(transaction.execution) {
                auto& execution=*transaction.execution;
                const auto& a=*transaction.target;const auto& b=transaction.undo;
                auto status=Status::success();
                const auto admit_execution_commit=[&](Status result,const char* name) {
                    if(!result.ok() && status.ok()) {status=result;transaction.witness.participant=name;}
                };
                if(transaction.cancel || self->depth_ || !self->CheckBinding() || self->surface_event_
                    || self->particle_command_pending_.load() || transaction.physics_queries_pending
                    || self->pause_boundary_!=PauseBoundary::CompletedApplication
                    || self->application_phase_!=ApplicationPhase::Idle || !self->engine_idle() || !self->world_idle()
                    || !self->simulation_->interval_complete() || !self->executor_.arena_empty()
                    || execution.render!=HistoricalRestore::Execution::Render::Settled
                    || execution.capture!=HistoricalRestore::Execution::Capture::Current
                    || std::any_of(execution.participants.begin(),execution.participants.end(),[](auto p) {
                        return p!=HistoricalRestore::Execution::Participant::Settled;
                    })) return false;
                admit_execution_commit(self->callback_admission_->Validate(b.callback_stamp),"execution_callback_lease");
                ReplaySourceState source{};
                admit_execution_commit(self->ReadCheckpointSource(source),"execution_source");
                if(status.ok()) admit_execution_commit(self->input_source_->Validate(source,self->input_source_->revision()),"execution_source_revision");
                admit_execution_commit(transaction.manager.ValidatePublished(),"execution_manager");
                admit_execution_commit(transaction.cpu.ValidateCommit(b.vfx),"execution_cpu");
                admit_execution_commit(transaction.gpu.ValidateCommit(b.vfx),"execution_gpu");
                admit_execution_commit(a.vfx.ValidatePublishedTilePools(b.vfx,transaction.pools),"execution_pools");
                admit_execution_commit(transaction.world.ValidatePublished(),"execution_world");
                admit_execution_commit(transaction.world.ValidateRenderWork(),"execution_world_render");
                admit_execution_commit(transaction.traces.ValidateCommit(),"execution_traces");
                admit_execution_commit(transaction.vfx_handler.ValidatePublished(),"execution_handler");
                admit_execution_commit(transaction.source_registration.ValidatePublished(),"execution_source_registration");
                admit_execution_commit(transaction.TargetScheduler().ValidatePublishedBacking(self->image_base_,self->world_,transaction.scheduler),"execution_scheduler");
                admit_execution_commit(b.hud->ValidateBindings(false),"execution_B_hud_owners");
                admit_execution_commit(execution.hud.ValidateBindings(),"execution_C_hud_bindings");
                admit_execution_commit(execution.hud.ValidateValues(),"execution_C_hud_values");
                admit_execution_commit(b.traces->ValidateBindings(),"execution_B_traces");
                admit_execution_commit(transaction.birth.ValidateQuarantinedRetirement(),"execution_B_retirement");
                admit_execution_commit(self->checkpoint_capture_->ValidateEnclosingWind(),"execution_wind");
                if(!status.ok()) {transaction.witness.failure=status.code;*output=transaction.witness;return false;}
                if(!execution.ownership.BeginCommit()) return false;
                self->PublishHistoricalOwnership();
                transaction.witness.commit_decided=execution.ownership.commit_decided();
                transaction.witness.phase=RestoreOperationPhase::Committing;
                transaction.witness.pending=true;
                *output=transaction.witness;return true;
            }
            // Failed Begin may not have allocated any participant storage.
            // Reject invalid actions before dereferencing A or calling native
            // physics. The public owner-thread check precedes this admission.
            if (transaction.witness.phase != RestoreOperationPhase::Held || transaction.cancel
                || !transaction.target || !transaction.physics_observation || !transaction.presented_surface
                || !transaction.target->valid || !transaction.undo.valid
                || !transaction.target->hud || !transaction.undo.hud
                || !transaction.target->traces || !transaction.undo.traces || !transaction.target->vfx_handler || !transaction.undo.vfx_handler
                || !self->CheckBinding() || self->surface_event_ || self->particle_command_pending_.load()) return false;
            const auto& a = *transaction.target;
            const auto& b = transaction.undo;
            auto physics_status = Sc6ReplayWorldState::ReadPhysicsBoundary(self->image_base_, self->world_, *transaction.physics_observation);
            if (physics_status.ok()) physics_status = transaction.world.ValidateRenderWork();
            if (physics_status.ok()) physics_status = Sc6ReplayWorldState::ValidatePhysicsProjection(a.physics, *transaction.physics_observation, false, true);
            success = transaction.witness.phase == RestoreOperationPhase::Held && !transaction.cancel
                && physics_status.ok() && transaction.physics_queries_pending
                && !self->surface_event_ && !self->particle_command_pending_.load()
                && transaction.surface_dirty;
            const auto admit_commit = [&](Status result, const char* participant) {
                if (!result.ok()) {
                    transaction.witness.participant = participant;
                    transaction.witness.failure = result.code;
                    RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical commit admission participant={} code={}\n"),
                        RC::to_generic_string(participant), static_cast<unsigned>(result.code));
                }
                return result.ok();
            };
            if (!success) admit_commit(physics_status.ok() ? Status::failure(FailureCode::IllegalTransition) : physics_status, "physics_or_phase");
            if (success) success = admit_commit(self->callback_admission_ ? self->callback_admission_->Validate(a.callback_stamp)
                : Status::failure(FailureCode::ContextUnavailable), "callback_admission");
            if(success) success=admit_commit(self->input_source_ && self->input_source_->revision()==a.input_revision
                ? self->input_source_->Validate(a.source,a.input_revision) : Status::failure(FailureCode::GenerationMismatch),"input_revision");
            if (success) success = admit_commit(transaction.manager.ValidatePublished(), "vfx_manager");
            if (success) success = admit_commit(transaction.source_registration.ValidatePublished(), "source_registration");
            if (success) success = admit_commit(transaction.cpu.ValidateCommit(b.vfx), "cpu_emitters");
            if (success) success = admit_commit(transaction.gpu.ValidateCommit(b.vfx), "gpu_emitters");
            if (success) success = admit_commit(a.vfx.ValidatePublishedTilePools(b.vfx, transaction.pools), "tile_pools");
            if (success) success = admit_commit(transaction.world.ValidatePublished(), "world");
            if (success) success = admit_commit(a.hud->ValidateValues(&transaction.hud), "hud");
            if (success) success = admit_commit(transaction.vfx_handler.ValidatePublished(), "vfx_handler");
            if (success) success = admit_commit(transaction.traces.ValidatePublished(), "traces");
            if (success) success = admit_commit(b.traces->ValidateBindings(), "trace_B_owners");
            if (success) success = admit_commit(b.hud->ValidateBindings(), "hud_B_owners");
            if (success) {
                Sc6ReplaySchedulerState::PublishedFailure failure{};
                success = admit_commit(transaction.TargetScheduler().ValidatePublishedBacking(self->image_base_, self->world_, transaction.scheduler, &failure), "scheduler");
                if (!success && failure.check) RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] historical scheduler publication check={} address={:x} owner={:x} offset={:x} expected={:x} observed={:x}\n"),
                    RC::to_generic_string(failure.check), failure.address, failure.owner, failure.offset, failure.expected, failure.observed);
            }
            if (success && transaction.witness.particle_birth) success = admit_commit(transaction.birth.ValidateRetirement(), "birth_retirement");
            if (success) success = admit_commit(self->checkpoint_capture_->ValidateEnclosingWind(), "wind_B_owners");
            if (success) {
                transaction.witness.commit_decided = true;
                transaction.witness.phase = RestoreOperationPhase::Committing;
            }
        }
    }
    catch (...) {
        success = false;
        if (self->historical_restore_) {
            auto& operation = self->historical_restore_->witness;
            operation.failure = FailureCode::ContextUnavailable;
            operation.participant = "unexpected_cpp_exception";
            operation.phase = self->checkpoint_restoring_ && operation.phase != RestoreOperationPhase::Committing
                && operation.phase != RestoreOperationPhase::Committed ? RestoreOperationPhase::Recovering : RestoreOperationPhase::Failed;
        }
    }
    if (self->historical_restore_) *output = self->historical_restore_->witness;
    return success;
}

void Sc6ReplayHost::TraceSeekStep(std::uint64_t origin,std::uint64_t target) const noexcept
{
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] host UI step accepted origin={} target={} B_retained=true pending_tails_preserved=true\n"),origin,target);
}
bool Sc6ReplayHost::CanRetryHistoricalInputs(const Checkpoint& earlier) const noexcept
{
    if(!historical_restore_ || !historical_restore_->target || !input_source_)return false;
    const auto& history=historical_restore_->undo.input_revision;
    // A retry must not silently discard deliberately changed subsequent inputs.
    return input_source_->revision()==history && earlier.input_revision==history
        && historical_restore_->target->input_revision==history;
}
void Sc6ReplayHost::TraceSeekExecutionFallback(std::uint64_t rejected,std::uint64_t selected,const RestoreOperationWitness& recovered) const noexcept
{
    RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] seek execution fallback rejected={} selected={} target={} B={} discarded_ticks={} discarded_intervals={} B_recovered=true transaction_released=true publication=true unchanged_input_history=true\n"),
        rejected,selected,seek_.witness.target,recovered.original_tick,recovered.executed_ticks,recovered.executed_intervals);
}
void Sc6ReplayHost::TraceIndexCheckpointHealth(const Checkpoint& image,const IndexCheckpointHealth& health) const noexcept
{
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] index checkpoint ownership sample checkpoint={} tick={} phase={} last_valid={} first_invalid={} valid_samples={} deferred_samples={} failure={} retained=true restorability_unproven=true\n"),
        image.execution.tick,health.last_check,static_cast<unsigned>(health.phase),health.last_valid,health.first_invalid,
        health.valid_samples,health.deferred_samples,static_cast<unsigned>(health.failure));
}
void Sc6ReplayHost::TraceRejectedIndexCheckpoint(const Checkpoint& image) const noexcept
{
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] index checkpoint release witness tick={} current_tick={} vfx_owners_live=false target_shape_supported={} owned_bytes={}\n"),
        image.execution.tick,simulation_->continuation().tick,image.historical_target_shape_supported(),AdmissionBytes());
    if(index_.witness().phase==ReplayTickIndex::Phase::Recording)
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] invalid index checkpoint retirement tick={} observed_tick={} indexing=true native_publication=false\n"),
            image.execution.tick,simulation_->continuation().tick);
    else
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] invalid index checkpoint retirement tick={} selected={} before_B_preparation=true native_publication=false\n"),
            image.execution.tick,seek_.witness.checkpoint);
}

void Sc6ReplayHost::TraceSeekPreparationFallback(std::uint64_t rejected,std::uint64_t selected,FailureCode failure) const noexcept
{
    RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] seek preparation fallback rejected={} selected={} failure={} B_recovered=true preparation_released=true publication=false\n"),
        rejected,selected,static_cast<unsigned>(failure));
}
void Sc6ReplayHost::TraceSeekUiResume(std::uint64_t tick) const noexcept
{
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay UI resume dispatched tick={} seek_released=true seek_release_result=completed\n"),tick);
}
