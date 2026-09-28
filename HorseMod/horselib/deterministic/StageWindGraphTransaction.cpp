#include "StageWindGraphTransaction.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>

namespace Horse::Deterministic
{
namespace
{
constexpr std::size_t root_size = 0xF0;
constexpr std::size_t max_nodes = 64;
constexpr std::size_t common_derived_state_size = 0x20;

template <typename T>
bool read_value(INativeMemory& memory, std::uintptr_t address, T& output) noexcept
{
    return memory.Read(address, std::as_writable_bytes(std::span{&output, 1}));
}

template <typename T>
void store(std::span<std::byte> bytes, std::size_t offset, const T& value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

bool collect_existing_nodes(
    INativeMemory& memory, std::uintptr_t root, std::uintptr_t image_base,
    std::size_t image_size, std::array<std::uintptr_t, max_nodes>& output,
    std::size_t& count) noexcept
{
    count = 0;
    std::uintptr_t node{};
    if (!read_value(memory, root, node)) return false;
    std::uintptr_t previous{};
    while (node != 0)
    {
        if (count == output.size()) return false;
        for (std::size_t index = 0; index < count; ++index)
            if (output[index] == node) return false;
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
        output[count++] = node;
        previous = node;
        node = next;
    }
    return true;
}

bool scatter_semantic_state(
    std::span<std::byte> bytes, const StageWindNodeLayout& layout,
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
    std::span<std::byte> bytes, std::span<const StageWindStateRange> ranges,
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

void free_all(IStageWindAllocator& allocator, std::span<const std::uintptr_t> nodes) noexcept
{
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) allocator.Free(*it);
}

bool build_node_bytes(
    std::span<std::byte> bytes, const StageWindNodeLayout& layout,
    const StageWindNodeImage& node, std::uintptr_t image_base,
    std::uintptr_t root, std::uintptr_t previous,
    std::uintptr_t next) noexcept
{
    if (bytes.size() != layout.allocation_size)
        return false;
    std::fill(bytes.begin(), bytes.end(), std::byte{});
    const auto vtable = image_base + layout.vtable_rva;
    store(bytes, 0x00, vtable);
    store(bytes, 0x10, next);
    store(bytes, 0x18, previous);
    store(bytes, 0x28, root);
    return scatter_semantic_state(bytes, layout, node.semantic_state)
        && scatter_ranges(bytes, layout.derived_ranges,
            std::span{node.derived_state}.subspan(common_derived_state_size))
        && scatter_ranges(bytes, StageWindCommonDerivedRanges(),
            std::span{node.derived_state}.first(common_derived_state_size));
}
}

StageWindGraphTransaction::StageWindGraphTransaction(
    INativeMemory& memory, IStageWindAllocator& allocator) noexcept
    : memory_(memory), allocator_(allocator)
{
}

std::size_t StageWindGraphTransaction::AllocationEnvelopeBytes() noexcept
{
    std::size_t payload{}, allocation{};
    for (const auto kind : {StageWindNodeKind::Parallel, StageWindNodeKind::RingOut,
            StageWindNodeKind::RingIn, StageWindNodeKind::ShockWave}) {
        const auto* layout = FindStageWindNodeLayout(kind);
        if (!layout) return (std::numeric_limits<std::size_t>::max)();
        const auto charge = allocator_.AllocationBytes(layout->allocation_size);
        if (charge < layout->allocation_size || charge > (std::numeric_limits<std::size_t>::max)() / (2 * max_nodes))
            return (std::numeric_limits<std::size_t>::max)();
        allocation = (std::max)(allocation, charge);
        // Separately maximize both vectors; different node kinds may provide
        // their maxima. The allocation size bounds both payloads together.
        payload = (std::max)(payload, 2 * layout->allocation_size);
    }
    const auto vectors = 6 * max_nodes * payload;
    if (allocation > ((std::numeric_limits<std::size_t>::max)() - vectors) / (2 * max_nodes))
        return (std::numeric_limits<std::size_t>::max)();
    return 2 * max_nodes * allocation + vectors;
}

Status StageWindGraphTransaction::Prepare(
    const StageWindTopologyAddresses& addresses,
    const StageWindTopologyImage& target, bool enclosing, std::size_t budget) noexcept
{
    if (prepared_) return Status::failure(FailureCode::IllegalTransition);
    const auto envelope = AllocationEnvelopeBytes();
    if (envelope == (std::numeric_limits<std::size_t>::max)() || envelope > budget)
        return Status::failure(FailureCode::CapacityExceeded);
    if (addresses.image_base == 0 || addresses.image_size == 0
        || addresses.root_pointer == 0 || addresses.generation == 0
        || target.generation != addresses.generation
        || !ValidateStageWindTopologyImage(target))
    {
        return Status::failure(FailureCode::RestorePreflightFailed);
    }
    for (const auto& node : target.nodes)
    {
        const auto* layout = FindStageWindNodeLayout(node.kind);
        if (layout == nullptr
            || node.semantic_state.size() != StageWindSemanticStateSize(*layout)
            || node.derived_state.size() != StageWindDerivedStateSize(*layout))
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
    std::array<std::uintptr_t, max_nodes> old_nodes{};
    std::size_t old_node_count{};
    if (!collect_existing_nodes(
            memory_, root, addresses.image_base, addresses.image_size,
            old_nodes, old_node_count))
        return Status::failure(FailureCode::IdentityMismatch);

    std::array<std::uintptr_t, max_nodes> replacements{};
    std::size_t replacement_count{};
    for (const auto& node : target.nodes)
    {
        const auto* layout = FindStageWindNodeLayout(node.kind);
        const auto replacement = allocator_.Allocate(layout->allocation_size);
        if (replacement == 0)
        {
            free_all(allocator_, std::span{replacements}.first(
                replacement_count));
            return Status::failure(FailureCode::CapacityExceeded);
        }
        replacements[replacement_count++] = replacement;
    }

    std::array<std::byte, 0x1E0> node_bytes{};
    for (std::size_t index = 0; index < replacement_count; ++index)
    {
        const auto* layout = FindStageWindNodeLayout(target.nodes[index].kind);
        const auto next = index + 1 < replacement_count
            ? replacements[index + 1] : 0;
        const auto previous = index == 0 ? 0 : replacements[index - 1];
        const auto bytes = std::span{node_bytes}.first(layout->allocation_size);
        if (!build_node_bytes(bytes, *layout, target.nodes[index],
                addresses.image_base, root, previous, next)
            || !memory_.Write(replacements[index], bytes))
        {
            free_all(allocator_, std::span{replacements}.first(
                replacement_count));
            return Status::failure(FailureCode::RestoreWriteFailed);
        }
    }

    auto new_root = undo_root;
    const auto head = replacement_count == 0 ? 0 : replacements.front();
    store(std::span{new_root}, 0x00, head);
    std::memcpy(new_root.data() + 0x08, target.root_clock.data(), target.root_clock.size());
    std::array<std::uintptr_t, 16> callbacks{};
    for (std::size_t index = 0; index < callbacks.size(); ++index)
        if (target.pending_callback_rvas[index] != 0)
            callbacks[index] = addresses.image_base + target.pending_callback_rvas[index];
    std::memcpy(new_root.data() + 0x18, callbacks.data(), sizeof(callbacks));
    std::memcpy(new_root.data() + 0x98, target.schedule_state.data(), target.schedule_state.size());
    std::memcpy(new_root.data() + 0xA8, target.root_unknown_a8.data(),
        target.root_unknown_a8.size());
    std::memcpy(new_root.data() + 0xB0, target.schedule_params.data(), target.schedule_params.size());
    std::memcpy(new_root.data() + 0xC0, target.output_force.data(), target.output_force.size());

    std::uintptr_t current_root{};
    if (!read_value(memory_, addresses.root_pointer, current_root) || current_root != root)
    {
        free_all(allocator_, std::span{replacements}.first(replacement_count));
        return Status::failure(FailureCode::GenerationMismatch);
    }
    // All allocation and target construction precede the root publication.
    // Keep exact B backing, including non-semantic bytes, detached until commit.
    try {
        StageWindTopologyProbe probe(memory_);
        auto status = probe.Bind(addresses);
        if (status.ok()) status = probe.Capture(original_image_);
        if (!status.ok()) {
            free_all(allocator_, std::span{replacements}.first(replacement_count));
            return status;
        }
        target_image_ = target;
    } catch (...) {
        free_all(allocator_, std::span{replacements}.first(replacement_count));
        return Status::failure(FailureCode::CapacityExceeded);
    }
    addresses_ = addresses;
    root_ = root;
    original_root_ = undo_root;
    target_root_ = new_root;
    original_count_ = old_node_count;
    target_count_ = replacement_count;
    owned_bytes_ = original_image_.nodes.dynamic_capacity_bytes() + target_image_.nodes.dynamic_capacity_bytes();
    for (std::size_t i = 0; i < old_node_count; ++i) {
        auto& n = original_nodes_[i];
        n.address = old_nodes[i];
        std::uintptr_t vtable{};
        if (!read_value(memory_, n.address, vtable)) {
            free_all(allocator_, std::span{replacements}.first(replacement_count));
            return Status::failure(FailureCode::CaptureFailed);
        }
        const auto* layout = FindStageWindNodeLayoutByVtable(static_cast<std::uint32_t>(vtable - addresses.image_base));
        if (!layout || !memory_.Read(n.address, std::span{n.bytes}.first(layout->allocation_size))) {
            free_all(allocator_, std::span{replacements}.first(replacement_count));
            return Status::failure(FailureCode::CaptureFailed);
        }
        n.size = layout->allocation_size;
        owned_bytes_ += allocator_.AllocationBytes(n.size);
    }
    for (std::size_t i = 0; i < replacement_count; ++i) {
        auto& n = target_nodes_[i];
        n.address = replacements[i];
        n.size = FindStageWindNodeLayout(target.nodes[i].kind)->allocation_size;
        if (!build_node_bytes(std::span{n.bytes}.first(n.size), *FindStageWindNodeLayout(target.nodes[i].kind),
                target.nodes[i], addresses.image_base, root, i ? replacements[i - 1] : 0,
                i + 1 < replacement_count ? replacements[i + 1] : 0)) {
            free_all(allocator_, std::span{replacements}.first(replacement_count));
            return Status::failure(FailureCode::CaptureFailed);
        }
        owned_bytes_ += allocator_.AllocationBytes(n.size);
    }
    prepared_ = true;
    enclosing_ = enclosing;
    recovered_ = false;
    return Status::success();
}

Status StageWindGraphTransaction::ValidateGraph(bool target, bool check_root) const noexcept
{
    if (!prepared_) return Status::failure(FailureCode::IllegalTransition);
    std::uintptr_t root{};
    if (!read_value(memory_, addresses_.root_pointer, root) || root != root_)
        return Status::failure(FailureCode::GenerationMismatch);
    if (check_root) {
        std::array<std::byte, root_size> bytes{};
        if (!memory_.Read(root_, bytes) || bytes != (target ? target_root_ : original_root_))
            return Status::failure(FailureCode::RestoreVerificationFailed);
    }
    const auto& nodes = target ? target_nodes_ : original_nodes_;
    const auto count = target ? target_count_ : original_count_;
    std::array<std::byte, 0x1e0> bytes{};
    for (std::size_t i = 0; i < count; ++i) {
        const auto& n = nodes[i];
        if (!memory_.Read(n.address, std::span{bytes}.first(n.size))
            || !std::equal(bytes.begin(), bytes.begin() + n.size, n.bytes.begin()))
            return Status::failure(FailureCode::RestoreVerificationFailed);
    }
    return Status::success();
}

Status StageWindGraphTransaction::Publish() noexcept
{
    if (published_) return write_complete_ && !undo_started_ ? ValidateCommit()
        : Status::failure(FailureCode::IllegalTransition);
    auto status = ValidateGraph(false, true);
    if (status.ok()) status = ValidateGraph(true, false);
    if (!status.ok()) return status;
    if (recovered_) return Status::failure(FailureCode::IllegalTransition);
    published_ = true; // A false memory write may still have changed the head.
    if (!memory_.Write(root_, target_root_)) return Status::failure(FailureCode::RestoreWriteFailed);
    status = ValidateGraph(true, true);
    write_complete_ = status.ok();
    return status;
}

Status StageWindGraphTransaction::Undo() noexcept
{
    if (!prepared_) return Status::success();
    if(executing_ && !execution_settled_)return Status::failure(FailureCode::IllegalTransition);
    if(executing_ && published_ && !undo_started_) {
        const auto current=ValidateGraph(true,true);if(!current.ok())return current;
    }
    auto status = ValidateGraph(false, !published_);
    if (!status.ok()) return status;
    if (published_) {
        // Never require a complete A header after an ambiguous write. Both
        // graphs remain owned, and retrying B publication is allocation-free.
        undo_started_ = true;
        if (!memory_.Write(root_, original_root_)) return Status::failure(FailureCode::UndoFailed);
        status = ValidateGraph(false, true);
        if (!status.ok()) return Status::failure(FailureCode::UndoFailed);
    }
    published_ = write_complete_ = undo_started_ = false;
    recovered_ = true;
    // Enclosing undo may still need to validate this participant again.
    if (!enclosing_) DiscardPrepared();
    return Status::success();
}

Status StageWindGraphTransaction::ValidateCommit() const noexcept
{
    if (!published_ || !write_complete_ || undo_started_ || (executing_ && !execution_settled_))
        return Status::failure(FailureCode::IllegalTransition);
    auto status = ValidateGraph(true, true);
    return status.ok() ? ValidateGraph(false, false) : status;
}

Status StageWindGraphTransaction::BeginExecution(std::size_t retirement_budget) noexcept
{
    if(executing_ || !enclosing_)return Status::failure(FailureCode::IllegalTransition);
    auto status=ValidateCommit();if(!status.ok())return status;
    const auto envelope=AllocationEnvelopeBytes();
    if(retirement_budget<envelope || retirement_budget>SIZE_MAX-owned_bytes_)
        return Status::failure(FailureCode::CapacityExceeded);
    // Native wind updates may remove or replace nodes. Retain B, but surrender
    // A's addresses so later recovery cannot free a node already retired there.
    target_count_=0;executing_=true;owned_bytes_+=retirement_budget;
    return Status::success();
}

Status StageWindGraphTransaction::SettleExecution() noexcept
{
    if(!executing_ || execution_settled_ || !published_ || undo_started_)
        return Status::failure(FailureCode::IllegalTransition);
    auto status=ValidateGraph(false,false);if(!status.ok())return status;
    std::array<std::uintptr_t,max_nodes> nodes{};std::size_t count{};
    if(!collect_existing_nodes(memory_,root_,addresses_.image_base,addresses_.image_size,nodes,count))
        return Status::failure(FailureCode::GenerationMismatch);
    const auto overlaps=[](std::uintptr_t a,std::size_t size,std::uintptr_t b,std::size_t length) {
        return size>UINTPTR_MAX-a || length>UINTPTR_MAX-b || (a<b+length && b<a+size);
    };
    for(std::size_t i=0;i<count;++i) {
        std::uintptr_t vtable{};
        if(!read_value(memory_,nodes[i],vtable))return Status::failure(FailureCode::ContextUnavailable);
        if(vtable<addresses_.image_base || vtable-addresses_.image_base>=addresses_.image_size
            || vtable-addresses_.image_base>UINT32_MAX)return Status::failure(FailureCode::IdentityMismatch);
        const auto* layout=FindStageWindNodeLayoutByVtable(static_cast<std::uint32_t>(vtable-addresses_.image_base));
        if(!layout || overlaps(nodes[i],layout->allocation_size,root_,root_size))
            return Status::failure(FailureCode::RestorePreflightFailed);
        for(std::size_t j=0;j<original_count_;++j)
            if(overlaps(nodes[i],layout->allocation_size,original_nodes_[j].address,original_nodes_[j].size))
                return Status::failure(FailureCode::RestorePreflightFailed);
        for(std::size_t j=0;j<i;++j)
            if(overlaps(nodes[i],layout->allocation_size,target_nodes_[j].address,target_nodes_[j].size))
                return Status::failure(FailureCode::RestorePreflightFailed);
        auto& n=target_nodes_[i];n.address=nodes[i];n.size=layout->allocation_size;
        if(!memory_.Read(n.address,std::span{n.bytes}.first(n.size)))
            return Status::failure(FailureCode::ContextUnavailable);
    }
    if(!memory_.Read(root_,target_root_))return Status::failure(FailureCode::ContextUnavailable);
    target_count_=count;execution_settled_=true;
    return ValidateCommit();
}

Status StageWindGraphTransaction::ReopenExecutionForUndo() noexcept
{
    if(!executing_ || !published_ || undo_started_) return Status::failure(FailureCode::IllegalTransition);
    if(!execution_settled_) return Status::success();
    auto status=ValidateCommit();if(!status.ok()) return status;
    status=ValidateGraph(false,false);if(!status.ok()) return status;
    target_count_=0;execution_settled_=false;
    return Status::success();
}

void StageWindGraphTransaction::DiscardPrepared() noexcept
{
    if (!prepared_ || published_) return;
    for (std::size_t i = target_count_; i > 0; --i) allocator_.Free(target_nodes_[i - 1].address);
    prepared_ = enclosing_ = recovered_ = false;
    target_count_ = original_count_ = 0;
    executing_=execution_settled_=false;
    owned_bytes_ = original_image_.nodes.dynamic_capacity_bytes() + target_image_.nodes.dynamic_capacity_bytes();
}

Status StageWindGraphTransaction::Commit() noexcept
{
    if (recovered_) { DiscardPrepared(); return Status::success(); }
    auto status = ValidateCommit();
    if (!status.ok()) return status;
    for (std::size_t i = original_count_; i > 0; --i) allocator_.Free(original_nodes_[i - 1].address);
    // Native root now owns A. Never free it as prepared scratch.
    target_count_ = 0;
    published_ = write_complete_ = false;
    DiscardPrepared();
    return Status::success();
}

Status StageWindGraphTransaction::Restore(const StageWindTopologyAddresses& addresses,
    const StageWindTopologyImage& target) noexcept
{
    if (enclosing_) {
        if (addresses.root_pointer != addresses_.root_pointer || addresses.generation != addresses_.generation
            || addresses.image_base != addresses_.image_base || addresses.image_size != addresses_.image_size)
            return Status::failure(FailureCode::GenerationMismatch);
        if (recovered_ && target == original_image_) return ValidateGraph(false, true);
        if(executing_)return target==original_image_?Undo():Status::failure(FailureCode::IllegalTransition);
        if (target == target_image_) return Publish();
        if (target == original_image_) return Undo();
        return Status::failure(FailureCode::RestorePreflightFailed);
    }
    auto status = Prepare(addresses, target);
    if (!status.ok()) return status;
    status = Publish();
    if (!status.ok()) {
        const auto undone = Undo();
        return undone.ok() ? status : Status::failure(FailureCode::UndoFailed);
    }
    return Commit();
}
}
