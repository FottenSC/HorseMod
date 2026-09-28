// Native FEngineLoop_Tick (140396450). Only the engine call is resumable;
// prefix and tail callees finish within their own native lifetimes. No native
// stack or borrowed prefix temporary survives an application return.
bool Sc6ReplayHost::BindApplication()
{
    constexpr std::array<unsigned char, 15> signature{
        0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x70, 0x18,
        0x55, 0x57, 0x41, 0x54};
    auto* image = reinterpret_cast<void*>(image_base_);
    if (std::memcmp(reinterpret_cast<void*>(image_base_ + 0x396450), signature.data(), signature.size()))
        return false;
    // These native statics must have initialized in a preceding native loop.
    // Binding from EngineTickPost never takes over that already-running loop.
    for (const auto offset : {0x41457f0u, 0x4145800u})
    {
        const auto guard = EngineField<int>(image, offset);
        if (guard == 0 || guard == -1) return false;
    }
    if (!EngineField<void*>(image, 0x41457f8)) return false;
    application_loop_ = reinterpret_cast<void*>(image_base_ + 0x406e0c0);
    application_phase_ = ApplicationPhase::Idle;
    application_active_ = application_stop_requested_ = false;
    completed_applications_ = 0;
    unpaced_applications_ = unpaced_admission_rejections_ = 0;
    application_hook_ = std::make_unique<PLH::x64Detour>(image_base_ + 0x396450,
        reinterpret_cast<std::uint64_t>(&TickApplication), &application_original_);
    if (application_hook_->hook()) {
        wind_construction_=std::make_unique<NativeWindConstruction>();
        replay_rendering_=std::make_unique<NativeReplayRendering>();
        widget_clock_=std::make_unique<NativeReplayWidgetClock>();
        emitter_lifetime_=std::make_unique<NativeReplayCpuEmitterLifetime>();
        callback_admission_=std::make_unique<NativeReplayCallbackAdmission>();
        input_source_=std::make_unique<Sc6ReplayInputSource>();
        if(wind_construction_->Bind(image_base_) && replay_rendering_->Bind(image_base_)
            && callback_admission_->Bind(image_base_, world_) && input_source_->Bind(image_base_,checkpoint_session_)
            && widget_clock_->Bind(image_base_,world_) && emitter_lifetime_->Bind(image_base_,this,&ProtectParticleLifecycle)) {
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay widget clock started owner=runtime policy=fixed60_application_widget_v1 tick={}\n"),
                *reinterpret_cast<unsigned*>(image_base_+0x470d0c4));
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay rendering started owner=runtime policy=fixed60_render_v7 tick={}\n"),
                *reinterpret_cast<unsigned*>(image_base_+0x470d0c4));
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] wind construction started owner=runtime policy=zero_initial_distance_v1 tick={}\n"),
                *reinterpret_cast<unsigned*>(image_base_+0x470d0c4));
            return true;
        }
        emitter_lifetime_.reset();widget_clock_.reset();input_source_.reset(); callback_admission_.reset(); replay_rendering_.reset();wind_construction_.reset();
        if(!application_hook_->unHook()) __fastfail(FAST_FAIL_INVALID_ARG);
    }
    application_hook_.reset();
    return false;
}

bool Sc6ReplayHost::UnbindApplication()
{
    if (application_active_ || application_phase_ != ApplicationPhase::Idle) return false;
    if(emitter_lifetime_) {
        if(!emitter_lifetime_->Stop())return false;
        emitter_lifetime_.reset();
    }
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay application pacing policy=physical_clock_fixed60_seek_v1 unpaced={} rejected={}\n"),
        unpaced_applications_, unpaced_admission_rejections_);
    if(widget_clock_) {
        if(!widget_clock_->Stop()) return false;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay widget clock stopped owner=runtime policy=fixed60_application_widget_v1 calls={} failed={} detached=true\n"),
            widget_clock_->calls(),widget_clock_->failed());
        widget_clock_.reset();
    }
    if (input_source_) {
        if(!input_source_->Stop()) return false;
        input_source_.reset();
    }
    if (callback_admission_) {
        if (!callback_admission_->Stop()) return false;
        const auto stamp=callback_admission_->stamp();
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] callback admission stopped latent={} streamable={} detached=true\n"),stamp[0],stamp[1]);
        callback_admission_.reset();
    }
    if(replay_rendering_) {
        if(!replay_rendering_->Stop()) return false;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay rendering stopped owner=runtime policy=fixed60_render_v7 constructed={} completed={} failed={} detached=true\n"),
            replay_rendering_->constructed(),replay_rendering_->completed(),replay_rendering_->failed());
        replay_rendering_.reset();
    }
    if (wind_construction_ && !wind_construction_->Stop()) return false;
    if (wind_construction_) RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] wind construction stopped owner=runtime policy=zero_initial_distance_v1 initialized={} failed={} detached=true\n"),
        wind_construction_->initialized(),wind_construction_->failed());
    wind_construction_.reset();
    if (application_hook_ && !application_hook_->unHook()) return false;
    application_hook_.reset();
    application_original_ = 0;
    application_loop_ = application_media_ = nullptr;
    application_stop_requested_ = false;
    return true;
}

bool Sc6ReplayHost::DirectApplicationRendering() const
{
    auto* image = reinterpret_cast<void*>(image_base_);
    return !EngineField<bool>(image, 0x4351840)
        && (!EngineField<bool>(image, 0x419718c)
            || GetCurrentThreadId() == EngineField<DWORD>(image, 0x419716c));
}

void Sc6ReplayHost::DispatchApplicationRenderTask(std::uintptr_t builder_rva)
{
    std::uintptr_t storage[3]{};
    auto* builder = EngineNative<std::uintptr_t*>(image_base_, builder_rva, storage, 0, 0xff);
    auto* task = reinterpret_cast<void*>(builder[0]);
    auto* event = EngineField<void*>(task, 0x18);
    if (event) InterlockedIncrement(&EngineField<LONG>(event, 0x48));
    EngineNative(image_base_, 0x1e63b80, task,
        reinterpret_cast<void*>(builder[1]), static_cast<int>(builder[2]), true);
    if (event && InterlockedDecrement(&EngineField<LONG>(event, 0x48)) == 0)
        EngineNative(image_base_, 0xd2ebe0, event);
}

bool Sc6ReplayHost::EnterUnpacedApplicationTime(void* engine)
{
    // Bounded port of 142189040's normal fixed-rate branch. Physical clocks,
    // smoothing callbacks and the native world-context clamp remain physical/
    // native. Only the admitted simulation delta and wall-clock wait separate.
    // No benchmark flag, QPC offset, engine epoch, world/render task or particle
    // publication is changed or skipped. Call only before a fresh application.
    auto* image = reinterpret_cast<void*>(image_base_);
    const auto guard = EngineField<int>(image, 0x43b3780);
    if (engine != engine_ || EngineField<std::uintptr_t>(engine, 0) != image_base_ + 0x38c6af0
        || guard == 0 || guard == -1 || EngineField<bool>(image, 0x416a943)
        || EngineField<bool>(image, 0x416a944)
        || !(EngineField<unsigned>(engine, 0x648) & 0x40)
        || EngineField<float>(engine, 0x64c) != 60.0f) return false;
    LARGE_INTEGER counter{};
    auto query = EngineField<BOOL (WINAPI*)(LARGE_INTEGER*)>(image, 0x322c678);
    if (!query || !query(&counter)) return false;
    const double measured = static_cast<double>(counter.QuadPart)
        * EngineField<double>(image, 0x415cd90) + EngineField<double>(image, 0x325bcc0);
    const double reference = EngineField<double>(image, 0x43b3778);
    const double previous = EngineField<double>(image, 0x418a9d8);
    const float elapsed = static_cast<float>(measured - reference);
    if (!std::isfinite(measured) || !std::isfinite(previous)
        || !std::isfinite(elapsed) || elapsed < 0.0f) return false;

    EngineField<double>(image, 0x418a9e0) = previous;
    // A preceding paced call can publish reference+1/rate slightly ahead of
    // QPC. Never rewind that process clock; do not advance it by replay ticks.
    EngineField<double>(image, 0x418a9d8) = measured < previous ? previous : measured;
    EngineVirtual(engine, 0x288, elapsed, true);
    (void)EngineVirtual<float>(engine, 0x270, elapsed, true);
    EngineField<double>(image, 0x418a9e8) = 0.0;
    EngineField<unsigned char>(image, 0x43b3784) = 1;
    EngineField<double>(image, 0x4070ba0) = 1.0 / 60.0;
    EngineField<double>(image, 0x43b3778) = measured;

    // Preserve 142189040's optional GameEngine clamp, including its native
    // queries. The exact GameEngine vtable above already establishes its class.
    const float clamp = EngineField<float>(engine, 0xcc8);
    if (clamp > 0.0f) {
        auto** contexts = EngineField<void**>(engine, 0xbe8);
        const int count = EngineField<int>(engine, 0xbf0);
        for (int i = 0; i < count; ++i) {
            auto* context = contexts[i];
            if (EngineField<unsigned char>(context, 0) != 1
                || !EngineField<void*>(context, 0x240)) continue;
            auto* owner = EngineField<void*>(context, 0x298);
            const int players = EngineNative<int>(image_base_, 0x1b02eb0,
                EngineField<void*>(context, 0x240));
            if (owner) {
                auto* player = EngineField<void*>(owner, 0xf0);
                if (player && EngineVirtual<int>(player, 0x670) == players
                    && static_cast<double>(clamp) < EngineField<double>(image, 0x4070ba0))
                    EngineField<double>(image, 0x4070ba0) = static_cast<double>(clamp);
            }
            break;
        }
    }
    if (++unpaced_applications_ == 1)
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay unpaced application started policy=physical_clock_fixed60_seek_v1 tick={} delta={} render_tasks=retained\n"),
            simulation_->continuation().tick, EngineField<double>(image, 0x4070ba0));
    return true;
}

void Sc6ReplayHost::EnterApplication()
{
    const bool measure_seek=seek_.witness.phase==SeekPhase::Advancing;
    const auto measured_start=std::chrono::steady_clock::now();
    auto* image = reinterpret_cast<void*>(image_base_);
    EngineNative(image_base_, 0xd4d260, EngineNative<void*>(image_base_, 0xd470f0));
    if (!EngineField<bool>(image, 0x4351841) && !EngineField<int>(image, 0x4168020))
        EngineNative(image_base_, 0x15efc10);
    if (EngineVirtual<bool>(EngineNative<void*>(image_base_, 0x1afdb10), 0x58))
        EngineVirtual(EngineNative<void*>(image_base_, 0x1afdb10), 0x60);
    if (auto* module = EngineNative<void*>(image_base_, 0xdea8d0)) EngineVirtual(module, 8);
    if (!EngineField<void*>(image, 0x415cd80)) EngineNative(image_base_, 0xd34a60);
    EngineVirtual(EngineField<void*>(image, 0x415cd80), 0x38);
    if (DirectApplicationRendering()) EngineNative(image_base_, 0x39a950);
    else DispatchApplicationRenderTask(0x39a160);
    EngineNative(image_base_, 0x3999c0, reinterpret_cast<void*>(image_base_ + 0x4075fc0));
    EngineVirtual(EngineNative<void*>(image_base_, 0xdc9bc0), 0x40);
    if ((EngineField<bool>(image, 0x416a943)
            && EngineField<std::uint64_t>(application_loop_, 0x28)
            && EngineField<std::uint64_t>(image, 0x4197170) > EngineField<std::uint64_t>(application_loop_, 0x28))
        || (EngineField<double>(application_loop_, 0x20) != 0.0
            && EngineField<double>(application_loop_, 0x20) < EngineField<double>(application_loop_, 0x18)))
        EngineNative(image_base_, 0xe0e590, false);
    auto* engine = EngineField<void*>(image, 0x43b3068);
    const bool unpaced = seek_.witness.phase == SeekPhase::Advancing
        && tick_advance_.phase == TickAdvancePhase::Advancing;
    if (!unpaced || !EnterUnpacedApplicationTime(engine)) {
        if (unpaced) ++unpaced_admission_rejections_;
        EngineNative(image_base_, 0x2189040, engine);
    }
    // XMM1 at 1403965EB..5FE is an actual second argument, absent from the
    // old decompiler prototype of this native audio/update callee.
    EngineNative(image_base_, 0x1d29d80, engine, static_cast<float>(EngineField<double>(image, 0x4070ba0)));
    EngineVirtual(EngineField<void*>(image, 0x41971c8), 0x58);
    {
        struct alignas(8) DelegateStorage
        {
            std::uintptr_t inline_words[4]{};
            void* heap{};
            std::uint64_t reserved{};
            int count{};
            int padding{};
        } storage;
        static_assert(offsetof(DelegateStorage, heap) == 0x20);
        static_assert(offsetof(DelegateStorage, count) == 0x30);
        EngineNative(image_base_, 0x3a1a70, &storage, 0, 2u, std::int64_t{16});
        storage.count = 2;
        auto* delegate = storage.heap ? storage.heap : &storage;
        EngineField<std::uintptr_t>(delegate, 0) = image_base_ + 0x325baf0;
        EngineField<std::uintptr_t>(delegate, 8) = image_base_ + 0x15e90a0;
        EngineField<std::uint64_t>(delegate, 0x18) = EngineNative<std::uint64_t>(image_base_, 0xd24030);
        EngineField<std::uintptr_t>(delegate, 0) = image_base_ + 0x325bb60;
        EngineNative(image_base_, 0x2d2bc0, static_cast<void*>(nullptr), &storage);
        if (storage.count)
        {
            EngineVirtual(storage.heap ? storage.heap : &storage, 0x48, false);
            if (storage.heap) storage.heap = EngineNative<void*>(image_base_, 0xd51430,
                storage.heap, std::size_t{0}, 0u);
            storage.count = 0;
        }
        if (storage.heap) EngineNative(image_base_, 0xd46a00, storage.heap);
    }
    EngineNative(image_base_, 0x399ad0);
    if (DirectApplicationRendering())
    {
        EngineNative(image_base_, 0x15edaf0);
        EngineField<std::uint8_t>(image, 0x4096828) = 1;
        EngineNative(image_base_, 0x15ed2e0);
    }
    else DispatchApplicationRenderTask(0x39a480);
    EngineNative(image_base_, 0xe0c3b0, true);
    application_idle_ = EngineNative<bool>(image_base_, 0x3a20e0);
    if (application_idle_) EngineNative(image_base_, 0xe1fa80, EngineField<float>(image, 0x3e8a180));
    if (EngineField<float>(image, 0x43b4ac0) != 0.0f)
    {
        if (auto* world = EngineField<void*>(image, 0x43b4db8))
        {
            auto* settings = EngineNative<void*>(image_base_, 0x21bc760, world, 0, true);
            if (EngineField<float>(settings, 0x488) != EngineField<float>(image, 0x43b4ac0))
                EngineField<float>(EngineNative<void*>(image_base_, 0x21bc760, world, 0, true), 0x488)
                    = EngineField<float>(image, 0x43b4ac0);
        }
        EngineField<float>(image, 0x43b4ac0) = 0.0f;
    }
    EngineNative(image_base_, 0xd54fb0, EngineNative<void*>(image_base_, 0xd470e0));
    if (auto* slate = EngineField<void*>(image, 0x42a8dd8); slate && !application_idle_)
    {
        EngineNative(image_base_, 0x10be9b0, slate);
        EngineNative(image_base_, 0x10aa630, slate);
    }
    std::uint64_t media_name{};
    EngineNative<void*>(image_base_, 0xdf5c80, &media_name, "Media", 1);
    application_media_ = EngineNative<void*>(image_base_, 0xdecf90,
        EngineNative<void*>(image_base_, 0xdea5f0), media_name, false);
    if (application_media_) EngineVirtual(application_media_, 0xa8);
    application_phase_ = ApplicationPhase::Engine;
    if(measure_seek) seek_.witness.prefix_us+=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-measured_start).count();
}

void Sc6ReplayHost::FinishApplication()
{
    const bool measure_seek=seek_.witness.phase==SeekPhase::Advancing;
    const auto measured_start=std::chrono::steady_clock::now();
    auto* image = reinterpret_cast<void*>(image_base_);
    if (application_media_) EngineVirtual(application_media_, 0x98);
    EngineVirtual(EngineNative<void*>(image_base_, 0x1afdb10), 0x50);
    if (auto* pending = EngineField<void*>(image, 0x43a5d50)) EngineNative(image_base_, 0x20b6980, pending, true, 0);
    if (auto* pending = EngineField<void*>(image, 0x43945e0)) EngineNative(image_base_, 0x2d2bc0, pending);
    if (application_media_) EngineVirtual(application_media_, 0xb0);
    void* demo_event{};
    // The submitted demo task borrows this payload, just as native does. It
    // remains in scope until its completion wait below; this tail never yields.
    struct DemoPayload { void* driver{}; float delta{}; } demo;
    auto* engine = EngineField<void*>(image, 0x43b3068);
    if (EngineVirtual<bool>(engine, 0x478))
    {
        demo.delta = static_cast<float>(EngineField<double>(image, 0x4070ba0));
        if (auto* viewport = EngineField<void*>(engine, 0x618))
            if (auto* world = EngineVirtual<void*>(viewport, 0x138))
                if (auto* driver = EngineField<void*>(world, 0xb8);
                    driver && EngineNative<bool>(image_base_, 0x1e1c260, driver))
                {
                    demo.driver = driver;
                    std::uintptr_t storage[3]{};
                    auto* builder = EngineNative<std::uintptr_t*>(image_base_, 0x39a610, storage, 0, 2);
                    auto* task = reinterpret_cast<void*>(builder[0]);
                    EngineField<std::uintptr_t>(task, 0x10) = image_base_ + 0x399b70;
                    EngineField<void*>(task, 0x18) = &demo;
                    demo_event = EngineField<void*>(task, 0x28);
                    if (demo_event) InterlockedIncrement(&EngineField<LONG>(demo_event, 0x48));
                    EngineNative(image_base_, 0x3a1ec0, task,
                        reinterpret_cast<void*>(builder[1]), static_cast<int>(builder[2]), true);
                }
    }
    if (EngineField<void*>(image, 0x42a8dd8) && !application_idle_)
    {
        EngineNative(image_base_, 0x3a0d50);
        EngineNative(image_base_, 0x10d09a0, EngineField<void*>(image, 0x42a8dd8), std::uint8_t{1});
    }
    if (demo_event)
    {
        auto* retained = demo_event;
        EngineNative(image_base_, 0x3a2340, EngineNative<void*>(image_base_, 0xd24400), &demo_event, 0xff);
        demo_event = nullptr;
        if (InterlockedDecrement(&EngineField<LONG>(retained, 0x48)) == 0)
            EngineNative(image_base_, 0xd2ebe0, retained);
    }
    EngineVirtual(EngineField<void*>(image, 0x4344608), 0x328,
        static_cast<float>(EngineField<double>(image, 0x4070ba0)));
    if (++EngineField<std::uint64_t>(image, 0x4197170) > 6)
        EngineField<double>(application_loop_, 0x18) += EngineField<double>(image, 0x4070ba0);
    auto* previous_cleanup = EngineField<void*>(application_loop_, 0x38);
    EngineField<void*>(application_loop_, 0x38) = EngineNative<void*>(image_base_, 0x15edb70);
    const auto sync_start=std::chrono::steady_clock::now();
    executor_.SyncFrame(image_base_, reinterpret_cast<void*>(image_base_ + 0x41457d8),
        *EngineField<int*>(image, 0x41457f8) != 0);
    if(measure_seek) seek_.witness.frame_sync_us+=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-sync_start).count();
    if (previous_cleanup)
    {
        EngineNative(image_base_, 0x15e83b0, previous_cleanup);
        EngineNative(image_base_, 0x1f90000, previous_cleanup, 16u);
    }
    EngineNative(image_base_, 0xf2dfa0);
    const auto delta = static_cast<float>(EngineField<double>(image, 0x4070ba0));
    EngineNative(image_base_, 0xd36b90, EngineNative<void*>(image_base_, 0xdc8ca0), delta);
    EngineNative(image_base_, 0xd54cb0, EngineNative<void*>(image_base_, 0xd47180));
    EngineNative(image_base_, 0x2187e10, EngineField<void*>(image, 0x43b3068));
    if (application_media_) EngineVirtual(application_media_, 0xa0);
    EngineNative(image_base_, 0x3999c0, reinterpret_cast<void*>(image_base_ + 0x4076030));
    if (DirectApplicationRendering())
    {
        auto* command_list = EngineNative<void*>(image_base_, 0x15edaf0);
        EngineNative(image_base_, 0x15d9910, command_list);
        auto aligned = (EngineField<std::uintptr_t>(command_list, 0x30) + 7) & ~std::uintptr_t{7};
        if (EngineField<std::uintptr_t>(command_list, 0x38) < aligned + 16)
        {
            EngineNative(image_base_, 0xdbfbc0, static_cast<std::byte*>(command_list) + 0x30, 24);
            aligned = (EngineField<std::uintptr_t>(command_list, 0x30) + 7) & ~std::uintptr_t{7};
        }
        EngineField<std::uintptr_t>(command_list, 0x30) = aligned + 16;
        ++EngineField<int>(command_list, 0x14);
        *EngineField<std::uintptr_t*>(command_list, 8) = aligned;
        EngineField<std::uintptr_t>(command_list, 8) = aligned;
        auto* command = reinterpret_cast<std::uintptr_t*>(aligned);
        command[0] = 0;
        command[1] = image_base_ + 0x39aeb0;
    }
    else DispatchApplicationRenderTask(0x39a2f0);
    float cpu_usage[2]{};
    EngineNative<float*>(image_base_, 0xe18090, cpu_usage);
    application_media_ = nullptr;
    application_phase_ = ApplicationPhase::Idle;
    ++completed_applications_;
    if(measure_seek) seek_.witness.tail_us+=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-measured_start).count();
}

bool Sc6ReplayHost::AdvanceSeekRetirement() noexcept
{
    if(!seek_retirement_pending_) return true;
    if(seek_retirement_failed_ || !CanRetireSeekCheckpoint()) return false;
    // No next engine epoch or world work is admitted while these releases are
    // in flight. Queued images retain their module/native owners on failure.
    bool complete=false;
    if(surface_event_ && !PollSurface(complete)) {seek_retirement_failed_=true;return false;}
    if(surface_event_ || particle_command_pending_.load()) return false;
    if(particle_command_failed_.load()) {seek_retirement_failed_=true;return false;}
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] seek retirement ownership checkpoint_refs={} configurations={} cold_pending_bytes={} render_pending={}\n"),
        released_seek_checkpoint_.use_count(),released_seek_checkpoint_?released_seek_checkpoint_->particle_configurations.size():0,
        Sc6ReplayParticleConfiguration::pending_bytes(),Sc6ReplayParticleCopy::capture_retirement_pending());
    released_seek_checkpoint_.reset();
    struct Driving {bool& value;bool prior;Driving(bool& v):value(v),prior(v){value=true;}~Driving(){value=prior;}} driving(seek_driving_);
    if(Sc6ReplayParticleCopy::capture_retirement_pending()) {
        Sc6ReplayParticleCopy::Witness witness{};bool pending{};
        if(!ParticleCopyExperiment(ParticleCopyAction::RetireCaptures,&witness,&pending)) seek_retirement_failed_=true;
        return false;
    }
    if(!Sc6ReplayParticleConfiguration::RetirePending(image_base_).ok()) {
        seek_retirement_failed_=true;return false;
    }
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] seek retirement cold drained pending_bytes={} render_pending={}\n"),
        Sc6ReplayParticleConfiguration::pending_bytes(),Sc6ReplayParticleCopy::capture_retirement_pending());
    seek_retirement_pending_=false;
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] seek checkpoint retired tick={} application_idle=true\n"),simulation_->continuation().tick);
    return true;
}

bool Sc6ReplayHost::AdmitGroundUpdate()
{
    if(application_phase_!=ApplicationPhase::Idle || !engine_idle())return false;
    if(!ground_update_pending_) {
        Sc6ReplayGroundDebrisState::UpdateDiagnostic diagnostic;
        const auto status=Sc6ReplayGroundDebrisState::ValidateUpdate(image_base_,manager_,world_,diagnostic);
        if(diagnostic.body_inventory.scan_complete) {
            const auto& current=diagnostic.body_inventory;
            // One bounded A617 membership witness per retained session. It
            // runs on ordinary forward application before any A publication.
            const bool target_probe=simulation_->continuation().tick==617
                && ground_body_inventory_probe_session_!=checkpoint_session_;
            bool report_rows=!status.ok() || target_probe;
            if(target_probe) {
                ground_body_inventory_probe_session_=checkpoint_session_;
                RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] ground body A617 probe session={} tick=617 actors={} scan_complete=true read_only=true\n"),
                    checkpoint_session_,current.count);
                for(unsigned i=0;i<diagnostic.meshes;++i) {
                    const auto& child=diagnostic.children[i];
                    RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] ground body A617 child={} session={} component={:x} component_index={} component_serial={} root={:x} root_actor={:x} root_slot={} ring_slot={} read_only=true\n"),
                        i,checkpoint_session_,child.component,child.component_index,child.component_serial,
                        child.root,child.root_actor,child.root_slot,child.ring_slot);
                }
            }
            if(ground_body_inventory_.scan_complete && ground_body_inventory_session_==checkpoint_session_) {
                const auto change=ground_body_inventory_.FirstDifference(current);
                if(change.kind!=ReplayPhysicsBodyInventory::Difference::None) {
                    report_rows=true;
                    RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] ground body inventory change={} tick={} session={} before_count={} after_count={} before_scene={:x} before_slot={} before_actor={:x} before_component={:x} before_body={:x} before_flags={} before_index={} before_serial={} after_scene={:x} after_slot={} after_actor={:x} after_component={:x} after_body={:x} after_flags={} after_index={} after_serial={} read_only=true generation_join=provisional\n"),
                        RC::to_generic_string(ReplayPhysicsBodyInventory::Name(change.kind)),simulation_->continuation().tick,
                        checkpoint_session_,ground_body_inventory_.count,current.count,change.before.scene,
                        change.before.scene_slot,change.before.actor,
                        change.before.component,change.before.body,change.before.body_flags,change.before.component_index,
                        change.before.component_serial,change.after.scene,change.after.scene_slot,change.after.actor,
                        change.after.component,change.after.body,change.after.body_flags,change.after.component_index,
                        change.after.component_serial);
                }
            } else {
                report_rows=true;
                RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] ground body inventory baseline tick={} session={} actors={} scan_complete=true read_only=true generation_join=provisional\n"),
                    simulation_->continuation().tick,checkpoint_session_,current.count);
            }
            if(report_rows)for(unsigned i=0;i<current.count;++i) {
                const auto& row=current.bodies[i];
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] ground body inventory row={} tick={} session={} scene={:x} scene_slot={} actor={:x} body={:x} component={:x} component_index={} component_serial={} indexed={} type={:x} role={} body_flags={} actor_flags={} read_only=true generation_join=provisional\n"),
                    i,simulation_->continuation().tick,checkpoint_session_,row.scene,row.scene_slot,row.actor,row.body,row.component,
                    row.component_index,row.component_serial,row.component_serial>0,row.type,
                    static_cast<unsigned>(row.role),row.body_flags,row.actor_flags);
            }
            if(status.ok()) {ground_body_inventory_=current;ground_body_inventory_session_=checkpoint_session_;}
        }
        if(status.ok())return true;
        ground_update_pending_=true;
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] ground update admission rejected check={} owner={:x} tick={} roots={} meshes={} before_native_application=true owners_retained=true\n"),
            RC::to_generic_string(diagnostic.check),diagnostic.owner,simulation_->continuation().tick,diagnostic.roots,diagnostic.meshes);
        if(diagnostic.physics_scene) {
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] ground physics callback census base={:x} scene={:x} collection={:x} count={} recursion={} observed={} complete={} read_only=true\n"),
                image_base_,diagnostic.physics_scene,diagnostic.physics_collection,diagnostic.physics_count,
                diagnostic.physics_recursion,diagnostic.physics_observed,diagnostic.physics_complete);
            for(unsigned i=0;i<diagnostic.physics_observed;++i) {
                const auto& row=diagnostic.physics_callbacks[i];
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] ground physics callback row={} entry={:x} storage_count={} callable={:x} vtable={:x} invoke={:x} q0={:x} q1={:x} q2={:x} q3={:x} readable={} read_only=true\n"),
                    i,row.entry,row.storage_count,row.callable,row.vtable,row.invoke,row.words[0],row.words[1],row.words[2],row.words[3],row.readable);
            }
        }
    }
    // An ordinary application has not started. Wait for its predecessor's
    // display completion, then use the existing completed-boundary hold and
    // cancellation owner. Never skip a component callback within a native tick.
    if(surface_event_ || !Horse::GameImGui::PresentHook::instance().replay_surface_ready())return false;
    const bool recoverable=historical_restore_ && historical_restore_->execution
        && historical_restore_->execution->ownership.retains_undo() && !historical_restore_->witness.commit_decided;
    tick_advance_.phase=TickAdvancePhase::Settling;
    if(recoverable) {tick_advance_context_=this;tick_advance_observer_=&ObserveSeekHold;}
    TrySuspendApplication();
    if(interior_phase_!=InteriorPhase::Holding)return false;
    ground_update_pending_=false;
    if(recoverable) {
        auto& operation=historical_restore_->witness;
        operation.failure=FailureCode::UnsupportedContent;operation.participant="ground_update_consumers";
        RestoreOperationWitness result{};
        struct Driving {bool& value;bool prior;Driving(bool& v):value(v),prior(v){value=true;}~Driving(){value=prior;}} driving(seek_driving_);
        if(RestoreOperation(RestoreOperationAction::Cancel,nullptr,&result))return false;
    }
    // Without a reversible transaction this is a terminal unsupported domain.
    // The owned hold reports failure; no subsequent native application runs.
    tick_advance_.phase=TickAdvancePhase::Failed;tick_advance_.failure=FailureCode::UnsupportedContent;
    interior_phase_=InteriorPhase::Failed;
    return false;
}

void Sc6ReplayHost::TickApplication(void* loop)
{
    auto* self = active_;
    if (!self || self->application_active_ || GetCurrentThreadId() != self->thread_
        || loop != self->application_loop_) __fastfail(FAST_FAIL_INVALID_ARG);
    // No service, observer, retirement, UI or arbitrary pump while a native
    // consumer task is retained. Resume continues this same application.
    if (self->executor_.task_groups().held_consumer_task()) {
        self->application_active_=true;
        if (!self->AdvanceConsumerTask()) {
            self->application_active_=false;Sleep(1);return;
        }
        goto consumer_application_tail;
    }
    // This is outside every application/native stack. A deferred native scene
    // stop may publish only after this owner has completely detached.
    if(Sc6ReplayParticleConfiguration::retirement_pending())self->seek_retirement_pending_=true;
    if(self->ServiceSessionExit()) { Sleep(1); return; }
    self->application_active_ = true;
    // A completed-application pause returns before FinishApplication below.
    // Service queued index retirement at that already-completed boundary too;
    // cancellation must not require admitting another simulation interval.
    if(self->application_phase_==ApplicationPhase::Idle && self->CanRetireSeekCheckpoint())
        self->FinishIndexAtApplicationBoundary();
    if(self->seek_retirement_pending_ && self->CanRetireSeekCheckpoint()
        && !self->AdvanceSeekRetirement()) {
        self->application_active_=false;Sleep(1);return;
    }
    if(self->IndexBoundaryRequiresCompletion()) {
        self->TrySuspendApplication();
        self->application_active_=false;Sleep(1);return;
    }
    if (self->HasInteriorContinuation())
    {
        self->AdvanceInteriorHold();
        if (self->pause_boundary_ == PauseBoundary::CompletedApplication)
        {
            self->application_active_ = false;
            Sleep(1);
            return; // The previous application tail already completed.
        }
        if (!self->engine_idle())
        {
            self->application_active_ = false;
            Sleep(1);
            return;
        }
    }
    else
    {
        if (self->tick_advance_.phase == TickAdvancePhase::Releasing
            && self->interior_phase_ == InteriorPhase::Resumed) {
            if (!self->ArmPause(self->tick_advance_.target, self->tick_advance_context_,
                    self->tick_advance_observer_, PauseBoundary::SimulationTick)) {
                self->tick_advance_.phase = TickAdvancePhase::Failed;
                self->tick_advance_.failure = FailureCode::AdvanceFailed;
                self->pause_boundary_ = PauseBoundary::CompletedApplication;
                self->interior_phase_ = InteriorPhase::Failed;
                self->application_active_ = false;
                return;
            }
            self->tick_advance_.phase = TickAdvancePhase::Arming;
        }
        if (self->interior_phase_ == InteriorPhase::Arming)
        {
            bool complete = false;
            const bool success = self->PollSurface(complete);
            if (complete) self->interior_phase_ = success ? InteriorPhase::Armed : InteriorPhase::Failed;
            // Do not run past a nearby target while its surface admission is
            // still queued on the render thread. No application has started.
            if (!complete || !success) {
                if (complete && self->tick_advance_.phase == TickAdvancePhase::Arming) {
                    self->tick_advance_.phase = TickAdvancePhase::Failed;
                    self->tick_advance_.failure = FailureCode::PresentationFailed;
                    self->pause_boundary_ = PauseBoundary::CompletedApplication;
                }
                self->application_active_ = false;
                Sleep(1);
                return;
            }
            if (self->tick_advance_.phase == TickAdvancePhase::Arming)
                self->tick_advance_.phase = TickAdvancePhase::Advancing;
        }
        if(self->interior_phase_==InteriorPhase::Armed && self->pause_boundary_==PauseBoundary::CompletedApplication
            && self->interior_target_==0 && self->simulation_->continuation().tick==0
            && self->completed_applications_==0 && self->completed_engines_==0) {
            // Arm's render command joined the preceding native application's
            // publication. Do not run the first engine interval past tick zero.
            self->TrySuspendApplication();
            self->application_active_=false;
            return;
        }
    if (self->application_phase_ == ApplicationPhase::Idle) {
        if(!self->AdmitGroundUpdate()) {self->application_active_=false;Sleep(1);return;}
        self->EnterApplication();
    }
    if (self->application_phase_ != ApplicationPhase::Engine) __fastfail(FAST_FAIL_INVALID_ARG);
    auto* image = reinterpret_cast<void*>(self->image_base_);
    const bool measure_seek=self->seek_.witness.phase==SeekPhase::Advancing;
    const auto engine_start=std::chrono::steady_clock::now();
    ReplayNiagaraObservation::World(reinterpret_cast<std::uintptr_t>(self->world_));
    ReplayConsumerFailure::Context(self->simulation_->continuation().tick,
        self->historical_restore_ && self->historical_restore_->execution
            ? static_cast<unsigned>(self->historical_restore_->execution->ownership.phase()) : ~0ull);
    EngineVirtual(EngineField<void*>(image, 0x43b3068), 0x268,
        static_cast<float>(EngineField<double>(image, 0x4070ba0)), self->application_idle_);
    if(measure_seek) self->seek_.witness.engine_us+=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-engine_start).count();
        if (!self->engine_idle())
        {
            if (!self->HasInteriorContinuation() && !self->executor_.task_groups().held_consumer_task()) __fastfail(FAST_FAIL_INVALID_ARG);
            self->application_active_ = false;
            return; // GuardedMain calls again; no engine/world stack remains.
        }
    }
consumer_application_tail:
    self->application_phase_ = ApplicationPhase::Tail;
    self->FinishApplication();
    if (!self->RetireConsumerProbe()) __fastfail(FAST_FAIL_INVALID_ARG);
    self->FinishIndexAtApplicationBoundary();
    if(self->seek_retirement_pending_) self->AdvanceSeekRetirement();
    if (!self->application_stop_requested_) self->TrySuspendApplication();
    self->application_active_ = false;
    if (self->application_stop_requested_)
    {
        if (!self->Stop()) __fastfail(FAST_FAIL_INVALID_ARG);
        RC::Output::send<RC::LogLevel::Default>(STR(
            "[HorseMod] application executor stopped intervals={} idle=true disabled=true\n"),
            self->completed_applications_);
    }
}
