#ifdef _WIN32
// Actual host destruction predicate, hook-set uninstall and exported deletion
// boundary. Native Stop/detour calls are controlled asynchronous dependencies;
// this tests ownership transfer/retry, not live engine shutdown by itself.
namespace ReplayShutdownTest {
inline unsigned destroyed{},freed_pins{},unhooks{},unsafe_hook_destruction{};
struct Sc6ReplayHost {
    inline static Sc6ReplayHost* active_{};
    enum class ApplicationPhase {Idle,Engine};
    bool registered_{},application_hook_{true},application_active_{true},allow_stop{true},complete{};
    ApplicationPhase application_phase_{ApplicationPhase::Engine};
    Sc6ReplayHost(){active_=this;}
    bool Stop() {
        if(!allow_stop)return false;
        if(complete){active_=nullptr;registered_=false;application_hook_=application_active_=false;application_phase_=ApplicationPhase::Idle;}
        return true; // Accepted application-tail stop can still be pending.
    }
    bool StopForDestruction() noexcept;
};
#include "../HorseMod/horselib/deterministic/Sc6ReplayHost.Destruction.inl"
struct Hook {
    bool hooked{true},allowed{true};
    bool isHooked()const{return hooked;}
    bool unHook(){if(!allowed)return false;hooked=false;++unhooks;return true;}
    ~Hook(){if(hooked)++unsafe_hook_destruction;}
};
struct DeterministicHookSet {
    Sc6ReplayHost replay_host_;
    inline static std::atomic<DeterministicHookSet*> active_{};
    std::atomic_bool installed_{true};std::atomic<unsigned> callbacks_in_flight_{};
    bool iat_ready{true};unsigned clears{};
    std::unique_ptr<Hook> resolved_hit_consumer_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> movevm_write_chara_state_short_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> movevm_execute_bank_slot_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> movevm_transition_author_07_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> movevm_evaluate_if_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> gameplay_xorshift96_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> particle_finished_bind_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> particle_spawn_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> battle_audio_append_parameter_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> battle_audio_stop_all_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> battle_audio_append_command_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> battle_audio_register_voice_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> battle_audio_resolve_chara_cue_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> battle_audio_blueprint_publish_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> battle_audio_tracking_rehash_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> battle_audio_tracking_insert_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> battle_audio_tracking_remove_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> battle_audio_phase_changed_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> battle_audio_contact_handler_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> battle_audio_remap_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> battle_audio_dispatch_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> stage_break_dispatch_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> stage_break_barrier_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> stage_break_wall_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> callback_executor_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> tutorial_tick_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> input_producer_tick_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> input_sample_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> outer_tick_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> replay_post_tick_detour_=std::make_unique<Hook>();
    std::unique_ptr<Hook> frame_fencepost_detour_=std::make_unique<Hook>();
    DeterministicHookSet(){active_=this;}
    bool UninstallUcrtIatHooks(){return iat_ready;}
    void ClearState(){++clears;}
    bool Uninstall() noexcept;
};
#include "../HorseMod/horselib/deterministic/DeterministicHookSet.Uninstall.inl"
struct CppUserModBase {virtual ~CppUserModBase()=default;};
struct HorseMod:CppUserModBase {
    DeterministicHookSet hooks;
    bool PrepareForDestruction(){return hooks.Uninstall();}
    ~HorseMod()override{expect(hooks.Uninstall(),"destructor sees completed shutdown");++destroyed;}
};
inline std::atomic<HorseMod*> g_horse_mod_instance{};
inline std::atomic<HMODULE> g_horse_mod_deferred_unload_pin{};
inline std::atomic_bool g_horse_mod_unload_guard_ready{};
inline BOOL TestFreeLibrary(HMODULE pin){expect(pin==reinterpret_cast<HMODULE>(1),"release retained module pin once");++freed_pins;return TRUE;}
namespace RC {
 enum class LogLevel {Warning};
 struct Output {template<LogLevel,class... A>static void send(A&&...) {}};
}
#pragma push_macro("HORSE_MOD_API")
#pragma push_macro("STR")
#pragma push_macro("FreeLibrary")
#undef HORSE_MOD_API
#undef STR
#define HORSE_MOD_API
#define STR(x) x
#define FreeLibrary TestFreeLibrary
#include "../HorseMod/HorseModService.Uninstall.inl"
#pragma pop_macro("FreeLibrary")
#pragma pop_macro("STR")
#pragma pop_macro("HORSE_MOD_API")
inline void Run() {
    destroyed=freed_pins=unhooks=unsafe_hook_destruction=0;
    auto* mod=new HorseMod;g_horse_mod_instance=mod;
    g_horse_mod_deferred_unload_pin=reinterpret_cast<HMODULE>(1);g_horse_mod_unload_guard_ready=true;
    auto retained=[&]{expect(g_horse_mod_instance==mod && g_horse_mod_deferred_unload_pin && !destroyed && !freed_pins,"rejected shutdown retains entire module/object ownership");};
    mod->hooks.replay_host_.allow_stop=false;uninstall_mod(mod);retained();
    mod->hooks.replay_host_.allow_stop=true;uninstall_mod(mod);retained();
    expect(!unhooks,"accepted pending application tail cannot remove hooks or destroy members");
    mod->hooks.replay_host_.complete=true;mod->hooks.callbacks_in_flight_=1;uninstall_mod(mod);retained();
    expect(!unhooks,"active callbacks defer retirement without spinning on current callback");
    mod->hooks.callbacks_in_flight_=0;mod->hooks.resolved_hit_consumer_detour_->allowed=false;uninstall_mod(mod);retained();
    expect(mod->hooks.installed_ && !mod->hooks.clears && !unhooks,"failed native unhook retains installation and trampoline storage");
    mod->hooks.resolved_hit_consumer_detour_->allowed=true;mod->hooks.iat_ready=false;uninstall_mod(mod);retained();
    expect(unhooks>0 && mod->hooks.installed_ && !mod->hooks.clears,"partial retirement remains retryable with owners intact");
    mod->hooks.iat_ready=true;uninstall_mod(mod);
    expect(destroyed==1 && freed_pins==1 && !g_horse_mod_instance && !g_horse_mod_deferred_unload_pin
        && !unsafe_hook_destruction,"completed retry destroys only detached members and releases its pin");
}
}
namespace ReplayShutdownIatTest {
struct DeterministicHookSet {
    std::uintptr_t srand_iat_slot_{},rand_iat_slot_{},original_srand_{0x1111},original_rand_{0x2222};
    std::array<std::uint32_t,2> iat_retirement_protection_{};
    static void UcrtSrandDetour(){}
    static void UcrtRandDetour(){}
    bool UninstallUcrtIatHooks() noexcept;
};
#include "../HorseMod/horselib/deterministic/DeterministicHookSet.UninstallUcrt.inl"
inline void Run() {
    auto* slots=static_cast<void**>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    expect(slots!=nullptr,"allocate real protected IAT fixture");if(!slots)return;
    DeterministicHookSet h;
    h.srand_iat_slot_=reinterpret_cast<std::uintptr_t>(slots);
    h.rand_iat_slot_=reinterpret_cast<std::uintptr_t>(slots+1);
    slots[0]=reinterpret_cast<void*>(&DeterministicHookSet::UcrtSrandDetour);
    slots[1]=reinterpret_cast<void*>(0x3333); // Another owner, not our hook.
    DWORD old{};expect(VirtualProtect(slots,4096,PAGE_READONLY,&old)!=0,"protect IAT backing");
    expect(!h.UninstallUcrtIatHooks() && !h.srand_iat_slot_ && h.rand_iat_slot_
        && slots[0]==reinterpret_cast<void*>(h.original_srand_) && slots[1]==reinterpret_cast<void*>(0x3333),
        "partial IAT retirement preserves foreign slot and retry lease");
    MEMORY_BASIC_INFORMATION info{};VirtualQuery(slots,&info,sizeof(info));
    expect(info.Protect==PAGE_READONLY && h.iat_retirement_protection_==std::array<std::uint32_t,2>{},"rejected CAS restores original page protection");
    VirtualProtect(slots,4096,PAGE_READWRITE,&old);
    slots[1]=reinterpret_cast<void*>(&DeterministicHookSet::UcrtRandDetour);
    VirtualProtect(slots,4096,PAGE_READONLY,&old);
    expect(h.UninstallUcrtIatHooks() && !h.rand_iat_slot_ && slots[1]==reinterpret_cast<void*>(h.original_rand_),"retry restores remaining owned IAT slot");
    h.rand_iat_slot_=reinterpret_cast<std::uintptr_t>(slots+1);
    h.iat_retirement_protection_[1]=PAGE_READONLY;
    VirtualProtect(slots,4096,PAGE_READWRITE,&old);
    expect(h.UninstallUcrtIatHooks() && !h.rand_iat_slot_ && !h.iat_retirement_protection_[1],"retry finishes pending original-protection journal without repeating publication");
    VirtualQuery(slots,&info,sizeof(info));expect(info.Protect==PAGE_READONLY,"pending retirement restores saved protection");
    h.rand_iat_slot_=1;
    expect(!h.UninstallUcrtIatHooks() && h.rand_iat_slot_==1,"unwritable IAT never discards its ownership lease");
    h.rand_iat_slot_=0;VirtualFree(slots,0,MEM_RELEASE);
}
}
#endif
