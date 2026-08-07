#pragma once

#include <cstddef>

namespace Horse
{
    template <typename Frame>
    static inline const char* RollbackPreallocatedHgCpuCapacityFailure(
        const Frame& frame,
        size_t hgcpu_bytes,
        size_t khit_nodes,
        size_t motion_bytes,
        size_t motion_control_bytes,
        size_t motion_tail_bytes,
        size_t skeleton_inline_bytes,
        size_t timer_root_bytes,
        size_t timer_backing_bytes,
        size_t timer_nodes) noexcept
    {
        if (frame.bytes.capacity() < hgcpu_bytes)
            return "hgcpu-buffer-capacity-insufficient";
        for (const auto& topology : frame.khit_topology)
            if (topology.nodes.capacity() < khit_nodes)
                return "khit-node-capacity-insufficient";
        if (frame.motion_banks.bytes.capacity() < motion_bytes
            || frame.motion_banks.control_bytes.capacity()
                < motion_control_bytes
            || frame.motion_tail.bytes.capacity() < motion_tail_bytes)
            return "motion-capacity-insufficient";
        const auto& skeleton = frame.skeleton_runtime;
        if (!skeleton.chara[0] || !skeleton.chara[1]
            || skeleton.inline_bytes.size() != skeleton_inline_bytes
            || skeleton.inline_bytes.capacity() < skeleton_inline_bytes
            || skeleton.aux_nodes.size() > skeleton.aux_nodes.capacity()
            || skeleton.chains.size() > skeleton.chains.capacity()
            || skeleton.spring_nodes.size()
                > skeleton.spring_nodes.capacity())
            return "skeleton-template-capacity-insufficient";
        for (const auto& node : skeleton.aux_nodes)
        {
            if (!node.address || !node.vtable)
                return "skeleton-template-capacity-insufficient";
            if (node.bytes.empty()
                || node.bytes.size() > node.bytes.capacity())
                return "skeleton-aux-byte-capacity-insufficient";
        }
        for (const auto& chain : skeleton.chains)
            if (!chain.address)
                return "skeleton-template-capacity-insufficient";
        for (const auto& node : skeleton.spring_nodes)
        {
            if (!node.address || !node.vtable)
                return "skeleton-template-capacity-insufficient";
            if (node.bytes.empty()
                || node.bytes.size() > node.bytes.capacity())
                return "skeleton-spring-byte-capacity-insufficient";
        }
        if (frame.timer_node.root_bytes.capacity() < timer_root_bytes
            || frame.timer_node.backing_bytes.capacity()
                < timer_backing_bytes
            || frame.timer_node.nodes.capacity() < timer_nodes)
            return "timer-node-capacity-insufficient";
        return "ok";
    }
}
