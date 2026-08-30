#include "StageWindTopology.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <span>

namespace Horse::Deterministic
{
namespace
{
constexpr std::size_t max_wind_nodes = stage_wind_max_nodes;

constexpr std::array common_ranges{
    StageWindStateRange{0x20, 0x02},
    StageWindStateRange{0x30, 0x04},
    // Oscillator tick, prepared, and active scheduling state. The sampled and
    // output force vectors at +0x40..+0x5F are presentation-derived.
    StageWindStateRange{0x60, 0x10},
};
constexpr std::array common_derived_ranges{
    StageWindStateRange{0x40, 0x20},
};
constexpr std::array parallel_ranges{
    StageWindStateRange{0x70, 0x70},
    StageWindStateRange{0x120, 0x0C},
};
constexpr std::array ring_out_ranges{
    StageWindStateRange{0x70, 0x70},
    StageWindStateRange{0x120, 0x0C},
};
constexpr std::array shock_wave_ranges{
    StageWindStateRange{0x70, 0x74},
    StageWindStateRange{0xF0, 0x20},
    StageWindStateRange{0x120, 0x0C},
    StageWindStateRange{0x130, 0x50},
};
constexpr std::array shock_wave_derived_ranges{
    // Current-executable prepare/update/sample virtuals never access this
    // allocator-residue gap. Retain it for byte-exact local rewind only.
    StageWindStateRange{0xE4, 0x0C},
};
constexpr std::array ring_in_ranges{
    StageWindStateRange{0x70, 0x84},
    StageWindStateRange{0xF8, 0x24},
    StageWindStateRange{0x130, 0x04},
    StageWindStateRange{0x148, 0x04},
};
constexpr std::array ring_in_derived_ranges{
    StageWindStateRange{0x120, 0x10},
    StageWindStateRange{0x134, 0x10},
    StageWindStateRange{0x150, 0x90},
};
constexpr std::array<StageWindStateRange, 0> no_derived_ranges{};
constexpr std::array classes{
    StageWindNodeLayout{StageWindNodeKind::Parallel, 0x3E88C88, 0x130,
        parallel_ranges, no_derived_ranges},
    StageWindNodeLayout{StageWindNodeKind::RingOut, 0x3E88CB8, 0x130,
        ring_out_ranges, no_derived_ranges},
    StageWindNodeLayout{StageWindNodeKind::RingIn, 0x3E88CE8, 0x1E0,
        ring_in_ranges, ring_in_derived_ranges},
    StageWindNodeLayout{StageWindNodeKind::ShockWave, 0x3E88D18, 0x180,
        shock_wave_ranges, shock_wave_derived_ranges},
};

template <typename T>
bool read_value(INativeMemory& memory, std::uintptr_t address, T& output) noexcept
{
    return memory.Read(address, std::as_writable_bytes(std::span{&output, 1}));
}

bool read_append(
    INativeMemory& memory, std::uintptr_t address, std::size_t size,
    std::vector<std::byte>& output) noexcept
{
    const auto old_size = output.size();
    try { output.resize(old_size + size); }
    catch (...) { return false; }
    if (memory.Read(address, std::span{output}.subspan(old_size, size))) return true;
    output.resize(old_size);
    return false;
}

void append(std::vector<std::byte>& output, const void* data, std::size_t size)
{
    const auto* first = static_cast<const std::byte*>(data);
    output.insert(output.end(), first, first + size);
}

}

std::span<const StageWindStateRange> StageWindCommonRanges() noexcept
{
    return common_ranges;
}

std::span<const StageWindStateRange> StageWindCommonDerivedRanges() noexcept
{
    return common_derived_ranges;
}

const StageWindNodeLayout* FindStageWindNodeLayout(StageWindNodeKind kind) noexcept
{
    const auto found = std::find_if(classes.begin(), classes.end(),
        [kind](const StageWindNodeLayout& item) { return item.kind == kind; });
    return found == classes.end() ? nullptr : &*found;
}

const StageWindNodeLayout* FindStageWindNodeLayoutByVtable(
    std::uint32_t vtable_rva) noexcept
{
    const auto found = std::find_if(classes.begin(), classes.end(),
        [vtable_rva](const StageWindNodeLayout& item) {
            return item.vtable_rva == vtable_rva;
        });
    return found == classes.end() ? nullptr : &*found;
}

std::size_t StageWindSemanticStateSize(const StageWindNodeLayout& layout) noexcept
{
    std::size_t total{};
    for (const auto range : common_ranges) total += range.size;
    for (const auto range : layout.class_ranges) total += range.size;
    return total;
}

std::size_t StageWindDerivedStateSize(const StageWindNodeLayout& layout) noexcept
{
    std::size_t total{};
    for (const auto range : common_derived_ranges) total += range.size;
    for (const auto range : layout.derived_ranges) total += range.size;
    return total;
}

bool ValidateStageWindTopologyImage(const StageWindTopologyImage& image) noexcept
{
    if (image.generation == 0 || image.nodes.size() > max_wind_nodes) return false;
    std::uint32_t active_bank{};
    std::int32_t pending_count{};
    std::memcpy(&active_bank, image.schedule_state.data(), sizeof(active_bank));
    std::memcpy(&pending_count, image.schedule_state.data() + 4, sizeof(pending_count));
    if (active_bank > 1 || pending_count < 0 || pending_count > 8) return false;
    for (const auto& node : image.nodes)
    {
        const auto* layout = FindStageWindNodeLayout(node.kind);
        if (layout == nullptr
            || node.semantic_state.size() != StageWindSemanticStateSize(*layout)
            || node.derived_state.size() != StageWindDerivedStateSize(*layout))
            return false;
    }
    return true;
}

StageWindTopologyProbe::StageWindTopologyProbe(INativeMemory& memory) noexcept
    : memory_(memory)
{
}

Status StageWindTopologyProbe::Bind(
    const StageWindTopologyAddresses& addresses) noexcept
{
    Invalidate();
    if (addresses.image_base == 0 || addresses.image_size == 0
        || addresses.root_pointer == 0 || addresses.generation == 0
        || addresses.image_size > std::numeric_limits<std::uint32_t>::max())
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    std::uintptr_t root{};
    if (!read_value(memory_, addresses.root_pointer, root) || root == 0)
        return Status::failure(FailureCode::ContextUnavailable);
    addresses_ = addresses;
    root_ = root;
    bound_ = true;
    StageWindTopologyImage ignored{};
    const auto status = Capture(ignored);
    if (!status.ok()) Invalidate();
    return status;
}

void StageWindTopologyProbe::Invalidate() noexcept
{
    addresses_ = {};
    root_ = 0;
    bound_ = false;
}

Status StageWindTopologyProbe::Capture(StageWindTopologyImage& output) noexcept
{
    // Preserve the bounded node/vector capacities across live captures. The
    // topology normally remains stable for a native generation; destroying the
    // image here caused avoidable heap churn on every authoritative frame and
    // every correction capture.
    output.generation = 0;
    output.root_clock = {};
    output.pending_callback_rvas = {};
    output.schedule_state = {};
    output.schedule_params = {};
    output.output_force = {};
    if (!bound_) return Status::failure(FailureCode::AdapterUnqualified);
    try
    {
        output.nodes.reserve(max_wind_nodes);
        std::size_t maximum_semantic{};
        std::size_t maximum_derived{};
        for (const auto& layout : classes)
        {
            maximum_semantic = (std::max)(maximum_semantic,
                StageWindSemanticStateSize(layout));
            maximum_derived = (std::max)(maximum_derived,
                StageWindDerivedStateSize(layout));
        }
        output.nodes.prepare_storage(maximum_semantic, maximum_derived);
    }
    catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    std::uintptr_t current_root{};
    if (!read_value(memory_, addresses_.root_pointer, current_root)
        || current_root != root_)
    {
        return Status::failure(FailureCode::GenerationMismatch);
    }

    output.generation = addresses_.generation;
    std::uintptr_t node{};
    if (!read_value(memory_, root_, node)
        || !memory_.Read(root_ + 0x08, output.root_clock))
    {
        output = {};
        return Status::failure(FailureCode::CaptureFailed);
    }
    std::array<std::uintptr_t, 16> callbacks{};
    if (!memory_.Read(root_ + 0x18, std::as_writable_bytes(std::span{callbacks}))
        || !memory_.Read(root_ + 0x98, output.schedule_state)
        || !memory_.Read(root_ + 0xB0, output.schedule_params)
        || !memory_.Read(root_ + 0xC0, output.output_force))
    {
        output = {};
        return Status::failure(FailureCode::CaptureFailed);
    }
    for (std::size_t index = 0; index < callbacks.size(); ++index)
    {
        if (callbacks[index] == 0) continue;
        if (callbacks[index] < addresses_.image_base
            || callbacks[index] - addresses_.image_base >= addresses_.image_size)
        {
            output = {};
            return Status::failure(FailureCode::IdentityMismatch);
        }
        output.pending_callback_rvas[index] = static_cast<std::uint32_t>(
            callbacks[index] - addresses_.image_base);
    }
    std::uint32_t active_bank{};
    std::int32_t pending_count{};
    std::memcpy(&active_bank, output.schedule_state.data(), sizeof(active_bank));
    std::memcpy(&pending_count, output.schedule_state.data() + 4, sizeof(pending_count));
    if (active_bank > 1 || pending_count < 0 || pending_count > 8)
    {
        output = {};
        return Status::failure(FailureCode::IdentityMismatch);
    }

    std::uintptr_t previous{};
    std::array<std::uintptr_t, max_wind_nodes> visited{};
    std::size_t captured_node_count{};
    while (node != 0)
    {
        if (captured_node_count == max_wind_nodes)
        {
            output = {};
            return Status::failure(FailureCode::CapacityExceeded);
        }
        if (std::find(visited.begin(), visited.begin() + captured_node_count, node)
            != visited.begin() + captured_node_count)
        {
            output = {};
            return Status::failure(FailureCode::IdentityMismatch);
        }
        visited[captured_node_count] = node;
        std::uintptr_t vtable{}, next{}, node_previous{}, node_root{};
        if (!read_value(memory_, node, vtable)
            || !read_value(memory_, node + 0x10, next)
            || !read_value(memory_, node + 0x18, node_previous)
            || !read_value(memory_, node + 0x28, node_root)
            || node_previous != previous || node_root != root_
            || vtable < addresses_.image_base
            || vtable - addresses_.image_base > std::numeric_limits<std::uint32_t>::max())
        {
            output = {};
            return Status::failure(FailureCode::IdentityMismatch);
        }
        const auto* node_class = FindStageWindNodeLayoutByVtable(static_cast<std::uint32_t>(
            vtable - addresses_.image_base));
        if (node_class == nullptr)
        {
            output = {};
            return Status::failure(FailureCode::AdapterUnqualified);
        }
        if (captured_node_count == output.nodes.size())
        {
            try { output.nodes.emplace_back(); }
            catch (...)
            {
                output = {};
                return Status::failure(FailureCode::CapacityExceeded);
            }
        }
        auto& image = output.nodes[captured_node_count];
        image.semantic_state.clear();
        image.derived_state.clear();
        image.kind = node_class->kind;
        try
        {
            image.semantic_state.reserve(
                StageWindSemanticStateSize(*node_class));
            image.derived_state.reserve(
                StageWindDerivedStateSize(*node_class));
        }
        catch (...)
        {
            output = {};
            return Status::failure(FailureCode::CapacityExceeded);
        }
        for (const auto range : common_ranges)
            if (!read_append(memory_, node + range.offset, range.size, image.semantic_state))
            {
                output = {};
                return Status::failure(FailureCode::CaptureFailed);
            }
        for (const auto range : node_class->class_ranges)
            if (!read_append(memory_, node + range.offset, range.size, image.semantic_state))
            {
                output = {};
                return Status::failure(FailureCode::CaptureFailed);
            }
        for (const auto range : common_derived_ranges)
            if (!read_append(memory_, node + range.offset, range.size, image.derived_state))
            {
                output = {};
                return Status::failure(FailureCode::CaptureFailed);
            }
        for (const auto range : node_class->derived_ranges)
            if (!read_append(memory_, node + range.offset, range.size, image.derived_state))
            {
                output = {};
                return Status::failure(FailureCode::CaptureFailed);
            }
        ++captured_node_count;
        previous = node;
        node = next;
    }
    output.nodes.resize(captured_node_count);
    return Status::success();
}

std::vector<std::byte> StageWindTopologyProbe::CanonicalBytes(
    const StageWindTopologyImage& image)
{
    std::vector<std::byte> output;
    CanonicalBytes(image, output);
    return output;
}

void StageWindTopologyProbe::CanonicalBytes(
    const StageWindTopologyImage& image, std::vector<std::byte>& output)
{
    output.clear();
    if (output.capacity() < 0x1000) output.reserve(0x1000);
    append(output, &image.generation, sizeof(image.generation));
    // Native root +0x08/+0x0C are the written strength and scene-tick
    // scalars. The captured +0x10 word has no writer or reader in the
    // verified native closure, so retain it for local byte-exact rewind but
    // exclude it from portable peer identity.
    constexpr std::size_t live_root_clock_size = sizeof(float) * 2;
    static_assert(live_root_clock_size <
        std::tuple_size_v<decltype(image.root_clock)>);
    append(output, image.root_clock.data(), live_root_clock_size);
    std::uint32_t active_bank{};
    std::int32_t pending_count{};
    std::int32_t schedule_state{};
    std::int32_t effect_pair_scheduled{};
    std::memcpy(&active_bank, image.schedule_state.data(),
        sizeof(active_bank));
    std::memcpy(&pending_count, image.schedule_state.data() + 4,
        sizeof(pending_count));
    std::memcpy(&schedule_state, image.schedule_state.data() + 8,
        sizeof(schedule_state));
    std::memcpy(&effect_pair_scheduled, image.schedule_state.data() + 12,
        sizeof(effect_pair_scheduled));
    if (active_bank > 1 || pending_count < 0 || pending_count > 8)
        return;
    // The bank bit is a symmetric double-buffer label. Only the ordered
    // pending span in the selected bank can be read before the next flip;
    // unused slots in either bank are stale native residue.
    append(output, &schedule_state, sizeof(schedule_state));
    append(output, &effect_pair_scheduled, sizeof(effect_pair_scheduled));
    append(output, &pending_count, sizeof(pending_count));
    append(output, image.pending_callback_rvas.data() + active_bank * 8,
        static_cast<std::size_t>(pending_count)
            * sizeof(image.pending_callback_rvas[0]));
    append(output, image.schedule_params.data(), image.schedule_params.size());
    // Root force lanes are presentation accumulation written after sampling.
    // They remain in the local reconstruction image, not canonical peer truth.
    const auto count = static_cast<std::uint32_t>(image.nodes.size());
    append(output, &count, sizeof(count));
    for (const auto& node : image.nodes)
    {
        append(output, &node.kind, sizeof(node.kind));
        const auto size = static_cast<std::uint32_t>(node.semantic_state.size());
        append(output, &size, sizeof(size));
        append(output, node.semantic_state.data(), node.semantic_state.size());
    }
}
}
