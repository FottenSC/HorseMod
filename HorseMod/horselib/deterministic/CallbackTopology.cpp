#include "CallbackTopology.hpp"

#include <limits>

namespace Horse::Deterministic
{
namespace
{
constexpr std::size_t maximum_collections = 5;
constexpr std::size_t maximum_entries_per_collection = 64;
constexpr std::size_t callback_entry_size = 0x40;

struct WeakCallbackPrefix
{
    std::uintptr_t vtable{};
    std::int32_t object_index{};
    std::int32_t serial_number{};
    std::uintptr_t callback{};
};
static_assert(sizeof(WeakCallbackPrefix) == 0x18);

template <typename T>
bool read_value(INativeMemory& memory, std::uintptr_t address, T& output) noexcept
{
    return memory.Read(address, std::as_writable_bytes(std::span{&output, 1}));
}

bool in_image(
    std::uintptr_t value,
    std::uintptr_t image_base,
    std::size_t image_size) noexcept
{
    return value >= image_base && value - image_base < image_size;
}

void append_hash(std::uint64_t& hash, const void* data, std::size_t size) noexcept
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index)
    {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
}

Status capture_collection(
    INativeMemory& memory,
    std::uintptr_t image_base,
    std::size_t image_size,
    const CallbackCollectionRef& collection,
    ResolveCallbackOwnerFn resolve_owner,
    void* resolve_user,
    CallbackTopology& output,
    std::uint64_t& signature) noexcept
{
    std::uintptr_t heap_entries{};
    std::int32_t count{};
    std::int32_t capacity{};
    if (collection.address == 0 || (collection.address & 7) != 0
        || !read_value(memory, collection.address + 0x40, heap_entries)
        || !read_value(memory, collection.address + 0x50, count)
        || !read_value(memory, collection.address + 0x54, capacity))
    {
        return Status::failure(FailureCode::CapturePreflightFailed);
    }
    if (count < 0 || capacity < count
        || capacity > static_cast<std::int32_t>(maximum_entries_per_collection)
        || (heap_entries == 0 && count > 1)
        || (heap_entries != 0 && (heap_entries & 7) != 0))
    {
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    const auto role = static_cast<std::uint8_t>(collection.role);
    append_hash(signature, &role, sizeof(role));
    append_hash(signature, &count, sizeof(count));
    const auto entries = heap_entries != 0 ? heap_entries : collection.address;
    for (std::int32_t index = 0; index < count; ++index)
    {
        const auto entry = entries + index * callback_entry_size;
        if (entry < entries) return Status::failure(FailureCode::IdentityMismatch);
        std::int32_t active{};
        std::uintptr_t callback_override{};
        if (!read_value(memory, entry + 0x30, active)
            || !read_value(memory, entry + 0x20, callback_override))
        {
            return Status::failure(FailureCode::CapturePreflightFailed);
        }
        if (active == 0) return Status::failure(FailureCode::IdentityMismatch);
        const auto callback_address = callback_override != 0
            ? callback_override : entry;
        if ((callback_address & 7) != 0)
            return Status::failure(FailureCode::IdentityMismatch);
        WeakCallbackPrefix callback{};
        if (!read_value(memory, callback_address, callback)
            || !in_image(callback.vtable, image_base, image_size)
            || !in_image(callback.callback, image_base, image_size))
        {
            return Status::failure(FailureCode::IdentityMismatch);
        }
        std::uint64_t owner_class_token{};
        const Status owner = resolve_owner(resolve_user, callback.object_index,
            callback.serial_number, owner_class_token);
        if (!owner.ok() || owner_class_token == 0) return owner.ok()
            ? Status::failure(FailureCode::IdentityMismatch) : owner;
        CallbackTopologyRecord record{
            collection.role,
            static_cast<std::uint16_t>(index),
            static_cast<std::uint16_t>(count - index - 1),
            callback.object_index,
            callback.serial_number,
            owner_class_token,
            static_cast<std::uint32_t>(callback.vtable - image_base),
            static_cast<std::uint32_t>(callback.callback - image_base),
        };
        output.records.push_back(record);
        append_hash(signature, &record.collection_order,
            sizeof(record.collection_order));
        append_hash(signature, &record.dispatch_order, sizeof(record.dispatch_order));
        append_hash(signature, &record.owner_object_index,
            sizeof(record.owner_object_index));
        append_hash(signature, &record.owner_serial_number,
            sizeof(record.owner_serial_number));
        append_hash(signature, &record.owner_class_token,
            sizeof(record.owner_class_token));
        append_hash(signature, &record.wrapper_vtable_rva,
            sizeof(record.wrapper_vtable_rva));
        append_hash(signature, &record.callback_rva, sizeof(record.callback_rva));
    }
    return Status::success();
}
}

CallbackTopologyProbe::CallbackTopologyProbe(INativeMemory& memory) noexcept
    : memory_(memory)
{
}

Status CallbackTopologyProbe::Capture(
    std::uintptr_t image_base,
    std::size_t image_size,
    std::span<const CallbackCollectionRef> collections,
    ResolveCallbackOwnerFn resolve_owner,
    void* resolve_user,
    CallbackTopology& output) noexcept
{
    output = {};
    if (image_base == 0 || image_size == 0
        || image_size > std::numeric_limits<std::uint32_t>::max()
        || collections.empty()
        || collections.size() > maximum_collections || resolve_owner == nullptr)
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    try
    {
        output.records.reserve(collections.size() * 8);
        std::uint64_t signature = 1469598103934665603ull;
        for (const auto& collection : collections)
        {
            const Status captured = capture_collection(memory_, image_base, image_size,
                collection, resolve_owner, resolve_user, output, signature);
            if (!captured.ok())
            {
                output = {};
                return captured;
            }
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
}
