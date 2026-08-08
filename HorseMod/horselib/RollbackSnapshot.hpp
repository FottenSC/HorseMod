// ============================================================================
// Horse::RollbackSnapshot
//
// Manifest types for same-round rollback snapshots. This file owns only
// explicit byte-range copies. Engine-authored snapshots such as HgCpuDirect
// are represented as coverage evidence and executed through
// RollbackHgCpuSnapshot.hpp.
// ============================================================================

#pragma once

#include "RollbackFloatingPointEnvironment.hpp"

#include "RollbackLaunchContract.hpp"
#include "RollbackSnapshotCanonicalPolicy.hpp"
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
    static constexpr uint32_t kRollbackRuntimeAbiVersion = 49;
    static constexpr uint32_t kRollbackCanonicalSchemaVersion = 35;

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

    enum class RollbackCoverageCapabilityId : uint8_t
    {
        None,
        HgCpuState,
        KHitState,
        SessionBaseline,
        HistoricalCamera,
        RoundIdentity,
        NativeRoundStateQueue,
        NativeSimulationState,
        PaletteVariantState,
        ExplicitState,
        ExcludedState,
        BreakableScalars,
        StageWind,
        StageIdentity,
        PresentationDispatch,
        BreakablePresentation,
        DiagnosticInputLog,
        LuxMoveCommandState,
        LuxMovePumpState,
        LuxMoveSlotParamState,
        LuxSubVmState,
        GameplayCameraState,
        FloatingPointPolicy,
        Count,
    };

    enum RollbackCoverageStage : uint32_t
    {
        RollbackCoverageStageCapture = 1u << 0,
        RollbackCoverageStageRestore = 1u << 1,
        RollbackCoverageStageExclusion = 1u << 2,
        RollbackCoverageStageIntercept = 1u << 3,
        RollbackCoverageStageCommit = 1u << 4,
        RollbackCoverageStageVerify = 1u << 5,
        RollbackCoverageStageTest = 1u << 6,
    };

    struct RollbackCoverageCapability
    {
        RollbackCoverageCapabilityId id {RollbackCoverageCapabilityId::None};
        uint32_t required_stages {0};
        uint32_t implemented_stages {0};
        const char* test_name {""};
    };

    static constexpr std::array<RollbackCoverageCapability, 22>
        kRollbackCoverageRegistry {{
            {RollbackCoverageCapabilityId::HgCpuState, 0x63u, 0x63u,
                "rollback_hgcpu_snapshot_selftest"},
            {RollbackCoverageCapabilityId::KHitState, 0x63u, 0x63u,
                "rollback_khit_snapshot_selftest"},
            {RollbackCoverageCapabilityId::SessionBaseline, 0x61u, 0x61u,
                "rollback_launch_contract_selftest"},
            {RollbackCoverageCapabilityId::HistoricalCamera, 0x78u, 0x78u,
                "rollback_camera_history_selftest"},
            {RollbackCoverageCapabilityId::RoundIdentity, 0x61u, 0x61u,
                "rollback_round_coordinator_selftest"},
            {RollbackCoverageCapabilityId::NativeRoundStateQueue,
                0x63u, 0x63u,
                "rollback_native_terminal_gate_selftest"},
            {RollbackCoverageCapabilityId::NativeSimulationState,
                0x63u, 0x63u,
                "rollback_native_simulation_iteration_selftest"},
            {RollbackCoverageCapabilityId::PaletteVariantState,
                0x63u, 0x63u,
                "rollback_snapshot_selftest"},
            {RollbackCoverageCapabilityId::ExplicitState, 0x63u, 0x63u,
                "rollback_snapshot_selftest"},
            {RollbackCoverageCapabilityId::ExcludedState, 0x64u, 0x64u,
                "rollback_snapshot_selftest"},
            {RollbackCoverageCapabilityId::BreakableScalars, 0x63u, 0x63u,
                "rollback_stage_snapshot_selftest"},
            {RollbackCoverageCapabilityId::StageWind, 0x63u, 0x63u,
                "rollback_stage_wind_snapshot_selftest"},
            {RollbackCoverageCapabilityId::StageIdentity, 0x61u, 0x61u,
                "rollback_stage_identity_selftest"},
            {RollbackCoverageCapabilityId::PresentationDispatch, 0x78u, 0x78u,
                "rollback_chara_presentation_selftest"},
            {RollbackCoverageCapabilityId::BreakablePresentation, 0x79u, 0x79u,
                "rollback_snapshot_selftest"},
            {RollbackCoverageCapabilityId::DiagnosticInputLog, 0x64u, 0x64u,
                "rollback_input_log_diagnostic_selftest"},
            {RollbackCoverageCapabilityId::LuxMoveCommandState,
                0x63u, 0x63u,
                "rollback_lux_move_state_selftest"},
            {RollbackCoverageCapabilityId::LuxMovePumpState, 0x63u, 0x63u,
                "rollback_lux_move_state_selftest"},
            {RollbackCoverageCapabilityId::LuxMoveSlotParamState,
                0x63u, 0x63u,
                "rollback_lux_move_state_selftest"},
            {RollbackCoverageCapabilityId::LuxSubVmState, 0x63u, 0x63u,
                "rollback_lux_move_state_selftest"},
            {RollbackCoverageCapabilityId::GameplayCameraState,
                0x67u, 0x67u,
                "rollback_battle_camera_snapshot_selftest; exact native +0x100 serializer projections"},
            {RollbackCoverageCapabilityId::FloatingPointPolicy,
                0x60u, 0x60u,
                "rollback_floating_point_environment_selftest"},
        }};

    static inline const RollbackCoverageCapability*
    FindRollbackCoverageCapability(RollbackCoverageCapabilityId id) noexcept
    {
        for (const auto& capability : kRollbackCoverageRegistry)
        {
            if (capability.id == id)
                return &capability;
        }
        return nullptr;
    }

    static constexpr bool RollbackCoverageCapabilityComplete(
        const RollbackCoverageCapability& capability) noexcept
    {
        return capability.id != RollbackCoverageCapabilityId::None
            && capability.test_name
            && capability.test_name[0] != '\0'
            && (capability.implemented_stages
                & capability.required_stages)
                == capability.required_stages;
    }

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
        RollbackCoverageCapabilityId capability {
            RollbackCoverageCapabilityId::None};
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
        // Allocation-free live validation hashes the accepted TArray storage
        // and actor pointer order without re-reading dynamic stage scalars.
        uint64_t stage_actor_order_digest {0};
        // 0 means unavailable; captured values are 0x10000 | packed stage id.
        uint32_t native_stage_identity {0};
        uint32_t input_log_frame {0};
        uint32_t round_ordinal {0};
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
                && (battle_status == 1 || battle_status == 2)
                && !auto_advance_armed
                && round_start_digest != 0
                && stage_layout_digest != 0
                && actor_set_digest != 0;
        }

        bool active_for(RollbackLifecycleMode mode) const noexcept
        {
            if (!active_battle_common()) return false;
            return mode == RollbackLifecycleMode::StockOnlinePvp
                && pvp_active && (presence == 7 || presence == 8);
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
                && stage_actor_order_digest
                    == other.stage_actor_order_digest
                && native_stage_identity == other.native_stage_identity
                && round_ordinal == other.round_ordinal
                && presence == other.presence
                && battle_main_state == other.battle_main_state
                && battle_status == other.battle_status
                && pvp_active == other.pvp_active
                && auto_advance_armed == other.auto_advance_armed
                && valid == other.valid;
        }
    };

    // Ghidra: LuxBattleManager_Tick_ProcessRoundStateSequence commits the
    // native queue values 1, 2, 3, 5, and 9 to BattleManager +0x1480.  The
    // accepted round starts in state 1 or 2, but the same immutable match identity
    // must remain owned while the stock round-intro/end sequence visits the
    // other verified values.  Anything else remains fail-closed.
    static constexpr bool RollbackRoundSequenceStateOwned(
        uint8_t accepted_state, uint8_t live_state) noexcept
    {
        if (accepted_state != 1 && accepted_state != 2)
            return live_state == accepted_state;
        return live_state == 1 || live_state == 2 || live_state == 3
            || live_state == 5 || live_state == 9;
    }

    // After the result-state handoff, NewRound's due call is allowed once so
    // the native finalize edge can be deferred. The following entry publishes
    // status 1 for the immediately next round without entering ActiveBattle.
    // All unrelated lifecycle changes remain fail-closed.
    static constexpr bool RollbackStockNextRoundBoundaryEligible(
        uint32_t expected_round_ordinal,
        uint64_t expected_round_start_digest,
        uint8_t expected_battle_status,
        uint32_t live_round_ordinal,
        uint64_t live_round_start_digest,
        uint8_t live_battle_status,
        uint32_t lifecycle_mismatch_mask) noexcept
    {
        constexpr uint32_t kAllowedMismatchMask =
            (1u << 5) | (1u << 8) | (1u << 10);
        (void)expected_round_start_digest;
        return (expected_battle_status == 1
                || expected_battle_status == 2)
            && live_battle_status == 1
            && live_round_start_digest != 0
            && (live_round_ordinal & 0xFFFFu)
                == ((expected_round_ordinal + 1u) & 0xFFFFu)
            && (lifecycle_mismatch_mask & (1u << 8)) != 0
            && (lifecycle_mismatch_mask & ~kAllowedMismatchMask) == 0;
    }

    // Keep rollback ownership through the result and NewRound sequence. Rearm
    // only after SC6 commits the immediately next round identity. Battle
    // status 3 or a nonzero result marker alone is not a round boundary.
    static constexpr bool RollbackStockRoundResultHandoffEligible(
        uint32_t expected_round_ordinal,
        uint8_t expected_battle_status,
        uint32_t live_round_ordinal,
        uint8_t live_battle_status,
        uint32_t lifecycle_mismatch_mask) noexcept
    {
        constexpr uint32_t kAllowedMismatchMask =
            (1u << 5) | (1u << 8) | (1u << 10);
        return (expected_battle_status == 1
                || expected_battle_status == 2)
            && RollbackRoundSequenceStateOwned(
                expected_battle_status, live_battle_status)
            && (live_round_ordinal & 0xFFFFu)
                == ((expected_round_ordinal + 1u) & 0xFFFFu)
            && (lifecycle_mismatch_mask & ~kAllowedMismatchMask) == 0
            && (lifecycle_mismatch_mask & (1u << 8)) != 0;
    }

    static constexpr bool RollbackStockRoundTransitionTokenEligible(
        uint32_t expected_round_ordinal,
        uint32_t live_round_ordinal,
        bool immutable_identity_matches,
        bool round_start_digest_valid,
        bool round_sequence_state_owned) noexcept
    {
        return immutable_identity_matches
            && round_start_digest_valid
            && round_sequence_state_owned
            && (live_round_ordinal & 0xFFFFu)
                == ((expected_round_ordinal + 1u) & 0xFFFFu);
    }

    static inline bool RollbackStockAttachObservedMatchSame(
        const RollbackLifecycleEpoch& armed,
        const RollbackLifecycleEpoch& live) noexcept
    {
        return armed.presence == 8
            && live.presence == 8
            && armed.battle_manager == live.battle_manager
            && armed.input_log == live.input_log
            && armed.stage_actor_manager == live.stage_actor_manager
            && armed.native_stage_identity != 0
            && armed.native_stage_identity
                == live.native_stage_identity;
    }

    static inline bool RollbackStockAttachBoundaryEligible(
        const RollbackLifecycleEpoch& armed,
        const RollbackLifecycleEpoch& live,
        bool allow_current_round) noexcept
    {
        if (!RollbackStockAttachObservedMatchSame(armed, live)
            || !live.active_for(RollbackLifecycleMode::StockOnlinePvp))
        {
            return false;
        }
        return allow_current_round
            && live.round_ordinal == armed.round_ordinal;
    }

    struct RollbackManifestValidationReport
    {
        bool ok {false};
        bool live_ready {false};
        uint32_t pending_gameplay_entries {0};
        uint32_t invalid_entries {0};
        uint32_t overlapping_entries {0};
        uint32_t missing_capability_entries {0};
        uint32_t first_entry {0xFFFFFFFFu};
        uint32_t second_entry {0xFFFFFFFFu};
        const char* failure {"not-run"};
    };

    struct RollbackSnapshotManifest
    {
        uint32_t version {5};
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
            h.add_scalar(kRollbackFloatingPointPolicyId);
            h.add_scalar(kRollbackFloatingPointMxcsr);
            h.add_scalar(kRollbackFloatingPointX87Control);
            h.add_scalar(kRollbackFloatingPointX87ControlMask);
            h.add_scalar(max_rollback_frames);
            for (const auto& e : entries)
            {
                if (e.name)
                    h.add_bytes(e.name, std::strlen(e.name));
                h.add_scalar(e.offset);
                h.add_scalar(e.bytes);
                h.add_scalar(static_cast<uint8_t>(e.coverage));
                h.add_scalar(static_cast<uint8_t>(e.canonical_policy));
                h.add_scalar(static_cast<uint8_t>(e.capability));
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
            reset_metadata();
        }

        void recycle_for_capture()
        {
            epoch = {};
            ranges.clear();
            reset_metadata();
        }

    private:
        void reset_metadata()
        {
            schema_hash = 0;
            canonical_hash = 0;
            integrity_hash = 0;
            hash = 0;
        }

    public:
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
        h.add_scalar(frame.epoch.stage_actor_order_digest);
        h.add_scalar(frame.epoch.native_stage_identity);
        h.add_scalar(frame.epoch.input_log_frame);
        h.add_scalar(frame.epoch.round_ordinal);
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
        // Each range hash already commits every captured byte. Rehashing the
        // entire backing buffer here made production capture scan explicit
        // state a third time (range, canonical, then frame integrity).
        // Validation recomputes every range hash before trusting this digest.
        return h.value;
    }

    static inline bool RollbackAddSnapshotRangeCanonical(
        RollbackFastHash& h,
        const RollbackSnapshotFrame& frame,
        const RollbackSnapshotRange& r) noexcept
    {
        h.add_scalar(r.manifest_index);
        h.add_scalar(r.bytes);
        if (r.bytes_offset > frame.bytes.size()
            || static_cast<size_t>(r.bytes)
                > frame.bytes.size() - r.bytes_offset)
        {
            return false;
        }
        if (r.canonical_policy == RollbackCanonicalPolicy::LuxBattleInputRing
            && r.bytes != 2u * 0x3Du * sizeof(uint64_t))
        {
            return false;
        }
        if (r.canonical_policy
                == RollbackCanonicalPolicy::LuxBattleInputRingCursor
            && r.bytes != 2u * sizeof(uint32_t))
        {
            return false;
        }

        const uint8_t* bytes = frame.bytes.data() + r.bytes_offset;
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
        else if (r.canonical_policy
                    == RollbackCanonicalPolicy::
                        LuxBattleWorldModeControl
            && r.bytes == 0x14
            && r.address >= 0x4843ED0ull)
        {
            const uintptr_t image_base = r.address - 0x4843ED0ull;
            for (size_t offset : {size_t{0}, size_t{8}})
            {
                uintptr_t pointer = 0;
                std::memcpy(&pointer, bytes + offset,
                    sizeof(pointer));
                const uintptr_t relative = pointer == 0
                    ? 0 : pointer - image_base;
                h.add_scalar(relative);
            }
            h.add_bytes(bytes + 0x10, 4);
        }
        else if (r.canonical_policy
                    == RollbackCanonicalPolicy::LuxBattleInputRing)
        {
            // Mode 1 addresses this circular history relative to its
            // monotonically increasing cursor. Different physical
            // rotations therefore represent the same gameplay history.
            // Keep the raw bytes for same-process restore, but compare
            // peers in logical cursor-relative order.
            const RollbackSnapshotRange* cursor_range = nullptr;
            for (const RollbackSnapshotRange& candidate : frame.ranges)
            {
                if (candidate.address == r.address + r.bytes
                    && candidate.bytes == 2u * sizeof(uint32_t)
                    && candidate.canonical_policy
                        == RollbackCanonicalPolicy::
                            LuxBattleInputRingCursor)
                {
                    cursor_range = &candidate;
                    break;
                }
            }
            if (!cursor_range
                || cursor_range->bytes_offset > frame.bytes.size()
                || cursor_range->bytes
                    > frame.bytes.size() - cursor_range->bytes_offset)
            {
                return false;
            }
            const uint8_t* cursor_bytes = frame.bytes.data()
                + cursor_range->bytes_offset;
            constexpr size_t kPlayers = 2;
            constexpr size_t kEntries = 0x3D;
            constexpr size_t kEntryBytes = sizeof(uint64_t);
            for (size_t player = 0; player < kPlayers; ++player)
            {
                uint32_t cursor = 0;
                std::memcpy(&cursor,
                    cursor_bytes + player * sizeof(cursor),
                    sizeof(cursor));
                const size_t first = cursor % kEntries;
                h.add_scalar(player);
                for (size_t logical = 0; logical < kEntries;
                     ++logical)
                {
                    const size_t physical =
                        (first + logical) % kEntries;
                    h.add_bytes(bytes
                            + (player * kEntries + physical)
                                * kEntryBytes,
                        kEntryBytes);
                }
            }
        }
        else if (r.canonical_policy
                    == RollbackCanonicalPolicy::
                        LuxBattleInputRingCursor)
        {
            bool ring_present = false;
            for (const RollbackSnapshotRange& candidate : frame.ranges)
            {
                ring_present = candidate.address + candidate.bytes
                        == r.address
                    && candidate.bytes
                        == 2u * 0x3Du * sizeof(uint64_t)
                    && candidate.canonical_policy
                        == RollbackCanonicalPolicy::LuxBattleInputRing;
                if (ring_present) break;
            }
            if (!ring_present) return false;
            // The paired ring policy already commits the complete logical
            // history. The absolute cursor is only the physical rotation
            // used by this process and remains restore-only.
        }
        else if (r.canonical_policy
                    == RollbackCanonicalPolicy::
                        LuxBattleCollisionCooldown)
        {
            if (r.bytes != sizeof(uint32_t) || r.address < 0xE14u)
                return false;
            const RollbackSnapshotRange* frame_counter_range = nullptr;
            for (const RollbackSnapshotRange& candidate : frame.ranges)
            {
                if (candidate.address + 0xE14u == r.address
                    && candidate.bytes == sizeof(uint32_t)
                    && candidate.canonical_policy
                        == RollbackCanonicalPolicy::
                            LuxBattleNativeFrameCounter)
                {
                    frame_counter_range = &candidate;
                    break;
                }
            }
            if (!frame_counter_range
                || frame_counter_range->bytes_offset > frame.bytes.size()
                || frame_counter_range->bytes
                    > frame.bytes.size()
                        - frame_counter_range->bytes_offset)
            {
                return false;
            }
            if (frame_counter_range->address < 0x619418u)
                return false;
            bool owner_present = false;
            for (const RollbackSnapshotRange& candidate : frame.ranges)
            {
                owner_present = candidate.address
                        == frame_counter_range->address - 0x619418u
                    && candidate.bytes == sizeof(uint32_t)
                    && candidate.canonical_policy
                        == RollbackCanonicalPolicy::
                            LuxBattleCollisionOwner;
                if (owner_present) break;
            }
            if (!owner_present) return false;
            uint32_t current_frame = 0;
            uint32_t last_dispatch_frame = 0;
            std::memcpy(&current_frame,
                frame.bytes.data() + frame_counter_range->bytes_offset,
                sizeof(current_frame));
            std::memcpy(&last_dispatch_frame, bytes,
                sizeof(last_dispatch_frame));
            h.add_scalar(RollbackCollisionCooldownRemaining(
                current_frame, last_dispatch_frame));
        }
        else if (r.canonical_policy
                    == RollbackCanonicalPolicy::
                        LuxBattleNativeFrameCounter)
        {
            bool cooldown_present = false;
            for (const RollbackSnapshotRange& candidate : frame.ranges)
            {
                cooldown_present = r.address + 0xE14u
                        == candidate.address
                    && candidate.bytes == sizeof(uint32_t)
                    && candidate.canonical_policy
                        == RollbackCanonicalPolicy::
                            LuxBattleCollisionCooldown;
                if (cooldown_present) break;
            }
            if (!cooldown_present) return false;
            // The paired cooldown policy commits the gameplay-relevant age.
            // The absolute native frame remains byte-exact local restore state.
        }
        else if (r.canonical_policy
                    == RollbackCanonicalPolicy::LuxBattleCollisionOwner)
        {
            if (r.bytes != sizeof(uint32_t)
                || r.address > (std::numeric_limits<uintptr_t>::max)()
                    - 0x619418u)
            {
                return false;
            }
            const uintptr_t frame_address = r.address + 0x619418u;
            if (frame_address > (std::numeric_limits<uintptr_t>::max)()
                    - 0xE14u)
            {
                return false;
            }
            const RollbackSnapshotRange* frame_range = nullptr;
            const RollbackSnapshotRange* cooldown_range = nullptr;
            for (const RollbackSnapshotRange& candidate : frame.ranges)
            {
                if (candidate.address == frame_address
                    && candidate.bytes == sizeof(uint32_t)
                    && candidate.canonical_policy
                        == RollbackCanonicalPolicy::
                            LuxBattleNativeFrameCounter)
                {
                    frame_range = &candidate;
                }
                else if (candidate.address == frame_address + 0xE14u
                    && candidate.bytes == sizeof(uint32_t)
                    && candidate.canonical_policy
                        == RollbackCanonicalPolicy::
                            LuxBattleCollisionCooldown)
                {
                    cooldown_range = &candidate;
                }
            }
            if (!frame_range || !cooldown_range
                || frame_range->bytes_offset > frame.bytes.size()
                || frame_range->bytes
                    > frame.bytes.size() - frame_range->bytes_offset
                || cooldown_range->bytes_offset > frame.bytes.size()
                || cooldown_range->bytes
                    > frame.bytes.size() - cooldown_range->bytes_offset)
            {
                return false;
            }
            uint32_t current_frame = 0;
            uint32_t last_dispatch_frame = 0;
            uint32_t owner = 0;
            std::memcpy(&current_frame,
                frame.bytes.data() + frame_range->bytes_offset,
                sizeof(current_frame));
            std::memcpy(&last_dispatch_frame,
                frame.bytes.data() + cooldown_range->bytes_offset,
                sizeof(last_dispatch_frame));
            std::memcpy(&owner, bytes, sizeof(owner));
            const uint32_t remaining = RollbackCollisionCooldownRemaining(
                current_frame, last_dispatch_frame);
            h.add_scalar(remaining == 0 ? 0u : owner);
        }
        else
        {
            h.add_bytes(bytes, r.bytes);
        }
        return true;
    }

    static inline uint64_t HashRollbackSnapshotRangeCanonical(
        const RollbackSnapshotFrame& frame,
        const RollbackSnapshotRange& range) noexcept
    {
        RollbackFastHash h {};
        return RollbackAddSnapshotRangeCanonical(h, frame, range)
            ? h.finish() : 0;
    }

    static inline uint64_t HashRollbackSnapshotCanonical(
        const RollbackSnapshotFrame& frame) noexcept
    {
        RollbackFastHash h {};
        h.add_scalar(frame.schema_hash);
        for (const RollbackSnapshotRange& range : frame.ranges)
        {
            if (!RollbackAddSnapshotRangeCanonical(h, frame, range))
                return 0;
        }
        return h.finish();
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
            const bool live_claim = e.coverage != RollbackCoverage::Unknown
                && e.coverage != RollbackCoverage::PendingEvidence
                && e.coverage != RollbackCoverage::DiagnosticOnly;
            if (live_claim)
            {
                const RollbackCoverageCapability* capability =
                    FindRollbackCoverageCapability(e.capability);
                if (!capability
                    || !RollbackCoverageCapabilityComplete(*capability))
                {
                    ++out.missing_capability_entries;
                    if (out.first_entry == 0xFFFFFFFFu)
                        out.first_entry = static_cast<uint32_t>(i);
                }
            }
            if (e.coverage == RollbackCoverage::ExplicitSnapshot
                && (e.address == 0 || e.bytes == 0
                    || e.address
                        > (std::numeric_limits<uintptr_t>::max)() - e.offset))
            {
                ++out.invalid_entries;
                if (out.first_entry == 0xFFFFFFFFu)
                    out.first_entry = static_cast<uint32_t>(i);
            }
            if (e.canonical_policy
                    == RollbackCanonicalPolicy::
                        LuxBattleNativeFrameCounter
                || e.canonical_policy
                    == RollbackCanonicalPolicy::
                        LuxBattleCollisionCooldown)
            {
                const bool is_counter = e.canonical_policy
                    == RollbackCanonicalPolicy::
                        LuxBattleNativeFrameCounter;
                const uintptr_t effective = e.address + e.offset;
                const uintptr_t counter_address = is_counter
                    ? effective : effective - 0xE14u;
                const bool expected_addresses = manifest.image_base
                        <= (std::numeric_limits<uintptr_t>::max)()
                            - 0x470DED8u
                    && counter_address
                        == manifest.image_base + 0x470D0C4u
                    && (is_counter
                        ? effective == manifest.image_base + 0x470D0C4u
                        : effective == manifest.image_base + 0x470DED8u);
                bool counterpart_present = false;
                bool owner_present = false;
                if (expected_addresses
                    && e.coverage == RollbackCoverage::ExplicitSnapshot
                    && e.capability
                        == RollbackCoverageCapabilityId::ExplicitState
                    && e.bytes == sizeof(uint32_t)
                    && (!is_counter || effective <=
                        (std::numeric_limits<uintptr_t>::max)() - 0xE14u)
                    && (is_counter || effective >= 0xE14u)
                    && counter_address >= 0x619418u)
                {
                    for (const RollbackManifestEntry& candidate
                         : manifest.entries)
                    {
                        const uintptr_t candidate_effective =
                            candidate.address + candidate.offset;
                        const auto counterpart_policy = is_counter
                            ? RollbackCanonicalPolicy::
                                LuxBattleCollisionCooldown
                            : RollbackCanonicalPolicy::
                                LuxBattleNativeFrameCounter;
                        const uintptr_t counterpart_address = is_counter
                            ? counter_address + 0xE14u : counter_address;
                        counterpart_present = counterpart_present
                            || (candidate_effective == counterpart_address
                                && candidate.bytes == sizeof(uint32_t)
                                && candidate.coverage
                                    == RollbackCoverage::ExplicitSnapshot
                                && candidate.capability
                                    == RollbackCoverageCapabilityId::
                                        ExplicitState
                                && candidate.canonical_policy
                                    == counterpart_policy);
                        owner_present = owner_present
                            || (candidate_effective
                                    == counter_address - 0x619418u
                                && candidate.bytes == sizeof(uint32_t)
                                && candidate.coverage
                                    == RollbackCoverage::ExplicitSnapshot
                                && candidate.capability
                                    == RollbackCoverageCapabilityId::
                                        ExplicitState
                                && candidate.canonical_policy
                                    == RollbackCanonicalPolicy::
                                        LuxBattleCollisionOwner);
                    }
                }
                if (!counterpart_present || !owner_present)
                {
                    ++out.invalid_entries;
                    if (out.first_entry == 0xFFFFFFFFu)
                        out.first_entry = static_cast<uint32_t>(i);
                }
            }
            if (e.canonical_policy
                    == RollbackCanonicalPolicy::LuxBattleCollisionOwner)
            {
                const uintptr_t effective = e.address + e.offset;
                const bool address_ok = manifest.image_base
                        <= (std::numeric_limits<uintptr_t>::max)()
                            - 0x40F3CACu
                    && effective == manifest.image_base + 0x40F3CACu
                    && effective
                        <= (std::numeric_limits<uintptr_t>::max)()
                            - 0x619418u;
                bool frame_present = false;
                bool cooldown_present = false;
                if (address_ok
                    && e.coverage == RollbackCoverage::ExplicitSnapshot
                    && e.capability
                        == RollbackCoverageCapabilityId::ExplicitState
                    && e.bytes == sizeof(uint32_t))
                {
                    const uintptr_t frame_address = effective + 0x619418u;
                    for (const RollbackManifestEntry& candidate
                         : manifest.entries)
                    {
                        const uintptr_t candidate_effective =
                            candidate.address + candidate.offset;
                        frame_present = frame_present
                            || (candidate_effective == frame_address
                                && candidate.bytes == sizeof(uint32_t)
                                && candidate.canonical_policy
                                    == RollbackCanonicalPolicy::
                                        LuxBattleNativeFrameCounter);
                        cooldown_present = cooldown_present
                            || (candidate_effective
                                    == frame_address + 0xE14u
                                && candidate.bytes == sizeof(uint32_t)
                                && candidate.canonical_policy
                                    == RollbackCanonicalPolicy::
                                        LuxBattleCollisionCooldown);
                    }
                }
                if (!frame_present || !cooldown_present)
                {
                    ++out.invalid_entries;
                    if (out.first_entry == 0xFFFFFFFFu)
                        out.first_entry = static_cast<uint32_t>(i);
                }
            }
            if (e.canonical_policy
                    == RollbackCanonicalPolicy::LuxBattleInputRing
                || e.canonical_policy
                    == RollbackCanonicalPolicy::LuxBattleInputRingCursor)
            {
                const bool is_ring = e.canonical_policy
                    == RollbackCanonicalPolicy::LuxBattleInputRing;
                const uint32_t expected_bytes = is_ring
                    ? 2u * 0x3Du * sizeof(uint64_t)
                    : 2u * sizeof(uint32_t);
                const uintptr_t effective = e.address + e.offset;
                bool paired = e.coverage
                        == RollbackCoverage::ExplicitSnapshot
                    && e.bytes == expected_bytes;
                if (paired)
                {
                    paired = false;
                    for (const RollbackManifestEntry& candidate
                         : manifest.entries)
                    {
                        const bool candidate_is_pair = is_ring
                            ? candidate.canonical_policy
                                == RollbackCanonicalPolicy::
                                    LuxBattleInputRingCursor
                            : candidate.canonical_policy
                                == RollbackCanonicalPolicy::
                                    LuxBattleInputRing;
                        const uintptr_t candidate_effective =
                            candidate.address + candidate.offset;
                        const bool address_matches = is_ring
                            ? candidate_effective
                                == effective + expected_bytes
                            : candidate_effective + candidate.bytes
                                == effective;
                        if (candidate_is_pair
                            && candidate.coverage
                                == RollbackCoverage::ExplicitSnapshot
                            && address_matches)
                        {
                            paired = true;
                            break;
                        }
                    }
                }
                if (!paired)
                {
                    ++out.invalid_entries;
                    if (out.first_entry == 0xFFFFFFFFu)
                        out.first_entry = static_cast<uint32_t>(i);
                }
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
        else if (out.missing_capability_entries != 0)
            out.failure = "incomplete-coverage-capability";
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
        if (snapshot_epoch.native_stage_identity
            != live_epoch.native_stage_identity)
        {
            out.status = RollbackEpochValidationStatus::StageLayoutMismatch;
            out.failure = "native-stage-identity-mismatch";
            return out;
        }
        if (snapshot_epoch.round_ordinal != live_epoch.round_ordinal)
        {
            out.status = RollbackEpochValidationStatus::RoundStartMismatch;
            out.failure = "round-ordinal-epoch-mismatch";
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
        snapshot.presence = 7;
        live.presence = 7;
        snapshot.pvp_active = true;
        live.pvp_active = true;
        return ValidateRollbackLifecycleEpoch(
            snapshot, live, RollbackLifecycleMode::StockOnlinePvp);
    }

    static inline RollbackSnapshotCopyReport CaptureRollbackSnapshotBytes(
        const RollbackSnapshotManifest& manifest,
        RollbackSnapshotFrame& out,
        bool require_preallocated = false)
    {
        RollbackSnapshotCopyReport report{};
        report.failure = "ok";
        out.recycle_for_capture();
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

        if (require_preallocated
            && (total_bytes > out.bytes.capacity()
                || explicit_entries > out.ranges.capacity()))
        {
            report.failure = "snapshot-preallocated-capacity-exceeded";
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
            r.hash = RollbackFastIntegrityHashBytes(dst, e.bytes);
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

    static inline bool RollbackSnapshotPreallocatedCaptureReady(
        const RollbackSnapshotManifest& manifest,
        const RollbackSnapshotFrame& out) noexcept
    {
        size_t total_bytes = 0;
        size_t explicit_entries = 0;
        for (const RollbackManifestEntry& entry : manifest.entries)
        {
            if (!rollback_manifest_entry_is_explicit_copy(entry)) continue;
            if (total_bytes > (std::numeric_limits<size_t>::max)()
                    - static_cast<size_t>(entry.bytes))
                return false;
            total_bytes += static_cast<size_t>(entry.bytes);
            ++explicit_entries;
        }
        return explicit_entries != 0
            && total_bytes <= out.bytes.capacity()
            && explicit_entries <= out.ranges.capacity();
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
            if (RollbackFastIntegrityHashBytes(src, r.bytes) != r.hash)
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
        RollbackSnapshotFrame& emergency,
        bool require_preallocated = false) noexcept
    {
        emergency.clear();
        emergency.epoch = target.epoch;
        emergency.schema_hash = target.schema_hash;
        if (require_preallocated
            && (target.bytes.size() > emergency.bytes.capacity()
                || target.ranges.size() > emergency.ranges.capacity()))
            return false;
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
            r.hash = RollbackFastIntegrityHashBytes(dst, r.bytes);
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
            if (r.canonical_policy
                    == RollbackCanonicalPolicy::LuxMoveSchedStateArray
                && r.bytes == 0xC0)
            {
                // FLuxMoveSchedState[2] contains current-round identities at
                // +0x10 pChara and +0x50 pSubVM in each 0x60-byte record.
                // Validate the generation, then restore only scalar storage.
                static constexpr size_t kIdentityOffsets[] = {0x10, 0x50};
                static constexpr size_t kWriteOffsets[] = {0x00, 0x18, 0x58};
                static constexpr size_t kWriteSizes[] = {0x10, 0x38, 0x08};
                for (size_t slot = 0; slot < 2; ++slot)
                {
                    for (size_t identity : kIdentityOffsets)
                    {
                        uintptr_t captured = 0;
                        uintptr_t live = 0;
                        std::memcpy(&captured,
                            src + slot * 0x60 + identity,
                            sizeof(captured));
                        if (!SafeReadBytes(reinterpret_cast<const void*>(
                                r.address + slot * 0x60 + identity),
                                &live, sizeof(live))
                            || live != captured)
                        {
                            report.failure =
                                "lux-move-sched-generation-mismatch";
                            report.failed_entry = r.manifest_index;
                            report.failed_address =
                                r.address + slot * 0x60 + identity;
                            return false;
                        }
                    }
                }
                for (size_t slot = 0; slot < 2; ++slot)
                {
                    for (size_t span = 0; span < std::size(kWriteOffsets);
                         ++span)
                    {
                        const size_t offset = slot * 0x60
                            + kWriteOffsets[span];
                        if (!SafeWriteBytes(reinterpret_cast<void*>(
                                r.address + offset), src + offset,
                                kWriteSizes[span]))
                        {
                            report.failure = "write-fault";
                            report.failed_entry = r.manifest_index;
                            report.failed_address = r.address + offset;
                            return false;
                        }
                    }
                }
            }
            else if (!SafeWriteBytes(
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

    static inline bool ValidateRollbackMoveSchedGenerations(
        const RollbackSnapshotFrame& snapshot) noexcept
    {
        for (const RollbackSnapshotRange& r : snapshot.ranges)
        {
            if (r.canonical_policy
                    != RollbackCanonicalPolicy::LuxMoveSchedStateArray
                || r.bytes != 0xC0
                || r.bytes_offset > snapshot.bytes.size()
                || r.bytes > snapshot.bytes.size() - r.bytes_offset)
                continue;
            const uint8_t* src = snapshot.bytes.data() + r.bytes_offset;
            for (size_t slot = 0; slot < 2; ++slot)
            {
                for (size_t identity : {size_t{0x10}, size_t{0x50}})
                {
                    uintptr_t captured = 0;
                    uintptr_t live = 0;
                    std::memcpy(&captured,
                        src + slot * 0x60 + identity, sizeof(captured));
                    if (!SafeReadBytes(reinterpret_cast<const void*>(
                            r.address + slot * 0x60 + identity),
                            &live, sizeof(live))
                        || live != captured)
                        return false;
                }
            }
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

    // Production rollback already owns the native boundary and performs one
    // complete post-restore capture in RollbackStepHarness. Do only the
    // validated write here: no nested emergency snapshot, retry, or second
    // verification allocation.
    static inline RollbackSnapshotCopyReport
    RestoreRollbackSnapshotBytesOnce(
        const RollbackSnapshotFrame& snapshot) noexcept
    {
        RollbackSnapshotCopyReport report {};
        report.failure = "ok";
        if (!ValidateRollbackSnapshotFrame(snapshot, report))
            return report;
        if (!WriteRollbackSnapshotFrameUnchecked(snapshot, report))
            return report;
        report.hash = snapshot.hash;
        report.ok = true;
        return report;
    }

    // CaptureRollbackStepState has just populated this frame and has not
    // exposed it to the arena yet. HgCpu's native writer mutates a subset of
    // these globals, so restore that fresh copy exactly once without taking
    // an emergency copy and verification copy on every ordinary Advance.
    // Stored snapshots still use the fully validated Load path above.
    static inline RollbackSnapshotCopyReport
    RestoreFreshRollbackSnapshotBytesOnce(
        const RollbackSnapshotFrame& snapshot) noexcept
    {
        RollbackSnapshotCopyReport report {};
        report.failure = "ok";
        if (snapshot.ranges.empty() || snapshot.bytes.empty()
            || snapshot.schema_hash == 0 || snapshot.canonical_hash == 0
            || snapshot.integrity_hash == 0)
        {
            report.failure = "fresh-snapshot-invalid";
            return report;
        }
        for (const RollbackSnapshotRange& range : snapshot.ranges)
        {
            if (range.address == 0 || range.bytes == 0
                || range.bytes_offset > snapshot.bytes.size()
                || static_cast<size_t>(range.bytes)
                    > snapshot.bytes.size() - range.bytes_offset)
            {
                report.failure = "fresh-snapshot-range-invalid";
                report.failed_entry = range.manifest_index;
                report.failed_address = range.address;
                return report;
            }
        }
        if (!WriteRollbackSnapshotFrameUnchecked(snapshot, report))
            return report;
        report.hash = snapshot.hash;
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

    static inline RollbackSnapshotCopyReport
    RestoreRollbackSnapshotBytesOnceIfEpochMatches(
        const RollbackSnapshotFrame& snapshot,
        const RollbackLifecycleEpoch& live_epoch,
        bool replay_fork_lab = false) noexcept
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
        return RestoreRollbackSnapshotBytesOnce(snapshot);
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
            "Native ExecMoveChangeAndPost/ExecFinalizeAndPost pair; Ghidra FLuxHgCpuBuffer size is 0x28018.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::HgCpuState
        });
        m.entries.push_back({
            "HgCpu canonical field map",
            0,
            0,
            0,
            RollbackCoverage::HgCpuDirectCovered,
            "Peer canonicalization hashes the Ghidra-verified native HgCpu field stream and logical KHit payloads. Horse-only motion, timer, and skeleton-runtime graph images remain local restore-integrity caches; their raw bytes and process links are excluded, while stable slot/component/node shape is covered and every restore is recaptured and verified.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::HgCpuState
        });
        m.entries.push_back({
            "KHit allocator and node lifetime",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "KHit nodes are canonicalized and restored by logical player/list/node index. Restore captures the live topology, requires identical logical shape/tag/serialized size, writes only native writer-serialized payload fields, and preserves live vtables, next links, list controls, and allocator ownership.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::KHitState
        });
        m.entries.push_back({
            "Coordinated peer baseline activation",
            0,
            0,
            0,
            RollbackCoverage::AuthenticatedLaunchContract,
            "StockOnlinePvp authenticates lifecycle mode, opposite Gekko slots, native-input ownership, rollback settings, deterministic fixture, and the accepted BattleBaseline before the native tick boundary is owned.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::SessionBaseline
        });
        m.entries.push_back({
            "Historical camera argument policy",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "Horse captures the intercepted 24-byte camera argument on each newly simulated frame and replays the exact retained bytes for duplicate or rollback Advances. The 128-frame integrity-checked ring exceeds the 60-frame production rollback window and fails closed on missing history, collisions, or corruption. Camera state remains excluded from the cross-peer gameplay digest.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::HistoricalCamera
        });
        m.entries.push_back({
            "Round-start canonical field map",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "Lifecycle identity hashes only Ghidra-observed FLuxBattleRoundStartData gameplay fields: move-provider ID at +0x00 and the three position scalars at +0x20/+0x24/+0x28. Opaque reserved bytes, padding, and possible process links are excluded.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::RoundIdentity
        });
        m.entries.push_back({
            "BattleManager native round-state queue",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "Per-step fixed-value capture restores BattleManager +0x1480 and the bounded TArray values/count at +0x1470/+0x1478. The live allocation and capacity are retained; Load never allocates and fails closed if retained capacity cannot hold the captured queue.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::NativeRoundStateQueue
        });
        m.entries.push_back({
            "BattleManager native SimulationLoop scalars",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "RollbackNativeSimulationState captures and restores the fixed BattleManager fields mutated by one complete native input-delta iteration, including the values behind the stable input-pair and previous-input pointers, command state, track-completion scalars at +0x14E0/+0x14E4/+0x14E8/+0x14EC, timers, active-state publication, and pending-dispatch state. The scheduling cursors are restored before snapshot capture and are not rollback state.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::NativeSimulationState
        });
        m.entries.push_back({
            "MoveVM palette-variant checkpoint slots",
            0,
            0,
            4 * 0x28018,
            RollbackCoverage::DynamicSnapshot,
            "Ghidra proves four CBattleSerializeStream<char,163840> objects at active-session +0xA8 with 0x28018-byte stride, paired with g_anMoveVmPaletteVariantSlots. Horse captures active slot states, cursors, and bounded payloads; binds local restore to the same active-session object; applies the ordinary pointer-normalized HgCpu chara/KHit canonicalization to each latent payload; restores buffers before publishing scalars; and recaptures exact local integrity after Load.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::PaletteVariantState
        });
        m.entries.push_back({
            "g_dwLuxBattleLcgRngState",
            addr(0x14485EB28ull),
            0,
            4,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra label uint. LuxMoveVM_ApplyAIPaletteMode consumes this Park-Miller LCG while native HgCpu restore rebuilds AI palette slots; replay-fork rollback must restore it after that reader side effect. Capturing it is harmless for human PVP and prevents fixture-only RNG drift.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_adwLuxBattleLfsrState",
            addr(0x14485EB30ull),
            0,
            0x64,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra type byte[100]; RNG xrefs compare index against 0x19 and write dwords through this state array.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_dwLuxBattleLfsrIndex",
            addr(0x14485EB94ull),
            0,
            4,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra type uint; RNG xrefs read/write the index beside g_adwLuxBattleLfsrState.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_LuxBattleRoundControlScalars",
            addr(0x14484639Cull),
            0,
            0x14,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra-verified contiguous scalars: result count/limit, current/applied round index, committed result flow, and match-complete flag. LuxBattle_EvaluateRoundResult and NewRound consume these across rollbackable result transitions.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_LuxBattleLastRoundResultPair",
            addr(0x144846408ull),
            0,
            4,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra ELuxBattleRoundResultType pair at +0x00/+0x02. The evaluator rejects a second commit while the primary value is nonzero, so Load must restore both values.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_LuxBattleRoundResultWinnerSlot",
            addr(0x144846420ull),
            0,
            2,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra ushort winner sentinel written by LuxBattle_EvaluateRoundResult and consumed by result/NewRound setup.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::ExplicitState
        });
        // HgCpu does not serialize the reusable world-mode objects. In
        // particular, ActiveBattle's frame/scratch fields continue advancing
        // once per native resimulation. If they are omitted, peers can restore
        // the same terminal fighter state but later re-enter the stock
        // ActiveBattle/status-3 bridge at different native coordinates.
        //
        // Exclude each static object's vtable at +0x00. It is immutable
        // process identity, not simulation state. The mutable scalar tails are
        // exact local restore bytes and peer-canonical gameplay state.
        m.entries.push_back({
            "g_LuxBattleWorldMode_Active_Mutable",
            addr(0x144100B40ull),
            0,
            0x20,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra FLuxBattleActiveBattlePhaseState_Partial +0x08..+0x27. The frame/sentinel and replay-effect scratch are mutated by ActiveBattle OnEnter/Tick and must rewind with fighter state; the immutable vtable at +0x00 is deliberately excluded.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_LuxBattleWorldMode_RoundResult_Mutable",
            addr(0x144100D90ull),
            0,
            0x30,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra RoundResult world-mode object +0x08..+0x37. Capture the native coordinate and mutable control scalars without hashing or restoring the immutable process-local vtable.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::ExplicitState
        });
        // Restore the two process pointers selecting the current and queued
        // objects explicitly; canonical hashing normalizes them to RVAs.
        m.entries.push_back({
            "g_LuxBattleWorldModeControl",
            addr(0x144843ED0ull),
            0,
            0x14,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra FLuxBattle_WorldModePump +0x00 current mode, +0x08 queued mode, and +0x10 transition-completed scalar. Missing pointer restore lets a Load combine pre-result character state with a post-result ActiveBattle mode.",
            RollbackCanonicalPolicy::LuxBattleWorldModeControl,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_LuxBattle_LatestEngineInput_PerPlayer",
            addr(0x144855700ull),
            0,
            0x10,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra retagged as ulonglong[2]; PerFrameTick writes qwords and LuxBattle_TickCharaInput consumes them.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_LuxBattle_PerPlayerInputRing",
            addr(0x14485E750ull),
            0,
            0x3D0,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra type FLuxBattleInputRing[2]; chara input consumer uses 0x3D qword ring entries per player.",
            RollbackCanonicalPolicy::LuxBattleInputRing,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_LuxBattle_PerPlayerInputRingCursor",
            addr(0x14485EB20ull),
            0,
            0x08,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra type int[2]; exactly 8 bytes. The former 0x10-byte range overlapped g_dwLuxBattleLcgRngState at 0x14485EB28.",
            RollbackCanonicalPolicy::LuxBattleInputRingCursor,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_stLuxBattleWindCombinedRngState",
            addr(0x14470E2B0ull),
            0,
            0x18,
            RollbackCoverage::DynamicSnapshot,
            "Ghidra type FLuxBattleWindCombinedRngState (24 bytes). IwWind_UpdateParallelOscillation conditionally consumes it to publish the parallel node's currentForce and currentAngles; LuxBattle_TickStageWindAndAccumulateForces then publishes those values into the root and fighter wind outputs. RollbackStageWindSnapshot captures, restores, verifies, peer-hashes, and launch-authorizes all six words with the wind graph.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::StageWind
        });
        m.entries.push_back({
            "g_flLuxMotionWrappedRootYawScratch",
            addr(0x14470D19Cull),
            0,
            sizeof(float),
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra float written by LuxMotion_BlendKeyframeTransforms selector 0x16 and read/re-written by SolveBonePose and EvaluateHitCueRootVector. This wrapped angular side channel crosses native pose-call lifetimes and must rewind with rollback.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_abLuxMotionPoseSharedScratch",
            addr(0x14470DE50ull),
            0,
            0x40,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra FLuxMotionPoseSharedScratchBank. Exact 64-byte process-global vector/yaw scratch shared by LuxMotion_BlendKeyframeTransforms, SolveBonePose, MoveVM_EvaluateBonePose, FK traversal, and hit-cue root evaluation; image-initialized to zero and observed through read-after-write pose paths.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_LuxBattle_FrameCounter",
            addr(0x14470D0C4ull),
            0,
            sizeof(uint32_t),
            RollbackCoverage::ExplicitSnapshot,
            "The native absolute frame is restored byte-for-byte locally. Its peer-canonical contribution is the paired collision cooldown remaining under the exact unsigned predicate in LuxBattle_RecordAndDispatchCollisionContact.",
            RollbackCanonicalPolicy::LuxBattleNativeFrameCounter,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_LuxBattle_InputRingBaseOffset_PerPlayer",
            addr(0x14470DED0ull),
            0,
            2u * sizeof(uint32_t),
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra xref 0x14031262E indexes two uint32 values; LuxBattle_TickCharaInput adds the selected base to the current cursor modulo 0x3D before ring lookup.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_LuxBattle_CollisionLastDispatchFrame",
            addr(0x14470DED8ull),
            0,
            sizeof(uint32_t),
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra LuxBattle_RecordAndDispatchCollisionContact compares this value to g_LuxBattle_FrameCounter with a three-frame unsigned cooldown. Contact types 6-8 also mutate terrain collision state inside the gated branch, so peer comparison hashes remaining cooldown rather than the process-local absolute frame.",
            RollbackCanonicalPolicy::LuxBattleCollisionCooldown,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_LuxBattle_ActiveFrameContextIndex",
            addr(0x14470DEDCull),
            0,
            sizeof(uint8_t),
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra LuxBattle_SetActiveFrameContextIndex writes this selector and terrain/collision simulation reads it; the remaining three bytes after it are padding and are not captured.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_LuxBattle_CollisionLastDispatchOwner",
            addr(0x1440F3CACull),
            0,
            sizeof(uint32_t),
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra LuxBattle_RecordAndDispatchCollisionContact pairs this owner with the native-frame cooldown. The owner is peer-canonical only while that cooldown remains active; after expiry it is stale history. Exact bytes remain in the local rewind snapshot.",
            RollbackCanonicalPolicy::LuxBattleCollisionOwner,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_LuxBattle_CCpuCommandArray",
            addr(0x144715400ull),
            0,
            0xC0,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra FLuxMoveSchedState[2], stride 0x60. Capture retains identity bytes for local generation validation, but restore excludes +0x10 pChara and +0x50 pSubVM in each record. Peer canonicalization includes only selected/active slot indices, move IDs, previous IDs, change counters, and extra parameters.",
            RollbackCanonicalPolicy::LuxMoveSchedStateArray,
            RollbackCoverageCapabilityId::ExplicitState
        });
        m.entries.push_back({
            "g_abLuxMoveSystemVMPumpState",
            0,
            0,
            0x88,
            RollbackCoverage::DynamicSnapshot,
            "Typed capture restores only the two verified lane scalar records and top-level pump controls. Fighter and lane-dispatch pointers remain local generation identities. Begin, End, fighter rebinding, and state-4 SubVM replacement fail preflight instead of restoring stale identities.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::LuxMovePumpState
        });
        m.entries.push_back({
            "g_abLuxMoveVMSlotParamArray",
            0,
            0,
            0x58,
            RollbackCoverage::DynamicSnapshot,
            "Ghidra FLuxMoveVMSlotParam[2], exact 0x2C stride. The typed snapshot captures, restores, peer-canonicalizes, and recapture-verifies each authoritative +0x00..+0x27 prefix before any resimulated MoveVM advance. Initialization-only +0x28 stride padding is retained in the native layout but excluded from restore and peer authority.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::LuxMoveSlotParamState
        });
        m.entries.push_back({
            "Lux CPU-direct SubVM generations",
            0,
            0,
            2 * 0x80,
            RollbackCoverage::DynamicSnapshot,
            "The complete native factory allowlist admits only verified 0x68/0x70/0x78/0x80 derived classes. Capture hashes vtable RVA and typed scalar state, preserves fighter/opponent/owner identities, excludes the uninitialized 0x80-class tail, and rejects generation drift before any restore mutation.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::LuxSubVmState
        });
        m.entries.push_back({
            "g_abLuxMoveCommandPlayers semantic arena",
            0,
            0,
            0x6070,
            RollbackCoverage::DynamicSnapshot,
            "Ghidra FLuxMoveCommandPlayerSlot[2], exact 0x3038 stride. Typed capture partitions every byte into nine semantic banks (12,076 bytes), 17 local generation identities, a diagnostic 0x80-byte text buffer, and a four-byte uninitialized tail. Restore preflights every identity across both slots before mutation, writes and peer-hashes only the semantic banks, leaves diagnostic/uninitialized bytes untouched, then recaptures and verifies the semantic hash.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::LuxMoveCommandState
        });
        m.entries.push_back({
            "Breakable wall/barrier scalar state",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "Ghidra ALuxBattleStageActorManagerRollback_Partial +0x3A8/+0x3B8 lists; wall break state +0x468 and barrier endurance/hit count +0x424/+0x468 are canonicalized in stable type/ID order. Wall fade timer/rate +0x46C/+0x470 are restored for local presentation integrity but excluded from peer gameplay hashes.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::BreakableScalars
        });
        m.entries.push_back({
            "Gameplay-facing Lux camera director and published frame",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "RollbackBattleCameraSnapshot owns director transition/blend controls, published output, the frame-vector bank/yaw/mode globals, and every live component through the exact native serializer selected by vtable +0x100. The common 308-byte projection includes all +0xF0/+0xF8 inputs; Game/Great, PlayerWatch, Attention, and Stay/Free subtype payloads mirror their native serializers. PlayerWatch character identity is normalized to P1/P2 and rebound from the live round; transient subtype references are cleared as the native deserializers do. Director, component, vtable, writer, timer-root, and character identities are atomically preflighted, and an unknown writer fails capture closed.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::GameplayCameraState
        });
        m.entries.push_back({
            "Versioned floating-point simulation policy",
            0,
            0,
            0,
            RollbackCoverage::AuthenticatedLaunchContract,
            "Every Horse-owned complete SimulationLoop iteration installs the schema-bound MXCSR/x87 control policy on the owning thread, clears defined status flags, rejects nesting, and restores the caller environment on every exit.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::FloatingPointPolicy
        });
        m.entries.push_back({
            "Stage wind scheduler, graph, and emitter mutable state",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "RollbackStageWindSnapshot captures the Ghidra-verified root scheduler/force fields, six-word combined-wind RNG, normalized active callback bank, every reachable graph node's mutable state, fixed-pool allocation state, and the pointer-free IwWindEmitter +0x50..+0xA8 scalar/vector record. It reconstructs graph headers only from retained same-match identities and rejects unreachable allocated slots. The fixed pool is used only at the four verified wind-node allocation return sites. Peer canonicalization excludes process addresses, inactive stale callback slots, and the parallel-family +0xE0..+0x11F gap that has no native field access; it hashes the verified scalar/vector/matrix ranges for all four concrete vtables.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::StageWind
        });
        m.entries.push_back({
            "Canonical stage identity",
            0,
            0,
            0,
            RollbackCoverage::AuthenticatedLaunchContract,
            "The native registry-resolved packed stage ID is captured in the accepted round identity and BattleBaseline. Peers fail closed when this identity differs, independent of breakable layout.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::StageIdentity
        });
        m.entries.push_back({
            "Breakable wall/barrier presentation reconciliation",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "Confirmed snapshots resolve each actor against the accepted stage identity. Walls call SC6's signature-checked pure SetStageBreakableWallVisibilityFromState routine. Barriers apply the face/back/breaking visibility proven by HandleStageBreakableBarrierHit, without spawning particles or redispatching break events. Per-client queued/committed/discarded counters and a value-only digest are emitted.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::BreakablePresentation
        });
        m.entries.push_back({
            "Presentation object lifetime and thread affinity",
            0,
            0,
            0,
            RollbackCoverage::DynamicSnapshot,
            "Production preserves all 38 listener-hub broadcasts and their reverse-order subscriber traversal at the native source frame. Activation inventories all 41 ULuxBattleEventListenerHub collections from +0x30, resolves every weak target, enforces the Ghidra-proven empty collections 8, 9, 21, 32, 33, 36, and 37, and seals lifecycle-local round, phase, and character owner identities. RollbackPresentationSemanticSnapshot captures, peer-hashes, atomically preflights, restores, and recaptures the round and phase presentation latches plus both characters' animation sidecars. Read-only character queries in collections 27 and 28 remain immediate. Ten stateful character subscribers covering weapon setup, phase trace state, Soul Charge, color fade, break/attack reset, player visibility, weapon-node alpha, and material charge are suppressed during speculation and stored as pointer-free role-bound transactions; confirmed commit resolves the live actor under the sealed epoch and counts success only after the exact native trampoline returns. Lower audio, VFX, stage, visibility, skeletal-mesh, material, and camera terminals use stable value records, logical shadow identities where creation is deferred, rollback discard, bounded chronological commit, and fail-closed identity/commit accounting. ApplyLuxStageCutFromMoveEvent at 0x140540EA0 has exactly two UE-presentation producers outside the owned SimulationLoop; those paths pass through and any unexpected owned occurrence is rejected before mutation.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::PresentationDispatch
        });
        m.entries.push_back({
            "ALuxBattleFrameInputLog cache/cursors",
            0,
            0x394,
            0x4084,
            RollbackCoverage::DiagnosticOnly,
            "Stock-path diagnostic schema only. Production Horse UDP/Gekko never restores or injects the native InputLog cache. The fixed +0x3A0/+0x3A4 scheduling cursors are scoped around each complete native SimulationLoop call and restored immediately; they are not rollback snapshot state.",
            RollbackCanonicalPolicy::AllBytes,
            RollbackCoverageCapabilityId::DiagnosticInputLog
        });

        return m;
    }
}
