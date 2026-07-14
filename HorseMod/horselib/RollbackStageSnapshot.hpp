// ============================================================================
// Horse::RollbackStageSnapshot
//
// Dynamic breakable-stage scalar capture.  Ghidra proves the manager's wall
// and barrier TArrays at +0x3A8/+0x3B8 and the actor scalar offsets below.
// Presentation objects and particle pointers are deliberately excluded.
// ============================================================================

#pragma once

#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

namespace Horse
{
    static constexpr uintptr_t kRollbackStageWallListOffset = 0x3A8;
    static constexpr uintptr_t kRollbackStageBarrierListOffset = 0x3B8;
    static constexpr uintptr_t kRollbackStageWallIdOffset = 0x450;
    static constexpr uintptr_t kRollbackStageWallBreakStateOffset = 0x468;
    static constexpr uintptr_t kRollbackStageWallFadeTimerOffset = 0x46C;
    static constexpr uintptr_t kRollbackStageWallFadeRateOffset = 0x470;
    static constexpr uintptr_t kRollbackStageWallIntactOpaqueOffset = 0x420;
    static constexpr uintptr_t kRollbackStageWallIntactTranslucentOffset = 0x428;
    static constexpr uintptr_t kRollbackStageWallBrokenOpaqueOffset = 0x430;
    static constexpr uintptr_t kRollbackStageWallBrokenTranslucentOffset = 0x438;
    static constexpr uintptr_t kRollbackStageWallBreakingOpaqueOffset = 0x440;
    static constexpr uintptr_t kRollbackStageWallBreakingTranslucentOffset = 0x448;
    static constexpr uintptr_t kRollbackStageBarrierIdOffset = 0x420;
    static constexpr uintptr_t kRollbackStageBarrierEnduranceOffset = 0x424;
    static constexpr uintptr_t kRollbackStageBarrierHitCountOffset = 0x468;
    static constexpr uintptr_t kRollbackStageBarrierFaceOffset = 0x400;
    static constexpr uintptr_t kRollbackStageBarrierBackOffset = 0x408;
    static constexpr uintptr_t kRollbackStageBarrierFloorOffset = 0x410;
    static constexpr uintptr_t kRollbackStageBarrierBreakingOffset = 0x418;
    static constexpr int32_t kRollbackStageMaxActorsPerKind = 256;

    struct RollbackStageArrayHeader
    {
        uintptr_t data {0};
        int32_t count {0};
        int32_t capacity {0};
    };

    static_assert(
        sizeof(RollbackStageArrayHeader) == 0x10,
        "UE TArray header must be 16 bytes");

    enum class RollbackBreakableActorKind : uint8_t
    {
        Wall = 1,
        Barrier = 2,
    };

    struct RollbackWallPresentationVisibility
    {
        bool valid {false};
        uintptr_t opaque_offset {0};
        uintptr_t translucent_offset {0};
        bool opaque_visible {false};
    };

    static inline RollbackWallPresentationVisibility
    ComputeRollbackWallPresentationVisibility(
        int32_t break_state,
        float fade_timer,
        float fade_rate) noexcept
    {
        RollbackWallPresentationVisibility result {};
        if (break_state == 0)
        {
            result.opaque_offset =
                kRollbackStageWallIntactOpaqueOffset;
            result.translucent_offset =
                kRollbackStageWallIntactTranslucentOffset;
        }
        else if (break_state == 1)
        {
            result.opaque_offset =
                kRollbackStageWallBreakingOpaqueOffset;
            result.translucent_offset =
                kRollbackStageWallBreakingTranslucentOffset;
        }
        else if (break_state == 2)
        {
            result.opaque_offset =
                kRollbackStageWallBrokenOpaqueOffset;
            result.translucent_offset =
                kRollbackStageWallBrokenTranslucentOffset;
        }
        else
        {
            return result;
        }
        result.opaque_visible =
            fade_rate == 0.0f && fade_timer >= 1.0f;
        result.valid = true;
        return result;
    }

    struct RollbackBreakableStageRecord
    {
        RollbackBreakableActorKind kind {
            RollbackBreakableActorKind::Wall};
        int32_t id {0};
        uint32_t ordinal {0};
        uintptr_t actor {0};
        int32_t scalar {0};
        int32_t endurance {0};
        float fade_timer {0.0f};
        float fade_rate {0.0f};
    };

    struct RollbackBreakableStageSnapshot
    {
        uintptr_t stage_actor_manager {0};
        std::vector<RollbackBreakableStageRecord> records;
        uint64_t stage_layout_digest {0};
        uint64_t actor_set_digest {0};
        uint64_t canonical_hash {0};
        uint64_t integrity_hash {0};

        void clear()
        {
            stage_actor_manager = 0;
            records.clear();
            stage_layout_digest = 0;
            actor_set_digest = 0;
            canonical_hash = 0;
            integrity_hash = 0;
        }
    };

    struct RollbackBreakableStageReport
    {
        bool ok {false};
        bool preflight_ok {false};
        bool emergency_captured {false};
        bool emergency_restored {false};
        bool verification_ok {false};
        uint32_t wall_count {0};
        uint32_t barrier_count {0};
        uint32_t restored_count {0};
        uintptr_t failed_actor {0};
        const char* failure {"not-run"};
    };

    struct RollbackBreakableStageHashes
    {
        uint64_t stage_layout_digest {0};
        uint64_t actor_set_digest {0};
        uint64_t canonical_hash {0};
        uint64_t integrity_hash {0};
    };

    static inline bool RollbackReadStageArrayHeader(
        uintptr_t manager,
        uintptr_t offset,
        RollbackStageArrayHeader& out) noexcept
    {
        out = {};
        if (!manager
            || !SafeReadBytes(
                reinterpret_cast<const void*>(manager + offset),
                &out,
                sizeof(out)))
        {
            return false;
        }
        return out.count >= 0
            && out.capacity >= out.count
            && out.capacity <= kRollbackStageMaxActorsPerKind
            && (out.count == 0 || out.data != 0);
    }

    static inline RollbackBreakableStageHashes
    ComputeRollbackBreakableStageHashes(
        const RollbackBreakableStageSnapshot& snapshot) noexcept
    {
        RollbackHash layout {};
        RollbackHash actors {};
        RollbackHash canonical {};
        RollbackHash integrity {};
        layout.add_scalar(snapshot.records.size());
        actors.add_scalar(snapshot.stage_actor_manager);
        integrity.add_scalar(snapshot.stage_actor_manager);
        for (const RollbackBreakableStageRecord& record : snapshot.records)
        {
            const uint8_t kind = static_cast<uint8_t>(record.kind);
            layout.add_scalar(kind);
            layout.add_scalar(record.id);
            layout.add_scalar(record.ordinal);
            actors.add_scalar(kind);
            actors.add_scalar(record.id);
            actors.add_scalar(record.ordinal);
            actors.add_scalar(record.actor);
            canonical.add_scalar(kind);
            canonical.add_scalar(record.id);
            canonical.add_scalar(record.ordinal);
            canonical.add_scalar(record.scalar);
            canonical.add_scalar(record.endurance);
            integrity.add_scalar(kind);
            integrity.add_scalar(record.id);
            integrity.add_scalar(record.ordinal);
            integrity.add_scalar(record.actor);
            integrity.add_scalar(record.scalar);
            integrity.add_scalar(record.endurance);
            integrity.add_scalar(record.fade_timer);
            integrity.add_scalar(record.fade_rate);
        }
        return {
            layout.value,
            actors.value,
            canonical.value,
            integrity.value,
        };
    }

    static inline void HashRollbackBreakableStageSnapshot(
        RollbackBreakableStageSnapshot& snapshot) noexcept
    {
        const RollbackBreakableStageHashes hashes =
            ComputeRollbackBreakableStageHashes(snapshot);
        snapshot.stage_layout_digest = hashes.stage_layout_digest;
        snapshot.actor_set_digest = hashes.actor_set_digest;
        snapshot.canonical_hash = hashes.canonical_hash;
        snapshot.integrity_hash = hashes.integrity_hash;
    }

    static inline bool RollbackCaptureBreakableKind(
        const RollbackStageArrayHeader& array,
        RollbackBreakableActorKind kind,
        RollbackBreakableStageSnapshot& out,
        RollbackBreakableStageReport& report) noexcept
    {
        std::vector<uintptr_t> actors;
        try
        {
            actors.resize(static_cast<size_t>(array.count));
        }
        catch (const std::bad_alloc&)
        {
            report.failure = "stage-actor-pointer-allocation-failed";
            return false;
        }
        if (!actors.empty()
            && !SafeReadBytes(
                reinterpret_cast<const void*>(array.data),
                actors.data(),
                actors.size() * sizeof(uintptr_t)))
        {
            report.failure = "stage-actor-list-read-failed";
            return false;
        }

        for (size_t ordinal = 0; ordinal < actors.size(); ++ordinal)
        {
            const uintptr_t actor = actors[ordinal];
            if (!actor)
            {
                report.failure = "null-stage-actor";
                return false;
            }
            RollbackBreakableStageRecord record {};
            record.kind = kind;
            record.ordinal = static_cast<uint32_t>(ordinal);
            record.actor = actor;
            if (kind == RollbackBreakableActorKind::Wall)
            {
                uint8_t state = 0;
                if (!SafeReadBytes(
                        reinterpret_cast<const void*>(
                            actor + kRollbackStageWallIdOffset),
                        &record.id,
                        sizeof(record.id))
                    || !SafeReadBytes(
                        reinterpret_cast<const void*>(
                            actor + kRollbackStageWallBreakStateOffset),
                        &state,
                        sizeof(state))
                    || !SafeReadBytes(
                        reinterpret_cast<const void*>(
                            actor + kRollbackStageWallFadeTimerOffset),
                        &record.fade_timer,
                        sizeof(record.fade_timer))
                    || !SafeReadBytes(
                        reinterpret_cast<const void*>(
                            actor + kRollbackStageWallFadeRateOffset),
                        &record.fade_rate,
                        sizeof(record.fade_rate)))
                {
                    report.failed_actor = actor;
                    report.failure = "breakable-wall-read-failed";
                    return false;
                }
                record.scalar = state;
                ++report.wall_count;
            }
            else
            {
                if (!SafeReadBytes(
                        reinterpret_cast<const void*>(
                            actor + kRollbackStageBarrierIdOffset),
                        &record.id,
                        sizeof(record.id))
                    || !SafeReadBytes(
                        reinterpret_cast<const void*>(
                            actor + kRollbackStageBarrierEnduranceOffset),
                        &record.endurance,
                        sizeof(record.endurance))
                    || !SafeReadBytes(
                        reinterpret_cast<const void*>(
                            actor + kRollbackStageBarrierHitCountOffset),
                        &record.scalar,
                        sizeof(record.scalar)))
                {
                    report.failed_actor = actor;
                    report.failure = "breakable-barrier-read-failed";
                    return false;
                }
                ++report.barrier_count;
            }
            out.records.push_back(record);
        }
        return true;
    }

    static inline RollbackBreakableStageReport
    CaptureRollbackBreakableStageSnapshot(
        uintptr_t stage_actor_manager,
        RollbackBreakableStageSnapshot& out) noexcept
    {
        RollbackBreakableStageReport report {};
        report.failure = "ok";
        out.clear();
        if (!stage_actor_manager)
        {
            report.failure = "stage-actor-manager-missing";
            return report;
        }

        RollbackStageArrayHeader walls {};
        RollbackStageArrayHeader barriers {};
        if (!RollbackReadStageArrayHeader(
                stage_actor_manager,
                kRollbackStageWallListOffset,
                walls)
            || !RollbackReadStageArrayHeader(
                stage_actor_manager,
                kRollbackStageBarrierListOffset,
                barriers))
        {
            report.failure = "stage-actor-list-header-invalid";
            return report;
        }

        try
        {
            out.records.reserve(
                static_cast<size_t>(walls.count + barriers.count));
        }
        catch (const std::bad_alloc&)
        {
            report.failure = "stage-record-allocation-failed";
            return report;
        }
        out.stage_actor_manager = stage_actor_manager;
        if (!RollbackCaptureBreakableKind(
                walls,
                RollbackBreakableActorKind::Wall,
                out,
                report)
            || !RollbackCaptureBreakableKind(
                barriers,
                RollbackBreakableActorKind::Barrier,
                out,
                report))
        {
            out.clear();
            return report;
        }

        std::sort(
            out.records.begin(),
            out.records.end(),
            [](const RollbackBreakableStageRecord& a,
               const RollbackBreakableStageRecord& b) noexcept {
                if (a.kind != b.kind)
                    return static_cast<uint8_t>(a.kind)
                        < static_cast<uint8_t>(b.kind);
                if (a.id != b.id) return a.id < b.id;
                return a.ordinal < b.ordinal;
            });

        HashRollbackBreakableStageSnapshot(out);
        report.ok = out.stage_layout_digest != 0
            && out.actor_set_digest != 0
            && out.canonical_hash != 0
            && out.integrity_hash != 0;
        if (!report.ok) report.failure = "stage-snapshot-hash-failed";
        return report;
    }

    static inline bool WriteRollbackBreakableStageSnapshotUnchecked(
        const RollbackBreakableStageSnapshot& snapshot,
        RollbackBreakableStageReport& report) noexcept
    {
        for (const RollbackBreakableStageRecord& record : snapshot.records)
        {
            bool ok = false;
            if (record.kind == RollbackBreakableActorKind::Wall)
            {
                const uint8_t state = static_cast<uint8_t>(record.scalar);
                ok = SafeWriteBytes(
                        reinterpret_cast<void*>(
                            record.actor
                            + kRollbackStageWallBreakStateOffset),
                        &state,
                        sizeof(state))
                    && SafeWriteBytes(
                        reinterpret_cast<void*>(
                            record.actor
                            + kRollbackStageWallFadeTimerOffset),
                        &record.fade_timer,
                        sizeof(record.fade_timer))
                    && SafeWriteBytes(
                        reinterpret_cast<void*>(
                            record.actor
                            + kRollbackStageWallFadeRateOffset),
                        &record.fade_rate,
                        sizeof(record.fade_rate));
            }
            else
            {
                ok = SafeWriteBytes(
                    reinterpret_cast<void*>(
                        record.actor
                        + kRollbackStageBarrierHitCountOffset),
                    &record.scalar,
                    sizeof(record.scalar));
            }
            if (!ok)
            {
                report.failed_actor = record.actor;
                report.failure = "breakable-stage-write-failed";
                return false;
            }
            ++report.restored_count;
        }
        return true;
    }

    static inline RollbackBreakableStageReport
    RestoreRollbackBreakableStageSnapshot(
        uintptr_t live_stage_actor_manager,
        const RollbackBreakableStageSnapshot& snapshot) noexcept
    {
        RollbackBreakableStageReport report {};
        report.failure = "ok";
        if (!live_stage_actor_manager
            || live_stage_actor_manager != snapshot.stage_actor_manager)
        {
            report.failure = "stage-actor-manager-epoch-mismatch";
            return report;
        }

        RollbackBreakableStageSnapshot emergency {};
        RollbackBreakableStageReport emergency_capture =
            CaptureRollbackBreakableStageSnapshot(
                live_stage_actor_manager, emergency);
        if (!emergency_capture.ok)
        {
            report.failure = "breakable-stage-emergency-capture-failed";
            return report;
        }
        report.emergency_captured = true;
        report.wall_count = emergency_capture.wall_count;
        report.barrier_count = emergency_capture.barrier_count;
        if (emergency.stage_layout_digest != snapshot.stage_layout_digest
            || emergency.actor_set_digest != snapshot.actor_set_digest)
        {
            report.failure = "breakable-stage-actor-set-mismatch";
            return report;
        }
        report.preflight_ok = true;

        if (!WriteRollbackBreakableStageSnapshotUnchecked(snapshot, report))
        {
            RollbackBreakableStageReport recovery {};
            recovery.failure = "ok";
            report.emergency_restored =
                WriteRollbackBreakableStageSnapshotUnchecked(
                    emergency, recovery);
            return report;
        }

        RollbackBreakableStageSnapshot verification {};
        const RollbackBreakableStageReport verify_capture =
            CaptureRollbackBreakableStageSnapshot(
                live_stage_actor_manager, verification);
        if (!verify_capture.ok
            || verification.integrity_hash != snapshot.integrity_hash)
        {
            RollbackBreakableStageReport recovery {};
            recovery.failure = "ok";
            report.emergency_restored =
                WriteRollbackBreakableStageSnapshotUnchecked(
                    emergency, recovery);
            report.failure = "breakable-stage-verification-failed";
            return report;
        }

        report.verification_ok = true;
        report.ok = true;
        return report;
    }

    // Resolve a live presentation actor from the stable logical identity used
    // by snapshots. Saved actor addresses never cross a confirmed-frame
    // boundary.
    static inline bool ResolveRollbackBreakableStageActor(
        uintptr_t stage_actor_manager,
        RollbackBreakableActorKind kind,
        int32_t id,
        uintptr_t& actor) noexcept
    {
        actor = 0;
        RollbackStageArrayHeader array {};
        const uintptr_t list_offset =
            kind == RollbackBreakableActorKind::Wall
                ? kRollbackStageWallListOffset
                : kRollbackStageBarrierListOffset;
        const uintptr_t id_offset =
            kind == RollbackBreakableActorKind::Wall
                ? kRollbackStageWallIdOffset
                : kRollbackStageBarrierIdOffset;
        if (!RollbackReadStageArrayHeader(
                stage_actor_manager, list_offset, array))
            return false;
        for (int32_t index = 0; index < array.count; ++index)
        {
            uintptr_t candidate = 0;
            int32_t candidate_id = 0;
            if (!SafeReadBytes(
                    reinterpret_cast<const void*>(
                        array.data
                        + static_cast<uintptr_t>(index)
                            * sizeof(uintptr_t)),
                    &candidate, sizeof(candidate))
                || !candidate
                || !SafeReadBytes(
                    reinterpret_cast<const void*>(candidate + id_offset),
                    &candidate_id, sizeof(candidate_id)))
                return false;
            if (candidate_id != id) continue;
            if (actor != 0) return false;
            actor = candidate;
        }
        return actor != 0;
    }
}
