#include "StageBreakListenerDiagnostics.hpp"

#include <Windows.h>

#include <cstring>
#include <iomanip>
#include <utility>

namespace Horse::Deterministic
{
namespace
{
constexpr std::size_t maximum_actors = 64;
constexpr std::size_t maximum_listeners_per_actor = 32;
constexpr std::size_t listener_entry_size = 0x40;
constexpr std::uint32_t weak_stage_break_delegate_callback_rva = 0x41D870;

template <typename T>
bool read_value(INativeMemory& memory, std::uintptr_t address, T& output) noexcept
{
    return memory.Read(address, std::as_writable_bytes(std::span{&output, 1}));
}

bool range_in_image(
    std::uintptr_t value,
    std::size_t extent,
    std::uintptr_t image_base,
    std::size_t image_size) noexcept
{
    return value >= image_base && extent <= image_size
        && value - image_base <= image_size - extent;
}

std::uint64_t append_hash(std::uint64_t hash, const void* data, std::size_t size) noexcept
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index)
    {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

struct ActorLayout
{
    std::size_t emitter_offset{};
    std::size_t id_offset{};
};

bool actor_layout(StageBreakActorKind kind, ActorLayout& output) noexcept
{
    switch (kind)
    {
    case StageBreakActorKind::Wall:
        output = {0x3B0, 0x450};
        return true;
    case StageBreakActorKind::Barrier:
        output = {0x390, 0x420};
        return true;
    }
    return false;
}

Status capture_actor(
    INativeMemory& memory,
    std::uintptr_t image_base,
    std::size_t image_size,
    const StageBreakActorRef& actor,
    std::uint16_t actor_order,
    std::int32_t& captured_actor_id,
    StageBreakListenerTopology& output,
    StageBreakListenerProbeFailure& failure) noexcept
{
    failure.kind = actor.kind;
    failure.actor_order = actor_order;
    ActorLayout layout{};
    if (actor.address == 0 || (actor.address & 7) != 0
        || !actor_layout(actor.kind, layout))
    {
        failure.fault = StageBreakListenerProbeFault::ActorReference;
        return Status::failure(FailureCode::InvalidConfiguration);
    }

    const auto emitter = actor.address + layout.emitter_offset;
    if (emitter < actor.address)
    {
        failure.fault = StageBreakListenerProbeFault::ActorLayoutOverflow;
        return Status::failure(FailureCode::IdentityMismatch);
    }
    std::int32_t actor_id{};
    std::uintptr_t heap_entries{};
    std::int32_t count{};
    std::int32_t capacity{};
    if (!read_value(memory, actor.address + layout.id_offset, actor_id)
        || !read_value(memory, emitter + 0x40, heap_entries)
        || !read_value(memory, emitter + 0x50, count)
        || !read_value(memory, emitter + 0x54, capacity))
    {
        failure.fault = StageBreakListenerProbeFault::ActorRead;
        return Status::failure(FailureCode::CaptureFailed);
    }
    failure.actor_id = actor_id;
    failure.listener_count = count;
    failure.listener_capacity = capacity;
    if (actor_id < 0 || count < 0
        || count > static_cast<std::int32_t>(maximum_listeners_per_actor)
        || capacity < count || capacity > static_cast<std::int32_t>(maximum_listeners_per_actor)
        || (heap_entries == 0 && count > 1)
        || (heap_entries != 0 && (heap_entries & 7) != 0))
    {
        failure.fault = StageBreakListenerProbeFault::CollectionBounds;
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    captured_actor_id = actor_id;

    const auto entries = heap_entries != 0 ? heap_entries : emitter;
    for (std::int32_t reverse = count; reverse > 0; --reverse)
    {
        const auto slot_index = static_cast<std::uint16_t>(reverse - 1);
        const auto entry = entries + slot_index * listener_entry_size;
        if (entry < entries)
        {
            failure.fault = StageBreakListenerProbeFault::EntryAddressOverflow;
            failure.slot_index = slot_index;
            return Status::failure(FailureCode::IdentityMismatch);
        }
        std::int32_t active{};
        std::uintptr_t listener_override{};
        if (!read_value(memory, entry + 0x30, active)
            || !read_value(memory, entry + 0x20, listener_override))
        {
            failure.fault = StageBreakListenerProbeFault::EntryRead;
            failure.slot_index = slot_index;
            return Status::failure(FailureCode::CaptureFailed);
        }
        if (active == 0) continue;
        const auto listener = listener_override != 0 ? listener_override : entry;
        failure.slot_index = slot_index;
        failure.listener_override_present = listener_override != 0;
        std::uintptr_t vtable{};
        std::uintptr_t callback{};
        if (!read_value(memory, listener, vtable))
        {
            failure.fault = StageBreakListenerProbeFault::ListenerVtableRead;
            return Status::failure(FailureCode::CaptureFailed);
        }
        if (!range_in_image(vtable, 0x70, image_base, image_size))
        {
            failure.fault = StageBreakListenerProbeFault::ListenerVtableOutsideImage;
            return Status::failure(FailureCode::IdentityMismatch);
        }
        if (!read_value(memory, vtable + 0x68, callback))
        {
            failure.fault = StageBreakListenerProbeFault::CallbackRead;
            return Status::failure(FailureCode::CaptureFailed);
        }
        if (!range_in_image(callback, 1, image_base, image_size))
        {
            failure.fault = StageBreakListenerProbeFault::CallbackOutsideImage;
            return Status::failure(FailureCode::IdentityMismatch);
        }
        auto bound_callback_rva = no_bound_stage_break_callback;
        if (callback - image_base == weak_stage_break_delegate_callback_rva)
        {
            std::uintptr_t bound_callback{};
            if (!read_value(memory, listener + 0x10, bound_callback))
            {
                failure.fault = StageBreakListenerProbeFault::BoundCallbackRead;
                return Status::failure(FailureCode::CaptureFailed);
            }
            if (!range_in_image(bound_callback, 1, image_base, image_size))
            {
                failure.fault = StageBreakListenerProbeFault::BoundCallbackOutsideImage;
                return Status::failure(FailureCode::IdentityMismatch);
            }
            bound_callback_rva = static_cast<std::uint32_t>(
                bound_callback - image_base);
        }
        output.listeners.push_back({
            actor.kind,
            actor_id,
            actor_order,
            static_cast<std::uint16_t>(count - reverse),
            slot_index,
            static_cast<std::uint32_t>(vtable - image_base),
            static_cast<std::uint32_t>(callback - image_base),
            bound_callback_rva,
        });
    }
    return Status::success();
}
}

std::string_view stage_break_listener_probe_fault_name(
    StageBreakListenerProbeFault fault) noexcept
{
    switch (fault)
    {
    case StageBreakListenerProbeFault::None: return "none";
    case StageBreakListenerProbeFault::ActorReference: return "actor_reference";
    case StageBreakListenerProbeFault::ActorLayoutOverflow: return "actor_layout_overflow";
    case StageBreakListenerProbeFault::ActorRead: return "actor_read";
    case StageBreakListenerProbeFault::CollectionBounds: return "collection_bounds";
    case StageBreakListenerProbeFault::EntryAddressOverflow: return "entry_address_overflow";
    case StageBreakListenerProbeFault::EntryRead: return "entry_read";
    case StageBreakListenerProbeFault::ListenerVtableRead: return "listener_vtable_read";
    case StageBreakListenerProbeFault::ListenerVtableOutsideImage:
        return "listener_vtable_outside_image";
    case StageBreakListenerProbeFault::CallbackRead: return "callback_read";
    case StageBreakListenerProbeFault::CallbackOutsideImage: return "callback_outside_image";
    case StageBreakListenerProbeFault::BoundCallbackRead: return "bound_callback_read";
    case StageBreakListenerProbeFault::BoundCallbackOutsideImage:
        return "bound_callback_outside_image";
    }
    return "unknown";
}

StageBreakListenerTopologyProbe::StageBreakListenerTopologyProbe(
    INativeMemory& memory) noexcept
    : memory_(memory)
{
}

Status StageBreakListenerTopologyProbe::Capture(
    std::uintptr_t image_base,
    std::size_t image_size,
    std::span<const StageBreakActorRef> actors,
    StageBreakListenerTopology& output,
    StageBreakListenerProbeFailure* failure) noexcept
{
    output = {};
    if (failure != nullptr) *failure = {};
    if (image_base == 0 || image_size == 0 || actors.empty())
        return Status::failure(FailureCode::ContextUnavailable);
    if (actors.size() > maximum_actors)
        return Status::failure(FailureCode::CapacityExceeded);
    try
    {
        output.actors.reserve(actors.size());
        output.listeners.reserve(actors.size() * 2);
        StageBreakListenerProbeFailure captured_failure{};
        for (std::size_t index = 0; index < actors.size(); ++index)
        {
            captured_failure = {};
            std::int32_t actor_id{};
            const auto status = capture_actor(memory_, image_base, image_size,
                actors[index], static_cast<std::uint16_t>(index), actor_id, output,
                captured_failure);
            if (!status.ok())
            {
                if (failure != nullptr) *failure = captured_failure;
                output = {};
                return status;
            }
            auto repeated_reference_of = no_repeated_actor_reference;
            for (std::size_t prior = 0; prior < index; ++prior)
            {
                if (actors[prior].kind == actors[index].kind
                    && actors[prior].address == actors[index].address)
                {
                    repeated_reference_of = static_cast<std::uint16_t>(prior);
                    break;
                }
            }
            output.actors.push_back({actors[index].kind, actor_id,
                static_cast<std::uint16_t>(index), repeated_reference_of});
        }
        const auto actor_count = static_cast<std::uint32_t>(output.actors.size());
        std::uint64_t signature = 1469598103934665603ull;
        signature = append_hash(signature, &actor_count, sizeof(actor_count));
        for (const auto& identity : output.actors)
        {
            const auto kind_value = static_cast<std::uint8_t>(identity.kind);
            signature = append_hash(signature, &kind_value, sizeof(kind_value));
            signature = append_hash(signature, &identity.actor_id,
                sizeof(identity.actor_id));
            signature = append_hash(signature, &identity.actor_order,
                sizeof(identity.actor_order));
            signature = append_hash(signature, &identity.repeated_reference_of,
                sizeof(identity.repeated_reference_of));
        }
        for (const auto& record : output.listeners)
        {
            const auto kind_value = static_cast<std::uint8_t>(record.kind);
            signature = append_hash(signature, &kind_value, sizeof(kind_value));
            signature = append_hash(signature, &record.actor_id, sizeof(record.actor_id));
            signature = append_hash(signature, &record.actor_order, sizeof(record.actor_order));
            signature = append_hash(signature, &record.dispatch_order,
                sizeof(record.dispatch_order));
            signature = append_hash(signature, &record.slot_index, sizeof(record.slot_index));
            signature = append_hash(signature, &record.listener_vtable_rva,
                sizeof(record.listener_vtable_rva));
            signature = append_hash(signature, &record.callback_rva,
                sizeof(record.callback_rva));
            signature = append_hash(signature, &record.bound_callback_rva,
                sizeof(record.bound_callback_rva));
        }
        output.signature = signature;
        return Status::success();
    }
    catch (...)
    {
        output = {};
        return Status::failure(FailureCode::CapacityExceeded);
    }
}

class StageBreakListenerRuntimeDiagnostics::ProcessMemory final : public INativeMemory
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
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool Write(std::uintptr_t, std::span<const std::byte>) noexcept override
    {
        return false;
    }

    bool ReadImageSize(std::uintptr_t image_base, std::size_t& image_size) noexcept
    {
        __try
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image_base);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                image_base + static_cast<std::uintptr_t>(dos->e_lfanew));
            if (dos->e_magic != IMAGE_DOS_SIGNATURE
                || nt->Signature != IMAGE_NT_SIGNATURE)
            {
                return false;
            }
            image_size = nt->OptionalHeader.SizeOfImage;
            return image_size != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
};

StageBreakListenerRuntimeDiagnostics::StageBreakListenerRuntimeDiagnostics(
    std::filesystem::path report_path)
    : report_path_(std::move(report_path)),
      memory_(std::make_unique<ProcessMemory>()),
      probe_(std::make_unique<StageBreakListenerTopologyProbe>(*memory_))
{
}

StageBreakListenerRuntimeDiagnostics::~StageBreakListenerRuntimeDiagnostics()
{
    Finish();
}

void StageBreakListenerRuntimeDiagnostics::write_header()
{
    std::filesystem::create_directories(report_path_.parent_path());
    report_.open(report_path_, std::ios::out | std::ios::trunc);
    if (!report_) return;
    report_ << "# Stage-break listener topology diagnostic\n\n"
            << "This is local reverse-engineering evidence, not qualification. "
               "Only stable actor IDs and module RVAs are recorded; native pointers are omitted.\n";
    header_written_ = true;
}

void StageBreakListenerRuntimeDiagnostics::write_topology(
    std::uint64_t frame,
    const StageBreakListenerTopology& topology)
{
    if (!report_) return;
    report_ << "\n## Frame " << frame << " signature `0x" << std::hex
            << topology.signature << std::dec << "`\n\n"
            << "Actors: " << topology.actors.size() << ".\n\n"
            << "| Kind | Actor ID | Actor order | Repeated reference of |\n"
            << "|---|---:|---:|---:|\n";
    for (const auto& actor : topology.actors)
    {
        report_ << "| "
                << (actor.kind == StageBreakActorKind::Wall ? "wall" : "barrier")
                << " | " << actor.actor_id << " | " << actor.actor_order << " | ";
        if (actor.repeated_reference_of == no_repeated_actor_reference)
            report_ << "-";
        else
            report_ << actor.repeated_reference_of;
        report_ << " |\n";
    }
    report_ << "\n"
            << "| Kind | Actor ID | Actor order | Dispatch order | Slot | Vtable RVA | Callback RVA | Bound callback RVA |\n"
            << "|---|---:|---:|---:|---:|---:|---:|---:|\n";
    for (const auto& listener : topology.listeners)
    {
        report_ << "| "
                << (listener.kind == StageBreakActorKind::Wall ? "wall" : "barrier")
                << " | " << listener.actor_id << " | " << listener.actor_order
                << " | " << listener.dispatch_order << " | " << listener.slot_index
                << " | `0x" << std::hex << listener.listener_vtable_rva
                << "` | `0x" << listener.callback_rva << "` | ";
        if (listener.bound_callback_rva == no_bound_stage_break_callback)
            report_ << "-";
        else
            report_ << "`0x" << listener.bound_callback_rva << "`";
        report_ << std::dec << " |\n";
    }
    report_.flush();
}

void StageBreakListenerRuntimeDiagnostics::write_failure(
    std::uint64_t frame,
    Status status,
    const StageBreakListenerProbeFailure& failure)
{
    if (!report_) return;
    report_ << "\n## Capture failure at frame " << frame << "\n\n"
            << "- Status: `" << failure_code_name(status.code) << "`\n"
            << "- Probe fault: `"
            << stage_break_listener_probe_fault_name(failure.fault) << "`\n"
            << "- Actor kind: `"
            << (failure.kind == StageBreakActorKind::Wall ? "wall" : "barrier")
            << "`\n"
            << "- Actor order: " << failure.actor_order << "\n"
            << "- Actor ID: " << failure.actor_id << "\n"
            << "- Listener count/capacity: " << failure.listener_count
            << "/" << failure.listener_capacity << "\n"
            << "- Listener slot: " << failure.slot_index << "\n"
            << "- Override object present: "
            << (failure.listener_override_present ? "yes" : "no") << "\n";
    report_.flush();
}

Status StageBreakListenerRuntimeDiagnostics::Observe(
    std::uintptr_t image_base,
    std::uint64_t frame,
    std::span<const StageBreakActorRef> actors) noexcept
{
    if (finished_ || samples_ >= maximum_samples) return Status::success();
    if (frame == last_frame_)
        return Status::failure(FailureCode::ContextUnavailable);
    std::size_t image_size{};
    if (!memory_->ReadImageSize(image_base, image_size))
        return Status::failure(FailureCode::ContextUnavailable);
    StageBreakListenerTopology topology{};
    StageBreakListenerProbeFailure failure{};
    const auto status = probe_->Capture(
        image_base, image_size, actors, topology, &failure);
    if (!status.ok())
    {
        if (!header_written_) write_header();
        write_failure(frame, status, failure);
        Finish();
        return status;
    }
    if (!header_written_) write_header();
    if (!report_) return Status::failure(FailureCode::CaptureFailed);
    last_frame_ = frame;
    ++samples_;
    if (topology.signature != last_signature_)
    {
        last_signature_ = topology.signature;
        write_topology(frame, topology);
    }
    if (samples_ >= maximum_samples) Finish();
    return Status::success();
}

void StageBreakListenerRuntimeDiagnostics::Finish() noexcept
{
    if (finished_) return;
    finished_ = true;
    if (report_)
    {
        report_ << "\nObserved frames: " << samples_ << ".\n";
        report_.close();
    }
}

bool StageBreakListenerRuntimeDiagnostics::complete() const noexcept
{
    return finished_ || samples_ >= maximum_samples;
}
}
