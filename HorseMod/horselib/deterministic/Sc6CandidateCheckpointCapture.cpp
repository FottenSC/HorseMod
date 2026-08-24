#include "Sc6CandidateCheckpointCapture.hpp"

#include "DeterministicHookSet.hpp"
#include "Schema.hpp"

#include <Windows.h>
#include <Unreal/FWeakObjectPtr.hpp>

#include <algorithm>
#include <cstring>
#include <span>

namespace Horse::Deterministic
{
namespace
{
constexpr std::uintptr_t fighter_roots_rva = 0x470DE90;
constexpr std::uintptr_t move_dispatch_filter_rva = 0x427940;
constexpr std::uintptr_t adjusted_weak_callback_vtable_rva = 0x3285198;
constexpr std::uintptr_t hgcpu_writer_rva = 0x3841E0;
constexpr std::uintptr_t pump_state_rva = 0x4100C70;
constexpr std::uintptr_t scheduler_base_rva = 0x4715400;
constexpr std::uintptr_t move_command_base_rva = 0x470F390;
constexpr std::uintptr_t slot_param_base_rva = 0x470E0C0;
constexpr std::ptrdiff_t input_filter_collection = 0x1210;
constexpr std::size_t callback_entry_size = 0x40;
constexpr std::size_t maximum_callback_entries = 64;

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

Sc6CandidateCheckpointCapture::Sc6CandidateCheckpointCapture()
    : memory_(std::make_unique<ProcessMemory>()),
      regions_(std::make_unique<NativeCandidateRegions>(*memory_))
{
}

Sc6CandidateCheckpointCapture::~Sc6CandidateCheckpointCapture() = default;

Status Sc6CandidateCheckpointCapture::Initialize(std::uintptr_t image_base) noexcept
{
    Reset();
    if (image_base == 0) return Status::failure(FailureCode::ContextUnavailable);
    image_base_ = image_base;
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
    const FrameFencepostObservation& observation,
    FrameCoordinate coordinate,
    std::uint64_t session_generation) noexcept
{
    std::uintptr_t move_dispatch{};
    std::array<std::uintptr_t, 2> fighter_roots{};
    const Status resolved = resolve_move_dispatch(
        observation.battle_manager, move_dispatch);
    if (!resolved.ok()) return resolved;
    if (!read_fighter_roots(fighter_roots))
        return Status::failure(FailureCode::ContextUnavailable);
    NativeCandidateAddresses addresses{
        image_base_,
        move_dispatch,
        image_base_ + pump_state_rva,
        image_base_ + scheduler_base_rva,
        image_base_ + move_command_base_rva,
        image_base_ + slot_param_base_rva,
        fighter_roots,
        session_generation,
        coordinate.generation,
    };
    const Status bound = regions_->Bind(addresses);
    if (!bound.ok()) return bound;
    bound_manager_ = observation.battle_manager;
    bound_move_dispatch_ = move_dispatch;
    bound_session_generation_ = session_generation;
    bound_round_generation_ = coordinate.generation;
    return Status::success();
}

Status Sc6CandidateCheckpointCapture::Capture(
    const FrameFencepostObservation& observation,
    FrameCoordinate coordinate,
    std::uint64_t session_generation) noexcept
{
    if (image_base_ == 0 || observation.battle_manager == 0
        || coordinate.generation == 0 || session_generation == 0)
    {
        return Status::failure(FailureCode::ContextUnavailable);
    }
    if (!regions_->IsBound() || bound_manager_ != observation.battle_manager
        || bound_session_generation_ != session_generation
        || bound_round_generation_ != coordinate.generation)
    {
        const Status rebound = bind(observation, coordinate, session_generation);
        if (!rebound.ok())
        {
            status_.failure = rebound.code;
            return rebound;
        }
    }

    NativeCandidateImage native{};
    Status captured = regions_->PreflightCapture();
    if (captured.ok()) captured = regions_->Capture(native);
    if (!captured.ok())
    {
        status_.failure = captured.code;
        return captured;
    }
    const HgCpuGenerationContext hgcpu_context{
        0xF8904E4B04BCA3B4ull,
        Schema::snapshot_schema_version,
        session_generation,
        coordinate.generation,
        {coordinate.generation * 2 - 1, coordinate.generation * 2},
        coordinate.generation,
    };
    HgCpuLocalImage hgcpu_image{};
    const auto writer = reinterpret_cast<HgCpuExecFn>(
        image_base_ + hgcpu_writer_rva);
    captured = hgcpu_.Capture(writer, hgcpu_context, hgcpu_image);
    if (!captured.ok())
    {
        status_.failure = captured.code;
        return captured;
    }
    Snapshot snapshot{};
    captured = CandidateCheckpointCodec::Encode(
        coordinate, session_generation, {std::move(native), std::move(hgcpu_image)},
        snapshot);
    if (captured.ok()) captured = snapshots_.Save(std::move(snapshot));
    if (!captured.ok())
    {
        status_.failure = captured.code;
        return captured;
    }
    status_.failure = FailureCode::None;
    status_.last_coordinate = coordinate;
    ++status_.captured;
    status_.bytes_used = snapshots_.BytesUsed();
    return Status::success();
}

void Sc6CandidateCheckpointCapture::ReleaseBinding() noexcept
{
    regions_->Invalidate();
    bound_manager_ = 0;
    bound_move_dispatch_ = 0;
    bound_session_generation_ = 0;
    bound_round_generation_ = 0;
}

void Sc6CandidateCheckpointCapture::Reset() noexcept
{
    ReleaseBinding();
    snapshots_.Clear();
    status_ = {};
    image_base_ = 0;
}

CandidateCheckpointCaptureStatus
Sc6CandidateCheckpointCapture::status() const noexcept
{
    return status_;
}

const SnapshotStore& Sc6CandidateCheckpointCapture::snapshots() const noexcept
{
    return snapshots_;
}
}
