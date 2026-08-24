#include "SecondaryEventState.hpp"

#include <cstring>

namespace Horse::Deterministic
{
namespace
{
constexpr std::size_t slot_stride = 0x18;
constexpr std::size_t pointer_block = 0x240;
constexpr std::size_t scalar_offset = 0x258;
constexpr std::size_t table_header_count = 0x14;
constexpr std::size_t header_stride = 8;
constexpr std::size_t header_cursor = 2;

template <typename T>
bool read_value(INativeMemory& memory, std::uintptr_t address, T& output) noexcept
{
    return memory.Read(address, std::as_writable_bytes(std::span{&output, 1}));
}

void append(std::vector<std::byte>& output, const void* data, std::size_t size)
{
    const auto* first = static_cast<const std::byte*>(data);
    output.insert(output.end(), first, first + size);
}
}

SecondaryEventState::SecondaryEventState(INativeMemory& memory) noexcept
    : memory_(memory)
{
}

void SecondaryEventState::Invalidate() noexcept
{
    fighters_ = {};
    topology_ = {};
    round_generation_ = 0;
    bound_ = false;
}

Status SecondaryEventState::Bind(
    const std::array<std::uintptr_t, 2>& fighters,
    std::uint64_t round_generation) noexcept
{
    Invalidate();
    if (fighters[0] == 0 || fighters[1] == 0 || round_generation == 0)
        return Status::failure(FailureCode::InvalidConfiguration);
    fighters_ = fighters;
    round_generation_ = round_generation;
    for (std::size_t player = 0; player < 2; ++player)
    {
        const auto stack = fighters[player] + secondary_event_stack_fighter_offset;
        auto& topology = topology_[player];
        std::int32_t count{};
        if (!read_value(memory_, stack + pointer_block, topology.table_header)
            || !read_value(memory_, stack + pointer_block + 8,
                topology.event_headers)
            || !read_value(memory_, stack + pointer_block + 0x10,
                topology.event_payloads)
            || topology.table_header == 0 || topology.event_headers == 0
            || topology.event_payloads == 0
            || !read_value(memory_, topology.table_header + table_header_count,
                count)
            || count < 0
            || count > static_cast<std::int32_t>(secondary_event_max_headers))
            return Status::failure(FailureCode::AdapterUnqualified);
        topology.header_count = static_cast<std::uint32_t>(count);
    }
    bound_ = true;
    return topology_matches() ? Status::success()
        : Status::failure(FailureCode::IdentityMismatch);
}

bool SecondaryEventState::topology_matches() noexcept
{
    if (!bound_) return false;
    for (std::size_t player = 0; player < 2; ++player)
    {
        const auto stack = fighters_[player] + secondary_event_stack_fighter_offset;
        std::uintptr_t table{}, headers{}, payloads{};
        std::int32_t count{};
        if (!read_value(memory_, stack + pointer_block, table)
            || !read_value(memory_, stack + pointer_block + 8, headers)
            || !read_value(memory_, stack + pointer_block + 0x10, payloads)
            || table == 0 || headers == 0 || payloads == 0
            || !read_value(memory_, table + table_header_count, count)
            || table != topology_[player].table_header
            || headers != topology_[player].event_headers
            || payloads != topology_[player].event_payloads
            || count < 0
            || static_cast<std::uint32_t>(count)
                != topology_[player].header_count) return false;
    }
    return true;
}

bool SecondaryEventState::Validate(
    const SecondaryEventStateImage& image) noexcept
{
    return image.round_generation != 0
        && image.header_counts[0] <= secondary_event_max_headers
        && image.header_counts[1] <= secondary_event_max_headers;
}

Status SecondaryEventState::capture_unchecked(
    SecondaryEventStateImage& output) noexcept
{
    output = {};
    if (!topology_matches())
        return Status::failure(FailureCode::IdentityMismatch);
    output.round_generation = round_generation_;
    for (std::size_t player = 0; player < 2; ++player)
    {
        const auto stack = fighters_[player] + secondary_event_stack_fighter_offset;
        output.header_counts[player] = topology_[player].header_count;
        for (std::size_t slot = 0; slot < secondary_event_slot_count; ++slot)
        {
            auto& target = output.slots[player][slot];
            const auto address = stack + slot * slot_stride;
            if (!memory_.Read(address, target.prefix)
                || !memory_.Read(address + 0x10, target.suffix))
                return Status::failure(FailureCode::CaptureFailed);
        }
        if (!memory_.Read(stack + scalar_offset, output.scalars[player]))
            return Status::failure(FailureCode::CaptureFailed);
        for (std::uint32_t index = 0;
             index < topology_[player].header_count; ++index)
        {
            if (!read_value(memory_, topology_[player].event_headers
                    + index * header_stride + header_cursor,
                output.header_cursors[player][index]))
                return Status::failure(FailureCode::CaptureFailed);
        }
    }
    return Status::success();
}

Status SecondaryEventState::Capture(SecondaryEventStateImage& output) noexcept
{
    return bound_ ? capture_unchecked(output)
        : Status::failure(FailureCode::AdapterUnqualified);
}

bool SecondaryEventState::write_unchecked(
    const SecondaryEventStateImage& image) noexcept
{
    if (!Validate(image) || image.round_generation != round_generation_
        || !topology_matches()
        || image.header_counts[0] != topology_[0].header_count
        || image.header_counts[1] != topology_[1].header_count) return false;
    for (std::size_t player = 0; player < 2; ++player)
    {
        const auto stack = fighters_[player] + secondary_event_stack_fighter_offset;
        for (std::size_t slot = 0; slot < secondary_event_slot_count; ++slot)
        {
            const auto& source = image.slots[player][slot];
            const auto address = stack + slot * slot_stride;
            if (!memory_.Write(address, source.prefix)
                || !memory_.Write(address + 0x10, source.suffix)) return false;
        }
        if (!memory_.Write(stack + scalar_offset, image.scalars[player]))
            return false;
        for (std::uint32_t index = 0;
             index < topology_[player].header_count; ++index)
        {
            if (!memory_.Write(topology_[player].event_headers
                    + index * header_stride + header_cursor,
                std::as_bytes(std::span{
                    &image.header_cursors[player][index], 1}))) return false;
        }
    }
    return true;
}

Status SecondaryEventState::RestoreTransactional(
    const SecondaryEventStateImage& image) noexcept
{
    if (!Validate(image) || image.round_generation != round_generation_
        || image.header_counts[0] != topology_[0].header_count
        || image.header_counts[1] != topology_[1].header_count
        || !topology_matches())
        return Status::failure(FailureCode::RestorePreflightFailed);
    SecondaryEventStateImage undo{};
    if (!capture_unchecked(undo).ok())
        return Status::failure(FailureCode::CaptureFailed);
    if (write_unchecked(image))
    {
        SecondaryEventStateImage observed{};
        if (capture_unchecked(observed).ok() && observed == image)
            return Status::success();
    }
    const bool undone = write_unchecked(undo);
    SecondaryEventStateImage verified{};
    if (!undone || !capture_unchecked(verified).ok() || verified != undo)
        return Status::failure(FailureCode::UndoFailed);
    return Status::failure(FailureCode::RestoreWriteFailed);
}

std::vector<std::byte> SecondaryEventState::CanonicalBytes(
    const SecondaryEventStateImage& image)
{
    std::vector<std::byte> output;
    output.reserve(0x800);
    append(output, &image.round_generation, sizeof(image.round_generation));
    for (std::size_t player = 0; player < 2; ++player)
    {
        for (const auto& slot : image.slots[player])
        {
            append(output, slot.prefix.data(), slot.prefix.size());
            append(output, slot.suffix.data(), slot.suffix.size());
        }
        append(output, image.scalars[player].data(), image.scalars[player].size());
        append(output, &image.header_counts[player],
            sizeof(image.header_counts[player]));
        append(output, image.header_cursors[player].data(),
            image.header_counts[player] * sizeof(std::uint16_t));
    }
    return output;
}

Status SecondaryEventState::DecodeCanonicalBytes(
    std::span<const std::byte> bytes, SecondaryEventStateImage& output) noexcept
{
    output = {};
    std::size_t cursor{};
    const auto take = [&bytes, &cursor](void* destination, std::size_t size) {
        if (size > bytes.size() - (cursor < bytes.size() ? cursor : bytes.size()))
            return false;
        std::memcpy(destination, bytes.data() + cursor, size);
        cursor += size;
        return true;
    };
    if (!take(&output.round_generation, sizeof(output.round_generation)))
        return Status::failure(FailureCode::CaptureFailed);
    for (std::size_t player = 0; player < 2; ++player)
    {
        for (auto& slot : output.slots[player])
        {
            if (!take(slot.prefix.data(), slot.prefix.size())
                || !take(slot.suffix.data(), slot.suffix.size()))
                return Status::failure(FailureCode::CaptureFailed);
        }
        if (!take(output.scalars[player].data(), output.scalars[player].size())
            || !take(&output.header_counts[player],
                sizeof(output.header_counts[player]))
            || output.header_counts[player] > secondary_event_max_headers
            || !take(output.header_cursors[player].data(),
                output.header_counts[player] * sizeof(std::uint16_t)))
            return Status::failure(FailureCode::CaptureFailed);
    }
    return cursor == bytes.size() && Validate(output)
        ? Status::success() : Status::failure(FailureCode::CaptureFailed);
}
}
