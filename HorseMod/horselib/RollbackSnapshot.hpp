// ============================================================================
// Horse::RollbackSnapshot
//
// Manifest types for same-round rollback snapshots. This file owns only
// explicit byte-range copies. Engine-authored snapshots such as HgCpuDirect
// are represented as coverage evidence and executed through
// RollbackHgCpuSnapshot.hpp.
// ============================================================================

#pragma once

#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace Horse
{
    enum class RollbackCoverage : uint8_t
    {
        Unknown,
        PendingEvidence,
        HgCpuDirectCovered,
        ExplicitSnapshot,
        ExcludedWithEvidence,
    };

    struct RollbackManifestEntry
    {
        const char* name {""};
        uintptr_t address {0};
        uint32_t offset {0};
        uint32_t bytes {0};
        RollbackCoverage coverage {RollbackCoverage::Unknown};
        const char* evidence {""};
    };

    static inline bool rollback_manifest_entry_is_explicit_copy(
        const RollbackManifestEntry& e) noexcept
    {
        return e.coverage == RollbackCoverage::ExplicitSnapshot
            && e.address != 0 && e.bytes != 0;
    }

    struct RollbackLifecycleEpoch
    {
        uintptr_t battle_manager {0};
        uintptr_t input_log {0};
        std::array<uintptr_t, 2> chara {};
        uint32_t round_number {0};
        uint32_t round_start_hash {0};
        uint64_t stage_context_hash {0};
        uint8_t presence {0xFF};

        bool same_as(const RollbackLifecycleEpoch& other) const noexcept
        {
            return battle_manager == other.battle_manager
                && input_log == other.input_log
                && chara == other.chara
                && round_number == other.round_number
                && round_start_hash == other.round_start_hash
                && stage_context_hash == other.stage_context_hash
                && presence == other.presence;
        }
    };

    struct RollbackSnapshotManifest
    {
        uint32_t version {1};
        uint32_t max_rollback_frames {0};
        uintptr_t image_base {0};
        std::vector<RollbackManifestEntry> entries;
        RollbackLifecycleEpoch epoch {};

        uint64_t coverage_hash() const noexcept
        {
            RollbackHash h{};
            h.add_scalar(version);
            h.add_scalar(max_rollback_frames);
            h.add_scalar(image_base);
            h.add_scalar(epoch.battle_manager);
            h.add_scalar(epoch.input_log);
            h.add_scalar(epoch.chara[0]);
            h.add_scalar(epoch.chara[1]);
            h.add_scalar(epoch.round_number);
            h.add_scalar(epoch.round_start_hash);
            h.add_scalar(epoch.stage_context_hash);
            h.add_scalar(epoch.presence);
            for (const auto& e : entries)
            {
                h.add_scalar(e.address);
                h.add_scalar(e.offset);
                h.add_scalar(e.bytes);
                h.add_scalar(static_cast<uint8_t>(e.coverage));
            }
            return h.value;
        }
    };

    struct RollbackSnapshotRange
    {
        uint32_t manifest_index {0};
        uintptr_t address {0};
        uint32_t bytes_offset {0};
        uint32_t bytes {0};
        uint64_t hash {0};
    };

    struct RollbackSnapshotFrame
    {
        RollbackLifecycleEpoch epoch {};
        std::vector<uint8_t> bytes;
        std::vector<RollbackSnapshotRange> ranges;
        uint64_t hash {0};

        void clear()
        {
            epoch = {};
            bytes.clear();
            ranges.clear();
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
    };

    enum class RollbackEpochValidationStatus : uint8_t
    {
        Accepted,
        SnapshotEpochNotActive,
        LiveEpochNotActive,
        CharaEpochMissing,
        BattleManagerMismatch,
        InputLogMismatch,
        CharaMismatch,
        RoundNumberMismatch,
        RoundStartMismatch,
        StageContextMismatch,
        PresenceMismatch,
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
        h.add_scalar(frame.epoch.battle_manager);
        h.add_scalar(frame.epoch.input_log);
        h.add_scalar(frame.epoch.chara[0]);
        h.add_scalar(frame.epoch.chara[1]);
        h.add_scalar(frame.epoch.round_number);
        h.add_scalar(frame.epoch.round_start_hash);
        h.add_scalar(frame.epoch.stage_context_hash);
        h.add_scalar(frame.epoch.presence);
        for (const RollbackSnapshotRange& r : frame.ranges)
        {
            h.add_scalar(r.manifest_index);
            h.add_scalar(r.address);
            h.add_scalar(r.bytes_offset);
            h.add_scalar(r.bytes);
            h.add_scalar(r.hash);
        }
        h.add_bytes(frame.bytes.data(), frame.bytes.size());
        return h.value;
    }

    static inline RollbackEpochValidationReport
    ValidateRollbackLifecycleEpoch(
        const RollbackLifecycleEpoch& snapshot_epoch,
        const RollbackLifecycleEpoch& live_epoch) noexcept
    {
        RollbackEpochValidationReport out {};
        if (snapshot_epoch.presence != 0x03)
        {
            out.status = RollbackEpochValidationStatus::SnapshotEpochNotActive;
            out.failure = "snapshot-epoch-not-active";
            return out;
        }
        if (live_epoch.presence != 0x03)
        {
            out.status = RollbackEpochValidationStatus::LiveEpochNotActive;
            out.failure = "live-epoch-not-active";
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
        if (snapshot_epoch.round_number != live_epoch.round_number)
        {
            out.status = RollbackEpochValidationStatus::RoundNumberMismatch;
            out.failure = "round-number-epoch-mismatch";
            return out;
        }
        if (snapshot_epoch.round_start_hash != live_epoch.round_start_hash)
        {
            out.status = RollbackEpochValidationStatus::RoundStartMismatch;
            out.failure = "round-start-epoch-mismatch";
            return out;
        }
        if (snapshot_epoch.stage_context_hash
            != live_epoch.stage_context_hash)
        {
            out.status = RollbackEpochValidationStatus::StageContextMismatch;
            out.failure = "stage-context-epoch-mismatch";
            return out;
        }
        if (snapshot_epoch.presence != live_epoch.presence)
        {
            out.status = RollbackEpochValidationStatus::PresenceMismatch;
            out.failure = "presence-epoch-mismatch";
            return out;
        }

        out.ok = true;
        out.status = RollbackEpochValidationStatus::Accepted;
        out.failure = "ok";
        return out;
    }

    static inline RollbackSnapshotCopyReport CaptureRollbackSnapshotBytes(
        const RollbackSnapshotManifest& manifest,
        RollbackSnapshotFrame& out)
    {
        RollbackSnapshotCopyReport report{};
        report.failure = "ok";
        out.clear();
        out.epoch = manifest.epoch;

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

        out.bytes.resize(total_bytes);
        out.ranges.reserve(explicit_entries);

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
            out.ranges.push_back(r);

            cursor += static_cast<size_t>(e.bytes);
            ++report.copied_entries;
        }

        report.copied_bytes = static_cast<uint32_t>(cursor);
        out.hash = HashRollbackSnapshotFrame(out);
        report.hash = out.hash;
        report.ok = true;
        return report;
    }

    static inline RollbackSnapshotCopyReport RestoreRollbackSnapshotBytes(
        const RollbackSnapshotFrame& snapshot)
    {
        RollbackSnapshotCopyReport report{};
        report.failure = "ok";

        if (snapshot.ranges.empty())
        {
            report.failure = "no-ranges";
            return report;
        }

        for (const RollbackSnapshotRange& r : snapshot.ranges)
        {
            if (r.bytes_offset > snapshot.bytes.size()
                || static_cast<size_t>(r.bytes)
                    > snapshot.bytes.size() - r.bytes_offset)
            {
                report.failure = "range-out-of-bounds";
                report.failed_entry = r.manifest_index;
                report.failed_address = r.address;
                return report;
            }
            const uint8_t* src = snapshot.bytes.data() + r.bytes_offset;
            if (!SafeWriteBytes(reinterpret_cast<void*>(r.address), src, r.bytes))
            {
                report.failure = "write-fault";
                report.failed_entry = r.manifest_index;
                report.failed_address = r.address;
                return report;
            }
            ++report.copied_entries;
            report.copied_bytes += r.bytes;
        }

        report.hash = snapshot.hash;
        report.ok = true;
        return report;
    }

    static inline RollbackSnapshotCopyReport
    RestoreRollbackSnapshotBytesIfEpochMatches(
        const RollbackSnapshotFrame& snapshot,
        const RollbackLifecycleEpoch& live_epoch)
    {
        const RollbackEpochValidationReport epoch =
            ValidateRollbackLifecycleEpoch(snapshot.epoch, live_epoch);
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
            "g_dwLuxBattleLcgRngState",
            addr(0x14485EB28ull),
            0,
            4,
            RollbackCoverage::ExcludedWithEvidence,
            "Ghidra label uint; only LuxMoveVM_ApplyAIPaletteMode and wind/effect init paths consume it during rollback probes, so it is presentation/AI-side diagnostic state for PVP rollback."
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
            0x10,
            RollbackCoverage::ExplicitSnapshot,
            "Ghidra type int[2]; captured as 0x10 to preserve alignment/padding around per-player cursors."
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
            "Ghidra xrefs use two 0x60-byte command slots from this base; full struct still pending."
        });
        m.entries.push_back({
            "Mutable gameplay stage state",
            0,
            0,
            0,
            RollbackCoverage::PendingEvidence,
            "Barrier block, terrain/contact flags, breakable wall/barrier state, and stage boundary context require restore proof or exclusion."
        });
        m.entries.push_back({
            "ALuxBattleFrameInputLog cache/cursors",
            0,
            0x394,
            0x4084,
            RollbackCoverage::PendingEvidence,
            "Only required for stock cache-path tests; ownership trace must prove drain/prediction/read order."
        });

        return m;
    }
}
