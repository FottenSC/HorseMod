bool Sc6ReplayHost::CheckSourceRegistration(const ReplaySourceState& source,bool trace) noexcept
{
    const auto read=[](std::uintptr_t address,auto& value) noexcept {
        __try {std::memcpy(&value,reinterpret_cast<void*>(address),sizeof(value));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    };
    const auto route=InspectReplayInputRoute(read,image_base_,reinterpret_cast<std::uintptr_t>(manager_),source.owner);
    if(trace || !route.valid || !route.registered)
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay source route tick={} active={} cursor={} input_log={:x} entries={:x} count={} capacity={} handle={:x} wrapper={:x} matches={} valid={} registered={}\n"),
            simulation_->continuation().tick,source.tracker_active,source.cursor,route.input_log,route.entries,
            route.count,route.capacity,route.handle,route.wrapper,route.matches,route.valid,route.registered);
    return route.valid && route.registered;
}

namespace {
// Failure-only witness from the shipped PxShapeGeneratedValues constructor.
// 1800088A0 calls the native A0/B0 filter getters into +50/+60. This
// diagnostic does not admit triangle geometry or become checkpoint state.
static bool ReadRejectedShapeFilters(std::uintptr_t shape, std::array<unsigned, 8>& filters) noexcept
{
    __try {
        const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PhysX3_x64.dll"));
        if (!module || shape < 0x30) return false;
        const auto getter = GetProcAddress(reinterpret_cast<HMODULE>(module), "??0PxShapeGeneratedValues@physx@@QEAA@PEBVPxShape@1@@Z");
        if (reinterpret_cast<std::uintptr_t>(getter) != module + 0x88a0) return false;
        alignas(16) std::array<std::byte, 0x98> values{};
        reinterpret_cast<void* (*)(void*, const void*)>(getter)(values.data(), reinterpret_cast<void*>(shape - 0x30));
        std::memcpy(filters.data(), values.data() + 0x50, sizeof(filters));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct TimerCaptureDiagnostic { std::uint64_t handle{}, name{}, kind{}, method{}; std::int32_t script_index{}, script_serial{}, native_index{}, native_serial{}, units{}; bool script_live{}, native_live{}; };
static unsigned ReadTimerCaptureDiagnostics(std::uintptr_t base, void* world, TimerCaptureDiagnostic* rows, unsigned capacity) noexcept
{
    unsigned used{};
    __try {
        auto* instance = EngineField<void*>(world, 0x140);
        auto* manager = instance ? EngineField<void*>(instance, 0xd8) : EngineField<void*>(world, 0x430);
        for (unsigned array = 0; array < 3; ++array) {
            auto* records = EngineField<std::byte*>(manager, 0x10 + array * 16);
            const auto count = EngineField<int>(manager, 0x18 + array * 16);
            for (int i = 0; i < count && used < capacity; ++i) {
                auto* record = records + i * 0xc0;
                auto& row = rows[used++];
                row.handle = EngineField<std::uint64_t>(record, 0xb0);
                row.name = EngineField<std::uint64_t>(record, 0x58);
                row.script_index = EngineField<int>(record, 0x50); row.script_serial = EngineField<int>(record, 0x54);
                row.script_live = EngineNative<void*>(base, 0xf823f0, record + 0x50) != nullptr;
                row.units = EngineField<int>(record, 0x40);
                if (!row.units) continue;
                auto* delegate = EngineField<std::byte*>(record, 0x30);
                if (!delegate) delegate = record + 0x10;
                row.kind = EngineField<std::uintptr_t>(delegate, 0) - base;
                row.native_index = EngineField<int>(delegate, 8); row.native_serial = EngineField<int>(delegate, 12);
                row.native_live = EngineNative<void*>(base, 0xf823f0, delegate + 8) != nullptr;
                row.method = EngineField<std::uintptr_t>(delegate, 16) - base;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return used; }
    return used;
}
}
bool Sc6ReplayHost::Checkpoint::historical_target_shape_supported() const noexcept
{
    if(!ReplayStatePolicy::Accepts(state_policy))return false;
    for(const auto& image:physics.broadphases)if(image.scene && !image.valid) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint shape rejected tick={} participant=broadphase check={} step={} scene={:x} extent={} boxes={} previous={} pairs={} pair_capacity={} bounds_changed={} persistent_changed={} origin_shifted={}\n"),
            execution.tick,RC::to_generic_string(ReplayPhysicsSapImage::AdmissionName(image.admission_step)),
            image.admission_step,image.scene,image.extent,image.boxes,image.previous_boxes,image.pair_count,
            image.pairs.size(),image.bounds_changed,image.persistent_changed,image.origin_shifted);
        return false;
    }
    const bool supported=valid && boundary==PauseBoundary::CompletedApplication && !task && !event
        && execution.phase==Sc6ReplayExecutor::Phase::Idle && gpu && (surface || rolling_display_omitted) && hud
        && hud->supports_historical_target();
    if(!supported) RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint shape rejected tick={} valid={} boundary={} task={} event={} execution={} gpu={} surface={} hud={} hud_supported={}\n"),
        execution.tick,valid,static_cast<unsigned>(boundary),task!=nullptr,event!=nullptr,static_cast<unsigned>(execution.phase),
        gpu!=nullptr,surface!=nullptr,hud!=nullptr,hud && hud->supports_historical_target());
    return supported;
}

// Initial checkpoint admission is deliberately one still-live world/task
// epoch. Expired graph events need reconstruction, not a pointer restore.
bool Sc6ReplayHost::ValidManager() const noexcept
{
    __try
    {
        const auto* item = RC::Unreal::FUObjectArray::IndexToObject(manager_index_);
        return item && item->GetUObject() == manager_ && item->IsValid(false)
            && item->GetSerialNumber() == manager_serial_;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void* Sc6ReplayHost::ResolveCheckpointSource(void* context) noexcept
{
    auto* self = static_cast<Sc6ReplayHost*>(context);
    if (!self->CheckBinding()) return nullptr;
    __try
    {
        auto* manager = static_cast<RC::Unreal::UObject*>(self->manager_);
        auto** property = manager->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(L"BattleReplayPlayer");
        if (!property || !*property) return nullptr;
        const auto* item = RC::Unreal::FUObjectArray::IndexToObject((*property)->GetInternalIndex());
        return item && item->GetUObject() == *property && item->IsValid(false) ? *property : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

Status Sc6ReplayHost::ReadCheckpointSource(ReplaySourceState& source) noexcept
{
    Sc6ReplayResolvers resolvers{};
    resolvers.user = this;
    resolvers.image_base = image_base_;
    resolvers.replay_player = &ResolveCheckpointSource;
    Sc6ReplayNativeBridge bridge(resolvers);
    const auto status = bridge.CapturePlaybackSource(source, true);
    return status.ok() && !source.owner ? Status::failure(FailureCode::ContextUnavailable) : status;
}

Status Sc6ReplayHost::ValidateCheckpointLease(const Checkpoint& checkpoint) noexcept
{
    if(!ReplayStatePolicy::Accepts(checkpoint.state_policy))
        return Status::failure(FailureCode::GenerationMismatch);
    if (!CheckBinding() || !checkpoint.valid || checkpoint.session != checkpoint_session_
        || checkpoint.gameplay.coordinate != FrameCoordinate{checkpoint.session, checkpoint.execution.tick}
        || interior_phase_ != InteriorPhase::Holding || checkpoint.boundary != pause_boundary_
        || surface_event_ || depth_ || !checkpoint_broker_)
        return Status::failure(FailureCode::RestorePreflightFailed);
    if (!input_source_ || input_source_->revision()!=checkpoint.input_revision)
        return Status::failure(FailureCode::GenerationMismatch);
    const auto revision_status=input_source_->Validate(checkpoint.source,checkpoint.input_revision);
    if(!revision_status.ok()) return revision_status;
    if (!callback_admission_) return Status::failure(FailureCode::ContextUnavailable);
    const auto callback_status=callback_admission_->Validate(checkpoint.callback_stamp);
    if (!callback_status.ok()) return callback_status;
    __try
    {
        const bool recovered_execution=historical_restore_ && &checkpoint==&historical_restore_->undo
            && historical_restore_->execution
            && historical_restore_->execution->ownership.phase()==ReplaySeekOwnership::Phase::Recovering
            && checkpoint.execution.tick==historical_restore_->execution->ownership.original_tick();
        const auto required_epoch=recovered_execution?historical_restore_->execution->boundary_epoch:checkpoint.epoch;
        if (required_epoch != EngineField<std::uint64_t>(reinterpret_cast<void*>(image_base_), 0x4197170))
            return Status::failure(FailureCode::GenerationMismatch);
        const auto* task = static_cast<const std::byte*>(executor_.task_groups().pending_manager_task());
        if (checkpoint.boundary == PauseBoundary::CompletedApplication)
        {
            // Captured storage can outlive this boundary. Historical restore
            // still fails the epoch/source lease until scheduler ownership is
            // reconstructed; null tasks are never treated as resumable work.
            if (task || checkpoint.task || checkpoint.event || application_phase_ != ApplicationPhase::Idle
                || !engine_idle() || !executor_.idle() || !executor_.arena_empty()
                || !simulation_->interval_complete())
                return Status::failure(FailureCode::GenerationMismatch);
        }
        else if (engine_phase_ != EnginePhase::World || !task || task != checkpoint.task
            || *reinterpret_cast<const void* const*>(task + 0x40) != checkpoint.event
            || !*reinterpret_cast<const std::uint8_t*>(task + 0x38)
            || !checkpoint.event || (*reinterpret_cast<const std::uint64_t*>(
                static_cast<const std::byte*>(checkpoint.event) + 8) & (1ull << 26)))
            return Status::failure(FailureCode::GenerationMismatch);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
    ReplayRenderState rendering{};
    if (!replay_rendering_->Capture(rendering) || rendering != checkpoint.rendering)
        return Status::failure(FailureCode::RestoreVerificationFailed);
    ReplaySourceState source{};
    const auto status = ReadCheckpointSource(source);
    if (!status.ok()) return status;
    // A suffix sharing one publication does not run the replay producer.
    // Cross-source/round restoration is not admitted by this first lease.
    if (source != checkpoint.source) return Status::failure(FailureCode::GenerationMismatch);
    const auto world_status = checkpoint.world.ValidateHeld(image_base_, world_);
    if (!world_status.ok() || checkpoint.boundary != PauseBoundary::CompletedApplication) return world_status;
    const auto scheduler_status = checkpoint.scheduler.ValidateBindings(image_base_, world_);
    if (!scheduler_status.ok()) return scheduler_status;
    if (!checkpoint.source_registration || !checkpoint.source_registration->ValidateValues().ok()) return Status::failure(FailureCode::GenerationMismatch);
    if (!checkpoint.vfx_handler) return Status::failure(FailureCode::GenerationMismatch);
    const auto handler_status=checkpoint.vfx_handler->ValidateValues();
    return handler_status.ok() ? checkpoint.vfx.ValidateHeld(image_base_, manager_) : handler_status;
}

std::size_t Sc6ReplayHost::AdmissionBytes(const Checkpoint* pending) const noexcept
{
    const auto limit = memory_limit_;
    std::size_t total = sizeof(*this) + retained_checkpoints_.capacity() * sizeof(std::weak_ptr<const Checkpoint>)
        + checkpoint_pins_.capacity() * sizeof(CheckpointHandle);
    const auto add = [&](std::size_t bytes) {
        total = total > limit || bytes > limit - total ? limit + 1 : total + bytes;
    };
    if (!companion_storage_) return limit + 1;
    add(NativeReplayTraceTaskGuard::ProcessOwnedBytes());
    add(ReplayConsumerFailure::OwnedBytes());
    add(NativeReplayNiagaraObservation::ProcessOwnedBytes());
    add(NativeReplayVfxCompletionObservation::ProcessOwnedBytes());
    add(NativeReplayMaterialTaskGuard::ProcessOwnedBytes());
    add(index_.storage_bytes());
    if(rolling_.schedule)add(rolling_.schedule->owned_bytes());
    add(companion_storage_(storage_context_));
    add(CandidateCheckpointCodec::ThreadScratchBytes());
    add(Sc6ReplayGroundDebrisState::update_scratch_bytes());
    add(Sc6ReplayObjectLease::quarantined_bytes());
    add(Sc6ReplayParticleConfiguration::pending_bytes());
    add(Sc6ReplayParticleCopy::retained_capture_bytes());
    if (particle_copy_) add(particle_copy_->witness().phase == Sc6ReplayParticleCopy::Phase::Released
        ? sizeof(*particle_copy_) : particle_copy_->witness().bytes - particle_copy_->shared_capture_bytes());
    if (corrected_particle_copy_) add((std::max)(sizeof(*corrected_particle_copy_),
        corrected_particle_copy_->witness().phase == Sc6ReplayParticleCopy::Phase::Released ? std::size_t{}
        : corrected_particle_copy_->witness().bytes - corrected_particle_copy_->shared_capture_bytes()));
    if (checkpoint_capture_) add(checkpoint_capture_->owned_scratch_bytes());
    if (replay_rendering_) add(replay_rendering_->owned_bytes());
    if (widget_clock_) add(widget_clock_->owned_bytes());
    if (emitter_lifetime_) add(emitter_lifetime_->owned_bytes());
    if (callback_admission_) add(callback_admission_->owned_bytes());
    if (consumer_hold_) add(consumer_hold_->owned_bytes());
    if (input_source_) add(input_source_->owned_bytes());
    add(Horse::GameImGui::PresentHook::instance().replay_surface_bytes());
    add(Horse::GameImGui::PresentHook::replay_timeline_control_bytes());
    // The ring's current checkpoint and private B can share the same immutable
    // SurfaceSnapshot. Charge its allocation once, without dropping any pin.
    // Bounded scratch: beyond this table, retain conservative duplicate charges.
    std::array<const SurfaceSnapshot*,32> charged_surfaces{};
    std::size_t charged_surface_count{};
    add(sizeof(charged_surfaces)+sizeof(charged_surface_count));
    std::array<const Sc6ReplayParticleConfiguration*,128> charged_configurations{};
    std::size_t charged_configuration_count{};
    add(sizeof(charged_configurations)+sizeof(charged_configuration_count));
    const auto checkpoint_bytes = [&](const Checkpoint& image) {
        // Immutable GPU reservations are charged once by their resource owner.
        auto bytes=image.owned_bytes()-Sc6ReplayParticleCopy::captured_bytes(image.gpu);
        if(image.surface) {
            bool counted=false;
            for(std::size_t i=0;i<charged_surface_count;++i)
                counted|=charged_surfaces[i]==image.surface.get();
            if(counted)bytes-=sizeof(SurfaceSnapshot)+image.surface->image.bytes;
            else if(charged_surface_count<charged_surfaces.size())
                charged_surfaces[charged_surface_count++]=image.surface.get();
        }
        for(const auto& configuration:image.particle_configurations)if(configuration) {
            bool counted=false;
            for(std::size_t i=0;i<charged_configuration_count;++i)
                counted|=charged_configurations[i]==configuration.get();
            if(counted)bytes-=configuration->owned_bytes();
            else if(charged_configuration_count<charged_configurations.size())
                charged_configurations[charged_configuration_count++]=configuration.get();
        }
        add(bytes);
    };
    bool pending_counted = false, undo_counted = false, target_counted = false, old_undo_counted = false;
    for (const auto& weak : retained_checkpoints_) if (const auto image = weak.lock()) {
        checkpoint_bytes(*image);
        pending_counted |= image.get() == pending;
        old_undo_counted |= image.get() == restore_undo_.get();
        if (historical_restore_) {
            undo_counted |= image.get() == &historical_restore_->undo;
            target_counted |= image == historical_restore_->target;
        }
    }
    if (restore_undo_ && !old_undo_counted) {
        checkpoint_bytes(*restore_undo_);
        pending_counted |= pending == restore_undo_.get();
    }
    if (historical_restore_) {
        const auto& operation = *historical_restore_;
        // Checkpoint::owned_bytes charges complete B's fixed storage below
        // (or through its registry entry). It is embedded in this operation,
        // so count that storage once, while retaining every dynamic B charge.
        add(sizeof(operation)-sizeof(operation.undo));
        if (!undo_counted) checkpoint_bytes(operation.undo);
        pending_counted |= pending == &operation.undo;
        if (operation.target && !target_counted) checkpoint_bytes(*operation.target);
        pending_counted |= pending == operation.target.get();
        add(operation.manager.owned_bytes()); add(operation.cpu.owned_bytes()); add(operation.gpu.owned_bytes());
        add(operation.pools.owned_bytes()); add(operation.scheduler.owned_bytes()); add(operation.world.owned_bytes());
        add(operation.traces.owned_bytes());
        add(operation.vfx_handler.owned_bytes());
        add(operation.source_registration.owned_bytes());
        add(operation.physics_markers.owned_bytes());
        add(operation.ground.owned_bytes());
        add(operation.fresh_particles.capacity()*sizeof(operation.fresh_particles[0]));
        add(operation.fresh_trace_children.capacity()*sizeof(operation.fresh_trace_children[0]));
        for(const auto& owner:operation.fresh_trace_children)if(owner)add(owner->owned_bytes());
        if(operation.trace_projection)add(operation.trace_projection->owned_bytes());
        if(operation.trace_survivors)add(operation.trace_survivors->owned_bytes());
        add(operation.particle_bindings.capacity()*sizeof(operation.particle_bindings[0]));
        add(operation.scheduler_bindings.capacity()*sizeof(operation.scheduler_bindings[0]));
        for(const auto& owner:operation.fresh_particles)if(owner)add(owner->owned_bytes());
        add(operation.fresh_ground_roots.capacity()*sizeof(operation.fresh_ground_roots[0]));
        for(const auto& owner:operation.fresh_ground_roots)if(owner)add(owner->owned_bytes());
        add(operation.fresh_scheduler_staging.owned_bytes());add(operation.fresh_scheduler_target.owned_bytes());
        add(operation.fresh_scheduler_adopted.owned_bytes());add(operation.fresh_scheduler_transfer.owned_bytes());
        if (operation.execution) add(operation.execution->owned_bytes());
        if (operation.physics_observation) add(sizeof(*operation.physics_observation));
        if (operation.presented_surface) add(sizeof(SurfaceSnapshot));
    }
    if (pending && !pending_counted) checkpoint_bytes(*pending);
    return total;
}

std::size_t Sc6ReplayHost::AdmissionRemaining(const Checkpoint* pending) const noexcept
{
    const auto used = AdmissionBytes(pending);
    return used < memory_limit_ ? memory_limit_ - used : 0;
}

bool Sc6ReplayHost::RegisterCheckpoint(const Checkpoint& checkpoint)
{
    const auto pinned = checkpoint.weak_from_this().lock();
    if (!pinned) return checkpoint.boundary != PauseBoundary::CompletedApplication;
    for (auto it = retained_checkpoints_.begin(); it != retained_checkpoints_.end();) {
        const auto image = it->lock();
        if (!image) it = retained_checkpoints_.erase(it);
        else { if (image.get() == &checkpoint) return true; ++it; }
    }
    // Reserve exact capacity before inserting; retain room for both old/new
    // pointer arrays during vector reallocation. Checkpoints themselves stay pinned by callers.
    if (retained_checkpoints_.size() == retained_checkpoints_.capacity()) {
        const auto bytes = (retained_checkpoints_.size() + 1) * sizeof(std::weak_ptr<const Checkpoint>);
        if (bytes > AdmissionRemaining(&checkpoint)) return false;
        retained_checkpoints_.reserve(retained_checkpoints_.size() + 1);
    }
    retained_checkpoints_.push_back(pinned);
    return true;
}

Status Sc6ReplayHost::CaptureParticleConfigurations(Checkpoint& output) noexcept
{
    if(!output.particle_configurations.empty())return Status::failure(FailureCode::IllegalTransition);
    struct Context {Sc6ReplayHost* host;Checkpoint* checkpoint;} context{this,&output};
    return output.vfx.VisitSingleGpuComponents(&context,[](void* opaque,std::uintptr_t source) {
        auto& context=*static_cast<Context*>(opaque);auto& output=*context.checkpoint;auto& host=*context.host;
        try {
            const auto allocation=(output.particle_configurations.size()+1)*sizeof(output.particle_configurations[0]);
            if(allocation>host.AdmissionRemaining(&output))return Status::failure(FailureCode::CapacityExceeded);
            output.particle_configurations.reserve(output.particle_configurations.size()+1);
            output.particle_configurations.emplace_back();
            const auto status=Sc6ReplayParticleConfiguration::Capture(host.image_base_,reinterpret_cast<void*>(source),
                host.AdmissionRemaining(&output),output.particle_configurations.back());
            bool shared=false;
            // Private B keeps its independent native construction. Queuing
            // duplicate retirement inside that transaction would interlock
            // with the completed-application retirement owner.
            if(status.ok() && !host.historical_restore_) {
                auto& fresh=output.particle_configurations.back();
                for(const auto& weak:host.retained_checkpoints_) {
                    const auto prior=weak.lock();
                    if(!prior || !prior->valid || prior->session!=host.checkpoint_session_)continue;
                    for(const auto& candidate:prior->particle_configurations)
                        if(candidate && fresh->EquivalentTo(*candidate)) {
                            // Only immutable configuration is shared. The new
                            // detached duplicate queues its normal retirement;
                            // fresh logical reconstruction still clones anew.
                            fresh=candidate;shared=true;break;
                        }
                    if(shared)break;
                }
            }
            if(const auto& owner=output.particle_configurations.back()) {
                const auto& w=owner->construction_witness();
                RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] particle configuration capture source={:x} copy={:x} success={} check={} native_entered={} detached={} properties={} arrays={} property_offset={:x} bytes={} shared={}\n"),
                    source,w.copy,status.ok(),RC::to_generic_string(w.check),w.native_construction_entered,w.detached,w.properties,w.arrays,w.property_offset,owner->owned_bytes(),shared);
                if(!status.ok() && std::string_view(w.check)=="property_recursive_ownership_unproven" && w.visited_property)
                    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] particle configuration rejected property={} kind={} offset={:x}\n"),
                        w.visited_property->GetFullName(),w.visited_property->GetClass().GetFName().ToString(),w.visited_property->GetOffset_Internal());
                if(!status.ok() && std::string_view(w.check)=="source_shared_material") {
                    using Cold=Sc6ReplayColdParticleOwner;
                    auto* object=reinterpret_cast<RC::Unreal::UObject*>(source);
                    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] particle material source component={} template={}\n"),
                        object->GetFullName(),reinterpret_cast<RC::Unreal::UObject*>(Cold::Read<std::uintptr_t>(source,0x808))->GetFullName());
                    for(auto* property:object->GetClassPrivate()->ForEachPropertyInChain()) {
                        const auto offset=property->GetOffset_Internal();
                        if(offset==0x810 || offset==0xa98 || offset==0xaa8)
                            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] particle material property name={} offset={:x} flags={:x} size={}\n"),
                                property->GetFullName(),offset,static_cast<std::uint64_t>(property->GetPropertyFlags()),property->GetElementSize());
                    }
                    for(unsigned side=0;side<2;++side) {
                        const auto offset=0xa98+side*16;
                        const auto data=Cold::Read<std::uintptr_t>(source,offset);const auto count=Cold::Read<int>(source,offset+8);
                        if(count<0 || count>32 || (count&&!data))continue;
                        for(int i=0;i<count;++i) {
                            const auto material=Cold::Read<std::uintptr_t>(data,i*8);
                            if(!material || !Cold::Live(reinterpret_cast<void*>(material)))continue;
                            auto* mid=reinterpret_cast<RC::Unreal::UObject*>(material);
                            const auto parent=Cold::Read<std::uintptr_t>(material,0x78);
                            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] particle material root side={} slot={} material={:x} name={} outer={:x} parent={:x} parent_name={} font_count={} scalar_count={} texture_count={} vector_count={}\n"),
                                side,i,material,mid->GetFullName(),Cold::Read<std::uintptr_t>(material,0x20),parent,
                                parent&&Cold::Live(reinterpret_cast<void*>(parent))?reinterpret_cast<RC::Unreal::UObject*>(parent)->GetFullName():STR("unavailable"),
                                Cold::Read<int>(material,0x90),Cold::Read<int>(material,0xa0),Cold::Read<int>(material,0xb0),Cold::Read<int>(material,0xc0));
                        }
                    }
                }
                if(!status.ok() && std::string_view(w.check)=="outer_modify_domain")
                    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] particle configuration outer rejected outer={:x} modify_rva={:x} transaction_buffer={:x} dirty_gate={}\n"),
                        w.outer,w.modify_rva,w.transaction_buffer,w.dirty_gate);
            }
            return status;
        } catch(...) {return Status::failure(FailureCode::CapacityExceeded);}
    });
}

Status Sc6ReplayHost::CaptureCheckpointUnchecked(Checkpoint& output)
{
    output.valid = false;
    auto* capture_copy=(capture_driving_ && corrected_capture_) ? corrected_particle_copy_.get() : particle_copy_.get();
    if (GetCurrentThreadId() != thread_ || !CheckBinding() || interior_phase_ != InteriorPhase::Holding
        || depth_ || surface_event_ || particle_command_pending_.load() || !checkpoint_broker_)
        return Status::failure(FailureCode::IllegalTransition);
    const bool completed_application = pause_boundary_ == PauseBoundary::CompletedApplication;
    if (completed_application
        ? (application_phase_ != ApplicationPhase::Idle || !engine_idle() || !executor_.idle()
            || !executor_.arena_empty() || !simulation_->interval_complete())
        : (engine_phase_ != EnginePhase::World
            || simulation_->continuation().phase != Sc6ReplayExecutor::Phase::RepeatDecision))
        return Status::failure(FailureCode::IllegalTransition);
    auto* task = static_cast<const std::byte*>(executor_.task_groups().pending_manager_task());
    if (completed_application ? task != nullptr : task == nullptr)
        return Status::failure(FailureCode::ContextUnavailable);
    if (completed_application && !Sc6ReplayObjectLease::RetireQuarantined(image_base_).ok())
        return Status::failure(FailureCode::ContextUnavailable);
    if (!callback_admission_) return Status::failure(FailureCode::ContextUnavailable);
    const auto callback_status=callback_admission_->Admit(output.callback_stamp);
    if (!callback_status.ok()) {
        const auto stamp=callback_admission_->stamp();
        const auto detail=callback_admission_->diagnostic();
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] callback capture admission code={} latent={} streamable={} check={} owner={:x} count={}\n"),
            static_cast<unsigned>(callback_status.code),stamp[0],stamp[1],detail.check,detail.owner,detail.count);
        return callback_status;
    }
    output.boundary = pause_boundary_;
    output.state_policy = ReplayStatePolicy::current;
    output.session = checkpoint_session_;
    output.epoch = EngineField<std::uint64_t>(reinterpret_cast<void*>(image_base_), 0x4197170);
    output.task = task;
    output.event = task ? *reinterpret_cast<const void* const*>(task + 0x40) : nullptr;
    output.execution = simulation_->continuation();
    output.gpu.reset();
    if (completed_application && !checkpoint_restoring_ && !historical_restore_ && output.weak_from_this().expired())
        return Status::failure(FailureCode::IllegalTransition);
    if (completed_application)
    {
        if (!CaptureCheckpointDisplay(output))
            return Status::failure(FailureCode::CapturePreflightFailed);
    }
    else { output.surface.reset(); output.rolling_display_omitted = false; }
    auto capture_segment_started=std::chrono::steady_clock::now();
    const auto captured = [&](Status result, const wchar_t* component) {
        const auto capture_segment_ended=std::chrono::steady_clock::now();
        if (!result.ok())
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint component failed component={} code={} tick={} epoch={}\n"),
                component, static_cast<unsigned>(result.code), output.execution.tick, output.epoch);
        // Bounded capture-stutter diagnostic. These are CPU wall segments,
        // including preparation between participants, not GPU/readback timings.
        // The prior timing log itself is excluded from the next segment.
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] checkpoint CPU segment tick={} through={} elapsed_us={} success={}\n"),
            output.execution.tick,component,std::chrono::duration_cast<std::chrono::microseconds>(
                capture_segment_ended-capture_segment_started).count(),result.ok());
        capture_segment_started=std::chrono::steady_clock::now();
        return result;
    };
    auto status = captured(replay_rendering_->Capture(output.rendering) ? Status::success() : Status::failure(FailureCode::CapturePreflightFailed), L"rendering");
    if (status.ok()) status = captured(ReadCheckpointSource(output.source), L"source");
    if (status.ok()) CheckSourceRegistration(output.source,true);
    if (status.ok()) {
        if(!input_source_) return Status::failure(FailureCode::ContextUnavailable);
        output.input_revision=input_source_->revision();
        status=captured(input_source_->Validate(output.source,output.input_revision),L"input_revision");
    }
    if (status.ok() && completed_application && capture_copy)
    {
        auto image = capture_copy->captured_image();
        const auto* identity = Sc6ReplayParticleCopy::identity(image);
        if (identity && identity->session == output.session && identity->epoch == output.epoch
            && identity->tick == output.execution.tick && identity->source == output.source
            && identity->source_revision == (output.input_revision?output.input_revision->id:0)
            && identity->interval == output.execution.interval
            && identity->execution_phase == static_cast<std::uint8_t>(output.execution.phase))
            output.gpu = std::move(image);
        // B capture has different coordinates and owns its independent undo
        // through the operation. Never label the retained A image as B.
    }
    if (status.ok()) status = captured(checkpoint_broker_->Capture(thread_, output.ucrt), L"ucrt");
    // Each participant receives the current shared remainder, not a fresh
    // copy of the whole checkpoint allowance. Leave a second envelope for
    // temporary vector storage while an existing allocation is replaced.
    const auto participant_budget = [&]() { return (std::min)(Schema::replay_checkpoint_memory_budget, AdmissionRemaining(&output) / 2); };
    if(status.ok() && participant_budget()<sizeof(Sc6ReplaySourceRegistrationState)+32768)
        status=captured(Status::failure(FailureCode::CapacityExceeded),L"source_registration_budget");
    if(status.ok()) {
        output.source_registration=std::make_shared<Sc6ReplaySourceRegistrationState>();
        status=captured(output.source_registration->Capture(image_base_,manager_,output.source.owner,participant_budget()),L"source_registration");
    }
    if (status.ok()) {
        status = captured(CaptureReplayWorld(output.world,image_base_,world_,participant_budget()), L"world");
        if (!status.ok()) {
            std::array<TimerCaptureDiagnostic, 12> rows{};
            const auto count = ReadTimerCaptureDiagnostics(image_base_, world_, rows.data(), static_cast<unsigned>(rows.size()));
            for (unsigned i = 0; i < count; ++i) {
                const auto& row = rows[i];
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] timer capture rejection row={} handle={} name={:x} units={} kind={:x} method={:x} script={}/{} live={} native={}/{} live={}\n"),
                    i, row.handle, row.name, row.units, row.kind, row.method, row.script_index, row.script_serial, row.script_live,
                    row.native_index, row.native_serial, row.native_live);
            }
        }
    }
    if (status.ok() && completed_application) {
        if (sizeof(Sc6ReplayHudState) > AdmissionRemaining(&output)) return Status::failure(FailureCode::CapacityExceeded);
        output.hud = std::make_shared<Sc6ReplayHudState>();
        status = captured(output.hud->Capture(image_base_, world_, manager_, participant_budget()), L"hud");
    }
    if (status.ok() && completed_application) {
        if (sizeof(Sc6ReplayTraceState) > AdmissionRemaining(&output)) return Status::failure(FailureCode::CapacityExceeded);
        output.traces = std::make_shared<Sc6ReplayTraceState>();
        status = captured(output.traces->Capture(image_base_, world_, participant_budget()), L"traces");
    }
    if (status.ok() && completed_application) {
        if (sizeof(Sc6ReplayVfxHandlerState) > AdmissionRemaining(&output)) return Status::failure(FailureCode::CapacityExceeded);
        output.vfx_handler = std::make_shared<Sc6ReplayVfxHandlerState>();
        status = captured(output.vfx_handler->Capture(image_base_, manager_, participant_budget()), L"vfx_handler");
    }
    if (status.ok() && completed_application) {
        if (sizeof(Sc6ReplayGroundDebrisState) > AdmissionRemaining(&output)) return Status::failure(FailureCode::CapacityExceeded);
        output.ground=std::make_shared<Sc6ReplayGroundDebrisState>();
        // Capture owned configuration arrays before native retirement can
        // destroy the historical meshes. Prepare only inventories/leases;
        // physics removal is exclusive to the private B transaction.
        status=captured(output.ground->Prepare(image_base_,manager_,world_,0,participant_budget()),L"ground_configuration");
        if(!status.ok())RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] ground checkpoint rejected tick={} check={} code={} read_only=true\n"),
            output.execution.tick,RC::to_generic_string(output.ground->failed_check()),static_cast<unsigned>(status.code));
    }
    if (status.ok() && completed_application)
        status = captured(output.scheduler.Capture(image_base_, world_, participant_budget(),
            {output.traces.get(),[](const void* traces,void* context,bool(*visitor)(void*,std::uintptr_t)) {
                return static_cast<const Sc6ReplayTraceState*>(traces)->VisitPrimaryTicks(context,visitor);
            }}), L"scheduler");
    if (status.ok() && completed_application)
    {
        status = captured(Sc6ReplayWorldState::ReadPhysicsBoundary(image_base_, world_, output.physics), L"physics_binding");
        if (!status.ok()) for (unsigned scene = 0; scene < output.physics.scenes.size(); ++scene) {
            const auto count = (std::min)(static_cast<std::size_t>(output.physics.observed_actors[scene]), output.physics.actors[scene].size());
            if (!count) continue;
            const auto& actor = output.physics.actors[scene][count - 1];
            // Already observed by the native generated-values getter, including
            // when geometry admission stops before the BodySim inventory.
            const auto rigid_flags = std::to_integer<unsigned>(actor.properties[0x9c]);
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] physics rejected shape count={} capacity={}\n"),
                actor.query_handle_count, actor.query_shapes.size());
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] physics rejected actor scene={} index={} kind={} total_static={} total_dynamic={} actor={:x} component={:x} weak={}/{} rigid_flags={:x} owner={}\n"),
                scene,count-1,actor.kind,output.physics.actor_counts[scene][0],output.physics.actor_counts[scene][1],
                actor.actor,actor.component,actor.component_weak[0],actor.component_weak[1],rigid_flags,
                actor.component?reinterpret_cast<RC::Unreal::UObject*>(actor.component)->GetFullName():STR("unresolved"));
            for (unsigned h = 0; h < (std::min)(static_cast<std::size_t>(actor.query_handle_count), actor.query_shapes.size()); ++h) {
                const auto& shape = actor.query_shapes[h];
                std::array<unsigned, 8> filters{};
                const auto filters_read = ReadRejectedShapeFilters(shape.shape, filters);
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] physics capture shape scene={} actor={} shape={} handle={:x} control={:x} flags={:x} geometry={} bytes={} bound={}\n"),
                    scene, count - 1, h, actor.query_handles[h], shape.control, shape.flags,
                    shape.geometry_kind, shape.geometry_bytes, shape.shape != 0);
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] physics rejected shape filters scene={} actor={} shape={} read={} getterA0={:x}/{:x}/{:x}/{:x} getterB0={:x}/{:x}/{:x}/{:x}\n"),
                    scene,count-1,h,filters_read,filters[0],filters[1],filters[2],filters[3],filters[4],filters[5],filters[6],filters[7]);
            }
        }
    }
    if (status.ok() && completed_application)
    {
        // Reuse the existing transaction's native GC lease. PhysX actors are
        // owned by these scene components; explicit destruction still fails
        // the saved weak/body/actor membership checks before any writes.
        std::array<void*, 4096> physics_owners{};
        std::size_t physics_owner_count{};
        physics_owners[physics_owner_count++] = world_;
        for(auto* object : output.world.camera_owners()) physics_owners[physics_owner_count++] = object;
        for(const auto& row:output.world.stage_visibility()) {
            const auto retain=[&](std::uintptr_t p){
                if(!p)return true;
                if(physics_owner_count==physics_owners.size())return false;
                physics_owners[physics_owner_count++]=reinterpret_cast<void*>(p);return true;
            };
            if(!retain(row.actor) || !retain(row.root) || !retain(row.effect))return Status::failure(FailureCode::CapacityExceeded);
            for(const auto& component:row.components) {
                if(!retain(component.object) || !retain(component.asset))return Status::failure(FailureCode::CapacityExceeded);
                for(auto mid:component.mids)if(!retain(mid))return Status::failure(FailureCode::CapacityExceeded);
            }
        }
        // A/B RT images were completed before this game-thread capture. The
        // existing native GC lease retains their primitive-component owners.
        if(capture_copy) for(auto* object:capture_copy->lighting_owners()) {
            // Captured trace meshes already have their exact native GC lease
            // in the trace participant, which owns replacement after logical
            // destruction. Do not also require their original identity as a
            // VFX reconstruction dependency merely for a lighting row. Other
            // roles below (physics, particle attachments/listeners) still
            // retain their own dependencies without this delegation.
            if(output.traces && output.traces->RetainsCapturedMesh(object)) continue;
            if(physics_owner_count>=physics_owners.size()) return Status::failure(FailureCode::CapacityExceeded);
            physics_owners[physics_owner_count++]=object;
        }
        for (std::size_t i = 0; i < output.physics.creation_owner_count; ++i) {
            const auto& entry = output.physics.creation_owners[i];
            physics_owners[physics_owner_count++] = reinterpret_cast<void*>(entry.component);
            physics_owners[physics_owner_count++] = reinterpret_cast<void*>(entry.owner);
        }
        for (unsigned scene = 0; scene < output.physics.scenes.size(); ++scene)
            for (unsigned i = 0; i < output.physics.observed_actors[scene]; ++i)
                if (const auto component = output.physics.actors[scene][i].component)
                    physics_owners[physics_owner_count++] = reinterpret_cast<void*>(component);
        const auto available = participant_budget();
        status = captured(sizeof(physics_owners) >= available
            ? Status::failure(FailureCode::CapacityExceeded)
            : CaptureReplayVfx(output.vfx, image_base_, manager_, available - sizeof(physics_owners),
                {physics_owners.data(), physics_owner_count},
                corrected_capture_ && historical_restore_ ? &historical_restore_->birth : nullptr), L"vfx_requests");
    }
    if (status.ok() && completed_application)
        status = captured(ValidateParticleCompletionOwnership(output), L"particle_completion_owners");
    if(status.ok() && completed_application && capture_driving_ && !restore_preparation_driving_)
        status=captured(CaptureParticleConfigurations(output),L"particle_configuration");
    if (!status.ok()) return status;
    if (!checkpoint_capture_)
    {
        if (Sc6CandidateCheckpointCapture::transient_initial_storage_bytes() > AdmissionRemaining(&output))
            return Status::failure(FailureCode::CapacityExceeded);
        checkpoint_capture_ = std::make_unique<Sc6CandidateCheckpointCapture>(Sc6CandidateCheckpointCapture::CaptureMode::ResumableCore);
        status = checkpoint_capture_->Initialize(image_base_, checkpoint_broker_);
        if (!status.ok()) { checkpoint_capture_.reset(); return status; }
    }
    status = CandidateCheckpointCodec::PrepareThreadScratchStorage(AdmissionRemaining(&output));
    if (!status.ok()) return status;
    if (checkpoint_capture_->CaptureAllocationEnvelopeBytes(reinterpret_cast<std::uintptr_t>(manager_),{checkpoint_session_,output.execution.tick},checkpoint_session_) > AdmissionRemaining(&output)) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint capacity participant=transient_capture tick={} owned={} envelope={} available={}\n"),
            output.execution.tick, AdmissionBytes(&output),
            checkpoint_capture_->CaptureAllocationEnvelopeBytes(reinterpret_cast<std::uintptr_t>(manager_),{checkpoint_session_,output.execution.tick},checkpoint_session_), AdmissionRemaining(&output));
        // Bounded first-rejection evidence only; no ownership or admission change.
        const auto detail=[&](const Checkpoint& image,unsigned ordinal) {
            std::size_t local{},config{};
            for(const auto& row:image.gameplay.local_images)local+=row.bytes.capacity();
            for(const auto& row:image.particle_configurations)if(row)config+=row->owned_bytes();
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint capacity image ordinal={} tick={} total={} fixed={} gameplay={} local={} world={} scheduler={} vfx={} hud={} traces={} handler={} registration={} configurations={} surface={} gpu={}\n"),
                ordinal,image.execution.tick,image.owned_bytes(),sizeof(image),image.gameplay.bytes.capacity(),local,
                image.world.owned_bytes(),image.scheduler.owned_bytes(),image.vfx.owned_bytes(),
                image.hud?image.hud->owned_bytes():0,image.traces?image.traces->owned_bytes():0,
                image.vfx_handler?image.vfx_handler->owned_bytes():0,image.source_registration?image.source_registration->owned_bytes():0,
                config,image.surface?sizeof(SurfaceSnapshot)+image.surface->image.bytes:0,Sc6ReplayParticleCopy::captured_bytes(image.gpu));
        };
        unsigned ordinal{};
        for(const auto& weak:retained_checkpoints_)if(const auto image=weak.lock()) {
            if(ordinal<16)detail(*image,ordinal);++ordinal;
        }
        detail(output,16);
        const auto scratch=checkpoint_capture_->owned_scratch_status();
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint capacity owners retained={} scratch_fixed={} scratch_adapter={} scratch_snapshots={} scratch_callbacks={} scratch_decode={} codec={} operation_fixed={} checkpoint_fixed={} gpu_retained={} gpu_active={} gpu_shared={} companion={}\n"),
            ordinal,scratch.fixed_subsystems,scratch.adapter,scratch.capture_snapshots,scratch.callback_topology,scratch.auxiliary_decode,
            CandidateCheckpointCodec::ThreadScratchBytes(),historical_restore_?sizeof(*historical_restore_):0,sizeof(Checkpoint),
            Sc6ReplayParticleCopy::retained_capture_bytes(),capture_copy?capture_copy->witness().bytes:0,
            capture_copy?capture_copy->shared_capture_bytes():0,companion_storage_(storage_context_));
        return Status::failure(FailureCode::CapacityExceeded);
    }
    const FrameCoordinate coordinate{checkpoint_session_, output.execution.tick};
    status = checkpoint_capture_->BindForCanonicalCapture(reinterpret_cast<std::uintptr_t>(manager_),
        coordinate, checkpoint_session_, thread_);
    if (!status.ok())
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint bind failed code={} tick={}\n"),
            static_cast<unsigned>(status.code), output.execution.tick);
    CandidateTransientCaptureDiagnostic diagnostic{};
    if (status.ok())
    {
        status = captured(checkpoint_capture_->CaptureTransient(coordinate, output.gameplay, &diagnostic),L"gameplay");
        if (!status.ok())
            RC::Output::send<RC::LogLevel::Warning>(STR(
                "[HorseMod] checkpoint capture failed code={} tick={} phase={} identity_issue={} expected={} observed={}\n"),
                static_cast<unsigned>(status.code), output.execution.tick, static_cast<unsigned>(diagnostic.phase),
                diagnostic.identity_issue, diagnostic.identity_expected, diagnostic.identity_observed);
        if (!status.ok())
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint native validation issue={} index={} expected={:08x}:{:08x} observed={:08x}:{:08x}\n"),
                static_cast<unsigned>(diagnostic.validation.issue), diagnostic.validation.index,
                static_cast<std::uint32_t>(diagnostic.validation.expected_b),static_cast<std::uint32_t>(diagnostic.validation.expected_a),
                static_cast<std::uint32_t>(diagnostic.validation.observed_b),static_cast<std::uint32_t>(diagnostic.validation.observed_a));
    }
    if (status.ok()) status = captured(checkpoint_capture_->ValidateSnapshotUcrt(output.gameplay, output.ucrt), L"encoded_ucrt");
    if (status.ok() && (output.owned_bytes() > Schema::replay_checkpoint_memory_budget
        || AdmissionBytes(&output) > memory_limit_))
        status = captured(Status::failure(FailureCode::CapacityExceeded), L"checkpoint_and_scratch_budget");
    if (status.ok()) RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] callback checkpoint tick={} latent={} streamable={} domain={} policy=quiescent_streamable_v2\n"),
        output.execution.tick, output.callback_stamp[0], output.callback_stamp[1], output.callback_stamp[2]);
    output.valid = status.ok();
    if (status.ok()) status = captured(ValidateCheckpointLease(output), L"lease");
    if (!status.ok()) output.valid = false;
    if (status.ok()) RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] checkpoint ownership tick={} checkpoint_bytes={} scratch_bytes={}\n"),
        output.execution.tick, output.owned_bytes(), checkpoint_capture_->owned_scratch_bytes());
    return status;
}

Status Sc6ReplayHost::Capture(Checkpoint& output) noexcept
{
    if(SeekOwnsExecution() && !seek_driving_ && !(capture_driving_ && corrected_capture_)) return Status::failure(FailureCode::IllegalTransition);
    if (GetCurrentThreadId() != thread_ || ((historical_restore_ || checkpoint_restoring_) && !(capture_driving_ && corrected_capture_)) || output.valid
        || (capture_operation_.phase!=CapturePhase::Idle && !capture_driving_)) return Status::failure(FailureCode::IllegalTransition);
    try {
        if (!RegisterCheckpoint(output)) return Status::failure(FailureCode::CapacityExceeded);
        return CaptureCheckpointUnchecked(output);
    }
    catch (...) { output.valid = false; return Status::failure(FailureCode::CaptureFailed); }
}

Status Sc6ReplayHost::Capture(CheckpointHandle& output) noexcept
{
    // Failed capture never evicts the caller's previous checkpoint.
    if (GetCurrentThreadId() != thread_) return Status::failure(FailureCode::WrongThread);
    if (sizeof(Checkpoint) > AdmissionRemaining()) return Status::failure(FailureCode::CapacityExceeded);
    try {
        auto checkpoint = std::make_shared<Checkpoint>();
        const auto status = Capture(*checkpoint);
        if (status.ok()) output = std::move(checkpoint);
        return status;
    } catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
}

Status Sc6ReplayHost::AdvancePendingRepeatToTick(std::uint64_t target) noexcept
{
    if(SeekOwnsExecution() && !seek_driving_) return Status::failure(FailureCode::IllegalTransition);
    if (capture_operation_.phase!=CapturePhase::Idle || checkpoint_restoring_ || !CheckBinding() || interior_phase_ != InteriorPhase::Holding || depth_ || surface_event_
        || engine_phase_ != EnginePhase::World)
        return Status::failure(FailureCode::IllegalTransition);
    const auto before = simulation_->continuation();
    if (target == before.tick) return Status::success();
    // First restore experiment: exactly the already-requested repeat, with
    // no input producer, task completion, world tail, or new epoch admitted.
    if (target != before.tick + 1 || before.phase != Sc6ReplayExecutor::Phase::RepeatDecision
        || !EngineField<std::uint8_t>(manager_, 0x1462))
        return Status::failure(FailureCode::UnsupportedContent);
    const auto* task = executor_.task_groups().pending_manager_task();
    ++depth_;
    AdvanceEngine();
    --depth_;
    if (simulation_->continuation().tick != target || engine_phase_ != EnginePhase::World
        || !task || executor_.task_groups().pending_manager_task() != task)
    {
        interior_phase_ = InteriorPhase::Failed;
        return Status::failure(FailureCode::AdvanceFailed);
    }
    return Status::success();
}

Status Sc6ReplayHost::Restore(const Checkpoint& target, const RestoreControl* control) noexcept
{
    if(SeekOwnsExecution() && !seek_driving_) return Status::failure(FailureCode::IllegalTransition);
    if (historical_restore_ || checkpoint_restoring_ || capture_operation_.phase!=CapturePhase::Idle) return Status::failure(FailureCode::IllegalTransition);
    // Completed-application capture is available for ownership work. Restoring
    // it is not admitted until scheduler/latent/presentation state is covered.
    if (target.boundary == PauseBoundary::CompletedApplication)
        return Status::failure(FailureCode::UnsupportedContent);
    auto status = ValidateCheckpointLease(target);
    if (!status.ok()) return status;
    // The CRT guard must describe the encoded image that the adapter will
    // actually restore, not merely a separately copied metadata field.
    if (!checkpoint_capture_) return Status::failure(FailureCode::ContextUnavailable);
    status = checkpoint_capture_->ValidateSnapshotUcrt(target.gameplay, target.ucrt);
    if (!status.ok()) return status;
    const auto current_execution = simulation_->continuation();
    auto same_boundary = target.execution;
    same_boundary.tick = current_execution.tick;
    if (same_boundary != current_execution) return Status::failure(FailureCode::RestorePreflightFailed);
    try
    {
        if (!restore_undo_) restore_undo_ = std::make_unique<Checkpoint>();
        status = Capture(*restore_undo_);
    }
    catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    if (!status.ok()) return status;
    Sc6ReplayWorldState::PreparedRestore target_world;
    // The world participant retains the exact native B arrays until the
    // enclosing commit, avoiding a second independently cloned undo graph.
    // Both captured images remain charged. Whole replay accounting is external.
    const auto world_budget = Schema::replay_checkpoint_memory_budget;
    if (restore_undo_->world.owned_bytes() > world_budget)
        return Status::failure(FailureCode::CapacityExceeded);
    status = target.world.PrepareRestore(image_base_, world_,
        world_budget - restore_undo_->world.owned_bytes(), target_world);
    if (!status.ok()) return status;
    struct TransactionScope
    {
        bool& active;
        explicit TransactionScope(bool& value) noexcept : active(value) { active = true; }
        ~TransactionScope() { active = false; }
    } transaction(checkpoint_restoring_);
    // Copy the query before invoking caller code. The caller may update its
    // cancellation request, but cannot replace this transaction's callbacks.
    const auto query = control ? *control : RestoreControl{};
    if (query.cancel_requested && query.cancel_requested(query.context, RestorePhase::BeforeWrites))
        return Status::failure(FailureCode::Cancelled);
    // The adapter restores the actual native CRT stream after native gameplay
    // writes. The enclosing undo includes that stream as well.
    const auto prior_mode = checkpoint_broker_->mode();
    status = checkpoint_capture_->EnsureRestoreOwnership(thread_);
    if (!status.ok()) return status;
    status = checkpoint_capture_->RestoreAndVerify(target.gameplay);
    if (status.ok()) status = target_world.Publish();
    if (status.ok()) status = simulation_->RestoreContinuation(current_execution, target.execution);
    if (status.ok()) status = ValidateCheckpointLease(target);
    if (status.ok() && query.cancel_requested
        && query.cancel_requested(query.context, RestorePhase::BeforeCommit))
        status = Status::failure(FailureCode::Cancelled);
    if (status.ok()) status = target_world.Commit();
    if (!status.ok())
    {
        if (status.code == FailureCode::Cancelled)
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] checkpoint restore cancelled before commit; recovering original paused state\n"));
        else
        {
        // Preserve the first failure before the undo reuses adapter diagnostics.
        const auto failure = checkpoint_capture_->restore_validation();
        RC::Output::send<RC::LogLevel::Warning>(STR(
            "[HorseMod] checkpoint animation failure issue={} observed={}\n"),
            static_cast<unsigned>(checkpoint_capture_->transient_animation_topology_issue()),
            checkpoint_capture_->transient_animation_topology_observed());
        RC::Output::send<RC::LogLevel::Warning>(STR(
            "[HorseMod] checkpoint restore failed code={} phase={} differences={} operations={} issue={} index={} observed_a={} observed_b={} expected_a={} expected_b={}\n"),
            static_cast<unsigned>(status.code), checkpoint_capture_->restore_failure_phase(),
            checkpoint_capture_->restore_difference_mask(), checkpoint_capture_->restore_operation_failure_mask(),
            static_cast<unsigned>(failure.issue), failure.index, failure.observed_a, failure.observed_b,
            failure.expected_a, failure.expected_b);
        }
        // Keep the enclosing undo alive through rebuild AND final verification.
        const auto undo_execution = simulation_->RestoreContinuation(
            simulation_->continuation(), restore_undo_->execution);
        const auto undo_world = target_world.Undo();
        const auto undo_game = checkpoint_capture_->RestoreAndVerify(restore_undo_->gameplay);
        const auto undo_lease = ValidateCheckpointLease(*restore_undo_);
        if (!undo_game.ok() || !undo_world.ok() || !undo_execution.ok() || !undo_lease.ok())
        {
            const auto undo_failure = checkpoint_capture_->restore_validation();
            RC::Output::send<RC::LogLevel::Warning>(STR(
                "[HorseMod] checkpoint undo failed game_code={} world_code={} execution_code={} lease_code={} phase={} differences={} operations={} issue={} index={} observed_a={} observed_b={} expected_a={} expected_b={}\n"),
                static_cast<unsigned>(undo_game.code), static_cast<unsigned>(undo_world.code), static_cast<unsigned>(undo_execution.code),
                static_cast<unsigned>(undo_lease.code), checkpoint_capture_->restore_failure_phase(),
                checkpoint_capture_->restore_difference_mask(), checkpoint_capture_->restore_operation_failure_mask(),
                static_cast<unsigned>(undo_failure.issue), undo_failure.index, undo_failure.observed_a,
                undo_failure.observed_b, undo_failure.expected_a, undo_failure.expected_b);
            interior_phase_ = InteriorPhase::Failed;
            return Status::failure(FailureCode::UndoFailed);
        }
    }
    if (prior_mode == UcrtRandBrokerMode::Observing && !checkpoint_broker_->ReleaseOwnership(thread_).ok())
    {
        interior_phase_ = InteriorPhase::Failed;
        return Status::failure(FailureCode::UndoFailed);
    }
    return status;
}

bool Sc6ReplayHost::BeginCorrectedCapture(CaptureWitness* witness) noexcept
{
    // Only the existing seek/rolling driver may capture regenerated history.
    // B stays in particle_copy_ through this entire independent acquisition.
    if(!witness || !seek_driving_ || capture_operation_.phase!=CapturePhase::Idle
        || corrected_capture_ || !historical_restore_ || !historical_restore_->execution
        || !checkpoint_restoring_ || !HistoricalExecutionAdmitted()
        || (corrected_particle_copy_ && corrected_particle_copy_->witness().phase!=Sc6ReplayParticleCopy::Phase::Released))return false;
    corrected_capture_=true;
    if(CaptureOperation(CaptureAction::Begin,witness))return true;
    corrected_capture_=false;return false;
}

// One host owner advances capture; UI/test callers never poll GPU completion.
bool Sc6ReplayHost::CaptureOperation(CaptureAction action,CaptureWitness* witness,CheckpointHandle* output) noexcept
{
    auto* self=active_;
    if(!witness) return false;
    if(!self) {*witness={CapturePhase::Failed,FailureCode::ContextUnavailable};return false;}
    if(GetCurrentThreadId()!=self->thread_) {*witness={CapturePhase::Failed,FailureCode::WrongThread};return false;}
    auto& operation=self->capture_operation_;
    const auto publish=[&](bool result) {*witness=operation;if(!result) witness->failure=FailureCode::IllegalTransition;return result;};
    if(action!=CaptureAction::Read && self->corrected_capture_ && self->historical_restore_) {
        // Cancellation must not drop the provisional checkpoint while a
        // command owns it or while material lifetime remains unresolved.
        // Classify only after render completion; the terminal witness is
        // sticky even when a caller asks to Cancel/Release the failed capture.
        if(self->historical_restore_->material_failed || self->surface_event_
            || self->particle_command_pending_.load(std::memory_order_acquire)
            || !self->CheckHistoricalMaterialBoundary()) {*witness=operation;return false;}
    }
    if(self->SeekOwnsExecution() && !self->seek_driving_ && action!=CaptureAction::Read) return publish(false);
    if(self->depth_) return publish(false);
    switch(action) {
    case CaptureAction::Read: return publish(true);
    case CaptureAction::CheckTargetEligibility:
        if(operation.phase!=CapturePhase::Idle || !self->CheckBinding()
            || self->interior_phase_!=InteriorPhase::Holding || self->pause_boundary_!=PauseBoundary::CompletedApplication
            || self->surface_event_ || !self->CanRetireSeekCheckpoint())return publish(false);
        *witness=operation;witness->tick=self->simulation_->continuation().tick;
        try {witness->failure=Sc6ReplayHudState::InspectTargetEligibility(self->image_base_,self->world_,self->manager_).code;}
        catch(...) {witness->failure=FailureCode::ContextUnavailable;}
        return true; // Query accepted; failure describes eligibility, not index state.
    case CaptureAction::Begin:
        if(operation.phase!=CapturePhase::Idle || ((self->historical_restore_ || self->checkpoint_restoring_) && !self->corrected_capture_)
            || !self->CheckBinding() || self->interior_phase_!=InteriorPhase::Holding
            || self->pause_boundary_!=PauseBoundary::CompletedApplication || self->surface_event_
            || self->particle_command_pending_.load() || !self->engine_idle() || !self->executor_.idle()
            || !self->executor_.arena_empty() || !self->simulation_->interval_complete()
            || !self->held_surface_ || !self->held_surface_->retained
            || (!self->corrected_capture_ && self->particle_copy_ && self->particle_copy_->witness().phase!=Sc6ReplayParticleCopy::Phase::Released
                && (self->particle_copy_->witness().phase!=Sc6ReplayParticleCopy::Phase::ReadyA
                    || !self->particle_copy_->witness().capture_sealed
                    || !self->particle_copy_->captured_image() || self->particle_copy_->blocks_resume())))
            return publish(false);
        // Retire the previous operation on its admitted render thread. Its
        // immutable image remains pinned by any retained checkpoint; Finish
        // releases only the operation's references and mutable scratch.
        (self->corrected_capture_?self->corrected_particle_command_failed_:self->particle_command_failed_).store(false);
        self->capture_started_=std::chrono::steady_clock::now();
        operation={CapturePhase::Preparing,FailureCode::None,self->simulation_->continuation().tick,0,0,true};
        return publish(true);
    case CaptureAction::Cancel:
        if(operation.phase==CapturePhase::Idle || operation.phase==CapturePhase::Cancelled || operation.phase==CapturePhase::Failed)
            return publish(false);
        self->captured_checkpoint_.reset();operation.phase=CapturePhase::Retiring;operation.pending=true;
        return publish(true);
    case CaptureAction::Take:
        if(operation.phase!=CapturePhase::Ready || !output || !self->captured_checkpoint_) return publish(false);
        if(const auto status=self->ValidateCheckpointLease(*self->captured_checkpoint_);!status.ok()) {
            *witness=operation;witness->failure=status.code;return false;
        }
        *output=std::move(self->captured_checkpoint_);operation={};self->corrected_capture_=false;return publish(true);
    case CaptureAction::Release:
        if(operation.phase!=CapturePhase::Cancelled && operation.phase!=CapturePhase::Failed) return publish(false);
        operation={};self->corrected_capture_=false;return publish(true);
    default: return publish(false);
    }
}

std::unique_ptr<Sc6ReplayParticleCopy>& Sc6ReplayHost::CaptureParticleCopyOwner() noexcept
{
    return corrected_capture_ ? corrected_particle_copy_ : particle_copy_;
}

bool Sc6ReplayHost::CaptureCommandFailed() const noexcept
{
    return corrected_capture_ ? corrected_particle_command_failed_.load() : particle_command_failed_.load();
}

void Sc6ReplayHost::AdvanceCaptureOperation() noexcept
{
    auto& operation=capture_operation_;
    auto& copy=CaptureParticleCopyOwner();
    if(operation.phase==CapturePhase::Idle || operation.phase==CapturePhase::Ready
        || operation.phase==CapturePhase::Cancelled || operation.phase==CapturePhase::Failed) return;
    operation.elapsed_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-capture_started_).count();
    // The RHI command mutates both copy ownership and completion state.
    // Acquire its completion before inspecting any of that accounting.
    if(surface_event_ || particle_command_pending_.load(std::memory_order_acquire)) return;
    // A deferred render Finish is command completion only. Classify material
    // work before queuing any retry, including cancellation's retirement path.
    if(corrected_capture_ && !CheckHistoricalMaterialBoundary())return;
    operation.owned_bytes=AdmissionBytes();
    capture_driving_=true;
    const auto fail=[&](FailureCode code) {
        if(operation.failure==FailureCode::None) operation.failure=code;
        captured_checkpoint_.reset();operation.phase=CapturePhase::Retiring;
    };
    const auto queue=[&](ParticleCopyAction action) {
        Sc6ReplayParticleCopy::Witness state{};bool pending{};
        const bool result=ParticleCopyExperiment(action,&state,&pending);
        if(!result) fail(FailureCode::CaptureFailed);
        return result;
    };
    if(operation.phase!=CapturePhase::Retiring && (interior_phase_!=InteriorPhase::Holding
        || !CheckBinding() || simulation_->continuation().tick!=operation.tick || CaptureCommandFailed()))
        fail(FailureCode::CaptureFailed);
    if(operation.phase==CapturePhase::Retiring) {
        if(copy) {
            const auto state=copy->witness();
            if(state.pending) {queue(ParticleCopyAction::RetireInFlight);capture_driving_=false;return;}
            if(state.phase!=Sc6ReplayParticleCopy::Phase::Released) {queue(ParticleCopyAction::Finish);capture_driving_=false;return;}
        }
        if(Sc6ReplayParticleCopy::capture_retirement_pending()) {
            queue(ParticleCopyAction::RetireCaptures);capture_driving_=false;return;
        }
        operation.pending=false;
        operation.phase=operation.failure==FailureCode::None?CapturePhase::Cancelled:CapturePhase::Failed;
        capture_driving_=false;return;
    }
    if(operation.phase==CapturePhase::Finishing) {
        if(copy && copy->witness().phase!=Sc6ReplayParticleCopy::Phase::Released) {
            queue(ParticleCopyAction::Finish);capture_driving_=false;return;
        }
        operation.phase=CapturePhase::Ready;operation.pending=false;
        capture_driving_=false;return;
    }
    if(operation.phase==CapturePhase::Preparing) {
        if(copy && copy->witness().phase!=Sc6ReplayParticleCopy::Phase::Released) {
            queue(ParticleCopyAction::Finish);capture_driving_=false;return;
        }
        copy.reset();
        operation.phase=CapturePhase::Copying;
    }
    if(!copy) {queue(ParticleCopyAction::BeginWithoutReadbacks);capture_driving_=false;return;}
    const auto state=copy->witness();
    if(state.pending) {queue(ParticleCopyAction::Poll);capture_driving_=false;return;}
    if(state.phase!=Sc6ReplayParticleCopy::Phase::ReadyA) {
        fail(FailureCode::CaptureFailed);capture_driving_=false;return;
    }
    if(!state.capture_sealed) {
        operation.phase=CapturePhase::Sealing;queue(ParticleCopyAction::SealCapture);capture_driving_=false;return;
    }
    const auto status=Capture(captured_checkpoint_);
    if(!status.ok() || !captured_checkpoint_ || !captured_checkpoint_->gpu) fail(status.ok()?FailureCode::CaptureFailed:status.code);
    else {
        operation.phase=corrected_capture_?CapturePhase::Finishing:CapturePhase::Ready;operation.pending=corrected_capture_;operation.owned_bytes=AdmissionBytes();
        operation.elapsed_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-capture_started_).count();
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] capture operation ready tick={} elapsed_us={} owned_bytes={} immutable=true gpu_complete=true\n"),operation.tick,operation.elapsed_us,operation.owned_bytes);
    }
    capture_driving_=false;
}
