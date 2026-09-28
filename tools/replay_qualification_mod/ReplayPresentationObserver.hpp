#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <polyhook2/Detour/x64Detour.hpp>
#include <mutex>
#include <algorithm>
#include <intrin.h>
#include <filesystem>
#include <fstream>
#include "ReplayIndexCompletion.hpp"
#include "ReplayReceiverObservation.hpp"
#include "../../HorseMod/horselib/deterministic/ReplayDiagnosticTrace.hpp"
#include "ReplayTraceMembership.hpp"
#include "../../HorseMod/horselib/deterministic/NativeWindConstruction.hpp"
#include "../../HorseMod/horselib/deterministic/Sc6ReplayObjectVisit.hpp"
#include "../../HorseMod/horselib/deterministic/NativeReplayRendering.hpp"
#include "../../HorseMod/horselib/deterministic/NativeReplayWidgetClock.hpp"

// Bounded test observer. Tags the actual Slate window-draw task on the game
// thread, then reads the native backbuffer on its render-thread Present path,
// before DXGI/overlay hooks. Saved paused surfaces have no tagged native task.
class ReplayPresentationObserver final
{
public:
    static bool DiagnosticAllowed(unsigned subsystem,unsigned tick) {
        const auto admitted=Horse::Deterministic::g_replay_diagnostics.Admit(subsystem,tick);
        if(admitted<0) RC::Output::send<RC::LogLevel::Warning>(STR("[ReplayQualification] replay diagnostic overflow=true tick={} read_only=true\n"),tick);
        return admitted>0;
    }
    using ReadTick = bool (*)(void*, std::uint32_t&);
    using OutputObserver = void (*)(void*, IDXGISwapChain*, std::uint64_t);
    using SetOutputObserver = bool (*)(void*, OutputObserver);
    // Bounded combat protocol currently admits targets through220 and checks
    // 120 subsequent ticks. Tags and pose producers must cover the same end.
    static constexpr std::uint32_t observation_last_tick = 340;
    std::uint32_t extra_first_{208},extra_last_{328};
    bool index_sequence_{};
    bool ObserveTick(std::uint32_t tick) const noexcept {
        // B300 cancellation uses an unchanged native control, so it does not
        // carry the corrected-input flag that previously selected300..420.
        return (index_sequence_ && ReplayQualification::ObserveIndexSequenceTick(tick))
            || ReplayQualification::ObserveEarlyCombatTarget(tick,extra_first_)
            || (tick>=170 && tick<=observation_last_tick) || (tick>=300 && tick<=449)
            || (tick>=2504 && tick<=2624) || (tick>=extra_first_ && tick<=extra_last_);
    }
    static constexpr std::size_t reserved_bytes = 128ull*1024*1024;
    // GPU diagnostics retain staging/source images up to the existing cap.
    // Ordinary continuation observes CPU publications only: reserve fixed
    // object/slot storage plus 16 MiB for hooks and bounded observation scratch.
    // Never read render-thread allocation counters from the game thread.
    std::size_t ReservationBytes() const noexcept {
        return Horse::Deterministic::ReplayDiagnosticTrace::reservation_bytes + (pass_diagnostics_ ? reserved_bytes
            : sizeof(*this) + sizeof(std::array<Slot,3>) + 16ull*1024*1024
                + (pixel_diagnostics_ ? 32ull*1024*1024 : 0)
                + (coherence_ ? 16ull*1024*1024 : 0));
        // Full-frame-only mode owns three staging images and one retained
        // source. Their descriptor-sized reservation is checked before any
        // allocation below. Per-pass diagnostics retain their larger cap.
    }

    // Completed shader-consumption probes are retired. Keep the launch-time
    // thread admission used by the shared replay automation.
    bool StartStartup(const std::string& run) {
        base_=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const bool owner=ReadField<bool>(reinterpret_cast<void*>(base_),0x419718c)
            && ReadField<DWORD>(reinterpret_cast<void*>(base_),0x419716c)==GetCurrentThreadId();
        if(owner) {startup_run_=RC::to_generic_string(run);startup_recording_=true;}
        return owner;
    }
    void ObserveStartupEngineBefore(float delta) {
        if(!startup_recording_)return;
        startup_current_={++startup_ordinal_,ReadField<unsigned>(reinterpret_cast<void*>(base_),0x470d0c4),0,
            ReadField<unsigned>(reinterpret_cast<void*>(base_),0x416673c),0,std::bit_cast<unsigned>(delta)};
        startup_current_.before_mode=ReadField<unsigned>(reinterpret_cast<void*>(base_),0x4846364);
        startup_current_.before_fighters=StartupFighterMask();
        startup_current_.before_initialized=ReadField<unsigned char>(reinterpret_cast<void*>(base_),0x470d0c1);
        startup_current_.before_launch=ReadStartupLaunch();
    }
    void ObserveStartupEngineAfter() {
        if(!startup_recording_ || !startup_current_.ordinal)return;
        startup_current_.after_tick=ReadField<unsigned>(reinterpret_cast<void*>(base_),0x470d0c4);
        startup_current_.after_rng=ReadField<unsigned>(reinterpret_cast<void*>(base_),0x416673c);
        startup_current_.after_mode=ReadField<unsigned>(reinterpret_cast<void*>(base_),0x4846364);
        startup_current_.after_fighters=StartupFighterMask();
        startup_current_.after_initialized=ReadField<unsigned char>(reinterpret_cast<void*>(base_),0x470d0c1);
        startup_current_.after_launch=ReadStartupLaunch();
        startup_rows_[(startup_current_.ordinal-1)%startup_rows_.size()]=startup_current_;
        if(startup_published_) {
            LogStartupRow(startup_current_);
            if(startup_current_.after_tick>=36)startup_recording_=false;
        }
        startup_current_={};
    }
    void ObserveStartupManager(void* manager) {
        if(!startup_recording_ || startup_published_ || !manager)return;
        const auto input=ReadField<void*>(manager,0x478);
        const std::array<unsigned,3> state{ReadField<unsigned char>(manager,0x1460),
            ReadField<unsigned char>(manager,0x1461),input!=nullptr};
        if(startup_manager_seen_ && state==startup_manager_state_)return;
        if(startup_manager_records_++>=64)return;
        startup_manager_state_=state;startup_manager_seen_=true;
        RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] startup manager readiness run_id={} engine_ordinal={} assets={} phase={} input_present={} seed={:08x} read_only=true\n"),
            startup_run_,startup_ordinal_,state[0],state[1],state[2],ReadField<unsigned>(reinterpret_cast<void*>(base_),0x416673c));
    }
    bool Start(void* context, ReadTick read, const std::string& run, bool native_control, bool pixel_diagnostics, bool pass_diagnostics, void* world,
        bool coherence=false, const std::filesystem::path& directory={}, SetOutputObserver set_output=nullptr, std::uint32_t extra_first=208,std::uint32_t extra_count=120, bool index_sequence=false, bool rolling_coherence=false)
    {
        if (!Horse::Deterministic::g_replay_diagnostics.LoadQualification()) return false;
        if (active_ || started_ || run.size()>128 || extra_count>600 || extra_first>36000-extra_count) return false;
        extra_first_=extra_first;extra_last_=extra_first+extra_count;index_sequence_=index_sequence;rolling_coherence_=rolling_coherence;
        base_ = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if (*reinterpret_cast<unsigned*>(base_+0x4095698)) return false; // Native FX.AllowAsyncTick=0 must be effective before the replay.
        if (!*reinterpret_cast<bool*>(base_+0x416a942)) return false; // Native -FixedSeed admission, not -UseFixedTimeStep.
        if (*reinterpret_cast<bool*>(base_ + 0x434459e)) return false; // Separate RHI worker not admitted.
        constexpr unsigned char signatures[14][12] = {
            {0x40,0x56,0x57,0x48,0x83,0xec,0x48,0x48,0x89,0x5c,0x24,0x40},
            {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89},
            {0x48,0x89,0x5c,0x24,0x18,0x89,0x54,0x24,0x10,0x57,0x48,0x83},
            {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b},
            {0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x48,0x8d,0x6c,0x24},
            {0x40,0x57,0x48,0x83,0xec,0x40,0x4c,0x89,0x64,0x24,0x38,0x48},
            {0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x48,0x8d,0x6c,0x24,0xc9},
            {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89},
            {0x4c,0x89,0x4c,0x24,0x20,0x4c,0x89,0x44,0x24,0x18,0x48,0x89},
            {0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x57,0x48},
            {0x40,0x57,0x41,0x54,0x41,0x57,0x48,0x83,0xec,0x30,0x48,0x83},
            {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b},
            {0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x81,0xec,0xa0,0x00},
            {0x40,0x57,0x41,0x57,0x48,0x83,0xec,0x38,0x0f,0x29,0x74,0x24}};
        // 1417A16D0 constructs 143733310; 1417AB640 calls window drawing
        // 1417A8880, whose RHI end-draw reaches native Present. FViewport's
        // earlier end-draw task does not present this game's window.
        constexpr std::uintptr_t entries[]{0x17a16d0, 0x17ab640, 0x1202100,0x4592c0,0x14f0180,0x1f9bd00,0x1faa2d0,0x1faa850,0x151d730,
            0x11f79a0,0x1f78870,0x1f941e0,0x8cfd70,0x1fa0b80};
        for (unsigned i=0; i<std::size(entries); ++i)
            if (std::memcmp(reinterpret_cast<void*>(base_+entries[i]), signatures[i], 12)) return false;
        Fence(); // No render callback is being patched while in flight.
        context_ = context; read_ = read; run_ = RC::to_generic_string(run); thread_ = GetCurrentThreadId();
        if(startup_recording_ && !startup_published_) {
            const auto first=startup_ordinal_>startup_rows_.size()?startup_ordinal_-startup_rows_.size():0;
            for(auto i=first;i<startup_ordinal_;++i)LogStartupRow(startup_rows_[i%startup_rows_.size()]);
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] startup RNG presentation admission run_id={} engine_ordinal={} native_tick={} seed={:08x} read_only=true\n"),
                startup_run_,startup_ordinal_,ReadField<unsigned>(reinterpret_cast<void*>(base_),0x470d0c4),ReadField<unsigned>(reinterpret_cast<void*>(base_),0x416673c));
            startup_published_=true;
        }
        accepting_=true;error_=0;frames_=bytes_=tagged_tasks_=presents_=tagged_presents_=0;diagnostic_traces_=0;
        last_tick_=generation_=0;source_binding_=nullptr;pose_count_=pose_async_=pose_tick_=0;
        slots_ = std::make_unique<std::array<Slot, 3>>();
        active_ = this; started_ = true;
        wind_control_=native_control;
        pixel_diagnostics_=pixel_diagnostics;pass_diagnostics_=pass_diagnostics;
        coherence_=coherence;coherence_directory_=directory;set_output_=set_output;
        if(wind_control_) {
            if(!wind_construction_.Bind(base_) || !replay_rendering_.Bind(base_) || !widget_clock_.Bind(base_,world)) {Stop();return false;}
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] replay widget clock started owner=control policy=fixed60_application_widget_v1 tick={}\n"),
                *reinterpret_cast<unsigned*>(base_+0x470d0c4));
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] replay rendering started owner=control policy=fixed60_render_v7 tick={}\n"),
                *reinterpret_cast<unsigned*>(base_+0x470d0c4));
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] wind construction started owner=control policy=zero_initial_distance_v1 tick={}\n"),
                *reinterpret_cast<unsigned*>(base_+0x470d0c4));
        }
        const std::uint64_t callbacks[]{reinterpret_cast<std::uint64_t>(&Build),
            reinterpret_cast<std::uint64_t>(&Execute), reinterpret_cast<std::uint64_t>(&Present),reinterpret_cast<std::uint64_t>(&Pose),reinterpret_cast<std::uint64_t>(&PublishView),reinterpret_cast<std::uint64_t>(&InitializeEmitter),reinterpret_cast<std::uint64_t>(&GpuEmitterTick),reinterpret_cast<std::uint64_t>(&CpuEmitterTick),reinterpret_cast<std::uint64_t>(&DrawTranslucentMesh),
            reinterpret_cast<std::uint64_t>(&CommitDrawResources),reinterpret_cast<std::uint64_t>(&CheckParticleCompletion),reinterpret_cast<std::uint64_t>(&CreateGpuEmitter),reinterpret_cast<std::uint64_t>(&TraceBone),reinterpret_cast<std::uint64_t>(&ParticleReceivers)};
        for (unsigned i=0; i<std::size(entries); ++i) {
            if (i>=8 && i<10 && !pass_diagnostics_) continue;
            
            hooks_[i] = std::make_unique<PLH::x64Detour>(base_+entries[i], callbacks[i], &originals_[i]);
            if (!hooks_[i]->hook()) { Stop(); return false; }
        }
        if(coherence_ && !native_control && (!set_output_ || !set_output_(this,[](void* owner,IDXGISwapChain* chain,std::uint64_t tick) {
            auto& self=*static_cast<ReplayPresentationObserver*>(owner);
            if(tick==208 || tick==214 || tick==self.extra_first_ || (self.extra_first_==300 && (tick==300 || tick==2504))) try {
                unsigned generation{};{std::lock_guard lock(self.mutex_);generation=self.generation_;}
                self.CaptureCoherence(chain,static_cast<unsigned>(tick),generation,true);
            } catch(...) {self.error_=34;}
        }))) {Stop();return false;}
        return true;
    }
    bool Stop()
    {
        if (!started_) return true;
        accepting_ = false;
        if(set_output_ && !set_output_(this,nullptr)) return false;
        Fence(); // Joins the tagged native task executions, without pumping gameplay tasks.
        bool pending{};
        for (const auto& slot : *slots_) pending |= slot.pending;
        for (const auto& slot : pass_slots_) pending |= slot.pending;
        pending |= stream_slot_.pending;
        pending |= coherence_pending_;
        for (const auto& slot : input_buffers_) pending |= slot.pending;
        if (pending) return false; // GPU completion is independent of the render fence; retain all leases.
        {
            std::lock_guard lock(mutex_);
            for (const auto& tag : tags_) if (tag.task) return false;
        }
        bool ok = true;
        if(wind_control_) {
            if(!widget_clock_.Stop()) return false;
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] replay widget clock stopped owner=control policy=fixed60_application_widget_v1 calls={} failed={} detached=true\n"),widget_clock_.calls(),widget_clock_.failed());
            if(widget_clock_.failed()) error_=22;
            if(!replay_rendering_.Stop() || !wind_construction_.Stop()) return false;
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] replay rendering stopped owner=control policy=fixed60_render_v7 constructed={} completed={} failed={} detached=true\n"),
                replay_rendering_.constructed(),replay_rendering_.completed(),replay_rendering_.failed());
            if(replay_rendering_.failed()) error_=22;
        }
        for (auto& hook : hooks_) if (hook && hook->isHooked()) ok = hook->unHook() && ok;
        if (!ok) return false;
        for (auto& hook : hooks_) hook.reset();
        slots_.reset(); device_.Reset(); context_d3d_.Reset();
        coherence_source_.Reset();coherence_image_.Reset();coherence_event_.Reset();coherence_context_.Reset();
        stream_slot_={};for(auto& slot:pass_slots_) slot={};for(auto& target:last_draw_targets_) target.Reset();
        for(auto& slot:input_buffers_) slot={};
        started_ = false; active_ = nullptr;
        if(wind_control_) {
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] wind construction stopped owner=control policy=zero_initial_distance_v1 initialized={} failed={} detached=true\n"),
                wind_construction_.initialized(),wind_construction_.failed());
            if(wind_construction_.failed()) error_=21;
        }
        RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] native presentation completed run_id={} frames={} bytes={} error={} tagged_tasks={} presents={} tagged_presents={} detached=true\n"),
            run_, frames_, bytes_+pass_bytes_, error_.load(),tagged_tasks_,presents_,tagged_presents_);
        return !error_.load() && frames_ >= 120;
    }
    ~ReplayPresentationObserver()
    {
        // Normal completion drains before destruction. A failed in-flight
        // observer is retained until owned-process cleanup, never cancelled by timeout.
        if (!Stop()) {
            slots_.release();
            coherence_source_.Detach();coherence_image_.Detach();coherence_event_.Detach();coherence_context_.Detach();
            stream_slot_.image.Detach();stream_slot_.source.Detach();stream_slot_.event.Detach();
            for(auto& slot:pass_slots_) {slot.image.Detach();slot.source.Detach();slot.event.Detach();}
            for(auto& slot:input_buffers_) {slot.image.Detach();slot.source.Detach();slot.event.Detach();}
            for(auto& target:last_draw_targets_) target.Detach();
            for (auto& hook : hooks_) hook.release();
        }
    }
private:
    struct StartupLaunch {int requests{-1};std::uintptr_t callback{};unsigned mode{255},manager{},assets{255},phase{255};std::array<unsigned,6> loaders{};};
    struct StartupRow {std::uint64_t ordinal{};unsigned before_tick{},after_tick{},before_rng{},after_rng{},delta{},before_mode{},after_mode{},before_fighters{},after_fighters{},before_initialized{},after_initialized{};StartupLaunch before_launch{},after_launch{};};
    RC::Unreal::UObject* startup_instance_{};
    std::uint64_t startup_find_ordinal_{};
    StartupLaunch ReadStartupLaunch() {
        StartupLaunch result{};
        // Discover once, retry at most once per60 intervals while startup has
        // no game instance. Per-interval validation uses the indexed slot.
        if(!IsLiveReplayObject(startup_instance_)) {
            startup_instance_=nullptr;
            if(startup_ordinal_<startup_find_ordinal_)return result;
            startup_find_ordinal_=startup_ordinal_+60;
            startup_instance_=RC::Unreal::UObjectGlobals::FindFirstOf(L"LuxGameInstance");
        }
        if(!IsLiveReplayObject(startup_instance_))return result;
        // Native CheckHasAnyBattleRequest140549E50: GI+168 TArray,
        // entries stride50, first callback at entry+0. Comparison data only.
        const auto count=ReadField<int>(startup_instance_,0x170);
        const auto capacity=ReadField<int>(startup_instance_,0x174);
        if(count>=0 && count<=64 && capacity>=count && capacity<=1024) {
            result.requests=count;
            const auto data=ReadField<void*>(startup_instance_,0x168);
            if(count && data) {
                const auto callback=ReadField<std::uintptr_t>(data,0);
                if(callback>=base_ && callback<base_+0x5000000)result.callback=callback-base_;
            }
        }
        if(result.requests>0) {
            auto* portable=ReadField<RC::Unreal::UObject*>(startup_instance_,0x148);
            auto* setup=IsLiveReplayObject(portable) ? ReadField<RC::Unreal::UObject*>(portable,0x28):nullptr;
            if(IsLiveReplayObject(setup))for(unsigned type=0;type<6;++type) {
                auto* part=type ? ReadField<RC::Unreal::UObject*>(setup,0x1e0+(type-1)*8):setup;
                if(!IsLiveReplayObject(part))continue;
                const auto size=ReadField<int>(part,0xf0);
                const auto data=ReadField<void*>(part,0xe8);
                // CheckSetupAsyncLoadersComplete14089ED30 reads these final
                // bytes without the child-map lock. Do not traverse its mutable
                // maps or call the predicate: it can update traversal state.
                if(size<0 || size>8 || (size && !data)) {result.loaders[type]=0xffffffff;continue;}
                auto bits=static_cast<unsigned>(size)<<24;
                for(int i=0;i<size;++i) {
                    const auto loader=ReadField<void*>(data,i*8);
                    if(!loader)continue;
                    const unsigned flags=1u | (ReadField<unsigned char>(loader,0x1a0)?2u:0u)
                        | (ReadField<unsigned char>(loader,0x1a1)?4u:0u);
                    bits|=flags<<(i*3);
                }
                result.loaders[type]=bits;
            }
        }
        // GI+1F0 is the native weak manual-launch GameMode binding. The
        // earlier native 'chara' annotation is not its class identity.
        const auto index=ReadField<int>(startup_instance_,0x1f0);
        const auto serial=ReadField<int>(startup_instance_,0x1f4);
        auto* item=index>=0 ? RC::Unreal::FUObjectArray::IndexToObject(index):nullptr;
        if(!item || serial<=0 || item->GetSerialNumber()!=serial || !item->IsValid(false))return result;
        auto* mode=item->GetUObject();
        if(!IsLiveReplayObject(mode))return result;
        result.mode=ReadField<unsigned char>(mode,0x468);
        auto** binding=mode->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(L"BattleManager");
        if(binding && IsLiveReplayObject(*binding)) {
            result.manager=1;
            result.assets=ReadField<unsigned char>(*binding,0x1460);
            result.phase=ReadField<unsigned char>(*binding,0x1461);
        }
        return result;
    }
    std::array<StartupRow,64> startup_rows_{};
    StartupRow startup_current_{};
    std::uint64_t startup_ordinal_{};
    std::wstring startup_run_;
    bool startup_recording_{},startup_published_{};
    std::array<unsigned,3> startup_manager_state_{};
    unsigned startup_manager_records_{};bool startup_manager_seen_{};
    unsigned StartupFighterMask() const {
        // Read native registry occupancy only; no early-lifetime dereference.
        return (ReadField<std::uintptr_t>(reinterpret_cast<void*>(base_),0x470de90)!=0 ? 1u:0u)
            | (ReadField<std::uintptr_t>(reinterpret_cast<void*>(base_),0x470de98)!=0 ? 2u:0u);
    }
    void LogStartupRow(const StartupRow& row) const {
        RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] startup RNG engine run_id={} ordinal={} before_tick={} after_tick={} before_seed={:08x} after_seed={:08x} delta={:08x} before_mode={} after_mode={} before_fighters={} after_fighters={} before_initialized={} after_initialized={} read_only=true\n"),
            startup_run_,row.ordinal,row.before_tick,row.after_tick,row.before_rng,row.after_rng,row.delta,row.before_mode,row.after_mode,row.before_fighters,row.after_fighters,row.before_initialized,row.after_initialized);
        RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] startup launch engine run_id={} ordinal={} before_requests={} after_requests={} before_callback={:x} after_callback={:x} before_launch={} after_launch={} before_manager={} after_manager={} before_assets={} after_assets={} before_phase={} after_phase={} read_only=true\n"),
            startup_run_,row.ordinal,row.before_launch.requests,row.after_launch.requests,row.before_launch.callback,row.after_launch.callback,
            row.before_launch.mode,row.after_launch.mode,row.before_launch.manager,row.after_launch.manager,row.before_launch.assets,row.after_launch.assets,row.before_launch.phase,row.after_launch.phase);
        if(row.before_launch.requests>0 || row.after_launch.requests>0)
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] startup setup loaders run_id={} ordinal={} before={:08x},{:08x},{:08x},{:08x},{:08x},{:08x} after={:08x},{:08x},{:08x},{:08x},{:08x},{:08x} encoding=count24_presence_complete_traversed3 read_only=true\n"),
                startup_run_,row.ordinal,row.before_launch.loaders[0],row.before_launch.loaders[1],row.before_launch.loaders[2],row.before_launch.loaders[3],row.before_launch.loaders[4],row.before_launch.loaders[5],
                row.after_launch.loaders[0],row.after_launch.loaders[1],row.after_launch.loaders[2],row.after_launch.loaders[3],row.after_launch.loaders[4],row.after_launch.loaders[5]);
    }
    bool coherence_{},coherence_pending_{},rolling_coherence_{};
    SetOutputObserver set_output_{};
    std::filesystem::path coherence_directory_;
    std::array<std::uint64_t,8> coherence_seen_{};
    unsigned coherence_count_{};DWORD coherence_thread_{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> coherence_source_,coherence_image_;
    Microsoft::WRL::ComPtr<ID3D11Query> coherence_event_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> coherence_context_;
    void CaptureCoherence(IDXGISwapChain* chain,unsigned tick,unsigned generation,bool held) {
        if(!coherence_ || !accepting_ || error_) return;
        // Rolling revisits the same native tick up to eight times. Retain the
        // first representative image per tick/kind; emitted metadata still
        // records its actual generation. This bounds the existing eight slots.
        const auto key=(std::uint64_t(held)<<63)|(std::uint64_t(rolling_coherence_?0:generation)<<32)|tick;
        for(unsigned i=0;i<coherence_count_;++i) if(coherence_seen_[i]==key)return;
        if(coherence_count_==coherence_seen_.size() || coherence_pending_) {error_=34;return;}
        if(!coherence_thread_) coherence_thread_=GetCurrentThreadId();
        if(coherence_thread_!=GetCurrentThreadId()) {error_=34;return;}
        DXGI_SWAP_CHAIN_DESC swap{};DWORD pid{};
        if(!chain || FAILED(chain->GetDesc(&swap))) {error_=34;return;}
        GetWindowThreadProcessId(swap.OutputWindow,&pid);
        if(pid!=GetCurrentProcessId() || FAILED(chain->GetBuffer(0,IID_PPV_ARGS(coherence_source_.ReleaseAndGetAddressOf())))) {error_=34;return;}
        D3D11_TEXTURE2D_DESC d{};coherence_source_->GetDesc(&d);
        const bool packed=d.Format==DXGI_FORMAT_R10G10B10A2_UNORM;
        const bool bgra=d.Format==DXGI_FORMAT_B8G8R8A8_UNORM || d.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if((!packed && !bgra && d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            || !d.Width || d.Width>3840 || !d.Height || d.Height>2160 || d.MipLevels!=1 || d.ArraySize!=1 || d.SampleDesc.Count!=1
            || std::uint64_t(d.Width)*d.Height*8+32768>16ull*1024*1024) {error_=34;return;}
        Microsoft::WRL::ComPtr<ID3D11Device> device;coherence_source_->GetDevice(device.GetAddressOf());
        device->GetImmediateContext(coherence_context_.ReleaseAndGetAddressOf());
        if(coherence_context_->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) {error_=34;return;}
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
        D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};
        if(FAILED(device->CreateTexture2D(&d,nullptr,coherence_image_.ReleaseAndGetAddressOf()))
            || FAILED(device->CreateQuery(&q,coherence_event_.ReleaseAndGetAddressOf()))) {error_=34;return;}
        const auto name=run_+L"-coherence-"+(held?L"held":L"native")+std::to_wstring(tick)+L"-g"+std::to_wstring(generation)+L".ppm";
        std::ofstream file(coherence_directory_/name,std::ios::binary|std::ios::trunc);
        if(!file) {error_=34;return;}
        coherence_pending_=true;
        Microsoft::WRL::ComPtr<ID3D11Predicate> predicate;BOOL predicate_value{};
        coherence_context_->GetPredication(predicate.GetAddressOf(),&predicate_value);
        coherence_context_->SetPredication(nullptr,FALSE);
        coherence_context_->CopyResource(coherence_image_.Get(),coherence_source_.Get());
        coherence_context_->End(coherence_event_.Get());
        coherence_context_->SetPredication(predicate.Get(),predicate_value);coherence_context_->Flush();
        const auto deadline=GetTickCount64()+500;BOOL done=FALSE;
        while(coherence_context_->GetData(coherence_event_.Get(),&done,sizeof(done),0)!=S_OK || !done) {
            if(GetTickCount64()>=deadline) {error_=34;return;} // Timeout retains every in-flight owner.
            SwitchToThread();
        }
        coherence_pending_=false;
        D3D11_MAPPED_SUBRESOURCE map{};
        if(FAILED(coherence_context_->Map(coherence_image_.Get(),0,D3D11_MAP_READ,0,&map))) {error_=34;return;}
        file<<"P6\n"<<d.Width<<" "<<d.Height<<"\n255\n";
        std::array<unsigned char,3840*3> row{};
        for(unsigned y=0;y<d.Height;++y) {
            const auto* source=static_cast<const unsigned char*>(map.pData)+std::size_t(y)*map.RowPitch;
            for(unsigned x=0;x<d.Width;++x) for(unsigned c=0;c<3;++c) {
                unsigned word{};if(packed) std::memcpy(&word,source+4*x,4);
                row[3*x+c]=packed?static_cast<unsigned char>(((word>>(c*10))&1023)*255/1023):source[4*x+(bgra?2-c:c)];
            }
            file.write(reinterpret_cast<const char*>(row.data()),d.Width*3);
        }
        coherence_context_->Unmap(coherence_image_.Get(),0);file.close();
        if(!file) {error_=34;return;}
        coherence_seen_[coherence_count_++]=key;
        RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] coherence image run_id={} tick={} generation={} held={} hwnd={} file={} width={} height={} gpu_complete=true maps=1\n"),
            run_,tick,generation,held,reinterpret_cast<std::uintptr_t>(swap.OutputWindow),name,d.Width,d.Height);
        coherence_source_.Reset();coherence_image_.Reset();coherence_event_.Reset();coherence_context_.Reset();
    }
    template<class T> static T ReadField(const void* p,std::size_t offset) {return *reinterpret_cast<const T*>(static_cast<const std::byte*>(p)+offset);}
    template<class T> using Com = Microsoft::WRL::ComPtr<T>;
    struct Tag { void* task{}; std::uint32_t tick{},generation{}; };
    struct ViewObservation { unsigned width{},height{},frame{},temporal{},sample_count{},frame_index{},occlusion{},jitter_x{},jitter_y{},aa{},game_time{},real_time{},random{},family_frame{},shader_index{}; std::uint64_t matrices{},previous_matrices{}; unsigned history_flags{},motion_blur_scale{}; };
    ViewObservation view_{};
    const std::byte* visibility_state_{};
    unsigned visibility_records_{};
    struct Slot {
        ViewObservation view;
        Com<ID3D11Texture2D> image, source;
        Com<ID3D11Query> event;
        std::uint32_t tick{},generation{};
        bool pending{};
    };
    struct PassSlot {
        Com<ID3D11Texture2D> image,source;
        Com<ID3D11Query> event;
        D3D11_TEXTURE2D_DESC desc{};
        unsigned ordinal{},last_draw{},generation{},bpp{},group{},target{};
        bool pending{},tagged{};
    };
    std::array<PassSlot,16> pass_slots_{};
    PassSlot stream_slot_{};
    unsigned stream_runs_{},stream_generation_{};
    std::uintptr_t stream_vs_{},stream_ps_{};
    struct BufferSlot {
        Com<ID3D11Buffer> image,source;Com<ID3D11Query> event;
        unsigned bytes{},kind{},index{},generation{};bool pending{},tagged{};
    };
    std::array<BufferSlot,64> input_buffers_{};
    unsigned input_count_{};std::uint64_t input_bytes_{};
    std::array<Com<ID3D11Resource>,9> last_draw_targets_;
    unsigned pass_color_groups_{};bool pass_depth_captured_{};
    unsigned pass_ordinal_{},pass_draws_{},pass_skipped_{},pass_group_draws_{};
    std::uint64_t pass_bytes_{};
    bool pass_overflow_{},pass_observing_{};
    inline static ReplayPresentationObserver* active_{};
    inline static thread_local std::uint32_t render_tick_{};
    inline static thread_local std::uint32_t render_generation_{};
    std::uintptr_t base_{};
    DWORD thread_{};
    void* context_{};
    ReadTick read_{};
    std::wstring run_;
    std::array<std::unique_ptr<PLH::x64Detour>,14> hooks_;
    std::array<std::uint64_t,14> originals_{};
    // Temporary read-only diagnostic, reset at every native Present. Record
    // native draw bindings; completed constant/stack probes have been removed.
    // No shader modification or expected-state feedback.
    struct ParameterRow { unsigned domain{},buffer{},offset{},size{},data{},live_tick{}; };
    std::array<ParameterRow,8192> parameter_rows_{};
    std::array<unsigned char,512*1024> parameter_bytes_{};
    unsigned parameter_count_{},parameter_size_{};
    std::atomic<DWORD> parameter_thread_{};
    std::atomic<bool> parameter_wrong_thread_{};
    bool parameter_truncated_{};
    
    std::array<Tag,16> tags_{};
    std::mutex mutex_;
    std::unique_ptr<std::array<Slot,3>> slots_;
    Com<ID3D11Device> device_;
    Com<ID3D11DeviceContext> context_d3d_;
    D3D11_TEXTURE2D_DESC desc_{};
    std::atomic<unsigned> error_{};
    std::atomic<bool> accepting_{true};
    std::uint64_t frames_{}, bytes_{};
    std::uint64_t tagged_tasks_{},presents_{},tagged_presents_{};
    std::atomic<unsigned> diagnostic_traces_{};
    bool started_{};
    bool wind_control_{};
    bool pixel_diagnostics_{},pass_diagnostics_{};
    Horse::Deterministic::NativeWindConstruction wind_construction_;
    Horse::Deterministic::NativeReplayRendering replay_rendering_;
    Horse::Deterministic::NativeReplayWidgetClock widget_clock_;
    ID3D11Texture2D* source_binding_{};
    std::uint32_t last_tick_{},generation_{};
    struct PoseObservation { int player{}; unsigned count{}; std::uint64_t mapping{}, hash{}; };
    std::array<PoseObservation,128> poses_{};
    unsigned pose_count_{},pose_async_{};
    std::uint32_t pose_tick_{};
    unsigned receiver_calls_{};
    void Fence()
    {
        void* fence{};
        reinterpret_cast<void (*)(void**, bool)>(base_+0x15e9510)(&fence,false);
        reinterpret_cast<void (*)(void**, bool)>(base_+0x15efec0)(&fence,false);
    }
    static void* Build(void* output, void* prerequisite, unsigned thread)
    {
        auto* self=active_;
        auto* result=reinterpret_cast<void* (*)(void*,void*,unsigned)>(self->originals_[0])(output,prerequisite,thread);
        if (self->accepting_ && GetCurrentThreadId()==self->thread_) {
            std::uint32_t tick{};
            if (!self->read_(self->context_,tick)) self->error_=1;
            else if (self->ObserveTick(tick)) {
                std::lock_guard lock(self->mutex_);
                auto* task=*reinterpret_cast<void**>(output);
                auto free=std::find_if(self->tags_.begin(),self->tags_.end(),[](const Tag& t){return !t.task;});
                if (!task || free==self->tags_.end()
                    || std::any_of(self->tags_.begin(),self->tags_.end(),[&](const Tag& t){return t.task==task;})) self->error_=2;
                else {
                    if(tick<self->last_tick_) ++self->generation_;
                    self->last_tick_=tick;
                    *free={task,tick,self->generation_}; ++self->tagged_tasks_;
                    self->LogPoses(tick);
                    RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] particle receivers completed run_id={} tick={} generation={} calls={} schema=1 read_only=true\n"),
                        self->run_,tick,self->generation_,self->receiver_calls_);
                    self->receiver_calls_=0;
                    self->LogTraceSources(tick);
                }
            }
        }
        return result;
    }
    struct TraceOwner { RC::Unreal::UObject* object{}; int index{},serial{}; };
    std::array<TraceOwner,16> trace_owners_{};
    unsigned trace_owner_count_{};
    bool trace_owners_bound_{};
    void LogTraceSources(unsigned tick)
    {
        // Completed-world CPU observer only. No retained controller references,
        // native setters, expected values, resource readbacks or new hooks.
        // 1408D1C00 establishes state=controller+10, size F8; 1408CED30
        // frees nested arrays at last strong release. Never dereference a
        // state obtained only from a historical pointer or expired weak ref.
        using Object=RC::Unreal::UObject;
        struct Ref {const std::byte* state;const std::byte* controller;};
        struct Array {const std::byte* data;int count,capacity;};
        const auto valid=[](const Array& a,int maximum) {
            return a.count>=0 && a.count<=maximum && a.capacity>=a.count
                && a.capacity<=maximum && (!a.capacity || a.data);
        };
        if(!trace_owners_bound_) {
            if(!Horse::Deterministic::VisitReplayObjectsOfClass(L"LuxTraceComponent",[&](Object* p) {
                if(!IsLiveReplayObject(p) || ReadField<unsigned>(p,0x498)>1 || !ReadField<void*>(p,0x490)) return true;
                if(trace_owner_count_==trace_owners_.size() || ReadField<std::uintptr_t>(p,0)!=base_+0x3360ca8) return false;
                auto* item=RC::Unreal::FUObjectArray::IndexToObject(p->GetInternalIndex());
                trace_owners_[trace_owner_count_++]={p,p->GetInternalIndex(),item->GetSerialNumber()};
                return true;
            })) {error_=27;return;}
            trace_owners_bound_=true;
            if(!trace_owner_count_) {error_=27;return;}
        }
        constexpr std::size_t offsets[]{0x3e0,0x3f0,0x400,0x418,0x428};
        for(unsigned owner=0;owner<trace_owner_count_;++owner) {
            const auto& id=trace_owners_[owner];
            auto* item=RC::Unreal::FUObjectArray::IndexToObject(id.index);
            if(!item || item->GetUObject()!=id.object || !item->IsValid(false)
                || item->GetSerialNumber()!=id.serial) {error_=27;return;}
            std::uint64_t hash=14695981039346656037ull,membership=hash;
            ReplayTraceMembership topology;
            const auto add=[](std::uint64_t& h,const void* p,std::size_t n) {
                for(auto* b=static_cast<const unsigned char*>(p);n;--n,++b) h=(h^*b)*1099511628211ull;
            };
            // Pointer-free fields consumed by 1408D9C20 and 1408DA260.
            // +444 is the incoming Unreal actor delta, refreshed by
            // 140437590 -> 1408D5590. The admitted fixed-step tick wrapper
            // and its lifetime/interpolator/material/activation callees do
            // not consume it. Keep it as a separate native-input observation;
            // the snapshot still captures/validates it at installation.
            for(const auto span:{std::pair{0x438u,1u},{0x43cu,8u},{0x448u,4u},{0x458u,3u},{0x45cu,4u}})
                add(hash,static_cast<const std::byte*>(static_cast<const void*>(id.object))+span.first,span.second);
            const auto clock_hash=hash;
            std::array<Ref,256> seen{};unsigned states{},expired{},attached{};
            std::array<int,5> counts{};
            for(unsigned collection=0;collection<5;++collection) {
                const auto a=ReadField<Array>(id.object,offsets[collection]);
                if(!valid(a,256)) {error_=27;return;}
                counts[collection]=a.count;add(hash,&a.count,4);
                if(!topology.Begin(collection,a.count)){error_=27;return;}
                for(int i=0;i<a.count;++i) {
                    const auto ref=ReadField<Ref>(a.data,std::size_t(i)*16);
                    add(membership,&ref,sizeof(ref)); // Same-process binding witness only.
                    if(!ref.controller || ref.state!=ref.controller+0x10
                        || ReadField<std::uintptr_t>(ref.controller,0)!=base_+0x3362590) {error_=27;return;}
                    const auto strong=ReadField<int>(ref.controller,8);
                    if(strong<0) {error_=27;return;}
                    const auto parent=strong?ReadField<Ref>(ref.state,0xa0):Ref{};
                    if(!topology.Member({reinterpret_cast<std::uintptr_t>(ref.state),reinterpret_cast<std::uintptr_t>(ref.controller)},strong!=0,
                        {reinterpret_cast<std::uintptr_t>(parent.state),reinterpret_cast<std::uintptr_t>(parent.controller)})){error_=27;return;}
                    if(!strong) {if(collection>=3){error_=27;return;}++expired;continue;}
                    if(std::any_of(seen.begin(),seen.begin()+states,[&](const Ref& r){return r.state==ref.state;})) continue;
                    if(states==seen.size()) {error_=27;return;}seen[states++]=ref;
                    std::uint64_t state_hash=14695981039346656037ull;
                    const auto state_add=[&](const void* p,std::size_t n){add(hash,p,n);add(state_hash,p,n);};
                    // Construction/copy routines leave padding uninitialized.
                    // Hash only retained scalar/vector ranges; pointers and
                    // process-local VFX slot identities are reported separately.
                    for(const auto span:{std::pair{0u,2u},{0x20u,0x35u},{0x80u,8u},{0x98u,4u},
                        {0xc0u,9u},{0xccu,12u},{0xdcu,0x14u},{0xf0u,1u},{0xf4u,4u}})
                        state_add(ref.state+span.first,span.second);
                    const bool detail=tick==418 && collection==0 && i==0
                        && ReadField<unsigned>(id.object,0x498)==0;
                    const auto detail_bytes=[&](unsigned domain,unsigned row,const std::byte* data,unsigned size) {
                        if(!detail)return;
                        for(unsigned offset=0;offset<size;offset+=16) {
                            std::array<unsigned,4> words{};const auto count=(std::min)(16u,size-offset);
                            std::memcpy(words.data(),data+offset,count);
                            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] trace field detail run_id={} tick={} generation={} domain={} row={} offset={:x} size={} words={:08x},{:08x},{:08x},{:08x} read_only=true\n"),
                                run_,tick,generation_,domain,row,offset,count,words[0],words[1],words[2],words[3]);
                        }
                    };
                    if(detail) {
                        for(const auto span:{std::pair{0x20u,0x35u},{0x80u,8u},{0x98u,4u},{0xc0u,9u},{0xccu,12u},{0xdcu,0x14u},{0xf4u,4u}})
                            detail_bytes(0,span.first,ref.state+span.first,span.second);
                    }
                    const auto scalar_hash=state_hash;
                    std::array<std::uint64_t,3> array_hashes{};
                    for(unsigned array=0;array<3;++array) {
                        const auto rows=ReadField<Array>(ref.state,array==0?0x10:array==1?0x70:0x88);
                        if(!valid(rows,1024)) {error_=27;return;}
                        state_add(&rows.count,4);
                        array_hashes[array]=14695981039346656037ull;
                        add(array_hashes[array],&rows.count,4);
                        const auto row_add=[&](const void* p,std::size_t n){state_add(p,n);add(array_hashes[array],p,n);};
                        const auto stride=array==0?0x70:0x2c;
                        for(int row=0;row<rows.count;++row) {
                            auto* data=rows.data+std::size_t(row)*stride;
                            if(row<2)detail_bytes(array+1,row,data,array==0?0x6cu:0x29u);
                            if(array==0) {row_add(data,0x2d);row_add(data+0x30,0x3c);}
                            else row_add(data,0x29);
                        }
                    }
                    auto* actor=ReadField<Object*>(ref.state,8);
                    auto* attachment=ReadField<Object*>(ref.state,0xb8);
                    if(!IsLiveReplayObject(actor) || (attachment && !IsLiveReplayObject(attachment))) {error_=27;return;}
                    attached+=attachment!=nullptr;
                    if(tick==185 || collection<3) {
                        RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] trace state run_id={} tick={} generation={} player={} parts={} kind={} collection={} ordinal={} hash={:016x} scalar={:016x} rows={:016x} history={:016x} filtered={:016x} life={:08x} fade={:08x} active={} actor={}\n"),
                            run_,tick,generation_,ReadField<unsigned>(id.object,0x498),ReadField<unsigned char>(ref.state,0),ReadField<unsigned char>(ref.state,1),collection,i,state_hash,scalar_hash,array_hashes[0],array_hashes[1],array_hashes[2],ReadField<unsigned>(ref.state,0xc0),ReadField<unsigned>(ref.state,0xdc),ReadField<unsigned char>(ref.state,0xf0),actor->GetName());
                    }

                    if(tick==205 || tick==210) {
                        RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] trace binding run_id={} tick={} generation={} owner={} state={:x} controller={:x} actor={} actor_index={} actor_serial={} attachment={:x} strong={} cached={:x}\n"),
                            run_,tick,generation_,owner,reinterpret_cast<std::uintptr_t>(ref.state),reinterpret_cast<std::uintptr_t>(ref.controller),actor->GetFullName(),actor->GetInternalIndex(),RC::Unreal::FUObjectArray::IndexToObject(actor->GetInternalIndex())->GetSerialNumber(),reinterpret_cast<std::uintptr_t>(attachment),strong,ReadField<std::uintptr_t>(ref.state,0xa0));
                    }
                }
            }
            if(!topology.complete()){error_=27;return;}
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] trace source run_id={} tick={} generation={} owner={} player={} states={} expired={} attached={} fading={} completion={} pending={} strong={} child={} hash={:016x} membership={:016x} membership_version=2 topology={:016x} clock_hash={:016x} remainder={:08x} time={:08x} scale={:08x} steps={} native_actor_delta={:08x}\n"),
                run_,tick,generation_,owner,ReadField<unsigned>(id.object,0x498),states,expired,attached,counts[0],counts[1],counts[2],counts[3],counts[4],hash,membership,topology.hash(),clock_hash,ReadField<unsigned>(id.object,0x43c),ReadField<unsigned>(id.object,0x440),ReadField<unsigned>(id.object,0x448),ReadField<unsigned>(id.object,0x45c),ReadField<unsigned>(id.object,0x444));
        }
    }
    //141FA0B80 is the native receiver loop, after all emitter ticks and before
    // world dispatch/retirement. No other hook owns this entry. Always call the
    // native implementation exactly once, including unobserved or invalid rows.
    static void ParticleReceivers(void* emitter,float delta)
    {
        auto& self=*active_;
        const auto tick=ReadField<unsigned>(reinterpret_cast<void*>(self.base_),0x470d0c4);
        const bool selected=self.accepting_ && tick>=170 && self.ObserveTick(tick);
        const auto* lod=selected ? ReadField<const void*>(emitter,0x28) : nullptr;
        const int routes=lod ? ReadField<int>(lod,0xa8) : 0;
        ReplayQualification::ReceiverCpuSample before{},after{};
        std::array<int,5> counts{};
        std::uint64_t asset_hash=14695981039346656037ull,event_hash=14695981039346656037ull;
        const auto add=[](std::uint64_t& h,const void* p,std::size_t n) {
            const auto* bytes=static_cast<const unsigned char*>(p);
            for(std::size_t i=0;i<n;++i)h=(h^bytes[i])*1099511628211ull;
        };
        const auto read=[](std::uintptr_t p,auto& value){std::memcpy(&value,reinterpret_cast<const void*>(p),sizeof(value));return true;};
        const bool owner=GetCurrentThreadId()==self.thread_;
        bool valid=selected && owner && routes>0 && routes<=128 && self.receiver_calls_<1024;
        unsigned rng_before{};
        if(selected && (!owner || routes<0 || routes>128 || self.receiver_calls_>=1024))self.error_=36;
        if(valid) {
            auto* asset=ReadField<RC::Unreal::UObject*>(emitter,0x10);
            const auto* component=ReadField<const void*>(emitter,0x18);
            valid=IsLiveReplayObject(asset) && component && before.Capture(read,reinterpret_cast<std::uintptr_t>(emitter));
            if(valid) {
                const auto name=asset->GetFullName();add(asset_hash,name.data(),name.size()*sizeof(name[0]));
                const auto* modules=ReadField<RC::Unreal::UObject* const*>(lod,0xa0);
                if(!modules)valid=false;
                else for(int i=0;i<routes;++i) {
                    if(!IsLiveReplayObject(modules[i])) {valid=false;break;}
                    const auto route=modules[i]->GetFullName();add(asset_hash,route.data(),route.size()*sizeof(route[0]));
                }
                struct Array {const std::byte* data;int count,capacity;};
                constexpr unsigned offsets[]{0x918,0x928,0x938,0x948,0x958};
                constexpr unsigned strides[]{0x40,0x50,0x78,0x48,0x40};
                for(unsigned queue=0;queue<5 && valid;++queue) {
                    const auto rows=ReadField<Array>(component,offsets[queue]);
                    if(rows.count<0 || rows.count>4096 || rows.capacity<rows.count || (rows.capacity&&!rows.data)) {valid=false;break;}
                    counts[queue]=rows.count;add(event_hash,&rows.count,4);
                    for(int i=0;i<rows.count;++i) {
                        const auto* row=rows.data+std::size_t(i)*strides[queue];
                        const auto name=RC::Unreal::FName(ReadField<std::int64_t>(row,8)).ToString();
                        add(event_hash,row,1);add(event_hash,name.data(),name.size()*sizeof(name[0]));
                        add(event_hash,row+0x10,0x1c); // native time, position, velocity; no callback pointers/padding
                    }
                }
            }
            rng_before=ReadField<unsigned>(reinterpret_cast<void*>(self.base_),0x416673c);
            if(!valid)self.error_=36;
        }
        reinterpret_cast<void(*)(void*,float)>(self.originals_[13])(emitter,delta);
        if(valid) {
            const auto rng_after=ReadField<unsigned>(reinterpret_cast<void*>(self.base_),0x416673c);
            if(!after.Capture(read,reinterpret_cast<std::uintptr_t>(emitter))) {self.error_=36;return;}
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] particle receiver run_id={} tick={} ordinal={} asset={:016x} routes={} events={},{},{},{},{} event_hash={:016x} before_count={} after_count={} before_hash={:016x} after_hash={:016x} rng_before={:08x} rng_after={:08x} delta={:08x} native_returned=true game_thread=true read_only=true\n"),
                self.run_,tick,self.receiver_calls_++,asset_hash,routes,counts[0],counts[1],counts[2],counts[3],counts[4],event_hash,before.count,after.count,before.hash,after.hash,rng_before,rng_after,std::bit_cast<unsigned>(delta));
        }
    }
    static void GpuEmitterTick(void* emitter,float delta,bool suppress) { EmitterTick(emitter,delta,suppress,true); }
    static void CpuEmitterTick(void* emitter,float delta,bool suppress) { EmitterTick(emitter,delta,suppress,false); }
    static void EmitterTick(void* emitter,float delta,bool suppress,bool gpu)
    {
        auto* self=active_;
        reinterpret_cast<void (*)(void*,float,bool)>(self->originals_[gpu ? 6 : 7])(emitter,delta,suppress);
        if(!self->accepting_) return;
        const auto tick=*reinterpret_cast<unsigned*>(self->base_+0x470d0c4);
        if(!(tick<=185 || tick==206 || tick==209 || tick==210)) return;
        const auto* bytes=static_cast<const std::byte*>(emitter);
        const auto* component=*reinterpret_cast<const std::byte* const*>(bytes+0x18);
        const auto* world=component ? *reinterpret_cast<const std::byte* const*>(component+0x1c8) : nullptr;
        const int count=*reinterpret_cast<const int*>(bytes+(gpu ? 0x240 : 0x118));
        const int capacity=*reinterpret_cast<const int*>(bytes+(gpu ? 0x244 : 0x120));
        const int stride=gpu ? 0x48 : *reinterpret_cast<const int*>(bytes+0x114);
        const auto* data=*reinterpret_cast<const std::byte* const*>(bytes+(gpu ? 0x238 : 0xf0));
        const auto* indices=gpu ? nullptr : *reinterpret_cast<const unsigned short* const*>(bytes+0xf8);
        if(count<0 || count>4096 || capacity<count || stride<0x48 || stride>4096 || (count && (!data || (!gpu && !indices)))) {self->error_=23;return;}
        std::uint64_t hash=14695981039346656037ull;
        for(int i=0;i<count;++i) {
            const auto index=gpu ? i : indices[i];
            if(index>=capacity) {self->error_=24;return;}
            const auto* row=data+std::size_t(index)*stride;
            // GPU +00..3F are authored spawn values written by 141F91FB0;
            // +40..47 locate the shared tile and are process-local allocation.
            // CPU +10..2B include native position/lifetime and base velocity;
            // actual velocity+30 and life+0C are in receiver observations.
            for(unsigned j=gpu ? 0u : 0x10u;j<(gpu ? 0x40u : 0x2cu);++j)
                hash=(hash^std::to_integer<unsigned char>(row[j]))*1099511628211ull;
        }
        RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] particle output run_id={} tick={} gpu={} template={} count={} delta={:08x} time={:08x} hash={:016x} shared_rng={:08x} game_thread={} world_time={:08x} last_render={:08x}\n"),
            self->run_,tick,gpu,(*reinterpret_cast<RC::Unreal::UObject* const*>(bytes+0x10))->GetFullName(),count,
            std::bit_cast<unsigned>(delta),*reinterpret_cast<const unsigned*>(bytes+0x12c),hash,*reinterpret_cast<unsigned*>(self->base_+0x416673c),GetCurrentThreadId()==self->thread_,world ? *reinterpret_cast<const unsigned*>(world+0x930) : 0,component ? *reinterpret_cast<const unsigned*>(component+0x674) : 0);
    }
    static void* CreateGpuEmitter(void* system,void* descriptor) {
        auto& self=*active_;
        auto* root=reinterpret_cast<void*(*)(void*,void*)>(self.originals_[11])(system,descriptor);
        const auto tick=ReadField<unsigned>(reinterpret_cast<void*>(self.base_),0x470d0c4);
        if(self.accepting_ && tick>=170 && self.ObserveTick(tick)) {
            if(!root || ReadField<std::uintptr_t>(root,0)!=self.base_+0x394c100
                || ReadField<void*>(root,0x1d0)!=system || ReadField<void*>(root,0x1d8)!=descriptor)
                self.error_=35;
            else RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] particle GPU constructed run_id={} tick={} emitter={:x} render={:x} system={:x} descriptor={:x} native_factory_returned=true\n"),
                self.run_,tick,reinterpret_cast<std::uintptr_t>(root),ReadField<std::uintptr_t>(root,0x1e0),reinterpret_cast<std::uintptr_t>(system),reinterpret_cast<std::uintptr_t>(descriptor));
        }
        return root;
    }
    // Observe the same bounded window as poses; never call completion or
    // destruction ourselves. 141F78870 is not owned by the host's five guards.
    static std::uint64_t CheckParticleCompletion(void* component) {
        auto& self=*active_;
        const auto tick=ReadField<unsigned>(reinterpret_cast<void*>(self.base_),0x470d0c4);
        struct Array {void** data;int count,capacity;};
        struct Entry {void* emitter;std::uintptr_t table;RC::Unreal::UObject* asset;};
        std::array<Entry,128> before{};
        const bool selected=self.accepting_ && tick>=170 && self.ObserveTick(tick);
        const auto pending=selected ? ReadField<void*>(component,0xa70) : nullptr;
        auto a=selected && !pending ? ReadField<Array>(component,0xa50) : Array{};
        const bool bounded=a.count>=0 && a.capacity>=a.count && a.capacity<=128 && (!a.capacity || a.data);
        if(selected && (!bounded || GetCurrentThreadId()!=self.thread_))self.error_=35;
        if(selected && bounded && !pending)for(int i=0;i<a.count;++i)if(auto* e=a.data[i])
            before[i]={e,ReadField<std::uintptr_t>(e,0),ReadField<RC::Unreal::UObject*>(e,0x10)};
        const auto result=reinterpret_cast<std::uint64_t(*)(void*)>(self.originals_[10])(component);
        if(selected && pending)
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] particle retirement skipped run_id={} tick={} reason=pending_async_fence\n"),self.run_,tick);
        if(selected && bounded && !pending) {
            const auto after=ReadField<Array>(component,0xa50);
            if(after.data!=a.data || after.count!=a.count || after.capacity!=a.capacity) {self.error_=35;return result;}
            for(int i=0;i<a.count;++i)if(before[i].emitter && !after.data[i]) {
                // The freed emitter is never dereferenced after the native call.
                RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] particle retirement run_id={} tick={} component={:x} emitter={:x} slot={} vtable_rva={:x} template={} native_completed={} peer_notifications_and_destructor_returned=true slot_cleared=true\n"),
                    self.run_,tick,reinterpret_cast<std::uintptr_t>(component),reinterpret_cast<std::uintptr_t>(before[i].emitter),i,before[i].table-self.base_,before[i].asset->GetFullName(),(result&255)!=0);
            }
        }
        return result;
    }
    static void* TraceBone(void* output,void* context,unsigned player,unsigned bone)
    {
        auto& self=*active_;
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-self.base_;
        auto* result=reinterpret_cast<void*(*)(void*,void*,unsigned,unsigned)>(self.originals_[12])(output,context,player,bone);
        const auto tick=ReadField<unsigned>(reinterpret_cast<void*>(self.base_),0x470d0c4);
        if(self.accepting_ && player==0 && tick>=416 && tick<=425
            && (caller==0x8da64e || caller==0x8da6f7) && GetCurrentThreadId()==self.thread_) {
            const auto fighter=ReadField<std::uintptr_t>(reinterpret_cast<void*>(self.base_),0x470de90);
            const auto* words=static_cast<const unsigned*>(output);
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] trace bone input run_id={} tick={} last_presented={} bone={} caller={:x} bank_slot={} current={:x} provider={:x} words={:08x},{:08x},{:08x},{:08x},{:08x},{:08x},{:08x},{:08x},{:08x},{:08x},{:08x},{:08x} read_only=true\n"),
                self.run_,tick,self.last_tick_,bone,caller,ReadField<unsigned>(reinterpret_cast<void*>(fighter),0x35c0),
                ReadField<std::uintptr_t>(reinterpret_cast<void*>(fighter),0x35c8),ReadField<std::uintptr_t>(reinterpret_cast<void*>(fighter),0x35d0),
                words[0],words[1],words[2],words[3],words[4],words[5],words[6],words[7],words[8],words[9],words[10],words[11]);
        }
        return result;
    }
    static void InitializeEmitter(void* emitter)
    {
        auto* self=active_;
        reinterpret_cast<void (*)(void*)>(self->originals_[5])(emitter);
        // Read native output only. This observer never seeds or repairs a stream.
        const auto tick=*reinterpret_cast<unsigned*>(self->base_+0x470d0c4);
        if(self->accepting_ && (tick<=observation_last_tick || self->ObserveTick(tick))) {
            const auto* bytes=static_cast<const std::byte*>(emitter);
            if(tick>=170 && self->ObserveTick(tick))
                RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] particle GPU initialized run_id={} tick={} component={:x} emitter={:x} template={}\n"),
                    self->run_,tick,ReadField<std::uintptr_t>(emitter,0x18),reinterpret_cast<std::uintptr_t>(emitter),ReadField<RC::Unreal::UObject*>(emitter,0x10)->GetFullName());
            if(GetCurrentThreadId()==self->thread_ && DiagnosticAllowed(Horse::Deterministic::ReplayDiagnosticTrace::Particles,tick)) {
                void* stack[16]{};std::array<std::uintptr_t,16> rvas{};
                const auto depth=Horse::Deterministic::g_replay_diagnostics.stack_depth;
                const auto count=depth ? ::CaptureStackBackTrace(0,depth,stack,nullptr) : 0;
                for(unsigned i=0;i<static_cast<unsigned>(count);++i) {
                    const auto address=reinterpret_cast<std::uintptr_t>(stack[i]);
                    if(address>=self->base_ && address<self->base_+0x5000000)rvas[i]=address-self->base_;
                }
                for(unsigned part=0;part<depth/8;++part) {
                    const auto i=part*8;
                    RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] particle initializer source run_id={} tick={} emitter={:x} part={} stack={:x},{:x},{:x},{:x},{:x},{:x},{:x},{:x} read_only=true\n"),
                        self->run_,tick,reinterpret_cast<std::uintptr_t>(emitter),part,rvas[i],rvas[i+1],rvas[i+2],rvas[i+3],rvas[i+4],rvas[i+5],rvas[i+6],rvas[i+7]);
                }
            }
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] particle seed run_id={} tick={} game_thread={} initial={} current={}\n"),
                self->run_,tick,GetCurrentThreadId()==self->thread_,
                *reinterpret_cast<const unsigned*>(bytes+0x288),*reinterpret_cast<const unsigned*>(bytes+0x28c));
        }
    }
    static bool SelectedPostprocessTick(unsigned tick) {
        return tick==185 || tick==205 || tick==206 || tick==210 || tick==211;
    }
    void RecordParameter(unsigned domain,unsigned buffer,unsigned offset,unsigned size,const void* data) {
        if(!accepting_) return;
        // Bounded CPU binding evidence. Separate RHI workers are rejected by Start.
        const auto owner=parameter_thread_.load();
        if(!owner) return; // Before the first native Present establishes owner.
        if(GetCurrentThreadId()!=owner) {parameter_wrong_thread_=true;return;}
        const auto tick=ReadField<unsigned>(reinterpret_cast<void*>(base_),0x470d0c4);
        if(tick<180 || tick>215) return;
        if(parameter_count_==parameter_rows_.size() || size>parameter_bytes_.size()-parameter_size_) {
            parameter_truncated_=true;return;
        }
        parameter_rows_[parameter_count_++]={domain,buffer,offset,size,parameter_size_,tick};
        if(size) std::memcpy(parameter_bytes_.data()+parameter_size_,data,size);
        parameter_size_+=size;
    }
    unsigned mesh_rows_{};
    static std::uint64_t DrawTranslucentMesh(void* commands,void* view,void* policy,void* mesh,
        void* mask,void* state,unsigned char fog,void* proxy,unsigned id) {
        auto& s=*active_;
        const auto tick=ReadField<unsigned>(reinterpret_cast<void*>(s.base_),0x470d0c4);
        if(s.accepting_ && (tick==206 || s.pass_observing_) && ReadField<unsigned>(policy,8)==1
            && proxy && mesh && s.mesh_rows_<128) {
            ++s.mesh_rows_;
            const auto* info=ReadField<const void*>(proxy,0x118);
            auto* component=info ? ReadField<RC::Unreal::UObject*>(info,0xe8) : nullptr;
            auto* factory=ReadField<void*>(mesh,0x90);
            const auto* material=ReadField<const void*>(mesh,0x98);
            if(!info || ReadField<const void*>(info,8)!=proxy || !IsLiveReplayObject(component)
                || ReadField<const void*>(component,0x790)!=proxy || !factory) {s.error_=34;}
            else {
                const auto* type=reinterpret_cast<const void* (*)(void*)>(ReadField<std::uintptr_t>(ReadField<const void*>(factory,0),0x40))(factory);
                if(!type || ReadField<std::uintptr_t>(type,0)!=s.base_+0x35bff50) s.error_=34;
                else {
                    const auto* name=ReadField<const wchar_t*>(type,0x10);
                    std::uintptr_t resource{};unsigned selector{},last_frame{};
                    if(type==reinterpret_cast<void*>(s.base_+0x439a010)) {
                        selector=ReadField<unsigned>(factory,0x388);last_frame=ReadField<unsigned>(factory,0x38c);
                        if(selector>1) s.error_=34;
                        else if(const auto* native=ReadField<const void*>(factory,0x370+16*selector)) {
                            auto* srv=ReadField<ID3D11ShaderResourceView*>(native,0x18);
                            if(srv) {Com<ID3D11Resource> image;srv->GetResource(&image);resource=reinterpret_cast<std::uintptr_t>(image.Get());}
                        }
                        const auto* declaration=ReadField<const void*>(factory,0x340);
                        if(declaration && ReadField<std::uintptr_t>(declaration,0)==s.base_+0x35e3ef0) {
                            const auto count=ReadField<unsigned>(declaration,0x218);
                            if(count>16) s.error_=34;
                            else for(unsigned i=0;i<static_cast<unsigned>(count);++i) {
                                const auto* d=reinterpret_cast<const D3D11_INPUT_ELEMENT_DESC*>(static_cast<const std::byte*>(declaration)+0x18+i*32);
                                RC::Output::send<RC::LogLevel::Default>(STR(
                                    "[ReplayQualification] translucent vertex declaration run_id={} live_tick={} factory={:x} semantic={} semantic_index={} format={} slot={} offset={} classification={} step={}\n"),
                                    s.run_,tick,reinterpret_cast<std::uintptr_t>(factory),RC::to_generic_string(d->SemanticName),d->SemanticIndex,
                                    static_cast<unsigned>(d->Format),d->InputSlot,d->AlignedByteOffset,static_cast<unsigned>(d->InputSlotClass),d->InstanceDataStepRate);
                            }
                        }
                    }
                    RC::Output::send<RC::LogLevel::Default>(STR(
                        "[ReplayQualification] translucent mesh owner run_id={} live_tick={} row={} component={:x} component_name=\"{}\" proxy={:x} mesh={:x} material={:x} factory={:x} factory_type={} bone_resource={:x} selector={} update_frame={}\n"),
                        s.run_,tick,s.mesh_rows_,reinterpret_cast<std::uintptr_t>(component),component->GetFullName(),
                        reinterpret_cast<std::uintptr_t>(proxy),reinterpret_cast<std::uintptr_t>(mesh),reinterpret_cast<std::uintptr_t>(material),
                        reinterpret_cast<std::uintptr_t>(factory),name,resource,selector,last_frame);
                }
            }
        }
        return reinterpret_cast<std::uint64_t (*)(void*,void*,void*,void*,void*,void*,unsigned char,void*,unsigned)>(s.originals_[8])(
            commands,view,policy,mesh,mask,state,fog,proxy,id);
    }
    static void CommitDrawResources(void* rhi) {
        auto& s=*active_;
        reinterpret_cast<void (*)(void*)>(s.originals_[9])(rhi);
        // Native 1411F79A0 commits graphics scalar constants after 1411F7910
        // resource tables and before the actual D3D draw (141208090).
        if(s.accepting_ && s.parameter_thread_.load()==GetCurrentThreadId()) {
            const auto tick=ReadField<unsigned>(reinterpret_cast<void*>(s.base_),0x470d0c4);
            if((tick>=180 && tick<=215) || s.pass_observing_) {
                const auto index=ReadField<int>(rhi,0x17a08);
                if(index<0 || index>10000) s.parameter_truncated_=true;
                else {
                    const auto* bound=ReadField<const void*>(rhi,0x4188+std::size_t(index?index-1:9999)*8);
                    if(!bound) s.parameter_truncated_=true;
                    else {
                        const auto caller=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(_ReturnAddress())-s.base_);
                        s.RecordParameter(6,0,caller,5*sizeof(void*),static_cast<const std::byte*>(bound)+0x28);
                        if((tick==206 || s.pass_observing_) && s.context_d3d_) {
                            s.pass_observing_=true;
                            s.RecordDrawBindings(caller,rhi);
                        }
                    }
                }
            }
        }
    }
    void RecordDrawBindings(unsigned caller,void* rhi) {
        // Read-only immediate-context state after native constant commit.
        // Selected buffer copies retain sources through explicit GPU completion.
        Com<ID3D11VertexShader> vs;Com<ID3D11PixelShader> ps;
        Com<ID3D11DepthStencilView> dsv;
        ID3D11RenderTargetView* raw_views[8]{};
        Com<ID3D11DepthStencilState> depth;Com<ID3D11RasterizerState> raster;
        context_d3d_->VSGetShader(&vs,nullptr,nullptr);
        context_d3d_->PSGetShader(&ps,nullptr,nullptr);
        context_d3d_->OMGetRenderTargets(8,raw_views,&dsv);
        unsigned stencil_ref{};context_d3d_->OMGetDepthStencilState(&depth,&stencil_ref);
        context_d3d_->RSGetState(&raster);
        D3D11_DEPTH_STENCIL_DESC d{};
        if(depth) depth->GetDesc(&d);
        else {d.DepthEnable=TRUE;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;d.DepthFunc=D3D11_COMPARISON_LESS;}
        D3D11_RASTERIZER_DESC r{};
        if(raster) raster->GetDesc(&r);
        else {r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_BACK;r.DepthClipEnable=TRUE;}
        std::array<Com<ID3D11Resource>,9> targets;
        for(unsigned i=0;i<8;++i) {
            Com<ID3D11RenderTargetView> view;view.Attach(raw_views[i]);
            if(view) view->GetResource(&targets[i]);
        }
        if(dsv) dsv->GetResource(&targets[8]);
        const auto& color=targets[0];const auto& depth_image=targets[8];
        bool changed{};for(unsigned i=0;i<targets.size();++i) changed|=last_draw_targets_[i].Get()!=targets[i].Get();
        if(changed) {if(pass_color_groups_==7) StreamTranslucentOutput();CapturePassOutputs();last_draw_targets_=targets;pass_group_draws_=0;}
        // Refine the proven group7-equal/group8-different interval. Copies
        // here precede the upcoming draw, so last_draw names completed work.
        if(pass_color_groups_==7) {
            if(!pass_group_draws_) {
                if(stream_runs_>=2) {error_=33;return;}
                stream_generation_=stream_runs_++;
            } else StreamTranslucentOutput();
            stream_vs_=reinterpret_cast<std::uintptr_t>(vs.Get());
            stream_ps_=reinterpret_cast<std::uintptr_t>(ps.Get());
        }
        ++pass_draws_;++pass_group_draws_;
        if(pass_color_groups_==7 && pass_group_draws_==10) CaptureTranslucentInputs();
        const std::uint64_t words[]{reinterpret_cast<std::uintptr_t>(vs.Get()),reinterpret_cast<std::uintptr_t>(ps.Get()),
            reinterpret_cast<std::uintptr_t>(color.Get()),reinterpret_cast<std::uintptr_t>(depth_image.Get()),
            static_cast<unsigned>(d.DepthEnable),d.DepthWriteMask,d.DepthFunc,static_cast<unsigned>(d.StencilEnable),stencil_ref,
            d.StencilReadMask,d.StencilWriteMask,d.FrontFace.StencilFailOp,d.FrontFace.StencilDepthFailOp,d.FrontFace.StencilPassOp,d.FrontFace.StencilFunc,
            d.BackFace.StencilFailOp,d.BackFace.StencilDepthFailOp,d.BackFace.StencilPassOp,d.BackFace.StencilFunc,
            r.FillMode,r.CullMode,static_cast<unsigned>(r.FrontCounterClockwise),static_cast<unsigned>(r.DepthBias),
            std::bit_cast<unsigned>(r.DepthBiasClamp),std::bit_cast<unsigned>(r.SlopeScaledDepthBias),static_cast<unsigned>(r.DepthClipEnable),
            static_cast<unsigned>(r.ScissorEnable),static_cast<unsigned>(r.MultisampleEnable),static_cast<unsigned>(r.AntialiasedLineEnable)};
        RecordParameter(8,0,caller,sizeof(words),words);
    }
    // One diagnostic frame only: stream completed draw outputs through one
    // staging image. No per-draw texture retention, no expected-state writes.
    void StreamTranslucentOutput() {
        if(error_ || !pass_group_draws_) return;
        auto& slot=stream_slot_;
        if(slot.pending || pass_group_draws_>64) {error_=33;return;}
        Com<ID3D11Texture2D> source;
        if(!last_draw_targets_[0] || FAILED(last_draw_targets_[0].As(&source))) {error_=33;return;}
        D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
        if(d.Width!=desc_.Width || d.Height!=desc_.Height || d.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT
            || d.SampleDesc.Count!=1 || d.MipLevels!=1 || d.ArraySize!=1) {error_=33;return;}
        if(!slot.image) {
            const auto allocation=std::uint64_t(d.Width)*d.Height*8*2+256;
            if(bytes_+pass_bytes_+allocation+4*1024*1024>reserved_bytes) {error_=33;return;}
            auto staging=d;staging.Usage=D3D11_USAGE_STAGING;staging.BindFlags=0;
            staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;staging.MiscFlags=0;
            const D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};
            if(FAILED(device_->CreateTexture2D(&staging,nullptr,&slot.image))
                || FAILED(device_->CreateQuery(&q,&slot.event))) {error_=33;return;}
            slot.desc=d;pass_bytes_+=allocation;
        }
        if(slot.desc.Width!=d.Width || slot.desc.Height!=d.Height) {error_=33;return;}
        slot.source=source;slot.pending=true;
        const auto start=GetTickCount64();
        context_d3d_->CopyResource(slot.image.Get(),source.Get());context_d3d_->End(slot.event.Get());
        context_d3d_->Flush();BOOL done=FALSE;
        while(true) {
            const auto hr=context_d3d_->GetData(slot.event.Get(),&done,sizeof(done),0);
            if(hr==S_OK && done) break;
            if(FAILED(hr) || GetTickCount64()-start>=500) {error_=33;return;}
            SwitchToThread();
        }
        D3D11_MAPPED_SUBRESOURCE map{};
        if(FAILED(context_d3d_->Map(slot.image.Get(),0,D3D11_MAP_READ,0,&map))) {error_=33;return;}
        std::uint64_t hash=1469598103934665603ull;
        for(unsigned y=0;y<d.Height;++y) {
            const auto* row=static_cast<const unsigned char*>(map.pData)+std::size_t(y)*map.RowPitch;
            for(unsigned x=0;x<d.Width*8;++x) hash=(hash^row[x])*1099511628211ull;
        }
        context_d3d_->Unmap(slot.image.Get(),0);
        RC::Output::send<RC::LogLevel::Default>(STR(
            "[ReplayQualification] translucent draw output run_id={} tick=206 generation={} draw={} last_draw={} hash={:016x} vs={:x} ps={:x} elapsed_ms={} gpu_complete=true\n"),
            run_,stream_generation_,pass_group_draws_,pass_draws_,hash,stream_vs_,stream_ps_,GetTickCount64()-start);
        slot.source.Reset();slot.pending=false;
    }
    void CaptureInputBuffer(ID3D11Buffer* buffer,unsigned kind,unsigned index) {
        if(!buffer || error_) return;
        D3D11_BUFFER_DESC d{};buffer->GetDesc(&d);
        if(input_count_==input_buffers_.size() || !d.ByteWidth || d.ByteWidth>8*1024*1024) {error_=32;return;}
        auto& slot=input_buffers_[input_count_++];
        if(slot.pending) {error_=32;return;}
        if(!slot.image || slot.bytes!=d.ByteWidth) {
            const auto retained=std::uint64_t(d.ByteWidth)*2;
            if(input_bytes_-slot.bytes*2ull+retained>32*1024*1024
                || bytes_+pass_bytes_-slot.bytes*2ull+retained+4*1024*1024>reserved_bytes) {error_=32;return;}
            slot.image.Reset();slot.event.Reset();
            D3D11_BUFFER_DESC staging{};staging.ByteWidth=d.ByteWidth;staging.Usage=D3D11_USAGE_STAGING;staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};
            if(FAILED(device_->CreateBuffer(&staging,nullptr,&slot.image)) || FAILED(device_->CreateQuery(&q,&slot.event))) {error_=32;return;}
            input_bytes_=input_bytes_-slot.bytes*2ull+retained;pass_bytes_=pass_bytes_-slot.bytes*2ull+retained;slot.bytes=d.ByteWidth;
        }
        slot.source=buffer;slot.kind=kind;slot.index=index;slot.pending=true;slot.tagged=false;
        context_d3d_->CopyResource(slot.image.Get(),buffer);context_d3d_->End(slot.event.Get());
    }
    void CaptureTranslucentInputs() {
        for(const auto& slot:input_buffers_) if(slot.pending) {error_=32;return;}
        input_count_=0;
        for(unsigned stage=0;stage<2;++stage) {
            ID3D11Buffer* raw[14]{};
            if(stage) context_d3d_->PSGetConstantBuffers(0,14,raw);else context_d3d_->VSGetConstantBuffers(0,14,raw);
            for(unsigned i=0;i<14;++i) {Com<ID3D11Buffer> buffer;buffer.Attach(raw[i]);CaptureInputBuffer(buffer.Get(),stage,i);}
        }
        ID3D11Buffer* raw[16]{};unsigned strides[16]{},offsets[16]{};
        context_d3d_->IAGetVertexBuffers(0,16,raw,strides,offsets);
        for(unsigned i=0;i<16;++i) {
            Com<ID3D11Buffer> buffer;buffer.Attach(raw[i]);CaptureInputBuffer(buffer.Get(),2,i);
            if(buffer) RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] translucent vertex binding run_id={} tick=206 slot={} stride={} offset={}\n"),run_,i,strides[i],offsets[i]);
        }
        Com<ID3D11Buffer> indices;DXGI_FORMAT format{};unsigned offset{};
        context_d3d_->IAGetIndexBuffer(&indices,&format,&offset);CaptureInputBuffer(indices.Get(),3,0);
        for(unsigned stage=0;stage<2;++stage) {
            ID3D11ShaderResourceView* views[16]{};
            if(stage) context_d3d_->PSGetShaderResources(0,16,views);else context_d3d_->VSGetShaderResources(0,16,views);
            for(unsigned i=0;i<16;++i) {
                Com<ID3D11ShaderResourceView> view;view.Attach(views[i]);if(!view) continue;
                D3D11_SHADER_RESOURCE_VIEW_DESC srv{};view->GetDesc(&srv);
                Com<ID3D11Resource> resource;view->GetResource(&resource);D3D11_RESOURCE_DIMENSION dimension{};resource->GetType(&dimension);
                Com<ID3D11Buffer> buffer;if(SUCCEEDED(resource.As(&buffer))) CaptureInputBuffer(buffer.Get(),4+stage,i);
                unsigned width{},height{},depth{},usage{},bind{},format_value{};
                Com<ID3D11Texture2D> t2;Com<ID3D11Texture3D> t3;
                if(SUCCEEDED(resource.As(&t2))) {D3D11_TEXTURE2D_DESC d{};t2->GetDesc(&d);width=d.Width;height=d.Height;depth=d.ArraySize;usage=d.Usage;bind=d.BindFlags;format_value=d.Format;
                    // Texture content selection follows the captured shader consumption audit.
                }
                else if(SUCCEEDED(resource.As(&t3))) {D3D11_TEXTURE3D_DESC d{};t3->GetDesc(&d);width=d.Width;height=d.Height;depth=d.Depth;usage=d.Usage;bind=d.BindFlags;format_value=d.Format;}
                RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] translucent resource binding run_id={} tick=206 stage={} slot={} resource={:x} dimension={} width={} height={} depth={} format={} usage={} bind={} view_dimension={} view_format={} first={} elements={}\n"),
                    run_,stage,i,reinterpret_cast<std::uintptr_t>(resource.Get()),unsigned(dimension),width,height,depth,format_value,usage,bind,unsigned(srv.ViewDimension),unsigned(srv.Format),srv.Buffer.FirstElement,srv.Buffer.NumElements);
            }
        }
    }
    void FinishInputBuffers() {
        for(auto& slot:input_buffers_) if(slot.pending) {
            if(!slot.tagged) {slot.generation=render_generation_;slot.tagged=true;}
            BOOL done{};const auto hr=context_d3d_->GetData(slot.event.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(hr==S_FALSE || (hr==S_OK && !done)) continue;
            if(FAILED(hr)) {error_=32;continue;}
            D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(context_d3d_->Map(slot.image.Get(),0,D3D11_MAP_READ,0,&map))) {error_=32;continue;}
            std::uint64_t hash=1469598103934665603ull;std::wstring words;constexpr wchar_t hex[]=L"0123456789abcdef";
            const auto word_bytes=std::min(slot.bytes,131072u);words.reserve(std::size_t(word_bytes)*2);
            for(unsigned i=0;i<slot.bytes;++i) {const auto b=static_cast<const unsigned char*>(map.pData)[i];hash=(hash^b)*1099511628211ull;if(i<word_bytes) {words+=hex[b>>4];words+=hex[b&15];}}
            context_d3d_->Unmap(slot.image.Get(),0);
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] translucent input buffer run_id={} tick=206 generation={} kind={} slot={} bytes={} hash={:016x} word_bytes={} words={} gpu_complete=true\n"),run_,slot.generation,slot.kind,slot.index,slot.bytes,hash,word_bytes,words);
            slot.pending=false;slot.source.Reset();
        }
    }
    bool FullResolutionResource(ID3D11Resource* resource) {
        if(!resource) return false;
        Com<ID3D11Texture2D> texture;
        if(FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture)))) return false;
        D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
        return d.Width==desc_.Width && d.Height==desc_.Height;
    }
    void CapturePassOutputs() {
        if(pass_color_groups_>=8) return;
        if(last_draw_targets_[0]) {
            if(!FullResolutionResource(last_draw_targets_[0].Get())) {++pass_skipped_;return;}
            ++pass_color_groups_;
            if(pass_color_groups_==1 || pass_color_groups_==7 || pass_color_groups_==8)
                CapturePassOutput(last_draw_targets_[0].Get(),0);
        } else if(!pass_depth_captured_ && FullResolutionResource(last_draw_targets_[8].Get())) {
            pass_depth_captured_=true;CapturePassOutput(last_draw_targets_[8].Get(),8);
        }
    }
    void CapturePassOutput(ID3D11Resource* resource,unsigned target,unsigned mip=0,bool input=false) {
        Com<ID3D11Texture2D> source;
        if(FAILED(resource->QueryInterface(IID_PPV_ARGS(&source)))) {++pass_skipped_;return;}
        D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
        const unsigned bpp=d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT ? 8u :
            (d.Format==DXGI_FORMAT_R16_UINT || d.Format==DXGI_FORMAT_R16_FLOAT || d.Format==DXGI_FORMAT_R16_UNORM) ? 2u :
            (d.Format==DXGI_FORMAT_R11G11B10_FLOAT || d.Format==DXGI_FORMAT_R10G10B10A2_UNORM
            || d.Format==DXGI_FORMAT_R8G8B8A8_UNORM || d.Format==DXGI_FORMAT_B8G8R8A8_UNORM
            || d.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS || d.Format==DXGI_FORMAT_R8G8B8A8_SNORM
            || d.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS || d.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
            || d.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || d.Format==DXGI_FORMAT_R16G16_FLOAT
            || d.Format==DXGI_FORMAT_R16G16_UNORM || d.Format==DXGI_FORMAT_R32_UINT
            || d.Format==DXGI_FORMAT_R24G8_TYPELESS || d.Format==DXGI_FORMAT_R32_TYPELESS
            || d.Format==DXGI_FORMAT_R32_FLOAT || d.Format==DXGI_FORMAT_D32_FLOAT ? 4u : 0u);
        // Selected full-resolution outputs only. Missing transitions and
        // compute-only outputs remain explicit limits, not implicit matches.
        if(!bpp || (!input && (d.Width!=desc_.Width || d.Height!=desc_.Height || d.MipLevels!=1)) || d.SampleDesc.Count!=1
            || mip>=d.MipLevels || d.ArraySize!=1) {
            ++pass_skipped_;
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] render pass omitted run_id={} live_tick=206 group={} target={} last_draw={} width={} height={} format={} mips={} array={} samples={}\n"),
                run_,pass_color_groups_,target,pass_draws_,d.Width,d.Height,static_cast<unsigned>(d.Format),d.MipLevels,d.ArraySize,d.SampleDesc.Count);
            return;
        }
        d.Width=std::max(1u,d.Width>>mip);d.Height=std::max(1u,d.Height>>mip);d.MipLevels=1;
        if(pass_ordinal_==pass_slots_.size()) {pass_overflow_=true;return;}
        auto& slot=pass_slots_[pass_ordinal_++];
        if(slot.pending) {pass_overflow_=true;return;}
        const auto allocation=std::uint64_t(d.Width)*d.Height*bpp;
        if(!slot.image || slot.desc.Width!=d.Width || slot.desc.Height!=d.Height || slot.desc.Format!=d.Format) {
            const auto old=std::uint64_t(slot.desc.Width)*slot.desc.Height*slot.bpp;
            // Include the full leased source as well as its staging image.
            if(bytes_+pass_bytes_-old*2+allocation*2+4*1024*1024>reserved_bytes) {pass_overflow_=true;return;}
            slot.image.Reset();slot.event.Reset();
            auto staging=d;staging.Usage=D3D11_USAGE_STAGING;staging.BindFlags=0;staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;staging.MiscFlags=0;
            D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
            if(FAILED(device_->CreateTexture2D(&staging,nullptr,&slot.image)) || FAILED(device_->CreateQuery(&query,&slot.event))) {error_=25;return;}
            pass_bytes_=pass_bytes_-old*2+allocation*2;slot.desc=d;slot.bpp=bpp;
        }
        slot.group=pass_color_groups_;slot.target=target;slot.ordinal=pass_ordinal_-1;slot.last_draw=pass_draws_;slot.source=source;slot.pending=true;slot.tagged=false;
        context_d3d_->CopySubresourceRegion(slot.image.Get(),0,0,0,0,source.Get(),mip,nullptr);context_d3d_->End(slot.event.Get());
    }
    void FinishPassOutputs(unsigned tick) {
        if(pass_draws_) {
            CapturePassOutputs();for(auto& target:last_draw_targets_) target.Reset();
            for(auto& slot:pass_slots_) if(slot.pending && !slot.tagged) {slot.generation=render_generation_;slot.tagged=true;}
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] render pass selection run_id={} tick={} generation={} selected={} skipped={} overflow={} bytes={} scope=groups1_7_8_with_streamed_translucent_draws_primary_depth\n"),
                run_,tick,render_generation_,pass_ordinal_,pass_skipped_,pass_overflow_,bytes_+pass_bytes_);
            if(tick!=206 || pass_overflow_) error_=26;
            pass_ordinal_=pass_draws_=pass_skipped_=pass_color_groups_=0;pass_depth_captured_=false;pass_overflow_=false;pass_observing_=false;
        }
        for(auto& slot:pass_slots_) if(slot.pending && slot.tagged) {
            BOOL complete{};const auto hr=context_d3d_->GetData(slot.event.Get(),&complete,sizeof(complete),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(hr==S_FALSE || (hr==S_OK && !complete)) continue;
            if(FAILED(hr)) {error_=27;continue;}
            D3D11_MAPPED_SUBRESOURCE map{};
            if(FAILED(context_d3d_->Map(slot.image.Get(),0,D3D11_MAP_READ,0,&map))) {error_=28;continue;}
            std::uint64_t hash=1469598103934665603ull;
            for(unsigned y=0;y<slot.desc.Height;++y) {
                auto* row=static_cast<const unsigned char*>(map.pData)+std::size_t(y)*map.RowPitch;
                for(unsigned x=0;x<slot.desc.Width*slot.bpp;++x) hash=(hash^row[x])*1099511628211ull;
            }
            constexpr wchar_t hex[]=L"0123456789abcdef";std::wstring pixels;pixels.reserve(96*54*slot.bpp*2);
            for(unsigned y=0;y<54;++y) for(unsigned x=0;x<96;++x) {
                auto* p=static_cast<const unsigned char*>(map.pData)+std::size_t((2*y+1)*slot.desc.Height/108)*map.RowPitch+((2*x+1)*slot.desc.Width/192)*slot.bpp;
                for(unsigned c=0;c<slot.bpp;++c) {pixels+=hex[p[c]>>4];pixels+=hex[p[c]&15];}
            }
            context_d3d_->Unmap(slot.image.Get(),0);
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] render pass output run_id={} tick=206 generation={} ordinal={} last_draw={} format={} width={} height={} hash={:016x} pixels={} gpu_complete=true group={} target={}\n"),
                run_,slot.generation,slot.ordinal,slot.last_draw,static_cast<unsigned>(slot.desc.Format),slot.desc.Width,slot.desc.Height,hash,pixels,slot.group,slot.target);
            slot.source.Reset();slot.pending=false;
        }
    }
    void LogShaderParameters(unsigned tick) {
        DWORD absent{};const auto thread=GetCurrentThreadId();
        parameter_thread_.compare_exchange_strong(absent,thread);
        if(parameter_thread_.load()!=thread) {parameter_wrong_thread_=true;return;}
        if(tick==206) {
            // The frame is tagged by the existing Slate task, while each row
            // retains its live tick to expose any GT/RT coordinate drift.
            // Domain6 contains process-local bound resources, not canonical IDs.
            constexpr wchar_t hex[]=L"0123456789abcdef";
            for(unsigned i=0;i<parameter_count_;++i) {
                const auto& row=parameter_rows_[i];std::wstring bytes;bytes.reserve(row.size*2);
                for(unsigned j=0;j<row.size;++j) {const auto b=parameter_bytes_[row.data+j];bytes+=hex[b>>4];bytes+=hex[b&15];}
                RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] shader parameter run_id={} tick={} generation={} ordinal={} domain={} buffer={} offset={} size={} live_tick={} bytes={}\n"),
                    run_,tick,render_generation_,i,row.domain,row.buffer,row.offset,row.size,row.live_tick,bytes);
            }
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] shader parameter completed run_id={} tick={} generation={} rows={} bytes={} truncated={} wrong_thread={} scope=native_constant_submissions_between_presents\n"),
                run_,tick,render_generation_,parameter_count_,parameter_size_,parameter_truncated_,parameter_wrong_thread_.load());
        }
        parameter_count_=parameter_size_=0;parameter_truncated_=false;
    }
    // Native histogram adaptation14138B8D0 writes the current exposure target
    // without the basic pass index swap. Keep all shader/RHI tails native.
    static void PublishView(void* view)
    {
        auto* self=active_;
        reinterpret_cast<void (*)(void*)>(self->originals_[4])(view);
        if(!self->accepting_) return;
        // 1414F0180 publishes the B90-byte view uniform block at view+1228
        // through RHI CreateUniformBuffer. Read only after its native builder.
        // These are actual shader inputs, not live GT clocks sampled on RT.
        const auto* views=static_cast<const std::byte*>(view);
        const auto* state=*reinterpret_cast<const std::byte* const*>(views+0x1220);
        self->visibility_state_=state;
        const auto* uniforms=*reinterpret_cast<const std::byte* const*>(views+0x1228);
        if(!state || !uniforms) {self->view_={};return;}
        const auto observed_tick=*reinterpret_cast<const unsigned*>(self->base_+0x470d0c4);
        if(SelectedPostprocessTick(observed_tick))
        {
            const auto family=*reinterpret_cast<const std::byte* const*>(views);
            const auto scene_interface=family ? *reinterpret_cast<void* const*>(family+0x30) : nullptr;
            const auto scene=scene_interface ? reinterpret_cast<const std::byte* (*)(void*)>((*reinterpret_cast<std::uintptr_t**>(scene_interface))[0x1c0/8])(scene_interface) : nullptr;
            const auto sky=scene ? *reinterpret_cast<const std::byte* const*>(scene+0xbd8) : nullptr;
            const auto component=sky ? *reinterpret_cast<const std::byte* const*>(sky) : nullptr;
            if(component) RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] skylight input diagnostic run_id={} tick={} source_type={} resolution={} angle={:08x} blend={:08x} captured={} coefficient={:08x},{:08x},{:08x}\n"),
                self->run_,observed_tick,*reinterpret_cast<const unsigned char*>(component+0x408),
                *reinterpret_cast<const unsigned*>(component+0x41c),*reinterpret_cast<const unsigned*>(component+0x418),
                *reinterpret_cast<const unsigned*>(component+0x4f4),*reinterpret_cast<const unsigned char*>(component+0x44e),
                *reinterpret_cast<const unsigned*>(component+0x474),*reinterpret_cast<const unsigned*>(component+0x47c),*reinterpret_cast<const unsigned*>(component+0x4d4));
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] view history inventory run_id={} tick={} state={:x} state_interface={:x} ao_jitter={} ao_history={} ao_index={}\n"),
                self->run_,observed_tick,reinterpret_cast<std::uintptr_t>(state),ReadField<std::uintptr_t>(views,8),ReadField<unsigned>(reinterpret_cast<void*>(self->base_),0x407b3f8),ReadField<unsigned>(reinterpret_cast<void*>(self->base_),0x407b3bc),ReadField<unsigned>(state,0xb24));
            // Native ReleaseRHI is the +20 subobject (constructor141492DD0).
            // Only independently verified owning target slots, not a raw state dump.
            for(const auto offset:{0xae0u,0xae8u,0xaf8u,0xb40u,0xb48u,0xb50u,0xb58u,0xb60u,0xb68u,0xbc0u,0xbc8u,0xbd0u}) {
                const auto target=ReadField<const void*>(state,offset);
                if(!target) {
                    RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] view history resource run_id={} tick={} offset={:x} present=false\n"),self->run_,observed_tick,offset);continue;
                }
                const bool known=ReadField<std::uintptr_t>(target,0)==self->base_+0x364c2a8;
                RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] view history resource run_id={} tick={} offset={:x} present=true known={} owner={:x} pool={:x} refs={}\n"),
                    self->run_,observed_tick,offset,known,reinterpret_cast<std::uintptr_t>(target),known?ReadField<std::uintptr_t>(target,0xa8):0,known?ReadField<int>(target,0x48):-1);
            }
            // Read-only first-mismatch projection. Unknown/padding words are
            // diagnostic evidence only, never canonical state or restore input.
            constexpr wchar_t hex[]=L"0123456789abcdef";
            std::wstring words;words.reserve(0xac0*2);
            for(unsigned i=0;i<0xac0;i+=4)
            {
                const auto value=*reinterpret_cast<const unsigned*>(uniforms+i);
                for(int n=7;n>=0;--n) words+=hex[(value>>(n*4))&15];
            }
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] view uniform diagnostic run_id={} tick={} words={}\n"),self->run_,observed_tick,words);
        }
        auto& v=self->view_;
        v.width=*reinterpret_cast<const int*>(views+0x78)-*reinterpret_cast<const int*>(views+0x70);
        v.height=*reinterpret_cast<const int*>(views+0x7c)-*reinterpret_cast<const int*>(views+0x74);
        v.frame=*reinterpret_cast<const unsigned*>(state+0x8fc);
        v.temporal=*reinterpret_cast<const unsigned char*>(state+0xb1c);
        v.sample_count=*reinterpret_cast<const unsigned char*>(state+0xb1d);
        v.frame_index=*reinterpret_cast<const unsigned*>(state+0xb20);
        v.occlusion=*reinterpret_cast<const unsigned*>(state+0xb24);
        v.jitter_x=*reinterpret_cast<const unsigned*>(views+0x954);
        v.jitter_y=*reinterpret_cast<const unsigned*>(views+0x958);
        v.aa=*reinterpret_cast<const unsigned*>(views+0x11d0);
        v.game_time=*reinterpret_cast<const unsigned*>(uniforms+0x808);
        v.real_time=*reinterpret_cast<const unsigned*>(uniforms+0x80c);
        v.random=*reinterpret_cast<const unsigned*>(uniforms+0x810);
        v.family_frame=*reinterpret_cast<const unsigned*>(uniforms+0x814);
        v.shader_index=*reinterpret_cast<const unsigned*>(uniforms+0x818);
        // First 0x240 bytes are explicitly written current view/projection
        // matrices in 14208C930; no padding, pointers or previous history.
        v.previous_matrices=1469598103934665603ull;
        // FViewMatrices ctor 141493B20 ends fields at +368; +36C is alignment padding.
        for(unsigned i=0;i<0x36c;i+=4)
            v.previous_matrices=(v.previous_matrices^*reinterpret_cast<const unsigned*>(views+0x1fe0+i))*1099511628211ull;
        v.history_flags=*reinterpret_cast<const unsigned*>(views+0x1fcc)&0x2c; // reset/occlusion bits audited in 1414F38B0
        v.motion_blur_scale=*reinterpret_cast<const unsigned*>(state+0x8a0);
        v.matrices=1469598103934665603ull;
        for(unsigned i=0;i<0x240;i+=4)
            v.matrices=(v.matrices^*reinterpret_cast<const unsigned*>(uniforms+i))*1099511628211ull;
    }
    static bool Pose(void* proxy,void* output)
    {
        auto* self=active_;
        const auto result=reinterpret_cast<bool (*)(void*,void*)>(self->originals_[3])(proxy,output);
        if(!self->accepting_ || !result || !output) return result;
        const auto tick=*reinterpret_cast<const std::uint32_t*>(self->base_+0x470d0c4);
        if(!self->ObserveTick(tick)) return result;
        // 140441A20 binds proxy+4B8 to ULuxCharaAnimInstance+378.
        // 140459740 and 140459310 produce the compact pose at context+8.
        const auto* raw=static_cast<const std::byte*>(proxy);
        const auto* instance=*reinterpret_cast<const std::byte* const*>(raw+0xa0);
        const auto* node=*reinterpret_cast<const std::byte* const*>(raw+0x4b8);
        if(!instance || node!=instance+0x378) {self->error_=12;return result;}
        const auto player=*reinterpret_cast<const int*>(node+0x30);
        const auto* mapping=*reinterpret_cast<const unsigned* const*>(node+0x38);
        const auto map_count=*reinterpret_cast<const int*>(node+0x40);
        const auto map_capacity=*reinterpret_cast<const int*>(node+0x44);
        const auto* context=static_cast<const std::byte*>(output);
        const auto* transforms=*reinterpret_cast<const unsigned* const*>(context+8);
        const auto count=*reinterpret_cast<const int*>(context+0x10);
        const auto capacity=*reinterpret_cast<const int*>(context+0x14);
        if(count<0 || count>1024 || capacity<count || (count && !transforms)
            || map_count<0 || map_count>1024 || map_capacity<map_count || (map_count && !mapping)) {
            self->error_=13;return result;
        }
        PoseObservation sample{player,static_cast<unsigned>(count),1469598103934665603ull,1469598103934665603ull};
        for(int i=0;i<map_count;++i) sample.mapping=(sample.mapping^mapping[i])*1099511628211ull;
        // FTransform's translation/scale W lanes are padding, not pose values.
        for(int i=0;i<count;++i) for(unsigned word=0;word<12;++word)
            if(word!=7 && word!=11) sample.hash=(sample.hash^transforms[i*12+word])*1099511628211ull;
        std::lock_guard lock(self->mutex_);
        if(self->pose_count_ && self->pose_tick_!=tick) {self->error_=14;return result;}
        if(self->pose_count_==self->poses_.size()) {self->error_=15;return result;}
        self->pose_tick_=tick;
        self->poses_[self->pose_count_++]=sample;
        if(tick==185 || tick==206) {
            std::wstring words;words.reserve(std::size_t(count)*80);
            constexpr wchar_t hex[]=L"0123456789abcdef";
            for(int bone=0;bone<count;++bone) for(unsigned word=0;word<12;++word) {
                if(word==7 || word==11) continue;
                const auto value=transforms[bone*12+word];
                for(int nibble=7;nibble>=0;--nibble) words+=hex[(value>>(nibble*4))&15];
            }
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] native pose detail run_id={} tick={} generation={} player={} bones={} mapping={:016x} hash={:016x} owner={} words={}\n"),
                self->run_,tick,self->generation_+(tick<self->last_tick_?1:0),player,count,sample.mapping,sample.hash,
                reinterpret_cast<RC::Unreal::UObject*>(const_cast<std::byte*>(instance))->GetFullName(),words);
        }
        if(GetCurrentThreadId()!=self->thread_) ++self->pose_async_;
        return result;
    }

    void LogPoses(std::uint32_t tick)
    {
        // Read-only consumer inputs for the first remaining hair/cloth
        // divergence. 14034B8D0 consumes these per-player values; a matching
        // gameplay position alone cannot establish equal spring admission.
        for(unsigned player=0;player<2;++player) {
            const auto* fighter=*reinterpret_cast<const std::byte* const*>(base_+0x470de90+player*sizeof(void*));
            if(!fighter) {error_=17;continue;}
            const auto* skeleton=fighter+0x29120;
            std::array<unsigned,4> wind{};
            std::memcpy(wind.data(),skeleton+0x1f0,sizeof(wind));
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] native spring inputs run_id={} tick={} generation={} player={} delta={:08x} mode={} wind={:08x},{:08x},{:08x},{:08x}\n"),
                run_,tick,generation_,player,*reinterpret_cast<const unsigned*>(skeleton+0x18c),
                *reinterpret_cast<const unsigned short*>(skeleton+0x1d0),wind[0],wind[1],wind[2],wind[3]);
        }
        // Preserve multiplicity while avoiding process-local instance addresses
        // and parallel evaluation order in the comparison key.
        if(pose_count_ && pose_tick_!=tick) error_=16;
        std::sort(poses_.begin(),poses_.begin()+pose_count_,[](const auto& a,const auto& b){
            if(a.player!=b.player) return a.player<b.player;
            if(a.mapping!=b.mapping) return a.mapping<b.mapping;
            if(a.count!=b.count) return a.count<b.count;
            return a.hash<b.hash;
        });
        for(unsigned i=0;i<pose_count_;++i) {
            const auto& sample=poses_[i];
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] native pose publication run_id={} tick={} generation={} ordinal={} player={} bones={} mapping={:016x} hash={:016x} async={}\n"),
                run_,tick,generation_,i,sample.player,sample.count,sample.mapping,sample.hash,pose_async_);
        }
        RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] native pose completed run_id={} tick={} generation={} evaluations={}\n"),run_,tick,generation_,pose_count_);
        pose_count_=pose_async_=pose_tick_=0;
    }

    static void Execute(void* task)
    {
        auto* self=active_;
        const auto prior=render_tick_;
        const auto prior_generation=render_generation_;
        render_tick_=0;
        {
            std::lock_guard lock(self->mutex_);
            for (auto& tag:self->tags_) if (tag.task==task) {render_tick_=tag.tick; render_generation_=tag.generation; tag={}; break;}
        }
        reinterpret_cast<void (*)(void*)>(self->originals_[1])(task);
        render_tick_=prior;
        render_generation_=prior_generation;
    }
    static bool Present(void* viewport, unsigned interval)
    {
        auto* self=active_;
        ++self->presents_;if(render_tick_) ++self->tagged_presents_;
        if (!render_tick_ && self->diagnostic_traces_.fetch_add(1)<3 && DiagnosticAllowed(Horse::Deterministic::ReplayDiagnosticTrace::Presentation,0)) {
            void* stack[16]{};const auto depth=Horse::Deterministic::g_replay_diagnostics.stack_depth;
            const auto count=depth ? CaptureStackBackTrace(0,depth,stack,nullptr) : 0;
            for(unsigned i=0;i<static_cast<unsigned>(count);++i)
                RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] native presentation untagged caller depth={} rva={:x}\n"),
                    i,reinterpret_cast<std::uintptr_t>(stack[i])-self->base_);
        }
        self->Observe(viewport,render_tick_);
        if(self->pass_diagnostics_) {
            self->LogShaderParameters(render_tick_);
            self->LogVisibilityOwners(render_tick_);
        }
        return reinterpret_cast<bool (*)(void*,unsigned)>(self->originals_[2])(viewport,interval);
    }
    // Read at the native Present boundary, after query submission and map
    // maintenance. PublishView is too early for the shadow-map final writer.
    // Resource addresses describe this process's ownership only; no result is
    // installed, no query is completed here, and no reference is acquired.
    void LogVisibilityOwners(unsigned tick)
    {
        if(!accepting_ || (tick!=205 && tick!=206 && tick!=210) || visibility_records_>=12) return;
        ++visibility_records_;
        const auto* state=visibility_state_;
        const auto live_tick=ReadField<unsigned>(reinterpret_cast<void*>(base_),0x470d0c4);
        if(!state || ReadField<std::uintptr_t>(state,0)!=base_+0x3657810) {
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] visibility ownership run_id={} tick={} live_tick={} valid=false\n"),run_,tick,live_tick);
            return;
        }
        const auto* auto_data=ReadField<const void*>(reinterpret_cast<void*>(base_),0x4335d18);
        const auto free_count=ReadField<int>(state,0x118);
        const auto free_capacity=ReadField<int>(state,0x11c);
        const auto* free_data=ReadField<const std::uintptr_t*>(state,0x110);
        std::array<std::uintptr_t,8192> free_queries{};
        bool valid=auto_data && free_count>=0 && free_capacity>=free_count && free_capacity<=8192
            && (!free_count || free_data) && ReadField<std::uintptr_t>(state,0x108)==base_+0x360c728
            && ReadField<unsigned>(state,0x120)==1;
        if(valid) {
            for(int i=0;i<free_count;++i) free_queries[i]=ReadField<std::uintptr_t>(free_data,std::size_t(i)*8);
            std::sort(free_queries.begin(),free_queries.begin()+free_count);
        }
        const int admitted_free=valid ? free_count : 0;
        RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] visibility ownership run_id={} tick={} generation={} live_tick={} state={:x} valid={} hzb_mode={} auto_downsample={} view_epoch={} family_epoch={} previous_family_epoch={} pool_free={} pool_capacity={} pool_bookkeeping={} shadow_count={} shadow_capacity={} shadow_heap={:x}\n"),
            run_,tick,render_generation_,live_tick,reinterpret_cast<std::uintptr_t>(state),valid,
            ReadField<int>(reinterpret_cast<void*>(base_),0x432d570),auto_data?ReadField<int>(auto_data,4):-1,
            ReadField<unsigned>(state,0x8fc),ReadField<unsigned>(state,0x890),ReadField<unsigned>(state,0x894),
            free_count,free_capacity,ReadField<int>(state,0x124),ReadField<int>(state,0x100),ReadField<int>(state,0x104),ReadField<std::uintptr_t>(state,0xf8));
        const auto* shadow_heap=ReadField<const std::byte*>(state,0xf8);
        const auto* shadows=shadow_heap ? shadow_heap : state+0x58;
        const int shadow_count=ReadField<int>(state,0x100);
        const std::array<const std::byte*,5> maps{
            shadow_count>0 && shadow_count<=2?shadows:nullptr,
            shadow_count==2?shadows+0x50:nullptr,state+0x950,state+0x9b0,state+0x1020};
        for(unsigned kind=0;kind<maps.size();++kind) {
            const auto* map=maps[kind];
            if(!map) continue;
            const auto* data=ReadField<const std::byte*>(map,0);
            const int count=ReadField<int>(map,8),capacity=ReadField<int>(map,0xc);
            const int bit_count=ReadField<int>(map,0x28),bit_capacity=ReadField<int>(map,0x2c);
            const int free_slots=ReadField<int>(map,0x34),hash_count=ReadField<int>(map,0x48);
            const auto* flag_heap=ReadField<const std::byte*>(map,0x20);
            const auto* flags=flag_heap?flag_heap:map+0x10;
            const unsigned stride=kind<2?0x28:kind==4?0x50:0x30;
            bool map_valid=count>=0 && capacity>=count && capacity<=8192 && bit_count==count
                && bit_capacity>=count && bit_capacity<=8192 && free_slots>=0 && free_slots<=count
                && (!count || data) && (flag_heap || bit_capacity<=128) && hash_count>=0 && hash_count<=16384;
            unsigned active{},refs{},ready{},bad_refs{},free_overlap{},heap_rings{},bad_rings{};
            std::uint64_t keys=1469598103934665603ull;
            auto inspect=[&](const void* resource,bool uniform) {
                if(!resource) return;
                ++refs;
                const auto vt=ReadField<std::uintptr_t>(resource,0);
                if(vt!=base_+(uniform?0x35d3dc0:0x35d3db8) || ReadField<int>(resource,8)<=0
                    || (!uniform && (ReadField<unsigned>(resource,0x2c)!=1 || !ReadField<void*>(resource,0x18)))) ++bad_refs;
                if(!uniform) {
                    ready+=(ReadField<unsigned>(resource,0x28)&1)!=0;
                    free_overlap+=std::binary_search(free_queries.begin(),free_queries.begin()+admitted_free,reinterpret_cast<std::uintptr_t>(resource));
                }
            };
            if(map_valid) for(int i=0;i<count;++i) {
                if(!(ReadField<unsigned>(flags,std::size_t(i/32)*4)&(1u<<(i%32)))) continue;
                ++active;
                const auto* entry=data+std::size_t(i)*stride;
                keys=(keys^ReadField<unsigned>(entry,0))*1099511628211ull;
                if(kind<2) inspect(ReadField<const void*>(entry,0x18),false);
                else if(kind==2) inspect(ReadField<const void*>(entry,0x10),true);
                else {
                    const auto* heap=ReadField<const std::byte*>(entry,0x18);
                    const auto* ring=heap?heap:entry+8;
                    const int size=ReadField<int>(entry,0x20),allocated=ReadField<int>(entry,0x24);
                    heap_rings+=heap!=nullptr;
                    if(size<0 || size>2 || allocated<size || allocated>2) {++bad_rings;continue;}
                    for(int j=0;j<size;++j) inspect(ReadField<const void*>(ring,std::size_t(j)*8),false);
                }
            }
            map_valid=map_valid && active==static_cast<unsigned>(count-free_slots) && !bad_refs && !bad_rings && !free_overlap;
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] visibility map run_id={} tick={} generation={} kind={} valid={} slots={} capacity={} active={} bit_capacity={} hash_count={} refs={} ready={} bad_refs={} free_overlap={} heap_rings={} bad_rings={} keys={:016x}\n"),
                run_,tick,render_generation_,kind,map_valid,count,capacity,active,bit_capacity,hash_count,refs,ready,bad_refs,free_overlap,heap_rings,bad_rings,keys);
        }
    }
    void Observe(void* viewport, std::uint32_t tick)
    {
        // The guard-resimulation window lands at300. Replace the earlier230
        // image with400 so the same bounded image slots cover resumed output.
        const bool coherence_tick=coherence_ && ((tick==190 && render_generation_==0)
            || (extra_first_>340 ? (tick==extra_first_+22 || tick==extra_first_+92)
                : (tick==300 || tick==(extra_first_==300?400u:230u))));
        if(coherence_tick) {
            if(*reinterpret_cast<void**>(static_cast<std::byte*>(viewport)+0xc0)
                || *reinterpret_cast<bool*>(base_+0x434459e)) {error_=34;return;}
            try {CaptureCoherence(*reinterpret_cast<IDXGISwapChain**>(static_cast<std::byte*>(viewport)+0x60),tick,render_generation_,false);} catch(...) {error_=34;}
        }
        if(!pixel_diagnostics_) {
            if(tick && accepting_ && !error_) {
                ++frames_;
                RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] native display boundary run_id={} tick={} generation={} gpu_readbacks={}\n"),run_,tick,render_generation_,
                    coherence_tick);
            }
            return;
        }
        if(device_) {FinishPassOutputs(tick);FinishInputBuffers();}
        if (device_) Retire();
        if (!tick || error_ || !accepting_) return;
        if (*reinterpret_cast<void**>(static_cast<std::byte*>(viewport)+0xc0)
            || *reinterpret_cast<bool*>(base_+0x434459e)) {error_=3;return;}
        auto* swap=*reinterpret_cast<IDXGISwapChain**>(static_cast<std::byte*>(viewport)+0x60);
        Com<ID3D11Texture2D> source;
        if (!swap || FAILED(swap->GetBuffer(0,IID_PPV_ARGS(source.GetAddressOf())))) {error_=4;return;}
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        if (!device_) {
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] native presentation descriptor width={} height={} format={} samples={} mips={} array={}\n"),
                desc.Width,desc.Height,static_cast<unsigned>(desc.Format),desc.SampleDesc.Count,desc.MipLevels,desc.ArraySize);
            if (desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM
                && desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                && desc.Format!=DXGI_FORMAT_R10G10B10A2_UNORM) {error_=5;return;}
            bytes_=std::uint64_t(desc.Width)*desc.Height*4*4+sizeof(*slots_)+sizeof(*this);
            if (desc.SampleDesc.Count!=1 || desc.MipLevels!=1 || desc.ArraySize!=1 || bytes_+(96*54+384*216)*8*sizeof(wchar_t)>ReservationBytes()-16ull*1024*1024) {error_=6;return;}
            desc_=desc;source_binding_=source.Get();source->GetDevice(device_.GetAddressOf());device_->GetImmediateContext(context_d3d_.GetAddressOf());
            desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
            for (auto& slot:*slots_) {
                D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
                if (FAILED(device_->CreateTexture2D(&desc,nullptr,slot.image.GetAddressOf()))
                    || FAILED(device_->CreateQuery(&query,slot.event.GetAddressOf()))) {error_=7;return;}
            }
        } else if (source.Get()!=source_binding_ || desc.Width!=desc_.Width || desc.Height!=desc_.Height || desc.Format!=desc_.Format) {error_=8;return;}
        for (auto& slot:*slots_) if (!slot.pending) {
            slot.view=view_;slot.source=source;slot.tick=tick;slot.generation=render_generation_;slot.pending=true;
            context_d3d_->CopyResource(slot.image.Get(),source.Get());context_d3d_->End(slot.event.Get());
            return; // Normal Present supplies submission; no per-frame forced Flush or wait.
        }
        error_=9;
    }
    void Retire()
    {
        for (auto& slot:*slots_) if (slot.pending) {
            BOOL complete{};
            const auto hr=context_d3d_->GetData(slot.event.Get(),&complete,sizeof(complete),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (hr==S_FALSE || (hr==S_OK && !complete)) continue;
            if (FAILED(hr)) {error_=10;continue;}
            D3D11_MAPPED_SUBRESOURCE map{};
            if (FAILED(context_d3d_->Map(slot.image.Get(),0,D3D11_MAP_READ,0,&map))) {error_=11;continue;}
            std::uint64_t hash=1469598103934665603ull;
            for (unsigned y=0;y<desc_.Height;++y) {
                const auto* row=static_cast<const std::byte*>(map.pData)+std::size_t(y)*map.RowPitch;
                for (unsigned x=0;x<desc_.Width*4;x+=4) {std::uint32_t word{};std::memcpy(&word,row+x,4);hash=(hash^word)*1099511628211ull;}
            }
            std::wstring pixels;pixels.reserve(96*54*8);
            constexpr wchar_t hex[]=L"0123456789abcdef";
            for (unsigned y=0;y<54;++y) for (unsigned x=0;x<96;++x) {
                const auto py=(2*y+1)*desc_.Height/108,px=(2*x+1)*desc_.Width/192;
                const auto* color=static_cast<const unsigned char*>(map.pData)+std::size_t(py)*map.RowPitch+px*4;
                unsigned char rgba[4]{};
                if(desc_.Format==DXGI_FORMAT_R10G10B10A2_UNORM) {
                    std::uint32_t word{};std::memcpy(&word,color,4);
                    for(unsigned c=0;c<3;++c) rgba[c]=static_cast<unsigned char>(((word>>(c*10))&1023)*255/1023);
                    rgba[3]=static_cast<unsigned char>((word>>30)*85);
                } else {
                    const bool bgra=desc_.Format==DXGI_FORMAT_B8G8R8A8_UNORM || desc_.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                    rgba[0]=color[bgra?2:0];rgba[1]=color[1];rgba[2]=color[bgra?0:2];rgba[3]=color[3];
                }
                for (unsigned c=0;c<4;++c) {pixels+=hex[rgba[c]>>4];pixels+=hex[rgba[c]&15];}
            }
            if(slot.tick==185 || slot.tick==206 || slot.tick==209 || slot.tick==210 || slot.tick==230 || slot.tick==280 || slot.tick==328) {
                // Bounded first-failure detail: enough resolution to locate
                // particle/geometry/HUD differences, not another state capture.
                // Seven selected ticks per generation, overwritten
                // with the runner's existing bounded raw log.
                std::wstring detail;detail.reserve(384*216*8);
                for(unsigned y=0;y<216;++y) for(unsigned x=0;x<384;++x) {
                    const auto py=(2*y+1)*desc_.Height/432,px=(2*x+1)*desc_.Width/768;
                    const auto* color=static_cast<const unsigned char*>(map.pData)+std::size_t(py)*map.RowPitch+px*4;
                    unsigned char rgba[4]{};
                    if(desc_.Format==DXGI_FORMAT_R10G10B10A2_UNORM) {
                        unsigned word{};std::memcpy(&word,color,4);
                        for(unsigned c=0;c<3;++c) rgba[c]=static_cast<unsigned char>(((word>>(c*10))&1023)*255/1023);
                        rgba[3]=static_cast<unsigned char>((word>>30)*85);
                    } else {
                        const bool bgra=desc_.Format==DXGI_FORMAT_B8G8R8A8_UNORM || desc_.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                        rgba[0]=color[bgra?2:0];rgba[1]=color[1];rgba[2]=color[bgra?0:2];rgba[3]=color[3];
                    }
                    for(unsigned c=0;c<4;++c) {detail+=hex[rgba[c]>>4];detail+=hex[rgba[c]&15];}
                }
                RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] native presentation detail run_id={} tick={} generation={} width=384 height=216 pixels={}\n"),run_,slot.tick,slot.generation,detail);
            }
            context_d3d_->Unmap(slot.image.Get(),0);
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] native presentation run_id={} tick={} generation={} width={} height={} format={} hash={:016x} pixels={}\n"),
                run_,slot.tick,slot.generation,desc_.Width,desc_.Height,static_cast<unsigned>(desc_.Format),hash,pixels);
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] native render view run_id={} tick={} generation={} width={} height={} frame={} temporal={} samples={} frame_index={} occlusion={} jitter={:08x},{:08x} aa={} game_time={:08x} real_time={:08x} random={} family_frame={} shader_index={} matrices={:016x} previous={:016x} history_flags={:x} blur_scale={:08x}\n"),
                run_,slot.tick,slot.generation,slot.view.width,slot.view.height,slot.view.frame,slot.view.temporal,slot.view.sample_count,slot.view.frame_index,slot.view.occlusion,slot.view.jitter_x,slot.view.jitter_y,slot.view.aa,slot.view.game_time,slot.view.real_time,slot.view.random,slot.view.family_frame,slot.view.shader_index,slot.view.matrices,slot.view.previous_matrices,slot.view.history_flags,slot.view.motion_blur_scale);
            ++frames_;slot.pending=false;slot.source.Reset();
        }
    }
};
