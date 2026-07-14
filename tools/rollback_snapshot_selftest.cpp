#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../HorseMod/horselib/RollbackSnapshot.hpp"
#include "../HorseMod/horselib/RollbackSecondaryEventStack.hpp"
#include "../HorseMod/horselib/RollbackStageSnapshot.hpp"

#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    bool bytes_equal(const void* a, const void* b, size_t n)
    {
        return std::memcmp(a, b, n) == 0;
    }
}

int main()
{
    Horse::RollbackSecondaryEventStackHistory event_stack_a {};
    Horse::RollbackSecondaryEventStackHistory event_stack_b {};
    event_stack_a.ok = event_stack_b.ok = true;
    event_stack_a.chara[0] = 0x100000;
    event_stack_b.chara[0] = 0x900000;
    event_stack_a.table_header[0] = 0x110000;
    event_stack_b.table_header[0] = 0x910000;
    event_stack_a.event_headers[0] = 0x120000;
    event_stack_b.event_headers[0] = 0x920000;
    event_stack_a.event_payloads[0] = 0x130000;
    event_stack_b.event_payloads[0] = 0x930000;
    event_stack_a.header_count[0] = event_stack_b.header_count[0] = 2;
    event_stack_a.header_cursors[0][0] =
        event_stack_b.header_cursors[0][0] = 3;
    event_stack_a.header_cursors[0][1] =
        event_stack_b.header_cursors[0][1] = 5;
    event_stack_a.bytes[0][0] = event_stack_b.bytes[0][0] = 7;
    event_stack_a.bytes[0][0x10] = event_stack_b.bytes[0][0x10] = 9;
    event_stack_a.bytes[0][0x258] = event_stack_b.bytes[0][0x258] = 1;
    event_stack_a.bytes[0][0x25C] = event_stack_b.bytes[0][0x25C] = 4;
    const uint64_t local_chara_a = 0x100000;
    const uint64_t local_chara_b = 0x900000;
    std::memcpy(event_stack_a.bytes[0].data() + 0x08,
                &local_chara_a, sizeof(local_chara_a));
    std::memcpy(event_stack_b.bytes[0].data() + 0x08,
                &local_chara_b, sizeof(local_chara_b));
    const uint64_t event_canonical_a =
        Horse::RollbackHashSecondaryEventStackCanonical(event_stack_a);
    const uint64_t event_canonical_b =
        Horse::RollbackHashSecondaryEventStackCanonical(event_stack_b);
    if (event_canonical_a == 0 || event_canonical_a != event_canonical_b)
    {
        std::printf("secondary-event pointer normalization failed\n");
        return 1;
    }
    ++event_stack_b.bytes[0][0x25C];
    if (event_canonical_a
        == Horse::RollbackHashSecondaryEventStackCanonical(event_stack_b))
    {
        std::printf("secondary-event previous-variant sensitivity failed\n");
        return 1;
    }
    event_stack_b.bytes[0][0x25C] = event_stack_a.bytes[0][0x25C];
    ++event_stack_b.header_cursors[0][1];
    if (event_canonical_a
        == Horse::RollbackHashSecondaryEventStackCanonical(event_stack_b))
    {
        std::printf("secondary-event cursor sensitivity failed\n");
        return 1;
    }

    std::array<uint8_t, 0xC0> round_a {};
    std::array<uint8_t, 0xC0> round_b {};
    const uint32_t provider = 0x10203040;
    const uint32_t position[3] = {1, 2, 3};
    std::memcpy(round_a.data(), &provider, sizeof(provider));
    std::memcpy(round_b.data(), &provider, sizeof(provider));
    std::memcpy(round_a.data() + 0x20, position, sizeof(position));
    std::memcpy(round_b.data() + 0x20, position, sizeof(position));
    round_a[0x80] = 0x11;
    round_b[0x80] = 0xEE;
    if (Horse::RollbackHashRoundStartCanonical(
            round_a.data(), round_a.size())
            != Horse::RollbackHashRoundStartCanonical(
                round_b.data(), round_b.size()))
    {
        std::printf("round-start opaque exclusion failed\n");
        return 1;
    }
    ++round_b[0x20];
    if (Horse::RollbackHashRoundStartCanonical(
            round_a.data(), round_a.size())
            == Horse::RollbackHashRoundStartCanonical(
                round_b.data(), round_b.size()))
    {
        std::printf("round-start gameplay sensitivity failed\n");
        return 1;
    }

    Horse::RollbackSnapshotFrame command_a {};
    Horse::RollbackSnapshotFrame command_b {};
    command_a.schema_hash = command_b.schema_hash = 0xCC01;
    command_a.bytes.resize(0xC0);
    command_b.bytes.resize(0xC0);
    command_a.ranges.push_back({});
    command_b.ranges.push_back({});
    for (auto* frame : {&command_a, &command_b})
    {
        frame->ranges[0].bytes = 0xC0;
        frame->ranges[0].canonical_policy =
            Horse::RollbackCanonicalPolicy::LuxMoveSchedStateArray;
        const uint32_t selected = 1;
        const uint32_t move = 0x1234;
        std::memcpy(frame->bytes.data() + 0x08, &selected, sizeof(selected));
        std::memcpy(frame->bytes.data() + 0x30, &move, sizeof(move));
        std::memcpy(frame->bytes.data() + 0x68, &selected, sizeof(selected));
        std::memcpy(frame->bytes.data() + 0x90, &move, sizeof(move));
    }
    const uint64_t pointer_a = 0x1111111122222222ull;
    const uint64_t pointer_b = 0xAAAAAAAA55555555ull;
    std::memcpy(command_a.bytes.data() + 0x10,
                &pointer_a, sizeof(pointer_a));
    std::memcpy(command_b.bytes.data() + 0x10,
                &pointer_b, sizeof(pointer_b));
    if (Horse::HashRollbackSnapshotCanonical(command_a)
            != Horse::HashRollbackSnapshotCanonical(command_b))
    {
        std::printf("CCpu canonical pointer exclusion failed\n");
        return 1;
    }
    ++command_b.bytes[0x30];
    if (Horse::HashRollbackSnapshotCanonical(command_a)
            == Horse::HashRollbackSnapshotCanonical(command_b))
    {
        std::printf("CCpu canonical gameplay field sensitivity failed\n");
        return 1;
    }

    std::array<uint8_t, 16> range_a{};
    std::array<uint8_t, 32> range_b{};
    for (size_t i = 0; i < range_a.size(); ++i)
        range_a[i] = static_cast<uint8_t>(0x10 + i);
    for (size_t i = 0; i < range_b.size(); ++i)
        range_b[i] = static_cast<uint8_t>(0x80 + i);

    const auto original_a = range_a;
    const auto original_b = range_b;

    Horse::RollbackSnapshotManifest manifest{};
    manifest.version = 0x54455354;
    manifest.max_rollback_frames = 12;
    manifest.epoch.generation = 7;
    manifest.epoch.battle_manager = 0x1000;
    manifest.epoch.input_log = 0x2000;
    manifest.epoch.chara[0] = 0x3000;
    manifest.epoch.chara[1] = 0x4000;
    manifest.epoch.stage_actor_manager = 0x5000;
    manifest.epoch.round_start_digest = 0x1234567812345678ull;
    manifest.epoch.stage_layout_digest = 0xABCDEF0123456789ull;
    manifest.epoch.actor_set_digest = 0x5566778899AABBCCull;
    manifest.epoch.input_log_frame = 0xFFFFFFFEu;
    manifest.epoch.presence = 7;
    manifest.epoch.battle_main_state = 2;
    manifest.epoch.battle_status = 2;
    manifest.epoch.pvp_active = true;
    manifest.epoch.auto_advance_armed = false;
    manifest.epoch.valid = true;
    Horse::RollbackLifecycleEpoch mirrored_epoch = manifest.epoch;
    mirrored_epoch.presence = 5;
    mirrored_epoch.pvp_active = false;
    const bool mirrored_mode_gate =
        mirrored_epoch.active_for(
            Horse::RollbackLifecycleMode::MirroredVersus)
        && !mirrored_epoch.active_pvp()
        && Horse::ValidateRollbackLifecycleEpoch(
            mirrored_epoch,
            mirrored_epoch,
            Horse::RollbackLifecycleMode::MirroredVersus).ok
        && !Horse::ValidateRollbackLifecycleEpoch(
            mirrored_epoch, mirrored_epoch).ok;
    if (!mirrored_mode_gate)
    {
        std::printf("mirrored lifecycle mode gate failed\n");
        return 1;
    }
    manifest.entries.push_back({
        "test range a",
        reinterpret_cast<uintptr_t>(range_a.data()),
        0,
        static_cast<uint32_t>(range_a.size()),
        Horse::RollbackCoverage::ExplicitSnapshot,
        "self-test explicit range"
    });
    manifest.entries.push_back({
        "pending range",
        reinterpret_cast<uintptr_t>(range_b.data()),
        0,
        static_cast<uint32_t>(range_b.size()),
        Horse::RollbackCoverage::PendingEvidence,
        "self-test skipped range"
    });
    manifest.entries.push_back({
        "test range b tail",
        reinterpret_cast<uintptr_t>(range_b.data()),
        8,
        16,
        Horse::RollbackCoverage::ExplicitSnapshot,
        "self-test explicit offset range"
    });

    Horse::RollbackSnapshotFrame snapshot{};
    const auto capture =
        Horse::CaptureRollbackSnapshotBytes(manifest, snapshot);
    if (!capture.ok || capture.copied_entries != 2
        || capture.skipped_entries != 1 || capture.copied_bytes != 32)
    {
        std::printf("capture failed ok=%d entries=%u skipped=%u bytes=%u failure=%s\n",
                    capture.ok ? 1 : 0,
                    capture.copied_entries,
                    capture.skipped_entries,
                    capture.copied_bytes,
                    capture.failure);
        return 1;
    }

    range_a.fill(0xEE);
    for (size_t i = 8; i < 24; ++i)
        range_b[i] = 0xDD;

    const auto restore =
        Horse::RestoreRollbackSnapshotBytesIfEpochMatches(
            snapshot, manifest.epoch);
    if (!restore.ok || restore.copied_entries != 2
        || !bytes_equal(range_a.data(), original_a.data(), range_a.size())
        || !bytes_equal(range_b.data() + 8, original_b.data() + 8, 16))
    {
        std::printf("restore failed ok=%d entries=%u failure=%s\n",
                    restore.ok ? 1 : 0,
                    restore.copied_entries,
                    restore.failure);
        return 1;
    }

    Horse::RollbackSnapshotFrame recaptured{};
    const auto recapture =
        Horse::CaptureRollbackSnapshotBytes(manifest, recaptured);
    if (!recapture.ok || recaptured.hash != snapshot.hash)
    {
        std::printf("recapture mismatch ok=%d before=0x%llX after=0x%llX failure=%s\n",
                    recapture.ok ? 1 : 0,
                    static_cast<unsigned long long>(snapshot.hash),
                    static_cast<unsigned long long>(recaptured.hash),
                    recapture.failure);
        return 1;
    }

    uint32_t boundary_refusals = 0;
    auto expect_refusal =
        [&](const Horse::RollbackSnapshotFrame& candidate,
            const Horse::RollbackLifecycleEpoch& live_epoch,
            const char* expected_failure) -> bool {
            range_a.fill(0xAB);
            const auto refused =
                Horse::RestoreRollbackSnapshotBytesIfEpochMatches(
                    candidate, live_epoch);
            if (refused.ok
                || std::strcmp(refused.failure, expected_failure) != 0
                || range_a.front() != 0xAB
                || range_a.back() != 0xAB)
            {
                std::printf(
                    "boundary refusal failed ok=%d failure=%s expected=%s "
                    "first=0x%02X last=0x%02X\n",
                    refused.ok ? 1 : 0,
                    refused.failure ? refused.failure : "?",
                    expected_failure,
                    range_a.front(),
                    range_a.back());
                return false;
            }
            ++boundary_refusals;
            return true;
        };

    Horse::RollbackSnapshotFrame snapshot_bad = snapshot;
    snapshot_bad.epoch.presence = 0xFF;
    if (!expect_refusal(
            snapshot_bad, manifest.epoch, "snapshot-epoch-not-active"))
        return 1;

    snapshot_bad = snapshot;
    snapshot_bad.epoch.chara[1] = 0;
    if (!expect_refusal(
            snapshot_bad, manifest.epoch, "snapshot-epoch-not-active"))
        return 1;

    Horse::RollbackLifecycleEpoch live_bad = manifest.epoch;
    live_bad.presence = 6;
    live_bad.pvp_active = false;
    if (!expect_refusal(snapshot, live_bad, "live-epoch-not-active"))
        return 1;

    live_bad = manifest.epoch;
    live_bad.generation += 1;
    if (!expect_refusal(snapshot, live_bad, "lifecycle-generation-mismatch"))
        return 1;

    live_bad = manifest.epoch;
    live_bad.battle_manager ^= 0x10;
    if (!expect_refusal(
            snapshot, live_bad, "battle-manager-epoch-mismatch"))
        return 1;

    live_bad = manifest.epoch;
    live_bad.chara[0] ^= 0x10;
    if (!expect_refusal(snapshot, live_bad, "chara-epoch-mismatch"))
        return 1;

    live_bad = manifest.epoch;
    live_bad.input_log ^= 0x10;
    if (!expect_refusal(snapshot, live_bad, "input-log-epoch-mismatch"))
        return 1;

    live_bad = manifest.epoch;
    live_bad.stage_actor_manager ^= 0x10;
    if (!expect_refusal(
            snapshot, live_bad, "stage-actor-manager-epoch-mismatch"))
        return 1;

    live_bad = manifest.epoch;
    live_bad.round_start_digest ^= 0x10;
    if (!expect_refusal(snapshot, live_bad, "round-start-epoch-mismatch"))
        return 1;

    live_bad = manifest.epoch;
    live_bad.stage_layout_digest ^= 0x10;
    if (!expect_refusal(snapshot, live_bad, "stage-layout-epoch-mismatch"))
        return 1;

    live_bad = manifest.epoch;
    live_bad.actor_set_digest ^= 0x10;
    if (!expect_refusal(snapshot, live_bad, "actor-set-epoch-mismatch"))
        return 1;

    Horse::RollbackSnapshotFrame corrupted = snapshot;
    corrupted.bytes[0] ^= 0x80;
    if (!expect_refusal(
            corrupted, manifest.epoch, "snapshot-integrity-mismatch"))
        return 1;

    Horse::RollbackSnapshotManifest relocated = manifest;
    relocated.image_base ^= 0x10000000;
    for (auto& entry : relocated.entries)
        entry.address += 0x100000;
    if (relocated.schema_hash() != manifest.schema_hash())
    {
        std::printf("schema hash changed across relocation\n");
        return 1;
    }

    Horse::RollbackSnapshotManifest overlap = manifest;
    overlap.entries.push_back({
        "overlap",
        reinterpret_cast<uintptr_t>(range_a.data()),
        4,
        8,
        Horse::RollbackCoverage::ExplicitSnapshot,
        "self-test overlap"});
    const auto overlap_report =
        Horse::ValidateRollbackSnapshotManifest(overlap, false);
    const auto pending_report =
        Horse::ValidateRollbackSnapshotManifest(manifest, true);
    if (overlap_report.ok || overlap_report.overlapping_entries == 0
        || pending_report.ok
        || pending_report.pending_gameplay_entries != 1)
    {
        std::printf("manifest validation failed overlap=%u pending=%u\n",
                    overlap_report.overlapping_entries,
                    pending_report.pending_gameplay_entries);
        return 1;
    }

    const Horse::RollbackSnapshotManifest production_manifest =
        Horse::BuildInitialRollbackManifest(0x140000000ull, 60);
    const Horse::RollbackSnapshotManifest default_window_manifest =
        Horse::BuildInitialRollbackManifest(0x140000000ull, 12);
    const auto production_validation =
        Horse::ValidateRollbackSnapshotManifest(
            production_manifest, true);
    Horse::RollbackSnapshotManifest changed_abi = production_manifest;
    ++changed_abi.version;
    const bool production_gate = production_validation.live_ready
        && production_validation.pending_gameplay_entries == 0;
    const bool abi_bound = changed_abi.schema_hash()
        != production_manifest.schema_hash();
    const Horse::RollbackWallPresentationVisibility intact =
        Horse::ComputeRollbackWallPresentationVisibility(0, 1.0f, 0.0f);
    const Horse::RollbackWallPresentationVisibility breaking =
        Horse::ComputeRollbackWallPresentationVisibility(1, 0.5f, -0.1f);
    const Horse::RollbackWallPresentationVisibility broken =
        Horse::ComputeRollbackWallPresentationVisibility(2, 1.5f, 0.0f);
    const Horse::RollbackWallPresentationVisibility invalid =
        Horse::ComputeRollbackWallPresentationVisibility(3, 1.0f, 0.0f);
    Horse::RollbackBreakableStageSnapshot stage_a {};
    stage_a.stage_actor_manager = 0x1000;
    stage_a.records.push_back({
        Horse::RollbackBreakableActorKind::Wall,
        7,
        0,
        0x2000,
        1,
        0,
        0.25f,
        -0.1f,
    });
    Horse::HashRollbackBreakableStageSnapshot(stage_a);
    Horse::RollbackBreakableStageSnapshot stage_b = stage_a;
    stage_b.records[0].actor = 0x3000;
    stage_b.records[0].fade_timer = 0.75f;
    stage_b.records[0].fade_rate = 0.0f;
    Horse::HashRollbackBreakableStageSnapshot(stage_b);
    const bool presentation_contract =
        intact.valid && intact.opaque_visible
        && intact.opaque_offset
            == Horse::kRollbackStageWallIntactOpaqueOffset
        && breaking.valid && !breaking.opaque_visible
        && breaking.opaque_offset
            == Horse::kRollbackStageWallBreakingOpaqueOffset
        && broken.valid && broken.opaque_visible
        && broken.opaque_offset
            == Horse::kRollbackStageWallBrokenOpaqueOffset
        && !invalid.valid
        && stage_a.canonical_hash == stage_b.canonical_hash
        && stage_a.integrity_hash != stage_b.integrity_hash;
    if (!production_gate || !abi_bound || !presentation_contract)
    {
        std::printf(
            "production gate/ABI/presentation failed "
            "live=%d pending=%u abi=%d presentation=%d\n",
            production_validation.live_ready ? 1 : 0,
            production_validation.pending_gameplay_entries,
            abi_bound ? 1 : 0,
            presentation_contract ? 1 : 0);
        return 1;
    }

    std::printf("rollback snapshot self-test passed hash=0x%llX bytes=%u\n",
                static_cast<unsigned long long>(snapshot.hash),
                capture.copied_bytes);
    std::printf("rollback snapshot boundary refusals passed count=%u\n",
                boundary_refusals);
    std::printf(
        "rollback production manifest gate passed pending=%u abi_bound=%d "
        "schema_window12=0x%llX schema_window60=0x%llX\n",
        production_validation.pending_gameplay_entries,
        abi_bound ? 1 : 0,
        static_cast<unsigned long long>(default_window_manifest.schema_hash()),
        static_cast<unsigned long long>(production_manifest.schema_hash()));
    return 0;
}
