#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../HorseMod/horselib/RollbackSnapshot.hpp"

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
    manifest.epoch.battle_manager = 0x1000;
    manifest.epoch.input_log = 0x2000;
    manifest.epoch.chara[0] = 0x3000;
    manifest.epoch.chara[1] = 0x4000;
    manifest.epoch.round_number = 2;
    manifest.epoch.round_start_hash = 0x12345678;
    manifest.epoch.stage_context_hash = 0xABCDEF0123456789ull;
    manifest.epoch.presence = 0x03;
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
            snapshot_bad, manifest.epoch, "chara-epoch-missing"))
        return 1;

    Horse::RollbackLifecycleEpoch live_bad = manifest.epoch;
    live_bad.presence = 0x02;
    if (!expect_refusal(snapshot, live_bad, "live-epoch-not-active"))
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
    live_bad.round_number += 1;
    if (!expect_refusal(snapshot, live_bad, "round-number-epoch-mismatch"))
        return 1;

    live_bad = manifest.epoch;
    live_bad.round_start_hash ^= 0x10;
    if (!expect_refusal(snapshot, live_bad, "round-start-epoch-mismatch"))
        return 1;

    live_bad = manifest.epoch;
    live_bad.stage_context_hash ^= 0x10;
    if (!expect_refusal(snapshot, live_bad, "stage-context-epoch-mismatch"))
        return 1;

    std::printf("rollback snapshot self-test passed hash=0x%llX bytes=%u\n",
                static_cast<unsigned long long>(snapshot.hash),
                capture.copied_bytes);
    std::printf("rollback snapshot boundary refusals passed count=%u\n",
                boundary_refusals);
    return 0;
}
