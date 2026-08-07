#pragma once

#include "RollbackStateHash.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace Horse
{
    static constexpr size_t kRollbackHgCpuCanonicalKHitNodeStreamStart =
        0x79AC + 0x41C;

    struct RollbackHgCpuIgnoreRange
    {
        size_t offset {0};
        size_t bytes {0};
        const char* reason {"unspecified"};
    };

    // Sorted by character-local offset so canonical hashing can walk these
    // ranges once instead of performing a linear search for every byte.
    static constexpr RollbackHgCpuIgnoreRange kRollbackHgCpuIgnoreRanges[] = {
        {0x260, 0x0C, "native reader/VFX dispatcher owns chara+0x270 restore-slot flags"},
        {0x290, 0x10, "UpdateLookAtIKTarget rebuilds chara+0x2A0 look-at target vector A"},
        {0x2A0, 0x10, "UpdateLookAtIKTarget rebuilds chara+0x2B0 look-at source vector A"},
        {0x2C0, 0x10, "UpdateLookAtIKTarget rebuilds chara+0x2D0 look-at target vector B"},
        {0x2D0, 0x10, "UpdateLookAtIKTarget rebuilds chara+0x2E0 look-at source/temp vector B"},
        {0x16C0, 0x180, "rebuilt FLuxBattleChara motion-input flag/history"},
        {0x2144, 0x1020, "rebuilt FLuxBattleChara input/command-history window"},
        {0x3580, 0x10, "native reader/VFX dispatcher owns chara+0x3590 tree holder"},
        {0x3590, 0x1840, "native solved-pose payload; peer canonical uses verified future-gameplay transforms from motion history"},
        {0x4DD0, 0x800, "native secondary solved-pose payload; local restore and presentation state"},
        {0x5648, 0x08, "LastHitSourceCellLo48 process-address token; classifier at 0x5640 remains canonical"},
        {0x5670, 0x30, "native reader rewrites chara+0x43DF0 self-pointer block"},
        {0x5778, 0x10, "native reader rewrites lane-state helper pointers"},
        {0x5BAC, 0x10, "FLuxMoveLane[0] padding"},
        {0x5BC2, 0x02, "FLuxMoveLane[0] padding"},
        {0x5BE0, 0x10, "native reader rewrites FLuxMoveLane[0] playback-slot pointers"},
        {0x5BF8, 0x04, "FLuxMoveLane[0] padding"},
        {0x5F6C, 0x04, "FLuxMoveLane[0] reserved tail"},
        {0x6014, 0x10, "FLuxMoveLane[1] padding"},
        {0x602A, 0x02, "FLuxMoveLane[1] padding"},
        {0x6048, 0x10, "native reader rewrites FLuxMoveLane[1] playback-slot pointers"},
        {0x6060, 0x04, "FLuxMoveLane[1] padding"},
        {0x63D4, 0x04, "FLuxMoveLane[1] reserved tail"},
        {0x6430, 0x08, "FLuxMotionPlaybackSlot[0] padding"},
        {0x6458, 0x12, "FLuxMotionPlaybackSlot[0] padding"},
        {0x6484, 0x04, "FLuxMotionPlaybackSlot[0] reserved tail"},
        {0x64E0, 0x08, "FLuxMotionPlaybackSlot[1] padding"},
        {0x6508, 0x12, "FLuxMotionPlaybackSlot[1] padding"},
        {0x6534, 0x04, "FLuxMotionPlaybackSlot[1] reserved tail"},
        {0x6590, 0x08, "FLuxMotionPlaybackSlot[2] padding"},
        {0x65B8, 0x12, "FLuxMotionPlaybackSlot[2] padding"},
        {0x65E4, 0x04, "FLuxMotionPlaybackSlot[2] reserved tail"},
        {0x6640, 0x08, "FLuxMotionPlaybackSlot[3] padding"},
        {0x6668, 0x12, "FLuxMotionPlaybackSlot[3] padding"},
        {0x6694, 0x04, "FLuxMotionPlaybackSlot[3] reserved tail"},
        {0x66F0, 0x08, "FLuxMotionPlaybackSlot[4] padding"},
        {0x6718, 0x12, "FLuxMotionPlaybackSlot[4] padding"},
        {0x6744, 0x04, "FLuxMotionPlaybackSlot[4] reserved tail"},
        {0x6748, 0x11F0, "native reader/VFX dispatcher canonicalizes chara+0x95FA0 effect-anchor block"},
        {0x794C, 0x18, "AI reset-slot process identity tuple; stable palette scalars start at +0x18"},
    };

    // These counters are process-local absolute progress representations, so
    // peers omit them from canonical comparison. They deliberately do not
    // belong to kRollbackHgCpuIgnoreRanges: raw restore verification must
    // still reject a counter byte that fails to round-trip in one process.
    static constexpr RollbackHgCpuIgnoreRange
        kRollbackHgCpuPeerCanonicalOnlyIgnoreRanges[] = {
            {0x63DC, 0x04, "FLuxMotionPlaybackSlot[0] absolute advance counter"},
            {0x648C, 0x04, "FLuxMotionPlaybackSlot[1] absolute advance counter"},
            {0x653C, 0x04, "FLuxMotionPlaybackSlot[2] absolute advance counter"},
            {0x65EC, 0x04, "FLuxMotionPlaybackSlot[3] absolute advance counter"},
            {0x669C, 0x04, "FLuxMotionPlaybackSlot[4] absolute advance counter"},
        };

    static inline bool RollbackHgCpuRestoreLocalOffsetIgnored(
        size_t local_offset,
        const char** reason_out = nullptr) noexcept
    {
        for (const auto& range : kRollbackHgCpuIgnoreRanges)
        {
            if (local_offset >= range.offset
                && local_offset < range.offset + range.bytes)
            {
                if (reason_out) *reason_out = range.reason;
                return true;
            }
        }
        if (reason_out) *reason_out = nullptr;
        return false;
    }

    static constexpr bool RollbackHgCpuPlaybackRampStepOffset(
        size_t local_offset) noexcept
    {
        constexpr size_t kSlotsStart = 0x63D8;
        constexpr size_t kSlotBytes = 0xB0;
        constexpr size_t kSlotCount = 5;
        if (local_offset < kSlotsStart
            || local_offset >= kSlotsStart + kSlotBytes * kSlotCount)
        {
            return false;
        }
        const size_t in_slot = (local_offset - kSlotsStart) % kSlotBytes;
        return in_slot >= 0x1C && in_slot < 0x20;
    }

    static inline bool RollbackHgCpuPlaybackRampInactive(
        const uint8_t* bytes,
        size_t record_limit,
        size_t ramp_step_offset) noexcept
    {
        constexpr size_t kSlotsStart = 0x63D8;
        constexpr size_t kSlotBytes = 0xB0;
        if (!bytes
            || !RollbackHgCpuPlaybackRampStepOffset(ramp_step_offset))
        {
            return false;
        }
        const size_t slot =
            (ramp_step_offset - kSlotsStart) / kSlotBytes;
        const size_t remaining_offset =
            kSlotsStart + slot * kSlotBytes + 0x18;
        if (remaining_offset > record_limit
            || sizeof(uint32_t) > record_limit - remaining_offset)
        {
            return false;
        }
        uint32_t remaining_bits = 0;
        std::memcpy(
            &remaining_bits, bytes + remaining_offset,
            sizeof(remaining_bits));
        // +0 and -0 both mean that the native interpolation is inactive.
        return (remaining_bits & 0x7FFFFFFFu) == 0;
    }

    static inline bool RollbackAddHgCpuCanonicalCharaBytes(
        RollbackFastHash& hash,
        const uint8_t* bytes,
        size_t record_limit,
        size_t local_begin,
        size_t khit_node_stream_start) noexcept
    {
        if (!bytes && record_limit != 0) return false;
        size_t range_index = 0;
        size_t peer_range_index = 0;
        size_t local = (std::min)(local_begin, record_limit);
        while (local < record_limit)
        {
            while (range_index < std::size(kRollbackHgCpuIgnoreRanges)
                   && kRollbackHgCpuIgnoreRanges[range_index].offset
                        + kRollbackHgCpuIgnoreRanges[range_index].bytes
                        <= local)
            {
                ++range_index;
            }
            while (peer_range_index
                        < std::size(
                            kRollbackHgCpuPeerCanonicalOnlyIgnoreRanges)
                   && kRollbackHgCpuPeerCanonicalOnlyIgnoreRanges[
                            peer_range_index].offset
                        + kRollbackHgCpuPeerCanonicalOnlyIgnoreRanges[
                            peer_range_index].bytes
                        <= local)
            {
                ++peer_range_index;
            }

            const bool khit_node_stream = local >= khit_node_stream_start;
            const bool restore_ignored = range_index
                    < std::size(kRollbackHgCpuIgnoreRanges)
                && local >= kRollbackHgCpuIgnoreRanges[range_index].offset;
            const bool peer_only_ignored = peer_range_index
                    < std::size(
                        kRollbackHgCpuPeerCanonicalOnlyIgnoreRanges)
                && local >= kRollbackHgCpuPeerCanonicalOnlyIgnoreRanges[
                        peer_range_index].offset;
            if (khit_node_stream || restore_ignored || peer_only_ignored)
            {
                const size_t zero_end = khit_node_stream
                    ? record_limit
                    : (std::min)(record_limit,
                        restore_ignored
                            ? kRollbackHgCpuIgnoreRanges[range_index].offset
                                + kRollbackHgCpuIgnoreRanges[
                                    range_index].bytes
                            : kRollbackHgCpuPeerCanonicalOnlyIgnoreRanges[
                                    peer_range_index].offset
                                + kRollbackHgCpuPeerCanonicalOnlyIgnoreRanges[
                                    peer_range_index].bytes);
                hash.add_zero_bytes(zero_end - local);
                local = zero_end;
                continue;
            }

            size_t live_end = (std::min)(
                record_limit, khit_node_stream_start);
            if (range_index < std::size(kRollbackHgCpuIgnoreRanges))
            {
                live_end = (std::min)(
                    live_end,
                    kRollbackHgCpuIgnoreRanges[range_index].offset);
            }
            if (peer_range_index
                < std::size(kRollbackHgCpuPeerCanonicalOnlyIgnoreRanges))
            {
                live_end = (std::min)(live_end,
                    kRollbackHgCpuPeerCanonicalOnlyIgnoreRanges[
                        peer_range_index].offset);
            }
            constexpr size_t kTransformSlotStart = 0x63D8;
            constexpr size_t kTransformSlotBytes = 0xB0;
            constexpr size_t kTransformSlotCount = 5;
            size_t transform_slot = 0;
            size_t ramp_slot = 0;
            while (local < live_end)
            {
                while (transform_slot < kTransformSlotCount
                       && kTransformSlotStart
                            + transform_slot * kTransformSlotBytes
                            + 0x80 <= local)
                {
                    ++transform_slot;
                }
                const size_t transform_start = transform_slot
                        < kTransformSlotCount
                    ? kTransformSlotStart
                        + transform_slot * kTransformSlotBytes + 0x60
                    : live_end;
                const size_t transform_end = transform_slot
                        < kTransformSlotCount
                    ? transform_start + 0x20
                    : live_end;
                while (ramp_slot < kTransformSlotCount
                       && kTransformSlotStart
                            + ramp_slot * kTransformSlotBytes
                            + 0x20 <= local)
                {
                    ++ramp_slot;
                }
                const size_t ramp_step_start = ramp_slot
                        < kTransformSlotCount
                    ? kTransformSlotStart
                        + ramp_slot * kTransformSlotBytes + 0x1C
                    : live_end;
                const size_t ramp_step_end = ramp_slot
                        < kTransformSlotCount
                    ? ramp_step_start + sizeof(uint32_t)
                    : live_end;
                const size_t next_special = (std::min)(
                    transform_start, ramp_step_start);
                if (local < next_special)
                {
                    const size_t bulk_end = (std::min)(
                        live_end, next_special);
                    hash.add_bytes(bytes + local, bulk_end - local);
                    local = bulk_end;
                    continue;
                }
                if (local < ramp_step_end
                    && local >= ramp_step_start)
                {
                    const size_t step_end = (std::min)(
                        live_end, ramp_step_end);
                    if (RollbackHgCpuPlaybackRampInactive(
                            bytes, record_limit, ramp_step_start))
                    {
                        hash.add_zero_bytes(step_end - local);
                    }
                    else
                    {
                        hash.add_bytes(bytes + local, step_end - local);
                    }
                    local = step_end;
                    if (local >= ramp_step_end) ++ramp_slot;
                    continue;
                }
                if (local < transform_end)
                {
                    const size_t cache_end = (std::min)(
                        live_end, transform_end);
                    hash.add_zero_bytes(cache_end - local);
                    local = cache_end;
                    continue;
                }
                ++transform_slot;
            }
        }
        return true;
    }

    static inline uint64_t RollbackHashHgCpuCanonicalCharaChunkBytes(
        const uint8_t* bytes,
        size_t effective_record_bytes,
        size_t player,
        size_t local_begin,
        size_t local_limit,
        size_t khit_node_stream_start =
            kRollbackHgCpuCanonicalKHitNodeStreamStart) noexcept
    {
        RollbackFastHash hash {};
        hash.add_scalar(player);
        hash.add_scalar(local_begin);
        hash.add_scalar(local_limit);
        const size_t record_limit = (std::min)(
            effective_record_bytes, local_limit);
        if (!RollbackAddHgCpuCanonicalCharaBytes(
                hash, bytes, record_limit, local_begin,
                khit_node_stream_start))
        {
            return 0;
        }
        return hash.finish();
    }

    static constexpr bool RollbackHgCpuCanonicalRestoreEvidenceMatches(
        uint64_t expected_canonical_hash,
        uint64_t observed_canonical_hash,
        bool motion_bank_match,
        size_t motion_bank_mismatch_count,
        bool motion_tail_match,
        bool secondary_event_stack_match,
        bool timer_node_match,
        size_t unignored_mismatch_count) noexcept
    {
        return expected_canonical_hash != 0
            && expected_canonical_hash == observed_canonical_hash
            && motion_bank_match
            && motion_bank_mismatch_count == 0
            && motion_tail_match
            && secondary_event_stack_match
            && timer_node_match
            && unignored_mismatch_count == 0;
    }

    static constexpr bool RollbackHgCpuPlaybackTransformCacheOffset(
        size_t local_offset) noexcept
    {
        constexpr size_t kSlotsStart = 0x63D8;
        constexpr size_t kSlotBytes = 0xB0;
        constexpr size_t kSlotCount = 5;
        if (local_offset < kSlotsStart
            || local_offset >= kSlotsStart + kSlotBytes * kSlotCount)
        {
            return false;
        }
        const size_t in_slot = (local_offset - kSlotsStart) % kSlotBytes;
        return in_slot >= 0x60 && in_slot < 0x80;
    }

    static constexpr bool RollbackHgCpuPlaybackAbsoluteCounterOffset(
        size_t local_offset) noexcept
    {
        constexpr size_t kSlotsStart = 0x63D8;
        constexpr size_t kSlotBytes = 0xB0;
        constexpr size_t kSlotCount = 5;
        if (local_offset < kSlotsStart
            || local_offset >= kSlotsStart + kSlotBytes * kSlotCount)
        {
            return false;
        }
        return (local_offset - kSlotsStart) % kSlotBytes == 0x04;
    }

}
