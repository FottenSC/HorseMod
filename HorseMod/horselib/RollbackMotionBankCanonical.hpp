// ============================================================================
// Horse::RollbackMotionBankCanonical
//
// Pointer-free logical identity for SC6's three-slot character matrix banks.
// ============================================================================

#pragma once

#include "RollbackStateHash.hpp"

#include <cstddef>
#include <cstdint>

namespace Horse
{
    static constexpr size_t kRollbackMotionBankPlayerCount = 2;
    static constexpr size_t kRollbackMotionBankCount = 2;
    static constexpr size_t kRollbackMotionBankBufferCount = 3;
    // Ghidra: every primary CMatrixBank slot is a fixed 0xC000-byte allocation
    // (768 FMatrix64 entries). CopyAllPlayerPrimaryBoneMatrices consumes the
    // authored count at native PLAYER +0x42550 without the native HgCpu
    // writer's narrower 0x1840-byte prefix. Preserve the complete physical
    // slot so characters whose authored skeleton exceeds 97 matrices remain
    // admissible and exact across rollback.
    static constexpr int32_t kRollbackPrimaryMotionBankMatrixCapacity = 768;
    static constexpr size_t kRollbackPrimaryMotionBankBytes = 0xC000;
    static constexpr size_t kRollbackSecondaryMotionBankBytes = 0x800;
    static constexpr size_t kRollbackMotionBankBytes[
        kRollbackMotionBankCount] = {
        kRollbackPrimaryMotionBankBytes,
        kRollbackSecondaryMotionBankBytes,
    };
    static constexpr size_t kRollbackMotionBankMatrixBytes = 0x40;
    static constexpr size_t kRollbackMotionBankGameplayBones[] = {
        0, 1, 2, 0x17,
    };

    constexpr bool RollbackPrimaryMotionBankCountAdmitted(
        int32_t authored_matrix_count) noexcept
    {
        return authored_matrix_count > 0
            && authored_matrix_count
                <= kRollbackPrimaryMotionBankMatrixCapacity;
    }

    static inline size_t RollbackMotionBankTotalBytes() noexcept
    {
        size_t per_player = 0;
        for (const size_t bytes : kRollbackMotionBankBytes)
            per_player += bytes * kRollbackMotionBankBufferCount;
        return per_player * kRollbackMotionBankPlayerCount;
    }

    static inline size_t RollbackMotionBankByteOffset(
        size_t player,
        size_t bank,
        size_t buffer) noexcept
    {
        size_t per_player = 0;
        for (const size_t bytes : kRollbackMotionBankBytes)
            per_player += bytes * kRollbackMotionBankBufferCount;

        size_t bank_base = player * per_player;
        for (size_t i = 0; i < bank && i < kRollbackMotionBankCount; ++i)
            bank_base += kRollbackMotionBankBytes[i]
                * kRollbackMotionBankBufferCount;
        return bank_base + buffer * kRollbackMotionBankBytes[bank];
    }

    template <typename MotionHistory>
    constexpr int RollbackMotionBankTimelineFrameSlot(
        const MotionHistory& motion, size_t player, size_t bank) noexcept
    {
        if (!motion.ok || player >= kRollbackMotionBankPlayerCount
            || bank >= kRollbackMotionBankCount)
            return -1;
        const int current = motion.current_slot[player][bank];
        return current >= 0
                && current < static_cast<int>(kRollbackMotionBankBufferCount)
            ? current : -1;
    }

    template <typename MotionHistory>
    static inline bool RollbackMotionBankLogicalPreviousLocation(
        const MotionHistory& motion,
        size_t player,
        size_t bank,
        uint32_t& provider_age,
        size_t& provider_offset,
        size_t& provider_bytes) noexcept
    {
        provider_age = 0;
        provider_offset = 0;
        provider_bytes = 0;
        if (!motion.ok || player >= kRollbackMotionBankPlayerCount
            || bank >= kRollbackMotionBankCount)
            return false;

        const int current = motion.current_slot[player][bank];
        const int provider = motion.provider_slot[player][bank];
        if (current < 0 || provider < 0
            || current >= static_cast<int>(kRollbackMotionBankBufferCount)
            || provider >= static_cast<int>(kRollbackMotionBankBufferCount))
            return false;

        provider_age = static_cast<uint32_t>(
            (provider - current
                + static_cast<int>(kRollbackMotionBankBufferCount))
            % static_cast<int>(kRollbackMotionBankBufferCount));
        provider_offset = RollbackMotionBankByteOffset(
            player, bank, static_cast<size_t>(provider));
        provider_bytes = kRollbackMotionBankBytes[bank];
        if (provider_offset > motion.bytes.size()
            || provider_bytes > motion.bytes.size() - provider_offset)
            return false;
        return true;
    }

    template <typename MotionHistory>
    static inline bool RollbackMotionBankLogicalCurrentLocation(
        const MotionHistory& motion,
        size_t player,
        size_t bank,
        size_t& current_offset,
        size_t& current_bytes) noexcept
    {
        current_offset = 0;
        current_bytes = 0;
        if (!motion.ok || player >= kRollbackMotionBankPlayerCount
            || bank >= kRollbackMotionBankCount)
            return false;

        const int current = motion.current_slot[player][bank];
        if (current < 0
            || current >= static_cast<int>(kRollbackMotionBankBufferCount))
            return false;

        current_offset = RollbackMotionBankByteOffset(
            player, bank, static_cast<size_t>(current));
        current_bytes = kRollbackMotionBankBytes[bank];
        return current_offset <= motion.bytes.size()
            && current_bytes <= motion.bytes.size() - current_offset;
    }

    template <typename MotionHistory>
    static inline bool RollbackAddMotionBankPeerState(
        RollbackFastHash& hash,
        const MotionHistory& motion) noexcept
    {
        if (!motion.ok) return false;
        for (size_t player = 0; player < kRollbackMotionBankPlayerCount;
             ++player)
        {
            for (size_t bank = 0; bank < kRollbackMotionBankCount; ++bank)
            {
                // AdvanceRingBuffer makes the logical current image the next
                // tick's previous image. Before that rotation, the prior
                // current primary bones 0 and 2 feed physics/facing; after it,
                // bone 1 feeds root-motion and bone 0x17 feeds conditional
                // pose repair. Hash those four complete matrices. Same-frame
                // KHit results are covered by their own canonical state. The
                // remaining solved pose, secondary bank, and old provider are
                // local restore/presentation state.
                uint32_t provider_age = 0;
                size_t provider_offset = 0;
                size_t provider_bytes = 0;
                size_t current_offset = 0;
                size_t current_bytes = 0;
                if (!RollbackMotionBankLogicalPreviousLocation(
                        motion, player, bank, provider_age,
                        provider_offset, provider_bytes)
                    || !RollbackMotionBankLogicalCurrentLocation(
                        motion, player, bank, current_offset, current_bytes))
                    return false;
                hash.add_scalar(provider_age);
                if (bank == 0)
                {
                    for (const size_t bone :
                         kRollbackMotionBankGameplayBones)
                    {
                        const size_t offset =
                            bone * kRollbackMotionBankMatrixBytes;
                        if (offset > current_bytes
                            || kRollbackMotionBankMatrixBytes
                                > current_bytes - offset)
                            return false;
                        hash.add_bytes(
                            motion.bytes.data() + current_offset + offset,
                            kRollbackMotionBankMatrixBytes);
                    }
                }
            }
        }
        return true;
    }

    template <typename MotionHistory>
    static inline uint64_t RollbackHashMotionBankPeerState(
        const MotionHistory& motion) noexcept
    {
        RollbackFastHash hash {};
        if (!RollbackAddMotionBankPeerState(hash, motion))
            return 0;
        return hash.finish();
    }
}
