#include "StageWindTopology.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <span>

namespace Horse::Deterministic
{
namespace
{
constexpr std::size_t max_wind_nodes = 64;

struct Range
{
    std::size_t offset;
    std::size_t size;
};

struct NodeClass
{
    std::uint32_t vtable_rva;
    StageWindNodeKind kind;
    std::span<const Range> ranges;
};

constexpr std::array common_ranges{
    Range{0x20, 0x02},
    Range{0x30, 0x04},
    Range{0x40, 0x30},
};
constexpr std::array parallel_ranges{
    Range{0x70, 0x70},
    Range{0x120, 0x0C},
};
constexpr std::array ring_out_ranges{
    Range{0x70, 0xA0},
    Range{0x120, 0x60},
};
constexpr std::array shock_wave_ranges{
    Range{0x70, 0xA0},
    Range{0x120, 0x0C},
    Range{0x130, 0x50},
};
constexpr std::array ring_in_ranges{
    Range{0x70, 0x84},
    Range{0xF8, 0x24},
    Range{0x130, 0x04},
    Range{0x148, 0x04},
    Range{0x1D0, 0x10},
};
constexpr std::array classes{
    NodeClass{0x3E88C88, StageWindNodeKind::Parallel, parallel_ranges},
    NodeClass{0x3E88CB8, StageWindNodeKind::RingOut, ring_out_ranges},
    NodeClass{0x3E88CE8, StageWindNodeKind::RingIn, ring_in_ranges},
    NodeClass{0x3E88D18, StageWindNodeKind::ShockWave, shock_wave_ranges},
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
    output.resize(old_size + size);
    if (memory.Read(address, std::span{output}.subspan(old_size, size))) return true;
    output.resize(old_size);
    return false;
}

void append(std::vector<std::byte>& output, const void* data, std::size_t size)
{
    const auto* first = static_cast<const std::byte*>(data);
    output.insert(output.end(), first, first + size);
}

const NodeClass* classify(std::uint32_t rva) noexcept
{
    const auto found = std::find_if(classes.begin(), classes.end(),
        [rva](const NodeClass& item) { return item.vtable_rva == rva; });
    return found == classes.end() ? nullptr : &*found;
}
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
    output = {};
    if (!bound_) return Status::failure(FailureCode::AdapterUnqualified);
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
    while (node != 0)
    {
        if (output.nodes.size() == max_wind_nodes)
        {
            output = {};
            return Status::failure(FailureCode::CapacityExceeded);
        }
        if (std::find(visited.begin(), visited.begin() + output.nodes.size(), node)
            != visited.begin() + output.nodes.size())
        {
            output = {};
            return Status::failure(FailureCode::IdentityMismatch);
        }
        visited[output.nodes.size()] = node;
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
        const auto* node_class = classify(static_cast<std::uint32_t>(
            vtable - addresses_.image_base));
        if (node_class == nullptr)
        {
            output = {};
            return Status::failure(FailureCode::AdapterUnqualified);
        }
        StageWindNodeImage image{};
        image.kind = node_class->kind;
        for (const auto range : common_ranges)
            if (!read_append(memory_, node + range.offset, range.size, image.semantic_state))
            {
                output = {};
                return Status::failure(FailureCode::CaptureFailed);
            }
        for (const auto range : node_class->ranges)
            if (!read_append(memory_, node + range.offset, range.size, image.semantic_state))
            {
                output = {};
                return Status::failure(FailureCode::CaptureFailed);
            }
        output.nodes.push_back(std::move(image));
        previous = node;
        node = next;
    }
    return Status::success();
}

std::vector<std::byte> StageWindTopologyProbe::CanonicalBytes(
    const StageWindTopologyImage& image)
{
    std::vector<std::byte> output;
    append(output, &image.generation, sizeof(image.generation));
    append(output, image.root_clock.data(), image.root_clock.size());
    append(output, image.pending_callback_rvas.data(),
        sizeof(image.pending_callback_rvas));
    append(output, image.schedule_state.data(), image.schedule_state.size());
    append(output, image.schedule_params.data(), image.schedule_params.size());
    append(output, image.output_force.data(), image.output_force.size());
    const auto count = static_cast<std::uint32_t>(image.nodes.size());
    append(output, &count, sizeof(count));
    for (const auto& node : image.nodes)
    {
        append(output, &node.kind, sizeof(node.kind));
        const auto size = static_cast<std::uint32_t>(node.semantic_state.size());
        append(output, &size, sizeof(size));
        append(output, node.semantic_state.data(), node.semantic_state.size());
    }
    return output;
}
}
