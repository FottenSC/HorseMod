#pragma once

#include "NativeCandidateRegions.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Horse::Deterministic
{
enum class StageWindNodeKind : std::uint8_t
{
    Parallel,
    RingOut,
    RingIn,
    ShockWave,
};

struct StageWindStateRange
{
    std::size_t offset{};
    std::size_t size{};
};

struct StageWindNodeLayout
{
    StageWindNodeKind kind{};
    std::uint32_t vtable_rva{};
    std::size_t allocation_size{};
    std::span<const StageWindStateRange> class_ranges{};
    std::span<const StageWindStateRange> derived_ranges{};
};

[[nodiscard]] std::span<const StageWindStateRange> StageWindCommonRanges() noexcept;
[[nodiscard]] std::span<const StageWindStateRange>
StageWindCommonDerivedRanges() noexcept;
[[nodiscard]] const StageWindNodeLayout* FindStageWindNodeLayout(
    StageWindNodeKind kind) noexcept;
[[nodiscard]] const StageWindNodeLayout* FindStageWindNodeLayoutByVtable(
    std::uint32_t vtable_rva) noexcept;
[[nodiscard]] std::size_t StageWindSemanticStateSize(
    const StageWindNodeLayout& layout) noexcept;
[[nodiscard]] std::size_t StageWindDerivedStateSize(
    const StageWindNodeLayout& layout) noexcept;

struct StageWindNodeImage
{
    StageWindNodeKind kind{};
    std::vector<std::byte> semantic_state;
    std::vector<std::byte> derived_state;

    friend bool operator==(const StageWindNodeImage&, const StageWindNodeImage&) = default;
};

inline constexpr std::size_t stage_wind_max_nodes = 64;

// Fixed slot ownership keeps each node's variable-sized byte buffers alive
// when the native topology temporarily shrinks. The active prefix retains the
// vector-like API used by serialization and restore code, while later node
// re-entry cannot allocate a fresh outer container or discard warmed buffers.
class StageWindNodeBuffer
{
public:
    using iterator = StageWindNodeImage*;
    using const_iterator = const StageWindNodeImage*;

    StageWindNodeBuffer() = default;
    StageWindNodeBuffer(const StageWindNodeBuffer& other)
    {
        *this = other;
    }
    StageWindNodeBuffer& operator=(const StageWindNodeBuffer& other)
    {
        if (this == &other) return *this;
        std::size_t semantic_capacity = other.prepared_semantic_capacity_;
        std::size_t derived_capacity = other.prepared_derived_capacity_;
        if (semantic_capacity == 0 && derived_capacity == 0)
            for (const auto& slot : other.nodes_)
            {
                semantic_capacity = (std::max)(semantic_capacity,
                    slot.semantic_state.capacity());
                derived_capacity = (std::max)(derived_capacity,
                    slot.derived_state.capacity());
            }
        prepare_storage(semantic_capacity, derived_capacity);
        size_ = other.size_;
        for (std::size_t index = 0; index < size_; ++index)
        {
            nodes_[index].kind = other.nodes_[index].kind;
            nodes_[index].semantic_state =
                other.nodes_[index].semantic_state;
            nodes_[index].derived_state =
                other.nodes_[index].derived_state;
        }
        return *this;
    }
    StageWindNodeBuffer(StageWindNodeBuffer&&) noexcept = default;
    StageWindNodeBuffer& operator=(StageWindNodeBuffer&&) noexcept = default;

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr std::size_t capacity() const noexcept
    {
        return stage_wind_max_nodes;
    }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] iterator begin() noexcept { return nodes_.data(); }
    [[nodiscard]] const_iterator begin() const noexcept { return nodes_.data(); }
    [[nodiscard]] iterator end() noexcept { return nodes_.data() + size_; }
    [[nodiscard]] const_iterator end() const noexcept
    {
        return nodes_.data() + size_;
    }
    [[nodiscard]] StageWindNodeImage* data() noexcept { return nodes_.data(); }
    [[nodiscard]] const StageWindNodeImage* data() const noexcept
    {
        return nodes_.data();
    }
    [[nodiscard]] StageWindNodeImage& operator[](std::size_t index) noexcept
    {
        return nodes_[index];
    }
    [[nodiscard]] const StageWindNodeImage& operator[](
        std::size_t index) const noexcept
    {
        return nodes_[index];
    }
    [[nodiscard]] StageWindNodeImage& front() noexcept { return nodes_.front(); }
    [[nodiscard]] const StageWindNodeImage& front() const noexcept
    {
        return nodes_.front();
    }
    void clear() noexcept { size_ = 0; }
    void reserve(std::size_t count) const
    {
        if (count > capacity()) throw std::length_error("stage wind node capacity");
    }
    void resize(std::size_t count)
    {
        reserve(count);
        size_ = count;
    }
    StageWindNodeImage& emplace_back()
    {
        resize(size_ + 1);
        return nodes_[size_ - 1];
    }
    void push_back(StageWindNodeImage value)
    {
        auto& destination = emplace_back();
        destination = std::move(value);
    }
    void prepare_storage(
        std::size_t semantic_capacity, std::size_t derived_capacity)
    {
        if (semantic_capacity <= prepared_semantic_capacity_
            && derived_capacity <= prepared_derived_capacity_)
            return;
        for (auto& slot : nodes_)
        {
            slot.semantic_state.reserve(semantic_capacity);
            slot.derived_state.reserve(derived_capacity);
        }
        prepared_semantic_capacity_ = semantic_capacity;
        prepared_derived_capacity_ = derived_capacity;
        prepared_dynamic_capacity_bytes_ = 0;
        for (const auto& slot : nodes_)
            prepared_dynamic_capacity_bytes_ += slot.semantic_state.capacity()
                + slot.derived_state.capacity();
    }
    [[nodiscard]] std::size_t dynamic_capacity_bytes() const noexcept
    {
        if (prepared_semantic_capacity_ != 0
            || prepared_derived_capacity_ != 0)
        {
            return prepared_dynamic_capacity_bytes_;
        }
        std::size_t bytes{};
        for (const auto& slot : nodes_)
            bytes += slot.semantic_state.capacity()
                + slot.derived_state.capacity();
        return bytes;
    }
    [[nodiscard]] std::span<const StageWindNodeImage> storage() const noexcept
    {
        return nodes_;
    }
    [[nodiscard]] std::span<StageWindNodeImage> storage() noexcept
    {
        return nodes_;
    }

    friend bool operator==(
        const StageWindNodeBuffer& a, const StageWindNodeBuffer& b) noexcept
    {
        return a.size_ == b.size_
            && std::equal(a.begin(), a.end(), b.begin());
    }

private:
    std::array<StageWindNodeImage, stage_wind_max_nodes> nodes_{};
    std::size_t size_{};
    std::size_t prepared_semantic_capacity_{};
    std::size_t prepared_derived_capacity_{};
    std::size_t prepared_dynamic_capacity_bytes_{};
};

struct StageWindTopologyImage
{
    std::uint64_t generation{};
    std::array<std::byte, 12> root_clock{};
    std::array<std::uint32_t, 16> pending_callback_rvas{};
    std::array<std::byte, 16> schedule_state{};
    std::array<std::byte, 8> root_unknown_a8{};
    std::array<std::byte, 16> schedule_params{};
    std::array<std::byte, 48> output_force{};
    StageWindNodeBuffer nodes;

    friend bool operator==(const StageWindTopologyImage&, const StageWindTopologyImage&) = default;
};

[[nodiscard]] bool ValidateStageWindTopologyImage(
    const StageWindTopologyImage& image) noexcept;

struct StageWindTopologyAddresses
{
    std::uintptr_t image_base{};
    std::size_t image_size{};
    std::uintptr_t root_pointer{};
    std::uint64_t generation{};
};

// Read-only admission probe. It deliberately serializes no native pointer and
// does not claim that wind allocations can be reconstructed or restored.
class StageWindTopologyProbe
{
public:
    explicit StageWindTopologyProbe(INativeMemory& memory) noexcept;

    Status Bind(const StageWindTopologyAddresses& addresses) noexcept;
    void Invalidate() noexcept;
    Status Capture(StageWindTopologyImage& output) noexcept;

    [[nodiscard]] static std::vector<std::byte> CanonicalBytes(
        const StageWindTopologyImage& image);
    [[nodiscard]] static Status CanonicalBytes(
        const StageWindTopologyImage& image,
        std::vector<std::byte>& output) noexcept;

private:
    INativeMemory& memory_;
    StageWindTopologyAddresses addresses_{};
    std::uintptr_t root_{};
    bool bound_{};
};
}
