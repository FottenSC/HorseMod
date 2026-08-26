#include "Sc6CandidateCheckpointCapture.hpp"

#include "DeterministicHookSet.hpp"

#include <chrono>

#include "Schema.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/UObject.hpp>

#include <algorithm>
#include <cstring>
#include <span>

namespace Horse::Deterministic
{
namespace
{
std::uintptr_t resolve_observed_battle_audio_handler(
    void*, std::size_t index) noexcept
{
    return DeterministicHookSet::ObservedBattleAudioHandler(index);
}
bool battle_audio_handler_overflowed(void*) noexcept
{
    return DeterministicHookSet::BattleAudioHandlerOverflowed();
}
constexpr std::uintptr_t fighter_roots_rva = 0x470DE90;
constexpr std::uintptr_t effect_camera_pointer_rva = 0x470DEE8;
constexpr std::uintptr_t camera_director_state_rva = 0x470E9F0;
constexpr std::uintptr_t camera_director_vtable_rva = 0x3E85568;
constexpr std::uintptr_t camera_interface_vtable_rva = 0x3E87A58;
constexpr std::size_t hgcpu_camera_state_size = 0x360;
constexpr std::uintptr_t camera_action_list_rva = 0x470EE90;
constexpr std::uintptr_t camera_action_owner_rva = 0x470ED50;
constexpr std::size_t camera_action_count = 17;
constexpr std::size_t camera_action_stride = 0x3E0;
constexpr std::size_t camera_action_backing_size =
    camera_action_count * camera_action_stride;
constexpr std::uintptr_t move_dispatch_filter_rva = 0x427940;
constexpr std::uintptr_t adjusted_weak_callback_vtable_rva = 0x3285198;
constexpr std::uintptr_t hgcpu_writer_rva = 0x3841E0;
constexpr std::uintptr_t hgcpu_reader_rva = 0x384540;
constexpr std::uintptr_t pump_state_rva = 0x4100C70;
constexpr std::uintptr_t scheduler_base_rva = 0x4715400;
constexpr std::uintptr_t move_command_base_rva = 0x470F390;
constexpr std::uintptr_t slot_param_base_rva = 0x470E0C0;
constexpr std::uintptr_t lcg_rng_rva = 0x485EB28;
constexpr std::uintptr_t lfsr_rng_rva = 0x485EB30;
constexpr std::uintptr_t xorshift_rng_rva = 0x470E2C8;
constexpr std::uintptr_t wind_rng_rva = 0x470E2B0;
constexpr std::uintptr_t vm_freeze_record_rva = 0x48462D0;
constexpr std::uintptr_t stage_wind_emitter_list_rva = 0x470F1C0;
constexpr std::uintptr_t pending_hit_record_rva = 0x485E738;
constexpr std::uintptr_t pending_launcher_sync_rva = 0x470F38D;
constexpr std::uintptr_t wind_root_pointer_rva = 0x470E038;
constexpr std::uintptr_t fmemory_malloc_rva = 0x4A61C0;
constexpr std::uintptr_t fmemory_free_rva = 0x1F90000;
constexpr std::ptrdiff_t input_filter_collection = 0x1210;
constexpr std::size_t callback_entry_size = 0x40;
constexpr std::size_t maximum_callback_entries = 64;
constexpr std::array<std::ptrdiff_t, 5> callback_collection_offsets{
    0x1210, 0x8E0, 0xA30, 0xB80, 0xF70};

struct WeakCallbackPrefix
{
    std::uintptr_t vtable{};
    std::int32_t object_index{};
    std::int32_t serial_number{};
    std::uintptr_t callback{};
};
static_assert(sizeof(WeakCallbackPrefix) == 0x18);

RC::Unreal::UObject* resolve_weak_object(
    std::int32_t object_index, std::int32_t serial_number) noexcept
{
    RC::Unreal::FWeakObjectPtr weak;
    weak.ObjectIndex = object_index;
    weak.ObjectSerialNumber = serial_number;
    return weak.Get();
}

Status resolve_callback_owner_class(
    void*, std::int32_t object_index, std::int32_t serial_number,
    std::uint64_t& class_token) noexcept
{
    class_token = 0;
    auto* object = resolve_weak_object(object_index, serial_number);
    if (object == nullptr || object->GetClassPrivate() == nullptr)
        return Status::failure(FailureCode::IdentityMismatch);
    try
    {
        const auto name = object->GetClassPrivate()->GetName();
        std::uint64_t hash = 1469598103934665603ull;
        for (const auto character : name)
        {
            const auto value = static_cast<std::uint32_t>(character);
            for (std::size_t byte = 0; byte < sizeof(value); ++byte)
            {
                hash ^= static_cast<std::uint8_t>(value >> (byte * 8));
                hash *= 1099511628211ull;
            }
        }
        class_token = hash;
        return name.empty() || class_token == 0
            ? Status::failure(FailureCode::IdentityMismatch)
            : Status::success();
    }
    catch (...)
    {
        return Status::failure(FailureCode::CaptureFailed);
    }
}
}

class Sc6CandidateCheckpointCapture::ProcessMemory final : public INativeMemory
{
public:
    bool Read(std::uintptr_t address, std::span<std::byte> destination) noexcept override
    {
        if (address == 0 || destination.empty()) return false;
        __try
        {
            std::memcpy(destination.data(), reinterpret_cast<const void*>(address),
                destination.size());
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool Write(
        std::uintptr_t address, std::span<const std::byte> source) noexcept override
    {
        if (address == 0 || source.empty()) return false;
        __try
        {
            std::memcpy(reinterpret_cast<void*>(address), source.data(), source.size());
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
};

class Sc6CandidateCheckpointCapture::ProcessStageWindAllocator final
    : public IStageWindAllocator
{
public:
    ProcessStageWindAllocator(
        std::uintptr_t image_base, std::uint32_t owner_thread_id) noexcept
        : image_base_(image_base), owner_thread_id_(owner_thread_id)
    {
    }

    std::uintptr_t Allocate(std::size_t size) noexcept override
    {
        if (GetCurrentThreadId() != owner_thread_id_
            || (size != 0x130 && size != 0x180 && size != 0x1E0))
            return 0;
        using MallocFn = void* (__fastcall*)(std::size_t);
        __try
        {
            return reinterpret_cast<std::uintptr_t>(
                reinterpret_cast<MallocFn>(image_base_ + fmemory_malloc_rva)(size));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    void Free(std::uintptr_t address) noexcept override
    {
        if (address == 0 || GetCurrentThreadId() != owner_thread_id_) return;
        using FreeFn = void (__fastcall*)(void*);
        __try
        {
            reinterpret_cast<FreeFn>(image_base_ + fmemory_free_rva)(
                reinterpret_cast<void*>(address));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

private:
    std::uintptr_t image_base_{};
    std::uint32_t owner_thread_id_{};
};

Sc6CandidateCheckpointCapture::Sc6CandidateCheckpointCapture()
    : memory_(std::make_unique<ProcessMemory>()),
      regions_(std::make_unique<NativeCandidateRegions>(*memory_)),
      battle_audio_selector_(
          std::make_unique<BattleAudioSelectorState>(*memory_)),
      motion_banks_(std::make_unique<MotionBankSnapshot>(*memory_)),
      move_dispatch_(std::make_unique<MoveDispatchState>(*memory_)),
      secondary_events_(std::make_unique<SecondaryEventState>(*memory_)),
      chara_animation_(std::make_unique<CharaAnimationState>(*memory_)),
      callback_probe_(std::make_unique<CallbackTopologyProbe>(*memory_)),
      wind_probe_(std::make_unique<StageWindTopologyProbe>(*memory_)),
      adapter_(std::make_unique<CandidateGameStateAdapter>(*regions_, hgcpu_))
{
}

Sc6CandidateCheckpointCapture::~Sc6CandidateCheckpointCapture() = default;

Status Sc6CandidateCheckpointCapture::Initialize(
    std::uintptr_t image_base, UcrtRandBroker* ucrt_broker) noexcept
{
    Reset();
    if (image_base == 0 || ucrt_broker == nullptr)
        return Status::failure(FailureCode::ContextUnavailable);
    __try
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image_base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            image_base + static_cast<std::uintptr_t>(dos->e_lfanew));
        if (dos->e_magic != IMAGE_DOS_SIGNATURE
            || nt->Signature != IMAGE_NT_SIGNATURE
            || nt->OptionalHeader.SizeOfImage == 0)
        {
            return Status::failure(FailureCode::ContextUnavailable);
        }
        image_size_ = nt->OptionalHeader.SizeOfImage;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return Status::failure(FailureCode::ContextUnavailable);
    }
    image_base_ = image_base;
    ucrt_broker_ = ucrt_broker;
    return Status::success();
}

bool Sc6CandidateCheckpointCapture::read_fighter_roots(
    std::array<std::uintptr_t, 2>& output) noexcept
{
    output = {};
    return memory_->Read(image_base_ + fighter_roots_rva,
               std::as_writable_bytes(std::span{output}))
        && output[0] != 0 && output[1] != 0 && output[0] != output[1];
}

Status Sc6CandidateCheckpointCapture::capture_camera_topology(
    CameraTopology& output) noexcept
{
    output = {};
    std::uintptr_t root{};
    if (!memory_->Read(image_base_ + effect_camera_pointer_rva,
            std::as_writable_bytes(std::span{&root, 1})))
    {
        return Status::failure(FailureCode::CapturePreflightFailed);
    }
    if (root == 0) return Status::success();

    std::array<std::byte, hgcpu_camera_state_size> readable{};
    std::array<std::uintptr_t, 2> vtables{};
    if (root != image_base_ + camera_director_state_rva
        || !memory_->Read(root, readable)
        || !memory_->Read(root,
            std::as_writable_bytes(std::span{vtables.data(), 1}))
        || !memory_->Read(root + 0x10,
            std::as_writable_bytes(std::span{vtables.data() + 1, 1}))
        || vtables[0] != image_base_ + camera_director_vtable_rva
        || vtables[1] != image_base_ + camera_interface_vtable_rva)
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }

    const std::uintptr_t list = image_base_ + camera_action_list_rva;
    std::uintptr_t owner{};
    std::uintptr_t backing{};
    std::array<std::uintptr_t, camera_action_count> slots{};
    std::byte backing_tail{};
    if (!memory_->Read(list,
            std::as_writable_bytes(std::span{&owner, 1}))
        || !memory_->Read(list + 0x08,
            std::as_writable_bytes(std::span{&backing, 1}))
        || !memory_->Read(list + 0x10,
            std::as_writable_bytes(std::span{slots}))
        || owner != image_base_ + camera_action_owner_rva || backing == 0
        || !memory_->Read(backing + camera_action_backing_size - 1,
            std::span<std::byte>{&backing_tail, 1}))
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }

    for (std::size_t index = 0; index < slots.size(); ++index)
    {
        const std::uintptr_t expected = backing + index * camera_action_stride;
        std::uint32_t slot_index{};
        std::uintptr_t action_owner{};
        std::uintptr_t action_list{};
        if (slots[index] != expected
            || !memory_->Read(expected,
                std::as_writable_bytes(
                    std::span{output.action_vtables.data() + index, 1}))
            || !memory_->Read(expected + 0x08,
                std::as_writable_bytes(std::span{&slot_index, 1}))
            || !memory_->Read(expected + 0x10,
                std::as_writable_bytes(std::span{&action_owner, 1}))
            || !memory_->Read(expected + 0x18,
                std::as_writable_bytes(std::span{&action_list, 1}))
            || !memory_->Read(expected + 0x20,
                std::as_writable_bytes(
                    std::span{output.action_types.data() + index, 1}))
            || slot_index != index || action_owner != owner || action_list != list
            || output.action_vtables[index] < image_base_
            || output.action_vtables[index] >= image_base_ + image_size_
            || (output.action_vtables[index] & 7) != 0
            || output.action_types[index] > 0x1A)
        {
            return Status::failure(FailureCode::IdentityMismatch);
        }
    }
    output.camera_root = root;
    output.action_backing = backing;
    return Status::success();
}

Status Sc6CandidateCheckpointCapture::capture_callback_topology(
    CallbackTopology& output) noexcept
{
    std::array<CallbackCollectionRef, callback_collection_offsets.size()> refs{};
    for (std::size_t index = 0; index < refs.size(); ++index)
    {
        refs[index] = {
            static_cast<CallbackCollectionRole>(index + 1),
            bound_manager_ + callback_collection_offsets[index],
        };
    }
    return callback_probe_->Capture(image_base_, image_size_, refs,
        &resolve_callback_owner_class, nullptr, output);
}

Status Sc6CandidateCheckpointCapture::resolve_move_dispatch(
    std::uintptr_t battle_manager, std::uintptr_t& output) noexcept
{
    output = 0;
    const std::uintptr_t collection = battle_manager + input_filter_collection;
    std::uintptr_t heap_entries{};
    std::int32_t count{};
    std::int32_t capacity{};
    if (!memory_->Read(collection + 0x40,
            std::as_writable_bytes(std::span{&heap_entries, 1}))
        || !memory_->Read(collection + 0x50,
            std::as_writable_bytes(std::span{&count, 1}))
        || !memory_->Read(collection + 0x54,
            std::as_writable_bytes(std::span{&capacity, 1}))
        || count < 1 || count > static_cast<std::int32_t>(maximum_callback_entries)
        || capacity < count || capacity > static_cast<std::int32_t>(maximum_callback_entries))
    {
        return Status::failure(FailureCode::AdapterUnqualified);
    }

    const std::uintptr_t entries = heap_entries != 0 ? heap_entries : collection;
    std::size_t matches{};
    for (std::int32_t index = 0; index < count; ++index)
    {
        const std::uintptr_t entry = entries + index * callback_entry_size;
        WeakCallbackPrefix callback{};
        std::uintptr_t heap_callback{};
        if (!memory_->Read(entry, std::as_writable_bytes(std::span{&callback, 1}))
            || !memory_->Read(entry + 0x20,
                std::as_writable_bytes(std::span{&heap_callback, 1})))
        {
            return Status::failure(FailureCode::CapturePreflightFailed);
        }
        if (heap_callback != 0
            && !memory_->Read(heap_callback,
                std::as_writable_bytes(std::span{&callback, 1})))
        {
            return Status::failure(FailureCode::CapturePreflightFailed);
        }
        if (callback.vtable != image_base_ + adjusted_weak_callback_vtable_rva
            || callback.callback != image_base_ + move_dispatch_filter_rva)
        {
            continue;
        }
        auto* target = resolve_weak_object(
            callback.object_index, callback.serial_number);
        if (target == nullptr) return Status::failure(FailureCode::IdentityMismatch);
        output = reinterpret_cast<std::uintptr_t>(target);
        ++matches;
    }
    return matches == 1
        ? Status::success()
        : Status::failure(FailureCode::AdapterUnqualified);
}

Status Sc6CandidateCheckpointCapture::bind(
    std::uintptr_t battle_manager,
    FrameCoordinate coordinate,
    std::uint64_t session_generation,
    std::uint32_t simulation_thread_id) noexcept
{
    std::uintptr_t move_dispatch{};
    std::array<std::uintptr_t, 2> fighter_roots{};
    CameraTopology camera_topology{};
    const Status resolved = resolve_move_dispatch(
        battle_manager, move_dispatch);
    if (!resolved.ok()) return resolved;
    if (!read_fighter_roots(fighter_roots))
        return Status::failure(FailureCode::ContextUnavailable);
    const Status camera_status = capture_camera_topology(camera_topology);
    if (!camera_status.ok()) return camera_status;
    NativeCandidateAddresses addresses{
        image_base_,
        battle_manager,
        0,
        image_base_ + Schema::Sc6FrameLayout::frame_counter_rva,
        move_dispatch,
        image_base_ + pump_state_rva,
        image_base_ + scheduler_base_rva,
        image_base_ + move_command_base_rva,
        image_base_ + slot_param_base_rva,
        image_base_ + lcg_rng_rva,
        image_base_ + lfsr_rng_rva,
        image_base_ + xorshift_rng_rva,
        image_base_ + wind_rng_rva,
        image_base_ + vm_freeze_record_rva,
        image_base_ + stage_wind_emitter_list_rva,
        image_base_ + pending_hit_record_rva,
        image_base_ + pending_launcher_sync_rva,
        camera_topology.camera_root,
        camera_topology.action_backing,
        fighter_roots,
        session_generation,
        coordinate.generation,
    };
    if (!memory_->Read(
            battle_manager + Schema::Sc6FrameLayout::manager_input_log,
            std::as_writable_bytes(std::span{&addresses.input_log, 1}))
        || addresses.input_log == 0)
    {
        return Status::failure(FailureCode::ContextUnavailable);
    }
    const Status bound = regions_->Bind(addresses);
    if (!bound.ok()) return bound;
    const NativeContext context{
        coordinate.generation,
        session_generation,
        {coordinate.generation * 2 - 1, coordinate.generation * 2},
        coordinate.generation,
    };
    const StageWindTopologyAddresses wind_addresses{
        image_base_, image_size_, image_base_ + wind_root_pointer_rva,
        coordinate.generation};
    const Status wind_status = wind_probe_->Bind(wind_addresses);
    if (!wind_status.ok())
    {
        regions_->Invalidate();
        return wind_status;
    }
    try
    {
        wind_allocator_ = std::make_unique<ProcessStageWindAllocator>(
            image_base_, simulation_thread_id);
        wind_transaction_ = std::make_unique<StageWindGraphTransaction>(
            *memory_, *wind_allocator_);
    }
    catch (...)
    {
        ReleaseBinding();
        return Status::failure(FailureCode::CapacityExceeded);
    }
    CandidateAdapterBinding adapter_binding{};
    adapter_binding.context = context;
    adapter_binding.hgcpu_context = {
        0xF8904E4B04BCA3B4ull,
        Schema::snapshot_schema_version,
        session_generation,
        coordinate.generation,
        {context.fighter_identities[0], context.fighter_identities[1]},
        context.stage_identity,
        static_cast<std::uint64_t>(camera_topology.camera_root),
        coordinate.generation,
    };
    adapter_binding.hgcpu_writer = reinterpret_cast<HgCpuExecFn>(
        image_base_ + hgcpu_writer_rva);
    adapter_binding.hgcpu_reader = reinterpret_cast<HgCpuExecFn>(
        image_base_ + hgcpu_reader_rva);
    adapter_binding.battle_audio_selector = battle_audio_selector_.get();
    adapter_binding.motion_banks = motion_banks_.get();
    adapter_binding.move_dispatch = move_dispatch_.get();
    adapter_binding.secondary_events = secondary_events_.get();
    adapter_binding.chara_animation = chara_animation_.get();
    adapter_binding.ucrt_broker = ucrt_broker_;
    adapter_binding.wind_probe = wind_probe_.get();
    adapter_binding.wind_transaction = wind_transaction_.get();
    adapter_binding.wind_addresses = wind_addresses;
    adapter_binding.simulation_thread_id = simulation_thread_id;
    const BattleAudioSelectorBinding audio_selector_binding{
        image_base_, image_size_, adapter_binding.hgcpu_context,
        &resolve_observed_battle_audio_handler,
        &battle_audio_handler_overflowed, nullptr};
    Status adapter_status = battle_audio_selector_->Bind(
        audio_selector_binding);
    if (adapter_status.ok()) adapter_status = motion_banks_->Bind(
        fighter_roots, adapter_binding.hgcpu_context);
    if (adapter_status.ok()) adapter_status = move_dispatch_->Bind(
        move_dispatch, coordinate.generation);
    if (adapter_status.ok()) adapter_status = secondary_events_->Bind(
        fighter_roots, coordinate.generation);
    if (adapter_status.ok()) adapter_status = chara_animation_->Bind(
        fighter_roots, coordinate.generation);
    if (adapter_status.ok()) adapter_status = adapter_->Configure(adapter_binding);
    if (adapter_status.ok()) adapter_status = adapter_->BindContext(context);
    if (!adapter_status.ok())
    {
        ReleaseBinding();
        return adapter_status;
    }
    bound_manager_ = battle_manager;
    bound_move_dispatch_ = move_dispatch;
    bound_session_generation_ = session_generation;
    bound_round_generation_ = coordinate.generation;
    bound_camera_topology_ = camera_topology;
    CallbackTopology topology{};
    const Status callback_status = capture_callback_topology(topology);
    if (!callback_status.ok())
    {
        ReleaseBinding();
        return callback_status;
    }
    bound_callback_topology_ = std::move(topology);
    return Status::success();
}

Status Sc6CandidateCheckpointCapture::Capture(
    CandidateCheckpointRole role,
    std::uintptr_t battle_manager,
    FrameCoordinate coordinate,
    std::uint64_t session_generation,
    std::uint32_t simulation_thread_id) noexcept
{
    if (image_base_ == 0 || battle_manager == 0
        || coordinate.generation == 0 || session_generation == 0
        || simulation_thread_id == 0)
    {
        return Status::failure(FailureCode::ContextUnavailable);
    }
    CandidateCheckpointCaptureStatus& capture_status = role
            == CandidateCheckpointRole::Landing
        ? landing_status_ : batch_entry_status_;
    capture_status.wind_node_count = 0;
    SnapshotStore& snapshots = role == CandidateCheckpointRole::Landing
        ? landing_snapshots_ : batch_entry_snapshots_;
    if (!regions_->IsBound() || bound_manager_ != battle_manager
        || bound_session_generation_ != session_generation
        || bound_round_generation_ != coordinate.generation)
    {
        const Status rebound = bind(
            battle_manager, coordinate, session_generation,
            simulation_thread_id);
        if (!rebound.ok())
        {
            capture_status.failure = rebound.code;
            capture_status.validation = regions_->validation_diagnostic();
            capture_status.animation_topology_issue =
                chara_animation_->topology_issue();
            capture_status.animation_topology_observed =
                chara_animation_->topology_observed();
            capture_status.animation_fighters = chara_animation_->fighters();
            capture_status.capture_phase = adapter_->last_capture_phase();
            return rebound;
        }
    }

    CameraTopology camera_topology{};
    const Status camera_status = capture_camera_topology(camera_topology);
    if (!camera_status.ok() || camera_topology != bound_camera_topology_)
    {
        const auto failure = camera_status.ok()
            ? FailureCode::IdentityMismatch : camera_status.code;
        ReleaseBinding();
        capture_status.failure = failure;
        return Status::failure(failure);
    }

    CallbackTopology topology{};
    const Status callback_status = capture_callback_topology(topology);
    if (!callback_status.ok() || topology != bound_callback_topology_)
    {
        const auto failure = callback_status.ok()
            ? FailureCode::IdentityMismatch : callback_status.code;
        ReleaseBinding();
        capture_status.failure = failure;
        return Status::failure(failure);
    }

    StageWindTopologyImage wind_topology{};
    const Status wind_status = wind_probe_->Capture(wind_topology);
    if (!wind_status.ok())
    {
        ReleaseBinding();
        capture_status.failure = wind_status.code;
        return wind_status;
    }

    Snapshot& snapshot = role == CandidateCheckpointRole::Landing
        ? landing_capture_scratch_ : batch_entry_capture_scratch_;
    TimingHistogram& capture_timing = role == CandidateCheckpointRole::Landing
        ? landing_capture_timing_ : batch_entry_capture_timing_;
    TimingHistogram& store_timing = role == CandidateCheckpointRole::Landing
        ? landing_store_timing_ : batch_entry_store_timing_;
    const auto capture_begin = std::chrono::steady_clock::now();
    Status captured = adapter_->Capture(coordinate, snapshot);
    const auto capture_end = std::chrono::steady_clock::now();
    capture_timing.Record(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            capture_end - capture_begin).count()));
    if (captured.ok())
    {
        const auto store_begin = std::chrono::steady_clock::now();
        // Keep the role-local encode capacity warm; SnapshotStore owns its
        // bounded copy and reports that cost separately from native capture.
        captured = snapshots.Save(snapshot);
        const auto store_end = std::chrono::steady_clock::now();
        store_timing.Record(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                store_end - store_begin).count()));
    }
    capture_status.capture_samples = capture_timing.samples;
    capture_status.capture_total_ns = capture_timing.total_ns;
    capture_status.capture_max_ns = capture_timing.maximum_ns;
    capture_status.capture_p99_ns = capture_timing.Percentile99();
    capture_status.store_max_ns = store_timing.maximum_ns;
    capture_status.store_p99_ns = store_timing.Percentile99();
    capture_status.adapter_performance = adapter_->performance_status();
    if (!captured.ok())
    {
        capture_status.failure = captured.code;
        capture_status.validation = regions_->validation_diagnostic();
        capture_status.animation_topology_issue =
            chara_animation_->topology_issue();
        capture_status.animation_topology_observed =
            chara_animation_->topology_observed();
        capture_status.animation_fighters = chara_animation_->fighters();
        capture_status.capture_phase = adapter_->last_capture_phase();
        return captured;
    }
    capture_status.failure = FailureCode::None;
    capture_status.validation = {};
    capture_status.animation_topology_issue = {};
    capture_status.animation_topology_observed = 0;
    capture_status.animation_fighters = {};
    capture_status.capture_phase = {};
    capture_status.last_coordinate = coordinate;
    ++capture_status.captured;
    capture_status.bytes_used = snapshots.BytesUsed();
    capture_status.wind_node_count = wind_topology.nodes.size();
    return Status::success();
}

Status Sc6CandidateCheckpointCapture::CaptureTransient(
    FrameCoordinate coordinate, Snapshot& output) noexcept
{
    transient_identity_issue_ = 0;
    transient_identity_expected_ = 0;
    transient_identity_observed_ = 0;
    if (!regions_->IsBound() || bound_manager_ == 0
        || coordinate.generation != bound_round_generation_)
    {
        output = {};
        return Status::failure(FailureCode::GenerationMismatch);
    }
    CameraTopology camera_topology{};
    const Status camera = capture_camera_topology(camera_topology);
    if (!camera.ok() || camera_topology != bound_camera_topology_)
    {
        transient_identity_issue_ = 1;
        transient_identity_expected_ = bound_camera_topology_.camera_root;
        transient_identity_observed_ = camera_topology.camera_root;
        return Status::failure(camera.ok()
            ? FailureCode::IdentityMismatch : camera.code);
    }
    CallbackTopology callback_topology{};
    const Status callbacks = capture_callback_topology(callback_topology);
    if (!callbacks.ok() || callback_topology != bound_callback_topology_)
    {
        transient_identity_issue_ = 2;
        transient_identity_expected_ = bound_callback_topology_.signature;
        transient_identity_observed_ = callback_topology.signature;
        return Status::failure(callbacks.ok()
            ? FailureCode::IdentityMismatch : callbacks.code);
    }
    const Status captured = adapter_->Capture(coordinate, output);
    if (captured.code == FailureCode::IdentityMismatch)
        transient_identity_issue_ = 3;
    return captured;
}

Status Sc6CandidateCheckpointCapture::CaptureCanonical(
    FrameCoordinate coordinate, Snapshot& output) noexcept
{
    transient_identity_issue_ = 0;
    transient_identity_expected_ = 0;
    transient_identity_observed_ = 0;
    if (!regions_->IsBound() || bound_manager_ == 0
        || coordinate.generation != bound_round_generation_)
    {
        output = {};
        return Status::failure(FailureCode::GenerationMismatch);
    }
    CameraTopology camera_topology{};
    const Status camera = capture_camera_topology(camera_topology);
    if (!camera.ok() || camera_topology != bound_camera_topology_)
    {
        transient_identity_issue_ = 1;
        transient_identity_expected_ = bound_camera_topology_.camera_root;
        transient_identity_observed_ = camera_topology.camera_root;
        return Status::failure(camera.ok()
            ? FailureCode::IdentityMismatch : camera.code);
    }
    CallbackTopology callback_topology{};
    const Status callbacks = capture_callback_topology(callback_topology);
    if (!callbacks.ok() || callback_topology != bound_callback_topology_)
    {
        transient_identity_issue_ = 2;
        transient_identity_expected_ = bound_callback_topology_.signature;
        transient_identity_observed_ = callback_topology.signature;
        return Status::failure(callbacks.ok()
            ? FailureCode::IdentityMismatch : callbacks.code);
    }
    const Status captured = adapter_->CaptureCanonical(coordinate, output);
    if (captured.code == FailureCode::IdentityMismatch)
        transient_identity_issue_ = 3;
    return captured;
}

CandidateCapturePhase
Sc6CandidateCheckpointCapture::transient_capture_phase() const noexcept
{
    return adapter_->last_capture_phase();
}

void Sc6CandidateCheckpointCapture::ResetCapturePerformanceWindow() noexcept
{
    if (adapter_ != nullptr) adapter_->ResetCapturePerformanceWindow();
}

CharaAnimationTopologyIssue
Sc6CandidateCheckpointCapture::transient_animation_topology_issue() const noexcept
{
    return chara_animation_->topology_issue();
}

std::uintptr_t
Sc6CandidateCheckpointCapture::transient_animation_topology_observed() const noexcept
{
    return chara_animation_->topology_observed();
}

std::uint32_t Sc6CandidateCheckpointCapture::transient_identity_issue() const noexcept
{
    return transient_identity_issue_;
}

std::uint64_t Sc6CandidateCheckpointCapture::transient_identity_expected() const noexcept
{
    return transient_identity_expected_;
}

std::uint64_t Sc6CandidateCheckpointCapture::transient_identity_observed() const noexcept
{
    return transient_identity_observed_;
}

Status Sc6CandidateCheckpointCapture::EnsureRestoreOwnership(
    std::uint32_t simulation_thread_id) noexcept
{
    if (ucrt_broker_ == nullptr || simulation_thread_id == 0
        || simulation_thread_id != ::GetCurrentThreadId())
    {
        return Status::failure(FailureCode::WrongThread);
    }
    return ucrt_broker_->EnsureOwnership(simulation_thread_id);
}

Status Sc6CandidateCheckpointCapture::RestoreAndVerify(
    const Snapshot& snapshot) noexcept
{
    if (!regions_->IsBound() || bound_manager_ == 0
        || snapshot.coordinate.generation != bound_round_generation_)
    {
        return Status::failure(FailureCode::GenerationMismatch);
    }
    Status status = adapter_->PreflightRestore(snapshot);
    if (status.ok()) status = adapter_->Restore(snapshot);
    if (status.ok()) status = adapter_->RebuildDerivedState();
    if (status.ok()) status = adapter_->VerifyRestoredState(snapshot);
    return status;
}

Status Sc6CandidateCheckpointCapture::RestoreBattleAudioSelectorForPresentation(
    const Snapshot& snapshot) noexcept
{
    if (!regions_->IsBound() || bound_manager_ == 0
        || snapshot.coordinate.generation != bound_round_generation_)
        return Status::failure(FailureCode::GenerationMismatch);
    CandidateCheckpointImage image{};
    const Status decoded = CandidateCheckpointCodec::Decode(snapshot, image);
    if (!decoded.ok()) return decoded;
    return battle_audio_selector_->RestoreTransactional(
        image.battle_audio_selector);
}

Status Sc6CandidateCheckpointCapture::RestoreInputLogForReplay(
    const Snapshot& snapshot) noexcept
{
    CandidateCheckpointImage image{};
    const Status decoded = CandidateCheckpointCodec::Decode(snapshot, image);
    if (!decoded.ok()) return decoded;
    return regions_->RestoreInputLogTransactional(image.native);
}

Status Sc6CandidateCheckpointCapture::RestoreMoveDispatchMasksForReplay(
    const Snapshot& snapshot) noexcept
{
    CandidateCheckpointImage image{};
    const Status decoded = CandidateCheckpointCodec::Decode(snapshot, image);
    if (!decoded.ok()) return decoded;
    return regions_->RestoreMoveDispatchMasksTransactional(image.native);
}

Status Sc6CandidateCheckpointCapture::CaptureCameraSourceFrame(
    NativeCameraSourceFrameImage& output) noexcept
{
    if (!regions_->IsBound() || bound_manager_ == 0)
        return Status::failure(FailureCode::GenerationMismatch);
    return regions_->CaptureCameraSourceFrame(output);
}

Status Sc6CandidateCheckpointCapture::RestoreCameraSourceFrameForReplay(
    const NativeCameraSourceFrameImage& image) noexcept
{
    if (!regions_->IsBound() || bound_manager_ == 0)
        return Status::failure(FailureCode::GenerationMismatch);
    return regions_->RestoreCameraSourceFrameTransactional(image);
}

Status Sc6CandidateCheckpointCapture::PrepareInputLogForReplay(
    const CanonicalInputDiagnostic& expected,
    const InputPair& input) noexcept
{
    return regions_->PrepareInputLogTransactional(expected, input);
}

Status Sc6CandidateCheckpointCapture::TraceLocalStreamOffset(
    std::size_t stream_offset, HgCpuWriteSpan& output,
    std::array<std::uintptr_t, 2>& fighter_roots,
    std::uintptr_t& image_base) noexcept
{
    fighter_roots = {};
    image_base = image_base_;
    if (!read_fighter_roots(fighter_roots))
        return Status::failure(FailureCode::ContextUnavailable);
    return adapter_->TraceLocalStreamOffset(stream_offset, output);
}

void Sc6CandidateCheckpointCapture::InvalidateHistory() noexcept
{
    ReleaseBinding();
    landing_snapshots_.Clear();
    batch_entry_snapshots_.Clear();
    landing_status_ = {};
    batch_entry_status_ = {};
    landing_capture_timing_ = {};
    batch_entry_capture_timing_ = {};
    landing_store_timing_ = {};
    batch_entry_store_timing_ = {};
}

void Sc6CandidateCheckpointCapture::ReleaseBinding() noexcept
{
    adapter_->Reset();
    motion_banks_->Invalidate();
    move_dispatch_->Invalidate();
    secondary_events_->Invalidate();
    chara_animation_->Invalidate();
    wind_transaction_.reset();
    wind_allocator_.reset();
    regions_->Invalidate();
    battle_audio_selector_->Reset();
    wind_probe_->Invalidate();
    bound_manager_ = 0;
    bound_move_dispatch_ = 0;
    bound_session_generation_ = 0;
    bound_round_generation_ = 0;
    bound_camera_topology_ = {};
    bound_callback_topology_ = {};
}

void Sc6CandidateCheckpointCapture::Reset() noexcept
{
    InvalidateHistory();
    image_base_ = 0;
    image_size_ = 0;
    ucrt_broker_ = nullptr;
}

CandidateCheckpointCaptureStatus Sc6CandidateCheckpointCapture::status(
    CandidateCheckpointRole role) const noexcept
{
    return role == CandidateCheckpointRole::Landing
        ? landing_status_ : batch_entry_status_;
}

const SnapshotStore& Sc6CandidateCheckpointCapture::snapshots(
    CandidateCheckpointRole role) const noexcept
{
    return role == CandidateCheckpointRole::Landing
        ? landing_snapshots_ : batch_entry_snapshots_;
}

NativeCandidateValidationDiagnostic
Sc6CandidateCheckpointCapture::restore_validation() const noexcept
{
    return regions_->validation_diagnostic();
}

CandidateAdapterPerformanceStatus
Sc6CandidateCheckpointCapture::adapter_performance() const noexcept
{
    return adapter_->performance_status();
}
}
