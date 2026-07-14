// ============================================================================
// Horse::RollbackSnapshot
//
// Manifest types for same-round rollback snapshots. This file owns only
// explicit byte-range copies. Engine-authored snapshots such as HgCpuDirect
// are represented as coverage evidence and executed through
// RollbackHgCpuSnapshot.hpp.
// ============================================================================

#pragma once

#include "RollbackLaunchContract.hpp"
#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace Horse
{
    static inline uint64_t RollbackHashRoundStartCanonical(
        const uint8_t* bytes,
        size_t byte_count) noexcept
    {
        if (!bytes || byte_count < 0x2C) return 0;
        uint32_t move_provider = 0;
        uint32_t position[3] {};
        std::memcpy(&move_provider, bytes, sizeof(move_provider));
        std::memcpy(position, bytes + 0x20, sizeof(position));
        RollbackHash hash {};
        hash.add_scalar(move_provider);
        hash.add_bytes(position, sizeof(position));
        return hash.value ? hash.value : 1;
    }

    // Peer compatibility contract. Bump these whenever native snapshot
    // serializers, canonical hashing, Gekko event semantics, authenticated
    // packet/profile layouts, or production frame-summary semantics change.
    // schema_hash() includes both values, so a Horse runtime ABI mismatch is
    // rejected by the authenticated protocol before activation.
    static constexpr uint32_t kRollbackRuntimeAbiVersion = 10;
    static constexpr uint32_t kRollbackCanonicalSchemaVersion = 7;

    enum class RollbackCoverage : uint8_t
    {
        Unknown,
        PendingEvidence,
        HgCpuDirectCovered,
        ExplicitSnapshot,
        DynamicSnapshot,
        DiagnosticOnly,
        ExcludedWithEvidence,
        AuthenticatedLaunchContract,
    };

    enum class RollbackCanonicalPolicy : uint8_t
    {
        AllBytes,
        LuxMoveSchedStateArray,
    };

    struct RollbackManifestEntry
    {
        const char* name {""};
        uintptr_t address {0};
        uint32_t offset {0};
        uint32_t bytes {0};
        RollbackCoverage coverage {RollbackCoverage::Unknown};
        const char* evidence {""};
        RollbackCanonicalPolicy canonical_policy {
            RollbackCanonicalPolicy::AllBytes};
    };

    static inline bool rollback_manifest_entry_is_explicit_copy(
        const RollbackManifestEntry& e) noexcept
    {
        return e.coverage == RollbackCoverage::ExplicitSnapshot
            && e.address != 0 && e.bytes != 0;
    }

    struct RollbackLifecycleEpoch
    {
        uint64_t generation {0};
        uintptr_t battle_manager {0};
        uintptr_t input_log {0};
        std::array<uintptr_t, 2> chara {};
        uintptr_t stage_actor_manager {0};
        uint64_t round_start_digest {0};
        uint64_t stage_layout_digest {0};
        uint64_t actor_set_digest {0};
        uint32_t input_log_frame {0};
        uint8_t presence {0xFF};
        uint8_t battle_main_state {0xFF};
        uint8_t battle_status {0xFF};
        bool pvp_active {false};
        bool auto_advance_armed {true};
        bool valid {false};

        bool active_battle_common() const noexcept
        {
            return valid
                && generation != 0
                && battle_manager != 0
                && input_log != 0
                && chara[0] != 0
                && chara[1] != 0
                && stage_actor_manager != 0
                && battle_main_state == 2
                && battle_status == 2
                && !auto_advance_armed
                && round_start_digest != 0
                && stage_layout_digest != 0
                && actor_set_digest != 0;
        }

        bool active_for(RollbackLifecycleMode mode) const noexcept
        {
            if (!active_battle_common()) return false;
            if (mode == RollbackLifecycleMode::StockOnlinePvp)
                return pvp_active && (presence == 7 || presence == 8);
            if (mode == RollbackLifecycleMode::MirroredVersus)
                return !pvp_active && presence == 5;
            return false;
        }

        bool active_pvp() const noexcept
        {
            return active_for(RollbackLifecycleMode::StockOnlinePvp);
        }

        bool same_as(const RollbackLifecycleEpoch& other) const noexcept
        {
            return generation == other.generation
                && battle_manager == other.battle_manager
                && input_log == other.input_log
                && chara == other.chara
                && stage_actor_manager == other.stage_actor_manager
                && round_start_digest == other.round_start_digest
                && stage_layout_digest == other.stage_layout_digest
                && actor_set_digest == other.actor_set_digest
                && presence == other.presence
                && battle_main_state == other.battle_main_state
                && battle_status == other.battle_status
                && pvp_active == other.pvp_active
                && auto_advance_armed == other.auto_advance_armed
                && valid == other.valid;
        }
    };

    struct RollbackManifestValidationReport
    {
        bool ok {false};
        bool live_ready {false};
        uint32_t pending_gameplay_entries {0};
        uint32_t invalid_entries {0};
        uint32_t overlapping_entries {0};
        uint32_t first_entry {0xFFFFFFFFu};
        uint32_t second_entry {0xFFFFFFFFu};
        const char* failure {"not-run"};
    };

    struct RollbackSnapshotManifest
    {
        uint32_t version {2};
        uint32_t max_rollback_frames {0};
        uintptr_t image_base {0};
        std::vector<RollbackManifestEntry> entries;
        RollbackLifecycleEpoch epoch {};

        uint64_t schema_hash() const noexcept
        {
            RollbackHash h{};
            h.add_scalar(version);
            h.add_scalar(kRollbackRuntimeAbiVersion);
            h.add_scalar(kRollbackCanonicalSchemaVersion);
            h.add_scalar(max_rollback_frames);
            for (const auto& e : entries)
            {
                if (e.name)
                    h.add_bytes(e.name, std::strlen(e.name));
                h.add_scalar(e.offset);
                h.add_scalar(e.bytes);
                h.add_scalar(static_cast<uint8_t>(e.coverage));
                h.add_scalar(static_cast<uint8_t>(e.canonical_policy));
            }
            return h.value;
        }

        uint64_t coverage_hash() const noexcept
        {
            return schema_hash();
        }
    };

    struct RollbackSnapshotRange
    {
        uint32_t manifest_index {0};
        uintptr_t address {0};
        uint32_t bytes_offset {0};
        uint32_t bytes {0};
        uint64_t hash {0};
        RollbackCanonicalPolicy canonical_policy {
            RollbackCanonicalPolicy::AllBytes};
    };

    struct RollbackSnapshotFrame
    {
        RollbackLifecycleEpoch epoch {};
        std::vector<uint8_t> bytes;
        std::vector<RollbackSnapshotRange> ranges;
        uint64_t schema_hash {0};
        uint64_t canonical_hash {0};
        uint64_t integrity_hash {0};
        uint64_t hash {0};

        void clear()
        {
            epoch = {};
            bytes.clear();
            ranges.clear();
            schema_hash = 0;
            canonical_hash = 0;
            integrity_hash = 0;
            hash = 0;
        }
    };

    struct RollbackSnapshotCopyReport
    {
        bool ok {false};
        uint32_t copied_entries {0};
        uint32_t skipped_entries {0};
        uint32_t copied_bytes {0};
        uint32_t failed_entry {0xFFFFFFFFu};
        uintptr_t failed_address {0};
        const char* failure {"not-run"};
        uint64_t hash {0};
        bool preflight_ok {false};
        bool emergency_captured {false};
        bool emergency_restored {false};
        bool verification_ok {false};
    };

    enum class RollbackEpochValidationStatus : uint8_t
    {
        Accepted,
        SnapshotEpochNotActive,
        LiveEpochNotActive,
        GenerationMismatch,
        CharaEpochMissing,
        BattleManagerMismatch,
        InputLogMismatch,
        CharaMismatch,
        StageActorManagerMismatch,
        RoundStartMismatch,
        StageLayoutMismatch,
        ActorSetMismatch,
        PresenceMismatch,
        BattleStateMismatch,
    };

    struct RollbackEpochValidationReport
    {
        bool ok {false};
        RollbackEpochValidationStatus status {
            RollbackEpochValidationStatus::SnapshotEpochNotActive};
        const char* failure {"not-run"};
    };

    struct RollbackSnapshotRoundTripReport
    {
        bool ok {false};
        bool hash_match {false};
        uint64_t before_hash {0};
        uint64_t after_hash {0};
        RollbackSnapshotCopyReport capture {};
        RollbackSnapshotCopyReport restore {};
        RollbackSnapshotCopyReport recapture {};
    };

    static inline uint64_t HashRollbackSnapshotFrame(
        const RollbackSnapshotFrame& frame) noexcept
    {
        RollbackHash h{};
        h.add_scalar(frame.schema_hash);
        h.add_scalar(frame.epoch.generation);
        h.add_scalar(frame.epoch.battle_manager);
        h.add_scalar(frame.epoch.input_log);
        h.add_scalar(frame.epoch.chara[0]);
        h.add_scalar(frame.epoch.chara[1]);
        h.add_scalar(frame.epoch.stage_actor_manager);
        h.add_scalar(frame.epoch.round_start_digest);
        h.add_scalar(frame.epoch.stage_layout_digest);
        h.add_scalar(frame.epoch.actor_set_digest);
        h.add_scalar(frame.epoch.input_log_frame);
        h.add_scalar(frame.epoch.presence);
        h.add_scalar(frame.epoch.battle_main_state);
        h.add_scalar(frame.epoch.battle_status);
        h.add_scalar(frame.epoch.pvp_active);
        h.add_scalar(frame.epoch.auto_advance_armed);
        h.add_scalar(frame.epoch.valid);
        for (const RollbackSnapshotRange& r : frame.ranges)
        {
            h.add_scalar(r.manifest_index);
            h.add_scalar(r.address);
            h.add_scalar(r.bytes_offset);
            h.add_scalar(r.bytes);
            h.add_scalar(r.hash);
            h.add_scalar(static_cast<uint8_t>(r.canonical_policy));
        }
        h.add_bytes(frame.bytes.data(), frame.bytes.size());
        return h.value;
    }

    static inline uint64_t HashRollbackSnapshotCanonical(
        const RollbackSnapshotFrame& frame) noexcept
    {
        RollbackHash h {};
        h.add_scalar(frame.schema_hash);
        for (const RollbackSnapshotRange& r : frame.ranges)
        {
            h.add_scalar(r.manifest_index);
            h.add_scalar(r.bytes);
            if (r.bytes_offset <= frame.bytes.size()
                && static_cast<size_t>(r.bytes)
                    <= frame.bytes.size() - r.bytes_offset)
            {
                const uint8_t* bytes =
                    frame.bytes.data() + r.bytes_offset;
                if (r.canonical_policy
                        == RollbackCanonicalPolicy::LuxMoveSchedStateArray
                    && r.bytes == 0xC0)
                {
                    static constexpr uint8_t kOffsets[] = {
                        0x08, 0x30, 0x38, 0x40, 0x48, 0x4C, 0x5C,
                    };
                    static constexpr uint8_t kSizes[] = {
                        4, 8, 8, 8, 4, 4, 4,
                    };
                    for (size_t slot = 0; slot < 2; ++slot)
                    {
                        h.add_scalar(slot);
                        for (size_t field = 0;
                             field < sizeof(kOffsets); ++field)
                        {
                            const size_t offset = slot * 0x60
                                + kOffsets[field];
                            h.add_bytes(bytes + offset, kSizes[field]);
                        }
                    }
                }
                else
                {
                    h.add_bytes(bytes, r.bytes);
                }
            }
        }
        return h.value;
    }

    static inline RollbackManifestValidationReport
    ValidateRollbackSnapshotManifest(
        const RollbackSnapshotManifest& manifest,
        bool require_live_coverage) noexcept
    {
        RollbackManifestValidationReport out {};
        out.failure = "ok";
        for (size_t i = 0; i < manifest.entries.size(); ++i)
        {
            const RollbackManifestEntry& e = manifest.entries[i];
            if (e.coverage == RollbackCoverage::Unknown)
            {
                ++out.invalid_entries;
                if (out.first_entry == 0xFFFFFFFFu)
                    out.first_entry = static_cast<uint32_t>(i);
            }
            if (e.coverage == RollbackCoverage::PendingEvidence)
                ++out.pending_gameplay_entries;
            if (e.coverage == RollbackCoverage::ExplicitSnapshot
                && (e.address == 0 || e.bytes == 0
                    || e.address
                        > (std::numeric_limits<uintptr_t>::max)() - e.offset))
            {
                ++out.invalid_entries;
                if (out.first_entry == 0xFFFFFFFFu)
                    out.first_entry = static_cast<uint32_t>(i);
            }
        }

        for (size_t i = 0; i < manifest.entries.size(); ++i)
        {
            const RollbackManifestEntry& a = manifest.entries[i];
            if (!rollback_manifest_entry_is_explicit_copy(a))
                continue;
            const uintptr_t a_begin = a.address + a.offset;
            const uintptr_t a_end = a_begin + a.bytes;
            for (size_t j = i + 1; j < manifest.entries.size(); ++j)
            {
                const RollbackManifestEntry& b = manifest.entries[j];
                if (!rollback_manifest_entry_is_explicit_copy(b))
                    continue;
                const uintptr_t b_begin = b.address + b.offset;
                const uintptr_t b_end = b_begin + b.bytes;
                if (a_begin < b_end && b_begin < a_end)
                {
                    ++out.overlapping_entries;
                    if (out.first_entry == 0xFFFFFFFFu)
                    {
                        out.first_entry = static_cast<uint32_t>(i);
                        out.second_entry = static_cast<uint32_t>(j);
                    }
                }
            }
        }

        if (out.invalid_entries != 0)
            out.failure = "invalid-manifest-entry";
        else if (out.overlapping_entries != 0)
            out.failure = "overlapping-manifest-ranges";
        else if (require_live_coverage && out.pending_gameplay_entries != 0)
            out.failure = "pending-gameplay-coverage";
        else
            out.ok = true;
        out.live_ready = out.ok && out.pending_gameplay_entries == 0;
        return out;
    }

    static inline RollbackEpochValidationReport
    ValidateRollbackLifecycleEpoch(
        const RollbackLifecycleEpoch& snapshot_epoch,
        const RollbackLifecycleEpoch& live_epoch,
        RollbackLifecycleMode mode =
            RollbackLifecycleMode::StockOnlinePvp) noexcept
    {
        RollbackEpochValidationReport out {};
        if (!snapshot_epoch.active_for(mode))
        {
            out.status = RollbackEpochValidationStatus::SnapshotEpochNotActive;
            out.failure = "snapshot-epoch-not-active";
            return out;
        }
        if (!live_epoch.active_for(mode))
        {
            out.status = RollbackEpochValidationStatus::LiveEpochNotActive;
            out.failure = "live-epoch-not-active";
            return out;
        }
        if (snapshot_epoch.generation != live_epoch.generation)
        {
            out.status = RollbackEpochValidationStatus::GenerationMismatch;
            out.failure = "lifecycle-generation-mismatch";
            return out;
        }
        if (snapshot_epoch.chara[0] == 0
            || snapshot_epoch.chara[1] == 0
            || live_epoch.chara[0] == 0
            || live_epoch.chara[1] == 0)
        {
            out.status = RollbackEpochValidationStatus::CharaEpochMissing;
            out.failure = "chara-epoch-missing";
            return out;
        }
        if (snapshot_epoch.battle_manager != live_epoch.battle_manager)
        {
            out.status = RollbackEpochValidationStatus::BattleManagerMismatch;
            out.failure = "battle-manager-epoch-mismatch";
            return out;
        }
        if (snapshot_epoch.input_log != live_epoch.input_log)
        {
            out.status = RollbackEpochValidationStatus::InputLogMismatch;
            out.failure = "input-log-epoch-mismatch";
            return out;
        }
        if (snapshot_epoch.chara != live_epoch.chara)
        {
            out.status = RollbackEpochValidationStatus::CharaMismatch;
            out.failure = "chara-epoch-mismatch";
            return out;
        }
        if (snapshot_epoch.stage_actor_manager
            != live_epoch.stage_actor_manager)
        {
            out.status =
                RollbackEpochValidationStatus::StageActorManagerMismatch;
            out.failure = "stage-actor-manager-epoch-mismatch";
            return out;
        }
        if (snapshot_epoch.round_start_digest
            != live_epoch.round_start_digest)
        {
            out.status = RollbackEpochValidationStatus::RoundStartMismatch;
            out.failure = "round-start-epoch-mismatch";
            return out;
        }
        if (snapshot_epoch.stage_layout_digest
            != live_epoch.stage_layout_digest)
        {
            out.status = RollbackEpochValidationStatus::StageLayoutMismatch;
            out.failure = "stage-layout-epoch-mismatch";
            return out;
        }
        if (snapshot_epoch.actor_set_digest != live_epoch.actor_set_digest)
        {
            out.status = RollbackEpochValidationStatus::ActorSetMismatch;
            out.failure = "actor-set-epoch-mismatch";
            return out;
        }
        if (snapshot_epoch.presence != live_epoch.presence)
        {
            out.status = RollbackEpochValidationStatus::PresenceMismatch;
            out.failure = "presence-epoch-mismatch";
            return out;
        }
        if (snapshot_epoch.battle_main_state != live_epoch.battle_main_state
            || snapshot_epoch.battle_status != live_epoch.battle_status
            || snapshot_epoch.pvp_active != live_epoch.pvp_active
            || snapshot_epoch.auto_advance_armed
                != live_epoch.auto_advance_armed)
        {
            out.status = RollbackEpochValidationStatus::BattleStateMismatch;
            out.failure = "battle-state-epoch-mismatch";
            return out;
        }

        out.ok = true;
        out.status = RollbackEpochValidationStatus::Accepted;
        out.failure = "ok";
        return out;
    }

    static inline RollbackEpochValidationReport
    ValidateRollbackReplayForkLifecycleEpoch(
        const RollbackLifecycleEpoch& snapshot_epoch,
        const RollbackLifecycleEpoch& live_epoch) noexcept
    {
        if (!snapshot_epoch.active_battle_common()
            || snapshot_epoch.presence != 10
            || snapshot_epoch.pvp_active)
        {
            RollbackEpochValidationReport out {};
            out.status = RollbackEpochValidationStatus::SnapshotEpochNotActive;
            out.failure = "replay-fork-snapshot-epoch-not-active";
            return out;
        }
        if (!live_epoch.active_battle_common()
            || live_epoch.presence != 10
            || live_epoch.pvp_active)
        {
            RollbackEpochValidationReport out {};
            out.status = RollbackEpochValidationStatus::LiveEpochNotActive;
            out.failure = "replay-fork-live-epoch-not-active";
            return out;
        }

        // Reuse the production field-by-field epoch validator after replacing
        // only its presence policy. RollbackLifecycleMode stays production-
        // only; replay-fork cannot become a production activation mode.
        RollbackLifecycleEpoch snapshot = snapshot_epoch;
        RollbackLifecycleEpoch live = live_epoch;
        snapshot.presence = 5;
        live.presence = 5;
        snapshot.pvp_active = false;
        live.pvp_active = false;
        return ValidateRollbackLifecycleEpoch(
            snapshot, live, RollbackLifecycleMode::MirroredVersus);
    }

    static inline RollbackSnapshotCopyReport CaptureRollbackSnapshotBytes(
        const RollbackSnapshotManifest& manifest,
        RollbackSnapshotFrame& out)
    {
        RollbackSnapshotCopyReport report{};
        report.failure = "ok";
        out.clear();
        out.epoch = manifest.epoch;
        out.schema_hash = manifest.schema_hash();

        const RollbackManifestValidationReport manifest_validation =
            ValidateRollbackSnapshotManifest(manifest, false);
        if (!manifest_validation.ok)
        {
            report.failure = manifest_validation.failure;
            report.failed_entry = manifest_validation.first_entry;
            return report;
        }

        size_t total_bytes = 0;
        uint32_t explicit_entries = 0;
        for (size_t i = 0; i < manifest.entries.size(); ++i)
        {
            const RollbackManifestEntry& e = manifest.entries[i];
            if (!rollback_manifest_entry_is_explicit_copy(e))
            {
                ++report.skipped_entries;
                continue;
            }
            if (e.address
                > (std::numeric_limits<uintptr_t>::max)() - e.offset)
            {
                report.failure = "address-overflow";
                report.failed_entry = static_cast<uint32_t>(i);
                report.failed_address = e.address;
                return report;
            }
            if (total_bytes
                > (std::numeric_limits<size_t>::max)()
                    - static_cast<size_t>(e.bytes))
            {
                report.failure = "snapshot-too-large";
                report.failed_entry = static_cast<uint32_t>(i);
                report.failed_address = e.address + e.offset;
                return report;
            }
            total_bytes += static_cast<size_t>(e.bytes);
            ++explicit_entries;
        }

        if (explicit_entries == 0)
        {
            report.failure = "no-explicit-ranges";
            return report;
        }

        try
        {
            out.bytes.resize(total_bytes);
            out.ranges.reserve(explicit_entries);
        }
        catch (...)
        {
            out.clear();
            report.failure = "snapshot-allocation-failed";
            return report;
        }

        size_t cursor = 0;
        for (size_t i = 0; i < manifest.entries.size(); ++i)
        {
            const RollbackManifestEntry& e = manifest.entries[i];
            if (!rollback_manifest_entry_is_explicit_copy(e))
                continue;

            const uintptr_t src = e.address + e.offset;
            uint8_t* dst = out.bytes.data() + cursor;
            if (!SafeReadBytes(
                    reinterpret_cast<const void*>(src), dst, e.bytes))
            {
                out.clear();
                report.failure = "read-fault";
                report.failed_entry = static_cast<uint32_t>(i);
                report.failed_address = src;
                return report;
            }

            RollbackSnapshotRange r{};
            r.manifest_index = static_cast<uint32_t>(i);
            r.address = src;
            r.bytes_offset = static_cast<uint32_t>(cursor);
            r.bytes = e.bytes;
            r.hash = RollbackHashBytes(dst, e.bytes);
            r.canonical_policy = e.canonical_policy;
            out.ranges.push_back(r);

            cursor += static_cast<size_t>(e.bytes);
            ++report.copied_entries;
        }

        report.copied_bytes = static_cast<uint32_t>(cursor);
        out.canonical_hash = HashRollbackSnapshotCanonical(out);
        out.integrity_hash = HashRollbackSnapshotFrame(out);
        out.hash = out.integrity_hash;
        report.hash = out.hash;
        report.ok = true;
        return report;
    }

    static inline bool ValidateRollbackSnapshotFrame(
        const RollbackSnapshotFrame& snapshot,
        RollbackSnapshotCopyReport& report) noexcept
    {
        if (snapshot.ranges.empty())
        {
            report.failure = "no-ranges";
            return false;
        }
        if (snapshot.schema_hash == 0 || snapshot.integrity_hash == 0
            || snapshot.hash != snapshot.integrity_hash
            || HashRollbackSnapshotFrame(snapshot) != snapshot.integrity_hash
            || HashRollbackSnapshotCanonical(snapshot)
                != snapshot.canonical_hash)
        {
            report.failure = "snapshot-integrity-mismatch";
            return false;
        }

        for (size_t i = 0; i < snapshot.ranges.size(); ++i)
        {
            const RollbackSnapshotRange& r = snapshot.ranges[i];
            if (r.address == 0 || r.bytes == 0
                || r.bytes_offset > snapshot.bytes.size()
                || static_cast<size_t>(r.bytes)
                    > snapshot.bytes.size() - r.bytes_offset)
            {
                report.failure = "range-out-of-bounds";
                report.failed_entry = r.manifest_index;
                report.failed_address = r.address;
                return false;
            }
            const uint8_t* src = snapshot.bytes.data() + r.bytes_offset;
            if (RollbackHashBytes(src, r.bytes) != r.hash)
            {
                report.failure = "range-hash-mismatch";
                report.failed_entry = r.manifest_index;
                report.failed_address = r.address;
                return false;
            }
            const uintptr_t r_end = r.address + r.bytes;
            if (r_end < r.address)
            {
                report.failure = "range-address-overflow";
                report.failed_entry = r.manifest_index;
                report.failed_address = r.address;
                return false;
            }
            for (size_t j = i + 1; j < snapshot.ranges.size(); ++j)
            {
                const RollbackSnapshotRange& other = snapshot.ranges[j];
                if (other.address >
                    (std::numeric_limits<uintptr_t>::max)() - other.bytes)
                {
                    report.failure = "range-address-overflow";
                    report.failed_entry = other.manifest_index;
                    report.failed_address = other.address;
                    return false;
                }
                const uintptr_t other_end = other.address + other.bytes;
                if (r.address < other_end && other.address < r_end)
                {
                    report.failure = "overlapping-snapshot-ranges";
                    report.failed_entry = other.manifest_index;
                    report.failed_address = other.address;
                    return false;
                }
            }
        }
        report.preflight_ok = true;
        return true;
    }

    static inline bool CaptureRollbackEmergencyFrame(
        const RollbackSnapshotFrame& target,
        RollbackSnapshotFrame& emergency) noexcept
    {
        emergency.clear();
        emergency.epoch = target.epoch;
        emergency.schema_hash = target.schema_hash;
        try
        {
            emergency.bytes.resize(target.bytes.size());
            emergency.ranges = target.ranges;
        }
        catch (...)
        {
            emergency.clear();
            return false;
        }
        for (RollbackSnapshotRange& r : emergency.ranges)
        {
            uint8_t* dst = emergency.bytes.data() + r.bytes_offset;
            if (!SafeReadBytes(
                    reinterpret_cast<const void*>(r.address), dst, r.bytes))
            {
                emergency.clear();
                return false;
            }
            r.hash = RollbackHashBytes(dst, r.bytes);
        }
        emergency.canonical_hash = HashRollbackSnapshotCanonical(emergency);
        emergency.integrity_hash = HashRollbackSnapshotFrame(emergency);
        emergency.hash = emergency.integrity_hash;
        return true;
    }

    static inline bool WriteRollbackSnapshotFrameUnchecked(
        const RollbackSnapshotFrame& snapshot,
        RollbackSnapshotCopyReport& report) noexcept
    {
        for (const RollbackSnapshotRange& r : snapshot.ranges)
        {
            const uint8_t* src = snapshot.bytes.data() + r.bytes_offset;
            if (!SafeWriteBytes(
                    reinterpret_cast<void*>(r.address), src, r.bytes))
            {
                report.failure = "write-fault";
                report.failed_entry = r.manifest_index;
                report.failed_address = r.address;
                return false;
            }
            ++report.copied_entries;
            report.copied_bytes += r.bytes;
        }
        return true;
    }

    static inline RollbackSnapshotCopyReport RestoreRollbackSnapshotBytes(
        const RollbackSnapshotFrame& snapshot)
    {
        RollbackSnapshotCopyReport report{};
        report.failure = "ok";

        if (!ValidateRollbackSnapshotFrame(snapshot, report))
            return report;

        RollbackSnapshotFrame emergency {};
        if (!CaptureRollbackEmergencyFrame(snapshot, emergency))
        {
            report.failure = "emergency-capture-failed";
            return report;
        }
        report.emergency_captured = true;

        if (!WriteRollbackSnapshotFrameUnchecked(snapshot, report))
        {
            RollbackSnapshotCopyReport emergency_report {};
            emergency_report.failure = "ok";
            report.emergency_restored =
                WriteRollbackSnapshotFrameUnchecked(
                    emergency, emergency_report);
            return report;
        }

        RollbackSnapshotFrame verification {};
        if (!CaptureRollbackEmergencyFrame(snapshot, verification)
            || verification.canonical_hash != snapshot.canonical_hash)
        {
            RollbackSnapshotCopyReport emergency_report {};
            emergency_report.failure = "ok";
            report.emergency_restored =
                WriteRollbackSnapshotFrameUnchecked(
                    emergency, emergency_report);
            report.failure = "post-restore-verification-failed";
            return report;
        }

        report.hash = snapshot.hash;
        report.verification_ok = true;
        report.ok = true;
        return report;
    }

    static inline RollbackSnapshotCopyReport
    RestoreRollbackSnapshotBytesIfEpochMatches(
        const RollbackSnapshotFrame& snapshot,
        const RollbackLifecycleEpoch& live_epoch,
        bool replay_fork_lab = false)
    {
        const RollbackEpochValidationReport epoch =
            replay_fork_lab
                ? ValidateRollbackReplayForkLifecycleEpoch(
                    snapshot.epoch, live_epoch)
                : ValidateRollbackLifecycleEpoch(
                    snapshot.epoch, live_epoch);
        if (!epoch.ok)
        {
            RollbackSnapshotCopyReport report {};
            report.failure = epoch.failure;
            return report;
        }
        return RestoreRollbackSnapshotBytes(snapshot);
    }

    static inline uintptr_t rollback_absolute_from_image_base(
        uintptr_t image_base,
        uintptr_t documented_absolute) noexcept
    {
        constexpr uintptr_t kGhidraImageBase = 0x140000000ull;
        if (image_base == 0 || documented_absolute < kGhidraImageBase)
            return documented_absolute;
        return image_base + (documented_absolute - kGhidraImageBase);
    }

    static inline RollbackSnapshotManifest BuildInitialRollbackManifest(
        uintptr_t image_base,
        uint32_t max_rollback_frames)
    {
        RollbackSnapshotManifest m{};
        m.max_rollback_frames = max_rollback_frames;
        m.image_base = image_base;
        auto addr = [image_base](uintptr_t absolute) noexcept {
            return rollback_absolute_from_image_base(image_base, absolute);
        };

        m.entries.push_back({
            "HgCpuDirect sim blob",
            addr(0x1403841E0ull),
            0,
            0x28018,
            RollbackCoverage::HgCpuDirectCovered,
            "Native ExecMoveChangeAndPost/ExecFinalizeAndPost pair; Ghidra FLuxHgCpuBuffer size is 0x28018."
        });
        m.entries.push_back({
            "HgCpu canonical field map",
            0,
            0,
            0,
            RollbackCoverage::HgCpuDirectCovered,
            "Peer canonicalization hashes the Ghidra-verified native HgCpu field stream and logical KHit payloads. Horse-only motion, timer, and skeleton-runtime graph images remain local restore-integrity caches; their raw bytes and process links are excluded, while stable slot/component/node shape is covered and every restore is recaptured and verified."
        });
        m.entries.push_back({
            "KHit allocator and node lifetime",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "KHit nodes are canonicalized and restored by logical player/list/node index. Restore captures the live topology, requires identical logical shape/tag/serialized size, writes only native writer-serialized payload fields, and preserves live vtables, next links, list controls, and allocator ownership."
        });
        m.entries.push_back({
            "Coordinated peer baseline activation",
            0,
            0,
            0,
            RollbackCoverage::AuthenticatedLaunchContract,
            "MirroredVersus authenticates lifecycle mode, opposite Gekko slots, native-input source, seed, rollback settings, and launch descriptor; exchanges SetupApplied and BattleBaseline barriers; and freezes the armed native tick boundary until both peers prove the same descriptor, frame, epoch, and canonical baseline."
        });
        m.entries.push_back({
            "Historical camera argument policy",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "Horse captures the intercepted 24-byte camera argument on each newly simulated frame and replays the exact retained bytes for duplicate or rollback Advances. The 128-frame integrity-checked ring exceeds the 60-frame production rollback window and fails closed on missing history, collisions, or corruption. Camera state remains excluded from the cross-peer gameplay digest."
        });
        m.entries.push_back({
            "Round-start canonical field map",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "Lifecycle identity hashes only Ghidra-observed FLuxBattleRoundStartData gameplay fields: move-provider ID at +0x00 and the three position scalars at +0x20/+0x24/+0x28. Opaque reserved bytes, padding, and possible process links are excluded."
        });
        m.entries.push_back({
            "g_dwLuxBattleLcgRngState",
            addr(0x14485EB28ull),
            0,
            4,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra label uint. LuxMoveVM_ApplyAIPaletteMode consumes this Park-Miller LCG while native HgCpu restore rebuilds AI palette slots; replay-fork rollback must restore it after that reader side effect. Capturing it is harmless for human PVP and prevents fixture-only RNG drift."
        });
        m.entries.push_back({
            "g_adwLuxBattleLfsrState",
            addr(0x14485EB30ull),
            0,
            0x64,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra type byte[100]; RNG xrefs compare index against 0x19 and write dwords through this state array."
        });
        m.entries.push_back({
            "g_dwLuxBattleLfsrIndex",
            addr(0x14485EB94ull),
            0,
            4,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra type uint; RNG xrefs read/write the index beside g_adwLuxBattleLfsrState."
        });
        m.entries.push_back({
            "g_LuxBattle_LatestEngineInput_PerPlayer",
            addr(0x144855700ull),
            0,
            0x10,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra retagged as ulonglong[2]; PerFrameTick writes qwords and LuxBattle_TickCharaInput consumes them."
        });
        m.entries.push_back({
            "g_LuxBattle_PerPlayerInputRing",
            addr(0x14485E750ull),
            0,
            0x3D0,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra type FLuxBattleInputRing[2]; chara input consumer uses 0x3D qword ring entries per player."
        });
        m.entries.push_back({
            "g_LuxBattle_PerPlayerInputRingCursor",
            addr(0x14485EB20ull),
            0,
            0x08,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra type int[2]; exactly 8 bytes. The former 0x10-byte range overlapped g_dwLuxBattleLcgRngState at 0x14485EB28."
        });
        m.entries.push_back({
            "g_stLuxBattleWindCombinedRngState",
            addr(0x14470E2B0ull),
            0,
            0x18,
            RollbackCoverage::ExcludedWithEvidence,
            "Ghidra type FLuxBattleWindCombinedRngState (24 bytes); IwWind_GetRandCombinedRngU32 reads/writes it for stage wind presentation and HgCpuDirect has no xref."
        });
        m.entries.push_back({
            "g_LuxBattle_InputRingBaseOffset_PerPlayer",
            addr(0x14470DED0ull),
            0,
            0x10,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra type int[2]; chara input consumer adds this base to the current cursor before ring lookup."
        });
        m.entries.push_back({
            "g_LuxBattle_CCpuCommandArray",
            addr(0x144715400ull),
            0,
            0xC0,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra FLuxMoveSchedState[2], stride 0x60. Restore preserves the complete same-epoch native objects; peer canonicalization includes only selected/active slot indices, move IDs, previous IDs, change counters, and extra parameters, excluding pChara, pSubVM, and padding.",
            RollbackCanonicalPolicy::LuxMoveSchedStateArray
        });
        m.entries.push_back({
            "Breakable wall/barrier scalar state",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "Ghidra ALuxBattleStageActorManagerRollback_Partial +0x3A8/+0x3B8 lists; wall break state +0x468 and barrier endurance/hit count +0x424/+0x468 are canonicalized in stable type/ID order. Wall fade timer/rate +0x46C/+0x470 are restored for local presentation integrity but excluded from peer gameplay hashes."
        });
        m.entries.push_back({
            "Stage wind emitter mutable state",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "RollbackStageWindSnapshot walks the native sentinel list at g_LuxBattle_StageWindEmitterList in list order, captures the proven +0x50 active, +0x54 remaining, +0x58 base timer, +0x5C reload timer, and +0xA4 jitter scalars, and restores only when the same-lifecycle node/emitter ownership is unchanged. Peer canonicalization excludes addresses; the RNG globals are explicit snapshot entries."
        });
        m.entries.push_back({
            "Canonical stage identity",
            0,
            0,
            0,
            RollbackCoverage::AuthenticatedLaunchContract,
            "The native registry-resolved packed stage ID is read back from the launcher, covered by the authenticated descriptor hash, and carried explicitly in SetupApplied and BattleBaseline. Peers fail closed when this identity differs, independent of breakable layout."
        });
        m.entries.push_back({
            "Breakable wall/barrier presentation reconciliation",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "Each simulated frame queues value-only wall/barrier presentation commands keyed by stable kind/ID. Predicted commands are discarded on Load; confirmed commits resolve the live actor by kind/ID and reproduce Ghidra-verified mesh visibility from wall state/fade (+0x468/+0x46C/+0x470) or barrier hit-count/endurance (+0x468/+0x424). Saved actor/component pointers never cross the ledger boundary."
        });
        m.entries.push_back({
            "Presentation object lifetime and thread affinity",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "Deferred audio records store only the logical player slot and value arguments; VFX records store the complete 16-byte value request. Current character and dispatcher objects are resolved only at confirmed commit. Capture and commit fail closed unless they run on the tick-owning simulation thread, and the ledger is bounded."
        });
        m.entries.push_back({
            "ALuxBattleFrameInputLog cache/cursors",
            0,
            0x394,
            0x4084,
            RollbackCoverage::DiagnosticOnly,
            "Stock-path diagnostic schema only. Production Horse UDP/Gekko never restores or injects the native InputLog cache."
        });

        return m;
    }
}
