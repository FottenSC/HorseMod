#include "StageWindGraphTransaction.hpp"

#include <array>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace Horse::Deterministic
{
namespace
{
constexpr std::size_t root_size = 0xF0;
constexpr std::size_t max_nodes = 64;

template <typename T>
bool read_value(INativeMemory& memory, std::uintptr_t address, T& output) noexcept
{
    return memory.Read(address, std::as_writable_bytes(std::span{&output, 1}));
}

template <typename T>
void store(std::vector<std::byte>& bytes, std::size_t offset, const T& value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

bool collect_existing_nodes(
    INativeMemory& memory, std::uintptr_t root, std::uintptr_t image_base,
    std::size_t image_size, std::vector<std::uintptr_t>& output) noexcept
{
    std::uintptr_t node{};
    if (!read_value(memory, root, node)) return false;
    std::uintptr_t previous{};
    while (node != 0)
    {
        if (output.size() == max_nodes) return false;
        for (const auto visited : output)
            if (visited == node) return false;
        std::uintptr_t vtable{}, next{}, node_previous{}, node_root{};
        if (!read_value(memory, node, vtable)
            || !read_value(memory, node + 0x10, next)
            || !read_value(memory, node + 0x18, node_previous)
            || !read_value(memory, node + 0x28, node_root)
            || node_previous != previous || node_root != root
            || vtable < image_base || vtable - image_base >= image_size
            || vtable - image_base > std::numeric_limits<std::uint32_t>::max()
            || FindStageWindNodeLayoutByVtable(
                static_cast<std::uint32_t>(vtable - image_base)) == nullptr)
        {
            return false;
        }
        output.push_back(node);
        previous = node;
        node = next;
    }
    return true;
}

bool scatter_semantic_state(
    std::vector<std::byte>& bytes, const StageWindNodeLayout& layout,
    std::span<const std::byte> state) noexcept
{
    if (state.size() != StageWindSemanticStateSize(layout)) return false;
    std::size_t cursor{};
    const auto scatter = [&](std::span<const StageWindStateRange> ranges) {
        for (const auto range : ranges)
        {
            if (range.offset > bytes.size() || range.size > bytes.size() - range.offset)
                return false;
            std::memcpy(bytes.data() + range.offset, state.data() + cursor, range.size);
            cursor += range.size;
        }
        return true;
    };
    return scatter(StageWindCommonRanges()) && scatter(layout.class_ranges)
        && cursor == state.size();
}

bool scatter_ranges(
    std::vector<std::byte>& bytes, std::span<const StageWindStateRange> ranges,
    std::span<const std::byte> state) noexcept
{
    std::size_t expected{};
    for (const auto range : ranges) expected += range.size;
    if (state.size() != expected) return false;
    std::size_t cursor{};
    for (const auto range : ranges)
    {
        if (range.offset > bytes.size() || range.size > bytes.size() - range.offset)
            return false;
        std::memcpy(bytes.data() + range.offset, state.data() + cursor, range.size);
        cursor += range.size;
    }
    return true;
}

std::size_t range_bytes(std::span<const StageWindStateRange> ranges) noexcept
{
    std::size_t total{};
    for (const auto range : ranges) total += range.size;
    return total;
}

void free_all(IStageWindAllocator& allocator, std::span<const std::uintptr_t> nodes) noexcept
{
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) allocator.Free(*it);
}
}

StageWindGraphTransaction::StageWindGraphTransaction(
    INativeMemory& memory, IStageWindAllocator& allocator) noexcept
    : memory_(memory), allocator_(allocator)
{
}

Status StageWindGraphTransaction::Restore(
    const StageWindTopologyAddresses& addresses,
    const StageWindTopologyImage& target) noexcept
{
    if (addresses.image_base == 0 || addresses.image_size == 0
        || addresses.root_pointer == 0 || addresses.generation == 0
        || target.generation != addresses.generation
        || target.nodes.size() > max_nodes)
    {
        return Status::failure(FailureCode::RestorePreflightFailed);
    }
    for (const auto& node : target.nodes)
    {
        const auto* layout = FindStageWindNodeLayout(node.kind);
        if (layout == nullptr
            || node.semantic_state.size() != StageWindSemanticStateSize(*layout)
            || node.derived_state.size() != range_bytes(layout->derived_ranges))
            return Status::failure(FailureCode::RestorePreflightFailed);
    }
    for (const auto rva : target.pending_callback_rvas)
        if (rva != 0 && rva >= addresses.image_size)
            return Status::failure(FailureCode::RestorePreflightFailed);

    std::uintptr_t root{};
    if (!read_value(memory_, addresses.root_pointer, root) || root == 0)
        return Status::failure(FailureCode::ContextUnavailable);
    std::array<std::byte, root_size> undo_root{};
    if (!memory_.Read(root, undo_root))
        return Status::failure(FailureCode::CaptureFailed);
    std::vector<std::uintptr_t> old_nodes;
    if (!collect_existing_nodes(
            memory_, root, addresses.image_base, addresses.image_size, old_nodes))
        return Status::failure(FailureCode::IdentityMismatch);

    std::vector<std::uintptr_t> replacements;
    replacements.reserve(target.nodes.size());
    for (const auto& node : target.nodes)
    {
        const auto* layout = FindStageWindNodeLayout(node.kind);
        const auto replacement = allocator_.Allocate(layout->allocation_size);
        if (replacement == 0)
        {
            free_all(allocator_, replacements);
            return Status::failure(FailureCode::CapacityExceeded);
        }
        replacements.push_back(replacement);
    }

    for (std::size_t index = 0; index < replacements.size(); ++index)
    {
        const auto* layout = FindStageWindNodeLayout(target.nodes[index].kind);
        std::vector<std::byte> bytes(layout->allocation_size);
        const auto vtable = addresses.image_base + layout->vtable_rva;
        const auto next = index + 1 < replacements.size() ? replacements[index + 1] : 0;
        const auto previous = index == 0 ? 0 : replacements[index - 1];
        store(bytes, 0x00, vtable);
        store(bytes, 0x10, next);
        store(bytes, 0x18, previous);
        store(bytes, 0x28, root);
        if (!scatter_semantic_state(bytes, *layout, target.nodes[index].semantic_state)
            || !scatter_ranges(bytes, layout->derived_ranges,
                target.nodes[index].derived_state)
            || !memory_.Write(replacements[index], bytes))
        {
            free_all(allocator_, replacements);
            return Status::failure(FailureCode::RestoreWriteFailed);
        }
    }

    std::vector<std::byte> new_root(undo_root.begin(), undo_root.end());
    const auto head = replacements.empty() ? 0 : replacements.front();
    store(new_root, 0x00, head);
    std::memcpy(new_root.data() + 0x08, target.root_clock.data(), target.root_clock.size());
    std::array<std::uintptr_t, 16> callbacks{};
    for (std::size_t index = 0; index < callbacks.size(); ++index)
        if (target.pending_callback_rvas[index] != 0)
            callbacks[index] = addresses.image_base + target.pending_callback_rvas[index];
    std::memcpy(new_root.data() + 0x18, callbacks.data(), sizeof(callbacks));
    std::memcpy(new_root.data() + 0x98, target.schedule_state.data(), target.schedule_state.size());
    std::memcpy(new_root.data() + 0xB0, target.schedule_params.data(), target.schedule_params.size());
    std::memcpy(new_root.data() + 0xC0, target.output_force.data(), target.output_force.size());

    std::uintptr_t current_root{};
    if (!read_value(memory_, addresses.root_pointer, current_root) || current_root != root)
    {
        free_all(allocator_, replacements);
        return Status::failure(FailureCode::GenerationMismatch);
    }
    if (!memory_.Write(root, new_root))
    {
        free_all(allocator_, replacements);
        return Status::failure(FailureCode::RestoreWriteFailed);
    }

    StageWindTopologyProbe verifier(memory_);
    StageWindTopologyImage restored{};
    const auto bind_status = verifier.Bind(addresses);
    const auto verify_status = bind_status.ok() ? verifier.Capture(restored) : bind_status;
    if (!verify_status.ok() || restored != target)
    {
        if (!memory_.Write(root, undo_root))
            return Status::failure(FailureCode::UndoFailed);
        free_all(allocator_, replacements);
        return Status::failure(FailureCode::RestoreVerificationFailed);
    }

    free_all(allocator_, old_nodes);
    return Status::success();
}
}
