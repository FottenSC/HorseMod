#pragma once

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace Horse
{
    struct RollbackVectorStorageIdentity
    {
        const void* data {nullptr};
        size_t size {0};
        size_t capacity {0};

        bool operator==(const RollbackVectorStorageIdentity& other) const noexcept
        {
            return data == other.data && size == other.size
                && capacity == other.capacity;
        }
    };

    struct RollbackVectorCapacityLimit
    {
        size_t capacity {0};

        bool operator==(const RollbackVectorCapacityLimit& other) const noexcept
        {
            return capacity == other.capacity;
        }
    };

    template <size_t MaximumAuxNodes, size_t MaximumSpringNodes>
    struct RollbackStepStateCapacityLimits
    {
        static constexpr size_t kMaximumVectorCount =
            18 + MaximumAuxNodes + MaximumSpringNodes;
        std::array<RollbackVectorCapacityLimit, kMaximumVectorCount>
            vectors {};
        size_t count {0};
        bool valid {false};

        bool operator==(const RollbackStepStateCapacityLimits& other) const noexcept
        {
            if (!valid || !other.valid || count != other.count) return false;
            for (size_t i = 0; i < count; ++i)
                if (!(vectors[i] == other.vectors[i])) return false;
            return true;
        }
    };

    template <size_t MaximumAuxNodes, size_t MaximumSpringNodes>
    struct RollbackStepStateStorageIdentity
    {
        static constexpr size_t kMaximumVectorCount =
            18 + MaximumAuxNodes + MaximumSpringNodes;
        std::array<RollbackVectorStorageIdentity, kMaximumVectorCount>
            vectors {};
        size_t count {0};
        bool valid {false};

        bool operator==(const RollbackStepStateStorageIdentity& other) const noexcept
        {
            if (!valid || !other.valid || count != other.count) return false;
            for (size_t i = 0; i < count; ++i)
            {
                if (!(vectors[i] == other.vectors[i])) return false;
            }
            return true;
        }
    };

    template <size_t MaximumAuxNodes, size_t MaximumSpringNodes,
              typename State>
    static inline RollbackStepStateStorageIdentity<
        MaximumAuxNodes, MaximumSpringNodes>
    CaptureRollbackStepStateStorageIdentity(const State& state) noexcept
    {
        RollbackStepStateStorageIdentity<
            MaximumAuxNodes, MaximumSpringNodes> result {};
        const auto add = [&result](const auto& values) noexcept {
            if (result.count >= result.vectors.size()) return false;
            result.vectors[result.count++] = {
                values.data(), values.size(), values.capacity()};
            return true;
        };
        if (!add(state.hgcpu.bytes)) return result;
        if (!add(state.palette_variants.payload)) return result;
        if (!add(state.palette_variants.writer_nodes)) return result;
        for (const auto& topology : state.hgcpu.khit_topology)
            if (!add(topology.nodes)) return result;
        if (!add(state.hgcpu.motion_banks.control_bytes)
            || !add(state.hgcpu.motion_banks.bytes)
            || !add(state.hgcpu.motion_tail.bytes))
            return result;
        const auto& skeleton = state.hgcpu.skeleton_runtime;
        if (skeleton.aux_nodes.size() > MaximumAuxNodes
            || skeleton.spring_nodes.size() > MaximumSpringNodes
            || !add(skeleton.inline_bytes) || !add(skeleton.aux_nodes))
            return result;
        for (const auto& node : skeleton.aux_nodes)
            if (!add(node.bytes)) return result;
        if (!add(skeleton.chains) || !add(skeleton.spring_nodes)) return result;
        for (const auto& node : skeleton.spring_nodes)
            if (!add(node.bytes)) return result;
        const auto& timer = state.hgcpu.timer_node;
        if (!add(timer.root_bytes) || !add(timer.backing_bytes)
            || !add(timer.nodes) || !add(state.explicit_snapshot.bytes)
            || !add(state.explicit_snapshot.ranges)
            || !add(state.breakable_stage.records))
            return result;
        result.valid = true;
        return result;
    }

    template <size_t MaximumAuxNodes, size_t MaximumSpringNodes,
              typename State>
    static inline RollbackStepStateCapacityLimits<
        MaximumAuxNodes, MaximumSpringNodes>
    CaptureRollbackStepStateCapacityLimits(const State& state) noexcept
    {
        RollbackStepStateCapacityLimits<
            MaximumAuxNodes, MaximumSpringNodes> result {};
        const auto add = [&result](const auto& values) noexcept {
            if (result.count >= result.vectors.size()) return false;
            result.vectors[result.count++] = {values.capacity()};
            return true;
        };
        if (!add(state.hgcpu.bytes)) return result;
        if (!add(state.palette_variants.payload)) return result;
        if (!add(state.palette_variants.writer_nodes)) return result;
        for (const auto& topology : state.hgcpu.khit_topology)
            if (!add(topology.nodes)) return result;
        if (!add(state.hgcpu.motion_banks.control_bytes)
            || !add(state.hgcpu.motion_banks.bytes)
            || !add(state.hgcpu.motion_tail.bytes))
            return result;
        const auto& skeleton = state.hgcpu.skeleton_runtime;
        if (skeleton.aux_nodes.size() > MaximumAuxNodes
            || skeleton.spring_nodes.size() > MaximumSpringNodes
            || !add(skeleton.inline_bytes) || !add(skeleton.aux_nodes))
            return result;
        for (const auto& node : skeleton.aux_nodes)
            if (!add(node.bytes)) return result;
        if (!add(skeleton.chains) || !add(skeleton.spring_nodes)) return result;
        for (const auto& node : skeleton.spring_nodes)
            if (!add(node.bytes)) return result;
        const auto& timer = state.hgcpu.timer_node;
        if (!add(timer.root_bytes) || !add(timer.backing_bytes)
            || !add(timer.nodes) || !add(state.explicit_snapshot.bytes)
            || !add(state.explicit_snapshot.ranges)
            || !add(state.breakable_stage.records))
            return result;
        result.valid = true;
        return result;
    }

    template <size_t MaximumAuxNodes, size_t MaximumSpringNodes,
              typename State>
    static inline bool TransferRollbackStepStateStorage(
        State& destination, State& source) noexcept
    {
        static_assert(std::is_nothrow_move_assignable_v<State>);
        if (&destination == &source) return false;
        const auto before = CaptureRollbackStepStateStorageIdentity<
            MaximumAuxNodes, MaximumSpringNodes>(source);
        if (!before.valid) return false;
        destination = std::move(source);
        const auto after = CaptureRollbackStepStateStorageIdentity<
            MaximumAuxNodes, MaximumSpringNodes>(destination);
        return before == after;
    }
}
