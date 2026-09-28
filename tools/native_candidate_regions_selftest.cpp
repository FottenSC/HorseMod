#include "replay_qualification_mod/ReplayStartupLoading.hpp"
#include "replay_qualification_mod/ReplayReceiverObservation.hpp"
#include "deterministic/Sc6ReplaySchedulerState.hpp"
#include "deterministic/ReplayLightingBinding.hpp"
#include "deterministic/CandidateCheckpoint.hpp"
#include "deterministic/Sc6ReplayExecutor.hpp"
#include "deterministic/Sc6ReplayTaskGroup.hpp"
#include "deterministic/ReplaySeekOwnership.hpp"
#include "deterministic/ReplayHudOwnerRoute.hpp"
#include "replay_qualification_mod/ReplayFailureProtocol.hpp"
#include <Unreal/VersionedContainer/Flags412.hpp>
#include "deterministic/BattleAudioSelectorState.hpp"
#include "deterministic/CallbackTopology.hpp"
#include "deterministic/CharaAnimationState.hpp"
#include "deterministic/CandidateGameStateAdapter.hpp"
#include "deterministic/InputTimeline.hpp"
#include "deterministic/NativeCandidateRegions.hpp"
#include "deterministic/HgCpuStream.hpp"
#include "deterministic/HgCpuCoverageProbe.hpp"
#include "deterministic/MoveDispatchState.hpp"
#include "deterministic/MotionBankSnapshot.hpp"
#include "deterministic/PresentationJournal.hpp"
#include "deterministic/Schema.hpp"
#include "deterministic/UcrtRandBroker.hpp"
#include "deterministic/ReplaySparseRegistry.hpp"
#include "deterministic/SecondaryEventState.hpp"
#include "deterministic/SimulationSession.hpp"
#include "deterministic/SnapshotStore.hpp"
#include "deterministic/StageBreakListenerDiagnostics.hpp"
#include "deterministic/StageBreakPresentationIdentity.hpp"
#include "deterministic/StageWindGraphTransaction.hpp"
#include "deterministic/StageWindTopology.hpp"
#ifdef _WIN32
#include "deterministic/ReplayGpuCompletion.hpp"
#include "deterministic/ReplayStaticVectorField.hpp"
#include "deterministic/ReplayPhysicsTaskCompletion.hpp"
#include "deterministic/ReplayPhysicsRemovalGuard.hpp"
#include "deterministic/ReplayPhysicsRemovalOperation.hpp"
#include "deterministic/ReplayGroundBodyAdmission.hpp"
#include "deterministic/ReplayPhysicsBodyInventory.hpp"
#include "deterministic/ReplayGroundPrivateBody.hpp"
#include "deterministic/ReplayGroundInitialBodyState.hpp"
#include "deterministic/ReplayPhysicsPublicationLists.hpp"
#include <thread>
#include "deterministic/ReplayUiTransactionAdmission.hpp"
#include "deterministic/ReplayVfxHandlerStorage.hpp"
#include "deterministic/ReplayVfxExecutionScope.hpp"
#include "GameImGui/ReplayOutputWindow.hpp"
#include "deterministic/Sc6ReplayWorldState.hpp"
#include "deterministic/ReplayPhysicsMarkerGraph.hpp"
#include "deterministic/Sc6ReplayPhysicsMarkers.hpp"
#include "deterministic/Sc6ReplayCpuEmitterState.hpp"
#endif

#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <malloc.h>
#include <span>
#include <unordered_map>
#include <vector>

using namespace Horse::Deterministic;

#ifdef _WIN32
namespace Horse::Deterministic {
struct ReplayTaskGroupTestAccess {
    static Sc6ReplayTaskGroup::DispatchOutcome Dispatch(std::uintptr_t base,void* task,void* thread) {
        Sc6ReplayTaskGroup group;
        group.base_=base;group.named_thread_=static_cast<std::byte*>(thread);
        return group.DispatchTask(task);
    }
    static Sc6ReplayTaskGroup::DispatchOutcome Retain(Sc6ReplayTaskGroup& group,
        std::uintptr_t base,void* task,void* thread,Sc6ReplayTaskGroup::ConsumerAdmissionHooks hooks) {
        group.BindConsumerAdmission(hooks);
        group.base_=base;group.named_thread_=static_cast<std::byte*>(thread);
        const auto result=group.DispatchTask(task);
        group.pumping_=true;
        return result;
    }
    static Sc6ReplayTaskGroup::DispatchOutcome PollRetained(Sc6ReplayTaskGroup& group) {
        group.PumpOne(false);
        return group.dispatch_outcome_;
    }
    static std::uint64_t Dispatched(const Sc6ReplayTaskGroup& group){return group.dispatched_tasks_;}
};
struct ReplayExecutorTestAccess {
    static void Prepare(Sc6ReplayExecutor& e, void* image, void* manager, Sc6ReplayExecutor::Phase phase, unsigned inputs) {
        e.bindings_.image_base=reinterpret_cast<std::uintptr_t>(image);
        e.bindings_.manager=static_cast<std::byte*>(manager);e.bindings_.thread=GetCurrentThreadId();
        e.state_={};e.state_.tick=208;e.state_.phase=phase;e.state_.remaining_inputs=inputs;
        e.state_.cache_offset=static_cast<int>(inputs)-1;e.interval_entry_tick_=208;
    }
};
// Value-only allocation-ownership fixtures. No native functions, allocations,
// object leases or gameplay observations are substituted by these tests.
struct ReplayStageStorageTestAccess {
    static bool Write(std::uintptr_t base,std::span<const ReplayStageVisibility> image) {return Sc6ReplayWorldState::WriteStage(base,image);}
};
struct ReplayVfxMaterialTestAccess {
    static bool CompletionOwnership() {
        Sc6ReplayVfxState image;
        alignas(8) std::array<std::byte,0x68> lifecycle{};
        image.base_=reinterpret_cast<std::uintptr_t>(lifecycle.data())-0x40956c0;

        // Immutable native-layout rows; the checker deliberately controls the
        // independently captured receiver domain, not native object liveness.
        image.valid_=true;image.manager_weak_={4,9};image.components_.resize(1);
        auto& callbacks=image.components_[0].completions;callbacks.count=1;
        std::array<std::int32_t,2> trace{7,11};std::uint64_t manager_name=20,trace_name=21;
        auto row=[](std::byte* dest,const auto& weak,std::uint64_t name) {
            std::memcpy(dest,weak.data(),8);std::memcpy(dest+8,&name,8);
        };
        row(callbacks.entries[0].data(),image.manager_weak_,manager_name);
        image.tables_[0].count=1;image.tables_[0].bytes=std::make_unique<std::byte[]>(48);
        row(image.tables_[0].bytes.get(),trace,trace_name);
        struct Domain {bool reject{};unsigned calls{};} domain;
        auto check=[](const void* opaque,bool manager,const std::array<std::int32_t,2>& weak,std::uint64_t name) {
            auto& d=*const_cast<Domain*>(static_cast<const Domain*>(opaque));++d.calls;
            return !d.reject && (manager ? weak==std::array<std::int32_t,2>{4,9} && name==20
                : weak==std::array<std::int32_t,2>{7,11} && name==21);
        };
        const auto valid=[&]{domain.calls=0;return image.ValidateCompletionOwnership(&domain,check).ok();};
        bool okay=valid() && domain.calls==2;
        domain.reject=true;okay=okay && !valid();domain.reject=false;
        // An identically named delegate on an uncaptured receiver is rejected.
        auto foreign=trace;++foreign[1];row(image.tables_[0].bytes.get(),foreign,trace_name);
        okay=okay && !valid();row(image.tables_[0].bytes.get(),trace,trace_name);
        // Number is part of FName even if ComparisonIndex matches.
        row(callbacks.entries[0].data(),image.manager_weak_,manager_name+(1ull<<32));
        okay=okay && !valid();row(callbacks.entries[0].data(),image.manager_weak_,manager_name);
        row(callbacks.entries[0].data(),trace,manager_name);
        okay=okay && !valid();row(callbacks.entries[0].data(),image.manager_weak_,manager_name);
        callbacks.entries[1]=callbacks.entries[0];callbacks.count=2;okay=okay && !valid();callbacks.count=1;
        row(image.tables_[0].bytes.get()+16,trace,trace_name);
        image.tables_[0].count=2;okay=okay && !valid();image.tables_[0].count=3;okay=okay && !valid();
        image.tables_[0].count=0;callbacks.count=0;okay=okay && valid() && domain.calls==0;
        callbacks.count=17;okay=okay && !valid();callbacks.count=0;
        // Native activation/finalization separately broadcast this global
        // collection. Empty component delegates do not establish its ownership.
        const int active_listener=1;
        std::memcpy(lifecycle.data()+0x50,&active_listener,4);
        okay=okay && !valid();
        std::memset(lifecycle.data()+0x50,0,4);
        std::memcpy(lifecycle.data()+0x64,&active_listener,4);
        okay=okay && !valid();
        std::memset(lifecycle.data()+0x64,0,4);
        okay=okay && valid();
        image.valid_=false;okay=okay && !valid();
        return okay;
    }
    static bool CapturedReferenceInventory() {
        Sc6ReplayVfxState image;
        image.slots_=std::make_unique<Sc6ReplayVfxState::Slot[]>(1);
        image.constructed_=image.capacity_=1;
        // Controlled immutable capture storage, with an intentionally unreadable
        // original address. This fixture grants no native lifetime admission.
        const std::array<std::int32_t,2> identity{17,23},foreign{17,24};
        alignas(8) std::array<std::byte,48> provider{};
        std::memcpy(provider.data()+8,identity.data(),8);
        const auto* data=provider.data();int count=3;
        std::memcpy(image.slots_[0].bytes.data()+0xa0,&data,8);
        std::memcpy(image.slots_[0].bytes.data()+0xb0,&count,4);
        image.components_.resize(2);
        image.components_[0].address=1;image.components_[0].weak=identity;
        image.components_[0].completions.count=1;
        std::memcpy(image.components_[0].completions.entries[0].data(),foreign.data(),8);
        image.components_[1].address=2;image.components_[1].weak=foreign;
        image.components_[1].completions.count=1;
        std::memcpy(image.components_[1].completions.entries[0].data(),identity.data(),8);
        image.tables_[0].count=2;image.tables_[0].bytes=std::make_unique<std::byte[]>(32);
        std::memcpy(image.tables_[0].bytes.get(),identity.data(),8);
        std::memcpy(image.tables_[0].bytes.get()+16,foreign.data(),8);
        Sc6ReplayObjectLease::FailureWitness w{};
        w.original=reinterpret_cast<void*>(1);w.index=17;w.serial=23;
        image.DescribeCapturedReferences(w);
        bool okay=w.captured_reference_inventory_valid && w.captured_provider_users==1
            && w.captured_completion_users==1 && w.captured_own_completions==1
            && w.captured_manager_listeners==2 && w.captured_listener_users==1;
        // A reused address with a different weak serial is not the same owner.
        ++w.serial;image.DescribeCapturedReferences(w);okay=okay && !w.captured_reference_inventory_valid;--w.serial;
        // Malformed metadata must not turn partial counts into a complete view.
        image.components_[1].completions.count=17;
        image.DescribeCapturedReferences(w);okay=okay && !w.captured_reference_inventory_valid;
        image.components_[1].completions.count=1;
        count=0;std::memcpy(image.slots_[0].bytes.data()+0xb0,&count,4);
        image.DescribeCapturedReferences(w);okay=okay && !w.captured_reference_inventory_valid;
        data=nullptr;std::memcpy(image.slots_[0].bytes.data()+0xa0,&data,8);
        image.DescribeCapturedReferences(w);
        okay=okay && w.captured_reference_inventory_valid && !w.captured_provider_users
            && w.captured_completion_users==1 && w.captured_listener_users==1;
        // The provider backing above belongs to the fixture, not native code.
        image.constructed_=0;
        return okay;
    }
    static bool Read(Sc6ReplayVfxState& image,std::uintptr_t base,void* component) {
        image.base_=base;image.components_.clear();image.components_.resize(1);
        auto& binding=image.components_[0];binding.address=reinterpret_cast<std::uintptr_t>(component);
        std::memcpy(binding.weak.data(),&binding.address,8);
        return Sc6ReplayVfxState::ReadComponentValues(base,binding).ok();
    }
    static void FreshValueContext(Sc6ReplayVfxState& image) {
        // Controlled capture context only; production validates the destination
        // and transfers its own captured projection. No native registration granted.
        image.valid_=true;image.thread_=GetCurrentThreadId();
    }
    static bool WriteComponentProjection(Sc6ReplayVfxState& a,Sc6ReplayVfxState& b,void* manager,bool target) {
        // Exercise production value installation/undo. Broader manager/native
        // owner admission is covered separately; this fixture grants neither.
        a.manager_=b.manager_=reinterpret_cast<std::uintptr_t>(manager);
        Sc6ReplayVfxState::PreparedManager prepared;
        prepared.target_=&a;prepared.current_=&b;
        return prepared.Write(target);
    }
    static bool PublicationBindings(const Sc6ReplayVfxState& a,const Sc6ReplayVfxState& b) {
        Sc6ReplayVfxState::PreparedManager prepared;
        prepared.target_=&a;prepared.current_=&b;
        return prepared.ValidateComponents(true,false).ok();
    }
    static bool Retire(const Sc6ReplayVfxState& image,void* component) {
        Sc6ReplayVfxState::ParticleBirth birth;birth.current_=&image;
        birth.component_=reinterpret_cast<std::uintptr_t>(component);
        return birth.ValidateRetirementBody().ok();
    }
};
struct ReplayEmitterStorageTestAccess {
    static void PreparedImage(Sc6ReplayCpuEmitterState::Prepared& p,Sc6ReplayCpuEmitterState& b,
        std::uintptr_t base,void* component,void* lod,void* c,void* original) {
        p.base_=b.base_=base;p.thread_=b.thread_=GetCurrentThreadId();b.valid_=true;
        p.object_=c;p.allocations_.push_back({c,0,0x1d0,nullptr});
        for(auto index:{1,2}) {
            auto& binding=p.bindings_[index];binding.address=reinterpret_cast<std::uintptr_t>(index==1?component:lod);
            std::memcpy(binding.weak.data(),&binding.address,sizeof(binding.address));
        }
        b.bindings_=p.bindings_;std::memcpy(b.object_.data(),original,0x1d0);
    }
    static void GpuBindingImage(Sc6ReplayCpuEmitterState& image,std::uintptr_t base,
        const void* root,std::uintptr_t asset,std::uintptr_t component,std::uintptr_t lod,std::uintptr_t type) {
        image.valid_=true;image.kind_=Sc6ReplayCpuEmitterState::Kind::Gpu;
        image.base_=base;image.thread_=GetCurrentThreadId();
        std::memcpy(image.object_.data(),root,image.object_.size());
        std::memcpy(&image.component_type_,reinterpret_cast<void*>(component),8);
        std::memcpy(&image.component_template_,reinterpret_cast<void*>(component+0x808),8);
        for(auto [index,address]:std::array<std::pair<unsigned,std::uintptr_t>,4>{{{0,asset},{1,component},{2,lod},{5,type}}}) {
            image.bindings_[index].address=address;
            std::memcpy(image.bindings_[index].weak.data(),&address,sizeof(address));
        }
    }
    static void FixtureCleanup(Sc6ReplayCpuEmitterState::Prepared& p) {
        p.ForgetExecutionOwner();p.allocations_.clear();p.published_=false;p.object_=nullptr;
    }
    static void Image(Sc6ReplayCpuEmitterState& image, std::uintptr_t backing,
        std::size_t bytes, bool gpu = false) {
        image.valid_ = true; image.kind_ = gpu ? Sc6ReplayCpuEmitterState::Kind::Gpu : Sc6ReplayCpuEmitterState::Kind::Sprite;
        image.buffers_.resize(1);
        auto& buffer = image.buffers_[0];
        buffer.parent = -1; buffer.offset = 0x80; buffer.bytes.resize(bytes);
        std::memcpy(image.object_.data() + buffer.offset, &backing, sizeof(backing));
    }
    static void MeshImage(Sc6ReplayCpuEmitterState& image) { image.kind_ = Sc6ReplayCpuEmitterState::Kind::Mesh; }
    static bool MeshPayload(const void* image) { return Sc6ReplayCpuEmitterState::MeshPayloadSupported(image); }
    static bool ModuleBindings(const void* image,std::uintptr_t first,std::uintptr_t second) {
        return Sc6ReplayCpuEmitterState::ModuleBindingsMatch(image,first,second);
    }
    static bool InstancePayload(std::uintptr_t base,const void* image,std::uintptr_t module) {
        return Sc6ReplayCpuEmitterState::InstancePayloadMatches(base,image,module);
    }
    static bool SpawnPerUnitPayload(std::uintptr_t base,const void* module,int bytes) {
        return Sc6ReplayCpuEmitterState::SpawnPerUnitPayloadMatches(base,module,bytes);
    }
    static Status CaptureGpu(Sc6ReplayCpuEmitterState& image,std::uintptr_t base,void* component,void* root) {
        image.kind_=Sc6ReplayCpuEmitterState::Kind::Gpu;
        return image.CaptureUnchecked(base,component,root,1024*1024);
    }
    static bool GpuFieldBinding(const void* image,std::uintptr_t asset) {
        return Sc6ReplayCpuEmitterState::GpuFieldBindingMatches(image,asset);
    }
    static bool MeshBindings(const void* image, std::uintptr_t type, std::uintptr_t mesh) {
        return Sc6ReplayCpuEmitterState::MeshBindingsMatch(image, type, mesh);
    }
    static void InvalidParent(Sc6ReplayCpuEmitterState& image) { image.buffers_[0].parent = 0; }
};
}
#endif

namespace
{
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

#ifdef _WIN32
#include "replay_render_reopen_selftest.inl"
#include "replay_publication_tail_selftest.inl"
#include "replay_surface_publication_selftest.inl"
#include "replay_startup_loading_selftest.inl"
#include "replay_surface_image_selftest.inl"
#include "replay_shutdown_selftest.inl"
#include "replay_executor_tail_selftest.inl"
#include "replay_visibility_release_selftest.inl"
#include "replay_physics_publication_lists_selftest.inl"
#endif

class FakeNativeMemory final : public INativeMemory
{
public:
    std::size_t WriteCalls() const noexcept { return write_calls_; }
    explicit FakeNativeMemory(std::uintptr_t base, std::size_t size)
        : base_(base), bytes_(size)
    {
    }

    bool Read(std::uintptr_t address, std::span<std::byte> destination) noexcept override
    {
        const auto offset = resolve(address, destination.size());
        if (offset != invalid)
        {
            std::memcpy(destination.data(), bytes_.data() + offset, destination.size());
            return true;
        }
        for (std::size_t index = 0; index < destination.size(); ++index)
        {
            const auto found = external_bytes_.find(address + index);
            if (found == external_bytes_.end()) return false;
            destination[index] = found->second;
        }
        return true;
    }

    bool Write(std::uintptr_t address, std::span<const std::byte> source) noexcept override
    {
        ++write_calls_;
        if (fail_write_call_ != 0 && (write_calls_ == fail_write_call_
            || (fail_next_write_ && write_calls_ == fail_write_call_ + 1))) {
            if (partial_write_bytes_) {
                const auto prefix = (std::min)(partial_write_bytes_, source.size());
                const auto partial_offset = resolve(address, prefix);
                if (partial_offset != invalid) std::memcpy(bytes_.data() + partial_offset, source.data(), prefix);
            }
            return false;
        }
        const auto offset = resolve(address, source.size());
        if (offset == invalid) return false;
        std::memcpy(bytes_.data() + offset, source.data(), source.size());
        if (corrupt_after_write_call_ == write_calls_)
            bytes_.at(resolve(corrupt_address_, 1)) = corrupt_value_;
        return true;
    }

    template <typename T>
    void Set(std::uintptr_t address, const T& value)
    {
        SetBytes(address, std::as_bytes(std::span{&value, 1}));
    }

    template <typename T>
    void SetExternal(std::uintptr_t address, const T& value)
    {
        const auto bytes = std::as_bytes(std::span{&value, 1});
        for (std::size_t index = 0; index < bytes.size(); ++index)
            external_bytes_[address + index] = bytes[index];
    }

    void SetBytes(std::uintptr_t address, std::span<const std::byte> source)
    {
        const auto offset = resolve(address, source.size());
        if (offset == invalid) throw std::runtime_error("fake memory address out of range");
        std::memcpy(bytes_.data() + offset, source.data(), source.size());
    }

    void Fill(std::uintptr_t address, std::size_t count, std::byte value)
    {
        const auto offset = resolve(address, count);
        if (offset == invalid) throw std::runtime_error("fake memory fill out of range");
        std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), count, value);
    }

    std::byte Get(std::uintptr_t address) const
    {
        return bytes_.at(resolve(address, 1));
    }

    void FailWrite(std::size_t call) noexcept
    {
        write_calls_ = 0;
        fail_write_call_ = call;
        corrupt_after_write_call_ = 0;
        partial_write_bytes_ = 0;
        fail_next_write_ = false;
    }

    void FailPartialWrite(std::size_t call, std::size_t prefix, bool also_next = false) noexcept
    {
        FailWrite(call);
        partial_write_bytes_ = prefix;
        fail_next_write_ = also_next;
    }

    void AllowWrites() noexcept
    {
        write_calls_ = 0;
        fail_write_call_ = 0;
        corrupt_after_write_call_ = 0;
        partial_write_bytes_ = 0;
        fail_next_write_ = false;
    }

    void CorruptAfterWrite(
        std::size_t call, std::uintptr_t address, std::byte value) noexcept
    {
        write_calls_ = 0;
        fail_write_call_ = 0;
        corrupt_after_write_call_ = call;
        corrupt_address_ = address;
        corrupt_value_ = value;
    }

    const std::vector<std::byte>& bytes() const noexcept { return bytes_; }

private:
    std::size_t partial_write_bytes_{};
    bool fail_next_write_{};
    std::size_t resolve(std::uintptr_t address, std::size_t size) const noexcept
    {
        if (address < base_) return invalid;
        const auto offset = static_cast<std::size_t>(address - base_);
        return offset <= bytes_.size() && size <= bytes_.size() - offset
            ? offset : invalid;
    }

    static constexpr std::size_t invalid = static_cast<std::size_t>(-1);
    std::uintptr_t base_{};
    std::vector<std::byte> bytes_;
    std::unordered_map<std::uintptr_t, std::byte> external_bytes_;
    std::size_t write_calls_{};
    std::size_t fail_write_call_{};
    std::size_t corrupt_after_write_call_{};
    std::uintptr_t corrupt_address_{};
    std::byte corrupt_value_{};
};

struct Fixture
{
    static constexpr std::uintptr_t memory_base = 0x10000000;
    static constexpr std::uintptr_t image_base = 0x140000000;

    Fixture()
        : memory(memory_base, 0x160000), regions(memory)
    {
        addresses.image_base = image_base;
        addresses.battle_manager = memory_base + 0x15000;
        addresses.input_log = memory_base + 0x17000;
        addresses.frame_counter = memory_base + 0x1F000;
        addresses.move_dispatch = memory_base + 0x1000;
        addresses.pump_state = memory_base + 0x3000;
        addresses.scheduler_base = memory_base + 0x4000;
        addresses.move_command_base = memory_base + 0x7000;
        addresses.slot_param_base = memory_base + 0xF000;
        addresses.lcg_rng = memory_base + 0x10000;
        addresses.lfsr_rng = memory_base + 0x10100;
        addresses.xorshift_rng = memory_base + 0x10200;
        addresses.wind_rng = memory_base + 0x10300;
        addresses.vm_freeze_record = memory_base + 0x10340;
        addresses.stage_wind_emitter_list = memory_base + 0x10480;
        addresses.pending_hit_record = memory_base + 0x10400;
        addresses.pending_launcher_sync = memory_base + 0x10420;
        addresses.camera_action_backing = memory_base + 0x1D000;
        addresses.fighter_roots = {
            memory_base + 0x12000, memory_base + 0x13000};
        addresses.session_generation = 11;
        addresses.round_generation = 7;
        initialize();
    }

    void initialize()
    {
        const auto previous_inputs = memory_base + 0x1C000;
        const auto input_pairs = memory_base + 0x1C100;
        const auto prior_input_pairs = memory_base + 0x1C200;
        const auto round_sequence = memory_base + 0x1C300;
        memory.Set(addresses.battle_manager + 0x478, addresses.input_log);
        memory.Set(addresses.input_log + 0x10, memory_base + 0x22000);
        memory.Set(addresses.battle_manager + 0x1498, previous_inputs);
        memory.Set(addresses.battle_manager + 0x14A0, std::int32_t{2});
        memory.Set(addresses.battle_manager + 0x14A4, std::int32_t{2});
        memory.Set(addresses.battle_manager + 0x14A8, input_pairs);
        memory.Set(addresses.battle_manager + 0x14B0, std::int32_t{2});
        memory.Set(addresses.battle_manager + 0x14B4, std::int32_t{2});
        memory.Set(addresses.battle_manager + 0x14B8, prior_input_pairs);
        memory.Set(addresses.battle_manager + 0x14C0, std::int32_t{2});
        memory.Set(addresses.battle_manager + 0x14C4, std::int32_t{2});
        memory.Set(addresses.battle_manager + 0x1470, round_sequence);
        memory.Set(addresses.battle_manager + 0x1478, std::int32_t{3});
        memory.Set(addresses.battle_manager + 0x147C, std::int32_t{8});
        memory.Set(addresses.battle_manager + 0x1480, std::uint8_t{2});
        memory.Set(round_sequence, std::array<std::uint8_t, 3>{2, 3, 5});
        memory.Set(addresses.frame_counter, std::uint32_t{42});
        memory.Set(addresses.input_log + 0x3A0, std::int32_t{3});
        memory.Set(addresses.input_log + 0x3A4, std::int32_t{42});
        memory.Set(addresses.input_log + 0x398, std::int32_t{2});
        memory.Set(addresses.input_log + 0x3C0, std::int32_t{3});
        memory.Set(addresses.input_log + 0x3C4, std::uint32_t{42});
        memory.Set(addresses.input_log + 0x3C8, std::uint32_t{0x10});
        memory.Set(addresses.input_log + 0x3CC, std::uint8_t{1});
        memory.Set(addresses.battle_manager + 0x1488, std::int32_t{3});
        memory.Set(addresses.battle_manager + 0x148C, std::uint32_t{42});
        memory.Set(addresses.battle_manager + 0x1490, std::uint32_t{11});
        memory.Set(addresses.battle_manager + 0x14F0, std::int32_t{0});
        memory.Set(addresses.battle_manager + 0x1462, std::uint8_t{0});
        memory.Set(addresses.battle_manager + 0x1463, std::uint8_t{0});
        memory.Set(previous_inputs, std::array<std::uint32_t, 2>{0x10, 0x20});
        memory.Set(input_pairs, std::array<PlayerInput, 2>{{{0x10, 0x10}, {0x20, 0x20}}});
        memory.Set(prior_input_pairs, std::array<PlayerInput, 2>{{{0x08, 0x08}, {0x10, 0x10}}});
        memory.Set(addresses.lcg_rng, std::uint32_t{0x12345678});
        for (std::size_t index = 0; index < 25; ++index)
            memory.Set(addresses.lfsr_rng + index * 4,
                static_cast<std::uint32_t>(0x1000 + index));
        memory.Set(addresses.lfsr_rng + 0x64, std::uint32_t{7});
        for (std::size_t index = 0; index < 3; ++index)
            memory.Set(addresses.xorshift_rng + index * 4,
                static_cast<std::uint32_t>(0x2000 + index));
        for (std::size_t index = 0; index < 6; ++index)
            memory.Set(addresses.wind_rng + index * 4,
                static_cast<std::uint32_t>(0x3000 + index));
        for (std::size_t index = 0; index < 0x40; ++index)
            memory.Set(addresses.vm_freeze_record + index,
                std::byte{static_cast<unsigned char>(0x40 + index)});
        const auto emitter_sentinel = memory_base + 0x10500;
        const auto emitter_node_one = memory_base + 0x10600;
        const auto emitter_node_two = memory_base + 0x10620;
        const auto emitter_one = memory_base + 0x10700;
        const auto emitter_two = memory_base + 0x10800;
        memory.Set(addresses.stage_wind_emitter_list, emitter_sentinel);
        memory.Set(emitter_sentinel, emitter_node_one);
        memory.Set(emitter_sentinel + 8, emitter_node_two);
        memory.Set(emitter_node_one, emitter_node_two);
        memory.Set(emitter_node_one + 8, emitter_sentinel);
        memory.Set(emitter_node_one + 0x10, emitter_one);
        memory.Set(emitter_node_one + 0x18, memory_base + 0x10900);
        memory.Set(emitter_node_two, emitter_sentinel);
        memory.Set(emitter_node_two + 8, emitter_node_one);
        memory.Set(emitter_node_two + 0x10, emitter_two);
        memory.Set(emitter_node_two + 0x18, memory_base + 0x10920);
        memory.Fill(emitter_one, native_stage_wind_emitter_state_size,
            std::byte{0x31});
        memory.Fill(emitter_two, native_stage_wind_emitter_state_size,
            std::byte{0x42});
        memory.Set(addresses.pending_hit_record, std::uint32_t{0x1234});
        memory.Set(addresses.pending_hit_record + 4, 0.25f);
        memory.Set(addresses.pending_hit_record + 8, addresses.fighter_roots[1]);
        memory.Set(addresses.pending_hit_record + 0x10, std::uint32_t{0x400000});
        memory.Set(addresses.pending_launcher_sync, std::uint8_t{1});

        for (std::size_t index = 0; index < native_camera_action_count; ++index)
        {
            const auto action = addresses.camera_action_backing + index * 0x3E0;
            memory.Set(action, image_base + std::uintptr_t{0x3E88000 + index * 8});
        }
        const auto player_watch = addresses.camera_action_backing + 3 * 0x3E0;
        memory.Set(player_watch, image_base + std::uintptr_t{0x3E87EB0});
        for (std::size_t index = 0; index < 16; ++index)
            memory.Set(player_watch + 0x25C + index * sizeof(float),
                static_cast<float>(100 + index));
        memory.Set(player_watch + 0x29C, std::int32_t{11});
        memory.Set(player_watch + 0x2A0, std::uint32_t{7});

        event_masks = memory_base + 0x2000;
        memory.Set(addresses.move_dispatch + 0x470, memory_base + 0x2100);
        memory.Set(addresses.move_dispatch + 0x478, std::int32_t{3});
        memory.Set(addresses.move_dispatch + 0x47C, std::int32_t{2});
        memory.Set(addresses.move_dispatch + 0x480, std::uint8_t{0});
        memory.Set(addresses.move_dispatch + 0x484, std::int32_t{10});
        memory.Set(addresses.move_dispatch + 0x488, std::uint8_t{1});
        memory.Set(addresses.move_dispatch + 0x490, std::uint32_t{0});
        memory.Set(addresses.move_dispatch + 0x494, std::int32_t{4});
        memory.Set(addresses.move_dispatch + 0x498, std::uintptr_t{});
        memory.Set(addresses.move_dispatch + 0x4A0, std::int32_t{0});
        memory.Set(addresses.move_dispatch + 0x4A4, std::int32_t{0});
        memory.Set(addresses.move_dispatch + 0x4A8, event_masks);
        memory.Set(addresses.move_dispatch + 0x4B0, std::int32_t{2});
        memory.Set(addresses.move_dispatch + 0x4B4, std::int32_t{2});
        memory.Set(event_masks, std::uint64_t{0x1111222233334444});
        memory.Set(event_masks + 8, std::uint64_t{0xAAAABBBBCCCCDDDD});

        constexpr std::size_t pump_ids[]{0, 8, 0x10, 0x18, 0x40, 0x48};
        for (std::size_t i = 0; i < std::size(pump_ids); ++i)
            memory.Set(addresses.pump_state + pump_ids[i], memory_base + 0x11000 + i * 0x100);
        memory.Fill(addresses.pump_state + 0x20, 0x1C, std::byte{0x21});
        memory.Fill(addresses.pump_state + 0x50, 0x1C, std::byte{0x22});
        memory.Fill(addresses.pump_state + 0x70, 0x18, std::byte{0});
        memory.Set(addresses.pump_state + 0x70, std::int32_t{2});
        memory.Set(addresses.pump_state + 0x7C, std::uint32_t{1});

        for (std::size_t lane = 0; lane < 2; ++lane)
        {
            const auto scheduler = addresses.scheduler_base + lane * 0x60;
            const auto subvm = memory_base + 0x5000 + lane * 0x1000;
            const auto fighter = memory_base + 0x12000 + lane * 0x1000;
            memory.Set(scheduler, image_base + std::uintptr_t{0x3E80000});
            memory.Set(scheduler + 0x10, fighter);
            memory.Set(scheduler + 0x50, subvm);
            memory.Fill(scheduler + 0x08, 4, std::byte{static_cast<unsigned char>(0x20 + lane)});
            memory.Fill(scheduler + 0x30, 0x20, std::byte{static_cast<unsigned char>(0x24 + lane)});
            memory.Set(scheduler + 0x58, std::uint32_t{static_cast<std::uint32_t>(lane)});
            memory.Set(subvm, image_base + std::uintptr_t{0x3E863D0});
            memory.Set(subvm + 0x10, fighter);
            memory.Set(subvm + 0x18, memory_base + 0x14000 - lane * 0x1000);
            memory.Set(subvm + 0x60, scheduler);
            memory.Fill(subvm + 0x08, 4, std::byte{static_cast<unsigned char>(0x30 + lane)});
            memory.Fill(subvm + 0x20, 0x3C, std::byte{static_cast<unsigned char>(0x40 + lane)});

            const auto command = addresses.move_command_base + lane * 0x3038;
            memory.Fill(command, 0x3038, std::byte{static_cast<unsigned char>(0x50 + lane)});
            constexpr std::size_t ids[]{
                0x0008,0x0010,0x0028,0x0030,0x0340,0x0BA8,0x0BB0,0x0BB8,
                0x0BC0,0x0BC8,0x0BD0,0x0BD8,0x0BE0,0x0CC8,0x0CD8,0x0CE0,0x1998};
            for (std::size_t i = 0; i < std::size(ids); ++i)
                memory.Set(command + ids[i], memory_base + 0x1000 + lane * 0x100 + i * 8);

            memory.Fill(
                addresses.slot_param_base + lane * 0x2C,
                0x2C, std::byte{static_cast<unsigned char>(0x70 + lane)});
        }

        constexpr std::array<std::ptrdiff_t, 2> bank_offsets{0x35A0, 0x27760};
        constexpr std::array<std::size_t, 2> bank_bytes{
            motion_bank_primary_bytes, motion_bank_secondary_bytes};
        std::uintptr_t next_buffer = memory_base + 0xC0000;
        for (std::size_t player = 0; player < 2; ++player)
        {
            for (std::size_t index = 0;
                 index < native_movevm_state_short_count; ++index)
            {
                memory.Set(addresses.fighter_roots[player] + 0x197C
                        + index * sizeof(std::uint16_t),
                    static_cast<std::uint16_t>(
                        0x100 * (player + 1) + index));
            }
            memory.Set(addresses.fighter_roots[player] + 0x42550,
                std::int32_t{768});
            for (std::size_t bank_index = 0; bank_index < 2; ++bank_index)
            {
                const auto bank = addresses.fighter_roots[player]
                    + bank_offsets[bank_index];
                memory.Set(bank, image_base + std::uintptr_t{0x3E90000
                    + player * 0x100 + bank_index * 8});
                std::array<std::uintptr_t, 3> buffers{};
                for (std::size_t slot = 0; slot < 3; ++slot)
                {
                    buffers[slot] = next_buffer;
                    next_buffer += bank_bytes[bank_index];
                    memory.Set(bank + 8 + slot * 8, buffers[slot]);
                    memory.Fill(buffers[slot], bank_bytes[bank_index],
                        std::byte{static_cast<unsigned char>(
                            0x10 + player * 8 + bank_index * 3 + slot)});
                }
                memory.Set(bank + 0x20, std::uint32_t{1});
                memory.Set(bank + 0x28, buffers[1]);
                memory.Set(bank + 0x30, buffers[2]);
            }
            memory.Fill(addresses.fighter_roots[player]
                    + motion_tail_fighter_offset,
                motion_tail_bytes,
                std::byte{static_cast<unsigned char>(0x90 + player)});

            const auto stack = addresses.fighter_roots[player]
                + secondary_event_stack_fighter_offset;
            const auto table = memory_base + 0x40000 + player * 0x3000;
            const auto headers = table + 0x1000;
            const auto payloads = table + 0x2000;
            memory.Set(stack + 0x240, table);
            memory.Set(stack + 0x248, headers);
            memory.Set(stack + 0x250, payloads);
            memory.Set(table + 0x14, std::int32_t{3});
            for (std::size_t slot = 0;
                 slot < secondary_event_slot_count; ++slot)
            {
                const auto address = stack + slot * 0x18;
                memory.Fill(address, 8, std::byte{static_cast<unsigned char>(
                    0x20 + player)});
                memory.Set(address + 8, addresses.fighter_roots[player]);
                memory.Fill(address + 0x10, 8,
                    std::byte{static_cast<unsigned char>(0x40 + slot)});
            }
            memory.Fill(stack + 0x258, 8,
                std::byte{static_cast<unsigned char>(0x60 + player)});
            for (std::size_t index = 0; index < 3; ++index)
                memory.Set(headers + index * 8 + 2,
                    static_cast<std::uint16_t>(10 + player * 3 + index));

            const auto packed = memory_base + 0x90000 + player * 0x10000;
            memory.Set(addresses.fighter_roots[player] + 0x971E8, std::uintptr_t{});
            memory.Set(packed, std::array<std::uint32_t, 5>{
                3, 0, 0, 0x100, 0x200});
            const auto section_table = packed + 0x100;
            memory.Set(section_table, std::array<std::uint32_t, 4>{
                2, 0x40, 0x60, 0x80});
            memory.Fill(section_table + 0x40, 0x40,
                std::byte{static_cast<unsigned char>(0x71 + player)});
            const auto clip = addresses.fighter_roots[player]
                + chara_anim_clip_player_offset;
            memory.Set(addresses.fighter_roots[player]
                    + chara_anim_slot_controller_offset,
                packed);
            memory.Set(clip, addresses.fighter_roots[player]);
            memory.Set(clip + 8, section_table + 0x40);
            memory.Fill(clip + 0x10, 0x20,
                std::byte{static_cast<unsigned char>(0x81 + player)});
            const auto runtime = addresses.fighter_roots[player]
                + chara_anim_runtime_offset;
            memory.Set(runtime, section_table + 0x40);
            memory.Fill(runtime + 8, 8,
                std::byte{static_cast<unsigned char>(0x91 + player)});

            const auto cue_owner = addresses.fighter_roots[player]
                + pose_event_cue_owner_offset;
            const auto scheduler = memory_base + 0x80000 + player * 0x2000;
            const auto head = scheduler + 0x100;
            const auto node_one = scheduler + 0x200;
            const auto node_two = scheduler + 0x220;
            const auto object_one = scheduler + 0x400;
            const auto object_two = scheduler + 0x420;
            memory.Set(cue_owner,
                image_base + std::uintptr_t{0x3EA0000 + player * 8});
            memory.Fill(cue_owner + 8, 0x20,
                std::byte{static_cast<unsigned char>(0xA1 + player)});
            memory.Set(cue_owner + 0x28, packed + 0x300);
            memory.Set(cue_owner + 0x30, scheduler);
            memory.Set(scheduler,
                image_base + std::uintptr_t{0x3EA0100 + player * 8});
            memory.Set(scheduler + 8, addresses.fighter_roots[player]);
            memory.Fill(scheduler + 0x10, 0x5C,
                std::byte{static_cast<unsigned char>(0xB1 + player)});
            memory.Set(scheduler + 0x70, head);
            memory.Set(scheduler + 0x78, std::uint64_t{2});
            memory.Set(head, node_one);
            memory.Set(head + 8, node_two);
            memory.Set(node_one, node_two);
            memory.Set(node_one + 8, head);
            memory.Set(node_one + 0x10, object_one);
            memory.Set(node_one + 0x18, scheduler + 0x600);
            memory.Set(node_two, head);
            memory.Set(node_two + 8, node_one);
            memory.Set(node_two + 0x10, object_two);
            memory.Set(node_two + 0x18, scheduler + 0x620);
            memory.Set(object_one,
                image_base + std::uintptr_t{0x3EA0200 + player * 0x10});
            memory.Set(object_two,
                image_base + std::uintptr_t{0x3EA0208 + player * 0x10});
            memory.Fill(object_one + 8, 0x18,
                std::byte{static_cast<unsigned char>(0xC1 + player)});
            memory.Fill(object_two + 8, 0x18,
                std::byte{static_cast<unsigned char>(0xD1 + player)});
        }
        // The compact fixture overlaps player0's synthetic motion tail with
        // player1's tracker. Initialize the native boolean fields last.
        for (auto root : addresses.fighter_roots)
            memory.Set(root + 0x95774, std::array<std::uint32_t,3>{});
    }

    FakeNativeMemory memory;
    NativeCandidateAddresses addresses{};
    NativeCandidateRegions regions;
    std::uintptr_t event_masks{};
};

bool contains_qword(const std::vector<std::byte>& bytes, std::uintptr_t value)
{
    std::array<std::byte, sizeof(value)> needle{};
    std::memcpy(needle.data(), &value, sizeof(value));
    return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
}

std::array<std::byte, 32> hgcpu_payload{};
bool hgcpu_read_matched = false;
UcrtRandBroker candidate_ucrt_broker{};
const std::uint32_t candidate_thread_id = GetCurrentThreadId();

UcrtRandBrokerImage candidate_ucrt_image(std::uint32_t state = 0x12345678u)
{
    return {
        Schema::Sc6UcrtLayout::algorithm_version,
        Schema::Sc6UcrtLayout::allowlist_version,
        state,
        0,
        true,
        state,
        0x01234500u, // srand argument; native seed may have a zero low nibble.
        0, 0, 0, 1, true,
    };
}

void prepare_candidate_ucrt_broker()
{
    candidate_ucrt_broker.Start();
    expect(candidate_ucrt_broker.BindNative(&std::rand, &std::srand).ok(), "bind candidate native CRT state");
    candidate_ucrt_broker.HandleSrand(candidate_thread_id,
        Schema::Sc6UcrtLayout::rng_init_srand_return_rva,
        0x01234500u, &std::srand);
    candidate_ucrt_broker.AcquireOwnership(candidate_thread_id);
}

void* __fastcall fake_hgcpu_writer(HgCpuStreamShim* shim)
{
    using WriteFn = std::int64_t (__fastcall*)(HgCpuStreamShim*, void*, std::size_t);
    auto** vtable = *reinterpret_cast<void***>(shim);
    auto write = reinterpret_cast<WriteFn>(vtable[5]);
    write(shim, hgcpu_payload.data(), hgcpu_payload.size());
    return shim;
}

void* __fastcall fake_hgcpu_overflow_writer(HgCpuStreamShim* shim)
{
    using WriteFn = std::int64_t (__fastcall*)(HgCpuStreamShim*, void*, std::size_t);
    auto** vtable = *reinterpret_cast<void***>(shim);
    auto write = reinterpret_cast<WriteFn>(vtable[5]);
    std::byte value{};
    write(shim, &value, hgcpu_stream_capacity + 1);
    return shim;
}

void* __fastcall fake_hgcpu_reader(HgCpuStreamShim* shim)
{
    using ReadFn = std::int64_t (__fastcall*)(HgCpuStreamShim*, void*, std::size_t);
    auto** vtable = *reinterpret_cast<void***>(shim);
    auto read = reinterpret_cast<ReadFn>(vtable[6]);
    std::array<std::byte, 32> actual{};
    read(shim, actual.data(), actual.size());
    hgcpu_read_matched = actual == hgcpu_payload;
    hgcpu_payload = actual;
    return shim;
}

bool fail_next_hgcpu_read = false;

void* __fastcall flaky_hgcpu_reader(HgCpuStreamShim* shim)
{
    using ReadFn = std::int64_t (__fastcall*)(HgCpuStreamShim*, void*, std::size_t);
    auto** vtable = *reinterpret_cast<void***>(shim);
    auto read = reinterpret_cast<ReadFn>(vtable[6]);
    if (fail_next_hgcpu_read)
    {
        fail_next_hgcpu_read = false;
        read(shim, hgcpu_payload.data(), hgcpu_payload.size() / 2);
        return shim;
    }
    return fake_hgcpu_reader(shim);
}

void* __fastcall fake_hgcpu_short_reader(HgCpuStreamShim* shim)
{
    using ReadFn = std::int64_t (__fastcall*)(HgCpuStreamShim*, void*, std::size_t);
    auto** vtable = *reinterpret_cast<void***>(shim);
    auto read = reinterpret_cast<ReadFn>(vtable[6]);
    std::array<std::byte, 16> ignored{};
    read(shim, ignored.data(), ignored.size());
    return shim;
}

std::array<std::byte, 32> coverage_source{};

void* __fastcall fake_coverage_writer(HgCpuStreamShim* shim)
{
    using WriteFn = std::int64_t (__fastcall*)(HgCpuStreamShim*, void*, std::size_t);
    auto** vtable = *reinterpret_cast<void***>(shim);
    auto write = reinterpret_cast<WriteFn>(vtable[5]);
    write(shim, coverage_source.data(), coverage_source.size());
    return shim;
}

class DirectNativeMemory final : public INativeMemory
{
public:
    bool Read(std::uintptr_t address, std::span<std::byte> destination) noexcept override
    {
        if (address == 0 || destination.empty()) return false;
        std::memcpy(destination.data(), reinterpret_cast<const void*>(address), destination.size());
        return true;
    }

    bool Write(std::uintptr_t address, std::span<const std::byte> source) noexcept override
    {
        if (address == 0 || source.empty()) return false;
        std::memcpy(reinterpret_cast<void*>(address), source.data(), source.size());
        return true;
    }
};

HgCpuGenerationContext hgcpu_context()
{
    return {0x231, Schema::snapshot_schema_version, 11, 7,
        {101, 102}, 201, 301, 7};
}

Status noop_reconcile(void*, FrameCoordinate) noexcept
{
    return Status::success();
}

std::uintptr_t resolve_test_battle_audio_handler(
    void* user, std::size_t index) noexcept
{
    return user != nullptr && index < maximum_battle_audio_handlers
        ? (*static_cast<std::array<std::uintptr_t,
            maximum_battle_audio_handlers>*>(user))[index] : 0;
}

bool test_battle_audio_handler_overflow(void*) noexcept
{
    return false;
}

void test_hgcpu_stream_contract()
{
    for (std::size_t i = 0; i < hgcpu_payload.size(); ++i)
        hgcpu_payload[i] = std::byte{static_cast<unsigned char>(i + 1)};
    HgCpuStreamShim shim;
    HgCpuLocalImage image{};
    std::array<HgCpuWriteSpan, 4> span_storage{};
    HgCpuWriteTrace trace{span_storage};
    const auto context = hgcpu_context();
    expect(shim.Capture(&fake_hgcpu_writer, context, image, &trace).ok(), "capture bounded HgCpu stream");
    expect(image.cursor == hgcpu_payload.size(), "record exact HgCpu cursor");
    expect(image.bytes == std::vector<std::byte>(
        hgcpu_payload.begin(), hgcpu_payload.end()), "capture exact HgCpu bytes");
    expect(trace.count == 1 && !trace.truncated, "record exact HgCpu write span count");
    expect(trace.storage[0].source_address == reinterpret_cast<std::uintptr_t>(hgcpu_payload.data())
        && trace.storage[0].stream_offset == 0
        && trace.storage[0].size == hgcpu_payload.size(), "record local-only HgCpu source mapping");
    hgcpu_read_matched = false;
    expect(shim.Restore(&fake_hgcpu_reader, context, image).ok(), "restore bounded HgCpu stream");
    expect(hgcpu_read_matched, "HgCpu reader receives exact stream");

    auto wrong_generation = context;
    ++wrong_generation.round_generation;
    expect(
        shim.Restore(&fake_hgcpu_reader, wrong_generation, image).code
            == FailureCode::RestorePreflightFailed,
        "HgCpu generation mismatch fails before native reader");
    auto wrong_allocation = context;
    ++wrong_allocation.allocation_generation;
    expect(
        shim.Restore(&fake_hgcpu_reader, wrong_allocation, image).code
            == FailureCode::RestorePreflightFailed,
        "local reconstruction allocation generation mismatch fails preflight");
    auto wrong_serializer = image;
    ++wrong_serializer.serializer_version;
    expect(
        shim.Restore(&fake_hgcpu_reader, context, wrong_serializer).code
            == FailureCode::RestorePreflightFailed,
        "local reconstruction serializer version mismatch fails preflight");
    expect(
        shim.Restore(&fake_hgcpu_short_reader, context, image).code
            == FailureCode::RestoreVerificationFailed,
        "HgCpu cursor mismatch fails verification");
    ++image.checksum;
    expect(
        shim.Restore(&fake_hgcpu_reader, context, image).code
            == FailureCode::RestorePreflightFailed,
        "HgCpu checksum mismatch fails preflight");

    HgCpuLocalImage overflow{};
    expect(
        shim.Capture(&fake_hgcpu_overflow_writer, context, overflow).code
            == FailureCode::CapacityExceeded,
        "HgCpu stream rejects native overflow");
}

std::array<std::array<std::byte,0x35a0>,2> stat_test_fighters{};
bool stat_short_transfer{};
bool stat_stop_after_first{};
void* __fastcall stat_test_transfer(HgCpuStreamShim* shim, unsigned slot)
{
    using Transfer = std::int64_t (__fastcall*)(HgCpuStreamShim*,void*,std::size_t);
    auto transfer=reinterpret_cast<Transfer>((*reinterpret_cast<void***>(shim))[slot]);
    for(unsigned i=0;i<2;++i) {
        transfer(shim,stat_test_fighters[i].data()+0x90,stat_short_transfer?0x3508:0x3510);
        if(stat_stop_after_first)break;
    }
    return shim;
}
void* __fastcall stat_test_writer(HgCpuStreamShim* shim){return stat_test_transfer(shim,5);}
void* __fastcall stat_test_reader(HgCpuStreamShim* shim){return stat_test_transfer(shim,6);}

void test_hgcpu_empty_stat_owner()
{
    struct EmptyTree {
        std::array<std::uintptr_t,7> header{};
        std::array<std::uintptr_t,2> owner{};
        void init() {
            const auto h=reinterpret_cast<std::uintptr_t>(header.data());
            header={h,h,h,0x101,0,0,0};owner={h,0};
        }
    };
    auto old=std::make_unique<std::array<EmptyTree,2>>();
    std::array<EmptyTree,2> current{};
    HgCpuStreamShim shim;
    std::array<std::uintptr_t,2> fighters{};
    auto bind=[&](auto& trees,std::byte value) {
        for(unsigned i=0;i<2;++i) {
            trees[i].init();stat_test_fighters[i].fill(value);
            const auto p=reinterpret_cast<std::uintptr_t>(trees[i].owner.data());
            std::memcpy(stat_test_fighters[i].data()+0x3590,&p,8);
            fighters[i]=reinterpret_cast<std::uintptr_t>(stat_test_fighters[i].data());
        }
        shim.BindStatOwners(fighters);
    };
    bind(*old,std::byte{0x31});
    HgCpuLocalImage a{},b{},again{};
    expect(shim.Capture(stat_test_writer,hgcpu_context(),a).ok(),"capture verified empty stat trees");
    for(unsigned i=0;i<2;++i) {
        std::uintptr_t token=1;std::memcpy(&token,a.bytes.data()+i*0x3510+0x3500,8);
        expect(token==0,"empty stat image contains no historical heap address");
    }
    bind(current,std::byte{0x72});old.reset();
    const auto original_b=stat_test_fighters;
    expect(shim.Capture(stat_test_writer,hgcpu_context(),b).ok(),"capture complete B with replacement trees");
    expect(shim.Restore(stat_test_reader,hgcpu_context(),a).ok(),"restore A after old tree allocations retire");
    expect(shim.Capture(stat_test_writer,hgcpu_context(),again).ok() && again.bytes==a.bytes,
        "restored A recaptures canonically with current native tree bindings");
    expect(shim.Restore(stat_test_reader,hgcpu_context(),b).ok() && stat_test_fighters==original_b,
        "B undo retains its exact live tree binding and surrounding state");
    current[0].owner[1]=1;
    expect(!shim.Restore(stat_test_reader,hgcpu_context(),a).ok() && stat_test_fighters==original_b,
        "nonempty current stat tree rejects before native writes");
    current[0].owner[1]=0;current[0].header[0]=0;
    expect(!shim.Restore(stat_test_reader,hgcpu_context(),a).ok() && stat_test_fighters==original_b,
        "malformed empty sentinel rejects before native writes");
    current[0].init();stat_short_transfer=true;
    expect(!shim.Restore(stat_test_reader,hgcpu_context(),a).ok() && stat_test_fighters==original_b,
        "unexpected native bulk transfer cannot overwrite a stat owner");
    stat_short_transfer=false;stat_stop_after_first=true;
    expect(!shim.Restore(stat_test_reader,hgcpu_context(),a).ok(),"interrupted two-fighter publication rejects");
    stat_stop_after_first=false;
    expect(shim.Restore(stat_test_reader,hgcpu_context(),b).ok() && stat_test_fighters==original_b,
        "complete B undo recovers interrupted fighter publication");
}

void test_candidate_checkpoint_codec()
{
    Fixture fixture;
    expect(fixture.regions.Bind(fixture.addresses).ok(),
        "bind checkpoint candidate regions");
    auto native_storage = std::make_unique<NativeCandidateImage>();
    auto& native = *native_storage;
    expect(fixture.regions.Capture(native).ok(),
        "capture checkpoint candidate regions");

    for (std::size_t i = 0; i < hgcpu_payload.size(); ++i)
        hgcpu_payload[i] = std::byte{static_cast<unsigned char>(0x80 + i)};
    HgCpuStreamShim shim;
    HgCpuLocalImage hgcpu{};
    expect(shim.Capture(&fake_hgcpu_writer, hgcpu_context(), hgcpu).ok(),
        "capture checkpoint HgCpu image");

    auto prepared_storage = std::make_unique<CandidateCheckpointImage>();
    auto& prepared = *prepared_storage;
    expect(PrepareCandidateCheckpointStorage(prepared, true).ok(),
        "prepare bounded checkpoint scratch storage");
    const auto prepared_capacity =
        CandidateCheckpointDynamicCapacity(prepared);
    prepared.native.stage_wind_emitters.states.resize(
        native_stage_wind_emitter_max_count);
    prepared.move_dispatch.sub_elements.resize(1024);
    prepared.local_images[0].bytes.resize(hgcpu_stream_capacity);
    prepared.local_images[1].bytes.resize(motion_bank_image_bytes);
    prepared.wind.nodes.resize(stage_wind_max_nodes);
    expect(CandidateCheckpointDynamicCapacity(prepared) == prepared_capacity,
        "bounded checkpoint scratch does not grow at maximum sizes");

    auto image_storage = std::make_unique<CandidateCheckpointImage>();
    auto& image = *image_storage;
    image.native = native;
    image.battle_audio_selector.session_generation = native.session_generation;
    image.battle_audio_selector.round_generation = native.round_generation;
    image.battle_audio_selector.alternations[0] = 1;
    image.battle_audio_selector.observed_count = 1;
    image.move_dispatch.generation = native.round_generation;
    image.move_dispatch.phase = MoveDispatchActionModeState{};
    image.local_images.push_back(hgcpu);
    MotionBankSnapshot motion{fixture.memory};
    expect(motion.Bind(fixture.addresses.fighter_roots, hgcpu_context()).ok(),
        "bind checkpoint matrix-bank snapshot");
    LocalReconstructionImage motion_image{};
    expect(motion.Capture(motion_image).ok(),
        "capture checkpoint matrix-bank image");
    image.local_images.push_back(motion_image);
    SecondaryEventState secondary{fixture.memory};
    expect(secondary.Bind(fixture.addresses.fighter_roots, 7).ok(),
        "bind checkpoint secondary-event state");
    expect(secondary.Capture(image.secondary_events).ok(),
        "capture checkpoint secondary-event state");
    CharaAnimationState animation{fixture.memory};
    expect(animation.Bind(fixture.addresses.fighter_roots, 7).ok(),
        "bind checkpoint character-animation state");
    expect(animation.Capture(image.chara_animation).ok(),
        "capture checkpoint character-animation state");
    image.ucrt = candidate_ucrt_image();
    image.wind.generation = native.round_generation;
    image.wind.root_clock[0] = std::byte{0x31};
    image.wind.schedule_params[15] = std::byte{0x7A};
    const auto* ring_in_layout = FindStageWindNodeLayout(StageWindNodeKind::RingIn);
    StageWindNodeImage ring_in{};
    ring_in.kind = StageWindNodeKind::RingIn;
    ring_in.semantic_state.assign(
        StageWindSemanticStateSize(*ring_in_layout), std::byte{0xCC});
    ring_in.derived_state.assign(
        StageWindDerivedStateSize(*ring_in_layout), std::byte{0xDD});
    image.wind.nodes.push_back(std::move(ring_in));
    Snapshot snapshot{};
    expect(CandidateCheckpointCodec::Encode({7, 30}, 0x9191, image, snapshot).ok(),
        "encode pointer-free candidate checkpoint");
    expect(!snapshot.bytes.empty()
            && snapshot.canonical_hash != CanonicalHash{},
        "checkpoint contains versioned payload and canonical component hash");
    expect(snapshot.canonical_wind[18] != 0
            && snapshot.canonical_wind[19] != 0,
        "checkpoint exposes root-clock and schedule-parameter wind diagnostics");
    Snapshot captured_snapshot{};
    auto captured_image_storage = std::make_unique<CandidateCheckpointImage>(image);
    auto& captured_image = *captured_image_storage;
    expect(CandidateCheckpointCodec::EncodeCaptured(
            {7, 30}, 0x9191, captured_image, captured_snapshot).ok()
            && captured_snapshot.local_images.size() == 2
            && captured_snapshot.canonical_hash == snapshot.canonical_hash,
        "fresh-capture encoding externalizes local images without changing canonical truth");

    auto local_staging_image = std::make_unique<CandidateCheckpointImage>(image);
    local_staging_image->native.input_log.scalars[0x0c] ^= std::byte{3};
    local_staging_image->native.input_log.scalars[0x1c] ^= std::byte{7};
    local_staging_image->native.input_log.scalars[0x28] ^= std::byte{0x40};
    local_staging_image->native.input_log.scalars[0x2c] ^= std::byte{0x20};
    local_staging_image->native.input_log.cache_rows[123].input_value ^= 0x4000;
    if (local_staging_image->native.camera_components[0].present != 0)
        local_staging_image->native.camera_components[0].common[0]
            ^= std::byte{0x33};
    Snapshot local_staging_snapshot{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, *local_staging_image,
            local_staging_snapshot).ok()
            && local_staging_snapshot.canonical_hash == snapshot.canonical_hash
            && local_staging_snapshot.bytes != snapshot.bytes,
        "peer identity excludes proven input-transport and camera-presentation local state");

    auto deterministic_input_log_image =
        std::make_unique<CandidateCheckpointImage>(*local_staging_image);
    deterministic_input_log_image->native.input_log.scalars[0x04]
        ^= std::byte{1};
    Snapshot deterministic_input_log_snapshot{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, *deterministic_input_log_image,
            deterministic_input_log_snapshot).ok()
            && deterministic_input_log_snapshot.canonical_hash
                != snapshot.canonical_hash,
        "peer identity retains deterministic FrameInputLog fields");

    constexpr std::size_t scheduler_active_offset = 0x64 - 0x10;
    auto inactive_scheduler_a =
        std::make_unique<CandidateCheckpointImage>(*local_staging_image);
    std::fill_n(inactive_scheduler_a->chara_animation.players[0]
            .scheduler_scalars.begin() + scheduler_active_offset,
        sizeof(std::uint32_t), std::byte{});
    inactive_scheduler_a->chara_animation.players[0]
        .scheduler_chara_bound = false;
    Snapshot inactive_scheduler_snapshot_a{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, *inactive_scheduler_a,
            inactive_scheduler_snapshot_a).ok(),
        "encode inactive scheduler peer baseline");
    auto inactive_scheduler_b =
        std::make_unique<CandidateCheckpointImage>(*inactive_scheduler_a);
    inactive_scheduler_b->chara_animation.players[0]
        .scheduler_scalars[0] ^= std::byte{0x21};
    inactive_scheduler_b->chara_animation.players[0]
        .scheduler_chara_bound = true;
    Snapshot inactive_scheduler_snapshot_b{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, *inactive_scheduler_b,
            inactive_scheduler_snapshot_b).ok()
            && inactive_scheduler_snapshot_b.canonical_hash
                == inactive_scheduler_snapshot_a.canonical_hash
            && inactive_scheduler_snapshot_b.bytes
                != inactive_scheduler_snapshot_a.bytes,
        "inactive scheduler residue and pChara binding are local-only");

    auto active_scheduler_a =
        std::make_unique<CandidateCheckpointImage>(*inactive_scheduler_a);
    const std::uint32_t active = 1;
    std::memcpy(active_scheduler_a->chara_animation.players[0]
            .scheduler_scalars.data() + scheduler_active_offset,
        &active, sizeof(active));
    active_scheduler_a->chara_animation.players[0]
        .scheduler_chara_bound = true;
    Snapshot active_scheduler_snapshot_a{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, *active_scheduler_a,
            active_scheduler_snapshot_a).ok(),
        "encode active scheduler peer baseline");
    auto active_scheduler_b =
        std::make_unique<CandidateCheckpointImage>(*active_scheduler_a);
    active_scheduler_b->chara_animation.players[0]
        .scheduler_scalars[0] ^= std::byte{0x21};
    Snapshot active_scheduler_snapshot_b{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, *active_scheduler_b,
            active_scheduler_snapshot_b).ok()
            && active_scheduler_snapshot_b.canonical_hash
                != active_scheduler_snapshot_a.canonical_hash,
        "active scheduler fields remain peer-canonical");

    auto inert_emitter_a =
        std::make_unique<CandidateCheckpointImage>(*local_staging_image);
    if (inert_emitter_a->native.stage_wind_emitters.states.empty())
        inert_emitter_a->native.stage_wind_emitters.states.push_back({});
    Snapshot inert_emitter_snapshot_a{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, *inert_emitter_a,
            inert_emitter_snapshot_a).ok(),
        "encode stage-emitter peer baseline");
    auto inert_emitter_b =
        std::make_unique<CandidateCheckpointImage>(*inert_emitter_a);
    inert_emitter_b->native.stage_wind_emitters.states[0][0x1C]
        ^= std::byte{0x33};
    Snapshot inert_emitter_snapshot_b{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, *inert_emitter_b,
            inert_emitter_snapshot_b).ok()
            && inert_emitter_snapshot_b.canonical_hash
                == inert_emitter_snapshot_a.canonical_hash
            && inert_emitter_snapshot_b.bytes != inert_emitter_snapshot_a.bytes,
        "unused stage-emitter Euler-W lane is local-only");
    inert_emitter_b->native.stage_wind_emitters.states[0][0x18]
        ^= std::byte{0x44};
    Snapshot semantic_emitter_snapshot{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, *inert_emitter_b,
            semantic_emitter_snapshot).ok()
            && semantic_emitter_snapshot.canonical_hash
                != inert_emitter_snapshot_a.canonical_hash,
        "consumed stage-emitter Euler lanes remain peer-canonical");

    auto deterministic_state_image =
        std::make_unique<CandidateCheckpointImage>(*local_staging_image);
    deterministic_state_image->chara_animation.players[0].scheduler_scalars[0]
        ^= std::byte{0x21};
    if (deterministic_state_image->native.stage_wind_emitters.states.empty())
        deterministic_state_image->native.stage_wind_emitters.states.push_back({});
    deterministic_state_image->native.stage_wind_emitters.states[0][0]
        ^= std::byte{0x17};
    Snapshot deterministic_state_snapshot{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, *deterministic_state_image,
            deterministic_state_snapshot).ok()
            && deterministic_state_snapshot.canonical_hash
                != snapshot.canonical_hash,
        "peer identity retains deterministic animation and stage-emitter state");
    auto canonical_image_storage = std::make_unique<CandidateCheckpointImage>(image);
    auto& canonical_image = *canonical_image_storage;
    canonical_image.local_images.clear();
    Snapshot canonical_snapshot{};
    expect(CandidateCheckpointCodec::EncodeCanonical(
            {7, 30}, 0x9191, canonical_image, canonical_snapshot).ok()
            && canonical_snapshot.bytes.empty()
            && canonical_snapshot.local_images.empty()
            && canonical_snapshot.canonical_hash == snapshot.canonical_hash
            && canonical_snapshot.canonical_components
                == snapshot.canonical_components
            && canonical_snapshot.canonical_native == snapshot.canonical_native
            && canonical_snapshot.canonical_move_dispatch
                == snapshot.canonical_move_dispatch
            && canonical_snapshot.canonical_input == snapshot.canonical_input
            && canonical_snapshot.canonical_wind_semantic
                == snapshot.canonical_wind_semantic
            && canonical_snapshot.canonical_wind == snapshot.canonical_wind
            && canonical_snapshot.canonical_wind_node
                == snapshot.canonical_wind_node,
        "canonical-only encoding preserves exact identity without restore payloads");
    Snapshot relocated_context_snapshot{};
    expect(CandidateCheckpointCodec::EncodeCanonical(
            {7, 30}, 0xA2A2, canonical_image,
            relocated_context_snapshot).ok()
            && relocated_context_snapshot.context_identity == 0xA2A2
            && relocated_context_snapshot.canonical_hash
                == canonical_snapshot.canonical_hash
            && relocated_context_snapshot.canonical_components
                == canonical_snapshot.canonical_components,
        "portable canonical identity excludes the process-local battle-manager address");
    auto rebound_session_image_storage = std::make_unique<CandidateCheckpointImage>(canonical_image);
    auto& rebound_session_image = *rebound_session_image_storage;
    ++rebound_session_image.native.session_generation;
    rebound_session_image.battle_audio_selector.session_generation =
        rebound_session_image.native.session_generation;
    Snapshot rebound_session_snapshot{};
    std::vector<std::byte> original_native_bytes;
    std::vector<std::byte> rebound_native_bytes;
    NativeCandidateRegions::CanonicalBytes(
        canonical_image.native, original_native_bytes);
    NativeCandidateRegions::CanonicalBytes(
        rebound_session_image.native, rebound_native_bytes);
    expect(CandidateCheckpointCodec::EncodeCanonical(
            {7, 30}, 0x9191, rebound_session_image,
            rebound_session_snapshot).ok()
            && rebound_session_snapshot.canonical_hash
                == canonical_snapshot.canonical_hash
            && rebound_session_snapshot.canonical_components
                == canonical_snapshot.canonical_components
            && rebound_session_snapshot.canonical_native[31]
                != canonical_snapshot.canonical_native[31]
            && original_native_bytes != rebound_native_bytes,
        "peer identity excludes process-local session generations while full restore images retain them");
    canonical_image.wind.nodes.clear();
    Snapshot fresh_without_wind_node{};
    expect(CandidateCheckpointCodec::EncodeCanonical(
            {7, 31}, 0x9191, canonical_image, fresh_without_wind_node).ok()
            && CandidateCheckpointCodec::EncodeCanonical(
                {7, 31}, 0x9191, canonical_image, canonical_snapshot).ok()
            && canonical_snapshot.canonical_hash
                == fresh_without_wind_node.canonical_hash
            && canonical_snapshot.canonical_wind
                == fresh_without_wind_node.canonical_wind
            && canonical_snapshot.canonical_wind_semantic
                == fresh_without_wind_node.canonical_wind_semantic
            && canonical_snapshot.canonical_wind_node
                == fresh_without_wind_node.canonical_wind_node,
        "reused canonical output clears variable wind diagnostic tails");
    auto decoded_captured_storage = std::make_unique<CandidateCheckpointImage>();
    auto& decoded_captured = *decoded_captured_storage;
    expect(CandidateCheckpointCodec::Decode(
            captured_snapshot, decoded_captured).ok()
            && decoded_captured.local_images.size() == 2
            && decoded_captured.local_images[0].bytes == hgcpu.bytes,
        "decode validates and rejoins attached local reconstruction images");

    auto decoded_storage = std::make_unique<CandidateCheckpointImage>();
    auto& decoded = *decoded_storage;
    expect(CandidateCheckpointCodec::Decode(snapshot, decoded).ok(),
        "decode candidate checkpoint");
    expect(decoded.native == native,
        "candidate checkpoint round-trips typed native image");
    {
        auto camera_image = std::make_unique<CandidateCheckpointImage>(image);
        auto& camera = camera_image->native.camera_components[0];
        camera.present = 1;
        camera.serialization = NativeCameraComponentSerialization::Base;
        camera.vtable_rva = 0x3E862D0;
        camera.writer_rva = 0x321000;
        camera.common.fill(std::byte{0x5A});
        camera.derived[0] = std::byte{0xA5};
        camera.derived_size = 1;
        camera.tracked_chara_slot = 1;
        camera.state_buffer_chara_slots = {0, 1};
        camera_image->native.camera_components[2] = {};
        Snapshot camera_snapshot;
        expect(CandidateCheckpointCodec::Encode({7, 30}, 0x9191,
                *camera_image, camera_snapshot).ok(), "encode complete local camera payload");
        auto camera_decoded = std::make_unique<CandidateCheckpointImage>();
        expect(CandidateCheckpointCodec::Decode(camera_snapshot, *camera_decoded).ok()
                && camera_decoded->native.camera_components == camera_image->native.camera_components,
            "local camera codec preserves every field and absent publication slots");
        expect(camera_snapshot.canonical_hash == snapshot.canonical_hash,
            "local camera payload does not silently redefine peer fingerprint");
        const auto difference = std::mismatch(snapshot.bytes.begin(), snapshot.bytes.end(),
            camera_snapshot.bytes.begin(), camera_snapshot.bytes.end());
        expect(difference.second != camera_snapshot.bytes.end(), "camera payload changes restorable bytes");
        if (difference.second != camera_snapshot.bytes.end()) {
            *difference.second ^= std::byte{1};
            expect(!CandidateCheckpointCodec::Decode(camera_snapshot, *camera_decoded).ok(),
                "corrupted local camera payload rejects before native publication");
        }
    }
    expect(decoded.battle_audio_selector == image.battle_audio_selector,
        "candidate checkpoint round-trips local battle-audio selector state");
    expect(decoded.move_dispatch == image.move_dispatch,
        "candidate checkpoint round-trips MoveDispatch semantic state");
    expect(decoded.local_images.size() == 2
            && decoded.local_images.front().serializer_id
                == LocalSerializerId::HgCpuDirect
            && decoded.local_images.front().serializer_version
                == hgcpu_direct_serializer_version
            && decoded.local_images.front().context == hgcpu.context
            && decoded.local_images.front().cursor == hgcpu.cursor
            && decoded.local_images.front().checksum == hgcpu.checksum
            && decoded.local_images.front().bytes == hgcpu.bytes
            && decoded.local_images[1].serializer_id
                == LocalSerializerId::MotionBankTriples
            && decoded.local_images[1].bytes == motion_image.bytes,
        "candidate checkpoint round-trips ordered local reconstruction images");
    expect(decoded.ucrt == image.ucrt,
        "candidate checkpoint round-trips both CRT lanes and initialization epoch");
    {
        auto changed = std::make_unique<CandidateCheckpointImage>(image);
        Snapshot encoded{};
        for (unsigned field = 0; field < 8; ++field)
        {
            changed->ucrt = image.ucrt;
            switch (field) {
            case 0: ++changed->ucrt.state; break;
            case 1: ++changed->ucrt.combat_state; break;
            case 2: ++changed->ucrt.draws; break;
            case 3: ++changed->ucrt.native_draws; break;
            case 4: ++changed->ucrt.native_draws; ++changed->ucrt.unknown_draws; break;
            case 5: ++changed->ucrt.epoch; break;
            case 6: ++changed->ucrt.warmup_draws; break;
            case 7: changed->ucrt.seed_state ^= 0x100u; break;
            }
            expect(CandidateCheckpointCodec::Encode({7, 30}, 0x9191, *changed, encoded).ok()
                    && encoded.canonical_hash != snapshot.canonical_hash,
                "both CRT cursors, omitted/extra draws and epoch are canonical");
            auto roundtrip = std::make_unique<CandidateCheckpointImage>();
            expect(CandidateCheckpointCodec::Decode(encoded, *roundtrip).ok()
                    && roundtrip->ucrt == changed->ucrt,
                "codec retains independent CRT fields without padding or native pointers");
        }
        for (unsigned invalid = 0; invalid < 5; ++invalid)
        {
            changed->ucrt = image.ucrt;
            switch (invalid) {
            case 0: --changed->ucrt.algorithm_version; break;
            case 1: changed->ucrt.combat_ready = false; break;
            case 2: changed->ucrt.epoch = 0; break;
            case 3: changed->ucrt.warmup_draws = 0x1000; break;
            case 4: changed->ucrt.unknown_draws = changed->ucrt.native_draws + 1; break;
            }
            expect(!CandidateCheckpointCodec::Encode({7, 30}, 0x9191, *changed, encoded).ok(),
                "codec rejects obsolete or incomplete CRT lane images");
        }
    }
    expect(decoded.wind == image.wind,
        "candidate checkpoint round-trips pointer-free wind state");
    const auto* local_storage = decoded.local_images.data();
    const auto* hgcpu_storage = decoded.local_images[0].bytes.data();
    const auto* motion_storage = decoded.local_images[1].bytes.data();
    const auto* wind_node_storage = decoded.wind.nodes.data();
    const auto* wind_semantic_storage =
        decoded.wind.nodes[0].semantic_state.data();
    const auto* wind_derived_storage =
        decoded.wind.nodes[0].derived_state.data();
    const auto* emitter_storage =
        decoded.native.stage_wind_emitters.states.data();
    expect(CandidateCheckpointCodec::Decode(snapshot, decoded).ok()
            && decoded.local_images.data() == local_storage
            && decoded.local_images[0].bytes.data() == hgcpu_storage
            && decoded.local_images[1].bytes.data() == motion_storage
            && decoded.wind.nodes.data() == wind_node_storage
            && decoded.wind.nodes[0].semantic_state.data()
                == wind_semantic_storage
            && decoded.wind.nodes[0].derived_state.data()
                == wind_derived_storage
            && decoded.native.stage_wind_emitters.states.data()
                == emitter_storage,
        "repeated checkpoint decode reuses all bounded variable storage");

    auto changed_movevm_state_storage = std::make_unique<CandidateCheckpointImage>(image);
    auto& changed_movevm_state = *changed_movevm_state_storage;
    changed_movevm_state.native.movevm_state_shorts.fighters[0][25] ^= 1;
    Snapshot changed_movevm_state_snapshot{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, changed_movevm_state,
            changed_movevm_state_snapshot).ok()
            && changed_movevm_state_snapshot.canonical_hash
                != snapshot.canonical_hash
            && changed_movevm_state_snapshot.canonical_native[30]
                != snapshot.canonical_native[30],
        "per-fighter MoveVM state shorts, including Tira behavior slot 25, are canonical truth");

    auto presentation_audio_storage = std::make_unique<CandidateCheckpointImage>(image);
    auto& presentation_audio = *presentation_audio_storage;
    presentation_audio.battle_audio_selector.alternations[0] = 0;
    Snapshot presentation_audio_snapshot{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, presentation_audio,
            presentation_audio_snapshot).ok()
            && presentation_audio_snapshot.canonical_hash
                == snapshot.canonical_hash
            && presentation_audio_snapshot.bytes != snapshot.bytes,
        "battle-audio selector remains local-restorable but excluded from canonical peer truth");

    auto presentation_wind_storage = std::make_unique<CandidateCheckpointImage>(image);
    auto& presentation_wind = *presentation_wind_storage;
    presentation_wind.wind.nodes.front().derived_state.front() ^= std::byte{1};
    presentation_wind.wind.output_force.front() ^= std::byte{1};
    Snapshot presentation_snapshot{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, presentation_wind, presentation_snapshot).ok()
            && presentation_snapshot.canonical_hash == snapshot.canonical_hash
            && presentation_snapshot.bytes != snapshot.bytes,
        "node and root wind sampled/output force remain local-restorable but are excluded "
        "from canonical peer truth");

    auto duplicate_local_storage = std::make_unique<CandidateCheckpointImage>(image);
    auto& duplicate_local = *duplicate_local_storage;
    duplicate_local.local_images[1] = hgcpu;
    Snapshot rejected_duplicate{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, duplicate_local, rejected_duplicate).code
            == FailureCode::IdentityMismatch,
        "checkpoint rejects an unsupported duplicate local serializer");

    Snapshot corrupted_wind = snapshot;
    const std::array derived_marker{
        std::byte{0xDD}, std::byte{0xDD}, std::byte{0xDD}, std::byte{0xDD},
        std::byte{0xDD}, std::byte{0xDD}, std::byte{0xDD}, std::byte{0xDD}};
    const auto marker = std::search(corrupted_wind.bytes.begin(),
        corrupted_wind.bytes.end(), derived_marker.begin(), derived_marker.end());
    expect(marker != corrupted_wind.bytes.end(),
        "checkpoint contains local wind-derived payload");
    if (marker != corrupted_wind.bytes.end()) *marker ^= std::byte{1};
    expect(CandidateCheckpointCodec::Decode(corrupted_wind, decoded).code
            == FailureCode::CaptureFailed,
        "checkpoint rejects corrupted non-canonical wind-derived bytes");

    Snapshot corrupted = snapshot;
    const std::array local_marker{
        std::byte{0x80}, std::byte{0x81}, std::byte{0x82}, std::byte{0x83},
        std::byte{0x84}, std::byte{0x85}, std::byte{0x86}, std::byte{0x87}};
    const auto local_byte = std::search(corrupted.bytes.begin(),
        corrupted.bytes.end(), local_marker.begin(), local_marker.end());
    expect(local_byte != corrupted.bytes.end(),
        "checkpoint contains opaque local reconstruction payload");
    if (local_byte != corrupted.bytes.end()) *local_byte ^= std::byte{1};
    expect(CandidateCheckpointCodec::Decode(corrupted, decoded).code
            == FailureCode::RestoreVerificationFailed,
        "checkpoint rejects corrupted local reconstruction bytes");

    Snapshot wrong_generation = snapshot;
    ++wrong_generation.coordinate.generation;
    expect(CandidateCheckpointCodec::Decode(wrong_generation, decoded).code
            == FailureCode::RestoreVerificationFailed,
        "checkpoint rejects native generation drift");
}

void test_correction_restores_complete_pre_tick_input_producer_boundary()
{
    Fixture fixture;
    expect(fixture.regions.Bind(fixture.addresses).ok(),
        "bind pre-tick input-producer fixture");

    constexpr std::int32_t input_delay = 0;
    constexpr std::int32_t game_round = 9;
    constexpr std::int32_t game_time = 238;
    constexpr std::uint32_t source_frame = 237;
    fixture.memory.Set(fixture.addresses.input_log + 0x390, input_delay);
    fixture.memory.Set(fixture.addresses.input_log + 0x3A0, game_round);
    fixture.memory.Set(fixture.addresses.input_log + 0x3A4, game_time);
    fixture.memory.Set(fixture.addresses.input_log + 0x3AC,
        std::uint32_t{0xA1B2C3D4});
    fixture.memory.Set(fixture.addresses.input_log + 0x3B8,
        std::uint32_t{0x10203040});
    const auto row_address = [&](std::size_t slot) {
        return fixture.addresses.input_log + 0x3C0
            + (slot * 512 + (source_frame & 0x1ffu)) * 0x10;
    };
    const std::array<NativeInputCacheRowImage, 2> produced{{
        {game_round, source_frame, 0x1111u, 1},
        {game_round, source_frame, 0x2222u, 1}}};
    for (std::size_t slot = 0; slot < produced.size(); ++slot)
    {
        fixture.memory.Set(row_address(slot), produced[slot].game_round);
        fixture.memory.Set(row_address(slot) + 4, produced[slot].frame_index);
        fixture.memory.Set(row_address(slot) + 8, produced[slot].input_value);
        fixture.memory.Set(row_address(slot) + 12, produced[slot].filled);
    }
    const auto unrelated = fixture.addresses.input_log + 0x3C0
        + (91u & 0x1ffu) * 0x10;
    const NativeInputCacheRowImage unrelated_produced{
        game_round, 91u, 0xABCDEF01u, 1};
    fixture.memory.Set(unrelated, unrelated_produced.game_round);
    fixture.memory.Set(unrelated + 4, unrelated_produced.frame_index);
    fixture.memory.Set(unrelated + 8, unrelated_produced.input_value);
    fixture.memory.Set(unrelated + 12, unrelated_produced.filled);

    NativeCandidateImage producer_boundary{};
    expect(fixture.regions.Capture(producer_boundary).ok(),
        "capture the complete pre-correction input-producer transaction");

    const NativeInputCacheRowImage historical{};
    fixture.memory.Set(fixture.addresses.input_log + 0x3A4,
        std::int32_t{game_time - 1});
    fixture.memory.Set(fixture.addresses.input_log + 0x3AC,
        std::uint32_t{0x55667788});
    fixture.memory.Set(fixture.addresses.input_log + 0x3B8,
        std::uint32_t{0x99AABBCC});
    for (std::size_t slot = 0; slot < produced.size(); ++slot)
    {
        fixture.memory.Set(row_address(slot), historical.game_round);
        fixture.memory.Set(row_address(slot) + 4, historical.frame_index);
        fixture.memory.Set(row_address(slot) + 8, historical.input_value);
        fixture.memory.Set(row_address(slot) + 12, historical.filled);
    }
    fixture.memory.Set(unrelated, historical.game_round);
    fixture.memory.Set(unrelated + 4, historical.frame_index);
    fixture.memory.Set(unrelated + 8, historical.input_value);
    fixture.memory.Set(unrelated + 12, historical.filled);
    expect(fixture.regions.RestoreInputLogTransactional(producer_boundary).ok(),
        "restore the exact producer boundary after historical resimulation");
    NativeCandidateImage restored{};
    expect(fixture.regions.Capture(restored).ok()
            && restored.input_log == producer_boundary.input_log,
        "restore scalars, current/update state, both source rows, and unrelated cache");

    fixture.memory.Set(fixture.addresses.input_log + 0x3A4,
        std::int32_t{game_time - 1});
    fixture.memory.Set(fixture.addresses.input_log + 0x3AC,
        std::uint32_t{0x01020304});
    fixture.memory.Set(row_address(0) + 8, std::uint32_t{0xDEADBEEF});
    fixture.memory.Set(unrelated + 8, std::uint32_t{0xCAFEBABE});
    const auto before_partial_write = fixture.memory.bytes();
    fixture.memory.FailWrite(2);
    expect(fixture.regions.RestoreInputLogTransactional(producer_boundary).code
                == FailureCode::RestoreWriteFailed,
        "partial full-producer write reports the authoritative restore failure");
    fixture.memory.AllowWrites();
    expect(fixture.memory.bytes() == before_partial_write,
        "partial full-producer write restores the exact pre-write image");
}

void test_motion_bank_snapshot_is_bounded_and_transactional()
{
    Fixture fixture;
    MotionBankSnapshot motion{fixture.memory};
    expect(motion.Bind(fixture.addresses.fighter_roots, hgcpu_context()).ok(),
        "bind all-three-slot motion-bank topology");
    LocalReconstructionImage baseline{};
    expect(motion.Capture(baseline).ok()
            && baseline.bytes.size() >= motion_bank_base_image_bytes
            && baseline.bytes.size() <= motion_bank_image_bytes
            && MotionBankSnapshot::ValidateLocalImage(baseline),
        "capture bounded pointer-free motion-bank image");
    expect(!contains_qword(baseline.bytes, Fixture::memory_base + 0xC0000)
            && std::all_of(baseline.bytes.begin(), baseline.bytes.begin() + 8,
                [](std::byte value) {
                    return std::to_integer<std::uint8_t>(value) < 3;
                }),
        "motion image stores slot identities rather than live pointers");

    const auto first_primary_slot = Fixture::memory_base + 0xC0000;
    fixture.memory.Fill(first_primary_slot, motion_bank_primary_bytes,
        std::byte{0xE1});
    fixture.memory.Fill(fixture.addresses.fighter_roots[0]
            + motion_tail_fighter_offset,
        motion_tail_bytes, std::byte{0xE2});
    const auto first_primary_bank = fixture.addresses.fighter_roots[0] + 0x35A0;
    fixture.memory.Set(first_primary_bank + 0x20, std::uint32_t{2});
    fixture.memory.Set(first_primary_bank + 0x28,
        first_primary_slot + 2 * motion_bank_primary_bytes);
    fixture.memory.Set(first_primary_bank + 0x30, first_primary_slot);
    expect(motion.RestoreTransactional(baseline).ok(),
        "restore all motion slots and slot controller atomically");
    LocalReconstructionImage restored{};
    expect(motion.Capture(restored).ok() && restored.bytes == baseline.bytes,
        "motion-bank restore recaptures exact local image");

    fixture.memory.Set(first_primary_bank + 8,
        Fixture::memory_base + 0x150000);
    const auto before_topology_rejection = fixture.memory.bytes();
    expect(motion.RestoreTransactional(baseline).code
            == FailureCode::RestorePreflightFailed
            && fixture.memory.bytes() == before_topology_rejection,
        "motion allocation replacement invalidates without mutation");
    fixture.memory.Set(first_primary_bank + 8, first_primary_slot);

    auto corrupt = baseline;
    ++corrupt.checksum;
    expect(motion.RestoreTransactional(corrupt).code
            == FailureCode::RestorePreflightFailed,
        "motion checksum mismatch fails preflight");

    fixture.memory.Fill(first_primary_slot, motion_bank_primary_bytes,
        std::byte{0xA7});
    const auto before_partial_failure = fixture.memory.bytes();
    fixture.memory.FailWrite(4);
    expect(motion.RestoreTransactional(baseline).code
            == FailureCode::RestoreWriteFailed,
        "partial motion write reports transactional failure");
    fixture.memory.AllowWrites();
    expect(fixture.memory.bytes() == before_partial_failure,
        "partial motion write restores the complete undo image exactly");
}

void test_motion_solver_values_keep_links_and_undo()
{
    Fixture fixture;
    const auto fighter=fixture.addresses.fighter_roots[0];
    const auto skeleton=fighter+0x29120, chain=fighter+0x45700, node=chain+0x100;
    fixture.memory.Set(skeleton+2,std::int16_t{1});
    fixture.memory.Set(skeleton+0x10,chain);
    fixture.memory.Set(chain+0x48,node);
    fixture.memory.Set(chain+0x50,std::uint8_t{1});
    fixture.memory.Set(node,std::uintptr_t{0x143e88d50});
    fixture.memory.Set(node+0x36,std::uint16_t{0xb});
    fixture.memory.Set(node+0x80,1.25f);
    MotionBankSnapshot motion{fixture.memory};
    LocalReconstructionImage a{},b{},observed{};
    expect(motion.Bind(fixture.addresses.fighter_roots,hgcpu_context()).ok()
        && motion.Capture(a).ok(),"capture retained spring node and chain values");
    fixture.memory.Set(node+0x80,9.5f);
    fixture.memory.Set(chain,2.0f);
    expect(motion.Capture(b).ok(),"capture full B solver undo");
    expect(motion.RestoreTransactional(a).ok() && motion.Capture(observed).ok()
        && observed.bytes==a.bytes,"restore A solver values and anchor");
    expect(motion.RestoreTransactional(b).ok(),"recover B solver values");
    const auto before=fixture.memory.bytes();
    // Existing bank writes precede the new solver value writes. Inject within
    // the appended participant and require exact recovery of all B memory.
    fixture.memory.FailWrite(48);
    expect(motion.RestoreTransactional(a).code==FailureCode::RestoreWriteFailed,
        "solver write failure is reported");
    fixture.memory.AllowWrites();
    expect(fixture.memory.bytes()==before,"solver failure preserves complete B undo");
    fixture.memory.Set(node+0x50,node+0x100);
    const auto invalid=fixture.memory.bytes();
    expect(motion.RestoreTransactional(a).code==FailureCode::RestorePreflightFailed
        && fixture.memory.bytes()==invalid,"changed spring link rejects before native writes");
}

void test_motion_propagated_collider_links_and_undo()
{
    Fixture fixture;
    const auto fighter=fixture.addresses.fighter_roots[0];
    const auto skeleton=fighter+0x29120, chain=fighter+0x45700;
    const auto node=chain+0x100, peer=chain+0x400, collider=chain+0x800;
    fixture.memory.Set(skeleton+2,std::int16_t{1});
    fixture.memory.Set(skeleton+0x10,chain);
    fixture.memory.Set(chain+0x48,node);
    fixture.memory.Set(chain+0x50,std::uint8_t{2});
    fixture.memory.Set(node+0x28,peer);
    for(auto p:{node,peer}) {
        fixture.memory.Set(p,std::uintptr_t{0x143e88d50});
        fixture.memory.Set(p+0x36,std::uint16_t{0xb});
    }
    for(unsigned i=0;i<4;++i) {
        fixture.memory.Set(collider+i*0x100,std::uintptr_t{0x143e88000});
        fixture.memory.Set(collider+i*0x100+0xe4,i);
    }
    fixture.memory.Set(node+0xda,std::uint16_t{3});
    for(unsigned i=0;i<3;++i) fixture.memory.Set(node+0x170+i*0x60,collider+i*0x100);
    fixture.memory.Set(peer+0xda,std::uint16_t{1});
    fixture.memory.Set(peer+0x170,collider+0x300);
    MotionBankSnapshot motion{fixture.memory};
    LocalReconstructionImage a{},b{},observed{};
    expect(motion.Bind(fixture.addresses.fighter_roots,hgcpu_context()).ok() && motion.Capture(a).ok(),
        "capture spring collider owner dictionary");
    // Native 140336F80 propagates an existing peer collider, without creating
    // a new owner. The fourth link and its point cache belong to B undo.
    fixture.memory.Set(node+0xda,std::uint16_t{4});
    fixture.memory.Set(node+0x290,collider+0x300);
    fixture.memory.Set(node+0x240,9.25f);
    expect(motion.Capture(b).ok(),"capture B with propagated fourth collider");
    fixture.memory.AllowWrites();
    expect(motion.RestoreTransactional(a).ok(),"restore three-link A from four-link B");
    const auto write_count=fixture.memory.WriteCalls();
    expect(motion.Capture(observed).ok() && observed.bytes==a.bytes,
        "A collider count, links and dormant point caches reproduce exactly");
    expect(motion.RestoreTransactional(b).ok(),"recover four-link B");
    const auto before=fixture.memory.bytes();
    // Fail the first node count write, after clearing A's dormant fourth link.
    // Undo must recover B even though the intermediate count/link pair is invalid.
    fixture.memory.FailWrite(write_count-5);
    expect(motion.RestoreTransactional(a).code==FailureCode::RestoreWriteFailed,
        "partial collider publication reports failure");
    fixture.memory.AllowWrites();
    expect(fixture.memory.bytes()==before,"partial collider publication preserves complete B");
    fixture.memory.Set(node+0x290,collider+0x400);
    const auto unknown=fixture.memory.bytes();
    expect(motion.RestoreTransactional(a).code==FailureCode::RestorePreflightFailed
        && fixture.memory.bytes()==unknown,"unknown collider owner rejects without writes");
    fixture.memory.Set(node+0x290,collider+0x300);
    fixture.memory.Set(collider+0x300+0xe4,std::uint32_t{8});
    const auto invalidated=fixture.memory.bytes();
    expect(motion.RestoreTransactional(a).code==FailureCode::RestorePreflightFailed
        && fixture.memory.bytes()==invalidated,"invalidated collider index rejects without writes");
}

void test_secondary_event_state_is_pointer_free_and_transactional()
{
    Fixture fixture;
    SecondaryEventState secondary{fixture.memory};
    expect(secondary.Bind(fixture.addresses.fighter_roots, 7).ok(),
        "bind secondary-event pointer topology");
    SecondaryEventStateImage baseline{};
    expect(secondary.Capture(baseline).ok(),
        "capture pointer-free secondary-event state");
    const auto canonical = SecondaryEventState::CanonicalBytes(baseline);
    expect(!contains_qword(canonical, fixture.addresses.fighter_roots[0])
            && !contains_qword(canonical, fixture.addresses.fighter_roots[1]),
        "secondary-event canonical state excludes fighter back-pointers");

    const auto stack = fixture.addresses.fighter_roots[0]
        + secondary_event_stack_fighter_offset;
    const auto preserved_back_pointer = fixture.addresses.fighter_roots[0]
        + 0x111;
    fixture.memory.Fill(stack, 8, std::byte{0xE1});
    fixture.memory.Set(stack + 8, preserved_back_pointer);
    fixture.memory.Fill(stack + 0x10, 8, std::byte{0xE2});
    fixture.memory.Fill(stack + 0x258, 8, std::byte{0xE3});
    fixture.memory.Set(Fixture::memory_base + 0x41000 + 2,
        std::uint16_t{0x7777});
    expect(secondary.RestoreTransactional(baseline).ok(),
        "restore secondary-event semantic fields and cursors");
    SecondaryEventStateImage restored{};
    expect(secondary.Capture(restored).ok() && restored == baseline,
        "secondary-event restore recaptures exact typed image");
    std::uintptr_t observed_back_pointer{};
    expect(fixture.memory.Read(stack + 8,
            std::as_writable_bytes(std::span{&observed_back_pointer, 1}))
            && observed_back_pointer == preserved_back_pointer,
        "secondary-event restore does not overwrite fighter back-pointers");

    const auto table_pointer_address = stack + 0x240;
    fixture.memory.Set(table_pointer_address, std::uintptr_t{});
    const auto before_topology_rejection = fixture.memory.bytes();
    expect(secondary.RestoreTransactional(baseline).code
            == FailureCode::RestorePreflightFailed
            && fixture.memory.bytes() == before_topology_rejection,
        "secondary-event allocation replacement fails without mutation");
    fixture.memory.Set(table_pointer_address, Fixture::memory_base + 0x40000);

    auto wrong_count = baseline;
    ++wrong_count.header_counts[0];
    const auto before_count_rejection = fixture.memory.bytes();
    expect(secondary.RestoreTransactional(wrong_count).code
            == FailureCode::RestorePreflightFailed
            && fixture.memory.bytes() == before_count_rejection,
        "secondary-event header-count mismatch fails before undo capture");

    fixture.memory.Fill(stack, 8, std::byte{0xA1});
    fixture.memory.Fill(stack + 0x10, 8, std::byte{0xA2});
    const auto before_partial_failure = fixture.memory.bytes();
    fixture.memory.FailWrite(4);
    expect(secondary.RestoreTransactional(baseline).code
            == FailureCode::RestoreWriteFailed,
        "partial secondary-event write reports transactional failure");
    fixture.memory.AllowWrites();
    expect(fixture.memory.bytes() == before_partial_failure,
        "partial secondary-event write restores the complete undo image exactly");
}

void test_chara_animation_state_normalizes_sections_and_undoes_exactly()
{
    Fixture fixture;
    CharaAnimationState animation{fixture.memory};
    expect(animation.Bind(fixture.addresses.fighter_roots, 7).ok(),
        "bind character-animation scheduler topology");
    CharaAnimationStateImage baseline{};
    expect(animation.Capture(baseline).ok()
            && baseline.players[0].clip_section.present
            && baseline.players[0].clip_section.index == 0
            && baseline.players[0].trigger_count == 2,
        "capture normalized clip section and bounded trigger state");
    const auto canonical = CharaAnimationState::CanonicalBytes(baseline);
    const auto fighter = fixture.addresses.fighter_roots[0];
    const auto scheduler = Fixture::memory_base + 0x80000;
    expect(!contains_qword(canonical, fighter)
            && !contains_qword(canonical, scheduler)
            && !contains_qword(canonical, scheduler + 0x100)
            && !contains_qword(canonical, scheduler + 0x400),
        "character-animation canonical state excludes owner, list, and payload pointers");

    const auto packed = Fixture::memory_base + 0x90000;
    const auto section_table = packed + 0x100;
    const auto clip = fighter + chara_anim_clip_player_offset;
    const auto runtime = fighter + chara_anim_runtime_offset;
    fixture.memory.Set(scheduler + 8, fixture.addresses.fighter_roots[1]);
    CharaAnimationStateImage cross_fighter_scheduler{};
    expect(animation.Capture(cross_fighter_scheduler).code
            == FailureCode::IdentityMismatch,
        "scheduler character rebinding invalidates the checkpoint generation");
    expect(animation.Bind(fixture.addresses.fighter_roots, 8).ok()
            && animation.Capture(cross_fighter_scheduler).code
                == FailureCode::IdentityMismatch,
        "a new binding cannot legitimize an active foreign scheduler character");
    fixture.memory.Set(scheduler + 8, fighter);
    expect(animation.Bind(fixture.addresses.fighter_roots, 7).ok(),
        "restore original animation topology after generation test");
    expect(animation.RestoreTransactional(baseline).code
            == FailureCode::RestorePreflightFailed,
        "same-round rebinding rejects the old local reconstruction dictionary");
    expect(animation.Capture(baseline).ok(), "capture the new binding identity");

    fixture.memory.Set(clip + 0x28, std::uint32_t{});
    fixture.memory.Set(runtime, Fixture::memory_base + 0xA8000);
    fixture.memory.Fill(runtime + 8, 8, std::byte{0xD7});
    CharaAnimationStateImage dormant{};
    expect(animation.Capture(dormant).code == FailureCode::CaptureFailed,
        "dormant runtime outside the retained section dictionary is not restorable");
    fixture.memory.Set(runtime, section_table + 0x40);
    expect(animation.Capture(dormant).ok()
            && dormant.players[0].runtime_section.present
            && std::all_of(dormant.players[0].runtime_scalars.begin(),
                dormant.players[0].runtime_scalars.end(),
                [](std::byte value) { return value == std::byte{0xD7}; }),
        "inactive clip retains exact runtime section and scalars for undo");
    fixture.memory.Set(runtime, Fixture::memory_base + 0xA8000);
    expect(animation.RestoreUnderEnclosingTransaction(dormant).ok(),
        "enclosing transaction repairs native-reader overlap even for an inactive clip");
    expect(animation.RestoreTransactional(baseline).ok(),
        "active clip restore reconstructs runtime after inactive cleanup lag");

    fixture.memory.Set(clip + 0x28, std::uint32_t{1});
    fixture.memory.Set(clip + 0x2C, std::uint32_t{});
    fixture.memory.Set(runtime, std::uintptr_t{});
    fixture.memory.Fill(runtime + 8, 8, std::byte{});
    CharaAnimationStateImage pending_bootstrap{};
    expect(animation.Capture(pending_bootstrap).ok()
            && !pending_bootstrap.players[0].runtime_section.present,
        "active pre-bootstrap clip preserves the native null runtime boundary");
    fixture.memory.Set(clip + 0x2C, std::uint32_t{1});
    fixture.memory.Set(runtime, section_table + 0x40);
    expect(animation.RestoreTransactional(pending_bootstrap).ok(),
        "active pre-bootstrap restore reproduces the native null runtime boundary");

    fixture.memory.Set(clip + 8, section_table + 0x60);
    fixture.memory.Set(clip + 0x2C, std::uint32_t{1});
    fixture.memory.Set(runtime, section_table + 0x40);
    CharaAnimationStateImage rebound_clip{};
    expect(animation.Capture(rebound_clip).ok()
            && rebound_clip.players[0].clip_section.present
            && rebound_clip.players[0].runtime_section.present
            && rebound_clip.players[0].clip_section
                != rebound_clip.players[0].runtime_section,
        "active clip rebinding preserves the independently consumed runtime section");
    fixture.memory.Set(runtime, section_table + 0x60);
    expect(animation.RestoreTransactional(rebound_clip).ok(),
        "animation restore reconstructs distinct clip and runtime section identities");
    expect(animation.RestoreTransactional(baseline).ok(),
        "bootstrapped clip restore reconstructs runtime after pre-bootstrap state");

    // The enclosing fighter transaction already owns baseline as its undo.
    // Its native reader can leave a temporary clip/runtime inconsistency;
    // the supplement must repair that state without capturing a second undo.
    fixture.memory.Set(runtime, Fixture::memory_base + 0xA8000);
    CharaAnimationStateImage intermediate{};
    expect(animation.Capture(intermediate).code == FailureCode::CaptureFailed,
        "intermediate active runtime is not a valid standalone checkpoint");
    expect(animation.PreflightRestore(baseline).ok()
            && animation.RestoreUnderEnclosingTransaction(baseline).ok()
            && animation.Capture(intermediate).ok() && intermediate == baseline,
        "enclosing transaction repairs runtime using its prevalidated captured image");

    fixture.memory.Set(clip + 8, section_table + 0x60);
    fixture.memory.Fill(clip + 0x10, 0x20, std::byte{0xE1});
    fixture.memory.Set(runtime, section_table + 0x60);
    fixture.memory.Fill(runtime + 8, 8, std::byte{0xE2});
    fixture.memory.Fill(scheduler + 0x10, 0x5C, std::byte{0xE3});
    fixture.memory.Fill(scheduler + 0x408, 0x18, std::byte{0xE4});
    fixture.memory.Set(scheduler + 0x6C, std::uint32_t{0x12345678});
    expect(animation.RestoreTransactional(baseline).ok(),
        "restore animation scalars and reconstruct authored section pointers");
    CharaAnimationStateImage restored{};
    expect(animation.Capture(restored).ok() && restored == baseline,
        "character-animation restore recaptures exact pointer-free image");
    std::uintptr_t restored_clip_pointer{}, restored_runtime_pointer{};
    expect(fixture.memory.Read(clip + 8,
                std::as_writable_bytes(std::span{&restored_clip_pointer, 1}))
            && fixture.memory.Read(runtime,
                std::as_writable_bytes(std::span{&restored_runtime_pointer, 1}))
            && restored_clip_pointer == section_table + 0x40
            && restored_runtime_pointer == section_table + 0x40,
        "animation restore derives pointers from live packed-data topology");
    std::uint32_t allocator_residue{};
    expect(fixture.memory.Read(scheduler + 0x6C,
                std::as_writable_bytes(std::span{&allocator_residue, 1}))
            && allocator_residue == 0x12345678,
        "animation restore preserves noncanonical scheduler allocator residue");

    fixture.memory.Set(scheduler + 0x200, scheduler + 0x100);
    const auto before_topology_rejection = fixture.memory.bytes();
    expect(animation.RestoreTransactional(baseline).code
            == FailureCode::RestorePreflightFailed
            && fixture.memory.bytes() == before_topology_rejection,
        "animation list replacement fails before mutation");
    fixture.memory.Set(scheduler + 0x200, scheduler + 0x220);

    auto wrong_count = baseline;
    ++wrong_count.players[0].trigger_count;
    const auto before_count_rejection = fixture.memory.bytes();
    expect(animation.RestoreTransactional(wrong_count).code
            == FailureCode::RestorePreflightFailed
            && fixture.memory.bytes() == before_count_rejection,
        "animation trigger-count drift fails before undo capture");

    fixture.memory.Fill(clip + 0x10, 0x20, std::byte{0xA1});
    fixture.memory.Fill(runtime + 8, 8, std::byte{0xA2});
    const auto before_partial_failure = fixture.memory.bytes();
    fixture.memory.FailWrite(4);
    expect(animation.RestoreTransactional(baseline).code
            == FailureCode::RestoreWriteFailed,
        "partial character-animation write reports transactional failure");
    fixture.memory.AllowWrites();
    expect(fixture.memory.bytes() == before_partial_failure,
        "partial character-animation write restores the complete undo image exactly");
}

void test_chara_animation_palette_bank_alias()
{
    Fixture fixture;
    const auto fighter = fixture.addresses.fighter_roots[0];
    const auto palette = fighter + 0x971E8;
    const auto bank = Fixture::memory_base + 0xA8000;
    const auto clip = fighter + chara_anim_clip_player_offset;
    const auto runtime = fighter + chara_anim_runtime_offset;
    fixture.memory.Set(palette, bank);
    fixture.memory.Set(bank, std::uint32_t{12});
    CharaAnimationState animation{fixture.memory};
    expect(animation.Bind(fixture.addresses.fighter_roots, 7).ok(),
        "bind authored clip and palette bank dictionaries");
    MotionBankSnapshot motion{fixture.memory};
    LocalReconstructionImage motion_image{};
    expect(motion.Bind(fixture.addresses.fighter_roots, hgcpu_context(), &animation).ok(),
        "motion delegates the shared pointer to its animation owner");
    CharaAnimationStateImage active{}, dormant{}, observed{};
    expect(animation.Capture(active).ok(), "capture active authored clip");
    fixture.memory.Set(clip + 0x28, std::uint32_t{});
    fixture.memory.Set(runtime, bank);
    fixture.memory.Fill(runtime + 8, 8, std::byte{0x27});
    expect(animation.Capture(dormant).ok() && dormant.players[0].runtime_motion_bank,
        "inactive shared runtime admits exactly its owning palette bank");
    expect(motion.Capture(motion_image).ok(),
        "native bank publication does not invalidate delegated motion topology");
    const auto bytes = CharaAnimationState::CanonicalBytes(dormant);
    expect(!contains_qword(bytes, bank)
        && CharaAnimationState::DecodeCanonicalBytes(bytes, observed).ok()
        && observed == dormant, "bank identity roundtrips without a process pointer");
    expect(animation.RestoreTransactional(active).ok(), "bank to clip restoration");
    const auto before = fixture.memory.bytes();
    fixture.memory.FailWrite(5);
    expect(animation.RestoreTransactional(dormant).code == FailureCode::RestoreWriteFailed,
        "publication failure after bank pointer write reports failure");
    fixture.memory.AllowWrites();
    expect(fixture.memory.bytes() == before, "failed bank publication completely recovers active B");
    expect(animation.RestoreTransactional(dormant).ok()
        && animation.Capture(observed).ok() && observed == dormant,
        "clip to bank restores exact dormant scalars");
    fixture.memory.Set(clip + 0x28, std::uint32_t{1});
    expect(!animation.Capture(observed).ok(), "active clip cannot interpret a palette bank");
    expect(!motion.Capture(motion_image).ok(), "delegation preserves invalid-runtime rejection");
    fixture.memory.Set(clip + 0x28, std::uint32_t{});
    fixture.memory.Set(bank, std::uint32_t{13});
    expect(!animation.PreflightRestore(dormant).ok(), "changed bank header rejects old binding");
    fixture.memory.Set(bank, std::uint32_t{12});
    expect(animation.Bind(fixture.addresses.fighter_roots, 7).ok()
        && !motion.Capture(motion_image).ok(),
        "same-fighter animation rebinding invalidates delegated motion snapshots");
}

void test_scheduler_activation_preserves_dormant_binding_and_undo()
{
    for (const auto residue : {std::uintptr_t{}, std::uintptr_t{0xDEADBEEF}})
    {
        Fixture fixture;
        const auto scheduler = Fixture::memory_base + 0x80000;
        const auto fighter = fixture.addresses.fighter_roots[0];
        fixture.memory.Set(scheduler + 8, residue);
        fixture.memory.Set(scheduler + 0x64, std::uint32_t{});
        CharaAnimationState animation{fixture.memory};
        expect(animation.Bind(fixture.addresses.fighter_roots, 7).ok(),
            "bind native dormant scheduler with untouched constructor bits");
        CharaAnimationStateImage dormant{};
        expect(animation.Capture(dormant).ok()
                && !dormant.players[0].scheduler_chara_bound,
            "retain dormant binding as a local dictionary choice");
        fixture.memory.Set(scheduler + 8, fighter);
        fixture.memory.Set(scheduler + 0x64, std::uint32_t{1});
        CharaAnimationStateImage active{};
        expect(animation.Capture(active).ok()
                && active.players[0].scheduler_chara_bound,
            "native activation assigns the fighter without replacing topology");
        const auto before_failure = fixture.memory.bytes();
        fixture.memory.FailWrite(8); // Dormant pointer written; active scalar still live.
        expect(animation.RestoreTransactional(dormant).code
                == FailureCode::RestoreWriteFailed,
            "scalar failure after dormant pointer write can still undo");
        fixture.memory.AllowWrites();
        expect(fixture.memory.bytes() == before_failure,
            "undo restores exact active bytes after temporary binding inconsistency");
        expect(animation.RestoreTransactional(dormant).ok(),
            "restore dormant scheduler from active playback");
        std::uintptr_t observed{};
        fixture.memory.Read(scheduler + 8, std::as_writable_bytes(std::span{&observed, 1}));
        expect(observed == residue, "restore exact dormant constructor bits");
        expect(animation.RestoreTransactional(active).ok(),
            "reactivate using the retained fighter identity");
        fixture.memory.Set(scheduler + 8, fighter + 8);
        expect(animation.Capture(active).code == FailureCode::IdentityMismatch,
            "reject a third scheduler character value");
        fixture.memory.Set(scheduler + 8, fighter);
        expect(animation.Bind(fixture.addresses.fighter_roots, 7).ok(),
            "explicit same-round rebinding creates a new local dictionary");
        const auto before_stale = fixture.memory.bytes();
        expect(animation.RestoreTransactional(dormant).code
                == FailureCode::RestorePreflightFailed
                && fixture.memory.bytes() == before_stale,
            "old dormant choice cannot be interpreted through a new dictionary");
    }
}

#ifdef _WIN32
void test_gpu_copy_completion_and_retirement()
{
    // Backend contract only: this does not validate SC6 resource bindings,
    // render-thread admission, native writers or historical restoration.
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf());
    expect(SUCCEEDED(hr), "GPU completion test creates WARP device");
    if (FAILED(hr)) return;
    std::array<std::uint32_t, 16> original{}, subsequent{};
    original.fill(0x12345678); subsequent.fill(0xabcdef01);
    D3D11_TEXTURE2D_DESC descriptor{};
    descriptor.Width = descriptor.Height = 4;
    descriptor.MipLevels = descriptor.ArraySize = descriptor.SampleDesc.Count = 1;
    descriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    descriptor.Usage = D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA data{original.data(), 16, 0};
    ComPtr<ID3D11Texture2D> source, image, readback;
    hr = device->CreateTexture2D(&descriptor, &data, source.GetAddressOf());
    if (SUCCEEDED(hr)) hr = device->CreateTexture2D(&descriptor, nullptr, image.GetAddressOf());
    descriptor.Usage = D3D11_USAGE_STAGING;
    descriptor.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (SUCCEEDED(hr)) hr = device->CreateTexture2D(&descriptor, nullptr, readback.GetAddressOf());
    expect(SUCCEEDED(hr), "GPU completion test reserves all transfer storage");
    if (FAILED(hr)) return;
    ReplayGpuCompletion completion;
    using Result = ReplayGpuCompletion::Result;
    const auto retire = [&] {
        const auto stop = ReplayGpuCompletion::Clock::now() + std::chrono::seconds(2);
        while (!completion.retired() && ReplayGpuCompletion::Clock::now() < stop)
        {
            completion.Poll(context.Get(), ReplayGpuCompletion::Clock::now());
            if (!completion.retired()) Sleep(1);
        }
        expect(completion.retired(), "GPU transfer retirement is observed within test deadline");
        return completion.retired();
    };
    const auto read_value = [&](std::uint32_t expected) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const auto status = context->Map(readback.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        expect(SUCCEEDED(status), "GPU readback is ready after acknowledged completion");
        if (FAILED(status)) return;
        bool equal = true;
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 4; ++x)
            {
                std::uint32_t value{};
                std::memcpy(&value, static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch + x * 4, 4);
                equal = equal && value == expected;
            }
        context->Unmap(readback.Get(), 0);
        expect(equal, "GPU copy contents survive later source writes");
    };
    expect(completion.submitted_serial()==0,"new completion owner has no submission receipt");
    expect(SUCCEEDED(completion.Prepare(device.Get())), "GPU query allocation precedes commands");
    context->CopyResource(image.Get(), source.Get());
    context->UpdateSubresource(source.Get(), 0, nullptr, subsequent.data(), 16, 0);
    context->CopyResource(readback.Get(), image.Get());
    expect(SUCCEEDED(completion.Submit(context.Get(), ReplayGpuCompletion::Clock::now() + std::chrono::seconds(2)))
        && completion.result() == Result::Pending && !completion.Release(),
        "GPU submission does not publish completion or release leases");
    if (!retire()) return;
    expect(completion.result() == Result::Complete && completion.submitted_serial()==1, "GPU query reports signaled completion for its first submission");
    read_value(original[0]);
    expect(completion.Release(), "GPU completed transfer can retire query storage");
    expect(SUCCEEDED(completion.Prepare(device.Get())), "GPU completion query can be reused after retirement");
    context->CopyResource(readback.Get(), source.Get());
    expect(SUCCEEDED(completion.Submit(context.Get(), ReplayGpuCompletion::Clock::now() + std::chrono::seconds(2))),
        "GPU source verification submits");
    if (!retire()) return;
    read_value(subsequent[0]);
    expect(completion.Release() && completion.submitted_serial()==2, "GPU source verification releases without reusing the prior submission identity");
    for (bool cancel : {false, true})
    {
        expect(SUCCEEDED(completion.Prepare(device.Get())), "GPU failure experiment prepares query");
        const auto now = ReplayGpuCompletion::Clock::now();
        expect(SUCCEEDED(completion.Submit(context.Get(), cancel ? now + std::chrono::seconds(2) : now)),
            "GPU failure experiment submits");
        if (cancel) completion.Cancel();
        expect(!completion.retired() && !completion.Release(), "GPU cancellation or deadline does not release in-flight work");
        completion.Poll(context.Get(), now);
        if (!retire()) return;
        expect(completion.result() == (cancel ? Result::Cancelled : Result::TimedOut),
            "GPU retirement preserves cancellation or timeout as failure");
        expect(completion.Release() && completion.submitted_serial()==(cancel?4u:3u), "GPU failed transfer releases only after retirement and preserves its submission identity");
    }
}
#endif

class EmptyStageWindAllocator final : public IStageWindAllocator
{
public:
    std::uintptr_t Allocate(std::size_t) noexcept override { return 0; }
    void Free(std::uintptr_t) noexcept override {}
};

void test_battle_audio_selector_is_generation_bound_and_transactional()
{
    Fixture fixture;
    constexpr auto handler = Fixture::memory_base + 0x155000;
    std::array<std::uintptr_t, maximum_battle_audio_handlers>
        observed_handlers{handler};
    fixture.memory.Set(handler,
        Fixture::image_base + std::uintptr_t{0x326A6C8});
    fixture.memory.Set(handler + 0x3E0, std::int32_t{});
    BattleAudioSelectorState selector{fixture.memory};
    const BattleAudioSelectorBinding binding{
        Fixture::image_base, 0x5000000, hgcpu_context(),
        &resolve_test_battle_audio_handler,
        &test_battle_audio_handler_overflow, &observed_handlers};
    expect(selector.Bind(binding).ok(),
        "bind generation-scoped battle-audio selector state");
    observed_handlers[0] = 0;
    BattleAudioSelectorImage undiscovered{};
    expect(selector.Capture(undiscovered).ok()
            && undiscovered.observed_count == 0
            && undiscovered.alternations[0] == 0,
        "capture the deterministic initial selector before handler discovery");
    observed_handlers[0] = handler;
    observed_handlers[1] = handler + 0x800;
    fixture.memory.Set(observed_handlers[1],
        Fixture::image_base + std::uintptr_t{0x326A6C8});
    fixture.memory.Set(handler + 0x3E0, std::int32_t{1});
    fixture.memory.Set(observed_handlers[1] + 0x3E0, std::int32_t{1});
    expect(selector.RestoreTransactional(undiscovered).ok(),
        "restore a pre-discovery checkpoint after multiple handlers become known");
    std::int32_t second_restored{};
    fixture.memory.Read(observed_handlers[1] + 0x3E0,
        std::as_writable_bytes(std::span{&second_restored, 1}));
    expect(second_restored == 0,
        "pre-discovery restore initializes every later handler slot to zero");
    BattleAudioSelectorImage baseline{};
    expect(selector.Capture(baseline).ok() && baseline.observed_count == 2
            && baseline.alternations[0] == 0
            && baseline.alternations[1] == 0,
        "capture the ordered set of battle-audio selectors");

    fixture.memory.Set(handler + 0x3E0, std::int32_t{1});
    expect(selector.RestoreTransactional(baseline).ok(),
        "restore battle-audio selector before semantic replay");
    std::int32_t restored{};
    fixture.memory.Read(handler + 0x3E0,
        std::as_writable_bytes(std::span{&restored, 1}));
    expect(restored == 0, "battle-audio selector restore writes exact value");

    auto strict_binding = binding;
    strict_binding.exact_membership = true;
    expect(selector.Bind(strict_binding).ok(), "bind exact audio membership for resumable host");
    expect(selector.PreflightRestore(undiscovered).code == FailureCode::IdentityMismatch,
        "resumable host cannot synthesize selectors for newly discovered handlers");
    fixture.memory.Set(handler + 0x3E0, std::int32_t{1});
    fixture.memory.FailPartialWrite(1, 1);
    expect(selector.RestoreTransactional(baseline).code == FailureCode::RestoreVerificationFailed,
        "partial audio write reports failure");
    fixture.memory.AllowWrites();
    fixture.memory.Read(handler + 0x3E0, std::as_writable_bytes(std::span{&restored, 1}));
    expect(restored == 1, "partial failed audio slot is included in exact B undo");

    auto wrong_generation = baseline;
    ++wrong_generation.round_generation;
    expect(selector.PreflightRestore(wrong_generation).code
            == FailureCode::GenerationMismatch,
        "battle-audio selector rejects generation drift before mutation");

    fixture.memory.Set(handler + 0x3E0, std::int32_t{1});
    fixture.memory.CorruptAfterWrite(
        1, handler + 0x3E0, std::byte{0x02});
    expect(selector.RestoreTransactional(baseline).code
            == FailureCode::RestoreVerificationFailed,
        "battle-audio selector reports post-write verification failure");
    fixture.memory.AllowWrites();
    fixture.memory.Read(handler + 0x3E0,
        std::as_writable_bytes(std::span{&restored, 1}));
    expect(restored == 1,
        "battle-audio selector verification failure restores exact undo value");

    observed_handlers[0] = handler + 0x800;
    expect(selector.PreflightRestore(baseline).code
            == FailureCode::IdentityMismatch,
        "battle-audio selector rejects handler identity drift");
    observed_handlers[0] = handler;
    fixture.memory.Set(handler, Fixture::image_base + std::uintptr_t{0x1234});
    selector.Reset();
    expect(selector.Bind(binding).ok()
            && selector.Capture(baseline).code
                == FailureCode::CapturePreflightFailed,
        "battle-audio selector rejects an invalid handler vtable");
    selector.Reset();
    expect(selector.Capture(baseline).code == FailureCode::AdapterUnqualified,
        "battle-audio selector lifecycle reset clears the binding");
}

struct CandidateWindFixture
{
    explicit CandidateWindFixture(Fixture& fixture)
        : probe(fixture.memory), transaction(fixture.memory, allocator),
          audio_selector(fixture.memory),
          motion(fixture.memory), secondary(fixture.memory),
          animation(fixture.memory), move_dispatch(fixture.memory)
    {
        addresses = {Fixture::image_base, 0x4300000,
            Fixture::memory_base + 0x1F000, 7};
        root = Fixture::memory_base + 0x1E000;
        fixture.memory.Set(addresses.root_pointer, root);
        fixture.memory.Set(root, std::uintptr_t{});
        fixture.memory.Set(root + 0x98, std::uint32_t{});
        fixture.memory.Set(root + 0x9C, std::int32_t{});
        expect(probe.Bind(addresses).ok(), "bind empty candidate wind fixture");
        expect(motion.Bind(fixture.addresses.fighter_roots, hgcpu_context()).ok(),
            "bind candidate matrix-bank fixture");
        expect(secondary.Bind(fixture.addresses.fighter_roots, 7).ok(),
            "bind candidate secondary-event fixture");
        expect(animation.Bind(fixture.addresses.fighter_roots, 7).ok(),
            "bind candidate character-animation fixture");
        expect(move_dispatch.Bind(fixture.addresses.move_dispatch, 7).ok(),
            "bind candidate MoveDispatch fixture");
        handlers[0] = Fixture::memory_base + 0x155000;
        fixture.memory.Set(handlers[0],
            Fixture::image_base + std::uintptr_t{0x326A6C8});
        fixture.memory.Set(handlers[0] + 0x3E0, std::int32_t{});
        const BattleAudioSelectorBinding selector_binding{
            Fixture::image_base, 0x5000000, hgcpu_context(),
            &resolve_test_battle_audio_handler,
            &test_battle_audio_handler_overflow, &handlers};
        expect(audio_selector.Bind(selector_binding).ok(),
            "bind candidate battle-audio selector fixture");
    }

    EmptyStageWindAllocator allocator;
    StageWindTopologyProbe probe;
    StageWindGraphTransaction transaction;
    BattleAudioSelectorState audio_selector;
    MotionBankSnapshot motion;
    SecondaryEventState secondary;
    CharaAnimationState animation;
    MoveDispatchState move_dispatch;
    StageWindTopologyAddresses addresses{};
    std::uintptr_t root{};
    std::array<std::uintptr_t, maximum_battle_audio_handlers> handlers{};
};

CandidateAdapterBinding candidate_binding(
    HgCpuExecFn reader, CandidateWindFixture& wind)
{
    prepare_candidate_ucrt_broker();
    CandidateAdapterBinding binding{};
    binding.context = NativeContext{7, 11, {101, 102}, 201};
    binding.battle_audio_selector = &wind.audio_selector;
    binding.hgcpu_context = hgcpu_context();
    binding.hgcpu_writer = &fake_hgcpu_writer;
    binding.hgcpu_reader = reader;
    binding.motion_banks = &wind.motion;
    binding.move_dispatch = &wind.move_dispatch;
    binding.secondary_events = &wind.secondary;
    binding.chara_animation = &wind.animation;
    binding.ucrt_broker = &candidate_ucrt_broker;
    binding.wind_probe = &wind.probe;
    binding.wind_transaction = &wind.transaction;
    binding.wind_addresses = wind.addresses;
    binding.simulation_thread_id = candidate_thread_id;
    binding.reconcile = &noop_reconcile;
    return binding;
}

void test_candidate_adapter_restore_and_outer_undo()
{
    Fixture restored_fixture;
    expect(restored_fixture.regions.Bind(restored_fixture.addresses).ok(),
        "bind candidate adapter restore fixture");
    HgCpuStreamShim restored_hgcpu;
    CandidateGameStateAdapter restored_adapter{
        restored_fixture.regions, restored_hgcpu};
    CandidateWindFixture restored_wind{restored_fixture};
    auto restored_binding = candidate_binding(&fake_hgcpu_reader, restored_wind);
    restored_binding.restore_audio_selector = true;
    expect(restored_adapter.Configure(restored_binding).ok(),
        "configure candidate adapter restore fixture");
    InputTimeline restored_inputs{16};
    SnapshotStore restored_snapshots{
        2 * 1024 * 1024, 8, CapacityPolicy::RejectNew};
    PresentationJournal restored_journal{16, 1024};
    SimulationSession restored_session{restored_adapter, restored_inputs,
        restored_snapshots, restored_journal};
    hgcpu_payload.fill(std::byte{0x21});
    const auto initial_hgcpu = hgcpu_payload;
    const auto initial_memory = restored_fixture.memory.bytes();
    UcrtRandBrokerImage initial_ucrt{};
    expect(candidate_ucrt_broker.Capture(
        candidate_thread_id, initial_ucrt).ok(),
        "capture candidate adapter UCRT baseline");
    expect(restored_session.BindAndCaptureBaseline(
        restored_binding.context, {7, 0}).ok(),
        "capture real candidate adapter baseline");
    restored_fixture.memory.Fill(
        restored_fixture.addresses.fighter_roots[0]
            + chara_anim_clip_player_offset + 0x10,
        sizeof(std::uint32_t), std::byte{0x4A});
    Snapshot canonical_diagnostic_snapshot{};
    expect(restored_adapter.CaptureCanonical(
            {7, 1}, canonical_diagnostic_snapshot).ok(),
        "capture canonical peer diagnostic source");
    PeerBaselineStateDiagnostic canonical_diagnostic{};
    expect(restored_adapter.GetLastCanonicalPeerDiagnostic(
            {7, 1}, canonical_diagnostic).ok()
            && canonical_diagnostic.animation_clip_words[0][0]
                == 0x4A4A4A4Au,
        "canonical peer diagnostic retains its canonical coordinate");
    Snapshot transient_diagnostic_snapshot{};
    expect(restored_adapter.Capture(
            {7, 2}, transient_diagnostic_snapshot).ok(),
        "capture transient state after canonical peer diagnostic");
    expect(restored_adapter.GetLastCanonicalPeerDiagnostic(
            {7, 1}, canonical_diagnostic).ok()
            && restored_adapter.GetLastCanonicalPeerDiagnostic(
                {7, 2}, canonical_diagnostic).code
                == FailureCode::GenerationMismatch,
        "transient capture cannot relabel canonical peer diagnostic state");
    restored_fixture.memory.Fill(
        restored_fixture.event_masks, 0x10, std::byte{0x91});
    restored_fixture.memory.Fill(
        restored_fixture.addresses.pump_state + 0x20, 0x1C, std::byte{0x92});
    hgcpu_payload.fill(std::byte{0x93});
    restored_fixture.memory.Set(restored_wind.handlers[0] + 0x3E0, std::int32_t{1});
    candidate_ucrt_broker.HandleRand(candidate_thread_id,
        Schema::Sc6UcrtLayout::movevm_rand_return_rva, &std::rand);
    candidate_ucrt_broker.HandleRand(candidate_thread_id, 0x895d6e, &std::rand);
    std::rand(); // Native calls bypassing the IAT belong to complete B too.
    expect(restored_session.RestoreAndResimulate({7, 0}, {7, 0}).ok(),
        "restore real candidate adapter through SimulationSession transaction");
    expect(restored_fixture.memory.bytes() == initial_memory,
        "candidate adapter restores exact typed native image");
    expect(hgcpu_payload == initial_hgcpu,
        "candidate adapter restores exact HgCpu reconstruction image");
    const auto baseline_snapshot = restored_snapshots.Load({7, 0});
    expect(baseline_snapshot.has_value(),
        "retain candidate baseline for canonical recapture verification");
    hgcpu_payload.fill(std::byte{0x7E});
    expect(restored_adapter.VerifyRestoredState(*baseline_snapshot).ok(),
        "opaque native recapture bytes are not canonical verification fields");
    hgcpu_payload = initial_hgcpu;
    UcrtRandBrokerImage restored_ucrt{};
    expect(candidate_ucrt_broker.Capture(
            candidate_thread_id, restored_ucrt).ok()
            && restored_ucrt == initial_ucrt,
        "candidate adapter restores exact value-only UCRT image");

    Fixture failed_fixture;
    expect(failed_fixture.regions.Bind(failed_fixture.addresses).ok(),
        "bind candidate adapter failure fixture");
    HgCpuStreamShim failed_hgcpu;
    CandidateGameStateAdapter failed_adapter{failed_fixture.regions, failed_hgcpu};
    CandidateWindFixture failed_wind{failed_fixture};
    const auto failed_binding = candidate_binding(&flaky_hgcpu_reader, failed_wind);
    expect(failed_adapter.Configure(failed_binding).ok(),
        "configure candidate adapter failure fixture");
    InputTimeline failed_inputs{16};
    SnapshotStore failed_snapshots{2 * 1024 * 1024, 8, CapacityPolicy::RejectNew};
    PresentationJournal failed_journal{16, 1024};
    SimulationSession failed_session{
        failed_adapter, failed_inputs, failed_snapshots, failed_journal};
    hgcpu_payload.fill(std::byte{0x31});
    expect(failed_session.BindAndCaptureBaseline(
        failed_binding.context, {7, 0}).ok(),
        "capture candidate adapter failure baseline");
    failed_fixture.memory.Fill(
        failed_fixture.event_masks, 0x10, std::byte{0xA1});
    hgcpu_payload.fill(std::byte{0xA2});
    candidate_ucrt_broker.HandleRand(candidate_thread_id,
        Schema::Sc6UcrtLayout::movevm_rand_return_rva, &std::rand);
    candidate_ucrt_broker.HandleRand(candidate_thread_id, 0x1111, &std::rand);
    UcrtRandBrokerImage before_failed_ucrt{};
    expect(candidate_ucrt_broker.Capture(candidate_thread_id, before_failed_ucrt).ok(),
        "retain both CRT lanes at adapter B before failed publication");
    const auto before_failed_memory = failed_fixture.memory.bytes();
    const auto before_failed_hgcpu = hgcpu_payload;
    fail_next_hgcpu_read = true;
    expect(failed_session.RestoreAndResimulate({7, 0}, {7, 0}).code
            == FailureCode::RestoreWriteFailed,
        "partial HgCpu reader failure reports typed restore failure");
    expect(failed_fixture.memory.bytes() == before_failed_memory
            && hgcpu_payload == before_failed_hgcpu,
        "partial HgCpu reader failure restores exact outer undo image");
    UcrtRandBrokerImage after_failed_ucrt{};
    expect(candidate_ucrt_broker.Capture(candidate_thread_id, after_failed_ucrt).ok()
            && after_failed_ucrt == before_failed_ucrt,
        "adapter cancellation retains B combat/native cursors and unresolved counters");
}

void test_full_binding_exit_releases_transaction_scratch_storage()
{
    Fixture fixture;
    expect(fixture.regions.Bind(fixture.addresses).ok(),
        "bind full-exit scratch fixture");
    HgCpuStreamShim hgcpu;
    CandidateGameStateAdapter adapter{fixture.regions, hgcpu};
    CandidateWindFixture wind{fixture};
    const auto binding = candidate_binding(&fake_hgcpu_reader, wind);
    expect(adapter.Configure(binding).ok(),
        "configure binding-owned transaction scratch");
    expect(adapter.owned_scratch_bytes() > 0
            && fixture.regions.ScratchCapacityBytes() > 0,
        "restore-capable binding owns prewarmed transaction scratch");

    adapter.ReleaseScratchStorage();
    fixture.regions.ReleaseScratchStorage();
    expect(adapter.owned_scratch_bytes() == 0
            && fixture.regions.ScratchCapacityBytes() == 0,
        "full scene exit returns binding-owned scratch to its pre-match footprint");
}

void test_candidate_adapter_native_failure_undoes_hgcpu()
{
    Fixture fixture;
    expect(fixture.regions.Bind(fixture.addresses).ok(),
        "bind candidate adapter native-failure fixture");
    HgCpuStreamShim hgcpu;
    CandidateGameStateAdapter adapter{fixture.regions, hgcpu};
    CandidateWindFixture wind{fixture};
    const auto binding = candidate_binding(&fake_hgcpu_reader, wind);
    expect(adapter.Configure(binding).ok(),
        "configure candidate adapter native-failure fixture");
    InputTimeline inputs{16};
    SnapshotStore snapshots{2 * 1024 * 1024, 8, CapacityPolicy::RejectNew};
    PresentationJournal journal{16, 1024};
    SimulationSession session{adapter, inputs, snapshots, journal};
    hgcpu_payload.fill(std::byte{0x41});
    expect(session.BindAndCaptureBaseline(binding.context, {7, 0}).ok(),
        "capture candidate adapter native-failure baseline");
    fixture.memory.Fill(fixture.event_masks, 0x10, std::byte{0xB1});
    fixture.memory.Fill(
        fixture.addresses.pump_state + 0x20, 0x1C, std::byte{0xB2});
    hgcpu_payload.fill(std::byte{0xB3});
    const auto before_memory = fixture.memory.bytes();
    const auto before_hgcpu = hgcpu_payload;
    fixture.memory.FailWrite(5);
    expect(session.RestoreAndResimulate({7, 0}, {7, 0}).code
            == FailureCode::RestoreWriteFailed,
        "native write after HgCpu reconstruction reports typed failure");
    expect(fixture.memory.bytes() == before_memory
            && hgcpu_payload == before_hgcpu,
        "native write failure restores native and HgCpu outer undo image");
}

void test_hgcpu_direct_source_coverage()
{
    DirectNativeMemory memory;
    HgCpuCoverageProbe probe(memory);
    std::array<std::byte, 16> other_target{};
    std::array<HgCpuCoverageTarget, 2> targets{{
        {reinterpret_cast<std::uintptr_t>(coverage_source.data()), coverage_source.size(), 101},
        {reinterpret_cast<std::uintptr_t>(other_target.data()), other_target.size(), 102},
    }};
    expect(probe.Bind(targets).ok(), "bind HgCpu coverage targets");
    HgCpuCoverageSample sample{};
    expect(probe.Observe(&fake_coverage_writer, hgcpu_context(), 1, sample).ok(),
        "capture HgCpu coverage baseline");
    coverage_source[3] = std::byte{0x44};
    other_target[5] = std::byte{0x55};
    expect(probe.Observe(&fake_coverage_writer, hgcpu_context(), 2, sample).ok(),
        "capture HgCpu coverage delta");
    expect(sample.changed_bytes == 2 && sample.directly_sourced_changed_bytes == 1,
        "classify direct and unmapped fighter mutations");
    expect(sample.unmapped_deltas.size() == 1
        && sample.unmapped_deltas[0].target == 1
        && sample.unmapped_deltas[0].offset == 5,
        "report pointer-free unmapped target offset");
}

void test_capture_restore_preserves_exclusions()
{
    Fixture fixture;
    const std::array<std::array<std::uint32_t,3>,2> contact_a{{{1,0,1},{0,1,0}}};
    const std::array<std::array<std::uint32_t,3>,2> contact_b{{{0,1,0},{1,0,1}}};
    for(std::size_t player=0;player<2;++player)
        fixture.memory.Set(fixture.addresses.fighter_roots[player]+0x95774,contact_a[player]);
    expect(fixture.regions.Bind(fixture.addresses).ok(), "bind candidate regions");
    NativeCandidateImage baseline{};
    expect(fixture.regions.Capture(baseline).ok(), "capture candidate image");

    for(std::size_t player=0;player<2;++player)
        fixture.memory.Set(fixture.addresses.fighter_roots[player]+0x95774,contact_b[player]);

    fixture.memory.Fill(fixture.event_masks, 0x10, std::byte{0xE1});
    fixture.memory.Fill(fixture.addresses.pump_state + 0x20, 1, std::byte{0xE2});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x08, 1, std::byte{0xE6});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x30, 1, std::byte{0xE7});
    fixture.memory.Set(fixture.addresses.scheduler_base + 0x58, std::uint32_t{1});
    fixture.memory.Fill(Fixture::memory_base + 0x5008, 1, std::byte{0xE3});
    fixture.memory.Fill(fixture.addresses.move_command_base, 1, std::byte{0xE4});
    fixture.memory.Fill(fixture.addresses.slot_param_base, 1, std::byte{0xE5});
    fixture.memory.Fill(fixture.addresses.lcg_rng, 4, std::byte{0xE8});
    fixture.memory.Fill(fixture.addresses.lfsr_rng, 0x64, std::byte{0xE9});
    fixture.memory.Set(fixture.addresses.lfsr_rng + 0x64, std::uint32_t{8});
    fixture.memory.Fill(fixture.addresses.xorshift_rng, 0x0C, std::byte{0xEA});
    fixture.memory.Fill(fixture.addresses.wind_rng, 0x18, std::byte{0xEB});
    fixture.memory.Fill(fixture.addresses.vm_freeze_record, 0x40,
        std::byte{0xE7});
    fixture.memory.Fill(Fixture::memory_base + 0x10700,
        native_stage_wind_emitter_state_size, std::byte{0xE6});
    fixture.memory.Fill(Fixture::memory_base + 0x10800,
        native_stage_wind_emitter_state_size, std::byte{0xE5});
    fixture.memory.Fill(Fixture::memory_base + 0x10700
            + native_stage_wind_emitter_state_size,
        8, std::byte{0xA9});
    fixture.memory.Set(fixture.addresses.pending_hit_record, std::uint32_t{0xABCD});
    fixture.memory.Set(fixture.addresses.pending_hit_record + 4, -0.125f);
    fixture.memory.Set(fixture.addresses.pending_hit_record + 8,
        fixture.addresses.fighter_roots[0]);
    fixture.memory.Set(fixture.addresses.pending_hit_record + 0x10,
        std::uint32_t{0x200000});
    fixture.memory.Set(fixture.addresses.pending_launcher_sync, std::uint8_t{0});
    const auto player_watch = fixture.addresses.camera_action_backing + 3 * 0x3E0;
    fixture.memory.Fill(player_watch + 0x25C, 16 * sizeof(float), std::byte{0xCC});
    fixture.memory.Set(player_watch + 0x29C, std::int32_t{19});
    fixture.memory.Set(player_watch + 0x2A0, std::uint32_t{12});
    fixture.memory.Set(fixture.addresses.frame_counter, std::uint32_t{99});
    fixture.memory.Set(fixture.addresses.input_log + 0x3A0, std::int32_t{8});
    fixture.memory.Set(fixture.addresses.input_log + 0x3A4, std::int32_t{99});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x1488, std::int32_t{8});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x148C, std::uint32_t{99});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x1490, std::uint32_t{44});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x14F0, std::int32_t{7});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x1462, std::uint8_t{1});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x1463, std::uint8_t{3});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x1464, std::uint8_t{1});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x1465, std::uint8_t{1});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x1478, std::int32_t{1});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x1480, std::uint8_t{9});
    fixture.memory.Set(Fixture::memory_base + 0x1C300,
        std::array<std::uint8_t, 3>{9, 9, 9});
    fixture.memory.Fill(Fixture::memory_base + 0x1C000, 8, std::byte{0xEC});
    fixture.memory.Fill(Fixture::memory_base + 0x1C100, 16, std::byte{0xED});
    fixture.memory.Fill(Fixture::memory_base + 0x1C200, 16, std::byte{0xEE});
    fixture.memory.Set(fixture.addresses.input_log + 0x3C0, std::int32_t{8});
    fixture.memory.Set(fixture.addresses.input_log + 0x3C4, std::uint32_t{99});
    fixture.memory.Set(fixture.addresses.input_log + 0x3C8, std::uint32_t{0xFE});
    fixture.memory.Set(fixture.addresses.input_log + 0x3CC, std::uint8_t{0});

    fixture.memory.Fill(fixture.addresses.pump_state + 0x3C, 1, std::byte{0xA1});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x0C, 1, std::byte{0xA6});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x18, 1, std::byte{0xA7});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x5C, 1, std::byte{0xA8});
    fixture.memory.Fill(Fixture::memory_base + 0x500C, 1, std::byte{0xA2});
    fixture.memory.Fill(fixture.addresses.move_command_base + 0x2A28, 1, std::byte{0xA3});
    fixture.memory.Fill(fixture.addresses.move_command_base + 0x3034, 1, std::byte{0xA4});
    fixture.memory.Fill(fixture.addresses.slot_param_base + 0x28, 1, std::byte{0xA5});
    fixture.memory.Fill(fixture.addresses.input_log + 0x3CD, 3, std::byte{0xAF});

    expect(fixture.regions.RestoreTransactional(baseline).ok(),
        "restore native candidate regions and explicit RNG streams");
    std::array<std::array<std::uint32_t,3>,2> contact_restored{};
    for(std::size_t player=0;player<2;++player)
        expect(fixture.memory.Read(fixture.addresses.fighter_roots[player]+0x95774,
            std::as_writable_bytes(std::span{contact_restored[player]})),"independent contact latch read");
    expect(contact_restored==contact_a,"restore captured contact-side edge latches before native callbacks");
    NativeCandidateImage restored{};
    expect(fixture.regions.Capture(restored).ok() && restored == baseline, "recapture exact semantic image");
    expect(fixture.memory.Get(fixture.addresses.pump_state + 0x3C) == std::byte{0xA1}, "preserve pump tail");
    expect(fixture.memory.Get(fixture.addresses.scheduler_base + 0x0C) == std::byte{0xA6}, "preserve scheduler constructor residue");
    expect(fixture.memory.Get(fixture.addresses.scheduler_base + 0x18) == std::byte{0xA7}, "preserve scheduler reserved bytes");
    expect(fixture.memory.Get(fixture.addresses.scheduler_base + 0x5C) == std::byte{0xA8}, "preserve scheduler allocator residue");
    expect(fixture.memory.Get(Fixture::memory_base + 0x500C) == std::byte{0xA2}, "preserve SubVM gap");
    expect(fixture.memory.Get(fixture.addresses.move_command_base + 0x2A28) == std::byte{0xA3}, "preserve diagnostic text");
    expect(fixture.memory.Get(fixture.addresses.move_command_base + 0x3034) == std::byte{0xA4}, "preserve uninitialized tail");
    expect(fixture.memory.Get(fixture.addresses.slot_param_base + 0x28) == std::byte{0xA5}, "preserve slot padding");
    expect(fixture.memory.Get(fixture.addresses.input_log + 0x3CD) == std::byte{0xAF},
        "preserve initialized cache-row reserved bytes");
    expect(restored.rng == baseline.rng,
        "restore all four explicit Lux RNG streams exactly");
    expect(restored.vm_freeze_record == baseline.vm_freeze_record,
        "restore the complete simulation freeze-output record exactly");
    expect(restored.stage_wind_emitters == baseline.stage_wind_emitters
            && restored.stage_wind_emitters.states.size() == 2,
        "restore bounded stage-wind emitter timers and admission state exactly");
    expect(fixture.memory.Get(Fixture::memory_base + 0x10700
            + native_stage_wind_emitter_state_size) == std::byte{0xA9},
        "preserve the unverified stage-wind emitter tail");
    expect(restored.frame == baseline.frame,
        "restore the coordinate clocks and input-pair boundary exactly");
    expect(restored.round_sequence == baseline.round_sequence,
        "restore the bounded round-state sequence values and current state");
    expect(restored.pending_hit == baseline.pending_hit
            && restored.pending_hit.attacker_slot == 2,
        "restore the pending-hit record through its fighter-relative slot");
    expect(restored.camera_distance_history == baseline.camera_distance_history
            && restored.camera_distance_history[3].present == 1
            && restored.camera_distance_history[3].sample_count == 11
            && restored.camera_distance_history[3].cursor == 7,
        "restore exact PlayerWatch distance-history ring and cursors");

    const auto canonical = NativeCandidateRegions::CanonicalBytes(baseline);
    expect(!contains_qword(canonical, fixture.event_masks), "canonical bytes exclude event owner pointer");
    expect(!contains_qword(canonical, Fixture::memory_base + 0x5000), "canonical bytes exclude SubVM pointer");
    expect(!contains_qword(canonical, fixture.addresses.input_log),
        "canonical bytes exclude InputLog owner pointer");
    expect(!contains_qword(canonical, Fixture::memory_base + 0x1C100),
        "canonical bytes exclude input-pair backing pointer");
    expect(!contains_qword(canonical, Fixture::memory_base + 0x1C300),
        "canonical bytes exclude round-sequence backing pointer");
    expect(!contains_qword(canonical, fixture.addresses.fighter_roots[0])
            && !contains_qword(canonical, fixture.addresses.fighter_roots[1]),
        "canonical bytes exclude pending-hit fighter pointers");
}

void test_contact_side_latch_recovery()
{
    Fixture fixture;
    expect(fixture.regions.Bind(fixture.addresses).ok(), "bind contact latch recovery");
    NativeCandidateImage a{}, b{}, decoded{};
    expect(fixture.regions.Capture(a).ok(), "capture zero contact latches");
    for (auto root : fixture.addresses.fighter_roots)
    {
        fixture.memory.Set(root + 0x95774, std::array<std::uint32_t,3>{1,1,1});
        fixture.memory.Set(root + 0x95770, std::uint32_t{0xABCDEF});
        fixture.memory.Set(root + 0x95780, std::uintptr_t{0x12345678});
    }
    expect(fixture.regions.Capture(b).ok(), "capture B contact latches");
    auto encoded = NativeCandidateRegions::CanonicalBytes(b);
    expect(NativeCandidateRegions::DecodeCanonicalBytes(encoded, decoded).ok()
        && decoded.contact_side_latches == b.contact_side_latches, "contact latch canonical round trip");
    auto invalid = a;
    invalid.contact_side_latches[1][2] = 2;
    const auto before = fixture.memory.bytes();
    expect(!fixture.regions.RestoreTransactional(invalid).ok()
        && fixture.memory.bytes() == before, "invalid captured latch rejects without mutation");
    encoded = NativeCandidateRegions::CanonicalBytes(invalid);
    expect(!NativeCandidateRegions::DecodeCanonicalBytes(encoded, decoded).ok(),
        "invalid encoded contact latch rejects");
    // Sweep partial failures through publication, including both newly owned writes.
    for (std::size_t call = 1; call <= 80; ++call)
    {
        fixture.memory.FailWrite(0);
        expect(fixture.regions.RestoreTransactional(b).ok(), "install complete B latches");
        const auto complete_b = fixture.memory.bytes();
        fixture.memory.FailPartialWrite(call, 5);
        const auto result = fixture.regions.RestoreTransactional(a);
        if (!result.ok())
            expect(fixture.memory.bytes() == complete_b, "partial publication restores complete B bytes");
    }
    fixture.memory.FailWrite(0);
    expect(fixture.regions.RestoreTransactional(a).ok(), "publish A contact latches");
    expect(fixture.regions.RestoreTransactional(b).ok()
        && fixture.regions.RestoreTransactional(b).ok(), "repeat B contact latch recovery");
    for (auto root : fixture.addresses.fighter_roots)
    {
        expect(fixture.memory.Get(root + 0x95770) == std::byte{0xEF}
            && fixture.memory.Get(root + 0x95780) == std::byte{0x78},
            "preserve adjacent native tracker state and owner pointer");
    }
    fixture.memory.Set(fixture.addresses.fighter_roots[0] + 0x95774, std::uint32_t{2});
    expect(!fixture.regions.Capture(decoded).ok(), "invalid live contact latch rejects capture");
}

void test_preflight_is_atomic()
{
    Fixture fixture;
    expect(fixture.regions.Bind(fixture.addresses).ok(), "bind identity-drift fixture");
    NativeCandidateImage baseline{};
    expect(fixture.regions.Capture(baseline).ok(), "capture identity-drift baseline");
    fixture.memory.Fill(fixture.event_masks, 1, std::byte{0xF1});
    fixture.memory.Set(fixture.addresses.move_command_base + 0x08, std::uintptr_t{0xDEADBEEF});
    const auto before = fixture.memory.bytes();
    expect(
        fixture.regions.RestoreTransactional(baseline).code == FailureCode::IdentityMismatch,
        "identity drift rejects restore");
    expect(fixture.memory.bytes() == before, "identity rejection performs zero mutation");
}

void test_unknown_class_and_invalid_header_fail_closed()
{
    Fixture unknown;
    unknown.memory.Set(Fixture::memory_base + 0x5000, Fixture::image_base + std::uintptr_t{0x1234});
    expect(
        unknown.regions.Bind(unknown.addresses).code == FailureCode::AdapterUnqualified,
        "unknown SubVM class is unqualified");

    Fixture header;
    expect(header.regions.Bind(header.addresses).ok(), "bind invalid-header fixture");
    NativeCandidateImage baseline{};
    expect(header.regions.Capture(baseline).ok(), "capture invalid-header baseline");
    header.memory.Set(header.addresses.move_dispatch + 0x4B0, std::int32_t{3});
    const auto before = header.memory.bytes();
    expect(
        header.regions.RestoreTransactional(baseline).code == FailureCode::IdentityMismatch,
        "invalid event-mask count rejects restore");
    expect(header.memory.bytes() == before, "invalid header performs zero mutation");

    Fixture camera_class;
    expect(camera_class.regions.Bind(camera_class.addresses).ok(),
        "bind camera-class drift fixture");
    expect(camera_class.regions.Capture(baseline).ok(),
        "capture camera-class drift baseline");
    camera_class.memory.Set(camera_class.addresses.camera_action_backing + 3 * 0x3E0,
        Fixture::image_base + std::uintptr_t{0x3E88018});
    NativeCandidateImage changed_camera_class{};
    expect(camera_class.regions.Capture(changed_camera_class).ok(),
        "in-place camera action class transition remains capture-qualified");
    expect(changed_camera_class.camera_distance_history[3].present == 0,
        "distance history follows the current action class, not bind-time class");
    expect(camera_class.regions.PreflightRestore(baseline).ok(),
        "a checkpoint from an earlier action class remains generation-compatible");

    const auto new_player_watch =
        camera_class.addresses.camera_action_backing + 4 * 0x3E0;
    camera_class.memory.Set(new_player_watch,
        Fixture::image_base + std::uintptr_t{0x3E87EB0});
    for (std::size_t index = 0; index < 16; ++index)
        camera_class.memory.Set(new_player_watch + 0x25C + index * sizeof(float),
            static_cast<float>(200 + index));
    camera_class.memory.Set(new_player_watch + 0x29C, std::int32_t{4});
    camera_class.memory.Set(new_player_watch + 0x2A0, std::uint32_t{3});
    expect(camera_class.regions.Capture(changed_camera_class).ok()
            && changed_camera_class.camera_distance_history[3].present == 0
            && changed_camera_class.camera_distance_history[4].present == 1,
        "PlayerWatch history tracks a native in-place class move between slots");

    Fixture camera_cursor;
    camera_cursor.memory.Set(
        camera_cursor.addresses.camera_action_backing + 3 * 0x3E0 + 0x2A0,
        std::uint32_t{16});
    expect(camera_cursor.regions.Bind(camera_cursor.addresses).code
            == FailureCode::CapturePreflightFailed,
        "PlayerWatch distance-history cursor outside the 16-slot ring fails closed");

    Fixture camera_boundary;
    expect(camera_boundary.regions.Bind(camera_boundary.addresses).ok(),
        "bind camera capture-boundary fixture");
    camera_boundary.memory.Set(
        camera_boundary.addresses.camera_action_backing + 3 * 0x3E0 + 0x2A0,
        std::uint32_t{16});
    NativeCandidateImage rejected_camera{};
    expect(camera_boundary.regions.Capture(rejected_camera).code
            == FailureCode::CaptureFailed,
        "camera history becoming invalid after bind fails the exact capture boundary");
    const auto camera_boundary_diagnostic =
        camera_boundary.regions.validation_diagnostic();
    expect(camera_boundary_diagnostic.issue
                == NativeCandidateValidationIssue::CandidateRegionRead
            && camera_boundary_diagnostic.index == 33,
        "post-bind camera capture failure preserves the exact PlayerWatch action index");

    Fixture no_camera;
    no_camera.addresses.camera_action_backing = 0;
    NativeCandidateImage no_camera_image{};
    expect(no_camera.regions.Bind(no_camera.addresses).ok()
            && no_camera.regions.Capture(no_camera_image).ok()
            && std::all_of(no_camera_image.camera_distance_history.begin(),
                no_camera_image.camera_distance_history.end(),
                [](const NativeCameraDistanceHistoryImage& history) {
                    return history.present == 0;
                }),
        "an absent optional camera produces an empty bounded history image");

    Fixture prior_header;
    prior_header.memory.Set(
        prior_header.addresses.battle_manager + 0x14C4, std::int32_t{1});
    expect(prior_header.regions.Bind(prior_header.addresses).code
            == FailureCode::AdapterUnqualified,
        "prior-input TArray capacity below its two-player count is unqualified");
    const auto diagnostic = prior_header.regions.validation_diagnostic();
    expect(diagnostic.issue == NativeCandidateValidationIssue::IdentityRead
            && diagnostic.index == 22 && diagnostic.observed_a == 2
            && diagnostic.observed_b == 1,
        "prior-input TArray rejection identifies the exact invalid header");

    Fixture pending_owner;
    pending_owner.memory.Set(pending_owner.addresses.pending_hit_record + 8,
        Fixture::memory_base + 0x21000);
    expect(pending_owner.regions.Bind(pending_owner.addresses).code
            == FailureCode::CapturePreflightFailed,
        "pending-hit owner outside the bound fighter pair fails closed");
    const auto pending_diagnostic = pending_owner.regions.validation_diagnostic();
    expect(pending_diagnostic.issue
                == NativeCandidateValidationIssue::CandidateRegionRead
            && pending_diagnostic.index == 14,
        "pending-hit owner rejection identifies the fighter-slot mapping");

    Fixture oversized_sequence;
    oversized_sequence.memory.Set(
        oversized_sequence.addresses.battle_manager + 0x1478,
        std::int32_t{static_cast<std::int32_t>(native_round_sequence_max_states + 1)});
    expect(oversized_sequence.regions.Bind(oversized_sequence.addresses).code
            == FailureCode::AdapterUnqualified,
        "oversized round-state sequence fails closed before capture");
}

void test_camera_component_class_is_mutable_state()
{
    Fixture fixture;
    fixture.addresses.camera_director = Fixture::memory_base + 0x30000;
    fixture.addresses.camera_velocity_basis = Fixture::memory_base + 0x30400;
    fixture.addresses.camera_timer_config = Fixture::memory_base + 0x30500;
    fixture.addresses.camera_timer_node = Fixture::memory_base + 0x30700;
    fixture.addresses.camera_timer_globals = {
        Fixture::memory_base + 0x30A00, Fixture::memory_base + 0x30A08,
        Fixture::memory_base + 0x30A10, Fixture::memory_base + 0x30A18};
    const auto component = Fixture::memory_base + 0x31000;
    const auto base_vtable = Fixture::image_base + std::uintptr_t{0x3E88000};
    const auto attention_vtable =
        Fixture::image_base + std::uintptr_t{0x3E88008};
    fixture.memory.Set(fixture.addresses.camera_director + 0x270, component);
    fixture.memory.Set(component, base_vtable);
    fixture.memory.SetExternal(base_vtable + 0x100,
        Fixture::image_base + std::uintptr_t{0x33FBC0});
    fixture.memory.SetExternal(attention_vtable + 0x100,
        Fixture::image_base + std::uintptr_t{0x340ED0});
    fixture.memory.Fill(component + 8, 0x320, std::byte{0});
    fixture.memory.Fill(fixture.addresses.camera_director, 0x360, std::byte{0});
    fixture.memory.Set(fixture.addresses.camera_director + 0x270, component);
    fixture.memory.Fill(fixture.addresses.camera_velocity_basis,
        native_camera_velocity_basis_bytes, std::byte{0x21});
    fixture.memory.Fill(fixture.addresses.camera_timer_config + 0xA8,
        native_camera_timer_config_state_bytes, std::byte{0x22});
    fixture.memory.Fill(fixture.addresses.camera_timer_node,
        native_camera_timer_node_bytes, std::byte{0});
    const auto action_owner = Fixture::memory_base + 0x30B00;
    fixture.memory.Set(fixture.addresses.camera_timer_node, action_owner);
    fixture.memory.Set(fixture.addresses.camera_timer_node + 8,
        fixture.addresses.camera_action_backing);
    for (std::size_t index = 0; index < native_camera_action_count; ++index)
    {
        const auto action = fixture.addresses.camera_action_backing
            + index * 0x3E0;
        fixture.memory.Set(fixture.addresses.camera_timer_node
            + 0x10 + index * sizeof(std::uintptr_t), action);
        fixture.memory.Set(action + 0x08, static_cast<std::uint32_t>(index));
        fixture.memory.Set(action + 0x10, action_owner);
        fixture.memory.Set(action + 0x18, fixture.addresses.camera_timer_node);
        fixture.memory.Set(action + 0x20, std::uint32_t{});
    }
    for (const auto timer_global : fixture.addresses.camera_timer_globals)
        fixture.memory.Set(timer_global, std::uint64_t{});

    expect(fixture.regions.Bind(fixture.addresses).ok(),
        "bind camera component class-transition fixture");
    NativeCameraSourceFrameImage before{};
    expect(fixture.regions.CaptureCameraSourceFrame(before).ok()
            && before.components[0].serialization
                == NativeCameraComponentSerialization::Base,
        "capture the initial camera component class");

    fixture.memory.Set(component, attention_vtable);
    NativeCameraSourceFrameImage after{};
    expect(fixture.regions.CaptureCameraSourceFrame(after).ok()
            && after.components[0].serialization
                == NativeCameraComponentSerialization::Attention,
        "capture an in-place native camera component class transition");
    expect(fixture.regions.RestoreCameraSourceFrameTransactional(before).ok(),
        "transactionally restore a prior camera component class and state");
    NativeCameraSourceFrameImage restored{};
    expect(fixture.regions.CaptureCameraSourceFrame(restored).ok()
            && restored == before,
        "camera source restore verifies the exact prior component generation");

    // Replay slots have immutable preallocated backing and mutable publication.
    // Simulate the native HgCpu director publication separately from typed state.
    fixture.addresses.replay_camera_publication = true;
    fixture.memory.Set(fixture.addresses.camera_timer_config + 0x90,
        fixture.addresses.camera_director);
    for (std::size_t i = 0; i < native_camera_component_count; ++i)
        fixture.memory.Set(fixture.addresses.camera_timer_config + 0x10 + i * 8,
            component + i * 0x400);
    expect(fixture.regions.Bind(fixture.addresses).ok(),
        "bind fixed replay camera backing with unpublished slots");
    auto a = std::make_unique<NativeCandidateImage>();
    auto b = std::make_unique<NativeCandidateImage>();
    auto actual = std::make_unique<NativeCandidateImage>();
    expect(fixture.regions.Capture(*a).ok() && !a->camera_components[2].present,
        "capture A with camera slot two unpublished");
    const auto slot = component + 2 * 0x400;
    fixture.memory.Fill(slot, 0x400, std::byte{});
    fixture.memory.Set(slot, attention_vtable);
    fixture.memory.Set(fixture.addresses.camera_director + 0x280, slot);
    expect(fixture.regions.Capture(*b).ok()
            && b->camera_components[2].serialization == NativeCameraComponentSerialization::Attention,
        "capture B after native publication in retained backing");
    expect(fixture.regions.PreflightRestore(*a).ok(),
        "A preflight accepts publication transition without changing backing");
    const auto unpublished_restore=fixture.regions.RestoreTransactional(*a);
    const auto unpublished_failure=fixture.regions.validation_diagnostic();
    expect(unpublished_failure.issue==NativeCandidateValidationIssue::CandidateRegionVerification
            && unpublished_failure.index==(1u<<15),
        "first camera publication mismatch survives successful internal undo verification");
    expect(!unpublished_restore.ok()
            && fixture.regions.Capture(*actual).ok() && *actual == *b,
        "typed restore cannot hide unperformed native unpublication and recovers B");
    fixture.memory.Set(fixture.addresses.camera_director + 0x280, std::uintptr_t{});
    expect(fixture.regions.RestoreTransactional(*a).ok()
            && fixture.regions.Capture(*actual).ok() && *actual == *a,
        "typed restore verifies A after native director unpublication");
    fixture.memory.Set(fixture.addresses.camera_director + 0x280, slot);
    expect(fixture.regions.RestoreTransactional(*b).ok()
            && fixture.regions.Capture(*actual).ok() && *actual == *b,
        "native B republication and typed undo recover complete B");
    fixture.memory.Set(fixture.addresses.camera_director + 0x280, slot + 8);
    expect(!fixture.regions.PreflightCapture().ok(),
        "publication outside its fixed backing rejects");
    fixture.memory.Set(fixture.addresses.camera_director + 0x280, slot);
    fixture.memory.Set(fixture.addresses.camera_timer_config + 0x20, component);
    expect(!fixture.regions.PreflightCapture().ok(),
        "aliased or replaced backing rejects before capture or writes");
}

void test_mt_complete_image_and_binding()
{
    Fixture fixture;
    fixture.addresses.mt_rng = Fixture::memory_base + 0x158000;
    std::array<std::uint32_t, 1252> native_mt{};
    for (std::size_t i = 0; i < native_mt.size(); ++i) native_mt[i] = static_cast<std::uint32_t>(i * 0x1923 + 7);
    native_mt[0] = 624;
    fixture.memory.Set(fixture.addresses.mt_rng, native_mt);
    expect(fixture.regions.Bind(fixture.addresses).ok(), "bind MT owner");
    NativeCandidateImage target{}, decoded{}, restored{};
    expect(fixture.regions.Capture(target).ok() && target.rng.mt_present && target.rng.mt == native_mt,
        "capture full MT cursor/state/temper/ranges");
    const auto encoded = NativeCandidateRegions::CanonicalBytes(target);
    expect(NativeCandidateRegions::DecodeCanonicalBytes(encoded, decoded).ok() && decoded.rng == target.rng,
        "MT image survives canonical encoding");
    fixture.memory.Fill(fixture.addresses.mt_rng, sizeof(native_mt), std::byte{0x39});
    expect(fixture.regions.RestoreTransactional(target).ok() && fixture.regions.Capture(restored).ok()
        && restored.rng == target.rng, "restore complete MT state including tail range globals");
    target.rng.mt_present = 0;
    const auto before = fixture.memory.bytes();
    expect(!fixture.regions.PreflightRestore(target).ok() && fixture.memory.bytes() == before,
        "missing MT ownership rejects before writes");
}

void test_lfsr_refill_sentinel_is_bounded()
{
    Fixture sentinel;
    sentinel.memory.Set(sentinel.addresses.lfsr_rng + 0x64, std::uint32_t{25});
    expect(sentinel.regions.Bind(sentinel.addresses).ok(),
        "LFSR index 25 is the valid native refill sentinel");
    NativeCandidateImage image{};
    expect(sentinel.regions.Capture(image).ok() && image.rng.lfsr_index == 25,
        "capture the valid LFSR refill sentinel exactly");

    Fixture invalid;
    invalid.memory.Set(invalid.addresses.lfsr_rng + 0x64, std::uint32_t{26});
    expect(invalid.regions.Bind(invalid.addresses).code
            == FailureCode::CapturePreflightFailed,
        "LFSR index above the refill sentinel fails closed");
}

void test_partial_write_undoes_exactly()
{
    Fixture fixture;
    expect(fixture.regions.Bind(fixture.addresses).ok(), "bind undo fixture");
    NativeCandidateImage baseline{};
    expect(fixture.regions.Capture(baseline).ok(), "capture undo target");
    fixture.memory.Fill(fixture.event_masks, 0x10, std::byte{0xD1});
    fixture.memory.Fill(fixture.addresses.pump_state + 0x20, 0x1C, std::byte{0xD2});
    const auto before = fixture.memory.bytes();
    fixture.memory.FailWrite(5);
    expect(
        fixture.regions.RestoreTransactional(baseline).code == FailureCode::RestoreWriteFailed,
        "partial write reports typed failure");
    expect(fixture.regions.validation_diagnostic().issue
            == NativeCandidateValidationIssue::CandidateRegionWrite,
        "partial write records the native write stage");
    expect(fixture.memory.bytes() == before, "partial write restores exact undo image");
}

struct MoveDispatchFixture
{
    static constexpr std::uintptr_t memory_base = Fixture::memory_base;
    static constexpr std::uintptr_t object = memory_base + 0x16000;
    static constexpr std::uintptr_t frame_slots = memory_base + 0x17000;
    static constexpr std::uintptr_t sub_elements = memory_base + 0x18000;

    MoveDispatchFixture() : memory(memory_base, 0x20000), state(memory)
    {
        memory.Set(object + 0x470, frame_slots);
        memory.Set(object + 0x478, std::int32_t{3});
        memory.Set(object + 0x47C, std::int32_t{2});
        memory.Set(object + 0x480, std::uint8_t{5});
        memory.Set(object + 0x484, std::int32_t{10});
        memory.Set(object + 0x488, std::uint8_t{1});
        memory.Set(object + 0x490, std::uint32_t{0});
        memory.Set(object + 0x494, std::int32_t{4});
        memory.Set(object + 0x498, sub_elements);
        memory.Set(object + 0x4A0, std::int32_t{2});
        memory.Set(object + 0x4A4, std::int32_t{2});
        memory.Set(object + 0x4B8, std::uint32_t{2});
        memory.Set(object + 0x4BC, std::uint32_t{7});
        memory.Set(sub_elements, std::int32_t{20});
        memory.Set(sub_elements + 4, std::uint8_t{0});
        memory.Fill(sub_elements + 8, 24, std::byte{0x31});
        memory.Set(sub_elements + 0x20, std::int32_t{21});
        memory.Set(sub_elements + 0x24, std::uint8_t{1});
        memory.Fill(sub_elements + 0x28, 24, std::byte{0x32});
    }

    FakeNativeMemory memory;
    MoveDispatchState state;
};

void test_move_dispatch_action_phase_restore()
{
    MoveDispatchFixture fixture;
    expect(fixture.state.Bind(MoveDispatchFixture::object, 19).ok(),
        "bind MoveDispatch action phase");
    MoveDispatchImage baseline{};
    expect(fixture.state.Capture(baseline).ok(),
        "capture MoveDispatch action phase");

    const auto before_provider_tick = MoveDispatchState::CanonicalBytes(baseline);
    expect(baseline.stable_provider_state == 2 && baseline.stable_provider_ticks == 7,
        "capture native tutorial provider state and tick count");
    fixture.memory.Set(MoveDispatchFixture::object + 0x4B8, std::uint32_t{1});
    fixture.memory.Set(MoveDispatchFixture::object + 0x4BC, std::uint32_t{20});
    MoveDispatchImage after_provider_tick{};
    expect(fixture.state.Capture(after_provider_tick).ok()
            && MoveDispatchState::CanonicalBytes(after_provider_tick) != before_provider_tick,
        "tutorial provider transition changes canonical identity");
    MoveDispatchImage decoded{};
    expect(MoveDispatchState::DecodeCanonicalBytes(before_provider_tick, decoded).ok()
            && decoded == baseline,
        "serialized checkpoint retains tutorial provider continuation");
    fixture.memory.Set(MoveDispatchFixture::object + 0x484, std::int32_t{44});
    fixture.memory.Set(MoveDispatchFixture::sub_elements, std::int32_t{99});
    fixture.memory.Fill(
        MoveDispatchFixture::sub_elements + 8, 24, std::byte{0x7A});
    expect(fixture.state.RestoreTransactional(baseline).ok(),
        "restore MoveDispatch semantic state");
    MoveDispatchImage restored{};
    expect(fixture.state.Capture(restored).ok() && restored == baseline,
        "recapture exact MoveDispatch semantic state");
    expect(fixture.memory.Get(MoveDispatchFixture::sub_elements + 8)
        == std::byte{0x7A}, "preserve MoveDispatch derived scratch bytes");

    const auto canonical = MoveDispatchState::CanonicalBytes(baseline);
    expect(!contains_qword(canonical, MoveDispatchFixture::frame_slots),
        "MoveDispatch canonical bytes exclude authored table pointer");
    expect(!contains_qword(canonical, MoveDispatchFixture::sub_elements),
        "MoveDispatch canonical bytes exclude subelement owner pointer");
}

void test_tutorial_consumer_interval_and_undo()
{
    MoveDispatchFixture fixture;
    constexpr auto owner = MoveDispatchFixture::object;
    constexpr auto masks = MoveDispatchFixture::memory_base + 0x19000;
    fixture.memory.Set(owner + 0x480, std::uint8_t{0});
    fixture.memory.Set(owner + 0x4A8, masks);
    fixture.memory.Set(owner + 0x4B0, std::int32_t{2});
    fixture.memory.Set(masks, std::uint64_t{0x110});
    fixture.memory.Set(masks + 8, std::uint64_t{1});
    expect(fixture.state.Bind(owner, 19).ok(), "bind tutorial interval fixture");
    const auto read = [&](std::uintptr_t address, auto& value) noexcept {
        return fixture.memory.Read(address, std::as_writable_bytes(std::span{&value, 1}));
    };
    TutorialConsumerObservation expected{}, observed{};
    expect(CaptureTutorialConsumerState(read, owner, expected.before),
        "capture actual tutorial consumer entry");
    expected.after = expected.before;
    expected.after.masks = {};
    expected.after.values[6] = 8;
    expected.parent.inert = true;
    expected.valid = true;
    struct Provider { std::uint32_t selected{2}; std::uint32_t calls{}; } provider;
    const auto query = [](void* context, std::uintptr_t,
        std::uint32_t& selected) noexcept {
        auto& provider = *static_cast<Provider*>(context);
        ++provider.calls;
        selected = provider.selected;
        return Status::success();
    };
    const auto before = fixture.memory.bytes();
    auto active = expected;
    active.before.mode = 5;
    expect(fixture.state.ReplayIdleConsumer(active, query, &provider, true, observed).code
            == FailureCode::RestorePreflightFailed && provider.calls == 0
            && fixture.memory.bytes() == before,
        "active tutorial mode rejects before provider queries or writes");
    fixture.memory.FailWrite(2);
    expect(fixture.state.ReplayIdleConsumer(expected, query, &provider, true, observed).code
            == FailureCode::RestoreWriteFailed && fixture.memory.bytes() == before,
        "consumer write failure restores event accumulator and provider continuation");
    provider.selected = 1;
    expect(fixture.state.ReplayIdleConsumer(expected, query, &provider, true, observed).code
            == FailureCode::StateHashMismatch && fixture.memory.bytes() == before,
        "different live provider result fails verification and undoes instead of injecting expectation");
    provider.selected = 2;
    expect(fixture.state.ReplayIdleConsumer(expected, query, &provider, true, observed).ok()
            && observed.after == expected.after,
        "native idle consumer clears events and advances matching provider once");
    const auto completed = fixture.memory.bytes();
    expect(fixture.state.ReplayIdleConsumer(expected, query, &provider, true, observed).code
            == FailureCode::StateHashMismatch && fixture.memory.bytes() == completed,
        "duplicate interval is rejected at its original entry boundary");
}

void test_move_dispatch_phase_drift_is_atomic()
{
    MoveDispatchFixture fixture;
    expect(fixture.state.Bind(MoveDispatchFixture::object, 19).ok(),
        "bind MoveDispatch phase-drift fixture");
    MoveDispatchImage baseline{};
    expect(fixture.state.Capture(baseline).ok(),
        "capture MoveDispatch phase-drift baseline");
    fixture.memory.Set(MoveDispatchFixture::object + 0x490, std::uint32_t{1});
    fixture.memory.Set(MoveDispatchFixture::object + 0x480,
        MoveDispatchFixture::memory_base + 0x19000);
    fixture.memory.Set(MoveDispatchFixture::object + 0x488, std::int32_t{0});
    fixture.memory.Set(MoveDispatchFixture::object + 0x48C, std::int32_t{2});
    const auto before = fixture.memory.bytes();
    expect(fixture.state.RestoreTransactional(baseline).code
            == FailureCode::IdentityMismatch,
        "MoveDispatch phase drift rejects restore");
    expect(fixture.memory.bytes() == before,
        "MoveDispatch phase drift performs zero mutation");
}

void test_move_dispatch_pending_phase_restore()
{
    MoveDispatchFixture fixture;
    constexpr auto pending_windows = MoveDispatchFixture::memory_base + 0x19000;
    fixture.memory.Set(MoveDispatchFixture::object + 0x490, std::uint32_t{1});
    fixture.memory.Set(MoveDispatchFixture::object + 0x480, pending_windows);
    fixture.memory.Set(MoveDispatchFixture::object + 0x488, std::int32_t{2});
    fixture.memory.Set(MoveDispatchFixture::object + 0x48C, std::int32_t{3});
    const MoveDispatchPendingWindow first{1, 2, 3, 4, 5, 6};
    const MoveDispatchPendingWindow second{7, 8, 9, 10, 11, 12};
    fixture.memory.Set(pending_windows, first);
    fixture.memory.Set(pending_windows + 0x20, second);

    expect(fixture.state.Bind(MoveDispatchFixture::object, 20).ok(),
        "bind MoveDispatch pending phase");
    MoveDispatchImage baseline{};
    expect(fixture.state.Capture(baseline).ok(),
        "capture MoveDispatch pending phase");
    fixture.memory.Set(pending_windows + 0x18, std::int32_t{77});
    fixture.memory.Set(MoveDispatchFixture::object + 0x488, std::int32_t{1});
    expect(fixture.state.RestoreTransactional(baseline).ok(),
        "restore MoveDispatch pending-window values and count");
    MoveDispatchImage restored{};
    expect(fixture.state.Capture(restored).ok() && restored == baseline,
        "recapture exact MoveDispatch pending phase");

    auto malformed = baseline;
    malformed.saved_input_and_gates = 0;
    const auto before = fixture.memory.bytes();
    expect(fixture.state.RestoreTransactional(malformed).code
            == FailureCode::RestorePreflightFailed,
        "reject inconsistent MoveDispatch phase tag");
    expect(fixture.memory.bytes() == before,
        "inconsistent MoveDispatch phase tag performs zero mutation");
    const auto canonical = MoveDispatchState::CanonicalBytes(baseline);
    expect(!contains_qword(canonical, pending_windows),
        "MoveDispatch canonical bytes exclude pending-window owner pointer");
}

void test_move_dispatch_partial_write_undoes_exactly()
{
    MoveDispatchFixture fixture;
    expect(fixture.state.Bind(MoveDispatchFixture::object, 19).ok(),
        "bind MoveDispatch undo fixture");
    MoveDispatchImage baseline{};
    expect(fixture.state.Capture(baseline).ok(),
        "capture MoveDispatch undo target");
    fixture.memory.Set(MoveDispatchFixture::object + 0x484, std::int32_t{80});
    fixture.memory.Set(MoveDispatchFixture::sub_elements, std::int32_t{81});
    fixture.memory.Set(MoveDispatchFixture::object + 0x4B8, std::uint32_t{1});
    fixture.memory.Set(MoveDispatchFixture::object + 0x4BC, std::uint32_t{23});
    const auto before = fixture.memory.bytes();
    fixture.memory.FailWrite(9); // Fail after provider state was written.
    expect(fixture.state.RestoreTransactional(baseline).code
            == FailureCode::RestoreWriteFailed,
        "MoveDispatch partial write reports typed failure");
    expect(fixture.memory.bytes() == before,
        "MoveDispatch partial write restores exact undo image");
}

void test_stage_break_listener_topology_is_value_only_and_bounded()
{
    constexpr std::uintptr_t base = 0x10000000;
    constexpr std::size_t image_size = 0x430000;
    FakeNativeMemory memory{base, 0x440000};
    constexpr auto wall = base + 0x1000;
    constexpr auto wall_emitter = wall + 0x3B0;
    constexpr auto wall_vtable = base + 0x8000;
    constexpr auto wall_callback = base + 0x9000;
    memory.Set(wall + 0x450, std::int32_t{7});
    memory.Set(wall_emitter + 0x40, std::uintptr_t{});
    memory.Set(wall_emitter + 0x50, std::int32_t{1});
    memory.Set(wall_emitter + 0x54, std::int32_t{1});
    memory.Set(wall_emitter, wall_vtable);
    memory.Set(wall_emitter + 0x20, std::uintptr_t{});
    memory.Set(wall_emitter + 0x30, std::int32_t{1});
    memory.Set(wall_vtable + 0x68, wall_callback);

    constexpr auto barrier = base + 0x3000;
    constexpr auto barrier_emitter = barrier + 0x390;
    constexpr auto heap_entries = base + 0x12000;
    constexpr auto listener_object = base + 0x15000;
    constexpr auto barrier_vtable = base + 0x8100;
    constexpr auto barrier_callback = base + 0x9100;
    memory.Set(barrier + 0x420, std::int32_t{9});
    memory.Set(barrier_emitter + 0x40, heap_entries);
    memory.Set(barrier_emitter + 0x50, std::int32_t{2});
    memory.Set(barrier_emitter + 0x54, std::int32_t{2});
    memory.Set(heap_entries + 0x30, std::int32_t{});
    memory.Set(heap_entries + 0x40 + 0x20, listener_object);
    memory.Set(heap_entries + 0x40 + 0x30, std::int32_t{1});
    memory.Set(listener_object, barrier_vtable);
    memory.Set(barrier_vtable + 0x68, barrier_callback);

    StageBreakListenerTopologyProbe probe{memory};
    const std::array actors{
        StageBreakActorRef{StageBreakActorKind::Wall, wall},
        StageBreakActorRef{StageBreakActorKind::Barrier, barrier},
    };
    StageBreakListenerTopology topology{};
    expect(probe.Capture(base, image_size, actors, topology).ok(),
        "capture bounded stage-break listener topology");
    expect(topology.actors.size() == 2 && topology.listeners.size() == 2,
        "retain actors and only active listeners");
    expect(topology.listeners[0].actor_id == 7
            && topology.listeners[0].slot_index == 0
            && topology.listeners[0].listener_vtable_rva == 0x8000
            && topology.listeners[0].callback_rva == 0x9000,
        "inline wall listener becomes module-relative values");
    expect(topology.listeners[1].actor_id == 9
            && topology.listeners[1].dispatch_order == 0
            && topology.listeners[1].slot_index == 1
            && topology.listeners[1].listener_vtable_rva == 0x8100
            && topology.listeners[1].callback_rva == 0x9100,
        "heap listener preserves reverse dispatch order without pointers");

    const std::array repeated_actors{
        StageBreakActorRef{StageBreakActorKind::Wall, wall},
        StageBreakActorRef{StageBreakActorKind::Barrier, barrier},
        StageBreakActorRef{StageBreakActorKind::Wall, wall},
    };
    StageBreakListenerTopology repeated_topology{};
    expect(probe.Capture(base, image_size, repeated_actors, repeated_topology).ok()
            && repeated_topology.actors.size() == 3
            && repeated_topology.listeners.size() == 3
            && repeated_topology.actors[0].repeated_reference_of
                == no_repeated_actor_reference
            && repeated_topology.actors[2].actor_id == 7
            && repeated_topology.actors[2].repeated_reference_of == 0,
        "ordered native list may repeat an actor reference without exposing its pointer");

    constexpr auto weak_delegate_wrapper = base + 0x41D870;
    constexpr auto bound_callback = base + 0xA000;
    memory.Set(wall_vtable + 0x68, weak_delegate_wrapper);
    memory.Set(wall_emitter + 0x10, bound_callback);
    StageBreakListenerTopology bound_topology{};
    expect(probe.Capture(base, image_size, actors, bound_topology).ok()
            && bound_topology.listeners[0].callback_rva == 0x41D870
            && bound_topology.listeners[0].bound_callback_rva == 0xA000
            && bound_topology.listeners[1].bound_callback_rva
                == no_bound_stage_break_callback,
        "verified weak-delegate wrapper exposes its value-only bound callback RVA");
    memory.Set(wall_vtable + 0x68, wall_callback);

    const auto first_signature = topology.signature;
    memory.Set(barrier_vtable + 0x68, base + 0x9200);
    expect(probe.Capture(base, image_size, actors, topology).ok()
            && topology.signature != first_signature,
        "callback target drift changes the value-only signature");

    const auto callback_drift_signature = topology.signature;
    memory.Set(heap_entries + 0x40 + 0x30, std::int32_t{});
    expect(probe.Capture(base, image_size, actors, topology).ok()
            && topology.actors.size() == 2
            && topology.listeners.size() == 1
            && topology.signature != callback_drift_signature,
        "actors with no active listeners remain visible in topology");

    memory.Set(heap_entries + 0x40 + 0x30, std::int32_t{1});
    memory.Set(barrier_vtable + 0x68, base + image_size + 0x100);
    StageBreakListenerProbeFailure failure{};
    expect(probe.Capture(base, image_size, actors, topology, &failure).code
            == FailureCode::IdentityMismatch
            && topology.listeners.empty()
            && failure.fault == StageBreakListenerProbeFault::CallbackOutsideImage
            && failure.actor_order == 1
            && failure.slot_index == 1,
        "callback targets outside the executable image fail closed");
}

void test_stage_break_presentation_identity_is_generation_scoped()
{
    constexpr std::uintptr_t wall = 0x10001000;
    constexpr std::uintptr_t barrier = 0x10002000;
    constexpr std::uintptr_t wall_asset = 0x20001000;
    constexpr std::uintptr_t hit_asset = 0x20002000;
    constexpr std::uintptr_t break_asset = 0x20003000;
    const std::array actors{
        StageBreakActorRef{StageBreakActorKind::Wall, wall},
        StageBreakActorRef{StageBreakActorKind::Barrier, barrier},
        StageBreakActorRef{StageBreakActorKind::Barrier, barrier},
    };
    StageBreakListenerTopology topology{};
    topology.signature = 0x12345678;
    topology.actors = {
        {StageBreakActorKind::Wall, 7, 0, no_repeated_actor_reference},
        {StageBreakActorKind::Barrier, 9, 1, no_repeated_actor_reference},
        {StageBreakActorKind::Barrier, 9, 2, 1},
    };
    const std::array assets{
        StageBreakParticleAssetRef{wall, ParticleRoute::WallBreak, 0, wall_asset},
        StageBreakParticleAssetRef{barrier, ParticleRoute::BarrierHit, 0, hit_asset},
        // A repeated template slot is one logical native asset identity.
        StageBreakParticleAssetRef{barrier, ParticleRoute::BarrierHit, 1, hit_asset},
        StageBreakParticleAssetRef{barrier, ParticleRoute::BarrierBreak, 0, break_asset},
    };

    StageBreakPresentationIdentityMap identities{};
    expect(identities.Bind(11, actors, topology, assets).ok()
            && identities.bound() && identities.generation() == 11
            && identities.topology_signature() == topology.signature,
        "seal bounded stage-break presentation identity topology");
    StageBreakPresentationIdentity wall_identity{};
    StageBreakPresentationIdentity hit_identity{};
    StageBreakPresentationIdentity break_identity{};
    expect(identities.Resolve(11, wall, ParticleRoute::WallBreak,
                wall_asset, wall_identity).ok()
            && identities.Resolve(11, barrier, ParticleRoute::BarrierHit,
                hit_asset, hit_identity).ok()
            && identities.Resolve(11, barrier, ParticleRoute::BarrierBreak,
                break_asset, break_identity).ok()
            && wall_identity.owner_logical_id != wall
            && hit_identity.owner_logical_id != barrier
            && hit_identity.asset_logical_id != hit_asset
            && hit_identity.owner_logical_id == break_identity.owner_logical_id
            && hit_identity.asset_logical_id != break_identity.asset_logical_id,
        "resolve route-qualified pointer-free owner and asset identities");
    std::uint64_t resolved_owner{};
    std::uintptr_t resolved_actor{};
    std::uintptr_t resolved_asset{};
    expect(identities.ResolveActor(11, barrier, resolved_owner).ok()
            && resolved_owner == hit_identity.owner_logical_id
            && identities.ResolveActorAddress(11, resolved_owner,
                StageBreakActorKind::Barrier, resolved_actor).ok()
            && resolved_actor == barrier
            && identities.ResolveAssetAddress(11, resolved_owner,
                hit_identity.asset_logical_id, ParticleRoute::BarrierHit,
                resolved_actor, resolved_asset).ok()
            && resolved_actor == barrier && resolved_asset == hit_asset,
        "reverse logical stage identities only within their native generation");
    StageBreakPresentationIdentity rejected{};
    expect(identities.Resolve(12, barrier, ParticleRoute::BarrierHit,
                hit_asset, rejected).code == FailureCode::GenerationMismatch
            && rejected.owner_logical_id == 0
            && identities.Resolve(11, barrier, ParticleRoute::WallBreak,
                hit_asset, rejected).code == FailureCode::UnsupportedContent,
        "generation drift and cross-route aliases fail closed");
    expect(identities.ResolveActorAddress(12, resolved_owner,
                StageBreakActorKind::Barrier, resolved_actor).code
                == FailureCode::GenerationMismatch
            && resolved_actor == 0
            && identities.ResolveAssetAddress(11, resolved_owner,
                hit_identity.asset_logical_id, ParticleRoute::WallBreak,
                resolved_actor, resolved_asset).code
                == FailureCode::UnsupportedContent
            && resolved_actor == 0 && resolved_asset == 0,
        "reverse logical identity rejects generation and route drift");

    auto replacement_actors = actors;
    replacement_actors[1].address = 0x10004000;
    replacement_actors[2].address = 0x10004000;
    expect(identities.Bind(12, replacement_actors, topology, {}).ok()
            && identities.Resolve(11, barrier, ParticleRoute::BarrierHit,
                hit_asset, rejected).code == FailureCode::GenerationMismatch
            && identities.Resolve(12, barrier, ParticleRoute::BarrierHit,
                hit_asset, rejected).code == FailureCode::UnsupportedContent,
        "allocation replacement atomically revokes prior native bindings");

    auto invalid_topology = topology;
    invalid_topology.actors[2].repeated_reference_of = 0;
    expect(identities.Bind(13, actors, invalid_topology, assets).code
            == FailureCode::IdentityMismatch
            && !identities.bound(),
        "invalid repeated-reference topology leaves the identity map unbound");
}

void test_stage_break_particle_asset_capture_is_bounded_and_atomic()
{
    constexpr std::uintptr_t base = 0x30000000;
    constexpr std::uintptr_t wall = base + 0x1000;
    constexpr std::uintptr_t barrier = base + 0x2000;
    constexpr std::uintptr_t hit_array = base + 0x5000;
    constexpr std::uintptr_t wall_asset = base + 0x7000;
    constexpr std::uintptr_t hit_asset0 = base + 0x7100;
    constexpr std::uintptr_t hit_asset1 = base + 0x7200;
    constexpr std::uintptr_t break_asset = base + 0x7300;
    struct NativeArray
    {
        std::uintptr_t data;
        std::int32_t count;
        std::int32_t capacity;
    };

    FakeNativeMemory memory{base, 0x10000};
    memory.Set(wall + 0x458, wall_asset);
    memory.Set(barrier + 0x450, NativeArray{hit_array, 3, 3});
    memory.Set(hit_array, hit_asset0);
    memory.Set(hit_array + 8, std::uintptr_t{});
    memory.Set(hit_array + 16, hit_asset1);
    memory.Set(barrier + 0x460, break_asset);
    const std::array actors{
        StageBreakActorRef{StageBreakActorKind::Wall, wall},
        StageBreakActorRef{StageBreakActorKind::Barrier, barrier},
    };
    std::array<StageBreakParticleAssetRef,
        StageBreakPresentationIdentityMap::maximum_assets> assets{};
    std::size_t asset_count{};
    expect(CaptureStageBreakParticleAssets(
               memory, actors, assets, asset_count).ok()
            && asset_count == 4
            && assets[0].actor_address == wall
            && assets[0].route == ParticleRoute::WallBreak
            && assets[0].asset_ordinal == 0
            && assets[0].asset_address == wall_asset
            && assets[1].actor_address == barrier
            && assets[1].route == ParticleRoute::BarrierHit
            && assets[1].asset_ordinal == 0
            && assets[1].asset_address == hit_asset0
            && assets[2].route == ParticleRoute::BarrierHit
            && assets[2].asset_ordinal == 2
            && assets[2].asset_address == hit_asset1
            && assets[3].route == ParticleRoute::BarrierBreak
            && assets[3].asset_ordinal == 0
            && assets[3].asset_address == break_asset,
        "capture native stage-break assets with stable route ordinals");

    const auto prior_assets = assets;
    const auto prior_count = asset_count;
    memory.Set(barrier + 0x450, NativeArray{hit_array, 4, 3});
    const auto assets_unchanged = [&]() noexcept {
        for (std::size_t index = 0; index < assets.size(); ++index)
        {
            if (assets[index].actor_address
                    != prior_assets[index].actor_address
                || assets[index].route != prior_assets[index].route
                || assets[index].asset_ordinal
                    != prior_assets[index].asset_ordinal
                || assets[index].asset_address
                    != prior_assets[index].asset_address)
                return false;
        }
        return true;
    };
    expect(CaptureStageBreakParticleAssets(
               memory, actors, assets, asset_count).code
                == FailureCode::ContextUnavailable
            && asset_count == prior_count && assets_unchanged(),
        "malformed native asset arrays leave the prior binding unchanged");
}

Status resolve_fake_callback_owner(
    void*, std::int32_t object_index, std::int32_t serial_number,
    std::uint64_t& class_token) noexcept
{
    if (object_index <= 0 || serial_number <= 0)
        return Status::failure(FailureCode::IdentityMismatch);
    class_token = (static_cast<std::uint64_t>(object_index) << 32)
        | static_cast<std::uint32_t>(serial_number);
    return Status::success();
}

void test_callback_topology_is_generation_bound_and_pointer_free()
{
    constexpr std::uintptr_t base = 0x20000000;
    constexpr std::size_t image_size = 0x10000;
    FakeNativeMemory memory{base, 0x20000};
    constexpr auto input = base + 0x1000;
    memory.Set(input, base + 0x5000);
    memory.Set(input + 0x08, std::int32_t{11});
    memory.Set(input + 0x0C, std::int32_t{21});
    memory.Set(input + 0x10, base + 0x6000);
    memory.Set(input + 0x20, std::uintptr_t{});
    memory.Set(input + 0x30, std::int32_t{1});
    memory.Set(input + 0x40, std::uintptr_t{});
    memory.Set(input + 0x50, std::int32_t{1});
    memory.Set(input + 0x54, std::int32_t{1});

    constexpr auto round = base + 0x2000;
    constexpr auto entries = base + 0x11000;
    constexpr auto override_callback = base + 0x15000;
    memory.Set(round + 0x40, entries);
    memory.Set(round + 0x50, std::int32_t{2});
    memory.Set(round + 0x54, std::int32_t{2});
    memory.Set(entries, base + 0x5100);
    memory.Set(entries + 0x08, std::int32_t{12});
    memory.Set(entries + 0x0C, std::int32_t{22});
    memory.Set(entries + 0x10, base + 0x6100);
    memory.Set(entries + 0x20, std::uintptr_t{});
    memory.Set(entries + 0x30, std::int32_t{1});
    memory.Set(entries + 0x40 + 0x20, override_callback);
    memory.Set(entries + 0x40 + 0x30, std::int32_t{1});
    memory.Set(override_callback, base + 0x5200);
    memory.Set(override_callback + 0x08, std::int32_t{13});
    memory.Set(override_callback + 0x0C, std::int32_t{23});
    memory.Set(override_callback + 0x10, base + 0x6200);

    const std::array collections{
        CallbackCollectionRef{CallbackCollectionRole::InputFilter, input},
        CallbackCollectionRef{CallbackCollectionRole::Round, round},
    };
    CallbackTopologyProbe probe{memory};
    CallbackTopology topology{};
    expect(probe.Capture(base, image_size, collections,
            &resolve_fake_callback_owner, nullptr, topology).ok()
            && topology.records.size() == 3
            && topology.records[0].wrapper_vtable_rva == 0x5000
            && topology.records[0].callback_rva == 0x6000
            && topology.records[1].dispatch_order == 1
            && topology.records[2].dispatch_order == 0
            && topology.records[2].owner_class_token
                == ((std::uint64_t{13} << 32) | 23),
        "callback topology retains only ordered weak generations, class tokens, and RVAs");

    const auto signature = topology.signature;
    memory.Set(override_callback + 0x10, base + 0x6300);
    expect(probe.Capture(base, image_size, collections,
            &resolve_fake_callback_owner, nullptr, topology).ok()
            && topology.signature != signature,
        "callback target drift changes the binding signature");
    memory.Set(entries + 0x40 + 0x30, std::int32_t{});
    expect(probe.Capture(base, image_size, collections,
            &resolve_fake_callback_owner, nullptr, topology).code
            == FailureCode::IdentityMismatch,
        "inactive callback entries fail binding closed before capture");
}

void test_stage_wind_topology_is_bounded_and_pointer_free()
{
    constexpr std::uintptr_t base = 0x30000000;
    constexpr std::uintptr_t image_base = 0x140000000;
    FakeNativeMemory memory{base, 0x20000};
    constexpr auto root_pointer = base + 0x100;
    constexpr auto root = base + 0x1000;
    constexpr auto first = base + 0x3000;
    constexpr auto second = base + 0x5000;
    memory.Set(root_pointer, root);
    memory.Set(root, first);
    memory.Fill(root + 0x08, 12, std::byte{0x11});
    memory.Set(root + 0x18, image_base + std::uintptr_t{0x334430});
    memory.Set(root + 0x98, std::uint32_t{0});
    memory.Set(root + 0x9C, std::int32_t{1});
    memory.Fill(root + 0xA0, 0x10, std::byte{0x22});
    memory.Fill(root + 0xB0, 0x10, std::byte{0x33});
    memory.Fill(root + 0xC0, 0x30, std::byte{0x44});

    memory.Set(first, image_base + std::uintptr_t{0x3E88C88});
    memory.Set(first + 0x10, second);
    memory.Set(first + 0x18, std::uintptr_t{});
    memory.Set(first + 0x28, root);
    memory.Fill(first + 0x20, 2, std::byte{0x51});
    memory.Fill(first + 0x30, 4, std::byte{0x52});
    memory.Fill(first + 0x40, 0x30, std::byte{0x53});
    memory.Fill(first + 0x70, 0x70, std::byte{0x54});
    memory.Fill(first + 0x120, 0x0C, std::byte{0x55});

    memory.Set(second, image_base + std::uintptr_t{0x3E88D18});
    memory.Set(second + 0x10, std::uintptr_t{});
    memory.Set(second + 0x18, first);
    memory.Set(second + 0x28, root);
    memory.Fill(second + 0x20, 2, std::byte{0x61});
    memory.Fill(second + 0x30, 4, std::byte{0x62});
    memory.Fill(second + 0x40, 0x30, std::byte{0x63});
    memory.Fill(second + 0x70, 0xA0, std::byte{0x64});
    memory.Fill(second + 0x120, 0x0C, std::byte{0x65});
    memory.Fill(second + 0x130, 0x50, std::byte{0x66});

    StageWindTopologyProbe probe{memory};
    const StageWindTopologyAddresses addresses{
        image_base, 0x4300000, root_pointer, 9};
    StageWindTopologyImage image{};
    expect(probe.Bind(addresses).ok() && probe.Capture(image).ok()
            && image.nodes.size() == 2
            && image.nodes[0].kind == StageWindNodeKind::Parallel
            && image.nodes[1].kind == StageWindNodeKind::ShockWave
            && image.pending_callback_rvas[0] == 0x334430,
        "wind topology captures ordered value-only node classes and callback RVAs");
    const auto* node_storage = image.nodes.data();
    const auto* first_semantic_storage = image.nodes[0].semantic_state.data();
    const auto* first_derived_storage = image.nodes[0].derived_state.data();
    const auto node_capacity = image.nodes.capacity();
    const auto semantic_capacity = image.nodes[0].semantic_state.capacity();
    const auto derived_capacity = image.nodes[0].derived_state.capacity();
    expect(probe.Capture(image).ok()
            && image.nodes.data() == node_storage
            && image.nodes.capacity() == node_capacity
            && image.nodes[0].semantic_state.data() == first_semantic_storage
            && image.nodes[0].semantic_state.capacity() == semantic_capacity
            && image.nodes[0].derived_state.data() == first_derived_storage
            && image.nodes[0].derived_state.capacity() == derived_capacity,
        "repeated wind capture reuses every bounded topology buffer");
    const auto canonical = StageWindTopologyProbe::CanonicalBytes(image);
    expect(!contains_qword(canonical, root) && !contains_qword(canonical, first)
            && !contains_qword(canonical, second),
        "wind canonical bytes contain no native root or node pointer");
    std::vector<std::byte> rejected{std::byte{0x7f}};
    auto invalid_schedule = image;
    const std::uint32_t invalid_bank = 2;
    std::memcpy(invalid_schedule.schedule_state.data(), &invalid_bank,
        sizeof(invalid_bank));
    expect(StageWindTopologyProbe::CanonicalBytes(
                invalid_schedule, rejected).code
                == FailureCode::IdentityMismatch
            && rejected.empty(),
        "invalid wind schedules fail before emitting canonical bytes");
    const std::uint32_t valid_bank = 0;
    const std::int32_t negative_count = -1;
    std::memcpy(invalid_schedule.schedule_state.data(), &valid_bank,
        sizeof(valid_bank));
    std::memcpy(invalid_schedule.schedule_state.data() + 4,
        &negative_count, sizeof(negative_count));
    rejected.assign(1, std::byte{0x7f});
    expect(!StageWindTopologyProbe::CanonicalBytes(
                invalid_schedule, rejected).ok()
            && rejected.empty(),
        "negative wind callback counts cannot leave canonical prefixes");
    const std::int32_t oversized_count = 9;
    std::memcpy(invalid_schedule.schedule_state.data() + 4,
        &oversized_count, sizeof(oversized_count));
    rejected.assign(1, std::byte{0x7f});
    expect(!StageWindTopologyProbe::CanonicalBytes(
                invalid_schedule, rejected).ok()
            && rejected.empty(),
        "oversized wind callback counts cannot leave canonical prefixes");

    auto equivalent_bank_variant = image;
    const std::uint32_t alternate_bank = 1;
    std::memcpy(equivalent_bank_variant.schedule_state.data(),
        &alternate_bank, sizeof(alternate_bank));
    equivalent_bank_variant.pending_callback_rvas[8] =
        image.pending_callback_rvas[0];
    equivalent_bank_variant.pending_callback_rvas[0] = 0x123456;
    equivalent_bank_variant.pending_callback_rvas[7] = 0x654321;
    expect(StageWindTopologyProbe::CanonicalBytes(equivalent_bank_variant)
            == canonical,
        "wind canonical bytes normalize the symmetric callback bank and exclude stale slots");
    equivalent_bank_variant.pending_callback_rvas[8] = 0x334431;
    expect(StageWindTopologyProbe::CanonicalBytes(equivalent_bank_variant)
            != canonical,
        "wind canonical bytes retain exact ordered active pending callback identity");

    auto root_residue_variant = image;
    root_residue_variant.root_clock[8] = std::byte{0xA5};
    expect(StageWindTopologyProbe::CanonicalBytes(root_residue_variant)
            == canonical,
        "wind canonical bytes exclude the verified unwritten root +0x10 word");
    auto root_live_variant = image;
    root_live_variant.root_clock[0] = std::byte{0xA5};
    expect(StageWindTopologyProbe::CanonicalBytes(root_live_variant)
            != canonical,
        "wind canonical bytes retain live root strength and scene-tick state");

    const auto canonical_before_residue = canonical;
    memory.Fill(first + 0x34, 0x0C, std::byte{0x7A});
    expect(probe.Capture(image).ok()
            && StageWindTopologyProbe::CanonicalBytes(image)
                == canonical_before_residue,
        "wind canonical bytes exclude base allocator residue");
    memory.Fill(first + 0x30, 4, std::byte{0x7B});
    expect(probe.Capture(image).ok()
            && StageWindTopologyProbe::CanonicalBytes(image)
                != canonical_before_residue,
        "wind lifecycle changes alter canonical state");

    const auto canonical_before_shock_presentation =
        StageWindTopologyProbe::CanonicalBytes(image);
    memory.Fill(second + 0xD0, 0x10, std::byte{0x81});
    memory.Fill(second + 0xF0, 0x20, std::byte{0x82});
    memory.Fill(second + 0x120, 0x0C, std::byte{0x83});
    memory.Fill(second + 0x130, 0x50, std::byte{0x84});
    expect(probe.Capture(image).ok()
            && StageWindTopologyProbe::CanonicalBytes(image)
                == canonical_before_shock_presentation,
        "shock-wave force geometry remains local-restorable but non-canonical");
    memory.Fill(second + 0xCC, 4, std::byte{0x85});
    expect(probe.Capture(image).ok()
            && StageWindTopologyProbe::CanonicalBytes(image)
                != canonical_before_shock_presentation,
        "shock-wave conditional RNG threshold remains canonical");

    memory.Set(second + 0x10, first);
    expect(probe.Capture(image).code == FailureCode::IdentityMismatch,
        "wind topology rejects cycles");
    memory.Set(second + 0x10, std::uintptr_t{});
    memory.Set(second, image_base + std::uintptr_t{0x3E88000});
    expect(probe.Capture(image).code == FailureCode::AdapterUnqualified,
        "wind topology rejects unknown node vtables");
    memory.Set(second, image_base + std::uintptr_t{0x3E88D18});
    memory.Set(second + 0x18, std::uintptr_t{});
    expect(probe.Capture(image).code == FailureCode::IdentityMismatch,
        "wind topology rejects broken reverse links");
}

class FixedStageWindAllocator final : public IStageWindAllocator
{
public:
    explicit FixedStageWindAllocator(std::uintptr_t next) : next_(next) {}

    std::uintptr_t Allocate(std::size_t size) noexcept override
    {
        ++allocation_calls_;
        if (fail_allocation_ == allocation_calls_) return 0;
        const auto result = next_;
        next_ += (size + 0xFF) & ~std::size_t{0xFF};
        allocations.push_back(result);
        return result;
    }

    void Free(std::uintptr_t address) noexcept override
    {
        frees.push_back(address);
    }

    void FailAllocation(std::size_t call) noexcept
    {
        allocation_calls_ = 0;
        fail_allocation_ = call;
    }

    std::vector<std::uintptr_t> allocations;
    std::vector<std::uintptr_t> frees;

private:
    std::uintptr_t next_{};
    std::size_t allocation_calls_{};
    std::size_t fail_allocation_{};
};

void test_stage_wind_graph_restore_is_transactional()
{
    constexpr std::uintptr_t base = 0x31000000;
    constexpr std::uintptr_t image_base = 0x140000000;
    constexpr auto root_pointer = base + 0x100;
    constexpr auto root = base + 0x1000;
    constexpr auto old_node = base + 0x3000;
    FakeNativeMemory memory{base, 0x40000};
    const auto* ring_out_layout = FindStageWindNodeLayout(StageWindNodeKind::RingOut);
    expect(ring_out_layout != nullptr && ring_out_layout->allocation_size == 0x130
            && ring_out_layout->class_ranges.back().offset
                + ring_out_layout->class_ranges.back().size <= 0x130,
        "ring-out layout is bounded by the assembly-proven 0x130 allocation");
    memory.Set(root_pointer, root);
    memory.Set(root, old_node);
    memory.Fill(root + 0x08, 12, std::byte{0x11});
    memory.Set(root + 0x98, std::uint32_t{});
    memory.Set(root + 0x9C, std::int32_t{});
    memory.Fill(root + 0xA8, 8, std::byte{0x2f});
    memory.Fill(root + 0xB0, 0x10, std::byte{0x33});
    memory.Fill(root + 0xC0, 0x30, std::byte{0x44});
    memory.Set(old_node, image_base + std::uintptr_t{0x3E88CE8});
    memory.Set(old_node + 0x10, std::uintptr_t{});
    memory.Set(old_node + 0x18, std::uintptr_t{});
    memory.Set(old_node + 0x28, root);
    memory.Fill(old_node + 0x20, 2, std::byte{0x21});
    memory.Fill(old_node + 0x30, 4, std::byte{0x22});
    memory.Fill(old_node + 0x40, 0x30, std::byte{0x23});
    memory.Fill(old_node + 0x70, 0x84, std::byte{0x24});
    memory.Fill(old_node + 0xF8, 0x24, std::byte{0x25});
    memory.Fill(old_node + 0x120, 0x10, std::byte{0x26});
    memory.Fill(old_node + 0x130, 0x04, std::byte{0x27});
    memory.Fill(old_node + 0x134, 0x10, std::byte{0x28});
    memory.Fill(old_node + 0x148, 0x04, std::byte{0x29});
    memory.Fill(old_node + 0x150, 0x90, std::byte{0x2A});

    const StageWindTopologyAddresses addresses{
        image_base, 0x4300000, root_pointer, 10};
    StageWindTopologyProbe probe{memory};
    StageWindTopologyImage target{};
    expect(probe.Bind(addresses).ok() && probe.Capture(target).ok(),
        "wind transaction fixture captures a qualified source graph");
    const auto canonical_before_derived_change =
        StageWindTopologyProbe::CanonicalBytes(target);
    target.root_clock[0] = std::byte{0x7A};
    target.root_unknown_a8[3] = std::byte{0x6f};
    target.nodes[0].semantic_state[0] = std::byte{0x6A};
    target.nodes[0].derived_state[0] = std::byte{0x5A};
    auto canonical_without_derived = target;
    canonical_without_derived.nodes[0].derived_state[0] = std::byte{0x4A};
    expect(StageWindTopologyProbe::CanonicalBytes(target)
            == StageWindTopologyProbe::CanonicalBytes(canonical_without_derived)
            && StageWindTopologyProbe::CanonicalBytes(target)
                != canonical_before_derived_change,
        "wind canonical bytes exclude local ring-in matrices and travel state");

    FixedStageWindAllocator allocator{base + 0x10000};
    StageWindGraphTransaction transaction{memory, allocator};
    const auto memory_before_admission = memory.bytes();
    expect(transaction.Prepare(addresses, target, true, 0).code == FailureCode::CapacityExceeded
            && allocator.allocations.empty() && allocator.frees.empty()
            && memory.bytes() == memory_before_admission && !transaction.pending(),
        "wind capacity rejection precedes native allocation and leaves B installed");
    expect(transaction.Restore(addresses, target).ok(),
        "wind graph restore commits a verified replacement graph");
    StageWindTopologyImage restored{};
    expect(probe.Capture(restored).ok() && restored == target,
        "wind graph replacement reconstructs the exact pointer-free target");
    expect(allocator.frees.size() == 1 && allocator.frees[0] == old_node,
        "wind graph commit frees the detached old graph only after verification");

    const auto committed_head = allocator.allocations.back();
    StageWindTopologyImage second_target = target;
    second_target.root_clock[1] = std::byte{0x5A};
    FixedStageWindAllocator failing_allocator{base + 0x20000};
    StageWindGraphTransaction failing_transaction{memory, failing_allocator};
    memory.FailWrite(2); // one replacement-node write, then the root publication
    expect(failing_transaction.Restore(addresses, second_target).code
            == FailureCode::RestoreWriteFailed,
        "wind graph root publication failure aborts the transaction");
    memory.AllowWrites();
    expect(probe.Capture(restored).ok() && restored == target,
        "wind graph publication failure leaves the committed graph unchanged");
    expect(failing_allocator.frees.size() == 1
            && failing_allocator.frees[0] != committed_head,
        "wind graph publication failure frees only unpublished replacements");

    FixedStageWindAllocator corrupting_allocator{base + 0x28000};
    StageWindGraphTransaction corrupting_transaction{memory, corrupting_allocator};
    memory.CorruptAfterWrite(2, base + 0x28000 + 0x20, std::byte{0xEE});
    expect(corrupting_transaction.Restore(addresses, second_target).code
            == FailureCode::RestoreVerificationFailed,
        "wind graph post-publication verification failure reports restore failure");
    memory.AllowWrites();
    expect(probe.Capture(restored).ok() && restored == target,
        "wind graph post-publication verification restores the exact old root");
    expect(corrupting_allocator.frees.size() == 1
            && corrupting_allocator.frees[0] == base + 0x28000,
        "wind graph verification failure retires only the rejected replacement");

    FixedStageWindAllocator exhausted_allocator{base + 0x30000};
    exhausted_allocator.FailAllocation(1);
    StageWindGraphTransaction exhausted_transaction{memory, exhausted_allocator};
    expect(exhausted_transaction.Restore(addresses, second_target).code
            == FailureCode::CapacityExceeded
            && probe.Capture(restored).ok() && restored == target,
        "wind graph allocation failure is mutation-free");

    StageWindTopologyImage invalid_schedule = target;
    std::uint32_t invalid_bank = 2;
    std::memcpy(invalid_schedule.schedule_state.data(), &invalid_bank,
        sizeof(invalid_bank));
    FixedStageWindAllocator unused_allocator{base + 0x38000};
    StageWindGraphTransaction invalid_transaction{memory, unused_allocator};
    expect(invalid_transaction.Restore(addresses, invalid_schedule).code
            == FailureCode::RestorePreflightFailed
            && unused_allocator.allocations.empty()
            && probe.Capture(restored).ok() && restored == target,
        "wind graph invalid callback-bank state fails before allocation or mutation");

    FixedStageWindAllocator retained_allocator{base + 0x38000};
    StageWindGraphTransaction retained_transaction{memory, retained_allocator};
    expect(retained_transaction.Prepare(addresses, second_target, true).ok(),
        "enclosing wind prepares A without publishing or freeing B");
    expect(probe.Capture(restored).ok() && restored == target && retained_allocator.frees.empty(),
        "wind preparation preserves exact B");
    memory.FailPartialWrite(1, 8, true);
    expect(retained_transaction.Publish().code == FailureCode::RestoreWriteFailed,
        "wind publication detects a writer that changes the head then fails");
    expect(retained_transaction.Undo().code == FailureCode::UndoFailed
        && retained_transaction.published() && retained_allocator.frees.empty(),
        "partial B undo retains both graphs and remains retryable");
    memory.AllowWrites();
    retained_allocator.FailAllocation(1);
    const auto allocated = retained_allocator.allocations.size();
    expect(retained_transaction.Undo().ok() && retained_transaction.Undo().ok()
        && retained_allocator.allocations.size() == allocated
        && probe.Capture(restored).ok() && restored == target,
        "wind undo is allocation-free, idempotent and independently recovers B values");
    std::uintptr_t recovered_head{};
    expect(memory.Read(root, std::as_writable_bytes(std::span{&recovered_head, 1}))
        && recovered_head == committed_head, "wind undo recovers original B allocation identity");
    expect(retained_transaction.Commit().ok() && retained_allocator.frees.size() == 1
        && retained_allocator.frees[0] != committed_head && !retained_transaction.pending(),
        "recovered wind retires only private A after verification");

    FixedStageWindAllocator executing_allocator{base+0x20000};
    StageWindGraphTransaction executing_transaction{memory,executing_allocator};
    expect(executing_transaction.Prepare(addresses,second_target,true).ok()
        && executing_transaction.Publish().ok(),"wind execution starts from published A with private B");
    expect(executing_transaction.BeginExecution(0).code==FailureCode::CapacityExceeded
        && executing_transaction.ValidateCommit().ok(),"wind execution budget rejection preserves held transaction");
    expect(executing_transaction.BeginExecution(executing_transaction.AllocationEnvelopeBytes()).ok()
        && !executing_transaction.Undo().ok() && !executing_transaction.Commit().ok(),
        "wind execution forbids retirement until current native graph settles");
    const auto a_head=executing_allocator.allocations.back();
    constexpr auto c_head=base+0x24000;
    std::array<std::byte,0x1e0> native_c{};
    expect(memory.Read(a_head,native_c) && memory.Write(c_head,native_c),"native wind fixture replaces A node during execution");
    executing_allocator.Free(a_head);memory.Fill(a_head,native_c.size(),std::byte{0xdd});
    memory.Set(root,c_head);memory.Fill(root+8,12,std::byte{0x59});
    memory.Fill(c_head+0x20,2,std::byte{0x51});
    memory.Set(root,committed_head);
    expect(!executing_transaction.SettleExecution().ok() && executing_allocator.frees.size()==1,
        "wind settlement rejects a current graph aliasing private B without freeing either graph");
    memory.Set(root,c_head);
    expect(executing_transaction.SettleExecution().ok(),"wind settlement observes changed values and replacement C address");
    expect(executing_transaction.ReopenExecutionForUndo().ok()
        && executing_transaction.ReopenExecutionForUndo().ok()
        && executing_allocator.frees.size()==1 && !executing_transaction.Undo().ok(),
        "wind late cancellation relinquishes only C metadata and keeps B through idempotent reopening");
    memory.Fill(c_head+0x20,2,std::byte{0x52});
    expect(executing_transaction.SettleExecution().ok(),
        "wind cancellation recaptures actual C after native teardown work before restoring B");
    memory.FailPartialWrite(1,8,true);
    expect(executing_transaction.Undo().code==FailureCode::UndoFailed && executing_allocator.frees.size()==1,
        "wind post-execution partial B publication retains C and B for retry");
    memory.AllowWrites();
    expect(executing_transaction.Undo().ok() && probe.Capture(restored).ok() && restored==target,
        "wind post-execution recovery reproduces independently captured B values");
    expect(executing_transaction.Commit().ok() && executing_allocator.frees.size()==2
        && executing_allocator.frees[0]==a_head && executing_allocator.frees[1]==c_head,
        "wind recovery retires actual C exactly once and never the dead A or retained B");
}

class RetryingPresentationSink final : public IPresentationSink
{
public:
    Status Publish(const PresentationEvent& event) noexcept override
    {
        ++attempts;
        if (fail_attempt != 0 && attempts == fail_attempt)
            return Status::failure(FailureCode::PresentationFailed);
        identities.push_back(event.identity);
        return Status::success();
    }

    std::size_t attempts{};
    std::size_t fail_attempt{};
    std::vector<std::uint64_t> identities;
};

PresentationEvent presentation_event(
    std::uint64_t generation, std::uint64_t frame,
    std::uint64_t identity, std::uint16_t payload_size = 8,
    std::uint32_t source_ordinal = 0)
{
    PresentationEvent event{};
    event.coordinate = {generation, frame};
    event.source_ordinal = source_ordinal == 0
        ? static_cast<std::uint32_t>(identity) : source_ordinal;
    event.kind = 1;
    event.identity = identity;
    event.payload_size = payload_size;
    event.payload[0] = std::byte(identity & 0xff);
    return event;
}

void test_presentation_journal_is_bounded_and_retry_safe()
{
    PresentationJournal journal{3, 24};
    expect(journal.capacity() == 3 && journal.pending_count() == 0
            && journal.payload_bytes() == 0,
        "presentation journal allocates a fixed slot budget up front");
    expect(journal.Record(presentation_event(7, 11, 3, 8, 1)).ok()
            && journal.Record(presentation_event(7, 10, 10, 8, 2)).ok()
            && journal.Record(presentation_event(7, 10, 90, 8, 1)).ok(),
        "presentation journal fills its fixed event and payload capacity");
    expect(journal.Record(presentation_event(7, 12, 4)).code
            == FailureCode::CapacityExceeded,
        "presentation journal fails closed at fixed capacity");
    expect(journal.Record(presentation_event(7, 10, 90, 8, 1)).ok()
            && journal.pending_count() == 3,
        "presentation journal suppresses a pending duplicate without growth");

    RetryingPresentationSink sink{};
    sink.fail_attempt = 2;
    expect(journal.CommitThrough({7, 11}, sink).code
            == FailureCode::PresentationFailed
            && sink.identities == std::vector<std::uint64_t>{90}
            && journal.pending_count() == 2,
        "partial presentation failure commits only the successful prefix");
    sink.fail_attempt = 0;
    expect(journal.CommitThrough({7, 11}, sink).ok()
            && sink.identities == std::vector<std::uint64_t>({90, 10, 3})
            && journal.pending_count() == 0
            && journal.payload_bytes() == 0,
        "presentation retry preserves authored order and resumes without replaying prefix");
    expect(journal.Record(presentation_event(7, 10, 9, 8, 1)).ok()
            && journal.pending_count() == 0,
        "committed frame watermark suppresses late speculative duplicates");

    expect(journal.Record(presentation_event(8, 20, 5)).ok()
            && journal.Record(presentation_event(8, 21, 6)).ok(),
        "presentation journal accepts a new generation within fixed storage");
    const std::array oversized_replacement{
        presentation_event(8, 21, 7, 16),
        presentation_event(8, 22, 8, 16),
        presentation_event(8, 23, 9, 16)};
    expect(journal.ReplaceFrom({8, 21}, oversized_replacement).code
            == FailureCode::CapacityExceeded
            && journal.pending_count() == 2 && journal.payload_bytes() == 16,
        "capacity failure leaves the original presentation suffix intact");
    const std::array corrected_replacement{
        presentation_event(8, 21, 60, 8),
        presentation_event(8, 22, 70, 8)};
    expect(journal.ReplaceFrom({8, 21}, corrected_replacement).ok()
            && journal.pending_count() == 3 && journal.payload_bytes() == 24,
        "correction atomically replaces a preflighted presentation suffix");
    journal.DiscardFrom({8, 21});
    expect(journal.pending_count() == 1 && journal.payload_bytes() == 8,
        "presentation correction discards only the invalid suffix");
    journal.InvalidateGeneration(8);
    const auto stats = journal.statistics();
    expect(journal.pending_count() == 0 && journal.payload_bytes() == 0
            && stats.attempted == 10 && stats.recorded == 7
            && stats.duplicates == 2 && stats.capacity_failures == 2
            && stats.committed == 3 && stats.discarded == 4
            && stats.publish_failures == 1
            && stats.first_publish_failure
                == FailureCode::PresentationFailed
            && stats.first_failed_event.identity == 10
            && stats.last_publish_failure
                == FailureCode::PresentationFailed
            && stats.last_failed_event.identity == 10,
        "presentation journal exposes bounded lifecycle and retry counters");

    PresentationJournal invalid{2,
        2 * Schema::maximum_presentation_payload + 1};
    expect(invalid.capacity() == 0
            && invalid.Record(presentation_event(9, 1, 1)).code
                == FailureCode::CapacityExceeded,
        "invalid journal byte budgets cannot allocate or accept events");
}
}

#ifdef _WIN32
namespace {
#include "replay_physics_markers_selftest.inl"
void test_complete_suppressed_marker_graph()
{
    using Graph=ReplayPhysicsMarkerGraph;
    auto a=std::make_unique<Graph::Image>();auto b=std::make_unique<Graph::Image>();
    a->observed_actors[0]=b->observed_actors[0]=2;
    const auto marker=[](unsigned scene_index,unsigned total,unsigned first,unsigned second,
                         unsigned first_index,unsigned second_index,unsigned owner_count) {
        Graph::Marker m{};m.address=0x10000+scene_index*64;
        const std::array<std::uintptr_t,2> owners{0x20000+first*256,0x20000+second*256};
        const std::array<unsigned,3> indices{scene_index,first_index,second_index};
        std::memcpy(m.header.data()+8,owners.data(),16);std::memcpy(m.header.data()+0x18,indices.data(),12);
        m.header[0x24]=std::byte{2};m.header[0x25]=std::byte{0xb};
        m.marker_elements={0x30000+first*256,0x30000+second*256};
        m.marker_shape_cores={0x40000+first*256,0x40000+second*256};
        m.marker_filter_valid=m.marker_registered=true;m.marker_filter_pair=0xffffffffu;
        m.marker_scene_count=m.marker_map_count=total;m.marker_actor_counts={owner_count,owner_count};
        m.marker_attributes={first==2?0u:0x11u,second==2?0u:0x11u};
        m.marker_filter_data[0][3]=m.marker_filter_data[1][3]=5u<<21;
        return m;
    };
    for(unsigned i=0;i<2;++i) {
        a->actors[0][i].simulation=b->actors[0][i].simulation=0x20000+i*256;
        a->actors[0][i].interaction_count=1;b->actors[0][i].interaction_count=2;
        a->actors[0][i].interactions[0]=marker(0,1,0,1,0,0,1);
    }
    b->actors[0][0].interactions[0]=b->actors[0][1].interactions[0]=marker(0,3,0,1,0,0,2);
    b->actors[0][0].interactions[1]=marker(1,3,0,2,1,0,2);
    b->actors[0][1].interactions[1]=marker(2,3,1,2,1,1,2);
    Graph ga{},gb{};
    expect(ga.Read(*a,0) && gb.Read(*b,0) && ga.TargetRetainedIn(gb),
        "complete marker graph includes unobserved static-owner membership and retained target pair");
    auto invalid=std::make_unique<Graph::Image>(*b);
    invalid->actors[0][0].interactions[1].marker_actor_counts[1]=3;
    expect(!gb.Read(*invalid,0),"unseen static-owner interaction rejects marker ownership");
    *invalid=*b;invalid->actors[0][1].interactions[1].marker_map_count=4;
    Graph::ReadFailure failure{};
    expect(!gb.Read(*invalid,0,&failure) && std::string_view(failure.check)=="marker_graph_map_count"
        && failure.actor==1 && failure.interaction==1 && failure.expected==3 && failure.observed==4,
        "unseen pair-map owner rejects with exact captured actor/interaction/count diagnostic");
    *invalid=*b;invalid->actors[0][1].interactions[1].marker_scene_count=4;
    expect(!gb.Read(*invalid,0),"incomplete scene enumeration rejects marker ownership");
    *invalid=*b;invalid->actors[0][1].interactions[0].marker_filter_data[0][3]=7u<<21;
    expect(!gb.Read(*invalid,0,&failure) && std::string_view(failure.check)=="marker_graph_suppression"
        && failure.actor==1 && failure.interaction==0,
        "channel7 rejects at its first captured observation without weakening suppression");
    auto flags=marker(0,1,0,1,0,0,1);
    flags.marker_attributes={1,1};
    expect(!Graph::Suppressed(flags),"dynamic contact pair is outside marker-only reconstruction");
    flags.marker_attributes={0x11,0};flags.marker_filter_data[0][3]=7u<<21;
    expect(Graph::Suppressed(flags),"kinematic-static suppression precedes shared filter-table access");
    *invalid=*b;invalid->actors[0][0].interactions[1].marker_filter_pair=0;
    expect(!gb.Read(*invalid,0),"callback-owned filter handle rejects marker reconstruction");
    expect(gb.Read(*b,0) && ga.TargetRetainedIn(gb),"negative cases do not mutate retained graph inputs");
}

void test_physics_order_transaction()
{
    using World=Sc6ReplayWorldState;
    auto a=std::make_unique<World::PhysicsBoundary>();
    auto b=std::make_unique<World::PhysicsBoundary>();
    auto observed=std::make_unique<World::PhysicsBoundary>();
    auto* code=static_cast<std::byte*>(VirtualAlloc(nullptr,0x44000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    auto* index_page=static_cast<std::byte*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    expect(code && index_page,"allocate controlled native scene-lock and inverse-index fixtures");
    if(!code || !index_page) {if(code)VirtualFree(code,0,MEM_RELEASE);if(index_page)VirtualFree(index_page,0,MEM_RELEASE);return;}
    code[0x431d0]=code[0x432b0]=std::byte{0xc3};
    DWORD old{};expect(VirtualProtect(code,0x44000,PAGE_EXECUTE_READ,&old)!=0,"seal test-only no-op scene locks");
    FlushInstructionCache(GetCurrentProcess(),code,0x44000);
    std::array<std::uintptr_t,0x340/8> table{};
    table[0x330/8]=reinterpret_cast<std::uintptr_t>(code)+0x431d0;
    table[0x338/8]=reinterpret_cast<std::uintptr_t>(code)+0x432b0;
    auto* table_pointer=table.data();
    std::array<std::byte,0x1120> scene{};
    std::array<std::byte,0x600> islands{};
    std::array<std::array<std::byte,0x80>,2> nodes{};
    std::array<std::array<unsigned,2>,2> lists{{{0,64},{0,64}}};
    std::array<std::uintptr_t,2> active{};
    std::array<std::array<std::byte,0x180>,2> actors{};
    std::array<std::array<std::byte,0xc8>,2> sims{};
    std::array<std::array<std::byte,0x40>,2> kine{};
    std::array<std::array<std::byte,0x310>,2> components{};
    const auto put=[](auto& bytes,std::size_t offset,auto value){std::memcpy(bytes.data()+offset,&value,sizeof(value));};
    a->module=reinterpret_cast<std::uintptr_t>(code);a->scene_count=1;
    a->scenes[0]=reinterpret_cast<std::uintptr_t>(&table_pointer);
    a->observed_actors[0]=2;a->actor_counts[0][1]=a->actor_counts[0][5]=2;
    for(unsigned i=0;i<2;++i)active[i]=reinterpret_cast<std::uintptr_t>(actors[i].data())+0x80;
    put(scene,0x20,reinterpret_cast<std::uintptr_t>(active.data()));put(scene,0x28,2u);put(scene,0x2c,2u);put(scene,0x30,2u);
    for(unsigned slot=0;slot<2;++slot) {
        const auto base=slot?0x310u:0xb0u;
        put(islands,base+0x18,reinterpret_cast<std::uintptr_t>(nodes[slot].data()));
        put(islands,base+0x20,2u);put(islands,base+0x24,2u);
        put(islands,base+0x28,reinterpret_cast<std::uintptr_t>(index_page+slot*16));
        put(islands,base+0x30,2u);put(islands,base+0x34,2u);
        put(islands,base+0x98,reinterpret_cast<std::uintptr_t>(lists[slot].data()));
        put(islands,base+0xa0,2u);put(islands,base+0xa4,2u);
        for(unsigned i=0;i<2;++i) {nodes[slot][i*0x20+4]=std::byte{6};std::memcpy(index_page+slot*16+i*4,&i,4);}
    }
    for(unsigned i=0;i<2;++i) {
        auto& row=a->actors[0][i];row.kind=2;
        row.actor=reinterpret_cast<std::uintptr_t>(actors[i].data());row.body=row.actor+0x80;
        row.simulation=reinterpret_cast<std::uintptr_t>(sims[i].data());row.kinematic=reinterpret_cast<std::uintptr_t>(kine[i].data());
        row.component=reinterpret_cast<std::uintptr_t>(components[i].data());
        row.properties[0x9c]=std::byte{3};put(row.dynamic_storage,0x68,0x83000000u);row.kinematic_storage[0x1f]=std::byte{1};
        put(row.simulation_storage,0,a->module+0x1aadc0);put(row.simulation_storage,0x48,row.actor+0x80);
        put(row.simulation_storage,0x40,reinterpret_cast<std::uintptr_t>(scene.data()));
        put(row.simulation_storage,0x88,row.actor+0x90);put(row.simulation_storage,0xb0,i*64);put(row.simulation_storage,0xb8,i);
        put(row.simulation_storage,0x60,2.0f);
        auto& admission=row.kinematic_admission;admission.valid=true;
        admission.scene=reinterpret_cast<std::uintptr_t>(scene.data());admission.controller=1;
        admission.controller_vtable=a->module+0x1ab0e8;admission.island_owner=reinterpret_cast<std::uintptr_t>(islands.data());
        std::memcpy(admission.active_header.data(),scene.data()+0x20,0x18);
        std::copy(active.begin(),active.end(),admission.active_bodies.begin());
        for(unsigned slot=0;slot<2;++slot) {
            auto& island=admission.islands[slot];const auto base=slot?0x310u:0xb0u;
            std::memcpy(island.storage_header.data(),islands.data()+base+0x18,0x20);
            std::memcpy(island.node.data(),nodes[slot].data()+i*0x20,0x20);island.index=i;
            std::memcpy(island.list_headers[0].data(),islands.data()+base+0x98,0x10);
            std::copy(lists[slot].begin(),lists[slot].end(),island.lists[0].begin());
        }
        sims[i]=row.simulation_storage;actors[i]=row.dynamic_storage;kine[i]=row.kinematic_storage;
    }
    *b=*a;
    for(unsigned i=0;i<2;++i) {
        auto& row=a->actors[0][i];put(row.simulation_storage,0xb8,1u-i);put(row.simulation_storage,0x60,1.0f);
        b->actors[0][i].component_transform_flags=0x410;
        row.component_transform_flags=0x411;put(components[i],0x240,0x410u);
        std::swap(row.kinematic_admission.active_bodies[0],row.kinematic_admission.active_bodies[1]);
        for(auto& island:row.kinematic_admission.islands) {island.index=1-i;std::swap(island.lists[0][0],island.lists[0][1]);}
    }
    expect(World::PreparePhysicsProjection(*a,*b).ok() && World::PreparePhysicsProjection(*b,*a).ok(),
        "complete retained kinematic permutation is reversible without native activation");
    expect(!World::ValidatePhysicsProjection(*a,*b).ok(),"B order and previous pose are not accepted as installed A");
    auto invalid=std::make_unique<World::PhysicsBoundary>(*a);
    invalid->actors[0][0].component_transform_flags^=0x10;
    expect(!World::PreparePhysicsProjection(*invalid,*b).ok(),"visibility changes do not enter transform-cache restoration");
    {
        auto pending_a=std::make_unique<World::PhysicsBoundary>(*a);
        auto pending_b=std::make_unique<World::PhysicsBoundary>(*b);
        pending_b->actors[0][0].component_transform_flags=0x401;
        expect(!World::PreparePhysicsProjection(*pending_a,*pending_b,nullptr,false,nullptr,true).ok(),
            "planned visibility without a retained render owner rejects");
        pending_a->creation_owner_count=pending_b->creation_owner_count=1;
        auto& left=pending_a->creation_owners[0];left.component=a->actors[0][0].component;
        left.owner=100;left.storage=200;left.count=left.capacity=1;left.visibility=0x411;
        pending_b->creation_owners[0]=left;pending_b->creation_owners[0].visibility=0x401;
        expect(World::PreparePhysicsProjection(*pending_a,*pending_b,nullptr,false,nullptr,true).ok(),
            "initial preflight admits only the retained owner's planned native visibility publication");
        expect(!World::InstallPhysicsProjection(*pending_b,*pending_a).ok()
            && actors[0]==b->actors[0][0].dynamic_storage,
            "physics installation cannot bypass incomplete native visibility publication");
        pending_b->creation_owners[0].mesh=1;
        expect(!World::PreparePhysicsProjection(*pending_a,*pending_b,nullptr,false,nullptr,true).ok(),
            "planned visibility with changed native mesh ownership rejects");
    }
    *invalid=*a;
    invalid->actors[0][0].kinematic_admission.active_bodies[0]=0;
    expect(!World::PreparePhysicsProjection(*invalid,*b).ok(),"unknown active body rejects before publication");
    *invalid=*a;invalid->actors[0][0].kinematic_admission.islands[1].index=0;
    expect(!World::PreparePhysicsProjection(*invalid,*b).ok(),"inconsistent inverse island index rejects");
    *invalid=*a;invalid->actors[0][0].kinematic_admission.islands[0].node[4]^=std::byte{0x20};
    expect(!World::PreparePhysicsProjection(*invalid,*b).ok(),"pending native island work rejects");
    *invalid=*a;invalid->actors[0][0].simulation_storage[0x80]=std::byte{1};
    expect(!World::PreparePhysicsProjection(*invalid,*b).ok(),"CCD owner cannot use previous-transform projection");
    expect(VirtualProtect(index_page,0x1000,PAGE_READONLY,&old)!=0,"protect an inverse index before installation");
    expect(!World::InstallPhysicsProjection(*b,*a).ok() && active[0]==b->actors[0][0].actor+0x80
        && sims[0]==b->actors[0][0].simulation_storage,"unwritable order destination preserves complete B before writes");
    expect(VirtualProtect(index_page,0x1000,PAGE_READWRITE,&old)!=0,"restore index fixture access");
    expect(World::InstallPhysicsProjection(*b,*a).ok(),"publish native array order, inverse indices and previous transforms together");
    const auto read_observed=[&](const World::PhysicsBoundary& desired) {
        *observed=desired;
        for(unsigned i=0;i<2;++i) {
            auto& row=observed->actors[0][i];row.simulation_storage=sims[i];
            std::memcpy(&row.component_transform_flags,components[i].data()+0x240,4);
            std::copy(active.begin(),active.end(),row.kinematic_admission.active_bodies.begin());
            for(unsigned slot=0;slot<2;++slot) {
                auto& island=row.kinematic_admission.islands[slot];
                island.storage_header=b->actors[0][i].kinematic_admission.islands[slot].storage_header;
                std::memcpy(&island.index,index_page+slot*16+i*4,4);
                std::copy(lists[slot].begin(),lists[slot].end(),island.lists[0].begin());
            }
        }
    };
    read_observed(*a);expect(World::ValidatePhysicsProjection(*a,*observed).ok(),"independent native-memory reads verify A order and previous pose");
    expect(World::InstallPhysicsProjection(*a,*b).ok(),"recover B without lifecycle calls");
    read_observed(*b);expect(World::ValidatePhysicsProjection(*b,*observed).ok(),"independent native-memory reads verify full B order and previous pose recovery");
    // A's native node/index vectors can have relocated since capture. Their
    // old addresses are deliberately unreadable; only current B bindings may
    // be followed. Logical extents and retained node identities remain exact.
    for(auto& row:a->actors[0])if(row.kinematic_admission.valid)for(unsigned slot=0;slot<2;++slot) {
        auto& header=row.kinematic_admission.islands[slot].storage_header;
        put(header,0,std::uintptr_t{1}+slot*0x100);put(header,16,std::uintptr_t{33}+slot*0x100);
        put(header,12,4u);put(header,28,4u);
    }
    expect(World::PreparePhysicsProjection(*a,*b).ok(),"historical vector addresses and spare capacities are not logical node identities");
    *invalid=*a;
    for(auto& row:invalid->actors[0])if(row.kinematic_admission.valid)for(auto& island:row.kinematic_admission.islands)put(island.storage_header,8,3u);
    expect(!World::PreparePhysicsProjection(*invalid,*b).ok(),"changed logical node extent still rejects");
    expect(World::InstallPhysicsProjection(*b,*a).ok(),"A permutation follows current owner storage without dereferencing retired A addresses");
    read_observed(*a);expect(World::ValidatePhysicsProjection(*a,*observed).ok(),"A values validate with current storage bindings");
    expect(World::InstallPhysicsProjection(*observed,*b).ok(),"relocated A publication preserves complete B recovery");
    read_observed(*b);expect(World::ValidatePhysicsProjection(*b,*observed).ok(),"relocated transaction independently reproduces B indices and previous transforms");
    // Completed native retirement leaves high-water node storage, but the
    // free-ID stack is authoritative continuation for future body creation.
    std::array<unsigned,2> free_ids{2,3};
    put(islands,0,reinterpret_cast<std::uintptr_t>(free_ids.data()));put(islands,8,2u);put(islands,12,2u);put(islands,16,4u);
    for(unsigned slot=0;slot<2;++slot) {
        const auto base=slot?0x310u:0xb0u;
        put(islands,base+0x20,4u);put(islands,base+0x24,4u);put(islands,base+0x30,4u);put(islands,base+0x34,4u);
        for(unsigned i=0;i<2;++i)put(nodes[slot],i*0x20+0x18,b->actors[0][i].simulation+0x60);
        for(unsigned i=2;i<4;++i) {
            nodes[slot][i*0x20+4]=std::byte{8};put(nodes[slot],i*0x20,0xffffffffu);
            put(nodes[slot],i*0x20+8,0xffffffc0u);put(nodes[slot],i*0x20+12,0xffffffc0u);
            const unsigned inactive=0x3ffffffu;std::memcpy(index_page+slot*16+i*4,&inactive,4);
        }
    }
    const auto observe_nodes=[&](World::PhysicsBoundary& image) {
        auto& d=image.node_domains[0];d={};d.valid=true;d.owner=reinterpret_cast<std::uintptr_t>(islands.data());
        std::memcpy(&d.free_storage,islands.data(),8);std::memcpy(&d.free_count,islands.data()+8,4);
        std::memcpy(&d.free_capacity,islands.data()+12,4);std::memcpy(&d.next_id,islands.data()+16,4);
        std::copy_n(free_ids.begin(),d.free_count,d.free_ids.begin());
        for(unsigned slot=0;slot<2;++slot) {
            d.counts[slot]=4;
            std::memcpy(d.nodes[slot].data(),nodes[slot].data(),0x80);std::memcpy(d.indices[slot].data(),index_page+slot*16,16);
            for(unsigned i=0;i<2;++i) {
                auto& island=image.actors[0][i].kinematic_admission.islands[slot];
                std::memcpy(island.storage_header.data(),islands.data()+(slot?0x310:0xb0)+0x18,0x20);
                std::memcpy(island.node.data(),nodes[slot].data()+i*0x20,0x20);
            }
        }
    };
    observe_nodes(*b);observe_nodes(*a);
    auto& an=a->node_domains[0];an.free_count=0;an.free_ids={};an.next_id=2;
    an.free_storage=0;an.free_capacity=0;
    for(unsigned slot=0;slot<2;++slot) {
        an.counts[slot]=2;
        for(unsigned i=0;i<2;++i)an.indices[slot][i]=a->actors[0][i].kinematic_admission.islands[slot].index;
        for(auto& row:a->actors[0])if(row.kinematic_admission.valid) {
            auto& header=row.kinematic_admission.islands[slot].storage_header;put(header,8,2u);put(header,24,2u);
        }
    }
    expect(World::PreparePhysicsProjection(*a,*b).ok(),"deleted high-water slots admit captured allocation continuation");
    *invalid=*b;invalid->node_domains[0].free_ids[1]=2;
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"duplicate free handle rejects");
    *invalid=*b;invalid->node_domains[0].free_ids[0]=0;
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"free handle cannot name a retained live body");
    *invalid=*b;invalid->node_domains[0].indices[0][0]=1;
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"domain and per-body inverse witnesses must agree");
    *invalid=*b;invalid->node_domains[0].pending_count=1;
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"pending node retirement rejects");
    *invalid=*b;invalid->node_domains[0].nodes[1][3][4]=std::byte{6};
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"extra live island node rejects");
    *invalid=*b;invalid->node_domains[0].nodes[0][2][0x10]=std::byte{1};
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"deleted slot with active references rejects");
    expect(!World::ValidatePhysicsProjection(*a,*b).ok(),"B free-ID continuation is not installed A");
    expect(World::InstallPhysicsProjection(*b,*a).ok(),"restore A allocator and order without shrinking native vectors");
    read_observed(*a);observe_nodes(*observed);
    expect(observed->node_domains[0].free_count==0 && observed->node_domains[0].next_id==2
        && observed->node_domains[0].counts[0]==4 && World::ValidatePhysicsProjection(*a,*observed).ok(),
        "native reads prove A allocation continuation in current high-water storage");
    // Simulate changed future allocation's write into the spare free-ID backing:
    // B must come from its saved values, not the surviving contents of the array.
    free_ids={63,62};
    expect(World::InstallPhysicsProjection(*observed,*b).ok(),"restore complete B allocator through actual A bindings");
    read_observed(*b);observe_nodes(*observed);
    expect(free_ids==std::array<unsigned,2>{2,3} && World::ValidatePhysicsProjection(*b,*observed).ok(),
        "B free-ID order/count/next-ID and native active indices recover independently");
    std::array<std::uintptr_t,8> interaction_heap{};
    put(b->actors[0][0].simulation_storage,0x28,reinterpret_cast<std::uintptr_t>(interaction_heap.data()));
    put(b->actors[0][0].simulation_storage,0x30,8u);sims[0]=b->actors[0][0].simulation_storage;
    put(a->actors[0][0].simulation_storage,0x28,a->actors[0][0].simulation+8);
    put(a->actors[0][0].simulation_storage,0x30,4u);put(a->actors[0][0].simulation_storage,8,std::uintptr_t{0x12345678});
    expect(World::InstallPhysicsProjection(*b,*a).ok(),"A empty inline history restores through untouched current heap ownership");
    read_observed(*a);observe_nodes(*observed);
    expect(World::ValidatePhysicsProjection(*a,*observed).ok()
        && !std::memcmp(sims[0].data()+8,b->actors[0][0].simulation_storage.data()+8,0x2c),
        "A publication never overwrites B native interaction backing or obsolete inline pointers");
    expect(World::InstallPhysicsProjection(*observed,*b).ok(),"B undo retains the native interaction allocation owner");
    read_observed(*b);observe_nodes(*observed);
    expect(World::ValidatePhysicsProjection(*b,*observed).ok(),"B interaction ownership and allocator continuation survive together");
    // A retains two isolated active kinematics; B has put the first to sleep.
    // Native memory is initialized as B, then only production publication and
    // independent reads may establish A and B recovery.
    *a=*b;
    a->node_domains[0].notifications_quiescent=b->node_domains[0].notifications_quiescent=true;
    for(unsigned i=0;i<2;++i) {
        for(auto* image:{a.get(),b.get()}) {
            auto& row=image->actors[0][i];row.isolated_kinematic=true;
            row.dynamic_storage[0xac]=std::byte{3};
            row.dynamic_storage[0xad]=std::byte{1}; // Separate native pose-identity byte, not flag bit100.
            for(unsigned slot=0;slot<2;++slot) {
                auto& node=image->node_domains[0].nodes[slot][i];
                put(node,0,0xffffffffu);put(node,8,0xffffffc0u);put(node,12,0xffffffc0u);
                row.kinematic_admission.islands[slot].node=node;
            }
        }
    }
    put(a->actors[0][0].dynamic_storage,0x11c,0.4f);
    put(a->actors[0][0].dynamic_storage,0x174,0.4f);
    put(a->actors[0][0].properties,0xcc,0.4f);
    put(a->actors[0][0].simulation_storage,0xb4,std::uint16_t{0x200});
    auto& asleep=b->actors[0][0];
    put(asleep.dynamic_storage,0x178,1u);
    put(asleep.simulation_storage,0xb8,0xfffffffeu);asleep.properties[0xbc]=std::byte{1};
    asleep.kinematic_admission={};asleep.kinematic_admission.scene=reinterpret_cast<std::uintptr_t>(scene.data());
    auto& survivor=b->actors[0][1];put(survivor.simulation_storage,0xb8,0u);
    auto& ba=survivor.kinematic_admission;
    put(ba.active_header,8,1u);put(ba.active_header,16,1u);ba.active_bodies={};ba.active_bodies[0]=survivor.actor+0x80;
    for(unsigned slot=0;slot<2;++slot) {
        auto& d=b->node_domains[0];d.nodes[slot][0][4]=std::byte{5};d.indices[slot][0]=0x3ffffffu;d.indices[slot][1]=0;
        auto& island=ba.islands[slot];island.index=0;island.lists[0]={};island.lists[0][0]=64;
        put(island.list_headers[0],8,1u);
        std::memcpy(nodes[slot].data(),d.nodes[slot].data(),0x80);
        std::memcpy(index_page+slot*16,d.indices[slot].data(),16);
        lists[slot]={64,0};put(islands,(slot?0x310:0xb0)+0xa0,1u);
    }
    active={survivor.actor+0x80,0};put(scene,0x28,1u);put(scene,0x30,1u);
    for(unsigned i=0;i<2;++i) {actors[i]=b->actors[0][i].dynamic_storage;sims[i]=b->actors[0][i].simulation_storage;}
    World::PhysicsProjectionFailure why{};
    const auto prepared=World::PreparePhysicsProjection(*a,*b,&why);
    if(!prepared.ok())std::printf("isolated activity prepare check=%s\n",why.check?why.check:"unknown");
    expect(prepared.ok() && World::PreparePhysicsProjection(*b,*a).ok(),"isolated completed active/sleep membership admits reversible projection");
    *invalid=*a;put(invalid->actors[0][0].dynamic_storage,0x178,2u);
    expect(!World::PreparePhysicsProjection(*invalid,*b).ok(),"invalid public sleeping value rejects before publication");
    *invalid=*a;put(invalid->actors[0][0].dynamic_storage,0x174,0.8f);
    expect(!World::PreparePhysicsProjection(*invalid,*b).ok(),"independent public wake getter witness must match captured native backing");
    *invalid=*a;invalid->actors[0][0].dynamic_storage[0xad]=std::byte{};
    expect(!World::PreparePhysicsProjection(*invalid,*b).ok(),"pose-identity byte remains independently strict beside one-byte rigid-body flags");
    *invalid=*a;invalid->node_domains[0].notifications_quiescent=false;
    expect(!World::PreparePhysicsProjection(*invalid,*b).ok(),"unproved notification completion rejects activity change");
    *invalid=*a;put(invalid->actors[0][0].simulation_storage,0xb4,std::uint16_t{0x220});
    expect(!World::PreparePhysicsProjection(*invalid,*b).ok(),"body notification membership rejects even with an empty set witness");
    *invalid=*a;invalid->actors[0][0].isolated_kinematic=false;
    expect(!World::PreparePhysicsProjection(*invalid,*b).ok(),"unowned bounds or preview membership rejects activity change");
    *invalid=*a;invalid->actors[0][0].interaction_count=1;
    expect(!World::PreparePhysicsProjection(*invalid,*b).ok(),"interaction ownership cannot use isolated activity projection");
    *invalid=*a;invalid->node_domains[0].nodes[0][0][0x10]=std::byte{1};invalid->actors[0][0].kinematic_admission.islands[0].node[0x10]=std::byte{1};
    expect(!World::PreparePhysicsProjection(*invalid,*b).ok(),"active graph references reject isolated activity projection");
    *invalid=*a;invalid->node_domains[0].nodes[0][0][4]=std::byte{0x26};invalid->actors[0][0].kinematic_admission.islands[0].node[4]=std::byte{0x26};
    expect(!World::PreparePhysicsProjection(*invalid,*b).ok(),"pending island admission rejects activity projection");
    put(scene,0x1080+0x34,1u);
    expect(!World::InstallPhysicsProjection(*b,*a).ok() && actors[0]==b->actors[0][0].dynamic_storage
        && sims[0]==b->actors[0][0].simulation_storage && active[0]==survivor.actor+0x80,
        "new live notification rejects before any activity or CPU writes");
    put(scene,0x1080+0x34,0u);
    expect(VirtualProtect(index_page,0x1000,PAGE_READONLY,&old)!=0,"protect inverse destination for activity preflight");
    expect(!World::InstallPhysicsProjection(*b,*a).ok() && sims[0]==b->actors[0][0].simulation_storage,
        "activity restoration validates all inverse storage before writing B");
    expect(VirtualProtect(index_page,0x1000,PAGE_READWRITE,&old)!=0,"restore activity fixture storage");
    const auto observe_activity=[&](const World::PhysicsBoundary& desired) {
        *observed=desired;
        auto& d=observed->node_domains[0];
        for(unsigned slot=0;slot<2;++slot) {
            std::memcpy(d.nodes[slot].data(),nodes[slot].data(),0x80);
            std::memcpy(d.indices[slot].data(),index_page+slot*16,16);
            if(d.island_ids_valid) {
                const auto offset=slot?0x310u:0xb0u;
                std::uintptr_t ids{};std::memcpy(&ids,islands.data()+offset+0xf0,8);
                std::memcpy(&d.island_id_counts[slot],islands.data()+offset+0xf8,4);
                d.island_ids[slot]={};std::memcpy(d.island_ids[slot].data(),reinterpret_cast<void*>(ids),d.island_id_counts[slot]*4);
                d.activation_domain_clear[slot]=true;
                for(unsigned at:{0x160u,0x170u,0x1a8u}) {
                    unsigned count{};std::memcpy(&count,islands.data()+offset+at,4);
                    d.activation_domain_clear[slot]&=count==0;
                }
                unsigned words{};std::uintptr_t bits{};std::memcpy(&words,islands.data()+offset+0x180,4);
                std::memcpy(&bits,islands.data()+offset+0x178,8);
                for(unsigned word=0;word<(words&0x7fffffff);++word)
                    d.activation_domain_clear[slot]&=reinterpret_cast<const unsigned*>(bits)[word]==0;
            }
        }
        unsigned count{};std::memcpy(&count,scene.data()+0x28,4);
        for(unsigned i=0;i<2;++i) {
            auto& row=observed->actors[0][i];row.dynamic_storage=actors[i];row.simulation_storage=sims[i];row.kinematic_storage=kine[i];
            unsigned index{};std::memcpy(&index,sims[i].data()+0xb8,4);
            unsigned sleeping{};std::memcpy(&sleeping,actors[i].data()+0x178,4);
            row.properties[0xbc]=std::byte(sleeping!=0);
            std::memcpy(row.properties.data()+0xcc,actors[i].data()+0x174,4);
            row.kinematic_admission={};row.kinematic_admission.scene=reinterpret_cast<std::uintptr_t>(scene.data());
            if(index==0xfffffffeu)continue;
            auto& admission=row.kinematic_admission;admission=a->actors[0][i].kinematic_admission;
            std::memcpy(admission.active_header.data(),scene.data()+0x20,0x18);admission.active_bodies={};
            std::copy_n(active.begin(),count,admission.active_bodies.begin());
            for(unsigned slot=0;slot<2;++slot) {
                auto& island=admission.islands[slot];island.node=d.nodes[slot][i];island.index=d.indices[slot][i];
                for(unsigned list=0;list<2;++list) {
                    std::memcpy(island.list_headers[list].data(),islands.data()+(slot?0x310:0xb0)+(list?0x190:0x98),16);
                    unsigned island_count{};std::uintptr_t list_storage{};
                    std::memcpy(&island_count,island.list_headers[list].data()+8,4);
                    std::memcpy(&list_storage,island.list_headers[list].data(),8);
                    island.lists[list]={};
                    if(island_count)std::memcpy(island.lists[list].data(),reinterpret_cast<void*>(list_storage),island_count*4);
                }
            }
        }
    };
    expect(World::InstallPhysicsProjection(*b,*a).ok(),"restore isolated active membership, countdown and wake counter together");
    observe_activity(*a);
    expect(World::ValidatePhysicsProjection(*a,*observed).ok() && sims[0]==a->actors[0][0].simulation_storage
        && actors[0]==a->actors[0][0].dynamic_storage && nodes[0][4]==std::byte{6},
        "independent native-memory reads establish installed A activity and exact CPU fields");
    expect(World::InstallPhysicsProjection(*observed,*b).ok(),"recover complete sleeping B without issuing wake/sleep callbacks");
    observe_activity(*b);
    expect(World::ValidatePhysicsProjection(*b,*observed).ok() && sims[0]==b->actors[0][0].simulation_storage
        && actors[0]==b->actors[0][0].dynamic_storage && nodes[0][4]==std::byte{5},
        "independent native-memory reads establish B activity/counts/countdown/wake and inverse recovery");
    // Native400/407: the BodySim is asleep, while both island nodes still
    // retain ACTIVE|KINEMATIC|READY_FOR_SLEEP (7), with their inverse index.
    // 112D60 removes the scene active body; 147490 only marks the island node.
    const auto settled_sleep=std::make_unique<World::PhysicsBoundary>(*b);
    for(unsigned slot=0;slot<2;++slot) {
        auto& d=b->node_domains[0];d.nodes[slot][0][4]=std::byte{7};d.indices[slot][0]=1;d.indices[slot][1]=0;
        auto& island=b->actors[0][1].kinematic_admission.islands[slot];
        island.index=0;island.lists[0]={};island.lists[0][0]=64;island.lists[0][1]=0;put(island.list_headers[0],8,2u);
        std::memcpy(nodes[slot].data(),d.nodes[slot].data(),0x80);
        std::memcpy(index_page+slot*16,d.indices[slot].data(),16);
        lists[slot]={64,0};put(islands,(slot?0x310:0xb0)+0xa0,2u);
    }
    why={};const auto pending_sleep=World::PreparePhysicsProjection(*a,*b,&why);
    if(!pending_sleep.ok())std::printf("native pending island sleep rejected check=%s\n",why.check?why.check:"unknown");
    expect(pending_sleep.ok() && World::PreparePhysicsProjection(*b,*a).ok(),
        "native pending island sleep has independent scene-body and island-node membership");
    *invalid=*b;invalid->actors[0][1].kinematic_admission.islands[0].lists[0][1]=64;
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"duplicate pending island list owner rejects");
    *invalid=*b;put(invalid->actors[0][1].kinematic_admission.islands[0].list_headers[0],8,1u);
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"truncated pending island list rejects its remaining inverse owner");
    *invalid=*b;invalid->actors[0][0].isolated_kinematic=false;
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"pending island sleep cannot grant unproved body ownership");
    lists[0][1]=64;
    expect(!World::InstallPhysicsProjection(*b,*a).ok() && sims[0]==b->actors[0][0].simulation_storage,
        "live island tail beyond body count rejects before any restoration writes");
    lists[0][1]=0;
    expect(World::InstallPhysicsProjection(*b,*a).ok(),"restore active A from pending island sleep B");
    observe_activity(*a);
    expect(World::ValidatePhysicsProjection(*a,*observed).ok(),"native reads establish A after pending sleep restoration");
    expect(World::InstallPhysicsProjection(*observed,*b).ok(),"recover complete B including pending island sleep");
    observe_activity(*b);
    unsigned body_count{},island_count{};std::memcpy(&body_count,scene.data()+0x28,4);std::memcpy(&island_count,islands.data()+0xb0+0xa0,4);
    expect(World::ValidatePhysicsProjection(*b,*observed).ok() && body_count==1 && island_count==2
        && lists[0]==std::array<unsigned,2>{64,0} && nodes[0][4]==std::byte{7},
        "independent native reads preserve pending B flag, full island list and separate body count");
    expect(World::InstallPhysicsProjection(*b,*settled_sleep).ok(),"pending-to-settled island sleep restores with body still asleep");
    observe_activity(*settled_sleep);
    expect(World::ValidatePhysicsProjection(*settled_sleep,*observed).ok(),"native reads establish settled sleep from pending state");
    expect(World::InstallPhysicsProjection(*observed,*b).ok(),"settled-to-pending island sleep restores complete B continuation");
    observe_activity(*b);
    expect(World::ValidatePhysicsProjection(*b,*observed).ok() && lists[0]==std::array<unsigned,2>{64,0}
        && nodes[0][4]==std::byte{7},"native reads establish exact pending sleep B after settled-state recovery");
    // Native402/409 completes a Lux boundary with a queued wake on an
    // otherwise isolated retained BodySim. Pending flags are continuation,
    // not proof of a different owner or permission to dispatch early.
    expect(World::InstallPhysicsProjection(*b,*a).ok(),"establish active A before pending notification boundary");
    std::array<std::byte,0x50> client{};
    std::array<std::uintptr_t,1> clients{reinterpret_cast<std::uintptr_t>(client.data())};
    std::array<std::uintptr_t,6> callback_table{};
    auto* callback_vtable=callback_table.data();
    put(client,0x48,reinterpret_cast<std::uintptr_t>(&callback_vtable));
    put(scene,0x10f8,reinterpret_cast<std::uintptr_t>(clients.data()));put(scene,0x1100,1u);put(scene,0x1104,1u);
    put(scene,0x10f0,static_cast<unsigned short>(0x101));
    for(unsigned i=0;i<2;++i) {
        put(a->actors[0][i].dynamic_storage,0x80,a->actors[0][i].simulation);
        actors[i]=a->actors[0][i].dynamic_storage;sims[i]=a->actors[0][i].simulation_storage;
    }
    expect(World::ReadPhysicsNotifications(*a,0).ok(),"real checkpoint reader captures empty notification continuation");
    auto notified=std::make_unique<World::PhysicsBoundary>(*a);
    put(notified->actors[0][0].simulation_storage,0xb4,static_cast<unsigned short>(0xa0));
    sims[0]=notified->actors[0][0].simulation_storage;
    auto* notification_page=static_cast<std::byte*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    expect(notification_page!=nullptr,"allocate bounded notification destination");
    if(!notification_page)std::exit(2);
    std::fill_n(reinterpret_cast<unsigned*>(notification_page),28,0xffffffffu);
    const auto notification_address=reinterpret_cast<std::uintptr_t>(notification_page);
    const auto core=notified->actors[0][0].actor+0x80;
    *reinterpret_cast<std::uintptr_t*>(notification_page+112)=core;
    reinterpret_cast<unsigned*>(notification_page)[ReplayPhysicsNotifications::Hash(core)&15]=0;
    put(scene,0x10b8,notification_address);put(scene,0x10c0,notification_address+112);
    put(scene,0x10c8,notification_address+64);put(scene,0x10d0,notification_address);
    put(scene,0x10d8,12u);put(scene,0x10dc,16u);put(scene,0x10e4,1u);put(scene,0x10ec,1u);
    expect(World::ReadPhysicsNotifications(*notified,0).ok(),"real checkpoint reader captures pending B with native inverse hash membership");
    why={};const auto notification_admission=World::PreparePhysicsProjection(*a,*notified,&why);
    if(!notification_admission.ok())std::printf("pending wake continuation rejected check=%s\n",why.check?why.check:"unknown");
    expect(notification_admission.ok(),"retained pending wake continuation is representable without dispatching it");
    *invalid=*notified;invalid->notifications[0].sets[1].cores[0]=0x12345678;
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"captured foreign notification owner cannot authorize projection");
    *invalid=*notified;put(invalid->actors[0][0].dynamic_storage,0x80,invalid->actors[0][1].simulation);
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"captured Core must belong to the notified simulation");
    *invalid=*notified;put(invalid->actors[0][0].simulation_storage,0xb4,static_cast<unsigned short>(0x80));
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"wake flags require actual captured queue membership");
    const auto saved_sims=sims;
    *reinterpret_cast<std::uintptr_t*>(notification_page+112)=0x12345678;
    expect(!World::InstallPhysicsProjection(*notified,*a).ok() && sims==saved_sims,
        "live foreign notification core rejects before any CPU writes");
    *reinterpret_cast<std::uintptr_t*>(notification_page+112)=core;
    put(client,0x48,std::uintptr_t{});
    expect(!World::InstallPhysicsProjection(*notified,*a).ok() && sims==saved_sims,
        "changed callback registration rejects before notification publication");
    put(client,0x48,reinterpret_cast<std::uintptr_t>(&callback_vtable));
    expect(VirtualProtect(notification_page,0x1000,PAGE_READONLY,&old)!=0,"protect native notification destination");
    expect(!World::InstallPhysicsProjection(*notified,*a).ok() && sims==saved_sims,
        "read-only notification backing rejects before body/order writes");
    expect(VirtualProtect(notification_page,0x1000,PAGE_READWRITE,&old)!=0,"restore notification storage");
    expect(World::InstallPhysicsProjection(*notified,*a).ok(),"real projection publishes A and its empty pending-work continuation");
    observe_activity(*a);expect(World::ReadPhysicsNotifications(*observed,0).ok()
        && World::ValidatePhysicsProjection(*a,*observed).ok(),"native memory establishes restored A queues and body flags");
    expect(World::InstallPhysicsProjection(*observed,*notified).ok(),"real projection recovers complete B queue and owner flags");
    observe_activity(*notified);expect(World::ReadPhysicsNotifications(*observed,0).ok()
        && World::ValidatePhysicsProjection(*notified,*observed).ok(),"native memory establishes recovered pending B continuation");
    expect(World::InstallPhysicsProjection(*observed,*notified).ok(),"repeated complete-B publication preserves one pending wake");
    // Native402/409 also has an active BodySim whose island activation is
    // still queued. Preserve both lists and their distinct inverse indices.
    auto* activation_page=static_cast<std::byte*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    expect(activation_page!=nullptr,"allocate independently protected activation destination");
    if(!activation_page)std::exit(2);
    std::array<std::array<unsigned,4>,2> island_ids{};
    auto pending_activation=std::make_unique<World::PhysicsBoundary>(*notified);
    for(unsigned slot=0;slot<2;++slot) {
        const auto offset=slot?0x310u:0xb0u;
        island_ids[slot].fill(0xffffffffu);
        put(islands,offset+0xf0,reinterpret_cast<std::uintptr_t>(island_ids[slot].data()));
        put(islands,offset+0xf8,4u);put(islands,offset+0xfc,4u);
        put(islands,offset+0x190,reinterpret_cast<std::uintptr_t>(activation_page+slot*16));
        put(islands,offset+0x198,1u);put(islands,offset+0x19c,2u);
        lists[slot]={64,0};put(islands,offset+0xa0,1u);
        nodes[slot][4]=std::byte{0x24};
        const unsigned zero=0;std::memcpy(index_page+slot*16,&zero,4);std::memcpy(index_page+slot*16+4,&zero,4);
        for(auto* image:{a.get(),notified.get(),pending_activation.get()}) {
            auto& domain=image->node_domains[0];domain.island_ids_valid=true;
            domain.island_id_counts[slot]=4;domain.activation_domain_clear[slot]=true;
            std::copy(island_ids[slot].begin(),island_ids[slot].end(),domain.island_ids[slot].begin());
            for(unsigned i=0;i<2;++i) {
                auto& island=image->actors[0][i].kinematic_admission.islands[slot];
                std::memcpy(island.list_headers[1].data(),islands.data()+offset+0x190,16);
                put(island.list_headers[1],8,image==pending_activation.get()?1u:0u);
                if(image==a.get())island.list_headers[1]={}; // A predates native allocation.
                island.lists[1]={};
                if(image==pending_activation.get()) {
                    std::memcpy(island.node.data(),nodes[slot].data()+i*32,32);
                    island.index=0;island.lists[0]={};island.lists[0][0]=64;put(island.list_headers[0],8,1u);
                    domain.nodes[slot][i]=island.node;domain.indices[slot][i]=0;
                }
            }
        }
    }
    why={};const auto activation_admission=World::PreparePhysicsProjection(*a,*pending_activation,&why);
    if(!activation_admission.ok())std::printf("pending activation continuation rejected check=%s\n",why.check?why.check:"unknown");
    expect(activation_admission.ok(),"retained pending island activation must preserve its native input list");
    *invalid=*pending_activation;invalid->actors[0][0].kinematic_admission.islands[0].lists[1][0]=64;
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"activation list cannot substitute another retained node");
    *invalid=*pending_activation;invalid->node_domains[0].island_ids[0][0]=0;
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"dynamic island membership cannot use isolated activation restoration");
    *invalid=*pending_activation;invalid->node_domains[0].activation_domain_clear[0]=false;
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"unrelated captured island work rejects activation restoration");
    *invalid=*pending_activation;put(invalid->node_domains[0].nodes[0][0],0x10,1u);
    put(invalid->actors[0][0].kinematic_admission.islands[0].node,0x10,1u);
    expect(!World::PreparePhysicsProjection(*invalid,*invalid).ok(),"unchanged pending nodes still require zero native edge references");
    *invalid=*pending_activation;put(invalid->actors[0][0].kinematic_admission.islands[0].list_headers[1],8,0u);
    expect(!World::PreparePhysicsProjection(*a,*invalid).ok(),"pending flag requires its complete activation input");
    const auto before_activation_sims=sims;
    const auto before_activation_nodes=nodes;
    *reinterpret_cast<unsigned*>(activation_page)=64;
    expect(!World::InstallPhysicsProjection(*pending_activation,*a).ok() && sims==before_activation_sims && nodes==before_activation_nodes,
        "changed native activation input rejects before any node or body writes");
    *reinterpret_cast<unsigned*>(activation_page)=0;
    island_ids[0][0]=0;
    expect(!World::InstallPhysicsProjection(*pending_activation,*a).ok() && sims==before_activation_sims,
        "changed native island affiliation rejects activation publication");
    island_ids[0][0]=0xffffffffu;
    put(islands,0xb0+0x160,1u);
    expect(!World::InstallPhysicsProjection(*pending_activation,*a).ok() && sims==before_activation_sims,
        "new unrelated native island work rejects before restoration");
    put(islands,0xb0+0x160,0u);
    expect(VirtualProtect(activation_page,0x1000,PAGE_READONLY,&old)!=0,"protect pending activation storage");
    expect(!World::InstallPhysicsProjection(*pending_activation,*a).ok() && sims==before_activation_sims,
        "activation destination is preflighted separately from notification storage");
    expect(VirtualProtect(activation_page,0x1000,PAGE_READWRITE,&old)!=0,"restore activation destination access");
    expect(World::InstallPhysicsProjection(*pending_activation,*a).ok(),"publish A order while complete B owns pending wake and activation");
    observe_activity(*a);expect(World::ReadPhysicsNotifications(*observed,0).ok()
        && World::ValidatePhysicsProjection(*a,*observed).ok(),"native reads establish A through current activation backing");
    expect(World::InstallPhysicsProjection(*observed,*pending_activation).ok(),"recover complete B pending activation and wake notification together");
    observe_activity(*pending_activation);expect(World::ReadPhysicsNotifications(*observed,0).ok()
        && World::ValidatePhysicsProjection(*pending_activation,*observed).ok(),"native reads establish both recovered B input lists and flags");
    expect(World::InstallPhysicsProjection(*observed,*pending_activation).ok(),"repeated B restoration retains one activation and one wake");
    VirtualFree(activation_page,0,MEM_RELEASE);
    VirtualFree(notification_page,0,MEM_RELEASE);
    std::printf("physics node domain checkpoint bytes=%zu\n",sizeof(World::PhysicsBoundary::NodeDomain)*2);
    VirtualFree(index_page,0,MEM_RELEASE);VirtualFree(code,0,MEM_RELEASE);
}

void test_physics_projection_public_admission()
{
    using World = Sc6ReplayWorldState;
    {
        std::array<std::byte,0xa0> native_shape{};
        const unsigned triangle=5;const std::uintptr_t mesh=0x123456789abcdef0ull;
        std::memcpy(native_shape.data()+0x68,&triangle,4);
        native_shape[0x68+0x20]=std::byte{1};
        std::memcpy(native_shape.data()+0x68+0x28,&mesh,8);
        World::PhysicsBoundary::ActorObservation::QueryShapeObservation shape{};
        expect(World::ReadPhysicsShapeGeometry(native_shape.data(),shape).ok()
            && shape.geometry_bytes==48 && shape.geometry.size()>=48,
            "native triangle geometry capture retains full descriptor including mesh pointer");
        if(shape.geometry_bytes==48 && shape.geometry.size()>=48)
            expect(!std::memcmp(shape.geometry.data(),native_shape.data()+0x68,48),
                "triangle capture retains scale flags and borrowed mesh identity without modifying native data");
        const unsigned unsupported=6;std::memcpy(native_shape.data()+0x68,&unsupported,4);
        expect(!World::ReadPhysicsShapeGeometry(native_shape.data(),shape).ok(),"unverified heightfield geometry remains unsupported");
        World::PhysicsBoundary::ActorObservation actor{};actor.kind=2;actor.query_handle_count=1;
        actor.query_shapes[0].geometry_kind=5;
        expect(!actor.mutable_projection(),"triangle value capture never grants writable body restoration");
    }
    auto a = std::make_unique<World::PhysicsBoundary>();
    auto b = std::make_unique<World::PhysicsBoundary>();
    a->observed_actors[0] = b->observed_actors[0] = 1;
    a->actors[0][0].query_handle_count = b->actors[0][0].query_handle_count = 17;
    a->scenes[0] = b->scenes[0] = 1; // Deliberately unreadable: rejection must precede native access.
    expect(World::PreparePhysicsProjection(*a, *b).code == FailureCode::CapacityExceeded
        && World::InstallPhysicsProjection(*a, *b).code == FailureCode::CapacityExceeded
        && World::ValidatePhysicsProjection(*a, *b).code == FailureCode::CapacityExceeded
        && World::ReconstructPhysicsQueries(*a).code == FailureCode::CapacityExceeded,
        "every public physics entry rejects oversized shape observations before native access");
    a = std::make_unique<World::PhysicsBoundary>();
    b = std::make_unique<World::PhysicsBoundary>();
    a->observed_actors[0] = 65;
    expect(World::PreparePhysicsProjection(*a, *b).code == FailureCode::CapacityExceeded,
        "physics admission rejects actor count before array traversal");
    a = std::make_unique<World::PhysicsBoundary>();
    a->observed_actors[0] = 1;
    auto& actor = a->actors[0][0];
    actor.kind = 2;
    actor.query_handle_count = 1;
    actor.query_shapes[0].geometry_kind = 4;
    actor.query_shapes[0].flags = 8;
    actor.query_handles[0] = 0xffffffff;
    *b = *a;
    expect(World::PreparePhysicsProjection(*a, *b).ok() && World::InstallPhysicsProjection(*a, *b).ok()
        && World::ValidatePhysicsProjection(*a, *b).ok(),
        "immutable convex witness is admitted consistently without fabricated writable kinematic storage");
    // Keep this large snapshot off the default Windows test-thread stack.
    a = std::make_unique<World::PhysicsBoundary>();
    a->observed_actors[0] = 1;
    auto& moving = a->actors[0][0];
    moving.kind = 2;
    moving.component = moving.body = moving.simulation = moving.kinematic = 1;
    moving.properties[0x9c] = std::byte{3};
    const unsigned control = 0x83000000;
    std::memcpy(moving.dynamic_storage.data() + 0x68, &control, 4);
    moving.kinematic_storage[0x1f] = std::byte{1};
    moving.kinematic_admission.valid = true;
    moving.kinematic_admission.controller_vtable = 0x1ab0e8;
    for (auto& island : moving.kinematic_admission.islands) island.node[4] = std::byte{2};
    *b = *a;
    moving.simulation_storage[0xb4] = std::byte{4};
    moving.kinematic_storage[0x1c] = std::byte{1};
    expect(World::PreparePhysicsProjection(*a, *b).ok() && World::PreparePhysicsProjection(*b, *a).ok(),
        "pending kinematic target supports forward and complete undo with unchanged active island owners");
    expect(!World::ValidatePhysicsProjection(*a, *b).ok(),
        "different pending target is never accepted as an installed image");
    b->actors[0][0].kinematic_admission.islands[1].node[4] = std::byte{0};
    expect(!World::PreparePhysicsProjection(*a, *b).ok(),
        "target restoration rejects a wake-list membership transition");
    b->actors[0][0].kinematic_admission = moving.kinematic_admission;
    moving.simulation_storage[0xb4] = std::byte{0x0c};
    expect(!World::PreparePhysicsProjection(*a, *b).ok(),
        "target restoration does not admit unrelated BodySim flags");
    moving.simulation_storage[0xb4] = std::byte{4};
    moving.kinematic_storage[0x1c] = std::byte{0};
    expect(!World::PreparePhysicsProjection(*a, *b).ok(),
        "moved flag without its target is rejected before publication");
    moving.kinematic_storage[0x1c] = std::byte{1};
    const std::uintptr_t inline_storage=moving.simulation+8;
    const unsigned inline_capacity=4;
    std::memcpy(moving.simulation_storage.data()+0x28,&inline_storage,8);
    std::memcpy(moving.simulation_storage.data()+0x30,&inline_capacity,4);
    *b=*a;
    b->actors[0][0].simulation_storage[8]=std::byte{0x71};
    b->actors[0][0].simulation_storage[0x10]=std::byte{0x52};
    expect(World::PreparePhysicsProjection(*a,*b).ok() && World::ValidatePhysicsProjection(*a,*b).ok()
        && World::ValidatePhysicsProjection(*b,*a).ok(),
        "empty inline interaction residue is not restored or treated as active physics state");
    b->actors[0][0].simulation_storage[0x34]=std::byte{1};
    expect(!World::PreparePhysicsProjection(*a,*b).ok(),"active interaction count cannot use empty-array exemption");
    b->actors[0][0].simulation_storage[0x34]=std::byte{0};
    b->actors[0][0].simulation_storage[0x30]=std::byte{8};
    expect(!World::PreparePhysicsProjection(*a,*b).ok(),"interaction allocation change remains a binding failure");
    b->actors[0][0].simulation_storage[0x30]=std::byte{4};
    b->actors[0][0].simulation_storage[0xb6]^=std::byte{8};
    expect(!World::PreparePhysicsProjection(*a,*b).ok(),"empty-array exemption preserves solver-state validation");
    // Insertion114080 writes data[count] before publishing count. Removed
    // entries beyond the active prefix can differ after a previous restore.
    for(unsigned count=1;count<=4;++count) {
        moving.interaction_count=count;
        std::memcpy(moving.simulation_storage.data()+0x34,&count,4);
        for(unsigned i=0;i<count;++i) {
            moving.interactions[i].address=0x123400+i*0x100;
            moving.interactions[i].header[0x24]=std::byte{2};
            std::memcpy(moving.simulation_storage.data()+8+i*8,&moving.interactions[i].address,8);
        }
        *b=*a;
        if(count<4) b->actors[0][0].simulation_storage[8+count*8]^=std::byte{0x71};
        expect(World::PreparePhysicsProjection(*a,*b).ok() && World::ValidatePhysicsProjection(*a,*b).ok()
            && World::ValidatePhysicsProjection(*b,*a).ok(),"unused inline suffix preserves the unchanged active prefix and B undo");
        b->actors[0][0].simulation_storage[8+(count-1)*8]^=std::byte{0x52};
        expect(!World::PreparePhysicsProjection(*a,*b).ok(),"every active inline interaction pointer remains strict");
        *b=*a;
        const std::uintptr_t heap_binding=0x8000;const unsigned heap_capacity=8;
        std::memcpy(b->actors[0][0].simulation_storage.data()+0x28,&heap_binding,8);
        std::memcpy(b->actors[0][0].simulation_storage.data()+0x30,&heap_capacity,4);
        b->actors[0][0].simulation_storage[8]^=std::byte{0x71};
        expect(World::PreparePhysicsProjection(*a,*b).ok() && World::ValidatePhysicsProjection(*a,*b).ok()
            && World::ValidatePhysicsProjection(*b,*a).ok(),"identical active graph survives native inline/heap storage transition");
        b->actors[0][0].interactions[count-1].address^=8;
        expect(!World::PreparePhysicsProjection(*a,*b).ok(),"heap rebinding cannot omit changed active membership");
        *b=*a;const unsigned bad_capacity=6;
        std::memcpy(b->actors[0][0].simulation_storage.data()+0x30,&bad_capacity,4);
        expect(!World::PreparePhysicsProjection(*a,*b).ok(),"unknown interaction allocation layout rejects");
        *b=*a;b->actors[0][0].interactions[count-1].header[0x26]=std::byte{1};
        expect(!World::PreparePhysicsProjection(*a,*b).ok(),"active interaction payload is never treated as unused suffix");
    }
    // Rebuilt inactive markers retain the current native owner; no old address
    // or padding byte is published. The live reader supplies registration proof.
    moving.interaction_count=1;
    moving.interactions={};
    const unsigned marker_count=1;
    std::memcpy(moving.simulation_storage.data()+0x34,&marker_count,4);
    auto& marker=moving.interactions[0];
    marker.address=0x123400;
    marker.header[0x24]=std::byte{2}; marker.header[0x25]=std::byte{0xb};
    marker.marker_elements={0x100000,0x100100}; marker.marker_filter_pair=0xffffffff;
    marker.marker_registered=true;
    marker.marker_filter_valid=true;
    marker.marker_scene_count=marker.marker_map_count=1;
    marker.marker_actor_counts={1,1};
    marker.marker_attributes={0x11,0x11};
    marker.marker_shape_cores={0x200000,0x200100};
    std::memcpy(moving.simulation_storage.data()+8,&marker.address,8);
    *b=*a;
    auto& rebuilt=b->actors[0][0].interactions[0];
    rebuilt.address+=64; rebuilt.header[0x27]=std::byte{0x25};
    std::memcpy(b->actors[0][0].simulation_storage.data()+8,&rebuilt.address,8);
    const auto original_a=std::make_unique<World::PhysicsBoundary>(*a);
    const auto original_b=std::make_unique<World::PhysicsBoundary>(*b);
    expect(World::PreparePhysicsProjection(*a,*b).ok() && World::ValidatePhysicsProjection(*a,*b).ok()
        && World::ValidatePhysicsProjection(*b,*a).ok(),"registered same-element marker replacement supports both A comparison and B undo");
    expect(*a==*original_a && *b==*original_b,"marker binding comparison never changes either retained image");
    for(unsigned witness=0;witness<8;++witness) {
        const auto saved=rebuilt;
        switch(witness) {
        case 0:++rebuilt.marker_scene_count;break;
        case 1:++rebuilt.marker_scene_active;break;
        case 2:++rebuilt.marker_map_count;break;
        case 3:++rebuilt.marker_actor_counts[0];break;
        case 4:rebuilt.marker_filter_valid=false;break;
        case 5:++rebuilt.marker_attributes[0];break;
        case 6:++rebuilt.marker_filter_data[1][2];break;
        case 7:++rebuilt.marker_shape_cores[1];break;
        }
        expect(!World::PreparePhysicsProjection(*a,*b).ok()
            && !World::ValidatePhysicsProjection(*a,*b).ok(),
            "marker rebinding preserves filter inputs and complete membership witnesses");
        rebuilt=saved;
    }
    rebuilt.marker_registered=false;
    expect(!World::PreparePhysicsProjection(*a,*b).ok(),"marker without current scene and pair-map registration rejects");
    rebuilt.marker_registered=true;rebuilt.marker_elements[1]+=8;
    expect(!World::PreparePhysicsProjection(*a,*b).ok(),"different shape-element pair rejects marker rebinding");
    rebuilt.marker_elements=marker.marker_elements;rebuilt.marker_filter_pair=0;
    expect(!World::PreparePhysicsProjection(*a,*b).ok(),"callback-owned filter pair cannot be rebound");
    rebuilt.marker_filter_pair=0xffffffff;rebuilt.header[0x18]^=std::byte{1};
    expect(!World::PreparePhysicsProjection(*a,*b).ok(),"scene registration order remains strict");
    rebuilt.header[0x18]^=std::byte{1};rebuilt.header[0x26]=std::byte{1};
    expect(!World::PreparePhysicsProjection(*a,*b).ok(),"pending marker filtering is authoritative state");
}
}
#endif

void test_sparse_particle_batch_removal()
{
    std::array<std::uint64_t, 5> slots{100, 0xffffffffffffffffull, 200, 300, 400};
    std::array<std::uint32_t, 1> flags{0x1d};
    std::int32_t head=1, count=1;
    const auto undo_slots=slots; const auto undo_flags=flags;
    const std::array<std::int32_t, 2> removed{4,2};
    expect(PlanReplaySparseRemoval(slots, flags, head, count, removed), "multiple nonadjacent GPU owners admitted");
    expect(head==2 && count==3 && flags[0]==9 && slots[0]==100 && slots[3]==300
        && slots[2]==0x00000004ffffffffull && slots[4]==0x0000000100000002ull
        && slots[1]==0xffffffff00000004ull, "native sparse free-chain order preserved across removals");
    const auto rejected_slots=slots; const auto rejected_flags=flags;
    expect(!PlanReplaySparseRemoval(slots, flags, head, count, removed)
        && slots==rejected_slots && flags==rejected_flags && head==2 && count==3,
        "already removed owners reject before a write");
    slots=undo_slots;flags=undo_flags;head=1;count=1;
    const std::array<std::int32_t, 2> duplicate{2,2};
    expect(!PlanReplaySparseRemoval(slots, flags, head, count, duplicate)
        && slots==undo_slots && flags==undo_flags && head==1 && count==1,
        "duplicate removal retains complete B");
    slots[1]=0x00000001ffffffffull;
    const auto corrupt=slots;
    expect(!PlanReplaySparseRemoval(slots, flags, head, count, removed) && slots==corrupt,
        "corrupt free chain rejects without partial planning");
    std::span<std::uint64_t> empty_slots; std::span<std::uint32_t> empty_flags;
    head=-1;count=0;
    expect(PlanReplaySparseRemoval(empty_slots,empty_flags,head,count,{}), "CPU-only delta admits an unchanged empty GPU registry");
}

#ifdef _WIN32
void test_replay_output_window_message_ownership()
{
    Horse::GameImGui::ReplayOutputWindow window;
    for(int attempt=0;attempt<2;++attempt) {
        const bool opened=window.open(64,64);
        expect(opened,"paused output window opens on its own message thread");
        if(opened) {
            DWORD pid{};
            const auto thread=GetWindowThreadProcessId(window.hwnd(),&pid);
            expect(pid==GetCurrentProcessId() && thread!=GetCurrentThreadId() && GetParent(window.hwnd())==nullptr,
                "output message ownership is independent of game/render parents");
            DWORD_PTR reply{};
            for(int message=0;message<3;++message)
                expect(SendMessageTimeoutW(window.hwnd(),WM_NULL,0,0,SMTO_ABORTIFHUNG,250,&reply)!=0,
                    "output remains responsive while caller performs no message pumping");
        }
        expect(window.close() && !window.active() && !window.hwnd(),"output thread and HWND retire together");
        expect(window.close(),"completed output retirement is idempotent");
    }
}
#endif

#ifdef _WIN32
static void test_vfx_handler_nested_slot_storage() {
    using Graph=ReplayVfxHandlerStorage::Graph;
    std::array<std::byte,0x50> root{};
    std::array<std::byte,0x20> row{};
    std::array<int,2> ids{7,7};
    const auto put=[](auto& buffer,std::size_t offset,auto value) {std::memcpy(buffer.data()+offset,&value,sizeof(value));};
    put(root,0,row.data());put(root,8,1);put(root,12,1);put(root,0x10,1u);
    put(root,0x28,1);put(root,0x2c,128);put(root,0x30,-1);put(root,0x38,0);put(root,0x48,1);
    put(row,0,2005u);put(row,8,ids.data());put(row,16,2);put(row,20,2);put(row,24,-1);put(row,28,0);
    auto graph=std::make_unique<Graph>();graph->budget=1024*1024;
    expect(graph->Add(root.data(),root.size())==0 && graph->Map(0,0,true),"VFX nested slot map capture");
    expect(graph->count==3 && graph->Matches(),"VFX nested map retains complete source graph");
    std::array<void*,Graph::limit> relocated{};
    std::array<std::vector<std::byte>,3> copies;
    for(std::size_t i=0;i<graph->count;++i){copies[i]=graph->nodes[i].bytes;relocated[i]=copies[i].data();}
    for(std::size_t i=1;i<graph->count;++i){const auto& node=graph->nodes[i];std::memcpy(copies[node.parent].data()+node.offset,&relocated[i],8);}
    expect(graph->Matches(&relocated),"VFX private image permits only declared pointer relocation");
    ids[0]=99;
    expect(!graph->Matches() && graph->Matches(&relocated),"VFX private captured image survives subsequent native slot writes");
    ids[0]=7;copies[2][0]=std::byte{9};
    expect(!graph->Matches(&relocated) && graph->Matches(),"VFX changed target slot is detected with complete B intact");
    put(row,24,0);auto cycle=std::make_unique<Graph>();cycle->budget=1024*1024;
    expect(cycle->Add(root.data(),root.size())==0 && !cycle->Map(0,0,true),"VFX map rejects hash cycle before publication");
    put(row,24,-1);put(row,8,root.data());auto alias=std::make_unique<Graph>();alias->budget=1024*1024;
    expect(alias->Add(root.data(),root.size())==0 && !alias->Map(0,0,true),"VFX map rejects nested allocation alias before retirement");
    auto capacity=std::make_unique<Graph>();capacity->budget=sizeof(Graph);
    expect(capacity->Add(root.data(),root.size())==-2,"VFX graph capacity failure leaves source untouched");
}
static void test_vfx_handler_execution_graph_ownership() {
    using Graph=ReplayVfxHandlerStorage::Graph;
    std::array<std::byte,0x88> root{};
    std::array<int,2> original_b{7,11};
    const auto install=[&](int* pointer,int count,int capacity) {
        std::memcpy(root.data()+0x78,&pointer,8);
        std::memcpy(root.data()+0x80,&count,4);
        std::memcpy(root.data()+0x84,&capacity,4);
    };
    install(original_b.data(),2,2);
    auto b=std::make_unique<Graph>();b->budget=1024*1024;
    expect(b->Runtime(root.data()) && b->Matches(),"handler B runtime graph is captured before publication");
    auto original_a=std::make_unique<int[]>(1);original_a[0]=3;install(original_a.get(),1,1);
    // Native append reallocates A. The old A address is deliberately freed;
    // settlement must discover C from the actual root instead of retaining it.
    auto current_c=std::make_unique<int[]>(4);current_c[0]=3;current_c[1]=19;
    install(current_c.get(),2,4);original_a.reset();
    auto c=std::make_unique<Graph>();c->budget=1024*1024;
    expect(c->Runtime(root.data()) && c->Matches() && c->AllocationsDisjoint(*b)
        && b->AllocatedValuesMatch(),"handler execution settlement sees grown C with private B intact");
    expect(c->count==2 && c->nodes[1].address==reinterpret_cast<std::byte*>(current_c.get()),
        "handler retirement selects actual post-advance allocation");
    original_b[1]=99;
    expect(!b->AllocatedValuesMatch(),"handler recovery rejects damaged displaced B");original_b[1]=11;
    install(original_b.data(),2,2);
    auto alias=std::make_unique<Graph>();alias->budget=1024*1024;
    expect(alias->Runtime(root.data()) && !alias->AllocationsDisjoint(*b),
        "handler execution settlement rejects C aliasing retained B");
    install(original_b.data()+1,1,1);
    auto overlap=std::make_unique<Graph>();overlap->budget=1024*1024;
    expect(overlap->Runtime(root.data()) && !overlap->AllocationsDisjoint(*b),
        "handler execution settlement rejects partial B allocation overlap");
    std::memcpy(root.data(),b->nodes[0].bytes.data(),root.size());
    expect(b->Matches(),"handler B root and private payload remain independently verifiable after execution");
    auto failed=std::make_unique<Graph>();failed->budget=1024*1024;
    expect(failed->Add(reinterpret_cast<void*>(1),64)==-2 && failed->count==0
        && failed->owned_bytes()>=sizeof(Graph)+64,
        "handler failed graph read retains its scratch memory charge");
}
#endif

static void test_registry_execution_backing_transaction()
{
#ifdef _WIN32
    struct Heap {
        std::unordered_map<void*, std::size_t> live;
        std::vector<void*> retired;
        unsigned double_frees{};
        ~Heap() { for (auto& [p, n] : live) ::operator delete(p); for (auto* p : retired) ::operator delete(p); }
        static void* Allocate(void* context, std::size_t bytes) {
            auto& h = *static_cast<Heap*>(context);
            auto* p = ::operator new(bytes); std::memset(p, 0, bytes); h.live.emplace(p, bytes); return p;
        }
        static void Release(void* context, void* p) {
            auto& h = *static_cast<Heap*>(context); const auto found = h.live.find(p);
            if (found == h.live.end()) { ++h.double_frees; return; }
            std::memset(p, 0xdd, found->second); h.live.erase(found); h.retired.push_back(p);
        }
        static std::size_t Charge(void*, std::size_t bytes) { return bytes; }
    };
    struct Registry {
        std::uint64_t* slots{}; int count{}, capacity{};
        std::array<std::uint32_t, 4> inline_flags{};
        std::uint32_t* flags{}; int bits{}, max_bits{}, head{-1}, free_count{};
    };
    static_assert(sizeof(Registry) == 0x38);
    for (const bool commit : {false, true}) {
        Heap heap;
        auto* root = static_cast<Registry*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        expect(root != nullptr, "registry fixture root allocation");
        if (!root) continue;
        *root = {};
        root->slots = static_cast<std::uint64_t*>(Heap::Allocate(&heap, 32));
        root->slots[0] = 0x10000; root->slots[1] = 0x20000;
        root->count = root->bits = 2; root->capacity = 4; root->max_bits = 128; root->inline_flags[0] = 3;
        const Registry original = *root;
        const std::array<std::uint64_t, 4> original_values{0x10000, 0x20000, 0, 0};
        ReplaySparseRegistryStorage transaction;
        const ReplaySparseRegistryStorage::Heap operations{&heap, &Heap::Allocate, &Heap::Release, &Heap::Charge};
        expect(!transaction.Prepare(root, operations, 63) && heap.live.size() == 1,
            "registry budget rejects before allocating or changing B");
        expect(transaction.Prepare(root, operations, 1024) && transaction.Publish(),
            "registry publication retains B and installs independent native backing");
        auto* a = root->slots;
        expect(a != original.slots && !std::memcmp(original.slots, original_values.data(), 32),
            "published registry never modifies displaced B backing");
        expect(transaction.BeginPublicationWrite(), "registry explicitly owns native exclusion writes before execution");
        const std::array<std::int32_t, 1> excluded{1};
        expect(PlanReplaySparseRemoval({root->slots, std::size_t(root->count)},
            {root->inline_flags.data(), 1}, root->head, root->free_count, excluded)
            && transaction.SealPublicationWrite()
            && !std::memcmp(original.slots, original_values.data(), 32),
            "native-style sparse exclusion modifies only A and seals its free chain");
        expect(transaction.BeginExecution(256), "registry execution explicitly hands A storage to native ownership");
        // Emulate native growth: retire A, publish new slot and heap-flag
        // allocations, and register another actual C entry. No expected data
        // is installed by the production transaction.
        auto* c = static_cast<std::uint64_t*>(Heap::Allocate(&heap, 64));
        std::memcpy(c, a, 16); c[1] = 0x40000; c[2] = 0x30000;
        Heap::Release(&heap, a);
        root->slots = c; root->capacity = 8; root->count = root->bits = 3;
        root->head = -1; root->free_count = 0;
        root->flags = static_cast<std::uint32_t*>(Heap::Allocate(&heap, 32));
        root->flags[0] = 7; root->max_bits = 256;
        auto* c_flags = root->flags;
        expect(!transaction.Undo() && root->slots == c, "unsettled execution cannot restore or retire registry storage");
        root->slots = original.slots;
        expect(!transaction.SettleExecution() && heap.live.contains(original.slots), "C/B registry backing alias rejects without retiring B");
        root->slots = c;
        original.slots[0] ^= 1;
        expect(!transaction.SettleExecution(), "private B corruption rejects settlement");
        original.slots[0] ^= 1;
        expect(transaction.SettleExecution(), "registry settlement adopts grown C and verifies untouched B");
        if (commit) {
            expect(transaction.Commit() && root->slots == c && root->flags == c_flags
                && !heap.live.contains(original.slots) && heap.live.contains(c) && heap.live.contains(c_flags),
                "registry commit retires only B and transfers actual C to native ownership");
        } else {
            DWORD previous{}, unused{};
            const bool protected_root = VirtualProtect(root, 4096, PAGE_READONLY, &previous) != 0;
            expect(protected_root, "registry interrupted-publication fixture protection");
            if (protected_root) {
                expect(!transaction.Undo() && heap.live.contains(c) && heap.live.contains(original.slots),
                    "failed registry undo retains both backing graphs");
                expect(VirtualProtect(root, 4096, previous, &unused) != 0, "registry fixture restores root protection");
            }
            expect(transaction.Undo() && !std::memcmp(root, &original, sizeof(original))
                && !std::memcmp(original.slots, original_values.data(), 32)
                && heap.live.contains(original.slots) && !heap.live.contains(c) && !heap.live.contains(c_flags),
                "retry recovers exact B header and backing, then retires actual C");
        }
        expect(!transaction.pending() && transaction.owned_bytes() == 0 && heap.double_frees == 0,
            "registry retirement never frees stale A or another owner's allocation twice");
        VirtualFree(root, 0, MEM_RELEASE);
    }
    {
        Heap heap; Registry root{};
        root.slots = static_cast<std::uint64_t*>(Heap::Allocate(&heap, 32));
        root.slots[0] = 0x10000; root.count = root.bits = 1;
        root.capacity = 4; root.max_bits = 128; root.inline_flags[0] = 1;
        const auto original = root;
        ReplaySparseRegistryStorage transaction;
        expect(transaction.Prepare(&root, {&heap, &Heap::Allocate, &Heap::Release, &Heap::Charge}, 1024)
            && transaction.Publish() && transaction.BeginPublicationWrite(), "registry partial exclusion fixture admission");
        auto* a = root.slots;
        root.head = 999; root.free_count = 1; root.inline_flags[0] = 0;
        expect(!transaction.SealPublicationWrite() && !transaction.BeginExecution(256),
            "broken exclusion cannot seal or admit execution");
        root.slots = original.slots;
        expect(!transaction.Undo() && heap.live.contains(original.slots),
            "partial exclusion cannot waive backing lifetime checks");
        root.slots = a;
        expect(transaction.Undo() && !std::memcmp(&root, &original, sizeof(root))
            && root.slots[0] == 0x10000 && heap.live.size() == 1 && heap.double_frees == 0,
            "interrupted exclusion recovers B despite incomplete A free links");
    }
#endif
}

static void test_stage_render_target_deferral()
{
    constexpr std::uintptr_t base=0x140000000;
    std::array<std::byte,0x500> actor{};
    std::array<std::byte,0x1000> component{};
    std::array<std::byte,0x80> mesh{};
    const auto a=reinterpret_cast<std::uintptr_t>(actor.data());
    const auto p=reinterpret_cast<std::uintptr_t>(component.data());
    const auto m=reinterpret_cast<std::uintptr_t>(mesh.data());
    const std::uintptr_t render_data=0x123400;
    const auto put=[](std::uintptr_t address,const auto& value){std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));};
    const auto read=[](std::uintptr_t address,auto& value){std::memcpy(&value,reinterpret_cast<void*>(address),sizeof(value));return true;};
    ReplayStageRenderOwner plan{};
    auto& b=plan.original;b.actor=a;b.type=base+0x32d2368;b.weak={1,2};b.fade_frames=10;b.enabled=1;b.component_count=1;
    auto& c=b.components[0];c.object=p;c.type=base+0x36cefb0;c.weak={3,4};c.member_offset=0x3a8;
    c.asset_offset=0x920;c.asset=m;c.primitive_id=441;c.visibility=0x401;c.flags=3;
    plan.target_visibility[0]=0x411;
    put(a,b.type);put(a+c.member_offset,p);put(p,c.type);put(p+0x190,a);put(p+0x920,m);
    put(m+0x38,render_data);put(p+0x420,c.primitive_id);put(p+0x240,c.visibility);put(p+0x188,c.flags);
    const auto resolve=[&](const auto& weak){return weak==b.weak?a:weak==c.weak?p:std::uintptr_t{};};
    const auto accepts=[&](const auto& owner){return owner.TargetDestination(base,p,c.weak,441,base+0x36bd1a8,2,m,render_data,read,resolve);};
    const auto before=component;
    auto target=b;target.components[0].visibility=0x411;
    expect(plan.Matches(target,b),"stage render plan pins both input images");
    auto changed_b=b;changed_b.components[0].visibility=0x411;
    expect(!plan.Matches(target,changed_b),"preparation retry cannot replace complete B visibility");
    expect(accepts(plan) && component==before,"hidden B static mesh admits owned target deferral without native writes");
    put(p+0x240,std::uint32_t{0x411});
    expect(!accepts(plan),"visible target cannot omit its native recreate queue");
    put(p+0x188,std::uint32_t{0x23});expect(!accepts(plan),"dirty flag alone cannot prove native queue admission");
    put(p+0x188,std::uint32_t{0x40000023});
    expect(accepts(plan),"published target requires native recreate and scheduling bits");
    put(p+0x790,std::uintptr_t{0x456700});expect(!accepts(plan),"a materialized destination must use live scene binding instead of deferral");
    put(p+0x790,std::uintptr_t{});put(p+0x188,std::uint32_t{0x40000123});
    expect(!accepts(plan),"unowned component flags remain strict");
    component=before;expect(accepts(plan),"restored B visibility and flags remain valid before native execution");
    for(unsigned mutation=0;mutation<6;++mutation) {
        auto changed=plan;
        if(mutation==0)changed.original.components[0].weak[1]++;
        if(mutation==1)changed.original.components[0].asset++;
        if(mutation==2)changed.original.components[0].primitive_id++;
        if(mutation==3)changed.original.kind=ReplayStageVisibility::Kind::Emitter;
        if(mutation==4)changed.target_visibility[0]=0x401;
        if(mutation==5)changed.original.components[0].visibility=0x411;
        expect(!accepts(changed),"changed lifetime/asset/ID or unsupported target cannot defer rendering");
    }
    put(a+c.member_offset,std::uintptr_t{});expect(!accepts(plan),"missing stage membership rejects despite a retained component");put(a+c.member_offset,p);
    put(m+0x38,render_data+8);expect(!accepts(plan),"changed native render-data owner rejects");
    std::cout << "Stage target deferral additional bytes per owner=" << sizeof(ReplayStageRenderOwner)-sizeof(ReplayStageVisibility) << " GPU_readbacks=0\n";
}

static void test_stage_visibility_source_transaction()
{
    ReplayStageParticleAdmission admission;
    expect(admission.Retain(0x40c) && admission.Accepts(0x60c),"native render creation may set admission bit while complete original flags remain retained");
    expect(admission.original==0x40c && admission.Retain(0x60c) && admission.original==0x40c,"retry cannot adopt native recreation side effect as original simulation state");
    expect(!admission.Accepts(0x60d) && !admission.Retain(0x68c),"unexpected native flag changes and finalization reject without dropping undo");
    expect(admission.Accepts(0x40c),"unchanged flags remain valid when native recreation was unnecessary");
    using Row=ReplayStageVisibility;
    std::array<std::byte,0x500> actor{};
    std::array<std::array<std::byte,0x1100>,3> components{};
    const auto address=reinterpret_cast<std::uintptr_t>(actor.data());
    const auto put=[](std::uintptr_t p,const auto& v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof(v));return true;};
    const auto read=[](std::uintptr_t p,auto& v){
        if(p==0x140000000ull+0x36cefb0+0x2d8) {const std::uintptr_t entry=0x141d58ac0;std::memcpy(&v,&entry,sizeof(v));return true;}
        std::memcpy(&v,reinterpret_cast<const void*>(p),sizeof(v));return true;
    };
    const auto weak=[](std::uintptr_t p,auto& pair){pair={static_cast<int>(p),static_cast<int>(p>>32)};return true;};
    constexpr std::uintptr_t base=0x140000000;
    put(address,base+0x32d2368);put(address+0x3c0,10);put(address+0x3c4,std::uint8_t{1});
    put(address+0x389,std::uint8_t{1});put(address+0x3c8,0.25f);put(address+0x3cc,0.1f);
    for(unsigned i=0;i<3;++i) {
        const auto p=reinterpret_cast<std::uintptr_t>(components[i].data());
        put(address+0x3a8+i*8,p);put(p,base+0x36cefb0);put(p+0x190,address);
        put(p+0x188,std::uint32_t{3});put(p+0x240,std::uint32_t{0x401});put(p+0x420,i+1);
    }
    Row a{},b{},observed{};
    expect(a.ReadFrom(base,address,read,weak),"stage captures source values and member identities");
    put(address+0x389,std::uint8_t{0});put(address+0x3c8,1.0f);put(address+0x3cc,0.0f);
    expect(b.ReadFrom(base,address,read,weak) && a.SameBinding(b),"stage fade advancement preserves immutable owner binding");
    unsigned writes{};
    expect(!a.WriteValues([&](std::uintptr_t p,const auto& v){return ++writes!=3 && put(p,v);}),"partial stage publication reports failure");
    expect(b.WriteValues(put) && observed.ReadFrom(base,address,read,weak) && observed.values==b.values
        && observed.SameBinding(b),"complete B source undo recovers after partial publication without pointer writes");
    auto invalid=a;invalid.values.rate=std::numeric_limits<float>::quiet_NaN();writes=0;
    expect(!invalid.WriteValues([&](std::uintptr_t,const auto&){++writes;return true;}) && writes==0,"invalid fade rejects before source publication");
    auto dormant=b;dormant.components[0].flags^=0x20000000u;
    expect(b.SameBinding(dormant),"base non-ticking component logical registration is owned source state");
    for(unsigned forbidden: {2u,0x40u}) {
        auto live=b,changed=dormant;live.components[0].primary_tick_flags|=forbidden;changed.components[0].primary_tick_flags|=forbidden;
        expect(!live.SameBinding(changed),"possible or registered native ticks cannot use dormant registration ownership");
    }
    auto overridden=b,changed=dormant;overridden.components[0].base_registration=false;changed.components[0].base_registration=false;
    expect(!overridden.SameBinding(changed),"subclass registration override rejects logical flag rebinding");
    auto registered=b,unregistered=b;
    for(auto* row:{&registered,&unregistered}) {
        auto& c=row->components[0];c.base_registration=false;c.primitive_registration=true;
        c.registration_entry=base+0x1da9000;c.primary_tick_flags=6;c.secondary_tick_flags=6;
    }
    registered.components[0].flags|=0x20000000u;
    registered.components[0].primary_tick_flags|=0x40;
    registered.components[0].secondary_tick_flags|=0x40;
    expect(!registered.SameBinding(unregistered),"two native ticks require explicit scheduler ownership");
    expect(registered.SameBinding(unregistered,1),"owned registration permits both native membership transitions");
    expect(!registered.SameBinding(unregistered,2),"another component ownership receipt cannot admit this transition");
    auto changed_capability=unregistered;changed_capability.components[0].secondary_tick_flags^=2;
    expect(!registered.SameBinding(changed_capability,1),"secondary CanEverTick changes remain rejected");
    changed_capability=unregistered;changed_capability.components[0].registration_entry++;
    expect(!registered.SameBinding(changed_capability,1),"changed registration virtual remains rejected with scheduler ownership");
    changed_capability=unregistered;changed_capability.components[0].weak[1]++;
    expect(!registered.SameBinding(changed_capability,1),"scheduler ownership cannot admit a replacement component generation");
    auto rebound=b;rebound.components[0].weak[1]++;
    expect(!b.SameBinding(rebound),"reused component generation rejects");
    rebound=b;rebound.components[1].materials=1;
    expect(!b.SameBinding(rebound),"replaced stage material backing rejects");
    rebound=b;rebound.components[0].children=1;
    expect(!b.SameBinding(rebound),"changed descendant ownership rejects");
    rebound=b;rebound.kind=Row::Kind::Wall;rebound.values.break_state=1;
    expect(!rebound.SupportedValues(),"active wall animation cannot be admitted by scalar visibility restoration");
    rebound.values.break_state=2;auto intact=rebound;intact.values.break_state=0;
    expect(!intact.SameBinding(rebound),"wall event and animation transitions remain separate ownership requirements");
    expect(a.WriteValues(put) && observed.ReadFrom(base,address,read,weak) && observed.values==a.values,
        "A source publication is reproducible after B recovery");
}

void test_mesh_emitter_storage_contract()
{
#ifdef _WIN32
    using Access = ReplayEmitterStorageTestAccess;
    std::array<std::byte, 0x200> root{};
    const auto put = [&](std::size_t offset, auto value) { std::memcpy(root.data() + offset, &value, sizeof(value)); };
    put(0x114, 256); put(0x118, 1); put(0x120, 2); put(0x1dc, 128); put(0x1e0, 200);
    expect(Access::MeshPayload(root.data()), "mesh rotation and previous-particle payload fit retained particle stride");
    put(0x1dc, 200);
    expect(!Access::MeshPayload(root.data()), "mesh rotation overflow rejects before publication");
    put(0x1dc, 128); put(0x1e0, 201);
    expect(!Access::MeshPayload(root.data()), "mesh previous-particle overflow rejects");
    put(0x1e0, 0); put(0x1e8, std::uintptr_t(0x12340));
    expect(!Access::MeshPayload(root.data()), "mesh material allocation cannot escape graph ownership");
    put(0x1e8, std::uintptr_t(0)); put(0x1f4, 1);
    expect(!Access::MeshPayload(root.data()), "mesh null material backing with capacity rejects");
    put(0x1f4, 0); put(0x1dc, 0);
    expect(!Access::MeshPayload(root.data()), "active mesh particles require rotation payload");
    put(0x118, 0); put(0x120, 0);
    expect(Access::MeshPayload(root.data()), "empty mesh admits constructor-zero payload offsets");
    std::array<std::byte, 0x50> authored{}, lod{}, type{};
    std::uintptr_t lod_address = reinterpret_cast<std::uintptr_t>(lod.data());
    const auto type_address = reinterpret_cast<std::uintptr_t>(type.data());
    std::uintptr_t mesh = 0x12340;
    const auto lod_array = reinterpret_cast<std::uintptr_t>(&lod_address);
    const int one = 1;
    std::memcpy(authored.data() + 0x38, &lod_array, 8); std::memcpy(authored.data() + 0x40, &one, 4);
    std::memcpy(lod.data() + 0x48, &type_address, 8); std::memcpy(type.data() + 0x30, &mesh, 8);
    put(0x10, reinterpret_cast<std::uintptr_t>(authored.data())); put(0x1d0, type_address);
    expect(Access::MeshBindings(root.data(), type_address, mesh), "mesh uses verified LOD-zero TypeData and mesh asset");
    expect(!Access::MeshBindings(root.data(), type_address, mesh + 8), "mesh asset substitution rejects");
    lod_address = 0;
    expect(!Access::MeshBindings(root.data(), type_address, mesh), "missing authored LOD rejects");
    Sc6ReplayCpuEmitterState current, undo;
    Access::Image(current, 0x5000, 64); Access::Image(undo, 0x6000, 64); Access::MeshImage(undo);
    expect(!current.StorageDisjoint(reinterpret_cast<void*>(0x21d0), undo, reinterpret_cast<void*>(0x2000)),
        "mesh derived root extent participates in complete B allocation exclusion");
    expect(current.StorageDisjoint(reinterpret_cast<void*>(0x2200), undo, reinterpret_cast<void*>(0x2000)),
        "mesh extent ends at verified 0x200 factory size");
    expect(Sc6ReplayCpuEmitterState::IsCpuVtable(0x140000000, 0x143949d88)
        && !Sc6ReplayCpuEmitterState::IsCpuVtable(0x140000000, 0x14394c100),
        "mesh CPU dispatch never admits GPU through CPU publication");
#endif
}

static void test_emitter_cross_owner_storage_exclusion()
{
#ifdef _WIN32
    Sc6ReplayCpuEmitterState current, undo, other;
    using Access = ReplayEmitterStorageTestAccess;
    Access::Image(current, 0x5000, 64);
    Access::Image(undo, 0x6000, 64);
    const auto* c = reinterpret_cast<void*>(0x1000);
    const auto* b = reinterpret_cast<void*>(0x2000);
    expect(current.StorageDisjoint(c, undo, b), "independent current and undo emitter ownership is disjoint");
    Access::Image(other, 0x6020, 64);
    expect(!other.StorageDisjoint(c, undo, b), "current backing cannot alias another emitter's undo backing interior");
    Access::Image(other, 0x2010, 64);
    expect(!other.StorageDisjoint(c, undo, b), "current backing cannot alias another undo emitter's root");
    Access::Image(current, 0x5000, 64, true);
    Access::Image(undo, 0x6000, 64, true);
    expect(!current.StorageDisjoint(c, undo, c)
        && current.StorageDisjoint(c, undo, c, true), "shared GPU root requires explicit admission");
    Access::Image(undo, 0x5008, 64, true);
    expect(!current.StorageDisjoint(c, undo, c, true), "shared GPU root never permits shared backing");
    Access::Image(undo, 0x6000, 64, false);
    expect(!current.StorageDisjoint(c, undo, c, true), "CPU root cannot use GPU shared-root exception");
    Access::Image(undo, UINTPTR_MAX - 8, 64);
    expect(!current.StorageDisjoint(c, undo, b), "overflowing captured allocation rejects retirement");
    Access::Image(undo, 0x6000, 64);
    Access::InvalidParent(undo);
    expect(!current.StorageDisjoint(c, undo, b), "cyclic captured allocation parent rejects ownership admission");
#endif
}

static void test_private_particle_material_retirement(std::uintptr_t native_game=0)
{
#ifdef _WIN32
    auto* memory=static_cast<std::byte*>(VirtualAlloc(nullptr,0x3950000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    expect(memory!=nullptr,"allocate native material ownership fixture");if(!memory)return;
    const auto base=reinterpret_cast<std::uintptr_t>(memory);
    const auto put=[](void* p,std::size_t off,auto v){std::memcpy(static_cast<std::byte*>(p)+off,&v,sizeof(v));};
    const auto resolve=+[](const void* weak)->std::uintptr_t {std::uintptr_t p{};std::memcpy(&p,weak,8);return p;};
    const auto bind=+[](void* weak,const void* object){const auto p=reinterpret_cast<std::uintptr_t>(object);std::memcpy(weak,&p,8);};
    static std::array<std::byte,0x98> emitter_class{};
    const auto get_emitter_class=+[]()->std::uintptr_t{return reinterpret_cast<std::uintptr_t>(emitter_class.data());};
    memory[0xf823f0]=std::byte{0x48};memory[0xf823f1]=std::byte{0xb8};put(memory,0xf823f2,reinterpret_cast<std::uintptr_t>(resolve));
    memory[0xf823fa]=std::byte{0xff};memory[0xf823fb]=std::byte{0xe0};
    DWORD old{};expect(VirtualProtect(memory+0xf82000,4096,PAGE_EXECUTE_READ,&old)!=0,"seal weak resolver fixture");
    FlushInstructionCache(GetCurrentProcess(),memory+0xf823f0,12);
    for(const auto [rva,target]:std::array<std::pair<unsigned,std::uintptr_t>,2>{{
        {0xf7bad0,reinterpret_cast<std::uintptr_t>(bind)},{0x234f470,reinterpret_cast<std::uintptr_t>(get_emitter_class)}}}) {
        memory[rva]=std::byte{0x48};memory[rva+1]=std::byte{0xb8};put(memory,rva+2,target);
        memory[rva+10]=std::byte{0xff};memory[rva+11]=std::byte{0xe0};
        expect(VirtualProtect(memory+(rva&~4095u),4096,PAGE_EXECUTE_READ,&old)!=0,"seal controlled UObject binding/class edges");
        FlushInstructionCache(GetCurrentProcess(),memory+rva,12);
    }
    std::array<std::byte,0x1d0> event_world{};
    std::array<std::byte,0x30> event_manager{};
    std::array<std::byte,0xad0> component{};
    std::array<std::byte,0x208> material{},parent{};
    std::uintptr_t roots[]{reinterpret_cast<std::uintptr_t>(material.data())};
    put(component.data(),0x1c8,event_world.data());put(event_world.data(),0xc0,event_manager.data());
    put(event_manager.data(),0,base+0x394b368);
    for(const auto [slot,rva]:std::array<std::pair<unsigned,unsigned>,4>{{
        {0x5f8,0x1f9ba80},{0x600,0x1f9b840},{0x608,0x1f9b5b0},{0x610,0x1f9b3e0}}})put(memory+0x394b368,slot,base+rva);
    put(component.data(),0,base+0x335db28);put(component.data(),0x188,1u);
    put(memory+0x335db28,0x300,base+0x8d8a10);put(memory+0x335db28,0x360,base+0x8cecf0);
    put(memory+0x335db28,0x370,base+0x1da5910);put(memory+0x335db28,0x1f8,base+0xf71540);
    put(component.data(),0xaa8,roots);put(component.data(),0xab0,1);put(component.data(),0xab4,1);
    put(material.data(),0,base+0x391ee70);put(material.data(),0x20,component.data());put(material.data(),0x78,parent.data());
    Sc6ReplayVfxState image;
    expect(ReplayVfxMaterialTestAccess::CompletionOwnership(),"completion routes require captured receiver ownership and exact delegate identity");
    expect(ReplayVfxMaterialTestAccess::CapturedReferenceInventory(),"captured reference inventory preserves weak identity and rejects partial metadata without reading a dead owner");
    expect(ReplayVfxMaterialTestAccess::Read(image,base,component.data()),"capture private material ownership");
    expect(ReplayVfxMaterialTestAccess::Retire(image,component.data()),"retained component-owned material roots admit native retirement");
    {
        Sc6ReplayVfxState a,b;
        alignas(8) std::array<std::byte,0x480> manager{};
        put(component.data(),0x8b4,1);
        put(component.data(),0x96c,0.25f);expect(ReplayVfxMaterialTestAccess::Read(a,base,component.data()),"capture component delay A");
        put(component.data(),0x8b4,2);
        put(component.data(),0x96c,1.5f);expect(ReplayVfxMaterialTestAccess::Read(b,base,component.data()),"capture component delay B");
        const auto observe=[&](float expected) {
            float delay{};std::memcpy(&delay,component.data()+0x96c,4);
            expect(delay==expected,"component delay survives production publication and complete value undo");
            int render_lod{};std::memcpy(&render_lod,component.data()+0x8b4,4);
            // Native particle proxy constructor141FB3AE0 selects packed
            // material relevance and initializes proxy+310 from this value.
            // Exercise the actual shared capture/write/undo projection.
            expect(render_lod==(expected==0.25f?1:2),"component LOD survives production publication and complete value undo");
            if(!native_game)return;
            // Shipped141FA5A20; one preallocated LOD and fixed authored ranges
            // avoid unresolved allocator/RNG imports. This executes the real
            // future consumer instead of comparing an incomplete snapshot hash.
            alignas(16) std::array<std::byte,0x2a0> emitter{};
            alignas(8) std::array<std::byte,0x48> asset{},lod{};
            alignas(8) std::array<std::byte,0xd8> required{};
            void* lods[]{lod.data()};float duration{};
            put(asset.data(),0x38,lods);put(asset.data(),0x40,1);
            put(lod.data(),0x30,required.data());put(required.data(),0xc8,0.5f);put(required.data(),0x68,2.0f);
            put(emitter.data(),0x10,asset.data());put(emitter.data(),0x18,component.data());
            put(emitter.data(),0x180,&duration);put(emitter.data(),0x188,1);put(emitter.data(),0x18c,1);
            reinterpret_cast<void(*)(void*)>(native_game+0x1fa5a20)(emitter.data());
            float selected{};std::memcpy(&selected,emitter.data()+0x178,4);
            expect(duration==expected+2.5f && selected==duration,"shipped duration setup consumes restored component delay");
        };
        expect(ReplayVfxMaterialTestAccess::WriteComponentProjection(a,b,manager.data(),true),"publish captured A component values");observe(0.25f);
        expect(ReplayVfxMaterialTestAccess::WriteComponentProjection(a,b,manager.data(),false),"recover complete B component values");observe(1.5f);
        expect(ReplayVfxMaterialTestAccess::WriteComponentProjection(a,b,manager.data(),true),"repeat component A publication");observe(0.25f);
        expect(ReplayVfxMaterialTestAccess::WriteComponentProjection(a,b,manager.data(),false),"repeat complete B value recovery");observe(1.5f);
        if(native_game)std::cout<<"Shipped particle duration consumer exercised across A/B publication and undo; no game launched\n";
    }

    {
        Sc6ReplayVfxState captured;
        auto source=component;
        std::array<std::byte,0xad0> fresh{};
        // This projection test has no private materials; the preceding test
        // owns its material under a different component identity.
        std::memset(source.data()+0xaa8,0,16);
        put(source.data(),0x808,parent.data());put(source.data(),0x188,0x60001u);
        put(source.data(),0x8b4,3);put(source.data(),0x96c,0.75f);
        expect(ReplayVfxMaterialTestAccess::Read(captured,base,source.data()),"capture fresh-owner value source");
        ReplayVfxMaterialTestAccess::FreshValueContext(captured);
        Sc6ReplayCpuEmitterState::ComponentReplacement binding{
            reinterpret_cast<std::uintptr_t>(source.data()),reinterpret_cast<std::uintptr_t>(fresh.data())};
        std::memcpy(binding.source_weak.data(),&binding.source,8);
        std::memcpy(binding.target_weak.data(),&binding.target,8);
        // Original bytes are no longer usable: transfer must consume only the
        // immutable captured projection, never resolve the source identity.
        source.fill(std::byte{0xdd});
        put(fresh.data(),0,base+0x335db28);put(fresh.data(),0x808,parent.data());
        put(fresh.data(),0x188,0x2000003u);
        const auto initial=fresh;
        bool dirty=false;
        const auto reject=[&](std::size_t offset,auto value) {
            fresh=initial;put(fresh.data(),offset,value);const auto before=fresh;
            expect(!captured.ApplyFreshComponentValues(binding,dirty).ok() && !dirty && fresh==before,
                "fresh-owner preflight rejects without mutation");
        };
        reject(0,std::uintptr_t{});reject(0x808,std::uintptr_t{});
        reject(0x188,0u);reject(0x188,0x40001u);
        reject(0x790,std::uintptr_t{1});reject(0x7a0,1);
        reject(0xa58,1);reject(0xa70,std::uintptr_t{1});reject(0xa80,std::uint8_t{1});reject(0xa81,std::uint8_t{1});
        fresh=initial;auto foreign=binding;++foreign.target_weak[0];
        expect(!captured.ApplyFreshComponentValues(foreign,dirty).ok() && !dirty && fresh==initial,
            "fresh-owner stale destination identity rejects");
        foreign=binding;++foreign.source_weak[1];
        expect(!captured.ApplyFreshComponentValues(foreign,dirty).ok() && !dirty && fresh==initial,
            "fresh-owner source identity must match captured metadata");
        expect(captured.ApplyFreshComponentValues(binding,dirty).ok() && dirty,"apply immutable values to fresh owner");
        int lod{};float delay{};unsigned flags{};std::uintptr_t table{},particle_template{};
        std::memcpy(&lod,fresh.data()+0x8b4,4);std::memcpy(&delay,fresh.data()+0x96c,4);
        std::memcpy(&flags,fresh.data()+0x188,4);std::memcpy(&table,fresh.data(),8);
        std::memcpy(&particle_template,fresh.data()+0x808,8);
        expect(lod==3 && delay==0.75f && flags==0x2060003u && table==base+0x335db28
            && particle_template==reinterpret_cast<std::uintptr_t>(parent.data()),
            "fresh owner receives captured logical values while retaining physical registration and identity");
        const auto applied=fresh;
        expect(!captured.ApplyFreshComponentValues(binding,dirty).ok() && dirty && fresh==applied,
            "dirty fresh-owner transfer cannot be repeated");
    }

    expect(!image.ValidateHistoricalMaterials().ok(),"historical material parameters remain unsupported");
    const auto material_witness=image.HistoricalMaterialAdmission();
    expect(material_witness.component==reinterpret_cast<std::uintptr_t>(component.data())
        && material_witness.array==1 && material_witness.count==1 && material_witness.non_null==1
        && material_witness.first_material==roots[0] && material_witness.first_parent==reinterpret_cast<std::uintptr_t>(parent.data()),
        "material admission witness reads exact captured owner and array");
    roots[0]=0;
    expect(image.HistoricalMaterialAdmission().first_material==material_witness.first_material,
        "material failure witness never reads changed live storage");
    roots[0]=material_witness.first_material;

    const auto admitted=[&]{return ReplayVfxMaterialTestAccess::Retire(image,component.data());};
    put(material.data(),0x20,parent.data());expect(!admitted(),"foreign material outer rejects retirement");put(material.data(),0x20,component.data());
    put(material.data(),0x78,component.data());expect(!admitted(),"changed material parent rejects private B retirement");put(material.data(),0x78,parent.data());
    roots[0]=0;expect(!admitted(),"lost material membership rejects private B retirement");roots[0]=reinterpret_cast<std::uintptr_t>(material.data());
    put(component.data(),0xab4,2);expect(!admitted(),"changed material allocation capacity rejects retirement");put(component.data(),0xab4,1);
    put(component.data(),0xa98,roots);put(component.data(),0xaa0,1);put(component.data(),0xaa4,1);
    expect(!ReplayVfxMaterialTestAccess::Read(image,base,component.data()),"aliased material arrays and duplicate owners reject capture");
    put(component.data(),0xa98,std::uintptr_t{});put(component.data(),0xaa0,0);put(component.data(),0xaa4,0);
    expect(ReplayVfxMaterialTestAccess::Read(image,base,component.data()) && admitted(),"negative checks preserve native material state");
    roots[0]=0;
    expect(ReplayVfxMaterialTestAccess::Read(image,base,component.data()),"capture null material entry with retained array membership");
    expect(image.ValidateHistoricalMaterials().ok(),"null-only historical material arrays have no parameter owner to reconstruct");
    const auto null_witness=image.HistoricalMaterialAdmission();
    expect(null_witness.count==1 && !null_witness.non_null && !null_witness.first_material,
        "diagnostic distinguishes array membership from live material ownership without admitting it");
    roots[0]=material_witness.first_material;
    expect(!image.ValidateHistoricalMaterials().ok(),"null target rejects a newly owned live material before publication");
    roots[0]=0;put(component.data(),0xab4,2);
    expect(!image.ValidateHistoricalMaterials().ok(),"null target retains exact array capacity");
    put(component.data(),0xab4,1);
    expect(image.ValidateHistoricalMaterials().ok(),"failed null-material admission leaves original binding intact");
    Sc6ReplayVfxState current;
    expect(ReplayVfxMaterialTestAccess::Read(current,base,component.data())
        && ReplayVfxMaterialTestAccess::PublicationBindings(image,current),"same null membership crosses actual manager publication boundary");
    std::uintptr_t relocated_roots[]{0};put(component.data(),0xaa8,relocated_roots);
    expect(ReplayVfxMaterialTestAccess::Read(current,base,component.data())
        && !ReplayVfxMaterialTestAccess::PublicationBindings(image,current),"different A/B material backing rejects actual publication even with equal null contents");
    put(component.data(),0xaa8,roots);
    expect(ReplayVfxMaterialTestAccess::Read(current,base,component.data())
        && ReplayVfxMaterialTestAccess::PublicationBindings(image,current),"publication rejection leaves original A/B membership reusable");
    // 141F6FF20 consumes this multicast before manager slot retirement. Equal
    // component/template bindings cannot authorize a different completion.
    std::array<std::uint64_t,2> completion{reinterpret_cast<std::uintptr_t>(parent.data()),0x1234};
    put(component.data(),0x970,completion.data());put(component.data(),0x978,1);put(component.data(),0x97c,1);
    expect(ReplayVfxMaterialTestAccess::Read(image,base,component.data()),"capture native completion binding");
    completion[1]=0x5678;
    expect(ReplayVfxMaterialTestAccess::Read(current,base,component.data())
        && !ReplayVfxMaterialTestAccess::PublicationBindings(image,current),"changed completion function rejects actual A/B publication");
    completion[1]=0x1234;
    expect(ReplayVfxMaterialTestAccess::Read(current,base,component.data())
        && ReplayVfxMaterialTestAccess::PublicationBindings(image,current),"completion rejection leaves original publication reusable");
    completion[0]=reinterpret_cast<std::uintptr_t>(material.data());
    expect(ReplayVfxMaterialTestAccess::Read(current,base,component.data())
        && !ReplayVfxMaterialTestAccess::PublicationBindings(image,current),"changed weak completion owner rejects publication");
    completion[0]=reinterpret_cast<std::uintptr_t>(parent.data());
    expect(ReplayVfxMaterialTestAccess::Read(current,base,component.data()),"capture original B completion binding");
    completion[1]=0x5678;
    expect(!ReplayVfxMaterialTestAccess::PublicationBindings(image,current),"post-preparation callback mutation rejects publication");
    completion[1]=0x1234;
    auto relocated_completion=completion;put(component.data(),0x970,relocated_completion.data());
    expect(ReplayVfxMaterialTestAccess::Read(current,base,component.data())
        && ReplayVfxMaterialTestAccess::PublicationBindings(image,current),"equal native callback entries can use independently owned backing");
    put(component.data(),0x978,17);put(component.data(),0x97c,17);
    expect(!ReplayVfxMaterialTestAccess::Read(current,base,component.data()),"oversized completion list rejects without truncating or reading backing");
    put(component.data(),0x978,1);put(component.data(),0x97c,0);
    expect(!ReplayVfxMaterialTestAccess::Read(current,base,component.data()),"malformed native completion capacity rejects");
    put(component.data(),0x97c,1);put(component.data(),0x970,std::uintptr_t{});
    expect(!ReplayVfxMaterialTestAccess::Read(current,base,component.data()),"nonempty null completion backing rejects");
    put(component.data(),0x970,completion.data());
    expect(ReplayVfxMaterialTestAccess::Read(current,base,component.data())
        && ReplayVfxMaterialTestAccess::PublicationBindings(image,current),"negative callback checks preserve reusable A and B");
    // Common capture must cover persistent stage components too. The private
    // reconstruction visitor skips them and cannot grant event admission.
    for(const auto tick:{0x8d8a10u,0x1f853c0u}) {
        put(memory+0x335db28,0x300,base+tick);
        for(const auto offset:{0x850u,0x860u,0x870u,0x880u}) {
            put(component.data(),offset,completion.data());put(component.data(),offset+8,1);put(component.data(),offset+12,1);
            const bool accepted=ReplayVfxMaterialTestAccess::Read(current,base,component.data());
            expect(!accepted,"common particle capture rejects unowned spawn/burst/death/collision delegate");
            if(accepted)std::exit(2);
            put(component.data(),offset+8,0);
            expect(ReplayVfxMaterialTestAccess::Read(current,base,component.data()),"empty retained event allocation admits normal and Lux component capture");
            put(component.data(),offset+8,-1);
            expect(!ReplayVfxMaterialTestAccess::Read(current,base,component.data()),"negative particle event delegate extent rejected");
            put(component.data(),offset+8,0);put(component.data(),offset+12,0);put(component.data(),offset,std::uintptr_t{});
        }
    }
    put(memory+0x335db28,0x300,base+0x8d8a10);
    std::array<std::byte,0x3d8> emitter_owner{};
    std::array<std::byte,0x98> derived_emitter_class{};
    std::array<std::uintptr_t,2> ancestry{reinterpret_cast<std::uintptr_t>(emitter_class.data())+0x88,0};
    put(derived_emitter_class.data(),0x88,ancestry.data());put(derived_emitter_class.data(),0x90,1);
    put(emitter_owner.data(),0x10,derived_emitter_class.data());put(component.data(),0x190,emitter_owner.data());
    expect(ReplayVfxMaterialTestAccess::Read(image,base,component.data()),"common capture admits derived AEmitter with empty owner event routes");
    for(const auto offset:{0x398u,0x3a8u,0x3b8u,0x3c8u}) {
        put(emitter_owner.data(),offset,completion.data());put(emitter_owner.data(),offset+8,1);put(emitter_owner.data(),offset+12,1);
        expect(!ReplayVfxMaterialTestAccess::Read(current,base,component.data()),"common capture rejects subclass owner particle event delegate");
        expect(!ReplayVfxMaterialTestAccess::PublicationBindings(image,image),"post-capture owner callback mutation rejects publication");
        put(emitter_owner.data(),offset+8,0);
    }
    expect(ReplayVfxMaterialTestAccess::Read(current,base,component.data())
        && ReplayVfxMaterialTestAccess::PublicationBindings(image,current),"empty owner callback allocations remain admitted");
    for(const auto slot:{0x5f8u,0x600u,0x608u,0x610u}) {
        std::uintptr_t saved{};std::memcpy(&saved,memory+0x394b368+slot,8);put(memory+0x394b368,slot,saved+1);
        expect(!ReplayVfxMaterialTestAccess::Read(current,base,component.data()),"common capture rejects changed event-manager consumer");
        put(memory+0x394b368,slot,saved);
    }
    put(event_world.data(),0xc0,std::uintptr_t{});
    expect(!ReplayVfxMaterialTestAccess::PublicationBindings(image,image),"changed event-manager membership rejects publication");
    expect(ReplayVfxMaterialTestAccess::Read(current,base,component.data()),"native completion supports absent world event manager without external dispatch");
    put(event_world.data(),0xc0,event_manager.data());put(component.data(),0x190,std::uintptr_t{});
    // The local check does not invoke a destructor, root-flag setter, expected
    // observation or renderer. Actual GPU retirement remains the live gate.
    VirtualFree(memory,0,MEM_RELEASE);
#endif
}

static void test_seeded_collision_payload_admission()
{
#ifdef _WIN32
    // Only the native read-only module-offset lookup is substituted. Production
    // validates the authored module graph, native signatures and payload ranges.
    auto* memory=static_cast<std::byte*>(VirtualAlloc(nullptr,0x3962000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    expect(memory!=nullptr,"allocate controlled native module tables");if(!memory)return;
    const auto base=reinterpret_cast<std::uintptr_t>(memory);
    const auto put=[](void* p,std::size_t off,auto value){std::memcpy(static_cast<std::byte*>(p)+off,&value,sizeof(value));};
    const auto lookup=+[](const void* image,const void* module)->std::uintptr_t {
        std::uintptr_t data{};int offset{};
        std::memcpy(&data,static_cast<const std::byte*>(image)+0x100,8);
        std::memcpy(&offset,static_cast<const std::byte*>(module)+0x20,4);
        return data+offset;
    };
    memory[0x1f9a380]=std::byte{0x48};memory[0x1f9a381]=std::byte{0xb8};
    put(memory,0x1f9a382,reinterpret_cast<std::uintptr_t>(lookup));
    memory[0x1f9a38a]=std::byte{0xff};memory[0x1f9a38b]=std::byte{0xe0};
    DWORD old{};expect(VirtualProtect(memory+0x1f9a000,4096,PAGE_EXECUTE_READ,&old)!=0,"seal offset lookup fixture");
    FlushInstructionCache(GetCurrentProcess(),memory+0x1f9a380,12);
    std::array<std::byte,0x200> image{},authored{},seed{},collision{};
    std::array<std::byte,12> payload{};
    std::uintptr_t modules[]{reinterpret_cast<std::uintptr_t>(seed.data()),reinterpret_cast<std::uintptr_t>(collision.data())};
    put(image.data(),0x10,authored.data());put(image.data(),0x100,payload.data());put(image.data(),0x108,12);
    put(authored.data(),0x158,modules);put(authored.data(),0x160,2);put(authored.data(),0x164,2);
    put(seed.data(),0,base+0x3961000);put(seed.data(),0xd8,1);
    put(collision.data(),0,base+0x39573d8);put(collision.data(),0x20,8);
    for(const auto [slot,rva]:std::array<std::pair<int,int>,5>{{{0x238,0x1fdb3a0},{0x268,0x1fd7e90},{0x270,0x1fd3d90},{0x2c8,0x1fbdd10},{0x308,0x1fa0850}}})put(memory+0x3961000,slot,base+rva);
    put(memory+0x39573d8,0x268,base+0x301490);put(memory+0x39573d8,0x270,base+0x1fd3b80);
    const auto valid=[&]{return ReplayEmitterStorageTestAccess::InstancePayload(base,image.data(),modules[0]);};
    expect(valid(),"seeded velocity plus collision owns the complete twelve-byte instance payload");
    put(collision.data(),0x20,4);expect(!valid(),"overlapping module payloads rejected");
    put(collision.data(),0x20,9);expect(!valid(),"out-of-range module payload rejected");
    put(collision.data(),0x20,8);put(collision.data(),0x128,1u);expect(!valid(),"external collision impulse remains rejected");
    put(collision.data(),0x128,0u);put(seed.data(),0xd8,0);expect(!valid(),"absent authored seed extent rejected");
    put(seed.data(),0xd8,1);put(image.data(),0x108,16);expect(!valid(),"unowned trailing payload rejected");
    put(image.data(),0x108,12);expect(valid(),"negative checks preserve valid module ownership");
    expect(ReplayEmitterStorageTestAccess::ModuleBindings(image.data(),modules[0],modules[1]),"both captured module identities match");
    expect(!ReplayEmitterStorageTestAccess::ModuleBindings(image.data(),modules[0],0),"unretained second module rejected");
    expect(!ReplayEmitterStorageTestAccess::ModuleBindings(image.data(),modules[0],modules[1]+8),"replaced second module identity rejected");
    for(auto slot:{0x238,0x268,0x270,0x2c8,0x308}) {
        std::uintptr_t saved{};std::memcpy(&saved,memory+0x3961000+slot,8);put(memory+0x3961000,slot,saved+1);
        expect(!valid(),"changed seeded module native consumer rejected");put(memory+0x3961000,slot,saved);
    }
    std::array<std::byte,20> event_payload{};
    std::array<std::byte,0x28> authored_event{};
    std::array<std::uintptr_t,1> game_callbacks{reinterpret_cast<std::uintptr_t>(collision.data())};
    modules[0]=reinterpret_cast<std::uintptr_t>(seed.data());
    put(seed.data(),0,base+0x3957d58);put(seed.data(),0x20,0);
    put(memory+0x3957d58,0x268,base+0x3014b0);put(memory+0x3957d58,0x270,base+0x1fd3b90);
    put(seed.data(),0x30,authored_event.data());put(seed.data(),0x38,1);put(seed.data(),0x3c,1);
    put(authored.data(),0x160,1);put(image.data(),0x100,event_payload.data());put(image.data(),0x108,20);
    expect(valid(),"scalar EventGenerator payload permits internal receiver feedback with no game callbacks");
    put(authored_event.data(),0x18,game_callbacks.data());put(authored_event.data(),0x20,1);put(authored_event.data(),0x24,1);
    const bool game_callback_accepted=valid();
    expect(!game_callback_accepted,"EventGenerator admission rejects authored game callback objects");
    if(game_callback_accepted)std::exit(2);
    put(authored_event.data(),0x20,0);
    expect(valid(),"empty retained callback allocation does not suppress internal particle events");
    put(authored_event.data(),0x20,-1);expect(!valid(),"malformed authored game callback extent rejected");
    put(authored_event.data(),0x20,0);put(seed.data(),0x38,2);
    expect(!valid(),"authored event count cannot exceed backing capacity");
    put(seed.data(),0x38,1);
    expect(valid(),"callback admission rejection preserves native instance payload ownership");
    VirtualFree(memory,0,MEM_RELEASE);
#endif
}

#ifdef _WIN32
struct NativeGpuDescriptorRouteFixture {
    inline static NativeGpuDescriptorRouteFixture* active{};
    void* root{};void* system{};void* descriptor{};void* asset{};void* component{};
    unsigned factories{},parameters{},initializations{};
    static void* Create(void* system,void* descriptor) {
        auto& f=*active;expect(system==f.system && descriptor==f.descriptor,"native GPU factory receives exact type-owned descriptor");
        ++f.factories;return f.root;
    }
    static void Parameters(void* root,void* asset,void* component) {
        auto& f=*active;expect(root==f.root && asset==f.asset && component==f.component,"native GPU route preserves parameter owners");
        ++f.parameters;
    }
    static void Initialize(void* root) {
        auto& f=*active;expect(root==f.root && f.factories==1 && f.parameters==1,"native asset initializes after construction and parameter binding");
        ++f.initializations;
    }
};
#endif
static void test_mapped_empty_gpu_render_owner(std::uintptr_t native_game)
{
#ifdef _WIN32
    // The constructor/destructor execute in the shipped image. Neither storage
    // block is registered or submitted. Live RHI ownership remains a separate gate.
    alignas(16) std::array<std::byte,0x260> a{},b{};
    a.fill(std::byte{0x6d});b.fill(std::byte{0xb3});
    const auto construct=reinterpret_cast<void*(*)(void*)>(native_game+0x1f8de20);
    const auto destroy=reinterpret_cast<void(*)(void*)>(native_game+0x1f8fb50);
    expect(construct(a.data())==a.data() && construct(b.data())==b.data(),"native GPU render versions use independent supplied storage");
    const auto value=[]<class T>(const auto& bytes,std::size_t offset){T result{};std::memcpy(&result,bytes.data()+offset,sizeof(T));return result;};
    for(const auto* image:{&a,&b}) {
        expect(value.operator()<std::uintptr_t>(*image,0)==native_game+0x394bfc0
            && value.operator()<std::uintptr_t>(*image,0x1f0)==native_game+0x394c000,"native render owner has both expected resource types");
        expect(value.operator()<int>(*image,0x240)==-1,"native render version starts outside FX registry");
        for(auto offset:{0x10,0x18,0x30,0x38,0x48,0xc0,0xd0,0xe0,0x200,0x208,0x220,0x228,0x238})
            expect(value.operator()<std::uintptr_t>(*image,offset)==0,"native render version starts without borrowed allocation or RHI backing");
        expect((*image)[0x28]==std::byte{} && (*image)[0x218]==std::byte{},"native render resources start uninitialized");
    }
    const auto retained_b=b;
    destroy(a.data());
    expect(b==retained_b,"destroying unpublished native A storage preserves complete independent B storage");
    destroy(b.data());
    std::cout<<"Shipped empty GPU render constructors/destructors exercised; no registration, GPU submission or game launched\n";
#endif
}

static void test_mapped_particle_receiver_dispatch(std::uintptr_t native_game)
{
#ifdef _WIN32
    {
        std::array<std::byte,0x200> emitter{};
        std::array<std::array<std::byte,0x80>,2> particles{};
        std::array<std::uint16_t,2> indices{0,1};
        const auto put=[](void* p,std::size_t off,auto value){std::memcpy(static_cast<std::byte*>(p)+off,&value,sizeof(value));};
        const auto read=[](std::uintptr_t p,auto& value){std::memcpy(&value,reinterpret_cast<const void*>(p),sizeof(value));return true;};
        put(emitter.data(),0xf0,particles.data());put(emitter.data(),0xf8,indices.data());
        put(emitter.data(),0x114,0x80);put(emitter.data(),0x118,1);put(emitter.data(),0x120,2);
        put(particles[0].data(),0xc,.5f);put(particles[0].data(),0x10,12.f);put(particles[0].data(),0x30,3.f);
        ReplayQualification::ReceiverCpuSample baseline{},observed{};
        const auto capture=[&](auto& value){return value.Capture(read,reinterpret_cast<std::uintptr_t>(emitter.data()));};
        expect(capture(baseline) && baseline.count==1,"receiver observer reads actual indexed active particle boundary");
        put(particles[0].data(),0x30,4.f);
        expect(capture(observed) && observed.hash!=baseline.hash,"receiver observer detects velocity change after native spawn");
        put(particles[0].data(),0x30,3.f);put(particles[1].data(),0x10,123.f);
        expect(capture(observed) && observed.hash==baseline.hash,"receiver observer excludes inactive backing slots");
        put(emitter.data(),0x11c,1u);
        expect(capture(observed) && observed.hash!=baseline.hash,"receiver observer detects a birth even when active count stays constant");
        put(emitter.data(),0x11c,0u);indices[0]=2;
        expect(!capture(observed),"receiver observer rejects out-of-allocation particle index");
        indices[0]=0;put(emitter.data(),0x118,-1);
        expect(!capture(observed),"receiver observer rejects malformed active count");
        put(emitter.data(),0x118,1);put(emitter.data(),0x114,0x20);
        expect(!capture(observed),"receiver observer rejects truncated native particle stride");
    }
    // Run the shipped receiver loop and Spawn receiver. Only the authored
    // constant distribution and terminal emitter allocation/spawn edge are
    // controlled: this proves dispatch/order, not particle continuation.
    struct Call {float delta{};int count{};std::array<float,3> position{},velocity{};};
    static std::array<Call,8> calls{};static std::size_t count{};count=0;
    const auto spawn=+[](void*,float delta,int ordinary,int forced,const float* position,const float* velocity) {
        expect(ordinary==0 && count<calls.size(),"native receiver requests bounded forced-spawn edge");
        if(count>=calls.size())return;
        auto& call=calls[count++];call.delta=delta;call.count=forced;
        std::memcpy(call.position.data(),position,12);std::memcpy(call.velocity.data(),velocity,12);
    };
    const auto constant=+[](void*,float,void*,void*)->float{return 1.f;};
    std::array<std::uintptr_t,0x258/8> distribution_table{};
    distribution_table[0x250/8]=reinterpret_cast<std::uintptr_t>(constant);
    std::uintptr_t distribution=reinterpret_cast<std::uintptr_t>(distribution_table.data());
    std::array<std::uintptr_t,0xc8/8> emitter_table{};
    emitter_table[0xc0/8]=reinterpret_cast<std::uintptr_t>(spawn);
    std::array<std::byte,0x200> emitter{},receiver{};std::array<std::byte,0xc0> lod{};
    std::array<std::byte,0x980> component{};std::array<std::array<std::byte,0x50>,3> deaths{};
    void* receivers[]{receiver.data()};
    const auto put=[](void* p,std::size_t off,auto value){std::memcpy(static_cast<std::byte*>(p)+off,&value,sizeof(value));};
    put(receiver.data(),0,native_game+0x39593b0);put(receiver.data(),0x30,std::uint8_t{2});
    put(receiver.data(),0x38,std::uint64_t{0x12345678});put(receiver.data(),0x70,&distribution);
    put(emitter.data(),0,emitter_table.data());put(emitter.data(),0x18,component.data());put(emitter.data(),0x28,lod.data());
    put(lod.data(),0xa0,receivers);put(lod.data(),0xa8,1);
    put(component.data(),0x928,deaths.data());put(component.data(),0x930,3);put(component.data(),0x934,3);
    for(std::size_t i=0;i<deaths.size();++i) {
        put(deaths[i].data(),0,std::uint32_t{2});put(deaths[i].data(),8,std::uint64_t{i==1?0x87654321:0x12345678});
        put(deaths[i].data(),0x14,float(10+i));put(deaths[i].data(),0x20,float(40+i));
    }
    const auto original=deaths;const auto process=reinterpret_cast<void(*)(void*,float)>(native_game+0x1fa0b80);
    process(emitter.data(),1.f/60.f);
    expect(count==2 && calls[0].count==1 && calls[1].count==1
        && calls[0].delta==1.f/60.f && calls[1].delta==1.f/60.f
        && calls[0].position[0]==10.f && calls[1].position[0]==12.f
        && calls[0].velocity==std::array<float,3>{} && calls[1].velocity==std::array<float,3>{}
        && deaths==original,"shipped death-to-spawn receiver preserves queue order, name selection and native delta");
    put(component.data(),0x930,0);process(emitter.data(),1.f/60.f);
    expect(count==2,"retired death queue does not redeliver native receiver events");
    put(component.data(),0x930,3);process(emitter.data(),1.f/30.f);
    expect(count==4 && calls[2].delta==1.f/30.f && calls[3].delta==1.f/30.f,
        "another simulated traversal executes required receiver events again");
    std::cout<<"Shipped death-to-spawn receiver dispatch exercised; terminal spawn edge controlled, continuation not claimed\n";
#endif
}

static void test_mapped_gpu_descriptor_route(std::uintptr_t native_game)
{
#ifdef _WIN32
    // Executes shipped141F70FF0 -> GPU virtual338/141F94230. Only the FX
    // allocator and emitter initialization are controlled; this does not claim
    // native UObject construction, GPU-resource allocation or live restoration.
    std::array<std::byte,0x170> asset{},world{};std::array<std::byte,0xc0> lod{};
    std::array<std::byte,0x460> module{};std::array<std::byte,0xad0> component{};
    std::array<std::byte,0x2a0> root{};std::array<std::uintptr_t,6> fx_vtable{};
    std::array<std::uintptr_t,3> emitter_vtable{};
    const auto address=[](auto& v){return reinterpret_cast<std::uintptr_t>(v.data());};
    const auto put=[](auto& v,std::size_t offset,auto value){std::memcpy(v.data()+offset,&value,sizeof(value));};
    std::array<std::uintptr_t,1> lods{address(lod)},system{address(fx_vtable)};
    put(asset,0x38,address(lods));put(asset,0x40,1);put(lod,0x48,address(module));
    put(module,0,native_game+0x394b9e0);put(component,0x1c8,address(world));
    put(component,0xa60,address(system));put(world,0x168,std::uintptr_t{1});
    fx_vtable[5]=reinterpret_cast<std::uintptr_t>(&NativeGpuDescriptorRouteFixture::Create);
    emitter_vtable[1]=reinterpret_cast<std::uintptr_t>(&NativeGpuDescriptorRouteFixture::Parameters);
    emitter_vtable[2]=reinterpret_cast<std::uintptr_t>(&NativeGpuDescriptorRouteFixture::Initialize);
    put(root,0,address(emitter_vtable));
    constexpr std::array<std::uintptr_t,7> gates{0x40956b4,0x409052a,0x40904e7,0x4090b18,0x40904e5,0x40904e6,0x409052b};
    std::array<std::byte,gates.size()> previous{};
    for(std::size_t i=0;i<gates.size();++i){auto* p=reinterpret_cast<std::byte*>(native_game+gates[i]);previous[i]=*p;*p=std::byte{1};}
    NativeGpuDescriptorRouteFixture f{root.data(),system.data(),module.data()+0x30,asset.data(),component.data()};
    NativeGpuDescriptorRouteFixture::active=&f;
    auto* result=reinterpret_cast<void*(*)(void*,void*)>(native_game+0x1f70ff0)(asset.data(),component.data());
    NativeGpuDescriptorRouteFixture::active=nullptr;
    for(std::size_t i=0;i<gates.size();++i)*reinterpret_cast<std::byte*>(native_game+gates[i])=previous[i];
    expect(result==root.data() && f.factories==1 && f.parameters==1 && f.initializations==1,"shipped GPU construction route completes exactly once");
    std::uintptr_t actual_lod{};std::memcpy(&actual_lod,root.data()+0x28,sizeof(actual_lod));
    expect(actual_lod==address(lod),"shipped factory selects first authored LOD");
    std::cout<<"Shipped GPU descriptor ownership route exercised with controlled FX allocation; no game launched\n";
#endif
}

#ifdef _WIN32
struct EmitterComponentReplacementFixture {
    static inline std::uintptr_t retired{};
    static inline std::uintptr_t retire_on_allocation{};
    static inline unsigned allocated{},freed{},retired_reads{};
    static inline void* factory_result{};
    static inline unsigned factory_calls{};
    static void* Factory(void*,void*) {++factory_calls;return factory_result;}
    static inline unsigned native_destroys{};
    static void* Destroy(void* root,unsigned flags) {
        ++native_destroys;expect(flags==1,"fresh GPU retirement uses native deleting destructor");
        std::memset(root,0xdd,0x2a0);return root;
    }
    static std::uintptr_t Resolve(const void* weak) {
        std::uintptr_t value{};std::memcpy(&value,weak,sizeof(value));
        if(value==retired) {++retired_reads;return 0;} return value;
    }
    static void* Allocate(std::size_t n) {++allocated;if(retire_on_allocation)retired=retire_on_allocation;return std::malloc(n);}
    static void Free(void* p) {++freed;std::free(p);}
    static std::size_t Charge(std::size_t n,unsigned) {return n;}
};
#endif

static void test_emitter_component_replacement()
{
#ifdef _WIN32
    using Fixture=EmitterComponentReplacementFixture;
    constexpr std::size_t fixture_bytes=0x1f95000;
    auto* memory=static_cast<std::byte*>(VirtualAlloc(nullptr,fixture_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    expect(memory!=nullptr,"replacement native dependency storage");if(!memory)return;
    const auto thunk=[&](std::size_t offset,auto function) {
        const auto target=reinterpret_cast<std::uintptr_t>(function);
        const unsigned char jump[]{0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
        if(offset>fixture_bytes-sizeof(jump)) {expect(false,"replacement thunk fits reserved native dependency storage");return;}
        std::memcpy(memory+offset,jump,sizeof(jump));std::memcpy(memory+offset+2,&target,sizeof(target));
        DWORD old{};expect(VirtualProtect(memory+(offset&~4095ull),4096,PAGE_EXECUTE_READ,&old)!=0,"replacement dependency thunk");
    };
    thunk(0xf823f0,&Fixture::Resolve);thunk(0x4a61c0,&Fixture::Allocate);
    thunk(0xd46a00,&Fixture::Free);thunk(0xd50dc0,&Fixture::Charge);
    thunk(0x1f941e0,&Fixture::Factory);
    thunk(0x1f90680,&Fixture::Destroy);
    const auto base=reinterpret_cast<std::uintptr_t>(memory);
    const auto address=[](auto& v){return reinterpret_cast<std::uintptr_t>(v.data());};
    const auto put=[](auto& v,std::size_t offset,auto value){std::memcpy(v.data()+offset,&value,sizeof(value));};
    std::array<std::byte,0x170> asset{};std::array<std::byte,0xc0> lod{};
    std::array<std::byte,0x460> module{};std::array<std::byte,0x2a0> root{};
    std::array<std::byte,0xad0> source{},fresh{};
    std::array<std::uintptr_t,1> lods{address(lod)};
    put(asset,0x38,address(lods));put(asset,0x40,1);put(lod,0x48,address(module));put(module,0,base+0x394b9e0);
    put(root,0,base+0x394c100);put(root,0x10,address(asset));put(root,0x18,address(source));
    put(root,0x28,address(lod));put(root,0x1d8,address(module)+0x30);
    put(source,0,base+0x335db28);put(fresh,0,base+0x335db28);
    put(source,0x808,address(asset));put(fresh,0x808,address(asset));
    Sc6ReplayCpuEmitterState image;
    ReplayEmitterStorageTestAccess::GpuBindingImage(image,base,root.data(),address(asset),address(source),address(lod),address(module));
    Sc6ReplayCpuEmitterState::ComponentReplacement binding{address(source),address(fresh)};
    std::memcpy(binding.source_weak.data(),&binding.source,8);std::memcpy(binding.target_weak.data(),&binding.target,8);
    const auto fingerprint=image.storage_fingerprint();const auto before_source=source,before_fresh=fresh;
    Fixture::allocated=Fixture::freed=Fixture::retired_reads=0;Fixture::retired=address(source);
    expect(!image.ValidateBindings().ok(),"ordinary restore still rejects a retired component");
    Fixture::retired_reads=0;
    {
        Sc6ReplayCpuEmitterState::Prepared prepared;
        const auto result=image.PrepareReplacement(1024*1024,prepared,binding);
        expect(result.ok(),"typed replacement prepares after source retirement");
        std::uintptr_t owner{};if(prepared.get())std::memcpy(&owner,static_cast<std::byte*>(prepared.get())+0x18,8);
        expect(owner==address(fresh),"prepared root uses only the fresh component binding");
        auto expected=root;put(expected,0x18,address(fresh));
        expect(prepared.get() && !std::memcmp(prepared.get(),expected.data(),expected.size()),
            "component translation changes exactly the typed owner field");
        expect(!prepared.published() && !prepared.execution_started(),"owner translation does not publish or execute");
    }
    expect(Fixture::retired_reads==0,"typed preparation never resolves the retired source owner");
    expect(Fixture::allocated==Fixture::freed && Fixture::allocated==1,"private replacement root retires once");
    expect(image.storage_fingerprint()==fingerprint && source==before_source && fresh==before_fresh,
        "binding translation preserves immutable A and both component objects");
    const auto reject=[&](const auto& bad){Sc6ReplayCpuEmitterState::Prepared p;const auto n=Fixture::allocated;
        expect(!image.PrepareReplacement(1024*1024,p,bad).ok() && !p.get() && Fixture::allocated==n,
            "invalid replacement rejects before native allocation");};
    auto bad=binding;bad.source_weak[1]^=1;reject(bad);
    bad=binding;bad.target_weak[1]^=1;reject(bad);
    bad=binding;bad.target=bad.source;bad.target_weak=bad.source_weak;reject(bad);
    put(fresh,0,base+0x335db30);reject(binding);put(fresh,0,base+0x335db28);
    put(fresh,0x808,address(asset)+8);reject(binding);put(fresh,0x808,address(asset));
    Fixture::retired=address(fresh);reject(binding);
    Fixture::retired=address(asset);reject(binding);
    Fixture::retired=address(source);
    {
        Sc6ReplayCpuEmitterState::Prepared p;const auto n=Fixture::allocated;
        expect(!image.PrepareReplacement(image.owned_bytes(),p,binding).ok() && !p.get() && Fixture::allocated==n,
            "replacement budget includes retained image and private metadata before allocation");
        Fixture::retire_on_allocation=address(fresh);
        expect(!image.PrepareReplacement(1024*1024,p,binding).ok() && !p.get() && Fixture::allocated==Fixture::freed,
            "replacement invalidation during preparation frees only unpublished private storage");
        Fixture::retire_on_allocation=0;
    }
    {
        // Constructor-layout destination and controlled allocation/weak APIs;
        // this tests the production storage handoff, not native construction,
        // render registration, GPU completion or gameplay continuation.
        std::array<std::byte,0x260> original_render{},fresh_render{};
        std::array<std::byte,0x2a0> constructed{};
        std::array<std::uintptr_t,1> system{},slots{address(constructed)};
        put(root,0x1d0,address(system));put(root,0x1e0,address(original_render));
        put(constructed,0,base+0x394c100);put(constructed,0x1d0,address(system));
        put(constructed,0x1d8,address(module)+0x30);put(constructed,0x1e0,address(fresh_render));
        put(fresh_render,0,base+0x394bfc0);put(fresh_render,0x1f0,base+0x394c000);put(fresh_render,0x240,-1);
        put(fresh,0xa50,address(slots));put(fresh,0xa58,1);put(fresh,0xa5c,1);
        put(fresh,0xa60,address(system));
        ReplayEmitterStorageTestAccess::GpuBindingImage(image,base,root.data(),address(asset),address(source),address(lod),address(module));
        Fixture::retired=address(source);Fixture::retired_reads=0;
        Fixture::factory_calls=0;Fixture::factory_result=constructed.data();
        Sc6ReplayCpuEmitterState::FreshGpuOwner native_owner;
        expect(!image.ConstructFreshGpuOwner(binding,0,native_owner).ok()
            && native_owner.phase==Sc6ReplayCpuEmitterState::FreshGpuOwner::Phase::Empty && !Fixture::factory_calls,
            "fresh GPU constructor reserves native allocation charge before entry");
        expect(image.ConstructFreshGpuOwner(binding,4096,native_owner).ok()
            && native_owner.root==constructed.data() && native_owner.render==address(fresh_render)
            && native_owner.charged_bytes==0x500+sizeof(native_owner) && Fixture::factory_calls==1,
            "fresh GPU constructor journal retains exact independent root and render identity");
        expect(!image.ConstructFreshGpuOwner(binding,4096,native_owner).ok() && Fixture::factory_calls==1,
            "native GPU construction is never repeated on an acquired journal");
        Sc6ReplayCpuEmitterState::FreshGpuOwner failed_owner;Fixture::factory_result=nullptr;
        expect(!image.ConstructFreshGpuOwner(binding,4096,failed_owner).ok()
            && failed_owner.phase==Sc6ReplayCpuEmitterState::FreshGpuOwner::Phase::Failed
            && !image.ConstructFreshGpuOwner(binding,4096,failed_owner).ok() && Fixture::factory_calls==2,
            "failed native construction preserves a nonretryable journal");
        // The native allocator may reuse the retired A render address. The
        // captured number is not a live owner and cannot exclude new storage.
        original_render=fresh_render;
        put(constructed,0x1e0,address(original_render));Fixture::factory_result=constructed.data();
        Sc6ReplayCpuEmitterState::FreshGpuOwner reused_owner;
        expect(image.ConstructFreshGpuOwner(binding,4096,reused_owner).ok()
            && reused_owner.render==address(original_render),"fresh GPU construction permits expired A render address reuse");
        Fixture::retired=0;
        Sc6ReplayCpuEmitterState::FreshGpuOwner live_alias;
        expect(!image.ConstructFreshGpuOwner(binding,4096,live_alias).ok(),"fresh GPU construction rejects live original render alias");
        Fixture::retired=address(source);put(constructed,0x1e0,address(fresh_render));
        Fixture::factory_result=nullptr;
        Sc6ReplayCpuEmitterState::Prepared p;
        expect(image.PrepareReplacement(1024*1024,p,binding).ok(),"prepare fresh native GPU storage handoff");
        const auto empty_root=constructed;
        const auto render_before=fresh_render,original_before=original_render;
        bool dirty=false;
        put(constructed,0x18,address(fresh));
        expect(!p.PublishFreshGpuStorage(0,constructed.data(),dirty).ok() && !dirty,
            "already bound GPU root cannot masquerade as newly constructed");
        constructed=empty_root;put(constructed,0x1e0,address(original_render));
        expect(p.ValidateFreshGpuDestination(0,constructed.data()).ok(),"fresh GPU publication permits expired render address reuse");
        Fixture::retired=0;
        expect(!p.PublishFreshGpuStorage(0,constructed.data(),dirty).ok() && !dirty,
            "fresh GPU publication rejects the still-live captured render owner");
        Fixture::retired=address(source);Fixture::retired_reads=0; // subsequent non-alias handoff must not query the old identity
        constructed=empty_root;put(fresh_render,0x240,0);
        expect(!p.PublishFreshGpuStorage(0,constructed.data(),dirty).ok() && !dirty,
            "registered render owner cannot use fresh GPU publication");
        fresh_render=render_before;put(constructed,0x1e8,std::uintptr_t{1});
        expect(!p.PublishFreshGpuStorage(0,constructed.data(),dirty).ok() && !dirty,
            "preexisting GPU tile ownership rejects before writing");
        constructed=empty_root;
        expect(p.PublishFreshGpuStorage(0,constructed.data(),dirty).ok() && dirty && p.published(),
            "fresh GPU publication installs captured storage without parameter initialization");
        auto expected=root;put(expected,0x18,address(fresh));put(expected,0x1e0,address(fresh_render));
        expect(constructed==expected && fresh_render==render_before && original_render==original_before
            && Fixture::retired_reads==0,"fresh GPU handoff preserves independent render ownership and never resolves old component");
        expect(!p.PublishFreshGpuStorage(0,constructed.data(),dirty).ok() && dirty,"dirty GPU publication cannot repeat");
        expect(p.UndoPublication().ok() && !p.published(),"unexecuted fresh GPU graph can recover constructor storage");
        auto bound_empty=empty_root;put(bound_empty,0x18,address(fresh));
        expect(constructed==bound_empty && fresh_render==render_before && original_render==original_before,
            "fresh graph undo leaves the native constructor/render pair for enclosing retirement");
        Fixture::native_destroys=0;
        expect(!image.QueueFreshGpuRetirement(binding,native_owner).ok() && !Fixture::native_destroys,
            "fresh native root cannot retire while still attached to its private emitter slot");
        slots[0]=0;put(fresh_render,0x240,2);
        expect(!image.QueueFreshGpuRetirement(binding,native_owner).ok() && !Fixture::native_destroys,
            "registered GPU work cannot use unused-constructor retirement");
        fresh_render=render_before;
        expect(image.QueueFreshGpuRetirement(binding,native_owner).ok() && Fixture::native_destroys==1
            && native_owner.phase==Sc6ReplayCpuEmitterState::FreshGpuOwner::Phase::RetirementQueued
            && native_owner.charged_bytes!=0,"native destructor return retains the journal charge pending render/GPU completion");
        expect(!image.QueueFreshGpuRetirement(binding,native_owner).ok() && Fixture::native_destroys==1,
            "queued GPU retirement never dereferences or destroys the freed root again");
    }
    expect(Fixture::allocated==Fixture::freed,"fresh publication retires private staging allocations once after undo");
    Fixture::retired=0;VirtualFree(memory,0,MEM_RELEASE);
#endif
}

static void test_gpu_descriptor_owner_binding()
{
#ifdef _WIN32
    // Controlled weak resolution only. The production ValidateBindings method
    // must establish descriptor ownership through the native asset/LOD graph.
    auto* memory=static_cast<std::byte*>(VirtualAlloc(nullptr,0xf84000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    expect(memory!=nullptr,"GPU descriptor weak fixture allocation");if(!memory)return;
    constexpr unsigned char resolve[]{0x48,0x8b,0x01,0xc3};
    std::memcpy(memory+0xf823f0,resolve,sizeof(resolve));DWORD previous{};
    expect(VirtualProtect(memory+0xf82000,4096,PAGE_EXECUTE_READ,&previous)!=0,"seal GPU weak fixture");
    const auto base=reinterpret_cast<std::uintptr_t>(memory);
    std::array<std::byte,0x170> asset{};std::array<std::byte,0xc0> lod0{},lod1{};
    std::array<std::byte,0x460> module{},foreign{};std::array<std::byte,0x2a0> emitter{};
    std::array<std::byte,0xad0> component{};
    const auto address=[](auto& v){return reinterpret_cast<std::uintptr_t>(v.data());};
    const auto put=[](auto& v,std::size_t offset,auto value){std::memcpy(v.data()+offset,&value,sizeof(value));};
    std::array<std::uintptr_t,2> lods{address(lod0),address(lod1)};
    put(asset,0x38,address(lods));put(asset,0x40,2);
    put(lod0,0x48,address(module));put(module,0,base+0x394b9e0);put(foreign,0,base+0x394b9e0);
    put(emitter,0x10,address(asset));put(emitter,0x18,address(component));put(emitter,0x28,address(lod1));
    put(emitter,0x1d8,address(module)+0x30);
    const auto validate=[&]{
        Sc6ReplayCpuEmitterState image;
        ReplayEmitterStorageTestAccess::GpuBindingImage(image,base,emitter.data(),address(asset),address(component),address(lod1),address(module));
        return image.ValidateBindings();
    };
    expect(validate().ok(),"GPU descriptor uses retained LOD-zero type owner while current LOD differs");
    put(emitter,0x1d8,address(foreign)+0x30);
    expect(!validate().ok(),"same-field foreign descriptor rejected despite live original module");
    put(emitter,0x1d8,address(module)+0x30);put(lod0,0x48,address(foreign));
    expect(!validate().ok(),"replaced LOD-zero type owner rejected");
    put(lod0,0x48,address(module));put(module,0,base+0x394b9e8);
    expect(!validate().ok(),"wrong native GPU module class rejected");
    put(module,0,base+0x394b9e0);put(asset,0x40,0);
    expect(!validate().ok(),"missing first LOD rejects descriptor construction");
    put(asset,0x40,2);
    expect(validate().ok(),"restored owner graph revalidates without mutation");
    VirtualFree(memory,0,MEM_RELEASE);
#endif
}

static void test_static_vector_field_publication()
{
#ifdef _WIN32
    constexpr std::uintptr_t base=0x140000000ull;
    std::array<std::byte,0x260> render{};
    std::array<std::byte,0x70> resource{};
    std::array<std::byte,0xa8> asset{};
    const auto r=reinterpret_cast<std::uintptr_t>(render.data());
    const auto f=reinterpret_cast<std::uintptr_t>(resource.data());
    const auto a=reinterpret_cast<std::uintptr_t>(asset.data());
    const auto put=[](auto& bytes,std::size_t offset,auto value){std::memcpy(bytes.data()+offset,&value,sizeof(value));};
    put(render,0,base+0x394bfc0);put(render,0xe0,f);
    put(resource,0,base+0x39e5a90);put(resource,0x30,std::uintptr_t{0x12340000});
    for(auto offset:{0x38,0x3c,0x40})put(resource,offset,50);
    put(asset,0,base+0x39e5068);put(asset,0x58,f);
    std::array<std::byte,0x2a0> emitter{};
    std::array<std::byte,0x40> descriptor{};
    put(emitter,0x1d8,reinterpret_cast<std::uintptr_t>(descriptor.data()));put(descriptor,0x30,a);
    expect(ReplayEmitterStorageTestAccess::GpuFieldBinding(emitter.data(),a),"GPU descriptor matches captured asset lease");
    put(descriptor,0x30,a+8);
    expect(!ReplayEmitterStorageTestAccess::GpuFieldBinding(emitter.data(),a),"GPU descriptor replacement rejects even when old asset stays alive");
    put(descriptor,0x30,std::uintptr_t{});
    expect(ReplayEmitterStorageTestAccess::GpuFieldBinding(emitter.data(),0),"GPU emitter without local field remains supported");
    ReplayStaticVectorField first,second;
    expect(first.Capture(base,r) && first.AssetBound(base,a),"static field borrows exact retained native asset resource");
    render[0x110]=std::byte{7};render[0x1d0]=std::byte{8};render[0x1dc]=std::byte{3};render[0x88]=std::byte{9};
    expect(second.Capture(base,r) && !first.Matches(base),"native field publication differs from retained A");
    const auto complete_b=render;
    std::array<ReplayStaticVectorField,2> publication{first,first};
    expect(!ReplayStaticVectorField::PublishSet(base,publication,false,true) && render==complete_b,"pending GPU completion rejects publication");
    expect(!ReplayStaticVectorField::PublishSet(base,publication,true,false) && render==complete_b,"missing complete undo rejects publication");
    publication[1].resource++;
    expect(!ReplayStaticVectorField::PublishSet(base,publication,true,true) && render==complete_b,"late binding rejection leaves every earlier field at B");
    publication[1]=first;
    expect(ReplayStaticVectorField::PublishSet(base,publication,true,true),"retry after preflight rejection publishes complete field set");
    expect(second.Publish(base) && render==complete_b,"batch publication recovers complete B");

    expect(first.Publish(base) && first.Matches(base),"publish consumed A transform and parameters");
    expect(render[0x88]==std::byte{},"publish A pending GPU timing as well as field transforms");
    expect(second.Publish(base) && render==complete_b,"complete B field recovery preserves unrelated native ownership bytes");
    expect(second.Publish(base) && render==complete_b,"repeated field undo is idempotent");
    put(render,0xc8,1);
    expect(!first.Publish(base),"nonempty native spawn queue cannot be omitted by scalar publication");
    put(render,0xc8,0);
    put(render,0xd8,1);
    expect(!first.Publish(base),"nonempty native tile queue cannot be omitted by scalar publication");
    put(render,0xd8,0);
    resource[0x44]=std::byte{1};
    expect(!first.Publish(base) && render==complete_b,"changed shared strength rejects before field writes");
    resource[0x44]=std::byte{};
    put(asset,0x58,f+8);expect(!first.AssetBound(base,a),"replaced native resource invalidates retained asset binding");
    put(asset,0x58,f);
    render[0x1e0]=std::byte{1};expect(!first.Publish(base),"owned animated field never uses shared-resource path");
    render[0x1e0]=std::byte{};
    put(resource,0,base+0x39e5a98);expect(!first.Publish(base),"unknown update implementation rejects");
    put(resource,0,base+0x39e5a90);
    expect(first.Publish(base) && second.Publish(base) && render==complete_b,"rejected preparation leaves undo reusable");
    auto* page=static_cast<std::byte*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    expect(page!=nullptr,"partial field publication allocates controlled destination");
    if(page) {
        std::memcpy(page,render.data(),render.size());
        ReplayStaticVectorField undo_second;expect(undo_second.Capture(base,reinterpret_cast<std::uintptr_t>(page)),"retain second complete B field");
        auto target_second=first;target_second.render=reinterpret_cast<std::uintptr_t>(page);
        std::array<ReplayStaticVectorField,2> targets{first,target_second},undo{second,undo_second};
        DWORD old_protection{};
        const bool protected_page=VirtualProtect(page,4096,PAGE_READONLY,&old_protection)!=0;
        expect(protected_page,"controlled second field becomes read-only");
        if(protected_page) {
            expect(!ReplayStaticVectorField::PublishSet(base,targets,true,true)
                && first.Matches(base) && undo_second.Matches(base),"partial native write failure preserves reusable complete B images");
            DWORD previous{};
            expect(VirtualProtect(page,4096,old_protection,&previous)!=0,"restore controlled native destination access");
            expect(ReplayStaticVectorField::PublishSet(base,undo,true,true)
                && render==complete_b && !std::memcmp(page,complete_b.data(),complete_b.size()),"partial publication recovers both B fields including untouched ownership bytes");
        }
        VirtualFree(page,0,MEM_RELEASE);
    }

#endif
}

#include "replay_gpu_spawn_payload_selftest.inl"

static void test_spawn_per_unit_payload_admission()
{
#ifdef _WIN32
    using Access=ReplayEmitterStorageTestAccess;
    std::array<std::uintptr_t,0x330/8> table{};
    const auto address=reinterpret_cast<std::uintptr_t>(table.data());
    const auto base=address-0x395ed20;
    std::uintptr_t module=address;
    table[0x268/8]=base+0x301490;table[0x270/8]=base+0x1e030c0;table[0x328/8]=base+0x1fcee40;
    expect(Access::SpawnPerUnitPayload(base,&module,4),"exact four-byte native distance payload admitted");
    for(int bytes:{0,3,5,20})expect(!Access::SpawnPerUnitPayload(base,&module,bytes),"wrong payload extent rejected");
    for(auto slot:{0x268/8,0x270/8,0x328/8}) {
        const auto original=table[slot];table[slot]++;
        expect(!Access::SpawnPerUnitPayload(base,&module,4),"changed native size/init/consumer rejected");
        table[slot]=original;
    }
    module+=8;expect(!Access::SpawnPerUnitPayload(base,&module,4),"unrecognized module class rejected");
    expect(!Access::SpawnPerUnitPayload(base,nullptr,4),"missing native module rejected without writes");
#endif
}

static void test_native_emitter_destruction_transaction()
{
    {
        const std::map<std::string,std::string> request{{"historical_anchor_tick","170"},{"historical_advanced_tick","6538"},
            {"host_seek_target","2510"},{"host_seek","true"}};
        expect(IsBoundedGroundCommit(request),"bounded ground commit admitted without cancellation");
        for(const auto& [key,value]:request) {
            auto invalid=request;invalid.erase(key);expect(!IsBoundedGroundCommit(invalid),"ground commit requires exact coordinates");
        }
        for(const auto key:{"historical_cancel","seek_advance_failure","seek_settlement_failure","seek_drained_cancel",
            "corrected_inputs","changed_inputs","source_revision","host_seek_repeat","seek_observer_failure","seek_preparation_failure"}) {
            auto invalid=request;invalid[key]="true";expect(!IsBoundedGroundCommit(invalid),"ground commit rejects interventions");
        }
    }

    {
        std::map<std::string,std::string> request{{"historical_anchor_tick","170"},{"historical_advanced_tick","300"},
            {"host_seek_target","300"},{"host_seek","true"},{"historical_cancel","after"},
            {"seek_advance_failure","true"},{"seek_settlement_failure","true"},{"seek_drained_cancel","true"}};
        const auto saved=request;
        expect(IsBoundedPrivateHudDrainedRecovery(request) && request==saved,"private HUD drained recovery request preserves exact boundary");
        for(const auto& [key,value]:saved) {
            auto invalid=saved;invalid.erase(key);expect(!IsBoundedPrivateHudDrainedRecovery(invalid),"private HUD drained recovery requires each phase coordinate");
        }
        for(const auto key:{"corrected_inputs","changed_inputs","source_revision","host_seek_repeat","seek_observer_failure","seek_preparation_failure"}) {
            auto invalid=saved;invalid[key]="true";expect(!IsBoundedPrivateHudDrainedRecovery(invalid),"private HUD drained recovery rejects unrelated intervention");
        }
    }
    {
        unsigned target=55;std::map<std::string,std::string> request;
        expect(ReadReplayObservationTarget(request,target) && target==0,"absent read-only observation window preserves defaults");
        request={{"observation_target","5695"},{"skip_intros","true"}};const auto saved=request;
        expect(ReadReplayObservationTarget(request,target) && target==5695 && request==saved,"native HUD/pose observation accepts B5695 without changing request history");
        for(const auto invalid:{"","0","169","35881","999999","-1","5695x"}) {
            request["observation_target"]=invalid;target=5695;
            expect(!ReadReplayObservationTarget(request,target) && target==5695,"invalid observation window rejects without partial output");
        }
        request=saved;request.erase("skip_intros");
        expect(!ReadReplayObservationTarget(request,target),"late observation cannot introduce an unverified intro intervention");
    }
    for(const auto original:{"329","5695","6538"}) {
        std::map<std::string,std::string> request{{"historical_anchor_tick","170"},{"historical_advanced_tick",original},
            {"host_seek_target","2510"},{"host_seek","true"},{"historical_cancel","after"},
            {"seek_advance_failure","true"},{"seek_settlement_failure","true"},{"seek_drained_cancel","true"}};
        const auto saved=request;
        expect(IsBoundedTraceRenderRecovery(request) && request==saved,"trace render drained recovery request preserves exact boundary");
        for(const auto& [key,value]:saved) {
            auto invalid=saved;invalid.erase(key);expect(!IsBoundedTraceRenderRecovery(invalid),"trace render drained recovery requires each phase coordinate");
        }
        for(const auto key:{"corrected_inputs","changed_inputs","source_revision","host_seek_repeat","seek_observer_failure","seek_preparation_failure"}) {
            auto invalid=saved;invalid[key]="true";expect(!IsBoundedTraceRenderRecovery(invalid),"trace render drained recovery rejects unrelated intervention");
        }
    }
    for(const auto cancel:{"before","after"}) {
        std::map<std::string,std::string> request{{"historical_anchor_tick","170"},{"historical_advanced_tick","300"},
            {"host_seek_target","208"},{"host_seek","true"},{"historical_cancel",cancel}};
        const auto saved=request;
        expect(IsBoundedPrivateHudRecovery(request) && request==saved,"private HUD recovery request is bounded and read-only");
        for(const auto& [key,value]:saved) {
            auto invalid=saved;invalid.erase(key);expect(!IsBoundedPrivateHudRecovery(invalid),"private HUD recovery requires each coordinate");
        }
        for(const auto key:{"corrected_inputs","changed_inputs","source_revision","host_seek_repeat","seek_advance_failure","seek_settlement_failure"}) {
            auto invalid=saved;invalid[key]="true";expect(!IsBoundedPrivateHudRecovery(invalid),"private HUD recovery rejects unrelated intervention");
        }
    }
    {
        std::map<std::string,std::string> guard{{"historical_anchor_tick","170"},{"historical_advanced_tick","300"},
            {"host_seek_target","300"},{"host_seek","true"},{"corrected_inputs","true"},
            {"source_revision","true"},{"source_revision_profile","guard201"}};
        const auto saved=guard;
        expect(IsBoundedSameOriginGuard(guard) && guard==saved,"same-origin guard protocol preserves request fields");
        for(const auto& [key,value]:saved) {
            auto missing=saved;missing.erase(key);
            expect(!IsBoundedSameOriginGuard(missing),"same-origin guard requires every bounded input/source coordinate");
        }
        for(const auto key:{"historical_cancel","host_seek_repeat","seek_advance_failure","seek_settlement_failure"}) {
            auto invalid=saved;invalid[key]="true";
            expect(!IsBoundedSameOriginGuard(invalid),"same-origin guard does not admit cancellation or failure protocols");
        }
    }
    std::map<std::string,std::string> request{{"historical_advanced_tick","210"},{"host_seek_target","208"},{"seek_settlement_failure","true"}};
    const auto request_before=request;
    {
        std::map<std::string,std::string> fallback{{"historical_anchor_tick","170"},{"historical_advanced_tick","210"},
            {"host_seek_target","220"},{"checkpoint_fallback","true"}};
        const auto original=fallback;
        expect(IsBoundedExecutionFallback(fallback) && fallback==original,"execution fallback request is read-only and bounded");
        for(const auto key:{"historical_cancel","host_seek_repeat","seek_advance_failure","corrected_inputs"}) {
            auto invalid=fallback;invalid[key]="true";
            expect(!IsBoundedExecutionFallback(invalid),"execution fallback cannot stand in for cancellation/injection/changed inputs");
        }
        fallback["historical_advanced_tick"]="220";
        expect(!IsBoundedExecutionFallback(fallback),"expired-nearest selection is separate from execution retry");
    }
    expect(IsBoundedEmitterRecovery(request) && request==request_before,"omitted anchor uses205 without mutating native request fields");
    {
        std::map<std::string,std::string> natural{{"historical_advanced_tick","210"},{"host_seek_target","220"},{"seek_advance_failure","true"},{"historical_cancel","after"}};
        const auto saved=natural;
        expect(IsBoundedParticleLifetimeRecovery(natural) && natural==saved,"natural lifetime request preserves omitted anchor and explicit B");
        natural["seek_settlement_failure"]="true";
        expect(!IsBoundedParticleLifetimeRecovery(natural),"injected settlement is not natural lifetime coverage");
        natural=saved;natural["historical_advanced_tick"]="220";
        expect(!IsBoundedParticleLifetimeRecovery(natural),"natural lifetime recovery is bounded to B210");
    }

    request["historical_anchor_tick"]="205";
    expect(IsBoundedEmitterRecovery(request),"explicit default anchor has the same bounded recovery meaning");
    for(const auto key:{"historical_anchor_tick","historical_advanced_tick","host_seek_target","seek_settlement_failure"}) {
        auto bad=request;bad[key]="unexpected";
        expect(!IsBoundedEmitterRecovery(bad),"unrecognized recovery coordinates or mode reject");
    }
#ifdef _WIN32
    using Prepared=Sc6ReplayCpuEmitterState::Prepared;
    const auto resolve=+[](const void* weak)->std::uintptr_t {std::uintptr_t p{};std::memcpy(&p,weak,sizeof(p));return p;};
    const auto base=reinterpret_cast<std::uintptr_t>(resolve)-0xf823f0;
    for(int scenario=0;scenario<11;++scenario) {
        std::array<std::byte,0xb00> component{};
        std::array<std::byte,0xc0> lod{};
        std::array<std::byte,0x1d0> original{};
        auto* c=static_cast<std::byte*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        expect(c!=nullptr,"allocate real C page for destruction test");if(!c)return;
        const auto set=[](void* p,std::size_t offset,auto value){std::memcpy(static_cast<std::byte*>(p)+offset,&value,sizeof(value));};
        set(original.data(),0,base+0x3949b60);set(original.data(),0x18,component.data());
        std::memcpy(c,original.data(),original.size());
        void* expected_B=scenario==8?nullptr:original.data();
        void* slots[]{expected_B,nullptr};
        set(component.data(),0xa50,slots);set(component.data(),0xa58,2);set(component.data(),0xa5c,2);
        Sc6ReplayCpuEmitterState b;Prepared p;
        const auto* saved_B=scenario==8?nullptr:&b;
        ReplayEmitterStorageTestAccess::PreparedImage(p,b,base,component.data(),lod.data(),c,original.data());
        expect(p.Publish(0,expected_B).ok() && p.BeginExecution(saved_B,65536).ok(),"publish A and retain complete private B before native execution");
        expect(Prepared::HasExecutionOwners(),"native C lifetime registered");
        expect(!p.SettleNativeDestruction(saved_B).ok(),"null/deletion admission requires native completion witness");
        if(scenario==1) {slots[0]=nullptr;expect(!p.SettleNativeDestruction(saved_B).ok(),"unwitnessed missing slot rejects");slots[0]=c;}
        if(scenario==2) {
            Prepared::NativeDestruction(c,false,0,false);
            Prepared::NativeDestruction(c,false,0,true);
            expect(p.destruction_diagnostic()==(3u|(7u<<2)|0x300u),"bounded journal records exact rejected deleting flags without reading retired storage");
        } else {
            Prepared::NativeDestruction(c,false,1,false);
            expect(!p.SettleNativeDestruction(saved_B).ok(),"destructor entry is not completion");
            // Make C unreadable before delivering the return witness. Every
            // subsequent production operation must avoid this retired graph.
            expect(VirtualFree(c,0,MEM_RELEASE)!=0,"native fixture releases C before return");
            Prepared::NativeDestruction(c,false,1,true);
            expect(p.destruction_diagnostic()==0x302u,"completed destructor journal remains readable after native release");
        }
        slots[0]=nullptr;
        if(scenario==10) {
            // Native allocator may reuse the retired address for another emitter.
            // The completed receipt belongs to the first allocation's lifetime;
            // later hooks at the same address must not invalidate it or read it.
            Prepared::NativeDestruction(c,false,1,false);
            Prepared::NativeDestruction(c,false,1,true);
            expect(p.native_destroyed() && p.destruction_diagnostic()==0x302u,
                "completed C death survives subsequent native address reuse");
        }
        if(scenario==2)expect(!p.SettleNativeDestruction(saved_B).ok(),"nondeleting destructor flags never admit missing C");
        else {
            if(scenario==3) {
                slots[0]=original.data();expect(!p.SettleNativeDestruction(saved_B).ok(),"rebound ordinal rejects");slots[0]=nullptr;
            }
            if(scenario==4) {
                set(component.data(),0xa58,1);expect(!p.SettleNativeDestruction(saved_B).ok(),"changed emitter array extent rejects");set(component.data(),0xa58,2);
            }
            if(scenario==5) {
                original[0x30]=std::byte{1};expect(!p.SettleNativeDestruction(saved_B).ok(),"B mutation rejects before settlement");original[0x30]=std::byte{};
            }
            if(scenario==6) {
                set(component.data(),0xa81,std::uint8_t{1});expect(!p.SettleNativeDestruction(saved_B).ok(),"pending native particle task rejects");set(component.data(),0xa81,std::uint8_t{});
            }
            expect(p.SettleNativeDestruction(saved_B).ok() && (saved_B?b.ValidateDisplaced(p):p.ValidatePublished()).ok(),"completed native deletion settles null C and intact B without reading freed storage");
            expect(p.ReopenExecutionForUndo(saved_B).ok() && p.ReopenExecutionForUndo(saved_B).ok()
                && p.SettleNativeDestruction(saved_B).ok(),"terminal C reopening and resettlement are retryable");
            if(scenario==7) {
                void* displaced{};expect(p.CommitPublication(displaced).ok() && displaced==original.data() && !slots[0],"commit keeps native C null and transfers B for retirement exactly once");
                expect(!p.CommitPublication(displaced).ok(),"second commit rejects");
            } else {
                expect(p.UndoPublication().ok() && slots[0]==expected_B,"undo restores exact original B root from null C");
                expect(p.UndoPublication().ok() && slots[0]==expected_B,"undo retry leaves B unchanged");
            }
            expect(!Prepared::HasExecutionOwners(),"commit or undo releases lifetime registration");
        }
        ReplayEmitterStorageTestAccess::FixtureCleanup(p);
        if(scenario==2)VirtualFree(c,0,MEM_RELEASE);
    }
#endif
}

static void test_ui_transaction_admission()
{
#ifdef _WIN32
    using Admission=ReplayUiTransactionAdmission;
    struct Array {void* data{};int count{},capacity{};};
    struct Dispatcher {std::uintptr_t vtable{};Array backends{};};
    static_assert(sizeof(Array)==16 && offsetof(Dispatcher,backends)==8);
    constexpr std::uintptr_t table=0x12345678;
    unsigned char traversing=0;
    Admission::Diagnostic diagnostic{};
    Dispatcher idle{table},unrelated{table+8,{nullptr,19,19}},pending{table};
    void* entries[]{&idle,&unrelated,nullptr,&pending};
    Array registry{entries,4,4};
    auto check=[&] {return Admission::CheckRegistry(&registry,&traversing,table,diagnostic);};
    expect(check() && diagnostic.check==0,"idle UI dispatchers and unrelated tickables admitted");
    std::array<std::byte,0xb8> empty_backend{};
    void* backend_pair[]{empty_backend.data(),nullptr};
    pending.backends={backend_pair,1,1};
    const auto before=pending;
    expect(!check() && diagnostic.check==41 && diagnostic.owner==reinterpret_cast<std::uintptr_t>(&pending)
        && diagnostic.count==1,"queued backend rejects even with zero internal requests until native retirement");
    expect(!std::memcmp(&pending,&before,sizeof(before)),"UI admission never edits queued ownership");
    pending.backends={};
    traversing=1;expect(!check() && diagnostic.check==40,"active native traversal rejects admission");traversing=0;
    for(const Array invalid:{Array{entries,-1,4},Array{entries,4,3},Array{nullptr,1,1},Array{entries,0,65537},Array{nullptr,0,-1}}) {
        registry=invalid;expect(!check(),"invalid tickable storage rejects admission");
    }
    registry={entries,4,4};
    for(const Array invalid:{Array{nullptr,0,1},Array{nullptr,-1,0},Array{nullptr,0,-1},Array{backend_pair,0,65537}}) {
        pending.backends=invalid;expect(!check() && diagnostic.check==41,"invalid dispatcher storage rejects admission");
    }
    pending.backends={};entries[3]=reinterpret_cast<void*>(1);
    expect(!check(),"unreadable tickable rejects safely");entries[3]=&pending;
    registry={};expect(check(),"empty native registry admitted");
    traversing=1;expect(!check(),"empty registry still rejects active traversal");traversing=0;
    expect(!Admission::CheckRegistry(nullptr,&traversing,table,diagnostic)
        && !Admission::CheckRegistry(&registry,nullptr,table,diagnostic)
        && !Admission::Check(0,diagnostic),"missing UI admission bindings reject");
    unsigned char signature[]{0x48,0x89,0x5c,0x24,0x20,0x56,0x48,0x83,0xec,0x20,0x48,0x8d,0x71,0x08,0x48,0x8b};
    const auto base=reinterpret_cast<std::uintptr_t>(signature)-0x2f21fd0;
    expect(Admission::Signature(base),"verified dispatcher signature accepted");
    signature[5]^=1;expect(!Admission::Signature(base) && !Admission::Signature(0),"changed dispatcher signature rejected");
#endif
}

void test_hud_native_owner_route()
{
    std::array<std::array<std::byte,0x600>,4> objects{};
    const auto address=[&](unsigned i){return reinterpret_cast<std::uintptr_t>(objects[i].data());};
    const auto put=[&](unsigned i,std::size_t offset,std::uintptr_t value){std::memcpy(objects[i].data()+offset,&value,sizeof(value));};
    constexpr std::uintptr_t base=0x140000000;
    put(0,0x530,address(1));put(1,0,base+0x3281920);put(1,0x98,address(0));
    put(1,0x3b8,address(2));put(2,0,base+0x327e810);put(2,0x98,address(0));
    put(1,0x3a0,address(3));put(3,0,base+0x327b740);put(3,0x98,address(0));
    const auto read=[&](std::uintptr_t p,void* out,std::size_t size) {
        for(const auto& object:objects) {
            const auto begin=reinterpret_cast<std::uintptr_t>(object.data());
            if(p>=begin && p-begin<=object.size() && size<=object.size()-(p-begin)) {
                std::memcpy(out,reinterpret_cast<void*>(p),size);return true;
            }
        }
        return false;
    };
    const auto check=[&]{return InspectReplayHudOwnerRoute(read,base,address(0));};
    const auto initial=objects;
    auto route=check();expect(route.valid && route.manager==address(1) && route.controller==address(2)
        && route.damage_listener==address(3) && objects==initial,"native HUD route reads the battle-owned objects without mutation");
    ReplayHudOwnerCensus census(route);
    expect(!census.complete() && census.Observe(address(2),1) && census.Observe(address(3),2) && census.complete(),
        "HUD capture requires both canonical owners in the complete census");
    expect(!census.Observe(address(1),1) && !census.complete(),"extra same-battle controller rejects despite a valid canonical route");
    ReplayHudOwnerCensus duplicate(route);
    expect(duplicate.Observe(address(2),1) && !duplicate.Observe(address(2),1),"duplicate HUD enumeration is rejected");
    ReplayHudOwnerCensus missing(route);missing.Observe(address(2),1);
    expect(!missing.complete(),"missing damage listener cannot pass capture");
    unsigned classifications{};
    const auto classify=[&](std::uintptr_t type){++classifications;return type==0x1000?1u:2u;};
    ReplayHudOwnerCensus cache(route);
    expect(cache.MatchClass(0x1000,classify)==1 && cache.MatchClass(0x1000,classify)==1 && classifications==1,
        "one census reuses an unchanged class predicate");
    expect(cache.MatchClass(0x1400,classify)==2 && cache.MatchClass(0x1000,classify)==1 && classifications==2,
        "two colliding class keys retain distinct predicates without repeated ancestry walks");
    ReplayHudOwnerCensus fresh(route);
    expect(fresh.MatchClass(0x1000,classify)==1 && classifications==3,"class predicate cache never survives the capture call");
    for(unsigned owner=1;owner<4;++owner) {
        put(owner,0x98,address(3));const auto before=objects;
        expect(!check().valid && objects==before,"foreign HUD owner rejects without writes");objects=initial;
        put(owner,0,base+1);expect(!check().valid,"unverified HUD owner class rejects");objects=initial;
    }
    for(const auto slot:std::array{std::pair{0u,0x530u},std::pair{1u,0x3b8u},std::pair{1u,0x3a0u}}) {
        for(const auto missing:std::array<std::uintptr_t,2>{0,1}) {
            put(slot.first,slot.second,missing);const auto before=objects;
            expect(!check().valid && objects==before,"missing or unreadable HUD ownership rejects before publication");objects=initial;
        }
    }
    expect(!InspectReplayHudOwnerRoute(read,0,address(0)).valid
        && !InspectReplayHudOwnerRoute(read,base,UINTPTR_MAX-8).valid,"HUD route rejects invalid image and overflowing owner");
}

static void test_framework_flag_conversion()
{
    using namespace RC::Unreal;
    using Impl=Flags412::EObjectFlags_Impl;
    // Independent lookup of the old per-flag mapping. Include unsupported
    // bits in inputs and preserve the public StrongRef/NonPIE alias.
#define FLAG_PAIR(name) std::pair{std::uint32_t(name),std::uint32_t(Impl::name)}
    const std::array mapping{
        FLAG_PAIR(RF_Public),FLAG_PAIR(RF_Standalone),FLAG_PAIR(RF_MarkAsNative),FLAG_PAIR(RF_Transactional),
        FLAG_PAIR(RF_ClassDefaultObject),FLAG_PAIR(RF_ArchetypeObject),FLAG_PAIR(RF_Transient),FLAG_PAIR(RF_MarkAsRootSet),
        FLAG_PAIR(RF_TagGarbageTemp),FLAG_PAIR(RF_NeedLoad),FLAG_PAIR(RF_NeedPostLoad),FLAG_PAIR(RF_NeedPostLoadSubobjects),
        FLAG_PAIR(RF_BeginDestroyed),FLAG_PAIR(RF_FinishDestroyed),FLAG_PAIR(RF_BeingRegenerated),FLAG_PAIR(RF_DefaultSubObject),
        FLAG_PAIR(RF_WasLoaded),FLAG_PAIR(RF_TextExportTransient),FLAG_PAIR(RF_LoadCompleted),
        FLAG_PAIR(RF_InheritableComponentTemplate),FLAG_PAIR(RF_StrongRefOnFrame),FLAG_PAIR(RF_Dynamic)};
#undef FLAG_PAIR
    const auto check=[&](std::uint32_t bits) {
        std::uint32_t expected{};
        for(const auto& [source,destination]:mapping)if(bits&source)expected|=destination;
        expect(std::uint32_t(Flags412::to_impl_flags(static_cast<EObjectFlags>(bits)))==expected,
            "framework flag conversion preserves named mapping and ignored bits");
    };
    check(0);check(UINT32_MAX);
    for(unsigned bit=0;bit<32;++bit){check(1u<<bit);check(~(1u<<bit));}
    for(std::uint32_t bits=0;bits<65536;++bits){check(bits);check(bits<<16);}
    std::uint32_t bits=0x9e3779b9;
    for(unsigned i=0;i<4096;++i){bits=bits*1664525u+1013904223u;check(bits);}
}

static void test_scheduler_reconstructed_hashes(std::uintptr_t mapped_game=0)
{
    using Scheduler=Horse::Deterministic::Sc6ReplaySchedulerState;
    std::array<std::byte,33*16> slots{};
    std::array<std::uint32_t,2> flags{0xffffffffu,1};
    flags[0]&=~((1u<<2)|(1u<<10));
    std::array<std::int32_t,16> hashes{};
    for(std::size_t i=0;i<33;++i) {
        const std::uintptr_t key=0x1b100000110ull+i*0x123450;
        std::memcpy(slots.data()+i*16,&key,8);
    }
    slots[2*16+8]=std::byte{0xa5};slots[10*16+12]=std::byte{0x5a};
    expect(Scheduler::RehashTickSetStorage(slots,flags,hashes).ok(),"rebuild scheduler pointer-set hash chains");
    expect(slots[2*16+8]==std::byte{0xa5} && slots[10*16+12]==std::byte{0x5a},"rehash preserves sparse free-list storage");
    const auto baseline=slots;const auto hash_baseline=hashes;
    std::memcpy(slots.data()+16,slots.data(),8);const auto duplicate=slots;
    expect(!Scheduler::RehashTickSetStorage(slots,flags,hashes).ok() && slots==duplicate && hashes==hash_baseline,"duplicate keys reject before private hash mutation");
    slots=baseline;
    expect(!Scheduler::RehashTickSetStorage(slots,std::span<const std::uint32_t>(flags.data(),1),hashes).ok() && slots==baseline,"short occupancy rejects without writes");
    expect(!Scheduler::RehashTickSetStorage(slots,flags,std::span<std::int32_t>(hashes.data(),3)).ok(),"non-power-of-two native hash size rejects");
    if(!mapped_game)return;
    constexpr unsigned char signature[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x8b,0x41,0x08,0x49,0x8b,0xf8,0x48,0x8b,0xda};
    expect(!std::memcmp(reinterpret_cast<void*>(mapped_game+0xe725d0),signature,sizeof(signature)),"shipped pointer-set lookup signature");
    alignas(8) std::array<std::byte,0x50> header{};
    auto store=[&](std::size_t offset,auto value){std::memcpy(header.data()+offset,&value,sizeof(value));};
    store(0,slots.data());store(8,33);store(0x34,2);store(0x40,hashes.data());store(0x48,16);
    const auto lookup=reinterpret_cast<int*(*)(void*,int*,std::uintptr_t)>(mapped_game+0xe725d0);
    for(std::size_t i=0;i<33;++i) {
        std::uintptr_t key{};std::memcpy(&key,slots.data()+i*16,8);int found=-2;
        lookup(header.data(),&found,key);
        expect(found==((flags[i/32]>>(i%32))&1u?int(i):-1),"shipped lookup independently finds exactly the occupied scheduler keys");
    }
    bool stale_rejected=false;
    for(std::uintptr_t key=0x7fff00000110ull;key<0x7fff00001110ull;key+=0x10) {
        std::memcpy(slots.data(),&key,8);int found=-2;lookup(header.data(),&found,key);
        if(found==-1){stale_rejected=true;break;}
    }
    expect(stale_rejected,"native lookup rejects a pointer-only replacement under stale hash chains");
    expect(Scheduler::RehashTickSetStorage(slots,flags,hashes).ok(),"rehash after typed tick-address substitution");
    std::uintptr_t replacement{};std::memcpy(&replacement,slots.data(),8);int found=-2;lookup(header.data(),&found,replacement);
    expect(found==0,"shipped lookup finds replacement after production hash rebuild");
    std::cout<<"Shipped scheduler pointer lookup validates rebuilt chains and rejects stale pointer-only replacement\n";
}

static void test_particle_lighting_key_projection(std::uintptr_t mapped_game=0)
{
    using Binding=Horse::Deterministic::ReplayLightingBinding;
    std::array<std::byte,5*24> entries{};
    std::array<std::byte,4> flags{};
    std::array<std::byte,4*4> hashes{};
    const auto put=[](auto& bytes,std::size_t offset,auto value){std::memcpy(bytes.data()+offset,&value,sizeof(value));};
    put(flags,0,std::uint32_t{0x1b});
    for(unsigned i=0;i<4;++i)put(hashes,i*4,UINT32_MAX);
    const std::array<unsigned,5> keys{1,5,42,2,6};
    for(unsigned i=0;i<5;++i) {
        put(entries,i*24,keys[i]);put(entries,i*24+8,std::uintptr_t{0x1000+i});
        put(entries,i*24+16,UINT32_MAX);put(entries,i*24+20,keys[i]&3u);
    }
    put(hashes,4,1u);put(entries,24+16,0u);
    put(hashes,8,4u);put(entries,4*24+16,3u);
    const auto source=entries;const auto source_hashes=hashes;
    std::array<Binding::PrimitiveIdReplacement,2> mapping{{{1,8},{5,9}}};
    expect(Binding::RebindPrimitiveAllocationKeys(entries,5,flags,hashes,mapping),"project primitive lighting keys and native hash chains");
    expect(!std::memcmp(entries.data()+2*24,source.data()+2*24,24),"primitive projection preserves sparse hole");
    for(unsigned i=0;i<5;++i)expect(!std::memcmp(entries.data()+i*24+4,source.data()+i*24+4,12),"primitive projection preserves allocation pointer and padding");
    const auto projected=entries;const auto projected_hashes=hashes;
    mapping={{{8,2},{9,10}}};
    expect(!Binding::RebindPrimitiveAllocationKeys(entries,5,flags,hashes,mapping)&&entries==projected&&hashes==projected_hashes,"projected ID collision rejects before mutation");
    mapping={{{8,0},{9,10}}};
    expect(!Binding::RebindPrimitiveAllocationKeys(entries,5,flags,hashes,mapping)&&entries==projected,"zero fresh primitive ID rejects");
    put(entries,16,0u);const auto malformed=entries;
    mapping={{{8,12},{9,13}}};
    expect(!Binding::RebindPrimitiveAllocationKeys(entries,5,flags,hashes,mapping)&&entries==malformed,"cyclic original lighting chain rejects without normalization");
    entries=projected;
    if(!mapped_game)return;
    constexpr unsigned char signature[]{0x8b,0x81,0x48,0x01,0,0,0x4c,0x8b,0xd9,0x3b,0x81,0x74,0x01,0,0,0x74,0x56,0x8b,0x81,0x88,0x01,0,0,0x4c};
    expect(!std::memcmp(reinterpret_cast<void*>(mapped_game+0x12f6170),signature,sizeof(signature)),"shipped primitive lighting lookup signature");
    alignas(8) std::array<std::byte,0x190> cache{};
    put(cache,0x140,entries.data());put(cache,0x148,5);put(cache,0x174,1);put(cache,0x180,hashes.data());put(cache,0x188,4);
    const auto lookup=reinterpret_cast<std::uintptr_t(*)(void*,unsigned)>(mapped_game+0x12f6170);
    expect(lookup(cache.data(),8)==0x1000&&lookup(cache.data(),9)==0x1001
        &&lookup(cache.data(),2)==0x1003&&lookup(cache.data(),6)==0x1004
        &&!lookup(cache.data(),1)&&!lookup(cache.data(),5)&&!lookup(cache.data(),42),"shipped lighting lookup validates projected and untouched keys");
    entries=source;hashes=source_hashes;put(entries,0,8u);
    expect(!lookup(cache.data(),8),"shipped lighting lookup rejects ID-only replacement under stale hashes");
    entries=source;mapping={{{1,8},{5,9}}};
    expect(Binding::RebindPrimitiveAllocationKeys(entries,5,flags,hashes,mapping)&&lookup(cache.data(),8)==0x1000,"shipped lighting lookup finds production-rebound primitive");
    std::cout<<"Shipped primitive lighting lookup validates typed ID projection and rejects stale hashes\n";
}

#ifdef _WIN32
static unsigned component_task_calls;
static void* component_task_expected_event;
static void* component_task_expected_task;
static float component_task_expected_delta,component_task_expected_previous;
static void observe_component_task_payload(void* tick,float delta,int mode,unsigned thread,void* event)
{
    ++component_task_calls;
    expect(delta==component_task_expected_delta && mode==2 && thread==2,"native tick callback arguments");
    expect(*reinterpret_cast<float*>(static_cast<std::byte*>(tick)+0x3c)==component_task_expected_previous,
        "native timestamp mutation precedes component callback");
    expect(*reinterpret_cast<void**>(static_cast<std::byte*>(tick)+0x18)==component_task_expected_task,
        "native task backreference remains pending during callback");
    expect(event==component_task_expected_event,"native callback receives original event reference address");
    const auto* completion=*static_cast<void**>(event);
    expect((*reinterpret_cast<const std::uint64_t*>(static_cast<const std::byte*>(completion)+8)&(1ull<<26))==0,
        "native callback runs before event completion");
}
#include "replay_native_prerequisite_selftest.inl"
static void test_mapped_component_task_payload(std::uintptr_t game)
{
    constexpr unsigned char constructor[]{0x40,0x53,0x48,0x83,0xec,0x20,0xc7,0x41,8,0xff,0,0,0};
    constexpr unsigned char payload[]{0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x30};
    if(std::memcmp(reinterpret_cast<void*>(game+0x2156650),constructor,sizeof(constructor))
        || std::memcmp(reinterpret_cast<void*>(game+0x215d250),payload,sizeof(payload))) {
        expect(false,"shipped component task constructor/payload signatures");return;
    }
    // Shipped code with a controlled external consumer, not graph completion
    // or pool emulation. This test cannot establish component lifetime.
    alignas(16) std::array<std::byte,0x58> task{},tick{},event{};
    alignas(16) std::array<std::byte,0x940> world{};
    const auto put=[](auto& storage,std::size_t offset,auto value){std::memcpy(storage.data()+offset,&value,sizeof(value));};
    put(event,0x48,7);void* reference=event.data();
    reinterpret_cast<void*(*)(void*,void**,int)>(game+0x2156650)(task.data(),&reference,3);
    expect(!reference && *reinterpret_cast<void**>(task.data()+0x40)==event.data(),"native constructor transfers event reference");
    expect(*reinterpret_cast<int*>(event.data()+0x48)==7,"event transfer does not create a payload reference");
    std::array<std::uintptr_t,2> table{0,reinterpret_cast<std::uintptr_t>(&observe_component_task_payload)};
    put(tick,0,table.data());put(tick,0xd,std::uint8_t{1});
    put(task,0x10,tick.data());put(task,0x18,0.25f);put(task,0x1c,2);put(task,0x28,world.data());
    put(world,0x930,10.0f);put(world,0x934,20.0f);
    component_task_expected_task=task.data();component_task_expected_event=task.data()+0x40;
    const auto execute=reinterpret_cast<void(*)(void*,unsigned,void*)>(game+0x215d250);
    for(unsigned mode=0;mode<3;++mode) {
        put(tick,0x18,task.data());put(tick,0x3c,8.0f);
        put(tick,0x40,mode?0.5f:0.0f);put(tick,0xc,std::uint8_t(mode==2));
        component_task_calls=0;component_task_expected_delta=mode?(mode==2?12.0f:2.0f):0.25f;
        component_task_expected_previous=mode?(mode==2?20.0f:10.0f):-1.0f;
        execute(task.data()+0x10,2,task.data()+0x40);
        expect(component_task_calls==1,"native tick payload executes consumer once");
        expect(!*reinterpret_cast<void**>(tick.data()+0x18),"native pending pointer clears after callback");
        expect(*reinterpret_cast<int*>(event.data()+0x48)==7,"native payload retains event ownership");
    }
    put(tick,0xd,std::uint8_t{0});put(tick,0x18,task.data());put(tick,0x3c,123.0f);
    component_task_calls=0;execute(task.data()+0x10,2,task.data()+0x40);
    expect(component_task_calls==0 && !*reinterpret_cast<void**>(tick.data()+0x18)
        && *reinterpret_cast<float*>(tick.data()+0x3c)==123.0f,"native disabled tick clears task without callback or clock mutation");
    // Execute the whole shipped graph wrapper with no dependents. An actual
    // owned Win32 TLS slot/pool supplies only its external storage service;
    // native code closes the event, releases its reference and recycles task.
    constexpr unsigned char wrapper[]{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56};
    if(std::memcmp(reinterpret_cast<void*>(game+0x215ed20),wrapper,sizeof(wrapper))) {
        expect(false,"shipped graph wrapper signature");return;
    }
    const DWORD slot=TlsAlloc();expect(slot!=TLS_OUT_OF_INDEXES,"allocate owned graph pool TLS slot");
    if(slot==TLS_OUT_OF_INDEXES)return;
    alignas(16) std::array<std::byte,0x18> pool{};
    auto* native_slot=reinterpret_cast<DWORD*>(game+0x415d660);
    auto* tls_import=reinterpret_cast<std::uintptr_t*>(game+0x322c800);
    DWORD protection{};
    if(!VirtualProtect(tls_import,sizeof(*tls_import),PAGE_READWRITE,&protection)) {
        expect(false,"bind mapped graph TLS import");TlsFree(slot);return;
    }
    const auto prior_slot=*native_slot;const auto prior_import=*tls_import;
    *native_slot=slot;*tls_import=reinterpret_cast<std::uintptr_t>(&TlsGetValue);
    const bool bound=TlsSetValue(slot,pool.data())!=0;expect(bound,"bind actual graph TLS pool");
    if(bound) {
        put(tick,0xd,std::uint8_t{1});put(tick,0xc,std::uint8_t{0});put(tick,0x40,0.0f);
        put(tick,0x18,task.data());put(task,0x38,std::uint8_t{1});
        component_task_calls=0;component_task_expected_delta=0.25f;component_task_expected_previous=-1.0f;
        // Link the actual production dispatcher. Its unchanged branch invokes
        // the shipped virtual graph wrapper, including native event/pool work.
        alignas(16) std::array<std::byte,0x400> named_thread{};
        put(named_thread,0x10,2u);
        expect(ReplayTaskGroupTestAccess::Dispatch(game,task.data(),named_thread.data())
            ==Sc6ReplayTaskGroup::DispatchOutcome::Completed,"production dispatcher completes shipped graph task");
        expect(component_task_calls==1,"whole native graph task executes one callback");
        expect((*reinterpret_cast<std::uint64_t*>(event.data()+8)&(1ull<<26))!=0,"native graph task closes completion event");
        expect(*reinterpret_cast<int*>(event.data()+0x48)==6,"native graph task releases exactly its event reference");
        expect(*reinterpret_cast<void**>(pool.data()+8)==task.data() && *reinterpret_cast<int*>(pool.data()+0x10)==1,
            "native graph task returns storage to TLS pool exactly once");
        expect(!*reinterpret_cast<void**>(tick.data()+0x18) && task[0x38]==std::byte{},"native task retirement clears pending state");

        // Production retained-task control flow around the shipped constructor,
        // payload, completion and TLS recycling. Stack allocation ownership is
        // an EXTERNAL test service, not production UObject admission/lifetime.
        alignas(16) std::array<std::byte,0x58> held_task{},held_tick{},held_event{};
        put(held_event,0x48,7);reference=held_event.data();
        reinterpret_cast<void*(*)(void*,void**,int)>(game+0x2156650)(held_task.data(),&reference,3);
        put(held_tick,0,table.data());put(held_tick,0xd,std::uint8_t{1});
        put(held_tick,0x18,held_task.data());put(held_tick,0x3c,123.0f);
        put(held_task,0x10,held_tick.data());put(held_task,0x18,0.25f);
        put(held_task,0x1c,2);put(held_task,0x28,world.data());put(held_task,0x38,std::uint8_t{1});
        struct ExternalTaskStorage {
            void* task{};bool valid=true;unsigned acquired{},validated{},entered{},completed{};
        } storage{held_task.data()};
        Sc6ReplayTaskGroup::ConsumerAdmissionHooks hooks{};
        hooks.context=&storage;
        hooks.acquire=[](void* context,Sc6ReplayTaskGroup::ConsumerTask& candidate) noexcept {
            auto& s=*static_cast<ExternalTaskStorage*>(context);++s.acquired;
            if(candidate.task!=s.task)return Sc6ReplayTaskGroup::ConsumerAdmission::Reject;
            candidate.owner=reinterpret_cast<std::uintptr_t>(candidate.function);
            candidate.owner_index=3;candidate.owner_generation=1;
            candidate.application_epoch=99;candidate.contract=123;
            return Sc6ReplayTaskGroup::ConsumerAdmission::Hold;
        };
        hooks.validate=[](void* context,const Sc6ReplayTaskGroup::ConsumerTask& candidate) noexcept {
            auto& s=*static_cast<ExternalTaskStorage*>(context);++s.validated;
            return s.valid && candidate.task==s.task;
        };
        hooks.begin_execution=[](void* context,const Sc6ReplayTaskGroup::ConsumerTask& candidate) noexcept {
            auto& s=*static_cast<ExternalTaskStorage*>(context);++s.entered;
            return s.valid && candidate.task==s.task;
        };
        hooks.completed=[](void* context,const Sc6ReplayTaskGroup::ConsumerTask& candidate) noexcept {
            auto& s=*static_cast<ExternalTaskStorage*>(context);
            // Saved identity only; the shipped wrapper has already recycled it.
            if(candidate.task==s.task)++s.completed;
        };
        Sc6ReplayTaskGroup retained_group;
        const auto untouched_task=held_task,untouched_tick=held_tick,untouched_event=held_event;
        const auto untouched_pool=pool;
        component_task_calls=0;component_task_expected_task=held_task.data();
        component_task_expected_event=held_task.data()+0x40;
        expect(ReplayTaskGroupTestAccess::Retain(retained_group,game,held_task.data(),named_thread.data(),hooks)
            ==Sc6ReplayTaskGroup::DispatchOutcome::ConsumerHeld,"production dispatcher retains actual untouched native task");
        for(unsigned poll=0;poll<8;++poll) {
            expect(ReplayTaskGroupTestAccess::PollRetained(retained_group)==Sc6ReplayTaskGroup::DispatchOutcome::ConsumerHeld,
                "repeated retained polls do not enter native task");
            expect(held_task==untouched_task && held_tick==untouched_tick && held_event==untouched_event && pool==untouched_pool,
                "retained native task bytes/event refs/tick/TLS pool unchanged across polls");
        }
        storage.valid=false;
        expect(!retained_group.RequestConsumerResume(),"external lifetime service can reject retained resume");
        expect(ReplayTaskGroupTestAccess::PollRetained(retained_group)==Sc6ReplayTaskGroup::DispatchOutcome::ConsumerHeld
            && held_task==untouched_task && held_event==untouched_event && pool==untouched_pool
            && component_task_calls==0 && storage.entered==0 && storage.completed==0,
            "failed resume leaves actual native completion and recycling untouched");
        storage.valid=true;
        expect(retained_group.RequestConsumerResume(),"request retained native task resume");
        expect(ReplayTaskGroupTestAccess::PollRetained(retained_group)==Sc6ReplayTaskGroup::DispatchOutcome::Completed,
            "production retained path completes shipped graph wrapper");
        expect(component_task_calls==1 && storage.acquired==1 && storage.entered==1 && storage.completed==1
            && ReplayTaskGroupTestAccess::Dispatched(retained_group)==1,
            "retained native callback and production completion occur exactly once");
        expect((*reinterpret_cast<std::uint64_t*>(held_event.data()+8)&(1ull<<26))!=0
            && *reinterpret_cast<int*>(held_event.data()+0x48)==6,
            "resumed native wrapper closes event and releases exactly one reference");
        expect(*reinterpret_cast<void**>(pool.data()+8)==held_task.data()
            && *reinterpret_cast<int*>(pool.data()+0x10)==2
            && *reinterpret_cast<void**>(held_task.data())==task.data(),
            "resumed task enters actual TLS free list once preserving prior head");
        expect(!*reinterpret_cast<void**>(held_tick.data()+0x18) && held_task[0x38]==std::byte{}
            && !retained_group.held_consumer_task(),"resumed native pending state and retained owner clear");
        const auto finished_task=held_task,finished_event=held_event;const auto finished_pool=pool;
        expect(!retained_group.RequestConsumerResume() && held_task==finished_task && held_event==finished_event
            && pool==finished_pool && component_task_calls==1,
            "second resume request cannot repeat native event release or pool recycling");
        std::cout<<"Shipped retained component task/event/TLS lifecycle PASS; external storage service, no UObject lifetime/admission proof\n";

        // A separate task starts with a genuine native constructor/event
        // transfer. The generic component binding now names Niagara. Reject
        // before the wrapper can change tick time, signal, release or recycle.
        alignas(16) std::array<std::byte,0x58> rejected{},rejected_event{};
        alignas(16) std::array<std::byte,0x1d0> component{};
        alignas(16) std::array<std::byte,0x358> component_table{};
        put(rejected_event,0x48,7);reference=rejected_event.data();
        reinterpret_cast<void*(*)(void*,void**,int)>(game+0x2156650)(rejected.data(),&reference,3);
        put(component,0,component_table.data());put(component_table,0x300,game+0x1bcdb90);
        put(tick,0,game+0x3865f98);put(tick,0x50,component.data());put(tick,0x18,rejected.data());put(tick,0x3c,123.0f);
        put(rejected,0x10,tick.data());put(rejected,0x28,world.data());put(rejected,0x38,std::uint8_t{1});
        const auto task_before=rejected,event_before=rejected_event,tick_before=tick;
        const auto pool_before=pool;
        expect(ReplayTaskGroupTestAccess::Dispatch(game,rejected.data(),named_thread.data())
            ==Sc6ReplayTaskGroup::DispatchOutcome::TerminalFailure,"production dispatcher rejects Niagara before shipped wrapper");
        expect(rejected==task_before && rejected_event==event_before && tick==tick_before && pool==pool_before
            && component_task_calls==1,"rejected native task has no payload, completion, reference or pool side effect");
        // The no-render-worker surface route executes this same shipped
        // captured-callback wrapper synchronously, with the actual GT scratch
        // shape. Only external storage/TLS is supplied by this fixture.
        constexpr unsigned char callback_ctor[]{0x40,0x53,0x48,0x83,0xec,0x20,0xc7,0x41,0x8,0xff,0,0,0,0x48,0x8d,0x5};
        constexpr unsigned char callback_wrapper[]{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57};
        expect(!std::memcmp(reinterpret_cast<void*>(game+0x397f70),callback_ctor,sizeof(callback_ctor))
            && !std::memcmp(reinterpret_cast<void*>(game+0x39b210),callback_wrapper,sizeof(callback_wrapper)),
            "shipped captured callback constructor/wrapper signatures");
        alignas(16) std::array<std::byte,0x100> surface_task{};
        alignas(16) std::array<std::byte,0x58> surface_event{};
        struct SurfaceContext {void* event;unsigned calls;DWORD thread;} surface_context{surface_event.data(),0,GetCurrentThreadId()};
        put(surface_event,0x48,7);reference=surface_event.data();
        reinterpret_cast<void*(*)(void*,void**,int)>(game+0x397f70)(surface_task.data(),&reference,0);
        expect(!reference && *reinterpret_cast<void**>(surface_task.data()+0x28)==surface_event.data(),
            "surface native constructor transfers completion ownership");
        const auto callback=+[](void* opaque) {
            auto& context=*static_cast<SurfaceContext*>(opaque);++context.calls;
            expect(GetCurrentThreadId()==context.thread,"inline surface callback stays on original GT");
            expect(!(*reinterpret_cast<std::uint64_t*>(static_cast<std::byte*>(context.event)+8)&(1ull<<26)),
                "surface callback precedes actual native event completion");
        };
        put(surface_task,0x10,callback);put(surface_task,0x18,&surface_context);
        put(surface_task,0x20,std::uint8_t{1});put(surface_task,8,2u);
        expect(InterlockedDecrement(reinterpret_cast<LONG*>(surface_task.data()+0xc))==0,
            "surface native constructor has exactly one submission prerequisite");
        expect(ReplayTaskGroupTestAccess::Dispatch(game,surface_task.data(),named_thread.data())
            ==Sc6ReplayTaskGroup::DispatchOutcome::Completed,"native inline function task returns normally");
        expect(surface_context.calls==1 && (*reinterpret_cast<std::uint64_t*>(surface_event.data()+8)&(1ull<<26))
            && *reinterpret_cast<int*>(surface_event.data()+0x48)==6,
            "native inline surface executes once, completes event and releases one reference");
        expect(*reinterpret_cast<void**>(pool.data()+8)==surface_task.data()
            && *reinterpret_cast<int*>(pool.data()+0x10)==3,
            "native inline surface recycles task once through original TLS pool");
        std::cout<<"Shipped synchronous surface callback/event/TLS lifecycle PASS; GPU completion remains separate\n";
        NativePrerequisiteTest::Run(game,named_thread.data(),pool.data());
        // Stack storage and fixture references are test-owned. This does not
        // establish a production lifetime lease or a resumable hold.
    }
    TlsSetValue(slot,nullptr);*native_slot=prior_slot;*tls_import=prior_import;
    DWORD ignored{};expect(VirtualProtect(tls_import,sizeof(*tls_import),protection,&ignored)!=0,"restore mapped TLS import protection");
    expect(TlsFree(slot)!=0,"release owned graph pool TLS slot");
    std::cout<<"Shipped component task constructor/payload checks complete\n";
}

static void test_mapped_niagara_shared_rand(std::uintptr_t game)
{
    // Execute shipped VectorVM random opcode22 kernel141B11750. Only its
    // unresolved fixture import is bound, to the actual validated UCRT rand;
    // no random result, counter, native instruction or observation is mocked.
    constexpr unsigned char signature[]{0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x56,0x57,0x41,0x56};
    if(std::memcmp(reinterpret_cast<void*>(game+0x1b11750),signature,sizeof(signature))) {
        expect(false,"shipped Niagara random kernel signature");return;
    }
    const auto crt=LoadLibraryW(L"ucrtbase.dll");
    expect(crt!=nullptr,"load actual Niagara/combat CRT dependency");if(!crt)return;
    const auto random=reinterpret_cast<UcrtRandFn>(GetProcAddress(crt,"rand"));
    const auto seed=reinterpret_cast<UcrtSrandFn>(GetProcAddress(crt,"srand"));
    UcrtRandBroker observer;
    const bool bound=observer.Start().ok() && observer.BindNative(random,seed).ok();
    expect(bound,"validate shared native CRT/PTD through production broker");
    if(!bound){FreeLibrary(crt);return;}
    std::uint32_t saved_state{};
    if(!observer.ObserveNative(GetCurrentThreadId(),saved_state).ok()) {
        expect(false,"read preceding actual CRT state");FreeLibrary(crt);return;
    }
    auto* import=reinterpret_cast<std::uintptr_t*>(game+Schema::Sc6UcrtLayout::rand_iat_rva);
    DWORD protection{};
    if(!VirtualProtect(import,sizeof(*import),PAGE_READWRITE,&protection)) {
        expect(false,"make mapped-fixture rand import writable");FreeLibrary(crt);return;
    }
    const auto preceding=*import;*import=reinterpret_cast<std::uintptr_t>(random);
    const auto execute=reinterpret_cast<void(*)(std::uintptr_t*)>(game+0x1b11750);
    const auto scale=*reinterpret_cast<const float*>(game+0x325bcb4);
    std::uint32_t state4{},state5{};
    for(unsigned mode:{0u,1u})for(unsigned lanes:{0u,1u,4u,5u,8u,9u}) {
        constexpr unsigned initial_seed=0x11750;
        const unsigned draws=4*((lanes+3)/4);
        std::array<int,12> control{};
        seed(initial_seed);
        for(unsigned i=0;i<draws;++i)control[i]=random();
        std::uint32_t expected_state{};
        expect(observer.ObserveNative(GetCurrentThreadId(),expected_state).ok(),"observe independent CRT control state");
        const int next_control=random();
        alignas(16) std::array<float,16> input{},output{};
        for(unsigned i=0;i<input.size();++i){input[i]=float(i+2);output[i]=-123.0f;}
        const float constant=3.0f;
        std::array<unsigned char,5> operands{static_cast<unsigned char>(mode),0,0,0,1};
        std::array<void*,2> registers{input.data(),output.data()};
        std::array<std::uintptr_t,8> context{};
        context[0]=reinterpret_cast<std::uintptr_t>(operands.data());
        context[1]=reinterpret_cast<std::uintptr_t>(registers.data());
        context[2]=reinterpret_cast<std::uintptr_t>(&constant);context[7]=lanes;
        seed(initial_seed);execute(context.data());
        std::uint32_t observed{};
        expect(observer.ObserveNative(GetCurrentThreadId(),observed).ok() && observed==expected_state,
            "Niagara consumes four actual shared draws per rounded vector batch");
        expect(random()==next_control,"next combat-thread CRT draw follows Niagara's actual draws");
        expect(context[0]==reinterpret_cast<std::uintptr_t>(operands.data()+operands.size()),
            "native random kernel consumes its actual operand encoding");
        for(unsigned i=0;i<output.size();++i) {
            const auto expected=i<draws?float(control[(i&~3u)+3-(i&3u)])*scale*(mode?constant:input[i]):-123.0f;
            expect(output[i]==expected,"native random lane ordering and rounded output extent");
        }
        if(!mode && lanes==4)state4=observed;
        if(!mode && lanes==5)state5=observed;
        std::cout<<"Niagara shared RNG mode="<<mode<<" lanes="<<lanes<<" draws="<<draws<<" state="<<observed<<"\n";
    }
    expect(state4!=state5,"changed Niagara population crosses shared RNG draw boundary");
    // Restore both fixture import and real thread state. No game was launched.
    *import=preceding;DWORD ignored{};
    expect(VirtualProtect(import,sizeof(*import),protection,&ignored)!=0,"restore mapped fixture import protection");
    seed(saved_state);observer.Stop();FreeLibrary(crt);
}
#endif

int main(int argc,char** argv)
{
    if(argc==2 && std::string_view(argv[1])=="--contact-latches") {
        test_contact_side_latch_recovery();
        return failures?1:0;
    }
    test_framework_flag_conversion();
    test_scheduler_reconstructed_hashes();
    test_particle_lighting_key_projection();
#ifdef _WIN32
    if(argc==4 && std::string_view(argv[1])=="--physics-markers") {
        const auto particle_game=LoadLibraryExA(argv[3],nullptr,DONT_RESOLVE_DLL_REFERENCES);
        expect(particle_game!=nullptr,"map verified shipped particle duration consumer");
        if(particle_game){test_mapped_component_task_payload(reinterpret_cast<std::uintptr_t>(particle_game));test_mapped_niagara_shared_rand(reinterpret_cast<std::uintptr_t>(particle_game));test_private_particle_material_retirement(reinterpret_cast<std::uintptr_t>(particle_game));test_mapped_particle_receiver_dispatch(reinterpret_cast<std::uintptr_t>(particle_game));test_mapped_gpu_descriptor_route(reinterpret_cast<std::uintptr_t>(particle_game));test_mapped_empty_gpu_render_owner(reinterpret_cast<std::uintptr_t>(particle_game));test_scheduler_reconstructed_hashes(reinterpret_cast<std::uintptr_t>(particle_game));test_particle_lighting_key_projection(reinterpret_cast<std::uintptr_t>(particle_game));FreeLibrary(particle_game);}
        if(failures)return 1;
        test_native_physics_markers(argv[2],argv[3]);
        test_native_physics_island_retirement(argv[2],argv[3]);
        test_native_physics_island_retirement(argv[2],argv[3],true);
        if(!failures)std::cout<<"Native physics marker transaction passed\n";
        return failures?1:0;
    }
    TestPhysicsPublicationLists();
    test_vfx_handler_nested_slot_storage();
    test_ui_transaction_admission();
    test_native_emitter_destruction_transaction();
    test_vfx_handler_execution_graph_ownership();
    test_emitter_cross_owner_storage_exclusion();
    test_private_particle_material_retirement();
    test_seeded_collision_payload_admission();
    test_gpu_descriptor_owner_binding();
    test_emitter_component_replacement();
    test_static_vector_field_publication();
    test_spawn_per_unit_payload_admission();
    test_gpu_spawn_payload_ownership();
    test_mesh_emitter_storage_contract();
    test_stage_visibility_source_transaction();
    test_stage_render_target_deferral();
    test_registry_execution_backing_transaction();
    RenderReopenTest::Run();
    PublicationTailTest::Run();
    SurfacePublicationTest::Run();
    StartupLoadingTest::Run();
    SurfaceImageTransactionTest::Run();
    ReplayExecutorTailTest::Run();
    ReplayShutdownTest::Run();
    ReplayShutdownIatTest::Run();
    VisibilityReleaseTest::Run();
#endif
    test_sparse_particle_batch_removal();
    const auto codec_before = CandidateCheckpointCodec::ThreadScratchBytes();
    expect(CandidateCheckpointCodec::PrepareThreadScratchStorage(0).code == FailureCode::CapacityExceeded
        && CandidateCheckpointCodec::ThreadScratchBytes() == codec_before,
        "codec capacity rejection does not allocate persistent thread scratch");
#ifdef _WIN32
    test_replay_output_window_message_ownership();
    test_gpu_copy_completion_and_retirement();
    test_physics_projection_public_admission();
    test_complete_suppressed_marker_graph();
    test_physics_order_transaction();
#endif
    test_hud_native_owner_route();
    test_hgcpu_stream_contract();
    test_hgcpu_empty_stat_owner();
    test_candidate_checkpoint_codec();
    test_correction_restores_complete_pre_tick_input_producer_boundary();
    test_motion_bank_snapshot_is_bounded_and_transactional();
    test_motion_solver_values_keep_links_and_undo();
    test_motion_propagated_collider_links_and_undo();
    test_secondary_event_state_is_pointer_free_and_transactional();
    test_chara_animation_state_normalizes_sections_and_undoes_exactly();
    test_scheduler_activation_preserves_dormant_binding_and_undo();
    test_chara_animation_palette_bank_alias();
    test_battle_audio_selector_is_generation_bound_and_transactional();
    test_candidate_adapter_restore_and_outer_undo();
    test_full_binding_exit_releases_transaction_scratch_storage();
    test_candidate_adapter_native_failure_undoes_hgcpu();
    test_hgcpu_direct_source_coverage();
    test_capture_restore_preserves_exclusions();
    test_contact_side_latch_recovery();
    test_preflight_is_atomic();
    test_unknown_class_and_invalid_header_fail_closed();
    test_camera_component_class_is_mutable_state();
    test_mt_complete_image_and_binding();
    test_lfsr_refill_sentinel_is_bounded();
    test_partial_write_undoes_exactly();
    test_move_dispatch_action_phase_restore();
    test_tutorial_consumer_interval_and_undo();
    test_move_dispatch_phase_drift_is_atomic();
    test_move_dispatch_pending_phase_restore();
    test_move_dispatch_partial_write_undoes_exactly();
    test_stage_break_listener_topology_is_value_only_and_bounded();
    test_stage_break_presentation_identity_is_generation_scoped();
    test_stage_break_particle_asset_capture_is_bounded_and_atomic();
    test_callback_topology_is_generation_bound_and_pointer_free();
    test_stage_wind_topology_is_bounded_and_pointer_free();
    test_stage_wind_graph_restore_is_transactional();
    test_presentation_journal_is_bounded_and_retry_safe();
    if (failures == 0)
        std::cout << "NativeCandidateRegionsSelfTest passed\n";
    return failures == 0 ? 0 : 1;
}
