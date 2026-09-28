#include "Sc6CandidateCheckpointCapture.hpp"

#include "DeterministicHookSet.hpp"

#include <chrono>
#include <DynamicOutput/DynamicOutput.hpp>

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
constexpr std::uintptr_t camera_velocity_basis_rva = 0x470E180;
constexpr std::uintptr_t camera_timer_config_rva = 0x470ED50;
constexpr std::uintptr_t camera_director_vtable_rva = 0x3E85568;
constexpr std::uintptr_t camera_interface_vtable_rva = 0x3E87A58;
constexpr std::size_t hgcpu_camera_state_size = 0x360;
constexpr std::uintptr_t camera_action_list_rva = 0x470EE90;
constexpr std::uintptr_t camera_action_owner_rva = 0x470ED50;
constexpr std::array<std::uintptr_t, 4> camera_timer_global_rvas{
    0x470DF48, 0x470DF68, 0x40F4A84, 0x470DFD8};
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

struct CallbackOwnerResolveContext
{
    const CallbackTopology* bound{};
    std::size_t next_record{};
};

RC::Unreal::UObject* resolve_weak_object(
    std::int32_t object_index, std::int32_t serial_number) noexcept
{
    RC::Unreal::FWeakObjectPtr weak;
    weak.ObjectIndex = object_index;
    weak.ObjectSerialNumber = serial_number;
    return weak.Get();
}

Status resolve_callback_owner_class(
    void* user, std::int32_t object_index, std::int32_t serial_number,
    std::uint64_t& class_token) noexcept
{
    class_token = 0;
    auto* object = resolve_weak_object(object_index, serial_number);
    if (object == nullptr || object->GetClassPrivate() == nullptr)
        return Status::failure(FailureCode::IdentityMismatch);
    // UObject class identity cannot change during an object's lifetime. Once
    // the bound topology has established the class-name token, the weak index
    // and serial pair proves the same live object and lets the per-frame probe
    // avoid formatting and hashing its class name again.
    auto* context = static_cast<CallbackOwnerResolveContext*>(user);
    if (context != nullptr && context->bound != nullptr)
    {
        // Capture walks the callback collections in the same canonical order
        // used to create the bound topology.  Match that exact position instead
        // of linearly searching the complete bound topology for every callback.
        // A reordered/replaced owner still falls through to live class hashing
        // and is rejected by the subsequent full-record comparison.
        const auto record_index = context->next_record++;
        if (record_index < context->bound->records.size())
        {
            const auto& record = context->bound->records[record_index];
            if (record.owner_object_index == object_index
                && record.owner_serial_number == serial_number)
            {
                class_token = record.owner_class_token;
                return class_token == 0
                    ? Status::failure(FailureCode::IdentityMismatch)
                    : Status::success();
            }
        }
    }
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
    std::size_t AllocationBytes(std::size_t requested) noexcept override
    {
        if (GetCurrentThreadId() != owner_thread_id_) return 0;
        __try {
            return reinterpret_cast<std::size_t (*)(std::size_t, unsigned)>(
                image_base_ + 0xd50dc0)(requested, 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
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

Sc6CandidateCheckpointCapture::Sc6CandidateCheckpointCapture(CaptureMode mode)
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
      adapter_(std::make_unique<CandidateGameStateAdapter>(*regions_, hgcpu_)),
      landing_snapshots_(Schema::replay_landing_checkpoint_memory_budget,
          mode == CaptureMode::LegacyBatch ? maximum_landing_checkpoints : 0, CapacityPolicy::RejectNew),
      batch_entry_snapshots_(Schema::replay_batch_entry_checkpoint_memory_budget,
          mode == CaptureMode::LegacyBatch ? maximum_batch_entry_checkpoints : 0, CapacityPolicy::RejectNew),
      auxiliary_decode_scratch_(
          std::make_unique<CandidateCheckpointImage>())
{
    resumable_core_ = mode == CaptureMode::ResumableCore;
}

Sc6CandidateCheckpointCapture::~Sc6CandidateCheckpointCapture() = default;

std::size_t Sc6CandidateCheckpointCapture::transient_initial_storage_bytes() noexcept
{
    // These constructors bind references and value-initialize fixed storage;
    // Configure/Bind allocate their dynamic scratch separately. No legacy
    // history slots are allocated by the transient constructor.
    return sizeof(Sc6CandidateCheckpointCapture) + sizeof(ProcessMemory)
        + sizeof(NativeCandidateRegions) + sizeof(BattleAudioSelectorState)
        + sizeof(MotionBankSnapshot) + sizeof(MoveDispatchState)
        + sizeof(SecondaryEventState) + sizeof(CharaAnimationState)
        + sizeof(CallbackTopologyProbe) + sizeof(StageWindTopologyProbe)
        + sizeof(CandidateGameStateAdapter) + sizeof(CandidateCheckpointImage);
}

std::size_t Sc6CandidateCheckpointCapture::owned_scratch_bytes() const noexcept
{
    return owned_scratch_status().aggregate();
}

CandidateCheckpointScratchStatus
Sc6CandidateCheckpointCapture::owned_scratch_status() const noexcept
{
    const auto snapshot_capacity = [](const Snapshot& snapshot) noexcept {
        std::size_t bytes = snapshot.bytes.capacity()
            + snapshot.local_images.capacity()
                * sizeof(LocalReconstructionImage);
        for (const auto& local : snapshot.local_images)
            bytes += local.bytes.capacity();
        return bytes;
    };
    CandidateCheckpointScratchStatus status{};
    status.fixed_subsystems = sizeof(Sc6CandidateCheckpointCapture)
        + landing_snapshots_.BytesUsed() + batch_entry_snapshots_.BytesUsed();
    if (memory_) status.fixed_subsystems += sizeof(ProcessMemory);
    if (regions_) status.fixed_subsystems += sizeof(NativeCandidateRegions);
    if (battle_audio_selector_)
        status.fixed_subsystems += sizeof(BattleAudioSelectorState);
    if (motion_banks_)
        status.fixed_subsystems += sizeof(MotionBankSnapshot);
    if (move_dispatch_)
        status.fixed_subsystems += sizeof(MoveDispatchState);
    if (secondary_events_)
        status.fixed_subsystems += sizeof(SecondaryEventState);
    if (chara_animation_)
        status.fixed_subsystems += sizeof(CharaAnimationState);
    if (callback_probe_)
        status.fixed_subsystems += sizeof(CallbackTopologyProbe);
    if (wind_probe_)
        status.fixed_subsystems += sizeof(StageWindTopologyProbe);
    if (wind_allocator_)
        status.fixed_subsystems += sizeof(ProcessStageWindAllocator);
    if (wind_transaction_)
        status.fixed_subsystems += sizeof(StageWindGraphTransaction) + wind_transaction_->owned_bytes();
    if (adapter_)
        status.adapter = sizeof(CandidateGameStateAdapter)
            + adapter_->owned_scratch_bytes();
    status.capture_snapshots = snapshot_capacity(landing_capture_scratch_)
        + snapshot_capacity(batch_entry_capture_scratch_);
    status.callback_topology = bound_callback_topology_.records.capacity()
            * sizeof(CallbackTopologyRecord)
        + callback_topology_scratch_.records.capacity()
            * sizeof(CallbackTopologyRecord);
    if (auxiliary_decode_scratch_)
        status.auxiliary_decode = sizeof(CandidateCheckpointImage)
            + CandidateCheckpointDynamicCapacity(
                *auxiliary_decode_scratch_, true);
    return status;
}

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
    CameraTopology& output, CameraTopologyCaptureDiagnostic* diagnostic) noexcept
{
    output = {};
    if (diagnostic != nullptr) *diagnostic = {};
    const auto fail = [diagnostic](FailureCode code,
                          CameraTopologyCaptureStage stage,
                          std::uint32_t index = 0,
                          std::uint64_t observed = 0,
                          std::uint64_t expected = 0) noexcept {
        if (diagnostic != nullptr)
            *diagnostic = {code, stage, index, observed, expected};
        return Status::failure(code);
    };
    std::uintptr_t root{};
    if (!memory_->Read(image_base_ + effect_camera_pointer_rva,
            std::as_writable_bytes(std::span{&root, 1})))
    {
        return fail(FailureCode::CapturePreflightFailed,
            CameraTopologyCaptureStage::RootPointer);
    }
    if (root == 0) return Status::success();

    std::array<std::byte, hgcpu_camera_state_size> readable{};
    std::array<std::uintptr_t, 2> vtables{};
    const auto expected_root = image_base_ + camera_director_state_rva;
    if (root != expected_root)
        return fail(FailureCode::IdentityMismatch,
            CameraTopologyCaptureStage::RootIdentity, 0, root, expected_root);
    if (!memory_->Read(root, readable))
        return fail(FailureCode::IdentityMismatch,
            CameraTopologyCaptureStage::RootReadable);
    if (!memory_->Read(root,
            std::as_writable_bytes(std::span{vtables.data(), 1}))
        || !memory_->Read(root + 0x10,
            std::as_writable_bytes(std::span{vtables.data() + 1, 1})))
        return fail(FailureCode::IdentityMismatch,
            CameraTopologyCaptureStage::DirectorVtables);
    const std::array expected_vtables{
        image_base_ + camera_director_vtable_rva,
        image_base_ + camera_interface_vtable_rva};
    if (vtables != expected_vtables)
        return fail(FailureCode::IdentityMismatch,
            CameraTopologyCaptureStage::DirectorVtables,
            vtables[0] == expected_vtables[0] ? 1u : 0u,
            vtables[0] == expected_vtables[0] ? vtables[1] : vtables[0],
            vtables[0] == expected_vtables[0]
                ? expected_vtables[1] : expected_vtables[0]);

    const std::uintptr_t list = image_base_ + camera_action_list_rva;
    std::uintptr_t owner{};
    std::uintptr_t backing{};
    std::array<std::uintptr_t, camera_action_count> slots{};
    std::byte backing_tail{};
    if (!memory_->Read(list,
            std::as_writable_bytes(std::span{&owner, 1})))
        return fail(FailureCode::IdentityMismatch,
            CameraTopologyCaptureStage::ActionListOwner);
    const auto expected_owner = image_base_ + camera_action_owner_rva;
    if (owner != expected_owner)
        return fail(FailureCode::IdentityMismatch,
            CameraTopologyCaptureStage::ActionListOwner,
            0, owner, expected_owner);
    if (!memory_->Read(list + 0x08,
            std::as_writable_bytes(std::span{&backing, 1})))
        return fail(FailureCode::IdentityMismatch,
            CameraTopologyCaptureStage::ActionListBacking);
    if (backing == 0)
        return fail(FailureCode::IdentityMismatch,
            CameraTopologyCaptureStage::ActionListBacking);
    if (!memory_->Read(list + 0x10,
            std::as_writable_bytes(std::span{slots})))
        return fail(FailureCode::IdentityMismatch,
            CameraTopologyCaptureStage::ActionListSlots);
    if (!memory_->Read(backing + camera_action_backing_size - 1,
            std::span<std::byte>{&backing_tail, 1}))
        return fail(FailureCode::IdentityMismatch,
            CameraTopologyCaptureStage::ActionBackingTail);

    for (std::size_t index = 0; index < slots.size(); ++index)
    {
        const std::uintptr_t expected = backing + index * camera_action_stride;
        std::uint32_t slot_index{};
        std::uintptr_t action_owner{};
        std::uintptr_t action_list{};
        if (slots[index] != expected)
            return fail(FailureCode::IdentityMismatch,
                CameraTopologyCaptureStage::ActionSlotPointer,
                static_cast<std::uint32_t>(index), slots[index], expected);
        if (!memory_->Read(expected,
                std::as_writable_bytes(
                    std::span{output.action_vtables.data() + index, 1})))
            return fail(FailureCode::IdentityMismatch,
                CameraTopologyCaptureStage::ActionVtable,
                static_cast<std::uint32_t>(index));
        if (!memory_->Read(expected + 0x08,
                std::as_writable_bytes(std::span{&slot_index, 1})))
            return fail(FailureCode::IdentityMismatch,
                CameraTopologyCaptureStage::ActionIndex,
                static_cast<std::uint32_t>(index));
        if (!memory_->Read(expected + 0x10,
                std::as_writable_bytes(std::span{&action_owner, 1})))
            return fail(FailureCode::IdentityMismatch,
                CameraTopologyCaptureStage::ActionOwner,
                static_cast<std::uint32_t>(index));
        if (!memory_->Read(expected + 0x18,
                std::as_writable_bytes(std::span{&action_list, 1})))
            return fail(FailureCode::IdentityMismatch,
                CameraTopologyCaptureStage::ActionList,
                static_cast<std::uint32_t>(index));
        if (!memory_->Read(expected + 0x20,
                std::as_writable_bytes(
                    std::span{output.action_types.data() + index, 1})))
            return fail(FailureCode::IdentityMismatch,
                CameraTopologyCaptureStage::ActionType,
                static_cast<std::uint32_t>(index));
        if (slot_index != index)
            return fail(FailureCode::IdentityMismatch,
                CameraTopologyCaptureStage::ActionIndex,
                static_cast<std::uint32_t>(index), slot_index, index);
        if (action_owner != owner)
            return fail(FailureCode::IdentityMismatch,
                CameraTopologyCaptureStage::ActionOwner,
                static_cast<std::uint32_t>(index), action_owner, owner);
        if (action_list != list)
            return fail(FailureCode::IdentityMismatch,
                CameraTopologyCaptureStage::ActionList,
                static_cast<std::uint32_t>(index), action_list, list);
        if (output.action_vtables[index] < image_base_
            || output.action_vtables[index] >= image_base_ + image_size_
            || (output.action_vtables[index] & 7) != 0)
            return fail(FailureCode::IdentityMismatch,
                CameraTopologyCaptureStage::ActionVtable,
                static_cast<std::uint32_t>(index),
                output.action_vtables[index], image_base_);
        if (output.action_types[index] > 0x1A)
            return fail(FailureCode::IdentityMismatch,
                CameraTopologyCaptureStage::ActionType,
                static_cast<std::uint32_t>(index),
                output.action_types[index], 0x1A);
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
    CallbackOwnerResolveContext context{&bound_callback_topology_};
    return callback_probe_->Capture(image_base_, image_size_, refs,
        &resolve_callback_owner_class, &context, output);
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
        image_base_ + camera_velocity_basis_rva,
        image_base_ + camera_timer_config_rva,
        image_base_ + camera_action_list_rva,
        camera_topology.action_backing,
        {image_base_ + camera_timer_global_rvas[0],
         image_base_ + camera_timer_global_rvas[1],
         image_base_ + camera_timer_global_rvas[2],
         image_base_ + camera_timer_global_rvas[3]},
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
    if (resumable_core_) {
        addresses.mt_rng = image_base_ + 0x4100ea0;
        addresses.replay_camera_publication=true;
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
    adapter_binding.hgcpu_stat_fighters=fighter_roots;
    adapter_binding.battle_audio_selector = battle_audio_selector_.get();
    adapter_binding.restore_audio_selector = resumable_core_;
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
        &battle_audio_handler_overflowed, nullptr, resumable_core_};
    Status adapter_status = battle_audio_selector_->Bind(
        audio_selector_binding);
    if (adapter_status.ok()) adapter_status = chara_animation_->Bind(
        fighter_roots, coordinate.generation);
    if (adapter_status.ok()) {
        adapter_status = motion_banks_->Bind(fighter_roots, adapter_binding.hgcpu_context,
            chara_animation_.get());
        if (!adapter_status.ok()) {
            const auto diagnostic=motion_banks_->binding_diagnostic();
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] motion skeleton binding rejected line={} address={:x} observed={} code={}\n"),
                diagnostic.line,diagnostic.address,diagnostic.observed,static_cast<unsigned>(adapter_status.code));
        }
    }
    if (adapter_status.ok()) adapter_status = move_dispatch_->Bind(
        move_dispatch, coordinate.generation);
    if (adapter_status.ok()) adapter_status = secondary_events_->Bind(
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
    const Status callback_status = capture_callback_topology(
        callback_topology_scratch_);
    if (!callback_status.ok())
    {
        ReleaseBinding();
        return callback_status;
    }
    try
    {
        bound_callback_topology_ = callback_topology_scratch_;
    }
    catch (...)
    {
        ReleaseBinding();
        return Status::failure(FailureCode::CapacityExceeded);
    }
    return Status::success();
}

Status Sc6CandidateCheckpointCapture::Capture(
    CandidateCheckpointRole role,
    std::uintptr_t battle_manager,
    FrameCoordinate coordinate,
    std::uint64_t session_generation,
    std::uint32_t simulation_thread_id,
    CandidateTransientCaptureDiagnostic* output_diagnostic) noexcept
{
    CandidateTransientCaptureDiagnostic diagnostic{};
    const auto finish = [&](Status result) noexcept {
        diagnostic.failure = result.code;
        if (output_diagnostic != nullptr) *output_diagnostic = diagnostic;
        return result;
    };
    if (image_base_ == 0 || battle_manager == 0
        || coordinate.generation == 0 || session_generation == 0
        || simulation_thread_id == 0)
    {
        return finish(Status::failure(FailureCode::ContextUnavailable));
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
            diagnostic.phase = capture_status.capture_phase;
            diagnostic.validation = capture_status.validation;
            diagnostic.animation_topology_issue = capture_status.animation_topology_issue;
            diagnostic.animation_topology_observed = capture_status.animation_topology_observed;
            return finish(rebound);
        }
    }

    CameraTopology camera_topology{};
    diagnostic.phase = CandidateCapturePhase::CameraTopology;
    const Status camera_status = capture_camera_topology(camera_topology, &diagnostic.camera);
    if (!camera_status.ok() || camera_topology != bound_camera_topology_)
    {
        const auto failure = camera_status.ok()
            ? FailureCode::IdentityMismatch : camera_status.code;
        if (camera_status.ok())
            diagnostic.camera = camera_topology.DifferenceFrom(bound_camera_topology_);
        diagnostic.identity_issue = 1;
        diagnostic.identity_expected = bound_camera_topology_.camera_root;
        diagnostic.identity_observed = camera_topology.camera_root;
        ReleaseBinding();
        capture_status.failure = failure;
        return finish(Status::failure(failure));
    }

    diagnostic.phase = CandidateCapturePhase::CallbackTopology;
    const Status callback_status = capture_callback_topology(
        callback_topology_scratch_);
    if (!callback_status.ok()
        || callback_topology_scratch_ != bound_callback_topology_)
    {
        const auto failure = callback_status.ok()
            ? FailureCode::IdentityMismatch : callback_status.code;
        diagnostic.identity_issue = 2;
        diagnostic.identity_expected = bound_callback_topology_.signature;
        diagnostic.identity_observed = callback_topology_scratch_.signature;
        ReleaseBinding();
        capture_status.failure = failure;
        return finish(Status::failure(failure));
    }

    diagnostic.phase = CandidateCapturePhase::Adapter;
    StageWindTopologyImage wind_topology{};
    const Status wind_status = wind_probe_->Capture(wind_topology);
    if (!wind_status.ok())
    {
        ReleaseBinding();
        capture_status.failure = wind_status.code;
        return finish(wind_status);
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
        // The first checkpoint prewarms every budget-admitted store slot.
        // Later captures copy into those retained buffers without allocating,
        // including after online status 4.
        captured = snapshots.SaveCopyPrewarmed(snapshot);
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
        diagnostic.phase = capture_status.capture_phase;
        diagnostic.validation = capture_status.validation;
        diagnostic.animation_topology_issue = capture_status.animation_topology_issue;
        diagnostic.animation_topology_observed = capture_status.animation_topology_observed;
        if (captured.code == FailureCode::IdentityMismatch) diagnostic.identity_issue = 3;
        return finish(captured);
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
    diagnostic.phase = CandidateCapturePhase::None;
    return finish(Status::success());
}

bool Sc6CandidateCheckpointCapture::RequiresCaptureBinding(
    std::uintptr_t battle_manager,FrameCoordinate coordinate,
    std::uint64_t session_generation) const noexcept
{
    return !regions_->IsBound() || bound_manager_ != battle_manager
        || bound_session_generation_ != session_generation
        || bound_round_generation_ != coordinate.generation;
}

std::size_t Sc6CandidateCheckpointCapture::CaptureAllocationEnvelopeBytes(
    std::uintptr_t battle_manager,FrameCoordinate coordinate,
    std::uint64_t session_generation) const noexcept
{
    const auto full=transient_allocation_envelope_bytes();
    if(RequiresCaptureBinding(battle_manager,coordinate,session_generation)) return full;
    return BoundAllocationEnvelopeBytes();
}

std::size_t Sc6CandidateCheckpointCapture::BoundAllocationEnvelopeBytes() noexcept
{
    // Capture/decode on an existing binding never calls MotionBankSnapshot::Bind.
    // Its retained topology capacity remains in adapter scratch accounting;
    // all other growth and old/new coexistence allowances remain reserved.
    const auto full=transient_allocation_envelope_bytes();
    const auto binding=MotionBankSnapshot::BindAllocationEnvelopeBytes();
    return full>=binding?full-binding:full;
}

Status Sc6CandidateCheckpointCapture::BindForCanonicalCapture(
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
    if (RequiresCaptureBinding(battle_manager,coordinate,session_generation))
    {
        return bind(battle_manager, coordinate, session_generation,
            simulation_thread_id);
    }
    return Status::success();
}

Status Sc6CandidateCheckpointCapture::finish_transient_capture(Status status,
    const CandidateTransientCaptureDiagnostic& diagnostic,
    CandidateTransientCaptureDiagnostic* output) noexcept
{
    auto completed = diagnostic;
    completed.failure = status.ok() ? FailureCode::None : status.code;
    completed.validation = regions_->validation_diagnostic();
    completed.animation_topology_issue = chara_animation_->topology_issue();
    completed.animation_topology_observed =
        chara_animation_->topology_observed();
    transient_identity_issue_ = completed.identity_issue;
    transient_identity_expected_ = completed.identity_expected;
    transient_identity_observed_ = completed.identity_observed;
    transient_capture_phase_ = completed.phase;
    if (output != nullptr) *output = completed;
    return status;
}

Status Sc6CandidateCheckpointCapture::CaptureTransient(
    FrameCoordinate coordinate, Snapshot& output,
    CandidateTransientCaptureDiagnostic* output_diagnostic) noexcept
{
    CandidateTransientCaptureDiagnostic diagnostic{};
    diagnostic.phase = CandidateCapturePhase::Adapter;
    if (!regions_->IsBound() || bound_manager_ == 0
        || coordinate.generation != bound_round_generation_)
    {
        output = {};
        return finish_transient_capture(
            Status::failure(FailureCode::GenerationMismatch),
            diagnostic, output_diagnostic);
    }
    CameraTopology camera_topology{};
    diagnostic.phase = CandidateCapturePhase::CameraTopology;
    const Status camera = capture_camera_topology(
        camera_topology, &diagnostic.camera);
    if (!camera.ok() || camera_topology != bound_camera_topology_)
    {
        if (camera.ok())
            diagnostic.camera = camera_topology.DifferenceFrom(bound_camera_topology_);
        diagnostic.identity_issue = 1;
        diagnostic.identity_expected = bound_camera_topology_.camera_root;
        diagnostic.identity_observed = camera_topology.camera_root;
        return finish_transient_capture(Status::failure(camera.ok()
                ? FailureCode::IdentityMismatch : camera.code),
            diagnostic, output_diagnostic);
    }
    diagnostic.phase = CandidateCapturePhase::CallbackTopology;
    const Status callbacks = capture_callback_topology(
        callback_topology_scratch_);
    if (!callbacks.ok()
        || callback_topology_scratch_ != bound_callback_topology_)
    {
        diagnostic.identity_issue = 2;
        diagnostic.identity_expected = bound_callback_topology_.signature;
        diagnostic.identity_observed = callback_topology_scratch_.signature;
        return finish_transient_capture(Status::failure(callbacks.ok()
                ? FailureCode::IdentityMismatch : callbacks.code),
            diagnostic, output_diagnostic);
    }
    diagnostic.phase = CandidateCapturePhase::Adapter;
    const Status captured = adapter_->Capture(coordinate, output);
    if (!captured.ok()
        && adapter_->last_capture_phase() != CandidateCapturePhase::None)
    {
        diagnostic.phase = adapter_->last_capture_phase();
        if(diagnostic.phase==CandidateCapturePhase::CharaAnimation) {
            const auto& section=chara_animation_->section_diagnostic();
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] animation capture rejected issue={} observed={:x} player={} root={:x} pointer={:x} header={:x},{:x},{:x},{:x},{:x} sections={}\n"),
                RC::to_generic_string(chara_animation_topology_issue_name(chara_animation_->topology_issue())),
                chara_animation_->topology_observed(),section.player,section.root,section.pointer,
                section.header[0],section.header[1],section.header[2],section.header[3],section.header[4],section.count);
        }
        if(diagnostic.phase==CandidateCapturePhase::MotionBanks) {
            const auto failure=motion_banks_->binding_diagnostic();
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] motion skeleton capture rejected line={} address={:x} fighter_offset={:x} expected={:x} observed={:x} kind={}\n"),
                failure.line,failure.address,failure.fighter_offset,failure.expected,failure.observed,
                RC::to_generic_string(failure.kind?failure.kind:"unknown"));
        }
    }
    if (captured.code == FailureCode::IdentityMismatch)
        diagnostic.identity_issue = 3;
    if (captured.ok()) diagnostic.phase = CandidateCapturePhase::None;
    return finish_transient_capture(captured, diagnostic, output_diagnostic);
}

Status Sc6CandidateCheckpointCapture::PrepareOnlineOwnedStorage(
    const Snapshot& prototype) noexcept
{
    if (adapter_ == nullptr || auxiliary_decode_scratch_ == nullptr)
        return Status::failure(FailureCode::AdapterUnqualified);
    Status status = adapter_->PrepareTransientCaptureStorage(prototype);
    if (status.ok())
        status = PrepareCandidateCheckpointStorage(
            *auxiliary_decode_scratch_, true);
    if (status.ok())
        status = PrepareSnapshotCaptureStorage(
            landing_capture_scratch_, prototype);
    if (status.ok())
        status = PrepareSnapshotCaptureStorage(
            batch_entry_capture_scratch_, prototype);
    if (status.ok())
        status = landing_snapshots_.PrewarmCaptureSlots(prototype);
    if (status.ok())
        status = batch_entry_snapshots_.PrewarmCaptureSlots(prototype);
    if (status.ok())
    {
        // Status 4 snapshots owned_storage_status(). Publish the prewarm
        // allocation now rather than making the next checkpoint capture look
        // like post-ownership growth when it merely refreshes these mirrors.
        landing_status_.bytes_used = landing_snapshots_.BytesUsed();
        batch_entry_status_.bytes_used = batch_entry_snapshots_.BytesUsed();
    }
    return status;
}

Status Sc6CandidateCheckpointCapture::StoreSynchronizedBatchEntry(
    const Snapshot& snapshot) noexcept
{
    if (!regions_->IsBound() || snapshot.coordinate.generation == 0
        || snapshot.coordinate.generation != bound_round_generation_
        || snapshot.bytes.empty() || snapshot.local_images.empty())
        return Status::failure(FailureCode::GenerationMismatch);
    return batch_entry_snapshots_.SaveCopyPrewarmed(snapshot);
}

Status Sc6CandidateCheckpointCapture::CaptureCanonical(
    FrameCoordinate coordinate, Snapshot& output,
    CandidateTransientCaptureDiagnostic* output_diagnostic) noexcept
{
    CandidateTransientCaptureDiagnostic diagnostic{};
    diagnostic.phase = CandidateCapturePhase::Adapter;
    if (!regions_->IsBound() || bound_manager_ == 0
        || coordinate.generation != bound_round_generation_)
    {
        output = {};
        return finish_transient_capture(
            Status::failure(FailureCode::GenerationMismatch),
            diagnostic, output_diagnostic);
    }
    CameraTopology camera_topology{};
    diagnostic.phase = CandidateCapturePhase::CameraTopology;
    const Status camera = capture_camera_topology(
        camera_topology, &diagnostic.camera);
    if (!camera.ok() || camera_topology != bound_camera_topology_)
    {
        if (camera.ok())
            diagnostic.camera = camera_topology.DifferenceFrom(bound_camera_topology_);
        diagnostic.identity_issue = 1;
        diagnostic.identity_expected = bound_camera_topology_.camera_root;
        diagnostic.identity_observed = camera_topology.camera_root;
        return finish_transient_capture(Status::failure(camera.ok()
                ? FailureCode::IdentityMismatch : camera.code),
            diagnostic, output_diagnostic);
    }
    diagnostic.phase = CandidateCapturePhase::CallbackTopology;
    const Status callbacks = capture_callback_topology(
        callback_topology_scratch_);
    if (!callbacks.ok()
        || callback_topology_scratch_ != bound_callback_topology_)
    {
        diagnostic.identity_issue = 2;
        diagnostic.identity_expected = bound_callback_topology_.signature;
        diagnostic.identity_observed = callback_topology_scratch_.signature;
        return finish_transient_capture(Status::failure(callbacks.ok()
                ? FailureCode::IdentityMismatch : callbacks.code),
            diagnostic, output_diagnostic);
    }
    diagnostic.phase = CandidateCapturePhase::Adapter;
    const Status captured = adapter_->CaptureCanonical(coordinate, output);
    if (!captured.ok()
        && adapter_->last_capture_phase() != CandidateCapturePhase::None)
    {
        diagnostic.phase = adapter_->last_capture_phase();
    }
    if (captured.code == FailureCode::IdentityMismatch)
        diagnostic.identity_issue = 3;
    if (captured.ok()) diagnostic.phase = CandidateCapturePhase::None;
    return finish_transient_capture(captured, diagnostic, output_diagnostic);
}

CandidateCapturePhase
Sc6CandidateCheckpointCapture::transient_capture_phase() const noexcept
{
    return transient_capture_phase_;
}

std::array<std::uint16_t, 2>
Sc6CandidateCheckpointCapture::last_captured_movevm_short25() const noexcept
{
    return adapter_ == nullptr
        ? std::array<std::uint16_t, 2>{}
        : adapter_->last_captured_movevm_short25();
}

NativeMoveVmStateShortImage
Sc6CandidateCheckpointCapture::last_captured_movevm_state_shorts() const noexcept
{
    return adapter_ == nullptr
        ? NativeMoveVmStateShortImage{}
        : adapter_->last_captured_movevm_state_shorts();
}

NativeRngImage Sc6CandidateCheckpointCapture::last_captured_rng() const noexcept
{
    return adapter_ == nullptr
        ? NativeRngImage{}
        : adapter_->last_captured_rng();
}

Status Sc6CandidateCheckpointCapture::GetLastCanonicalPeerDiagnostic(
    FrameCoordinate coordinate, PeerBaselineStateDiagnostic& output)
    const noexcept
{
    return adapter_ == nullptr
        ? Status::failure(FailureCode::ContextUnavailable)
        : adapter_->GetLastCanonicalPeerDiagnostic(coordinate, output);
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

Status Sc6CandidateCheckpointCapture::PrepareEnclosingWind(const Snapshot& snapshot, std::size_t budget) noexcept
{
    if (!wind_transaction_ || !auxiliary_decode_scratch_ || snapshot.coordinate.generation != bound_round_generation_)
        return Status::failure(FailureCode::GenerationMismatch);
    const auto decode_envelope = BoundAllocationEnvelopeBytes();
    const auto wind_envelope = wind_transaction_->AllocationEnvelopeBytes();
    if (decode_envelope > budget || wind_envelope > budget - decode_envelope)
        return Status::failure(FailureCode::CapacityExceeded);
    const auto status = CandidateCheckpointCodec::Decode(snapshot, *auxiliary_decode_scratch_);
    if (!status.ok()) return status;
    return wind_transaction_->Prepare({image_base_, image_size_, image_base_ + wind_root_pointer_rva,
        bound_round_generation_}, auxiliary_decode_scratch_->wind, true, budget - decode_envelope);
}

std::size_t Sc6CandidateCheckpointCapture::transient_allocation_envelope_bytes() noexcept
{
    std::size_t wind_semantic{}, wind_derived{};
    for (const auto kind : {StageWindNodeKind::Parallel, StageWindNodeKind::RingOut,
            StageWindNodeKind::RingIn, StageWindNodeKind::ShockWave}) {
        const auto* layout = FindStageWindNodeLayout(kind);
        if (!layout) return Schema::replay_timeline_memory_limit;
        wind_semantic = (std::max)(wind_semantic, StageWindSemanticStateSize(*layout));
        wind_derived = (std::max)(wind_derived, StageWindDerivedStateSize(*layout));
    }
    const auto emitter = native_stage_wind_emitter_max_count * native_stage_wind_emitter_state_size;
    const auto wind = stage_wind_max_nodes * (wind_semantic + wind_derived);
    // Existing MoveDispatch admission: 1024 sub-elements, 16 pending windows.
    // Both the active variant and its inactive capacity owner are included.
    const auto move = 1024 * sizeof(MoveDispatchSubElementState)
        + 2 * 16 * sizeof(MoveDispatchPendingWindow);
    const auto local = maximum_local_reconstruction_images * sizeof(LocalReconstructionImage)
        + hgcpu_stream_capacity + motion_bank_image_bytes;
    const auto image = emitter + wind + move + local;
    // Four adapter images plus auxiliary decode, output and short-lived
    // validation images. Three envelopes cover old/new vector coexistence;
    // already-retained storage remains charged independently by the host.
    return MotionBankSnapshot::BindAllocationEnvelopeBytes()
        + 3 * (7 * image + 2 * move + 3 * emitter
            + 2 * 5 * 64 * sizeof(CallbackTopologyRecord))
        + 2 * sizeof(CandidateCheckpointImage) + 2 * sizeof(NativeCandidateImage)
        + sizeof(ProcessStageWindAllocator) + sizeof(StageWindGraphTransaction)
        + 3 * candidate_checkpoint_capture_byte_capacity;
}
Status Sc6CandidateCheckpointCapture::UndoEnclosingWind() noexcept
{
    return wind_transaction_ ? wind_transaction_->Undo() : Status::failure(FailureCode::ContextUnavailable);
}
Status Sc6CandidateCheckpointCapture::ValidateEnclosingWind() const noexcept
{
    return wind_transaction_ ? wind_transaction_->ValidateCommit() : Status::failure(FailureCode::ContextUnavailable);
}
Status Sc6CandidateCheckpointCapture::BeginEnclosingWindExecution(std::size_t retirement_budget) noexcept
{
    return wind_transaction_ ? wind_transaction_->BeginExecution(retirement_budget) : Status::failure(FailureCode::ContextUnavailable);
}
Status Sc6CandidateCheckpointCapture::SettleEnclosingWindExecution() noexcept
{
    return wind_transaction_ ? wind_transaction_->SettleExecution() : Status::failure(FailureCode::ContextUnavailable);
}
Status Sc6CandidateCheckpointCapture::ReopenEnclosingWindForUndo() noexcept
{
    return wind_transaction_ ? wind_transaction_->ReopenExecutionForUndo() : Status::failure(FailureCode::ContextUnavailable);
}
std::size_t Sc6CandidateCheckpointCapture::EnclosingWindExecutionBudget() const noexcept
{
    return wind_transaction_ ? wind_transaction_->AllocationEnvelopeBytes() : SIZE_MAX;
}
Status Sc6CandidateCheckpointCapture::FinishEnclosingWind() noexcept
{
    if (!wind_transaction_) return Status::failure(FailureCode::ContextUnavailable);
    return wind_transaction_->pending() ? wind_transaction_->Commit() : Status::success();
}
bool Sc6CandidateCheckpointCapture::PendingEnclosingWind() const noexcept
{
    return wind_transaction_ && wind_transaction_->pending();
}

Status Sc6CandidateCheckpointCapture::RestoreAndVerify(
    const Snapshot& snapshot) noexcept
{
    restore_failure_phase_ = 1;
    if (!regions_->IsBound() || bound_manager_ == 0
        || snapshot.coordinate.generation != bound_round_generation_)
    {
        return Status::failure(FailureCode::GenerationMismatch);
    }
    Status status = adapter_->PreflightRestore(snapshot);
    if (status.ok())
    {
        restore_failure_phase_ = 2;
        status = adapter_->Restore(snapshot);
    }
    if (status.ok())
    {
        restore_failure_phase_ = 3;
        status = adapter_->RebuildDerivedState();
    }
    if (status.ok())
    {
        restore_failure_phase_ = 4;
        status = adapter_->VerifyRestoredState(snapshot);
    }
    if (status.ok()) restore_failure_phase_ = 0;
    else {
        const auto failure=motion_banks_->binding_diagnostic();
        const auto native=adapter_->last_native_restore_diagnostic();
        if(adapter_->last_restore_operation_failure_mask()&(1u<<2))
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint native first failure issue={} index={} observed_a={} observed_b={} expected_a={} expected_b={}\n"),
                RC::to_generic_string(native_candidate_validation_issue_name(native.issue)),native.index,
                native.observed_a,native.observed_b,native.expected_a,native.expected_b);
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint restore participant failure code={} phase={} operations={} differences={} motion_line={} motion_offset={:x} motion_expected={:x} motion_observed={:x}\n"),
            static_cast<unsigned>(status.code),restore_failure_phase_,adapter_->last_restore_operation_failure_mask(),
            adapter_->last_restore_difference_mask(),failure.line,failure.fighter_offset,failure.expected,failure.observed);
    }
    return status;
}

Status Sc6CandidateCheckpointCapture::ValidateSnapshotUcrt(
    const Snapshot& snapshot, const UcrtRandBrokerImage& expected) noexcept
{
    if (!auxiliary_decode_scratch_) return Status::failure(FailureCode::ContextUnavailable);
    const auto decoded = CandidateCheckpointCodec::Decode(snapshot, *auxiliary_decode_scratch_);
    if (!decoded.ok()) return decoded;
    if (auxiliary_decode_scratch_->ucrt != expected || !ucrt_broker_)
        return Status::failure(FailureCode::RestorePreflightFailed);
    // This runs before participant preparation/publication, while restore
    // permission may still be released. Reject old native seed epochs here;
    // the adapter rechecks permission, TLS and the full image before writes.
    UcrtRandBrokerImage current{};
    const auto captured = ucrt_broker_->Capture(::GetCurrentThreadId(), current);
    if (!captured.ok()) return captured;
    return current.epoch == expected.epoch && current.seed_state == expected.seed_state
            && current.warmup_draws == expected.warmup_draws
        ? Status::success() : Status::failure(FailureCode::RestorePreflightFailed);
}

Status Sc6CandidateCheckpointCapture::RestoreBattleAudioSelectorForPresentation(
    const Snapshot& snapshot) noexcept
{
    if (!regions_->IsBound() || bound_manager_ == 0
        || snapshot.coordinate.generation != bound_round_generation_)
        return Status::failure(FailureCode::GenerationMismatch);
    const Status decoded = CandidateCheckpointCodec::Decode(
        snapshot, *auxiliary_decode_scratch_);
    if (!decoded.ok()) return decoded;
    return battle_audio_selector_->RestoreTransactional(
        auxiliary_decode_scratch_->battle_audio_selector);
}

Status Sc6CandidateCheckpointCapture::RestoreInputLogForReplay(
    const Snapshot& snapshot) noexcept
{
    const Status decoded = CandidateCheckpointCodec::Decode(
        snapshot, *auxiliary_decode_scratch_);
    if (!decoded.ok()) return decoded;
    return regions_->RestoreInputLogTransactional(
        auxiliary_decode_scratch_->native);
}

namespace
{
Status QueryNativeTutorialProvider(void* user, std::uintptr_t owner,
    std::uint32_t& selected) noexcept
{
    const auto image_base = reinterpret_cast<std::uintptr_t>(user);
    selected = 0;
    __try
    {
        using Active = bool (__fastcall*)(std::uintptr_t, std::int32_t);
        using State = bool (__fastcall*)(std::uintptr_t, std::int32_t, std::uint32_t);
        if (reinterpret_cast<Active>(image_base + 0x426890)(owner, 0))
            for (std::uint32_t index = 1; index <= 2; ++index)
                if (reinterpret_cast<State>(image_base + 0x426780)(owner, 0, index))
                { selected = index; break; }
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    { return Status::failure(FailureCode::AdvanceFailed); }
}
}

Status Sc6CandidateCheckpointCapture::ReplayTutorialConsumer(
    const TutorialConsumerObservation& expected, bool verify_recorded,
    TutorialConsumerObservation& observed) noexcept
{
    if (!OwnsTutorialConsumer(expected.before.owner) || !expected.parent.inert)
        return Status::failure(FailureCode::RestorePreflightFailed);
    std::uint32_t native_frame{};
    if (expected.thread_id != ::GetCurrentThreadId()
        || !memory_->Read(image_base_ + Schema::Sc6FrameLayout::frame_counter_rva,
            std::as_writable_bytes(std::span{&native_frame, 1}))
        || native_frame != expected.native_frame)
        return Status::failure(FailureCode::IdentityMismatch);
    TutorialParentGuard current{};
    if (!DeterministicHookSet::CaptureTutorialParentGuard(
            reinterpret_cast<void*>(bound_move_dispatch_), image_base_, current,
            expected.parent.receive_tick)
        || !current.inert || current.actor_class != expected.parent.actor_class
        || current.receive_tick != expected.parent.receive_tick
        || current.object_index != expected.parent.object_index
        || current.object_serial != expected.parent.object_serial
        || current.latent_manager != expected.parent.latent_manager)
        return Status::failure(FailureCode::RestorePreflightFailed);
    return move_dispatch_->ReplayIdleConsumer(expected, QueryNativeTutorialProvider,
        reinterpret_cast<void*>(image_base_), verify_recorded, observed);
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

void Sc6CandidateCheckpointCapture::DiscardHistoryBefore(
    FrameCoordinate minimum) noexcept
{
    landing_snapshots_.DiscardBeforeRetainingNearest(minimum);
    batch_entry_snapshots_.DiscardBeforeRetainingNearest(minimum);
}

void Sc6CandidateCheckpointCapture::ReleaseHistoryStorage() noexcept
{
    InvalidateHistory();
    landing_snapshots_.ReleasePrewarmedCopySlots();
    batch_entry_snapshots_.ReleasePrewarmedCopySlots();
    landing_capture_scratch_ = {};
    batch_entry_capture_scratch_ = {};
    if (auxiliary_decode_scratch_ != nullptr)
        *auxiliary_decode_scratch_ = {};
    callback_topology_scratch_ = {};
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
    transient_identity_issue_ = 0;
    transient_identity_expected_ = 0;
    transient_identity_observed_ = 0;
    transient_capture_phase_ = CandidateCapturePhase::None;
}

void Sc6CandidateCheckpointCapture::ReleaseBindingStorage() noexcept
{
    ReleaseBinding();
    adapter_->ReleaseScratchStorage();
    regions_->ReleaseScratchStorage();
    motion_banks_->ReleaseScratchStorage();
    move_dispatch_->ReleaseScratchStorage();
}

void Sc6CandidateCheckpointCapture::Reset() noexcept
{
    ReleaseHistoryStorage();
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

Status Sc6CandidateCheckpointCapture::ReplaceCorrectionSnapshots(
    std::span<Snapshot> landing_replacements,
    std::span<const CanonicalHash> expected_landing_hashes,
    std::span<Snapshot> batch_entry_replacements,
    std::span<const CanonicalHash> expected_batch_entry_hashes) noexcept
{
    const Status status = ValidateCorrectionSnapshots(landing_replacements,
        expected_landing_hashes, batch_entry_replacements,
        expected_batch_entry_hashes);
    if (!status.ok()) return status;
    landing_snapshots_.CommitValidatedExactReplacement(landing_replacements);
    batch_entry_snapshots_.CommitValidatedExactReplacement(
        batch_entry_replacements);
    return Status::success();
}

Status Sc6CandidateCheckpointCapture::ValidateCorrectionSnapshots(
    std::span<const Snapshot> landing_replacements,
    std::span<const CanonicalHash> expected_landing_hashes,
    std::span<const Snapshot> batch_entry_replacements,
    std::span<const CanonicalHash> expected_batch_entry_hashes) const noexcept
{
    Status status = landing_snapshots_.ValidateExactReplacement(
        landing_replacements, expected_landing_hashes);
    if (!status.ok()) return status;
    return batch_entry_snapshots_.ValidateExactReplacement(
        batch_entry_replacements, expected_batch_entry_hashes);
}

NativeCandidateValidationDiagnostic
Sc6CandidateCheckpointCapture::restore_validation() const noexcept
{
    return regions_->validation_diagnostic();
}

std::uint32_t Sc6CandidateCheckpointCapture::restore_difference_mask()
    const noexcept
{
    return adapter_ == nullptr ? 0 : adapter_->last_restore_difference_mask();
}

std::uint32_t Sc6CandidateCheckpointCapture::restore_operation_failure_mask()
    const noexcept
{
    return adapter_ == nullptr
        ? 0 : adapter_->last_restore_operation_failure_mask();
}

CandidateAdapterPerformanceStatus
Sc6CandidateCheckpointCapture::adapter_performance() const noexcept
{
    return adapter_->performance_status();
}
}
