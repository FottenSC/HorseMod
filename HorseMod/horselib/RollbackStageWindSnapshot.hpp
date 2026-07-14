// ============================================================================
// Horse::RollbackStageWindSnapshot
//
// Logical same-round snapshot for the stage-wind emitter scheduler. The native
// list/node addresses are retained only to prove restore ownership. Peer hashes
// contain list order and the scalar fields used by SpawnStageWindParticles.
// ============================================================================

#pragma once

#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    static constexpr uintptr_t kRollbackStageWindEmitterListRva = 0x470F1C0;
    static constexpr uintptr_t kRollbackStageWindEmitterCountRva = 0x470F1C8;
    static constexpr size_t kRollbackStageWindEmitterMaxCount = 32;

    struct RollbackStageWindEmitterRecord
    {
        uintptr_t list_node {0};
        uintptr_t emitter {0};
        int32_t active {0};
        int32_t remaining {0};
        float base_timer {0.0f};
        float reload_timer {0.0f};
        float jitter {0.0f};
    };

    struct RollbackStageWindSnapshot
    {
        uintptr_t sentinel {0};
        uint32_t count {0};
        std::array<
            RollbackStageWindEmitterRecord,
            kRollbackStageWindEmitterMaxCount> emitters {};
        uint64_t canonical_hash {0};
        uint64_t integrity_hash {0};
    };

    struct RollbackStageWindSnapshotReport
    {
        bool ok {false};
        uint32_t count {0};
        const char* failure {"not-run"};
    };

    static inline void RollbackHashStageWindScalar(
        RollbackHash& hash,
        const RollbackStageWindEmitterRecord& record) noexcept
    {
        hash.add_scalar(record.active);
        hash.add_scalar(record.remaining);
        hash.add_bytes(&record.base_timer, sizeof(record.base_timer));
        hash.add_bytes(&record.reload_timer, sizeof(record.reload_timer));
        hash.add_bytes(&record.jitter, sizeof(record.jitter));
    }

    static inline uint64_t HashRollbackStageWindCanonical(
        const RollbackStageWindSnapshot& snapshot) noexcept
    {
        if (snapshot.count > snapshot.emitters.size()) return 0;
        RollbackHash hash {};
        hash.add_scalar(snapshot.count);
        for (uint32_t index = 0; index < snapshot.count; ++index)
        {
            hash.add_scalar(index);
            RollbackHashStageWindScalar(hash, snapshot.emitters[index]);
        }
        return hash.value;
    }

    static inline uint64_t HashRollbackStageWindIntegrity(
        const RollbackStageWindSnapshot& snapshot) noexcept
    {
        if (snapshot.count > snapshot.emitters.size()) return 0;
        RollbackHash hash {};
        hash.add_scalar(snapshot.sentinel);
        hash.add_scalar(snapshot.count);
        for (uint32_t index = 0; index < snapshot.count; ++index)
        {
            const RollbackStageWindEmitterRecord& record =
                snapshot.emitters[index];
            hash.add_scalar(record.list_node);
            hash.add_scalar(record.emitter);
            RollbackHashStageWindScalar(hash, record);
        }
        return hash.value;
    }

    static inline bool ReadRollbackStageWindPointer(
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

    static inline bool ReadRollbackStageWindRecord(
        uintptr_t node,
        RollbackStageWindEmitterRecord& record) noexcept
    {
        record = {};
        record.list_node = node;
        if (!ReadRollbackStageWindPointer(node + 0x10, record.emitter)
            || record.emitter == 0)
            return false;
        return SafeReadBytes(
                   reinterpret_cast<const void*>(record.emitter + 0x50),
                   &record.active,
                   sizeof(record.active))
            && SafeReadBytes(
                   reinterpret_cast<const void*>(record.emitter + 0x54),
                   &record.remaining,
                   sizeof(record.remaining))
            && SafeReadBytes(
                   reinterpret_cast<const void*>(record.emitter + 0x58),
                   &record.base_timer,
                   sizeof(record.base_timer))
            && SafeReadBytes(
                   reinterpret_cast<const void*>(record.emitter + 0x5C),
                   &record.reload_timer,
                   sizeof(record.reload_timer))
            && SafeReadBytes(
                   reinterpret_cast<const void*>(record.emitter + 0xA4),
                   &record.jitter,
                   sizeof(record.jitter));
    }

    static inline RollbackStageWindSnapshotReport
    CaptureRollbackStageWindSnapshot(
        uintptr_t image_base,
        RollbackStageWindSnapshot& snapshot) noexcept
    {
        RollbackStageWindSnapshotReport report {};
        snapshot = {};
        if (!image_base)
        {
            report.failure = "stage-wind-image-base-missing";
            return report;
        }
        uint64_t native_count = 0;
        if (!ReadRollbackStageWindPointer(
                image_base + kRollbackStageWindEmitterListRva,
                snapshot.sentinel)
            || snapshot.sentinel == 0
            || !SafeReadBytes(
                reinterpret_cast<const void*>(
                    image_base + kRollbackStageWindEmitterCountRva),
                &native_count,
                sizeof(native_count))
            || native_count > kRollbackStageWindEmitterMaxCount)
        {
            report.failure = "stage-wind-list-header-invalid";
            return report;
        }

        uintptr_t node = 0;
        if (!ReadRollbackStageWindPointer(snapshot.sentinel, node))
        {
            report.failure = "stage-wind-list-head-unreadable";
            return report;
        }
        while (node != snapshot.sentinel)
        {
            if (node == 0 || snapshot.count >= snapshot.emitters.size())
            {
                report.failure = "stage-wind-list-truncated";
                return report;
            }
            RollbackStageWindEmitterRecord& record =
                snapshot.emitters[snapshot.count];
            if (!ReadRollbackStageWindRecord(node, record))
            {
                report.failure = "stage-wind-emitter-unreadable";
                return report;
            }
            ++snapshot.count;
            uintptr_t next = 0;
            if (!ReadRollbackStageWindPointer(node, next) || next == node)
            {
                report.failure = "stage-wind-list-link-invalid";
                return report;
            }
            node = next;
        }
        if (snapshot.count != native_count)
        {
            report.failure = "stage-wind-native-count-mismatch";
            return report;
        }
        snapshot.canonical_hash = HashRollbackStageWindCanonical(snapshot);
        snapshot.integrity_hash = HashRollbackStageWindIntegrity(snapshot);
        if (snapshot.canonical_hash == 0 || snapshot.integrity_hash == 0)
        {
            report.failure = "stage-wind-hash-failed";
            return report;
        }
        report.ok = true;
        report.count = snapshot.count;
        report.failure = "ok";
        return report;
    }

    static inline RollbackStageWindSnapshotReport
    RestoreRollbackStageWindSnapshot(
        uintptr_t image_base,
        const RollbackStageWindSnapshot& snapshot) noexcept
    {
        RollbackStageWindSnapshotReport report {};
        if (HashRollbackStageWindCanonical(snapshot)
                != snapshot.canonical_hash
            || HashRollbackStageWindIntegrity(snapshot)
                != snapshot.integrity_hash)
        {
            report.failure = "stage-wind-snapshot-integrity-failed";
            return report;
        }

        RollbackStageWindSnapshot live {};
        const RollbackStageWindSnapshotReport captured =
            CaptureRollbackStageWindSnapshot(image_base, live);
        if (!captured.ok || live.count != snapshot.count
            || live.sentinel != snapshot.sentinel)
        {
            report.failure = captured.ok
                ? "stage-wind-list-ownership-changed" : captured.failure;
            return report;
        }
        for (uint32_t index = 0; index < snapshot.count; ++index)
        {
            const RollbackStageWindEmitterRecord& expected =
                snapshot.emitters[index];
            const RollbackStageWindEmitterRecord& current =
                live.emitters[index];
            if (current.list_node != expected.list_node
                || current.emitter != expected.emitter)
            {
                report.failure = "stage-wind-emitter-ownership-changed";
                return report;
            }
            const bool wrote = SafeWriteBytes(
                    reinterpret_cast<void*>(expected.emitter + 0x50),
                    &expected.active,
                    sizeof(expected.active))
                && SafeWriteBytes(
                    reinterpret_cast<void*>(expected.emitter + 0x54),
                    &expected.remaining,
                    sizeof(expected.remaining))
                && SafeWriteBytes(
                    reinterpret_cast<void*>(expected.emitter + 0x58),
                    &expected.base_timer,
                    sizeof(expected.base_timer))
                && SafeWriteBytes(
                    reinterpret_cast<void*>(expected.emitter + 0x5C),
                    &expected.reload_timer,
                    sizeof(expected.reload_timer))
                && SafeWriteBytes(
                    reinterpret_cast<void*>(expected.emitter + 0xA4),
                    &expected.jitter,
                    sizeof(expected.jitter));
            if (!wrote)
            {
                report.failure = "stage-wind-emitter-write-failed";
                return report;
            }
        }

        RollbackStageWindSnapshot verified {};
        const RollbackStageWindSnapshotReport verification =
            CaptureRollbackStageWindSnapshot(image_base, verified);
        if (!verification.ok
            || verified.integrity_hash != snapshot.integrity_hash
            || verified.canonical_hash != snapshot.canonical_hash)
        {
            report.failure = verification.ok
                ? "stage-wind-post-restore-mismatch" : verification.failure;
            return report;
        }
        report.ok = true;
        report.count = snapshot.count;
        report.failure = "ok";
        return report;
    }
}
