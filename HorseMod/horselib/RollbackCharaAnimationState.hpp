// ============================================================================
// Horse::RollbackCharaAnimationState
//
// Exact local rewind and pointer-free peer state for SC6's auxiliary
// character clip player and heap-backed Enshutsu pose/event scheduler.
//
// The scheduler owns a reference-counted circular trigger list. Reconfiguration
// frees and rebuilds that list, so restore is deliberately fail-closed unless
// the live pointer graph is still identical to the captured graph. We restore
// semantic scalar payloads only; native ownership pointers and list links are
// never written from a historical snapshot.
// ============================================================================

#pragma once

#include "RollbackStateHash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    static constexpr uintptr_t kRollbackCharaAnimClipPlayerOffset = 0x95ED0;
    static constexpr size_t kRollbackCharaAnimClipPlayerBytes = 0x30;
    static constexpr uintptr_t kRollbackCharaAnimSlotControllerOffset = 0x95EC0;
    static constexpr size_t kRollbackCharaAnimClipPlayerBindingOffset = 0x08;
    static constexpr size_t kRollbackCharaAnimClipPlayerBindingBytes = 0x08;
    static constexpr size_t kRollbackCharaAnimClipPlayerScalarOffset = 0x10;
    static constexpr size_t kRollbackCharaAnimClipPlayerScalarBytes = 0x20;
    static constexpr uintptr_t kRollbackCharaAnimRuntimeOffset = 0x2B270;
    static constexpr size_t kRollbackCharaAnimRuntimeBytes = 0x10;
    static constexpr size_t kRollbackCharaAnimRuntimeScalarOffset = 0x08;
    static constexpr size_t kRollbackCharaAnimRuntimeScalarBytes = 0x08;
    static constexpr size_t kRollbackCharaAnimClipPlayerActiveOffset = 0x28;

    static constexpr uintptr_t kRollbackPoseEventCueOwnerOffset = 0x95720;
    static constexpr size_t kRollbackPoseEventCueOwnerBytes = 0x38;
    static constexpr size_t kRollbackPoseEventCueOwnerScalarOffset = 0x08;
    static constexpr size_t kRollbackPoseEventCueOwnerScalarBytes = 0x20;
    static constexpr size_t kRollbackPoseEventCueOwnerEnstOffset = 0x28;
    static constexpr size_t kRollbackPoseEventCueOwnerSchedulerOffset = 0x30;

    static constexpr size_t kRollbackEnshutsuSchedulerBytes = 0x80;
    static constexpr size_t kRollbackEnshutsuSchedulerScalarOffset = 0x10;
    static constexpr size_t kRollbackEnshutsuSchedulerScalarBytes = 0x60;
    // Native construction/configuration initialize +0x10..+0x6B. The final
    // word at +0x6C is never written by the constructor, configuration path,
    // or tick and contains allocator-dependent residue. Retain it in the
    // exact local image, but never make it peer-canonical.
    static constexpr size_t kRollbackEnshutsuSchedulerCanonicalBytes = 0x5C;
    static constexpr size_t kRollbackEnshutsuSchedulerAllocatorResidueOffset =
        0x6C;
    static constexpr size_t kRollbackEnshutsuSchedulerAllocatorResidueBytes =
        0x04;
    static constexpr size_t kRollbackEnshutsuSchedulerListHeadOffset = 0x70;
    static constexpr size_t kRollbackEnshutsuSchedulerListCountOffset = 0x78;
    static constexpr size_t kRollbackEnshutsuListNodeBytes = 0x20;
    static constexpr size_t kRollbackEnshutsuTriggerBytes = 0x20;
    static constexpr size_t kRollbackEnshutsuTriggerScalarOffset = 0x08;
    static constexpr size_t kRollbackEnshutsuTriggerScalarBytes = 0x18;
    static constexpr size_t kRollbackEnshutsuMaximumTriggers = 64;

    template<typename T>
    static inline T RollbackAnimationReadScalar(
        const uint8_t* bytes,
        size_t offset) noexcept
    {
        T value {};
        if (bytes) std::memcpy(&value, bytes + offset, sizeof(value));
        return value;
    }

    struct RollbackEnshutsuTriggerHistory
    {
        uintptr_t node {0};
        uintptr_t next {0};
        uintptr_t previous {0};
        uintptr_t object {0};
        uintptr_t control {0};
        uintptr_t object_vtable {0};
        std::array<uint8_t, kRollbackEnshutsuListNodeBytes> node_bytes {};
        std::array<uint8_t, kRollbackEnshutsuTriggerBytes> trigger_bytes {};
    };

    struct RollbackCharaAnimationPlayerHistory
    {
        uintptr_t chara {0};
        uintptr_t clip_packed_data_owner {0};
        std::array<uint8_t, kRollbackCharaAnimClipPlayerBytes> clip {};
        std::array<uint8_t, kRollbackCharaAnimRuntimeBytes> clip_runtime {};
        std::array<uint8_t, kRollbackPoseEventCueOwnerBytes> owner {};
        std::array<uint8_t, kRollbackEnshutsuSchedulerBytes> scheduler {};
        uintptr_t scheduler_address {0};
        uintptr_t list_head {0};
        uintptr_t list_head_next {0};
        uintptr_t list_head_previous {0};
        uint32_t trigger_count {0};
        std::array<
            RollbackEnshutsuTriggerHistory,
            kRollbackEnshutsuMaximumTriggers> triggers {};
    };

    struct RollbackCharaAnimationStateHistory
    {
        bool ok {false};
        std::array<RollbackCharaAnimationPlayerHistory, 2> players {};
        uint64_t canonical_hash {0};
        uint64_t integrity_hash {0};

        void clear() noexcept
        {
            ok = false;
            players = {};
            canonical_hash = 0;
            integrity_hash = 0;
        }

        void recycle_for_capture() noexcept
        {
            clear();
        }
    };

    static inline uint64_t RollbackHashCharaAnimationIntegrity(
        const RollbackCharaAnimationStateHistory& history) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(history.ok);
        for (const auto& player : history.players)
        {
            hash.add_scalar(player.chara);
            hash.add_scalar(player.clip_packed_data_owner);
            hash.add_bytes(player.clip.data(), player.clip.size());
            hash.add_bytes(
                player.clip_runtime.data(), player.clip_runtime.size());
            hash.add_bytes(player.owner.data(), player.owner.size());
            hash.add_bytes(player.scheduler.data(), player.scheduler.size());
            hash.add_scalar(player.scheduler_address);
            hash.add_scalar(player.list_head);
            hash.add_scalar(player.list_head_next);
            hash.add_scalar(player.list_head_previous);
            hash.add_scalar(player.trigger_count);
            for (uint32_t index = 0; index < player.trigger_count; ++index)
            {
                const auto& trigger = player.triggers[index];
                hash.add_scalar(trigger.node);
                hash.add_scalar(trigger.next);
                hash.add_scalar(trigger.previous);
                hash.add_scalar(trigger.object);
                hash.add_scalar(trigger.control);
                hash.add_scalar(trigger.object_vtable);
                hash.add_bytes(
                    trigger.node_bytes.data(), trigger.node_bytes.size());
                hash.add_bytes(
                    trigger.trigger_bytes.data(),
                    trigger.trigger_bytes.size());
            }
        }
        return hash.value;
    }

    static inline uint64_t RollbackHashCharaAnimationCanonical(
        const RollbackCharaAnimationStateHistory& history) noexcept
    {
        if (!history.ok) return 0;
        RollbackHash hash {};
        for (const auto& player : history.players)
        {
            hash.add_bytes(
                player.clip.data()
                    + kRollbackCharaAnimClipPlayerScalarOffset,
                kRollbackCharaAnimClipPlayerScalarBytes);
            // Native slot-controller callbacks can clear this authored-data
            // pointer and cursor between actor ticks after the outer player
            // is already inactive. The runtime has no native consumer in
            // that state. Retain its bytes for exact local rewind, but make
            // them peer-semantic only while native code can consume them.
            const bool clip_active =
                RollbackAnimationReadScalar<uint32_t>(
                    player.clip.data(),
                    kRollbackCharaAnimClipPlayerActiveOffset) != 0;
            if (clip_active)
            {
                hash.add_scalar(
                    RollbackAnimationReadScalar<uintptr_t>(
                        player.clip_runtime.data(), 0) != 0);
                hash.add_bytes(
                    player.clip_runtime.data()
                        + kRollbackCharaAnimRuntimeScalarOffset,
                    kRollbackCharaAnimRuntimeScalarBytes);
            }
            hash.add_bytes(
                player.owner.data() + kRollbackPoseEventCueOwnerScalarOffset,
                kRollbackPoseEventCueOwnerScalarBytes);
            hash.add_bytes(
                player.scheduler.data()
                    + kRollbackEnshutsuSchedulerScalarOffset,
                kRollbackEnshutsuSchedulerCanonicalBytes);
            hash.add_scalar(player.trigger_count);
            for (uint32_t index = 0; index < player.trigger_count; ++index)
            {
                hash.add_bytes(
                    player.triggers[index].trigger_bytes.data()
                        + kRollbackEnshutsuTriggerScalarOffset,
                    kRollbackEnshutsuTriggerScalarBytes);
            }
        }
        return hash.value;
    }

    static inline bool RollbackCharaAnimationHistoryValid(
        const RollbackCharaAnimationStateHistory& history) noexcept
    {
        return history.ok
            && history.canonical_hash != 0
            && history.integrity_hash != 0
            && RollbackHashCharaAnimationCanonical(history)
                == history.canonical_hash
            && RollbackHashCharaAnimationIntegrity(history)
                == history.integrity_hash;
    }

    enum class RollbackAnimationDispatchSite : uint8_t
    {
        PerFrameTick = 0,
        PerTickAdvanceAll = 1,
    };

    struct RollbackAnimationDispatchLedger
    {
        int64_t native_coordinate {-1};
        uint32_t per_frame_tick_count {0};
        uint32_t per_tick_advance_all_count {0};
        bool owned_simulation {false};

        void begin(int64_t coordinate, bool owned) noexcept
        {
            native_coordinate = coordinate;
            per_frame_tick_count = 0;
            per_tick_advance_all_count = 0;
            owned_simulation = owned;
        }

        bool admit(
            int64_t coordinate,
            RollbackAnimationDispatchSite site) noexcept
        {
            if (coordinate != native_coordinate) return false;
            uint32_t& count =
                site == RollbackAnimationDispatchSite::PerFrameTick
                ? per_frame_tick_count : per_tick_advance_all_count;
            ++count;
            return count == 1;
        }

        bool owned_direct_iteration_valid() const noexcept
        {
            return owned_simulation
                && per_frame_tick_count == 1
                && per_tick_advance_all_count == 0;
        }
    };

    static inline bool RollbackMatchStartSchedulerContract(
        bool controller_waiting_for_scheduler,
        bool scheduler_active) noexcept
    {
        return controller_waiting_for_scheduler == scheduler_active;
    }
}

#if !defined(HORSE_ROLLBACK_ANIMATION_PURE_STATE_ONLY)
#include "SafeMemoryRead.hpp"

namespace Horse
{
    static inline bool RollbackReadAnimationPointer(
        uintptr_t address,
        uintptr_t& value) noexcept
    {
        value = 0;
        return address != 0
            && SafeReadBytes(
                reinterpret_cast<const void*>(address),
                &value,
                sizeof(value));
    }

    static inline bool RollbackCaptureCharaAnimationPlayer(
        uintptr_t chara,
        RollbackCharaAnimationPlayerHistory& player) noexcept
    {
        player = {};
        player.chara = chara;
        if (!chara
            || !RollbackReadAnimationPointer(
                chara + kRollbackCharaAnimSlotControllerOffset,
                player.clip_packed_data_owner)
            || !player.clip_packed_data_owner
            || !SafeReadBytes(
                reinterpret_cast<const void*>(
                    chara + kRollbackCharaAnimClipPlayerOffset),
                player.clip.data(), player.clip.size())
            || !SafeReadBytes(
                reinterpret_cast<const void*>(
                    chara + kRollbackCharaAnimRuntimeOffset),
                player.clip_runtime.data(), player.clip_runtime.size())
            || !SafeReadBytes(
                reinterpret_cast<const void*>(
                    chara + kRollbackPoseEventCueOwnerOffset),
                player.owner.data(), player.owner.size()))
        {
            return false;
        }

        player.scheduler_address =
            RollbackAnimationReadScalar<uintptr_t>(
                player.owner.data(),
                kRollbackPoseEventCueOwnerSchedulerOffset);
        if (!player.scheduler_address
            || !SafeReadBytes(
                reinterpret_cast<const void*>(player.scheduler_address),
                player.scheduler.data(), player.scheduler.size()))
        {
            return false;
        }
        const uintptr_t scheduler_chara =
            RollbackAnimationReadScalar<uintptr_t>(
                player.scheduler.data(), 0x08);
        player.list_head = RollbackAnimationReadScalar<uintptr_t>(
            player.scheduler.data(),
            kRollbackEnshutsuSchedulerListHeadOffset);
        const uint64_t native_count =
            RollbackAnimationReadScalar<uint64_t>(
                player.scheduler.data(),
                kRollbackEnshutsuSchedulerListCountOffset);
        if (scheduler_chara != chara
            || !player.list_head
            || native_count > kRollbackEnshutsuMaximumTriggers)
        {
            return false;
        }
        player.trigger_count = static_cast<uint32_t>(native_count);

        std::array<uint8_t, kRollbackEnshutsuListNodeBytes> head {};
        if (!SafeReadBytes(
                reinterpret_cast<const void*>(player.list_head),
                head.data(), head.size()))
        {
            return false;
        }
        player.list_head_next =
            RollbackAnimationReadScalar<uintptr_t>(head.data(), 0);
        player.list_head_previous =
            RollbackAnimationReadScalar<uintptr_t>(head.data(), 8);
        uintptr_t current = player.list_head_next;
        uintptr_t previous = player.list_head;
        for (uint32_t index = 0; index < player.trigger_count; ++index)
        {
            if (!current || current == player.list_head) return false;
            for (uint32_t prior = 0; prior < index; ++prior)
                if (player.triggers[prior].node == current) return false;

            auto& trigger = player.triggers[index];
            trigger.node = current;
            if (!SafeReadBytes(
                    reinterpret_cast<const void*>(current),
                    trigger.node_bytes.data(),
                    trigger.node_bytes.size()))
            {
                return false;
            }
            trigger.next = RollbackAnimationReadScalar<uintptr_t>(
                trigger.node_bytes.data(), 0);
            trigger.previous = RollbackAnimationReadScalar<uintptr_t>(
                trigger.node_bytes.data(), 8);
            trigger.object = RollbackAnimationReadScalar<uintptr_t>(
                trigger.node_bytes.data(), 0x10);
            trigger.control = RollbackAnimationReadScalar<uintptr_t>(
                trigger.node_bytes.data(), 0x18);
            if (trigger.previous != previous
                || !trigger.next
                || !trigger.object
                || !trigger.control
                || !SafeReadBytes(
                    reinterpret_cast<const void*>(trigger.object),
                    trigger.trigger_bytes.data(),
                    trigger.trigger_bytes.size()))
            {
                return false;
            }
            trigger.object_vtable =
                RollbackAnimationReadScalar<uintptr_t>(
                    trigger.trigger_bytes.data(), 0);
            if (!trigger.object_vtable) return false;
            previous = current;
            current = trigger.next;
        }
        if (current != player.list_head
            || (player.trigger_count == 0
                    ? (player.list_head_next != player.list_head
                        || player.list_head_previous != player.list_head)
                    : player.list_head_previous != previous))
        {
            return false;
        }
        return true;
    }

    static inline bool CaptureRollbackCharaAnimationState(
        const uintptr_t charas[2],
        RollbackCharaAnimationStateHistory& history) noexcept
    {
        history.recycle_for_capture();
        if (!charas
            || !RollbackCaptureCharaAnimationPlayer(
                charas[0], history.players[0])
            || !RollbackCaptureCharaAnimationPlayer(
                charas[1], history.players[1]))
        {
            history.clear();
            return false;
        }
        history.ok = true;
        history.canonical_hash =
            RollbackHashCharaAnimationCanonical(history);
        history.integrity_hash =
            RollbackHashCharaAnimationIntegrity(history);
        if (!history.canonical_hash || !history.integrity_hash)
        {
            history.clear();
            return false;
        }
        return true;
    }

    enum class RollbackCharaAnimationPreflightFailure : uint8_t
    {
        None = 0,
        InvalidHistory,
        LiveCaptureFailed,
        CharaIdentity,
        SchedulerIdentity,
        ListHeadIdentity,
        ListHeadNextIdentity,
        ListHeadPreviousIdentity,
        TriggerCount,
        ClipOwnerIdentity,
        ClipPackedDataOwnerIdentity,
        ClipDataIdentity,
        ClipDataUnreadable,
        CueOwnerVtableIdentity,
        CueOwnerEnstIdentity,
        CueOwnerSchedulerIdentity,
        SchedulerVtableIdentity,
        SchedulerCharaIdentity,
        TriggerNodeIdentity,
        TriggerNextIdentity,
        TriggerPreviousIdentity,
        TriggerObjectIdentity,
        TriggerControlIdentity,
        TriggerVtableIdentity,
    };

    static inline const char* RollbackCharaAnimationPreflightFailureName(
        RollbackCharaAnimationPreflightFailure failure) noexcept
    {
        switch (failure)
        {
        case RollbackCharaAnimationPreflightFailure::None:
            return "chara-animation-restore-preflight-ok";
        case RollbackCharaAnimationPreflightFailure::InvalidHistory:
            return "chara-animation-preflight-invalid-history";
        case RollbackCharaAnimationPreflightFailure::LiveCaptureFailed:
            return "chara-animation-preflight-live-capture-failed";
        case RollbackCharaAnimationPreflightFailure::CharaIdentity:
            return "chara-animation-preflight-chara-identity";
        case RollbackCharaAnimationPreflightFailure::SchedulerIdentity:
            return "chara-animation-preflight-scheduler-identity";
        case RollbackCharaAnimationPreflightFailure::ListHeadIdentity:
            return "chara-animation-preflight-list-head-identity";
        case RollbackCharaAnimationPreflightFailure::ListHeadNextIdentity:
            return "chara-animation-preflight-list-head-next-identity";
        case RollbackCharaAnimationPreflightFailure::ListHeadPreviousIdentity:
            return "chara-animation-preflight-list-head-previous-identity";
        case RollbackCharaAnimationPreflightFailure::TriggerCount:
            return "chara-animation-preflight-trigger-count";
        case RollbackCharaAnimationPreflightFailure::ClipOwnerIdentity:
            return "chara-animation-preflight-clip-owner-identity";
        case RollbackCharaAnimationPreflightFailure::ClipPackedDataOwnerIdentity:
            return "chara-animation-preflight-clip-packed-data-owner-identity";
        case RollbackCharaAnimationPreflightFailure::ClipDataIdentity:
            return "chara-animation-preflight-clip-data-identity";
        case RollbackCharaAnimationPreflightFailure::ClipDataUnreadable:
            return "chara-animation-preflight-clip-data-unreadable";
        case RollbackCharaAnimationPreflightFailure::CueOwnerVtableIdentity:
            return "chara-animation-preflight-cue-owner-vtable-identity";
        case RollbackCharaAnimationPreflightFailure::CueOwnerEnstIdentity:
            return "chara-animation-preflight-cue-owner-enst-identity";
        case RollbackCharaAnimationPreflightFailure::CueOwnerSchedulerIdentity:
            return "chara-animation-preflight-cue-owner-scheduler-identity";
        case RollbackCharaAnimationPreflightFailure::SchedulerVtableIdentity:
            return "chara-animation-preflight-scheduler-vtable-identity";
        case RollbackCharaAnimationPreflightFailure::SchedulerCharaIdentity:
            return "chara-animation-preflight-scheduler-chara-identity";
        case RollbackCharaAnimationPreflightFailure::TriggerNodeIdentity:
            return "chara-animation-preflight-trigger-node-identity";
        case RollbackCharaAnimationPreflightFailure::TriggerNextIdentity:
            return "chara-animation-preflight-trigger-next-identity";
        case RollbackCharaAnimationPreflightFailure::TriggerPreviousIdentity:
            return "chara-animation-preflight-trigger-previous-identity";
        case RollbackCharaAnimationPreflightFailure::TriggerObjectIdentity:
            return "chara-animation-preflight-trigger-object-identity";
        case RollbackCharaAnimationPreflightFailure::TriggerControlIdentity:
            return "chara-animation-preflight-trigger-control-identity";
        case RollbackCharaAnimationPreflightFailure::TriggerVtableIdentity:
            return "chara-animation-preflight-trigger-vtable-identity";
        }
        return "chara-animation-preflight-unknown";
    }

    static inline RollbackCharaAnimationPreflightFailure
    RollbackCharaAnimationRestorePreflightFailure(
        const uintptr_t charas[2],
        const RollbackCharaAnimationStateHistory& history) noexcept
    {
        using Failure = RollbackCharaAnimationPreflightFailure;
        if (!charas || !RollbackCharaAnimationHistoryValid(history))
            return Failure::InvalidHistory;
        RollbackCharaAnimationStateHistory live {};
        if (!CaptureRollbackCharaAnimationState(charas, live))
            return Failure::LiveCaptureFailed;
        for (size_t player_index = 0; player_index < 2; ++player_index)
        {
            const auto& expected = history.players[player_index];
            const auto& current = live.players[player_index];
            if (current.chara != expected.chara)
                return Failure::CharaIdentity;
            if (current.scheduler_address != expected.scheduler_address)
                return Failure::SchedulerIdentity;
            if (current.list_head != expected.list_head)
                return Failure::ListHeadIdentity;
            if (current.list_head_next != expected.list_head_next)
                return Failure::ListHeadNextIdentity;
            if (current.list_head_previous != expected.list_head_previous)
                return Failure::ListHeadPreviousIdentity;
            if (current.trigger_count != expected.trigger_count)
                return Failure::TriggerCount;
            if (RollbackAnimationReadScalar<uintptr_t>(current.clip.data(), 0)
                != RollbackAnimationReadScalar<uintptr_t>(expected.clip.data(), 0))
                return Failure::ClipOwnerIdentity;
            if (current.clip_packed_data_owner
                != expected.clip_packed_data_owner)
                return Failure::ClipPackedDataOwnerIdentity;
            const uintptr_t expected_clip_data =
                RollbackAnimationReadScalar<uintptr_t>(
                    expected.clip.data(),
                    kRollbackCharaAnimClipPlayerBindingOffset);
            std::array<uint8_t, 0x10> expected_clip_header {};
            if (expected_clip_data
                && !SafeReadBytes(
                    reinterpret_cast<const void*>(expected_clip_data),
                    expected_clip_header.data(), expected_clip_header.size()))
                return Failure::ClipDataUnreadable;
            if (RollbackAnimationReadScalar<uintptr_t>(current.owner.data(), 0)
                != RollbackAnimationReadScalar<uintptr_t>(expected.owner.data(), 0))
                return Failure::CueOwnerVtableIdentity;
            if (RollbackAnimationReadScalar<uintptr_t>(
                    current.owner.data(), kRollbackPoseEventCueOwnerEnstOffset)
                != RollbackAnimationReadScalar<uintptr_t>(
                    expected.owner.data(), kRollbackPoseEventCueOwnerEnstOffset))
                return Failure::CueOwnerEnstIdentity;
            if (RollbackAnimationReadScalar<uintptr_t>(
                    current.owner.data(), kRollbackPoseEventCueOwnerSchedulerOffset)
                != RollbackAnimationReadScalar<uintptr_t>(
                    expected.owner.data(), kRollbackPoseEventCueOwnerSchedulerOffset))
                return Failure::CueOwnerSchedulerIdentity;
            if (RollbackAnimationReadScalar<uintptr_t>(current.scheduler.data(), 0)
                != RollbackAnimationReadScalar<uintptr_t>(expected.scheduler.data(), 0))
                return Failure::SchedulerVtableIdentity;
            if (RollbackAnimationReadScalar<uintptr_t>(current.scheduler.data(), 8)
                != RollbackAnimationReadScalar<uintptr_t>(expected.scheduler.data(), 8))
                return Failure::SchedulerCharaIdentity;
            for (uint32_t index = 0; index < expected.trigger_count; ++index)
            {
                const auto& expected_trigger = expected.triggers[index];
                const auto& current_trigger = current.triggers[index];
                if (current_trigger.node != expected_trigger.node)
                    return Failure::TriggerNodeIdentity;
                if (current_trigger.next != expected_trigger.next)
                    return Failure::TriggerNextIdentity;
                if (current_trigger.previous != expected_trigger.previous)
                    return Failure::TriggerPreviousIdentity;
                if (current_trigger.object != expected_trigger.object)
                    return Failure::TriggerObjectIdentity;
                if (current_trigger.control != expected_trigger.control)
                    return Failure::TriggerControlIdentity;
                if (current_trigger.object_vtable != expected_trigger.object_vtable)
                    return Failure::TriggerVtableIdentity;
            }
        }
        return Failure::None;
    }

    static inline bool RollbackCharaAnimationRestorePreflight(
        const uintptr_t charas[2],
        const RollbackCharaAnimationStateHistory& history) noexcept
    {
        return RollbackCharaAnimationRestorePreflightFailure(charas, history)
            == RollbackCharaAnimationPreflightFailure::None;
    }

    static inline bool RestoreRollbackCharaAnimationState(
        const uintptr_t charas[2],
        const RollbackCharaAnimationStateHistory& history) noexcept
    {
        if (!RollbackCharaAnimationRestorePreflight(charas, history))
            return false;
        bool ok = true;
        for (size_t player_index = 0; player_index < 2; ++player_index)
        {
            const auto& player = history.players[player_index];
            // LuxMoveVM_ApplyCharaAnimSlotEntry writes the selected type-1
            // authored section at controller +0x18, which aliases this clip
            // player's +0x08 pClipData. The section is interior to the stable
            // packed-data allocation verified by preflight, so a rebind is
            // rollback state rather than an ownership-lifetime failure.
            ok &= SafeWriteBytes(
                reinterpret_cast<void*>(player.chara
                    + kRollbackCharaAnimClipPlayerOffset
                    + kRollbackCharaAnimClipPlayerBindingOffset),
                player.clip.data()
                    + kRollbackCharaAnimClipPlayerBindingOffset,
                kRollbackCharaAnimClipPlayerBindingBytes);
            ok &= SafeWriteBytes(
                reinterpret_cast<void*>(player.chara
                    + kRollbackCharaAnimClipPlayerOffset
                    + kRollbackCharaAnimClipPlayerScalarOffset),
                player.clip.data()
                    + kRollbackCharaAnimClipPlayerScalarOffset,
                kRollbackCharaAnimClipPlayerScalarBytes);
            // The owner-side runtime pointer is process-local and may become
            // null when a clip ends. Restore it with the selected section and
            // cursor so the clip-player transaction is not mixed across time.
            ok &= SafeWriteBytes(
                reinterpret_cast<void*>(player.chara
                    + kRollbackCharaAnimRuntimeOffset),
                player.clip_runtime.data(),
                player.clip_runtime.size());
            ok &= SafeWriteBytes(
                reinterpret_cast<void*>(player.chara
                    + kRollbackPoseEventCueOwnerOffset
                    + kRollbackPoseEventCueOwnerScalarOffset),
                player.owner.data()
                    + kRollbackPoseEventCueOwnerScalarOffset,
                kRollbackPoseEventCueOwnerScalarBytes);
            ok &= SafeWriteBytes(
                reinterpret_cast<void*>(player.scheduler_address
                    + kRollbackEnshutsuSchedulerScalarOffset),
                player.scheduler.data()
                    + kRollbackEnshutsuSchedulerScalarOffset,
                kRollbackEnshutsuSchedulerScalarBytes);
            for (uint32_t index = 0;
                 index < player.trigger_count; ++index)
            {
                const auto& trigger = player.triggers[index];
                ok &= SafeWriteBytes(
                    reinterpret_cast<void*>(trigger.object
                        + kRollbackEnshutsuTriggerScalarOffset),
                    trigger.trigger_bytes.data()
                        + kRollbackEnshutsuTriggerScalarOffset,
                    kRollbackEnshutsuTriggerScalarBytes);
            }
        }
        if (!ok) return false;

        RollbackCharaAnimationStateHistory verified {};
        return CaptureRollbackCharaAnimationState(charas, verified)
            && verified.integrity_hash == history.integrity_hash
            && verified.canonical_hash == history.canonical_hash;
    }
}
#endif
