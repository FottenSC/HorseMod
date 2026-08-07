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
#include <array>
#include <vector>

namespace Horse
{
    static constexpr uintptr_t kRollbackStageWallListOffset = 0x3A8;
    static constexpr uintptr_t kRollbackStageBarrierListOffset = 0x3B8;
    static constexpr uintptr_t kRollbackStageWallIdOffset = 0x450;
    static constexpr uintptr_t kRollbackStageWallBreakStateOffset = 0x468;
    static constexpr uintptr_t kRollbackStageWallFadeTimerOffset = 0x46C;
    static constexpr uintptr_t kRollbackStageWallFadeRateOffset = 0x470;
    static constexpr uintptr_t kRollbackStageWallRootComponentOffset = 0x168;
    static constexpr uintptr_t kRollbackStageWallParticleSystemOffset = 0x458;
    static constexpr uintptr_t
        kRollbackStageWallParticleComponentOffset = 0x460;
    static constexpr uintptr_t
        kRollbackSceneComponentWorldRotationOffset = 0x270;
    static constexpr uintptr_t
        kRollbackSceneComponentWorldTranslationOffset = 0x280;
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
    // Lifecycle-bound presentation fields used only by the confirmed
    // barrier-hit terminal transaction. They are deliberately excluded from
    // snapshot/canonical state.
    static constexpr uintptr_t kRollbackStageBarrierHitEffectOffset = 0x470;
    static constexpr uintptr_t kRollbackStageBarrierBreakEffectOffset = 0x478;
    static constexpr uintptr_t
        kRollbackStageBarrierMaterialRuntimeDirtyOffset = 0x4E0;
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

    struct RollbackBarrierPresentationVisibility
    {
        bool valid {false};
        bool face_visible {false};
        bool back_visible {false};
        bool breaking_visible {false};
    };

    static inline RollbackBarrierPresentationVisibility
    ComputeRollbackBarrierPresentationVisibility(
        int32_t hit_count, int32_t endurance) noexcept
    {
        RollbackBarrierPresentationVisibility result {};
        if (endurance <= 0 || hit_count < 0) return result;
        const bool intact = hit_count < endurance;
        result.valid = true;
        result.face_visible = intact;
        result.back_visible = intact;
        result.breaking_visible = !intact;
        return result;
    }

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
        // Local lifecycle identity only. Native UObject/vtable addresses are
        // never part of the peer-canonical projection.
        uintptr_t vtable {0};
        uintptr_t root_component {0};
        uintptr_t root_component_vtable {0};
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
            reset_metadata();
        }

        void recycle_for_capture()
        {
            stage_actor_manager = 0;
            reset_metadata();
        }

    private:
        void reset_metadata()
        {
            stage_layout_digest = 0;
            actor_set_digest = 0;
            canonical_hash = 0;
            integrity_hash = 0;
        }

    public:
    };

    struct RollbackBreakablePresentationValue
    {
        int32_t scalar {0};
        int32_t endurance {0};
        float fade_timer {0.0f};
        float fade_rate {0.0f};
    };

    template <size_t MaximumActors =
                  static_cast<size_t>(kRollbackStageMaxActorsPerKind) * 2u>
    struct RollbackBreakablePresentationFrame
    {
        uint64_t epoch {0};
        uint32_t frame {0};
        uint32_t count {0};
        uint64_t stage_layout_digest {0};
        uint64_t actor_set_digest {0};
        uint64_t canonical_hash {0};
        bool valid {false};
        std::array<RollbackBreakablePresentationValue, MaximumActors> values {};
    };

    // Confirmed presentation can trail simulation farther than the rollback
    // load window. Keep only the small value fields needed for presentation,
    // keyed by logical frame, instead of retaining full native snapshots.
    template <size_t FrameCapacity = 128,
              size_t MaximumActors =
                  static_cast<size_t>(kRollbackStageMaxActorsPerKind) * 2u>
    class RollbackBreakablePresentationHistory
    {
        static_assert(FrameCapacity >= 2
            && (FrameCapacity & (FrameCapacity - 1u)) == 0,
            "presentation history capacity must be a power of two");

    public:
        using Frame = RollbackBreakablePresentationFrame<MaximumActors>;

        bool record(
            uint64_t epoch,
            uint32_t frame,
            const RollbackBreakableStageSnapshot& snapshot) noexcept
        {
            if (epoch == 0 || snapshot.records.size() > MaximumActors
                || snapshot.stage_layout_digest == 0
                || snapshot.actor_set_digest == 0
                || snapshot.canonical_hash == 0)
            {
                return false;
            }
            Frame& out = m_frames[frame & (FrameCapacity - 1u)];
            out.epoch = epoch;
            out.frame = frame;
            out.count = static_cast<uint32_t>(snapshot.records.size());
            out.stage_layout_digest = snapshot.stage_layout_digest;
            out.actor_set_digest = snapshot.actor_set_digest;
            out.canonical_hash = snapshot.canonical_hash;
            for (size_t i = 0; i < snapshot.records.size(); ++i)
            {
                const RollbackBreakableStageRecord& record =
                    snapshot.records[i];
                out.values[i] = {
                    record.scalar,
                    record.endurance,
                    record.fade_timer,
                    record.fade_rate,
                };
            }
            out.valid = true;
            return true;
        }

        const Frame* find(uint64_t epoch, uint32_t frame) const noexcept
        {
            const Frame& found = m_frames[frame & (FrameCapacity - 1u)];
            return found.valid && found.epoch == epoch
                    && found.frame == frame
                ? &found : nullptr;
        }

        void clear() noexcept
        {
            for (Frame& frame : m_frames) frame.valid = false;
        }

    private:
        std::array<Frame, FrameCapacity> m_frames {};
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

    static inline uint64_t HashRollbackBreakablePresentationDigest(
        const RollbackBreakableStageSnapshot& snapshot) noexcept
    {
        if (snapshot.records.empty()) return 0;
        RollbackHash hash {};
        for (const auto& record : snapshot.records)
        {
            hash.add_scalar(static_cast<uint8_t>(record.kind));
            hash.add_scalar(record.id);
            hash.add_scalar(record.ordinal);
            hash.add_scalar(record.scalar);
            hash.add_scalar(record.endurance);
            hash.add_scalar(record.fade_timer);
            hash.add_scalar(record.fade_rate);
        }
        return hash.value ? hash.value : 1;
    }

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
            actors.add_scalar(record.vtable);
            actors.add_scalar(record.root_component);
            actors.add_scalar(record.root_component_vtable);
            canonical.add_scalar(kind);
            canonical.add_scalar(record.id);
            canonical.add_scalar(record.ordinal);
            canonical.add_scalar(record.scalar);
            canonical.add_scalar(record.endurance);
            integrity.add_scalar(kind);
            integrity.add_scalar(record.id);
            integrity.add_scalar(record.ordinal);
            integrity.add_scalar(record.actor);
            integrity.add_scalar(record.vtable);
            integrity.add_scalar(record.root_component);
            integrity.add_scalar(record.root_component_vtable);
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
            if (!SafeReadBytes(
                    reinterpret_cast<const void*>(actor),
                    &record.vtable, sizeof(record.vtable))
                || !record.vtable)
            {
                report.failed_actor = actor;
                report.failure = "stage-actor-vtable-read-failed";
                return false;
            }
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
                        sizeof(record.fade_rate))
                    || !SafeReadBytes(reinterpret_cast<const void*>(actor
                            + kRollbackStageWallRootComponentOffset),
                        &record.root_component,
                        sizeof(record.root_component))
                    || (record.root_component
                        && (!SafeReadBytes(reinterpret_cast<const void*>(
                                record.root_component),
                            &record.root_component_vtable,
                            sizeof(record.root_component_vtable))
                            || !record.root_component_vtable)))
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

    // Active rollback already validated the immutable actor topology at the
    // round boundary. Refresh only the mutable gameplay scalars; rediscovering
    // and sorting the stage arrays every frame is both redundant and allocates.
    static inline RollbackBreakableStageReport
    CaptureRollbackBreakableStageScalars(
        const RollbackBreakableStageSnapshot& identity,
        RollbackBreakableStageSnapshot& out) noexcept
    {
        RollbackBreakableStageReport report {};
        report.failure = "ok";
        if (!identity.stage_actor_manager
            || identity.stage_layout_digest == 0
            || identity.actor_set_digest == 0)
        {
            report.failure = "stage-identity-invalid";
            return report;
        }
        out.recycle_for_capture();
        if (identity.records.size() > out.records.capacity())
        {
            report.failure = "stage-record-preallocated-capacity-exceeded";
            return report;
        }
        try
        {
            out.records.resize(identity.records.size());
        }
        catch (const std::bad_alloc&)
        {
            report.failure = "stage-record-capacity-growth";
            return report;
        }
        out.stage_actor_manager = identity.stage_actor_manager;
        for (size_t i = 0; i < identity.records.size(); ++i)
        {
            const RollbackBreakableStageRecord& expected =
                identity.records[i];
            RollbackBreakableStageRecord& record = out.records[i];
            record = expected;
            if (record.kind == RollbackBreakableActorKind::Wall)
            {
                uint8_t state = 0;
                uintptr_t live_root_component = 0;
                uintptr_t live_root_vtable = 0;
                if (!SafeReadBytes(reinterpret_cast<const void*>(
                        record.actor + kRollbackStageWallBreakStateOffset),
                        &state, sizeof(state))
                    || !SafeReadBytes(reinterpret_cast<const void*>(
                        record.actor + kRollbackStageWallFadeTimerOffset),
                        &record.fade_timer, sizeof(record.fade_timer))
                    || !SafeReadBytes(reinterpret_cast<const void*>(
                        record.actor + kRollbackStageWallFadeRateOffset),
                        &record.fade_rate, sizeof(record.fade_rate))
                    || !SafeReadBytes(reinterpret_cast<const void*>(
                            record.actor
                            + kRollbackStageWallRootComponentOffset),
                        &live_root_component,
                        sizeof(live_root_component))
                    || (live_root_component
                        && (!SafeReadBytes(reinterpret_cast<const void*>(
                                live_root_component), &live_root_vtable,
                            sizeof(live_root_vtable))
                            || !live_root_vtable))
                    || live_root_component != record.root_component
                    || live_root_vtable != record.root_component_vtable)
                {
                    report.failed_actor = record.actor;
                    report.failure = "breakable-wall-read-failed";
                    return report;
                }
                record.scalar = state;
                ++report.wall_count;
            }
            else
            {
                if (!SafeReadBytes(reinterpret_cast<const void*>(
                        record.actor + kRollbackStageBarrierEnduranceOffset),
                        &record.endurance, sizeof(record.endurance))
                    || !SafeReadBytes(reinterpret_cast<const void*>(
                        record.actor + kRollbackStageBarrierHitCountOffset),
                        &record.scalar, sizeof(record.scalar)))
                {
                    report.failed_actor = record.actor;
                    report.failure = "breakable-barrier-read-failed";
                    return report;
                }
                ++report.barrier_count;
            }
        }
        HashRollbackBreakableStageSnapshot(out);
        report.ok = out.stage_layout_digest == identity.stage_layout_digest
            && out.actor_set_digest == identity.actor_set_digest
            && out.canonical_hash != 0 && out.integrity_hash != 0;
        if (!report.ok) report.failure = "stage-identity-drift";
        return report;
    }

    static inline bool RollbackBreakableStagePreallocatedCaptureReady(
        const RollbackBreakableStageSnapshot& identity,
        const RollbackBreakableStageSnapshot& out) noexcept
    {
        return identity.stage_actor_manager != 0
            && identity.stage_layout_digest != 0
            && identity.actor_set_digest != 0
            && identity.records.size() <= out.records.capacity();
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
