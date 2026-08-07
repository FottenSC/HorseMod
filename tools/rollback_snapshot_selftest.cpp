#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../HorseMod/horselib/RollbackSnapshot.hpp"
#include "../HorseMod/horselib/RollbackHgCpuCanonical.hpp"
#define HORSE_ROLLBACK_HGCPU_PURE_STATE_ONLY 1
#include "../HorseMod/horselib/RollbackHgCpuSnapshot.hpp"
#undef HORSE_ROLLBACK_HGCPU_PURE_STATE_ONLY
#include "../HorseMod/horselib/RollbackMotionBankCanonical.hpp"
#include "../HorseMod/horselib/RollbackMotionBankAuthority.hpp"
#include "../HorseMod/horselib/RollbackObserverCaptureTransaction.hpp"
#include "../HorseMod/horselib/RollbackAiPaletteDiagnostics.hpp"
#include "../HorseMod/horselib/RollbackPaletteVariantSnapshot.hpp"
#include "../HorseMod/horselib/RollbackCarriedStateTransaction.hpp"
#include "../HorseMod/horselib/RollbackSecondaryEventStack.hpp"
#include "../HorseMod/horselib/RollbackSecondaryEventAuthority.hpp"
#include "../HorseMod/horselib/RollbackStageSnapshot.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace
{
    struct MockMotionBankHistory
    {
        bool ok {false};
        int current_slot[Horse::kRollbackMotionBankPlayerCount]
            [Horse::kRollbackMotionBankCount] {};
        int provider_slot[Horse::kRollbackMotionBankPlayerCount]
            [Horse::kRollbackMotionBankCount] {};
        std::vector<uint8_t> bytes;
    };

    bool bytes_equal(const void* a, const void* b, size_t n)
    {
        return std::memcmp(a, b, n) == 0;
    }
}

int main()
{
    static_assert(Horse::kRollbackPrimaryMotionBankBytes
        / Horse::kRollbackMotionBankMatrixBytes
        == static_cast<size_t>(
            Horse::kRollbackPrimaryMotionBankMatrixCapacity));
    if (Horse::RollbackPrimaryMotionBankCountAdmitted(0)
        || !Horse::RollbackPrimaryMotionBankCountAdmitted(1)
        || !Horse::RollbackPrimaryMotionBankCountAdmitted(768)
        || Horse::RollbackPrimaryMotionBankCountAdmitted(769))
    {
        std::printf("primary matrix admission contract failed\n");
        return 1;
    }
    MockMotionBankHistory timeline_role {};
    timeline_role.ok = true;
    timeline_role.current_slot[0][0] = 2;
    timeline_role.provider_slot[0][0] = 1;
    if (Horse::RollbackMotionBankTimelineFrameSlot(
            timeline_role, 0, 0) != 2)
    {
        std::printf("motion-bank timeline slot role failed\n");
        return 1;
    }

    if (!Horse::RollbackTimerActionManagerAliasValid(
            0x140001000, 0x140001000)
        || Horse::RollbackTimerActionManagerAliasValid(
            0x140001000, 0x140002000)
        || Horse::RollbackTimerActionManagerAliasValid(0, 0))
    {
        std::printf("timer/action-manager alias contract failed\n");
        return 1;
    }

    {
        std::array<uint8_t, 0x5C> palette_bytes {};
        const std::array<uintptr_t, 3> pointers {
            0x140100000ull, 0x140200000ull, 0x140300000ull,
        };
        Horse::RollbackAiPaletteScalars scalars {};
        scalars.current_mode = 3;
        scalars.ai_mode = 2;
        scalars.phase = 0.25f;
        scalars.phase_velocity = 0.01f;
        scalars.phase_scale = 1.0f;
        scalars.random_threshold = 0.75f;
        scalars.publish_tick_count = 105;
        std::memcpy(palette_bytes.data(), pointers.data(), sizeof(pointers));
        std::memcpy(
            palette_bytes.data() + 0x18, &scalars, sizeof(scalars));
        const auto baseline =
            Horse::CaptureRollbackAiPaletteDiagnosticsFromLayout(
                palette_bytes.data(), 0x140000000ull);
        scalars.phase += 0.01f;
        std::memcpy(
            palette_bytes.data() + 0x18, &scalars, sizeof(scalars));
        const auto changed =
            Horse::CaptureRollbackAiPaletteDiagnosticsFromLayout(
                palette_bytes.data(), 0x140000000ull);
        const bool restored =
            Horse::RestoreRollbackAiPaletteDiagnosticsToLayout(
                palette_bytes.data(), 0x140000000ull, baseline);
        const auto after_restore =
            Horse::CaptureRollbackAiPaletteDiagnosticsFromLayout(
                palette_bytes.data(), 0x140000000ull);
        if (!baseline.readable || !changed.readable
            || baseline.scalar_hash == changed.scalar_hash
            || !restored
            || after_restore.scalar_hash != baseline.scalar_hash
            || baseline.active_motion_bank_rva != 0x100000
            || baseline.key_buffer_rva != 0x200000
            || baseline.bone_data_bank_rva != 0x300000
            || baseline.scalars.ai_mode != 2
            || baseline.scalars.publish_tick_count != 105)
        {
            std::printf("AI palette diagnostic capture contract failed\n");
            return 1;
        }
    }

    {
        uint32_t live_lcg = 0x121A1703u;
        const uint32_t captured_lcg = live_lcg;
        unsigned cleanup_calls = 0;
        const auto repaired =
            Horse::RunRollbackObserverCaptureTransaction(
                [&]() noexcept {
                    live_lcg = 0x62A9E6A7u;
                    return true;
                },
                [&]() noexcept {
                    ++cleanup_calls;
                    live_lcg = captured_lcg;
                    return true;
                });
        if (!repaired.ok || !repaired.observer_ok || !repaired.cleanup_ok
            || cleanup_calls != 1 || live_lcg != captured_lcg)
        {
            std::printf(
                "observer cleanup did not repair LCG mutation "
                "ok=%d observer=%d cleanup=%d calls=%u lcg=0x%08X\n",
                repaired.ok ? 1 : 0,
                repaired.observer_ok ? 1 : 0,
                repaired.cleanup_ok ? 1 : 0,
                cleanup_calls,
                live_lcg);
            return 1;
        }

        cleanup_calls = 0;
        live_lcg = captured_lcg;
        const auto failed_observer =
            Horse::RunRollbackObserverCaptureTransaction(
                [&]() noexcept {
                    live_lcg = 0x62A9E6A7u;
                    return false;
                },
                [&]() noexcept {
                    ++cleanup_calls;
                    live_lcg = captured_lcg;
                    return true;
                });
        if (failed_observer.ok || failed_observer.observer_ok
            || !failed_observer.cleanup_ok || cleanup_calls != 1
            || live_lcg != captured_lcg)
        {
            std::printf(
                "observer failure skipped mandatory cleanup "
                "ok=%d observer=%d cleanup=%d calls=%u lcg=0x%08X\n",
                failed_observer.ok ? 1 : 0,
                failed_observer.observer_ok ? 1 : 0,
                failed_observer.cleanup_ok ? 1 : 0,
                cleanup_calls,
                live_lcg);
            return 1;
        }

        const auto failed_cleanup =
            Horse::RunRollbackObserverCaptureTransaction(
                []() noexcept { return true; },
                []() noexcept { return false; });
        if (failed_cleanup.ok || !failed_cleanup.observer_ok
            || failed_cleanup.cleanup_ok)
        {
            std::printf(
                "observer transaction accepted failed cleanup "
                "ok=%d observer=%d cleanup=%d\n",
                failed_cleanup.ok ? 1 : 0,
                failed_cleanup.observer_ok ? 1 : 0,
                failed_cleanup.cleanup_ok ? 1 : 0);
            return 1;
        }
    }

    {
        constexpr uintptr_t mock_vtable = 0x123456789ABCDEF0ull;
        std::array<int32_t,
            Horse::kRollbackPaletteVariantSlotCount> states {
                -1, 0, 2, -1};
        std::vector<uint8_t> session(
            Horse::kRollbackPaletteVariantFirstBufferOffset
            + Horse::kRollbackPaletteVariantSlotCount
                * Horse::kRollbackPaletteVariantObjectBytes);
        const auto object = [&](size_t slot) noexcept {
            return reinterpret_cast<uintptr_t>(session.data())
                + Horse::kRollbackPaletteVariantFirstBufferOffset
                + slot * Horse::kRollbackPaletteVariantObjectBytes;
        };
        for (size_t slot = 0;
             slot < Horse::kRollbackPaletteVariantSlotCount; ++slot)
        {
            std::memcpy(reinterpret_cast<void*>(object(slot)),
                &mock_vtable, sizeof(mock_vtable));
        }
        Horse::RollbackHgCpuSnapshotFrame semantic_layout {};
        semantic_layout.khit_topology_ok = true;
        semantic_layout.khit_topology[0].ok = true;
        semantic_layout.khit_topology[1].ok = true;
        const auto add_khit_node = [](
            Horse::RollbackHgCpuSnapshotFrame& layout,
            size_t player,
            uint8_t list,
            uint16_t index,
            uint8_t tag,
            size_t writer_bytes) {
            Horse::RollbackHgCpuSnapshotFrame::KHitNodeImage node {};
            node.list_index = list;
            node.node_index = index;
            node.writer_tag = tag;
            node.writer_bytes = writer_bytes;
            layout.khit_topology[player].nodes.push_back(node);
            layout.khit_topology[player].node_stream_bytes += writer_bytes;
        };
        add_khit_node(semantic_layout, 0, 0, 0, 0, 0x26);
        add_khit_node(semantic_layout, 0, 1, 4, 1, 0x42);
        add_khit_node(semantic_layout, 1, 2, 7, 2, 0x32);
        const uint64_t serialized_bytes =
            Horse::RollbackHgCpuCharaRecordBytes(&semantic_layout, 0)
            + Horse::RollbackHgCpuCharaRecordBytes(&semantic_layout, 1);
        constexpr uint64_t native_tail_bytes = 0x180;
        const uint64_t write1 = serialized_bytes + native_tail_bytes;
        const uint64_t read1 = 7;
        const uint64_t write2 = serialized_bytes + native_tail_bytes;
        const uint64_t read2 = 11;
        std::memcpy(reinterpret_cast<void*>(object(1) + 8),
            &write1, sizeof(write1));
        std::memcpy(reinterpret_cast<void*>(object(1) + 0x10),
            &read1, sizeof(read1));
        std::memcpy(reinterpret_cast<void*>(object(2) + 8),
            &write2, sizeof(write2));
        std::memcpy(reinterpret_cast<void*>(object(2) + 0x10),
            &read2, sizeof(read2));
        std::memset(reinterpret_cast<void*>(object(1)
                + Horse::kRollbackPaletteVariantPayloadOffset),
            0x41, static_cast<size_t>(write1));
        std::memset(reinterpret_cast<void*>(object(2)
                + Horse::kRollbackPaletteVariantPayloadOffset),
            0x82, static_cast<size_t>(write2));

        Horse::RollbackPaletteVariantWriterRegistry writer_registry {};
        Horse::PrepareRollbackPaletteVariantStorage(
            writer_registry, false);
        if (Horse::ObserveRollbackPaletteVariantWriterFromLayout(
                object(1), reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, semantic_layout, writer_registry)
                != Horse::RollbackPaletteVariantWriterObservation::Observed
            || Horse::ObserveRollbackPaletteVariantWriterFromLayout(
                object(2), reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, semantic_layout, writer_registry)
                != Horse::RollbackPaletteVariantWriterObservation::Observed)
        {
            std::printf("palette writer observation fixture failed\n");
            return 1;
        }
        Horse::RollbackPaletteVariantSnapshot palette {};
        const auto captured =
            Horse::CaptureRollbackPaletteVariantSnapshotFromLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, palette, writer_registry, false);
        const auto* palette_storage = palette.payload.data();
        const size_t palette_capacity = palette.payload.capacity();
        states = {9, 9, 9, 9};
        const uint64_t changed_write = 1;
        const uint64_t changed_read = 1;
        std::memcpy(reinterpret_cast<void*>(object(1) + 8),
            &changed_write, sizeof(changed_write));
        std::memcpy(reinterpret_cast<void*>(object(1) + 0x10),
            &changed_read, sizeof(changed_read));
        std::memset(reinterpret_cast<void*>(object(1)
                + Horse::kRollbackPaletteVariantPayloadOffset),
            0xCC, static_cast<size_t>(write1));
        const auto restored =
            Horse::RestoreRollbackPaletteVariantSnapshotToLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, palette, writer_registry);
        Horse::RollbackPaletteVariantSnapshot verified {};
        Horse::PrepareRollbackPaletteVariantStorage(verified, false);
        const auto* verified_storage = verified.payload.data();
        const size_t verified_capacity = verified.payload.capacity();
        const auto verified_capture =
            Horse::CaptureRollbackPaletteVariantSnapshotFromLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, verified, writer_registry, true);
        uint64_t restored_write = 0;
        uint64_t restored_read = 0;
        std::memcpy(&restored_write,
            reinterpret_cast<const void*>(object(1) + 8),
            sizeof(restored_write));
        std::memcpy(&restored_read,
            reinterpret_cast<const void*>(object(1) + 0x10),
            sizeof(restored_read));
        if (!captured.ok || captured.active_mask != 0x06
            || captured.copied_bytes != write1 + write2
            || !restored.ok || !verified_capture.ok
            || states[0] != -1 || states[1] != 0
            || states[2] != 2 || states[3] != -1
            || palette.integrity_hash != verified.integrity_hash
            || palette.canonical_hash != verified.canonical_hash
            || restored_write != write1 || restored_read != read1
            || palette.payload.data() != palette_storage
            || palette.payload.capacity() != palette_capacity
            || verified.payload.data() != verified_storage
            || verified.payload.capacity() != verified_capacity
            || !palette.slots[1].writer_layout.valid
            || !palette.slots[2].writer_layout.valid
            || palette.slots[1].writer_layout.node_count[0] != 2
            || palette.slots[1].writer_layout.node_count[1] != 1
            || writer_registry.producer_pending[1]
            || writer_registry.producer_pending[2])
        {
            std::printf("palette variant round-trip failed\n");
            return 1;
        }

        Horse::RollbackPaletteVariantWriterRegistry duplicate_registry {};
        Horse::PrepareRollbackPaletteVariantStorage(
            duplicate_registry, false);
        const auto non_palette_observation =
            Horse::ObserveRollbackPaletteVariantWriterFromLayout(
                object(1) + 0x40,
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, semantic_layout, duplicate_registry);
        std::array<int32_t,
            Horse::kRollbackPaletteVariantSlotCount> duplicate_states {
                -1, 0, -1, -1};
        if (non_palette_observation
                != Horse::RollbackPaletteVariantWriterObservation::
                    NotPaletteBuffer
            || duplicate_registry.next_producer_serial != 0
            || Horse::ObserveRollbackPaletteVariantWriterFromLayout(
                object(1), reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, semantic_layout, duplicate_registry)
                != Horse::RollbackPaletteVariantWriterObservation::Observed)
        {
            std::printf("palette writer producer filter failed\n");
            return 1;
        }
        Horse::RollbackPaletteVariantSnapshot duplicate_first {};
        Horse::PrepareRollbackPaletteVariantStorage(
            duplicate_first, false);
        const auto duplicate_first_capture =
            Horse::CaptureRollbackPaletteVariantSnapshotFromLayout(
                duplicate_states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, duplicate_first, duplicate_registry, true);
        const uint64_t duplicate_first_serial =
            duplicate_first.slots[1].writer_layout.producer_serial;
        if (!duplicate_first_capture.ok
            || duplicate_first_serial == 0
            || duplicate_registry.producer_pending[1]
            || Horse::ObserveRollbackPaletteVariantWriterFromLayout(
                object(1), reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, semantic_layout, duplicate_registry)
                != Horse::RollbackPaletteVariantWriterObservation::Observed)
        {
            std::printf("palette writer first producer token failed\n");
            return 1;
        }
        Horse::RollbackPaletteVariantSnapshot duplicate_second {};
        Horse::PrepareRollbackPaletteVariantStorage(
            duplicate_second, false);
        const auto duplicate_second_capture =
            Horse::CaptureRollbackPaletteVariantSnapshotFromLayout(
                duplicate_states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, duplicate_second, duplicate_registry, true);
        if (!duplicate_second_capture.ok
            || duplicate_second.canonical_hash
                != duplicate_first.canonical_hash
            || duplicate_second.slots[1].writer_layout.producer_serial
                <= duplicate_first_serial
            || duplicate_second.integrity_hash
                == duplicate_first.integrity_hash
            || duplicate_registry.producer_pending[1])
        {
            std::printf("palette writer duplicate producer token failed\n");
            return 1;
        }
        if (!Horse::RestoreRollbackPaletteVariantSnapshotToLayout(
                duplicate_states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, duplicate_first, duplicate_registry).ok
            || duplicate_registry.slots[1].producer_serial
                != duplicate_first_serial
            || duplicate_registry.producer_pending[1])
        {
            std::printf("palette writer producer replay failed\n");
            return 1;
        }

        Horse::RollbackPaletteVariantSnapshot missing_preallocation {};
        missing_preallocation.payload.reserve(
            Horse::kRollbackPaletteVariantTotalPayloadBytes);
        Horse::RollbackPaletteVariantWriterRegistry
            missing_preallocation_registry {};
        const auto* missing_storage =
            missing_preallocation.payload.data();
        const size_t missing_capacity =
            missing_preallocation.payload.capacity();
        const auto preallocation_rejected =
            Horse::CaptureRollbackPaletteVariantSnapshotFromLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable,
                missing_preallocation, missing_preallocation_registry,
                true);
        if (preallocation_rejected.ok
            || missing_preallocation.payload.data() != missing_storage
            || missing_preallocation.payload.capacity() != missing_capacity
            || !missing_preallocation.payload.empty())
        {
            std::printf("palette variant preallocation gate failed\n");
            return 1;
        }

        Horse::RollbackPaletteVariantSnapshot semantic_changed {};
        Horse::PrepareRollbackPaletteVariantStorage(
            semantic_changed, false);
        uint8_t* live_slot1 = reinterpret_cast<uint8_t*>(
            object(1) + Horse::kRollbackPaletteVariantPayloadOffset);
        live_slot1[0x100] ^= 1;
        if (Horse::ObserveRollbackPaletteVariantWriterFromLayout(
                object(1), reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, semantic_layout, writer_registry)
            != Horse::RollbackPaletteVariantWriterObservation::Observed)
        {
            std::printf("palette semantic writer observation failed\n");
            return 1;
        }
        const auto semantic_changed_capture =
            Horse::CaptureRollbackPaletteVariantSnapshotFromLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, semantic_changed,
                writer_registry, true);
        live_slot1[0x100] ^= 1;
        Horse::RollbackPaletteVariantSnapshot pointer_changed {};
        Horse::PrepareRollbackPaletteVariantStorage(
            pointer_changed, false);
        live_slot1[0x260] ^= 1;
        if (Horse::ObserveRollbackPaletteVariantWriterFromLayout(
                object(1), reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, semantic_layout, writer_registry)
            != Horse::RollbackPaletteVariantWriterObservation::Observed)
        {
            std::printf("palette pointer writer observation failed\n");
            return 1;
        }
        const auto pointer_changed_capture =
            Horse::CaptureRollbackPaletteVariantSnapshotFromLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, pointer_changed,
                writer_registry, true);
        live_slot1[0x260] ^= 1;
        Horse::RollbackPaletteVariantSnapshot cursor_changed = palette;
        ++cursor_changed.slots[1].read_cursor;
        const uint64_t cursor_changed_canonical =
            Horse::HashRollbackPaletteVariantCanonical(cursor_changed);
        Horse::RollbackPaletteVariantSnapshot tail_changed {};
        Horse::PrepareRollbackPaletteVariantStorage(
            tail_changed, false);
        live_slot1[static_cast<size_t>(serialized_bytes) + 3] ^= 1;
        if (Horse::ObserveRollbackPaletteVariantWriterFromLayout(
                object(1), reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, semantic_layout, writer_registry)
            != Horse::RollbackPaletteVariantWriterObservation::Observed)
        {
            std::printf("palette native tail writer observation failed\n");
            return 1;
        }
        const auto tail_changed_capture =
            Horse::CaptureRollbackPaletteVariantSnapshotFromLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, tail_changed, writer_registry, true);
        live_slot1[static_cast<size_t>(serialized_bytes) + 3] ^= 1;
        if (!semantic_changed_capture.ok
            || semantic_changed.canonical_hash == palette.canonical_hash
            || !pointer_changed_capture.ok
            || pointer_changed.integrity_hash == palette.integrity_hash
            || pointer_changed.canonical_hash != palette.canonical_hash
            || !tail_changed_capture.ok
            || tail_changed.integrity_hash == palette.integrity_hash
            || tail_changed.canonical_hash == palette.canonical_hash
            || cursor_changed_canonical == palette.canonical_hash)
        {
            std::printf("palette variant semantic hash policy failed\n");
            return 1;
        }

        // A retained native checkpoint must keep the topology from the frame
        // that authored it, even when the current KHit layout has the same
        // total byte count but a different P1/P2 split and node order.
        if (!Horse::RestoreRollbackPaletteVariantSnapshotToLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, palette, writer_registry).ok)
        {
            std::printf("palette writer registry restore failed\n");
            return 1;
        }
        if (writer_registry.producer_pending[1]
            || writer_registry.producer_pending[2]
            || writer_registry.slots[1].producer_serial
                != palette.slots[1].writer_layout.producer_serial)
        {
            std::printf("palette restored producer token invalid\n");
            return 1;
        }
        Horse::RollbackHgCpuSnapshotFrame changed_current_layout {};
        changed_current_layout.khit_topology_ok = true;
        changed_current_layout.khit_topology[0].ok = true;
        changed_current_layout.khit_topology[1].ok = true;
        add_khit_node(changed_current_layout, 0, 2, 3, 2, 0x32);
        add_khit_node(changed_current_layout, 1, 0, 9, 0, 0x26);
        add_khit_node(changed_current_layout, 1, 1, 5, 1, 0x42);
        if (Horse::RollbackHgCpuCharaRecordBytes(
                &changed_current_layout, 0)
                + Horse::RollbackHgCpuCharaRecordBytes(
                    &changed_current_layout, 1)
            != serialized_bytes)
        {
            std::printf("palette same-total layout fixture invalid\n");
            return 1;
        }
        Horse::RollbackPaletteVariantSnapshot retained {};
        Horse::PrepareRollbackPaletteVariantStorage(retained, false);
        const auto retained_capture =
            Horse::CaptureRollbackPaletteVariantSnapshotFromLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, retained, writer_registry, true);
        if (!retained_capture.ok
            || retained.canonical_hash != palette.canonical_hash
            || retained.slots[1].writer_layout.descriptor_hash
                != palette.slots[1].writer_layout.descriptor_hash)
        {
            std::printf("palette retained writer layout was replaced\n");
            return 1;
        }

        Horse::RollbackPaletteVariantWriterRegistry missing_registry {};
        Horse::PrepareRollbackPaletteVariantStorage(
            missing_registry, false);
        Horse::RollbackPaletteVariantSnapshot missing_layout {};
        Horse::PrepareRollbackPaletteVariantStorage(missing_layout, false);
        const auto missing_layout_rejected =
            Horse::CaptureRollbackPaletteVariantSnapshotFromLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, missing_layout, missing_registry, true);
        if (missing_layout_rejected.ok
            || std::strcmp(missing_layout_rejected.failure,
                "palette-variant-writer-layout-missing") != 0)
        {
            std::printf("palette missing writer layout accepted\n");
            return 1;
        }

        Horse::RollbackPaletteVariantWriterRegistry ambiguous_registry {};
        Horse::PrepareRollbackPaletteVariantStorage(
            ambiguous_registry, false);
        Horse::RollbackPaletteVariantSnapshot ambiguous {};
        Horse::PrepareRollbackPaletteVariantStorage(ambiguous, false);
        if (Horse::ObserveRollbackPaletteVariantWriterFromLayout(
                object(1), reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, changed_current_layout, ambiguous_registry)
                != Horse::RollbackPaletteVariantWriterObservation::Observed
            || Horse::ObserveRollbackPaletteVariantWriterFromLayout(
                object(2), reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, changed_current_layout, ambiguous_registry)
                != Horse::RollbackPaletteVariantWriterObservation::Observed)
        {
            std::printf("palette ambiguous writer observation failed\n");
            return 1;
        }
        const auto ambiguous_capture =
            Horse::CaptureRollbackPaletteVariantSnapshotFromLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, ambiguous,
                ambiguous_registry, true);
        if (!ambiguous_capture.ok
            || ambiguous.slots[1].writer_layout.descriptor_hash
                == palette.slots[1].writer_layout.descriptor_hash
            || ambiguous_registry.producer_pending[1]
            || ambiguous_registry.producer_pending[2])
        {
            std::printf("palette same-total layout ambiguity missed\n");
            return 1;
        }

        const size_t node_byte =
            Horse::kRollbackHgCpuHitAreaLocalStart
            + Horse::kRollbackHgCpuHitAreaFixedBytes;
        live_slot1[node_byte] ^= 0x20;
        Horse::RollbackPaletteVariantSnapshot stale {};
        Horse::PrepareRollbackPaletteVariantStorage(stale, false);
        const auto stale_rejected =
            Horse::CaptureRollbackPaletteVariantSnapshotFromLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, stale, writer_registry, true);
        Horse::RollbackPaletteVariantSnapshot node_changed {};
        Horse::PrepareRollbackPaletteVariantStorage(node_changed, false);
        if (Horse::ObserveRollbackPaletteVariantWriterFromLayout(
                object(1), reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, semantic_layout, writer_registry)
            != Horse::RollbackPaletteVariantWriterObservation::Observed)
        {
            std::printf("palette node writer observation failed\n");
            return 1;
        }
        const auto node_changed_capture =
            Horse::CaptureRollbackPaletteVariantSnapshotFromLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, node_changed,
                writer_registry, true);
        live_slot1[node_byte] ^= 0x20;
        if (stale_rejected.ok
            || std::strcmp(stale_rejected.failure,
                "palette-variant-writer-layout-stale") != 0
            || !node_changed_capture.ok
            || node_changed.canonical_hash == palette.canonical_hash
            || writer_registry.producer_pending[1])
        {
            std::printf("palette stale/layout semantic gate failed\n");
            return 1;
        }
        if (!Horse::RestoreRollbackPaletteVariantSnapshotToLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, palette, writer_registry).ok)
        {
            std::printf("palette writer registry final restore failed\n");
            return 1;
        }

        std::vector<uint8_t> other_session = session;
        std::array<int32_t,
            Horse::kRollbackPaletteVariantSlotCount> other_states {
                8, 8, 8, 8};
        const size_t other_slot1_payload =
            Horse::kRollbackPaletteVariantFirstBufferOffset
            + Horse::kRollbackPaletteVariantObjectBytes
            + Horse::kRollbackPaletteVariantPayloadOffset;
        const uint8_t other_before = other_session[other_slot1_payload];
        const uint64_t producer_serial_before_session_reject =
            writer_registry.next_producer_serial;
        const auto stale_session_observation =
            Horse::ObserveRollbackPaletteVariantWriterFromLayout(
                reinterpret_cast<uintptr_t>(other_session.data())
                    + Horse::kRollbackPaletteVariantFirstBufferOffset
                    + Horse::kRollbackPaletteVariantObjectBytes,
                reinterpret_cast<uintptr_t>(other_session.data()),
                mock_vtable, semantic_layout, writer_registry);
        const auto changed_session_rejected =
            Horse::RestoreRollbackPaletteVariantSnapshotToLayout(
                other_states.data(),
                reinterpret_cast<uintptr_t>(other_session.data()),
                mock_vtable, palette, writer_registry);
        const uint8_t other_after = other_session[other_slot1_payload];
        if (changed_session_rejected.ok
            || stale_session_observation
                != Horse::RollbackPaletteVariantWriterObservation::
                    SessionChanged
            || writer_registry.next_producer_serial
                != producer_serial_before_session_reject
            || other_states != std::array<int32_t, 4>{8, 8, 8, 8}
            || other_before != other_after)
        {
            std::printf("palette variant session identity gate failed\n");
            return 1;
        }

        const uint64_t malformed_read = write1 + 1;
        std::memcpy(reinterpret_cast<void*>(object(1) + 0x10),
            &malformed_read, sizeof(malformed_read));
        Horse::RollbackPaletteVariantSnapshot malformed {};
        Horse::PrepareRollbackPaletteVariantStorage(malformed, false);
        const auto malformed_rejected =
            Horse::CaptureRollbackPaletteVariantSnapshotFromLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, malformed,
                writer_registry, true);
        std::memcpy(reinterpret_cast<void*>(object(1) + 0x10),
            &read1, sizeof(read1));
        if (malformed_rejected.ok)
        {
            std::printf("palette variant malformed cursor accepted\n");
            return 1;
        }

        uintptr_t bad_vtable = mock_vtable + 1;
        std::memcpy(reinterpret_cast<void*>(object(2)),
            &bad_vtable, sizeof(bad_vtable));
        std::memset(reinterpret_cast<void*>(object(1)
                + Horse::kRollbackPaletteVariantPayloadOffset),
            0xCC, static_cast<size_t>(write1));
        states = {8, 8, 8, 8};
        const auto rejected =
            Horse::RestoreRollbackPaletteVariantSnapshotToLayout(
                states.data(),
                reinterpret_cast<uintptr_t>(session.data()),
                mock_vtable, palette, writer_registry);
        uint8_t slot1_first = 0;
        std::memcpy(&slot1_first,
            reinterpret_cast<const void*>(object(1)
                + Horse::kRollbackPaletteVariantPayloadOffset),
            sizeof(slot1_first));
        if (rejected.ok || states[0] != 8 || states[1] != 8
            || states[2] != 8 || states[3] != 8
            || slot1_first != 0xCC)
        {
            std::printf("palette variant fail-closed restore failed\n");
            return 1;
        }
    }

    constexpr size_t hg_chunk_total = 0x2000;
    std::array<uint8_t, hg_chunk_total> hg_chunk_a {};
    std::array<uint8_t, hg_chunk_total> hg_chunk_b {};
    hg_chunk_a.fill(0x11);
    hg_chunk_b = hg_chunk_a;
    const uint64_t hg_chunk_hash =
        Horse::RollbackHashHgCpuCanonicalCharaChunkBytes(
            hg_chunk_a.data(), hg_chunk_a.size(), 0, 0,
            hg_chunk_a.size());
    if (hg_chunk_hash == 0 || hg_chunk_hash
            != Horse::RollbackHashHgCpuCanonicalCharaChunkBytes(
                hg_chunk_b.data(), hg_chunk_b.size(), 0, 0,
                hg_chunk_b.size()))
    {
        std::printf("HgCpu chunk diagnostic stability failed\n");
        return 1;
    }
    ++hg_chunk_b[0x100];
    if (hg_chunk_hash == Horse::RollbackHashHgCpuCanonicalCharaChunkBytes(
            hg_chunk_b.data(), hg_chunk_b.size(), 0, 0,
            hg_chunk_b.size()))
    {
        std::printf("HgCpu chunk diagnostic sensitivity failed\n");
        return 1;
    }
    hg_chunk_b = hg_chunk_a;
    ++hg_chunk_b[0x260];
    if (hg_chunk_hash != Horse::RollbackHashHgCpuCanonicalCharaChunkBytes(
            hg_chunk_b.data(), hg_chunk_b.size(), 0, 0,
            hg_chunk_b.size()))
    {
        std::printf("HgCpu chunk diagnostic exclusion failed\n");
        return 1;
    }

    const bool canonical_restore_policy_ok =
        Horse::RollbackHgCpuCanonicalRestoreEvidenceMatches(
            0x1234, 0x1234, true, 0, true, true, true, 0);
    const bool canonical_restore_rejects_bytes =
        !Horse::RollbackHgCpuCanonicalRestoreEvidenceMatches(
            0x1234, 0x1234, true, 0, true, true, true, 1);
    const bool round_sequence_states_ok =
        Horse::RollbackRoundSequenceStateOwned(1, 1)
        && Horse::RollbackRoundSequenceStateOwned(1, 2)
        && Horse::RollbackRoundSequenceStateOwned(1, 3)
        && Horse::RollbackRoundSequenceStateOwned(1, 5)
        && Horse::RollbackRoundSequenceStateOwned(1, 9)
        && Horse::RollbackRoundSequenceStateOwned(2, 1)
        && Horse::RollbackRoundSequenceStateOwned(2, 2)
        && Horse::RollbackRoundSequenceStateOwned(2, 3)
        && Horse::RollbackRoundSequenceStateOwned(2, 5)
        && Horse::RollbackRoundSequenceStateOwned(2, 9)
        && !Horse::RollbackRoundSequenceStateOwned(2, 0)
        && !Horse::RollbackRoundSequenceStateOwned(2, 4)
        && Horse::RollbackRoundSequenceStateOwned(7, 7)
        && !Horse::RollbackRoundSequenceStateOwned(7, 2);
    constexpr uint32_t round_identity_mismatch = 1u << 8;
    const bool next_round_boundary_ok =
        Horse::RollbackStockNextRoundBoundaryEligible(
            3, 0x1111, 1, 4, 0x2222, 1,
            round_identity_mismatch)
        && Horse::RollbackStockNextRoundBoundaryEligible(
            3, 0x1111, 2, 4, 0x2222, 1,
            round_identity_mismatch)
        && !Horse::RollbackStockNextRoundBoundaryEligible(
            3, 0x1111, 1, 4, 0x2222, 3,
            round_identity_mismatch)
        && !Horse::RollbackStockNextRoundBoundaryEligible(
            3, 0x1111, 1, 4, 0x2222, 2,
            round_identity_mismatch)
        && !Horse::RollbackStockNextRoundBoundaryEligible(
            3, 0x1111, 1, 5, 0x2222, 1,
            round_identity_mismatch)
        // Status 1 is only eligible after the exact next ordinal exists.
        && !Horse::RollbackStockNextRoundBoundaryEligible(
            3, 0x1111, 1, 3, 0x1111, 1, 0)
        && !Horse::RollbackStockNextRoundBoundaryEligible(
            3, 0x1111, 1, 4, 0x2222, 1,
            round_identity_mismatch | (1u << 2))
        // Status 3 by itself is not a handoff. The verified applied-round
        // index must advance exactly once with no unrelated lifecycle drift.
        && !Horse::RollbackStockRoundResultHandoffEligible(
            3, 2, 3, 3, 1u << 10)
        && Horse::RollbackStockRoundResultHandoffEligible(
            3, 1, 4, 3, (1u << 8) | (1u << 10))
        && Horse::RollbackStockRoundResultHandoffEligible(
            3, 2, 4, 3, (1u << 8) | (1u << 10))
        && Horse::RollbackStockRoundResultHandoffEligible(
            3, 2, 4, 2, (1u << 8))
        && !Horse::RollbackStockRoundResultHandoffEligible(
            3, 2, 4, 3, (1u << 2) | (1u << 8) | (1u << 10));
    if (!round_sequence_states_ok || !next_round_boundary_ok)
    {
        std::printf("stock round lifecycle predicate failed\n");
        return 1;
    }
    const bool bounded_round_transition =
        Horse::RollbackStockRoundTransitionTokenEligible(
            1, 2, true, true, true);
    const bool rejects_two_round_jump =
        !Horse::RollbackStockRoundTransitionTokenEligible(
            1, 3, true, true, true);
    if (!bounded_round_transition || !rejects_two_round_jump)
    {
        std::printf("bounded stock round transition failed\n");
        return 1;
    }
    std::array<uint8_t, 257> zeros {};
    Horse::RollbackHash zero_reference {};
    Horse::RollbackHash zero_fast {};
    zero_reference.add_bytes(zeros.data(), zeros.size());
    zero_fast.add_zero_bytes(zeros.size());
    if (zero_reference.value != zero_fast.value)
    {
        std::printf("zero-span hash equivalence failed\n");
        return 1;
    }

    if (!Horse::RollbackHgCpuPlaybackTransformCacheOffset(0x6438)
        || !Horse::RollbackHgCpuPlaybackTransformCacheOffset(0x6457)
        || Horse::RollbackHgCpuPlaybackTransformCacheOffset(0x6437)
        || Horse::RollbackHgCpuPlaybackTransformCacheOffset(0x6458))
    {
        std::printf("HgCpu playback transform-cache classification failed\n");
        return 1;
    }

    if (!Horse::RollbackHgCpuPlaybackAbsoluteCounterOffset(0x63DC)
        || !Horse::RollbackHgCpuPlaybackAbsoluteCounterOffset(0x648C)
        || !Horse::RollbackHgCpuPlaybackAbsoluteCounterOffset(0x669C)
        || Horse::RollbackHgCpuPlaybackAbsoluteCounterOffset(0x63DB)
        || Horse::RollbackHgCpuPlaybackAbsoluteCounterOffset(0x63E0)
        || !Horse::RollbackHgCpuRestoreLocalOffsetIgnored(0x260))
    {
        std::printf("HgCpu playback absolute-counter classification failed\n");
        return 1;
    }
    if (!Horse::RollbackHgCpuPlaybackRampStepOffset(0x63F4)
        || !Horse::RollbackHgCpuPlaybackRampStepOffset(0x64A4)
        || Horse::RollbackHgCpuPlaybackRampStepOffset(0x63F3)
        || Horse::RollbackHgCpuPlaybackRampStepOffset(0x63F8))
    {
        std::printf("HgCpu playback ramp-step classification failed\n");
        return 1;
    }

    std::array<uint8_t, 0x6800> inactive_ramp_a {};
    std::array<uint8_t, 0x6800> inactive_ramp_b {};
    constexpr size_t kPlaybackSlot1 = 0x63D8 + 0xB0;
    const float stale_ramp_step = -0.2f;
    std::memcpy(
        inactive_ramp_b.data() + kPlaybackSlot1 + 0x1C,
        &stale_ramp_step, sizeof(stale_ramp_step));
    const auto hash_playback = [](const auto& bytes) noexcept {
        return Horse::RollbackHashHgCpuCanonicalCharaChunkBytes(
            bytes.data(), bytes.size(), 0, 0, bytes.size());
    };
    if (hash_playback(inactive_ramp_a) == 0
        || hash_playback(inactive_ramp_a)
            != hash_playback(inactive_ramp_b))
    {
        std::printf(
            "HgCpu inactive playback ramp-step canonicalization failed\n");
        return 1;
    }
    const float active_ramp_remaining = 1.0f;
    std::memcpy(
        inactive_ramp_a.data() + kPlaybackSlot1 + 0x18,
        &active_ramp_remaining, sizeof(active_ramp_remaining));
    std::memcpy(
        inactive_ramp_b.data() + kPlaybackSlot1 + 0x18,
        &active_ramp_remaining, sizeof(active_ramp_remaining));
    if (hash_playback(inactive_ramp_a)
        == hash_playback(inactive_ramp_b))
    {
        std::printf(
            "HgCpu active playback ramp-step sensitivity failed\n");
        return 1;
    }

    std::array<uint8_t, 0x6800> playback_a {};
    std::array<uint8_t, 0x6800> playback_b {};
    playback_a.fill(0x31);
    playback_b = playback_a;
    const uint64_t playback_hash =
        Horse::RollbackHashHgCpuCanonicalCharaChunkBytes(
            playback_a.data(), playback_a.size(), 0, 0,
            playback_a.size());
    for (size_t slot = 0; slot < 5; ++slot)
        ++playback_b[0x63D8 + slot * 0xB0 + 0x04];
    const uint64_t playback_integrity_a =
        Horse::RollbackFastIntegrityHashBytes(
            playback_a.data(), playback_a.size());
    const uint64_t playback_integrity_b =
        Horse::RollbackFastIntegrityHashBytes(
            playback_b.data(), playback_b.size());
    bool counters_restore_visible = true;
    for (size_t slot = 0; slot < 5; ++slot)
    {
        counters_restore_visible = counters_restore_visible
            && !Horse::RollbackHgCpuRestoreLocalOffsetIgnored(
                0x63D8 + slot * 0xB0 + 0x04);
    }
    size_t unignored_counter_mismatches = 0;
    for (size_t offset = 0; offset < playback_a.size(); ++offset)
    {
        if (playback_a[offset] != playback_b[offset]
            && !Horse::RollbackHgCpuRestoreLocalOffsetIgnored(offset))
        {
            ++unignored_counter_mismatches;
        }
    }
    if (playback_hash == 0 || playback_hash !=
            Horse::RollbackHashHgCpuCanonicalCharaChunkBytes(
                playback_b.data(), playback_b.size(), 0, 0,
                playback_b.size())
        || playback_integrity_a == playback_integrity_b
        || !counters_restore_visible
        || unignored_counter_mismatches != 5
        || Horse::RollbackHgCpuCanonicalRestoreEvidenceMatches(
            playback_hash, playback_hash, true, 0, true, true, true,
            unignored_counter_mismatches))
    {
        std::printf(
            "HgCpu playback peer-only counter exclusion failed\n");
        return 1;
    }
    ++playback_b[0x63D8 + 0x02];
    if (playback_hash ==
            Horse::RollbackHashHgCpuCanonicalCharaChunkBytes(
                playback_b.data(), playback_b.size(), 0, 0,
                playback_b.size()))
    {
        std::printf("HgCpu playback gameplay-field sensitivity failed\n");
        return 1;
    }

    Horse::RollbackHgCpuSnapshotFrame peer_frame {};
    peer_frame.khit_topology_ok = true;
    for (size_t player = 0; player < 2; ++player)
    {
        auto& topology = peer_frame.khit_topology[player];
        topology.ok = true;
        topology.node_stream_bytes = 0x26;
        topology.nodes.resize(1);
        topology.nodes[0].list_index = static_cast<uint8_t>(player);
        topology.nodes[0].node_index = 0;
        topology.nodes[0].writer_tag = 0;
        topology.nodes[0].writer_bytes = 0x26;
        topology.nodes[0].bytes[0x14] =
            static_cast<uint8_t>(0x20 + player);
    }
    const size_t peer_record =
        Horse::RollbackHgCpuCharaRecordBytes(&peer_frame, 0);
    peer_frame.bytes.resize(peer_record * 2, 0x31);
    peer_frame.used_bytes = peer_frame.bytes.size();
    peer_frame.motion_banks.ok = true;
    peer_frame.motion_banks.bytes.resize(
        Horse::RollbackMotionBankTotalBytes(), 0x41);
    for (size_t player = 0;
         player < Horse::kRollbackMotionBankPlayerCount; ++player)
    {
        for (size_t bank = 0;
             bank < Horse::kRollbackMotionBankCount; ++bank)
        {
            peer_frame.motion_banks.current_slot[player][bank] = 0;
            peer_frame.motion_banks.provider_slot[player][bank] = 1;
        }
    }
    peer_frame.secondary_event_stack.ok = true;
    peer_frame.chara_animation.ok = true;
    peer_frame.motion_tail.ok = true;
    peer_frame.motion_tail.bytes.resize(
        2 * Horse::kRollbackMoveVMMotionTailBytes, 0x51);
    peer_frame.skeleton_runtime.ok = true;
    peer_frame.timer_node.ok = true;
    const uint64_t peer_canonical =
        Horse::RollbackHashHgCpuCanonical(peer_frame);
    const auto peer_breakdown =
        Horse::RollbackBuildHgCpuPeerBreakdown(peer_frame);
    if (peer_canonical == 0
        || peer_breakdown.chara_stream_hash[0] == 0
        || peer_breakdown.chara_stream_hash[1] == 0
        || peer_breakdown.khit_hash[0] == 0
         || peer_breakdown.khit_hash[1] == 0
         || peer_breakdown.motion_slot_hash == 0
         || peer_breakdown.motion_provider_hash[0][0] == 0
         || peer_breakdown.motion_provider_hash[0][1] == 0
         || peer_breakdown.motion_provider_hash[1][0] == 0
         || peer_breakdown.motion_provider_hash[1][1] == 0
         || peer_breakdown.motion_provider_age[0][0] != 1
         || peer_breakdown.motion_provider_age[0][1] != 1
         || peer_breakdown.motion_provider_age[1][0] != 1
         || peer_breakdown.motion_provider_age[1][1] != 1
        || peer_breakdown.secondary_event_hash == 0
        || peer_breakdown.timer_shape_hash == 0
        || peer_breakdown.skeleton_shape_hash == 0
        || Horse::RollbackHgCpuPeerBreakdownMismatchMask(
            peer_breakdown, peer_breakdown) != 0
         || Horse::RollbackHgCpuPeerCharaChunkMismatchMask(
             peer_breakdown, peer_breakdown) != 0
         || Horse::RollbackHgCpuPeerMotionContributionMismatchMask(
             peer_breakdown, peer_breakdown) != 0)
    {
        std::printf(
            "HgCpu peer breakdown baseline failed "
            "canonical=%llu chara=%llu/%llu khit=%llu/%llu "
            "motion=%llu provider=%llu/%llu/%llu/%llu "
            "age=%u/%u/%u/%u secondary=%llu timer=%llu skeleton=%llu\n",
            static_cast<unsigned long long>(peer_canonical),
            static_cast<unsigned long long>(peer_breakdown.chara_stream_hash[0]),
            static_cast<unsigned long long>(peer_breakdown.chara_stream_hash[1]),
            static_cast<unsigned long long>(peer_breakdown.khit_hash[0]),
            static_cast<unsigned long long>(peer_breakdown.khit_hash[1]),
            static_cast<unsigned long long>(peer_breakdown.motion_slot_hash),
            static_cast<unsigned long long>(peer_breakdown.motion_provider_hash[0][0]),
            static_cast<unsigned long long>(peer_breakdown.motion_provider_hash[0][1]),
            static_cast<unsigned long long>(peer_breakdown.motion_provider_hash[1][0]),
            static_cast<unsigned long long>(peer_breakdown.motion_provider_hash[1][1]),
            static_cast<unsigned>(peer_breakdown.motion_provider_age[0][0]),
            static_cast<unsigned>(peer_breakdown.motion_provider_age[0][1]),
            static_cast<unsigned>(peer_breakdown.motion_provider_age[1][0]),
            static_cast<unsigned>(peer_breakdown.motion_provider_age[1][1]),
            static_cast<unsigned long long>(peer_breakdown.secondary_event_hash),
            static_cast<unsigned long long>(peer_breakdown.timer_shape_hash),
            static_cast<unsigned long long>(peer_breakdown.skeleton_shape_hash));
        return 1;
    }
    auto require_peer_breakdown_mask = [&](
        const Horse::RollbackHgCpuSnapshotFrame& changed,
        uint32_t expected_mask,
        uint32_t expected_chunk_mask,
        const char* label) {
        const auto changed_breakdown =
            Horse::RollbackBuildHgCpuPeerBreakdown(changed);
        if (Horse::RollbackHgCpuPeerBreakdownMismatchMask(
                peer_breakdown, changed_breakdown) != expected_mask
            || Horse::RollbackHgCpuPeerCharaChunkMismatchMask(
                peer_breakdown, changed_breakdown) != expected_chunk_mask
            || Horse::RollbackHashHgCpuCanonical(changed) == peer_canonical)
        {
            std::printf("HgCpu peer breakdown %s isolation failed\n", label);
            return false;
        }
        return true;
    };
    auto changed_peer_frame = peer_frame;
    ++changed_peer_frame.bytes[0x100];
    if (!require_peer_breakdown_mask(changed_peer_frame,
            Horse::kRollbackHgCpuPeerMismatchChara0, 1u, "chara"))
        return 1;
    changed_peer_frame = peer_frame;
    ++changed_peer_frame.bytes[peer_record + 0x100];
    if (!require_peer_breakdown_mask(changed_peer_frame,
            Horse::kRollbackHgCpuPeerMismatchChara1, 1u << 8,
            "chara-player-1"))
        return 1;
    changed_peer_frame = peer_frame;
    ++changed_peer_frame.khit_topology[0].nodes[0].bytes[0x14];
    if (!require_peer_breakdown_mask(changed_peer_frame,
            Horse::kRollbackHgCpuPeerMismatchKHit0, 0, "khit"))
        return 1;
    changed_peer_frame = peer_frame;
    ++changed_peer_frame.khit_topology[1].nodes[0].bytes[0x14];
    if (!require_peer_breakdown_mask(changed_peer_frame,
            Horse::kRollbackHgCpuPeerMismatchKHit1, 0, "khit-player-1"))
        return 1;
    for (size_t player = 0;
         player < Horse::kRollbackMotionBankPlayerCount; ++player)
    {
        for (size_t bank = 0;
             bank < Horse::kRollbackMotionBankCount; ++bank)
        {
            changed_peer_frame = peer_frame;
            ++changed_peer_frame.motion_banks.bytes[
                Horse::RollbackMotionBankByteOffset(player, bank, 1)];
            const auto changed_motion_breakdown =
                Horse::RollbackBuildHgCpuPeerBreakdown(changed_peer_frame);
            const uint32_t expected_bit = 1u << static_cast<uint32_t>(
                player * Horse::kRollbackMotionBankCount + bank);
            bool other_coordinates_equal = true;
            for (size_t other_player = 0;
                 other_player < Horse::kRollbackMotionBankPlayerCount;
                 ++other_player)
            {
                for (size_t other_bank = 0;
                     other_bank < Horse::kRollbackMotionBankCount;
                     ++other_bank)
                {
                    if (other_player == player && other_bank == bank)
                        continue;
                    other_coordinates_equal = other_coordinates_equal
                        && peer_breakdown.motion_provider_age
                            [other_player][other_bank]
                            == changed_motion_breakdown.motion_provider_age
                                [other_player][other_bank]
                        && peer_breakdown.motion_provider_hash
                            [other_player][other_bank]
                            == changed_motion_breakdown.motion_provider_hash
                                [other_player][other_bank];
                }
            }
            if (Horse::RollbackHgCpuPeerBreakdownMismatchMask(
                    peer_breakdown, changed_motion_breakdown)
                    != 0
                || Horse::RollbackHgCpuPeerMotionContributionMismatchMask(
                    peer_breakdown, changed_motion_breakdown) != expected_bit
                || Horse::RollbackHashHgCpuCanonical(changed_peer_frame)
                    != peer_canonical
                || !other_coordinates_equal)
            {
                std::printf(
                    "HgCpu presentation motion p%zu/b%zu exclusion failed\n",
                    player, bank);
                return 1;
            }
        }
    }
    for (size_t player = 0;
         player < Horse::kRollbackMotionBankPlayerCount; ++player)
    {
        for (size_t bank = 0;
             bank < Horse::kRollbackMotionBankCount; ++bank)
        {
            changed_peer_frame = peer_frame;
            const size_t excluded_local = bank == 0
                ? 3 * Horse::kRollbackMotionBankMatrixBytes
                : 0;
            ++changed_peer_frame.motion_banks.bytes[
                Horse::RollbackMotionBankByteOffset(player, bank, 0)
                    + excluded_local];
            const auto changed_current_breakdown =
                Horse::RollbackBuildHgCpuPeerBreakdown(changed_peer_frame);
            if (Horse::RollbackHgCpuPeerBreakdownMismatchMask(
                    peer_breakdown, changed_current_breakdown)
                    != 0
                || Horse::RollbackHgCpuPeerMotionContributionMismatchMask(
                    peer_breakdown, changed_current_breakdown) != 0
                || Horse::RollbackHashHgCpuCanonical(changed_peer_frame)
                    != peer_canonical)
            {
                std::printf(
                    "HgCpu solved-pose p%zu/b%zu exclusion failed\n",
                    player, bank);
                return 1;
            }
        }
    }
    for (size_t player = 0;
         player < Horse::kRollbackMotionBankPlayerCount; ++player)
    {
        for (const size_t bone : Horse::kRollbackMotionBankGameplayBones)
        {
            changed_peer_frame = peer_frame;
            ++changed_peer_frame.motion_banks.bytes[
                Horse::RollbackMotionBankByteOffset(player, 0, 0)
                    + bone * Horse::kRollbackMotionBankMatrixBytes];
            const auto changed_gameplay_breakdown =
                Horse::RollbackBuildHgCpuPeerBreakdown(changed_peer_frame);
            if (Horse::RollbackHgCpuPeerBreakdownMismatchMask(
                    peer_breakdown, changed_gameplay_breakdown)
                    != Horse::kRollbackHgCpuPeerMismatchMotion
                || Horse::RollbackHgCpuPeerMotionContributionMismatchMask(
                    peer_breakdown, changed_gameplay_breakdown) != 0
                || Horse::RollbackHashHgCpuCanonical(changed_peer_frame)
                    == peer_canonical)
            {
                std::printf(
                    "HgCpu future-gameplay matrix p%zu/bone%zu sensitivity failed\n",
                    player, bone);
                return 1;
            }
        }
    }
    changed_peer_frame = peer_frame;
    changed_peer_frame.motion_banks.provider_slot[0][0] = 2;
    auto changed_motion_breakdown =
        Horse::RollbackBuildHgCpuPeerBreakdown(changed_peer_frame);
    if (Horse::RollbackHgCpuPeerBreakdownMismatchMask(
            peer_breakdown, changed_motion_breakdown)
            != Horse::kRollbackHgCpuPeerMismatchMotion
        || Horse::RollbackHgCpuPeerMotionContributionMismatchMask(
            peer_breakdown, changed_motion_breakdown) != 1u
        || changed_motion_breakdown.motion_provider_age[0][0] != 2
        || changed_motion_breakdown.motion_provider_hash[0][0]
            != peer_breakdown.motion_provider_hash[0][0]
        || changed_motion_breakdown.motion_slot_hash
            == peer_breakdown.motion_slot_hash
        || Horse::RollbackHashHgCpuCanonical(changed_peer_frame)
            == peer_canonical)
    {
        std::printf("HgCpu motion contribution age isolation failed\n");
        return 1;
    }
    changed_peer_frame = peer_frame;
    ++changed_peer_frame.motion_banks.bytes[
        Horse::RollbackMotionBankByteOffset(0, 0, 2)];
    changed_motion_breakdown =
        Horse::RollbackBuildHgCpuPeerBreakdown(changed_peer_frame);
    if (Horse::RollbackHgCpuPeerBreakdownMismatchMask(
            peer_breakdown, changed_motion_breakdown) != 0
        || Horse::RollbackHgCpuPeerMotionContributionMismatchMask(
            peer_breakdown, changed_motion_breakdown) != 0
        || Horse::RollbackHashHgCpuCanonical(changed_peer_frame)
            != peer_canonical)
    {
        std::printf("HgCpu unused motion buffer isolation failed\n");
        return 1;
    }
    changed_peer_frame = peer_frame;
    ++changed_peer_frame.secondary_event_stack.bytes[0][0x25C];
    if (!require_peer_breakdown_mask(changed_peer_frame,
            Horse::kRollbackHgCpuPeerMismatchSecondary, 0, "secondary"))
        return 1;
    changed_peer_frame = peer_frame;
    ++changed_peer_frame.chara_animation.players[0].scheduler[0x38];
    if (!require_peer_breakdown_mask(
            changed_peer_frame,
            Horse::kRollbackHgCpuPeerMismatchSecondary,
            0,
            "chara-animation"))
        return 1;
    changed_peer_frame = peer_frame;
    ++changed_peer_frame.timer_node.indexed_nonzero_count;
    if (!require_peer_breakdown_mask(changed_peer_frame,
            Horse::kRollbackHgCpuPeerMismatchTimer, 0, "timer"))
        return 1;
    changed_peer_frame = peer_frame;
    changed_peer_frame.skeleton_runtime.inline_bytes.resize(1);
    if (!require_peer_breakdown_mask(changed_peer_frame,
            Horse::kRollbackHgCpuPeerMismatchSkeleton, 0, "skeleton"))
        return 1;
    auto shape_breakdown = peer_breakdown;
    ++shape_breakdown.effective_bytes;
    if (Horse::RollbackHgCpuPeerBreakdownMismatchMask(
            peer_breakdown, shape_breakdown)
            != Horse::kRollbackHgCpuPeerMismatchShape
        || Horse::RollbackHgCpuPeerCharaChunkMismatchMask(
            peer_breakdown, shape_breakdown) != 0)
    {
        std::printf("HgCpu peer breakdown shape isolation failed\n");
        return 1;
    }

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
    const uint64_t event_slots_a =
        Horse::RollbackHashSecondaryEventSlotStateCanonical(
            event_stack_a, 0);
    const uint64_t event_slots_b =
        Horse::RollbackHashSecondaryEventSlotStateCanonical(
            event_stack_b, 0);
    const uint64_t event_cursors_a =
        Horse::RollbackHashSecondaryEventCursorCanonical(
            event_stack_a, 0);
    const uint64_t event_cursors_b =
        Horse::RollbackHashSecondaryEventCursorCanonical(
            event_stack_b, 0);
    if (event_canonical_a == 0 || event_canonical_a != event_canonical_b
        || event_slots_a == 0 || event_slots_a != event_slots_b
        || event_cursors_a == 0 || event_cursors_a != event_cursors_b)
    {
        std::printf("secondary-event pointer normalization failed\n");
        return 1;
    }
    ++event_stack_b.bytes[0][0x25C];
    if (event_canonical_a
            == Horse::RollbackHashSecondaryEventStackCanonical(event_stack_b)
        || event_slots_a
            == Horse::RollbackHashSecondaryEventSlotStateCanonical(
                event_stack_b, 0)
        || event_cursors_a
            != Horse::RollbackHashSecondaryEventCursorCanonical(
                event_stack_b, 0))
    {
        std::printf("secondary-event previous-variant sensitivity failed\n");
        return 1;
    }
    event_stack_b.bytes[0][0x25C] = event_stack_a.bytes[0][0x25C];
    ++event_stack_b.header_cursors[0][1];
    if (event_canonical_a
            == Horse::RollbackHashSecondaryEventStackCanonical(event_stack_b)
        || event_slots_a
            != Horse::RollbackHashSecondaryEventSlotStateCanonical(
                event_stack_b, 0)
        || event_cursors_a
            == Horse::RollbackHashSecondaryEventCursorCanonical(
                event_stack_b, 0))
    {
        std::printf("secondary-event cursor sensitivity failed\n");
        return 1;
    }
    event_stack_b = event_stack_a;
    event_stack_b.header_cursors[0][7] = 0xFFFF;
    if (Horse::RollbackHashSecondaryEventStackHistory(event_stack_a)
            != Horse::RollbackHashSecondaryEventStackHistory(event_stack_b))
    {
        std::printf("secondary-event unused cursor affected integrity\n");
        return 1;
    }

    Horse::RollbackSecondaryEventStackHistory authority_source {};
    Horse::RollbackSecondaryEventStackHistory authority_guest {};
    authority_source.ok = authority_guest.ok = true;
    for (size_t player = 0; player < 2; ++player)
    {
        authority_source.header_count[player] = 3;
        authority_guest.header_count[player] = 3;
        authority_source.chara[player] = 0x1000 + player * 0x100;
        authority_guest.chara[player] = 0x9000 + player * 0x100;
        for (size_t slot = 0;
             slot < Horse::kRollbackSecondaryEventSlotCount; ++slot)
        {
            const size_t offset =
                slot * Horse::kRollbackSecondaryEventSlotBytes;
            std::memset(
                authority_source.bytes[player].data() + offset,
                static_cast<int>(0x10 + player + slot), 8);
            std::memset(
                authority_source.bytes[player].data() + offset + 0x10,
                static_cast<int>(0x40 + player + slot), 8);
            const uint64_t guest_pointer =
                0xABC00000ull + player * 0x1000 + slot;
            std::memcpy(
                authority_guest.bytes[player].data() + offset + 0x08,
                &guest_pointer, sizeof(guest_pointer));
        }
        for (size_t scalar = 0;
             scalar < Horse::kRollbackSecondaryEventScalarBytes; ++scalar)
        {
            authority_source.bytes[player]
                [Horse::kRollbackSecondaryEventScalarOffset + scalar] =
                    static_cast<uint8_t>(0x80 + player * 8 + scalar);
        }
        for (size_t cursor = 0; cursor < 3; ++cursor)
        {
            authority_source.header_cursors[player][cursor] =
                static_cast<uint16_t>(10 + player * 4 + cursor);
            authority_guest.header_cursors[player][cursor] =
                static_cast<uint16_t>(50 + player * 4 + cursor);
        }
    }
    authority_source.hash =
        Horse::RollbackHashSecondaryEventStackHistory(authority_source);
    authority_guest.hash =
        Horse::RollbackHashSecondaryEventStackHistory(authority_guest);
    std::array<Horse::RollbackSecondaryEventAuthorityMessage, 2>
        authority_messages {};
    for (uint8_t player = 0; player < 2; ++player)
    {
        if (!Horse::RollbackBuildSecondaryEventAuthorityMessage(
                authority_source, 0, player, 0x1111, 2, 1, 0x2222,
                authority_messages[player])
            || !Horse::RollbackApplySecondaryEventAuthorityMessage(
                authority_messages[player], authority_guest))
        {
            std::printf("secondary-event authority transfer failed\n");
            return 1;
        }
    }
    for (size_t player = 0; player < 2; ++player)
    {
        if (Horse::RollbackHashSecondaryEventSlotStateCanonical(
                authority_source, player)
                != Horse::RollbackHashSecondaryEventSlotStateCanonical(
                    authority_guest, player)
            || Horse::RollbackHashSecondaryEventCursorCanonical(
                authority_source, player)
                != Horse::RollbackHashSecondaryEventCursorCanonical(
                    authority_guest, player))
        {
            std::printf("secondary-event authority canonical mismatch\n");
            return 1;
        }
        for (size_t slot = 0;
             slot < Horse::kRollbackSecondaryEventSlotCount; ++slot)
        {
            const size_t pointer_offset =
                slot * Horse::kRollbackSecondaryEventSlotBytes + 0x08;
            uint64_t pointer = 0;
            std::memcpy(&pointer,
                authority_guest.bytes[player].data() + pointer_offset,
                sizeof(pointer));
            const uint64_t expected =
                0xABC00000ull + player * 0x1000 + slot;
            if (pointer != expected)
            {
                std::printf(
                    "secondary-event authority changed local pointer\n");
                return 1;
            }
        }
    }
    const auto authority_applied = authority_guest;
    if (!Horse::RollbackApplySecondaryEventAuthorityMessage(
            authority_messages[0], authority_guest)
        || Horse::RollbackHashSecondaryEventStackCanonical(authority_guest)
            != Horse::RollbackHashSecondaryEventStackCanonical(
                authority_applied))
    {
        std::printf("secondary-event authority is not idempotent\n");
        return 1;
    }
    auto invalid_authority = authority_messages[0];
    ++invalid_authority.slots[0];
    if (Horse::RollbackApplySecondaryEventAuthorityMessage(
            invalid_authority, authority_guest))
    {
        std::printf("secondary-event authority accepted bad hash\n");
        return 1;
    }
    invalid_authority = authority_messages[0];
    --invalid_authority.header_count;
    invalid_authority.state_hash =
        Horse::RollbackHashSecondaryEventAuthorityMessage(
            invalid_authority);
    if (Horse::RollbackApplySecondaryEventAuthorityMessage(
            invalid_authority, authority_guest))
    {
        std::printf("secondary-event authority accepted bad header count\n");
        return 1;
    }

    Horse::RollbackSecondaryEventAuthorityInbox authority_inbox {};
    const auto terminal_expected =
        Horse::ResolveRollbackSecondaryEventAuthorityExpectedRound(
            Horse::RollbackSecondaryEventAuthorityReceiveWindow::
                TerminalAccepted,
            true, 1, 0);
    const auto inter_round_expected =
        Horse::ResolveRollbackSecondaryEventAuthorityExpectedRound(
            Horse::RollbackSecondaryEventAuthorityReceiveWindow::
                StockInterRound,
            false, 1, 0);
    const auto frozen_expected =
        Horse::ResolveRollbackSecondaryEventAuthorityExpectedRound(
            Horse::RollbackSecondaryEventAuthorityReceiveWindow::FrozenRound,
            false, 2, 1);
    const auto premature_terminal =
        Horse::ResolveRollbackSecondaryEventAuthorityExpectedRound(
            Horse::RollbackSecondaryEventAuthorityReceiveWindow::
                TerminalAccepted,
            false, 1, 0);
    if (!terminal_expected.valid || terminal_expected.generation != 2
        || terminal_expected.ordinal != 1
        || inter_round_expected.generation != terminal_expected.generation
        || inter_round_expected.ordinal != terminal_expected.ordinal
        || !frozen_expected.valid || frozen_expected.generation != 2
        || frozen_expected.ordinal != 1 || premature_terminal.valid)
    {
        std::printf("secondary-event authority receive window failed\n");
        return 1;
    }
    if (!Horse::RollbackSecondaryEventAuthorityMayPublishBaseline(
            false, 1, false, false)
        || Horse::RollbackSecondaryEventAuthorityMayPublishBaseline(
            true, 2, false, false)
        || !Horse::RollbackSecondaryEventAuthorityMayPublishBaseline(
            true, 2, true, false)
        || Horse::RollbackSecondaryEventAuthorityMayPublishBaseline(
            false, 2, false, false)
        || !Horse::RollbackSecondaryEventAuthorityMayPublishBaseline(
            false, 2, false, true))
    {
        std::printf("secondary-event authority publication gate failed\n");
        return 1;
    }
    if (!authority_inbox.configure(0, 0x1111, 2, 1, 0x2222)
        || authority_inbox.accept(authority_messages[1])
            != Horse::RollbackSecondaryEventAuthorityInboxDisposition::Accepted
        || authority_inbox.ready()
        || authority_inbox.accept(authority_messages[1])
            != Horse::RollbackSecondaryEventAuthorityInboxDisposition::Duplicate
        || authority_inbox.accept(authority_messages[0])
            != Horse::RollbackSecondaryEventAuthorityInboxDisposition::Accepted
        || !authority_inbox.ready())
    {
        std::printf("secondary-event authority reorder/duplicate failed\n");
        return 1;
    }
    auto conflict_authority = authority_messages[0];
    ++conflict_authority.slots[0];
    conflict_authority.state_hash =
        Horse::RollbackHashSecondaryEventAuthorityMessage(conflict_authority);
    if (authority_inbox.accept(conflict_authority)
        != Horse::RollbackSecondaryEventAuthorityInboxDisposition::Conflict)
    {
        std::printf("secondary-event authority conflict was accepted\n");
        return 1;
    }
    Horse::RollbackSecondaryEventAuthorityInbox future_inbox {};
    if (!future_inbox.configure(0, 0x1111, 2, 1, 0x2222))
        return 1;
    auto future_authority = authority_messages[0];
    future_authority.round_generation = 3;
    if (future_inbox.accept(future_authority)
        != Horse::RollbackSecondaryEventAuthorityInboxDisposition::Future)
    {
        std::printf("secondary-event authority future generation accepted\n");
        return 1;
    }
    future_authority = authority_messages[0];
    future_authority.round_ordinal = 2;
    if (future_inbox.accept(future_authority)
        != Horse::RollbackSecondaryEventAuthorityInboxDisposition::Future)
    {
        std::printf("secondary-event authority future ordinal accepted\n");
        return 1;
    }
    Horse::RollbackSecondaryEventAuthorityInbox stale_inbox {};
    if (!stale_inbox.configure(0, 0x1111, 3, 2, 0x2222)
        || stale_inbox.accept(authority_messages[0])
            != Horse::RollbackSecondaryEventAuthorityInboxDisposition::Stale)
    {
        std::printf("secondary-event authority stale generation failed\n");
        return 1;
    }
    Horse::RollbackSecondaryEventAuthorityInbox wrapped_stale_inbox {};
    auto wrapped_stale_authority = authority_messages[0];
    wrapped_stale_authority.round_generation = 2;
    wrapped_stale_authority.round_ordinal = 0xFFFFu;
    if (!wrapped_stale_inbox.configure(0, 0x1111, 3, 0, 0x2222)
        || wrapped_stale_inbox.accept(wrapped_stale_authority)
            != Horse::RollbackSecondaryEventAuthorityInboxDisposition::Stale)
    {
        std::printf(
            "secondary-event authority wrapped old generation not stale\n");
        return 1;
    }
    auto wrong_identity = authority_messages[0];
    wrong_identity.session_epoch = 0x9999;
    if (future_inbox.accept(wrong_identity)
        != Horse::RollbackSecondaryEventAuthorityInboxDisposition::Invalid)
    {
        std::printf("secondary-event authority wrong identity accepted\n");
        return 1;
    }
    wrong_identity = authority_messages[0];
    wrong_identity.source_player_slot = 1;
    if (future_inbox.accept(wrong_identity)
        != Horse::RollbackSecondaryEventAuthorityInboxDisposition::Invalid)
    {
        std::printf("secondary-event authority wrong owner accepted\n");
        return 1;
    }
    wrong_identity = authority_messages[0];
    wrong_identity.match_identity_digest = 0x3333;
    if (future_inbox.accept(wrong_identity)
        != Horse::RollbackSecondaryEventAuthorityInboxDisposition::Invalid)
    {
        std::printf("secondary-event authority wrong match accepted\n");
        return 1;
    }
    using ActiveAuthorityDisposition =
        Horse::RollbackSecondaryEventAuthorityActiveDisposition;
    const auto classify_active_authority =
        [](const Horse::RollbackSecondaryEventAuthorityMessage& message,
           bool local_is_owner = false,
           uint64_t generation = 2,
           uint32_t ordinal = 1) noexcept {
            return Horse::
                ClassifyRollbackSecondaryEventAuthorityDuringActiveRound(
                    local_is_owner, 0, 0x1111, 0x2222,
                    generation, ordinal, message);
        };
    if (classify_active_authority(authority_messages[0])
            != ActiveAuthorityDisposition::DiscardStale
        || classify_active_authority(authority_messages[0], true)
            != ActiveAuthorityDisposition::Fatal)
    {
        std::printf("active secondary-event authority role gate failed\n");
        return 1;
    }
    auto prior_wrapped_authority = authority_messages[0];
    prior_wrapped_authority.round_generation = 2;
    prior_wrapped_authority.round_ordinal = 0xFFFF;
    if (classify_active_authority(
            prior_wrapped_authority, false, 3, 0)
            != ActiveAuthorityDisposition::DiscardStale)
    {
        std::printf("active authority rejected stale wrapped ordinal\n");
        return 1;
    }
    auto future_active_authority = authority_messages[0];
    future_active_authority.round_generation = 3;
    if (classify_active_authority(future_active_authority)
            != ActiveAuthorityDisposition::Fatal)
    {
        std::printf("active authority accepted future generation\n");
        return 1;
    }
    future_active_authority = authority_messages[0];
    future_active_authority.round_ordinal = 2;
    if (classify_active_authority(future_active_authority)
            != ActiveAuthorityDisposition::Fatal
        || classify_active_authority(wrong_identity)
            != ActiveAuthorityDisposition::Fatal)
    {
        std::printf("active authority accepted future/wrong identity\n");
        return 1;
    }
    size_t transaction_writes = 0;
    if (Horse::RollbackCommitSecondaryEventAuthorityTransaction(
            authority_source, authority_guest,
            [&](const auto&) noexcept {
                ++transaction_writes;
                return true;
            })
            != Horse::RollbackSecondaryEventAuthorityTransactionResult::Applied
        || transaction_writes != 1)
    {
        std::printf("secondary-event authority transaction apply failed\n");
        return 1;
    }
    transaction_writes = 0;
    bool transaction_order_ok = true;
    if (Horse::RollbackCommitSecondaryEventAuthorityTransaction(
            authority_source, authority_guest,
            [&](const auto& expected) noexcept {
                ++transaction_writes;
                transaction_order_ok &= transaction_writes == 1
                    ? expected.hash == authority_guest.hash
                    : expected.hash == authority_source.hash;
                return transaction_writes == 2;
            })
            != Horse::RollbackSecondaryEventAuthorityTransactionResult::
                RecoveredOriginal
        || transaction_writes != 2 || !transaction_order_ok)
    {
        std::printf("secondary-event authority recovery failed\n");
        return 1;
    }
    transaction_writes = 0;
    if (Horse::RollbackCommitSecondaryEventAuthorityTransaction(
            authority_source, authority_guest,
            [&](const auto&) noexcept {
                ++transaction_writes;
                return false;
            })
            != Horse::RollbackSecondaryEventAuthorityTransactionResult::
                Unrecoverable
        || transaction_writes != 2)
    {
        std::printf("secondary-event authority unrecoverable path failed\n");
        return 1;
    }

    MockMotionBankHistory motion_a {};
    MockMotionBankHistory motion_b {};
    motion_a.ok = motion_b.ok = true;
    motion_a.bytes.resize(Horse::RollbackMotionBankTotalBytes());
    motion_b.bytes.resize(Horse::RollbackMotionBankTotalBytes());
    for (size_t player = 0;
         player < Horse::kRollbackMotionBankPlayerCount;
         ++player)
    {
        for (size_t bank = 0;
             bank < Horse::kRollbackMotionBankCount;
             ++bank)
        {
            motion_a.current_slot[player][bank] = 0;
            motion_a.provider_slot[player][bank] = 1;
            motion_b.current_slot[player][bank] = 1;
            motion_b.provider_slot[player][bank] = 2;
            const size_t bytes = Horse::kRollbackMotionBankBytes[bank];
            for (size_t slot = 0;
                 slot < Horse::kRollbackMotionBankBufferCount;
                 ++slot)
            {
                const uint8_t value = static_cast<uint8_t>(
                    0x10 + player * 8 + bank * 3 + slot);
                const size_t a_offset = Horse::RollbackMotionBankByteOffset(
                    player, bank, slot);
                const size_t b_slot = (slot + 1)
                    % Horse::kRollbackMotionBankBufferCount;
                const size_t b_offset = Horse::RollbackMotionBankByteOffset(
                    player, bank, b_slot);
                std::memset(motion_a.bytes.data() + a_offset, value, bytes);
                std::memset(motion_b.bytes.data() + b_offset, value, bytes);
            }
        }
    }
    const uint64_t motion_peer_hash =
        Horse::RollbackHashMotionBankPeerState(motion_a);
    if (motion_peer_hash == 0
        || motion_peer_hash
            != Horse::RollbackHashMotionBankPeerState(motion_b))
    {
        std::printf("motion-bank logical ring normalization failed\n");
        return 1;
    }
    const size_t nonlogical_offset = Horse::RollbackMotionBankByteOffset(
        0, 0, 0);
    ++motion_b.bytes[nonlogical_offset];
    if (motion_peer_hash
        != Horse::RollbackHashMotionBankPeerState(motion_b))
    {
        std::printf("motion-bank unused physical slot affected peer hash\n");
        return 1;
    }
    const size_t logical_provider_offset = Horse::RollbackMotionBankByteOffset(
        0, 0, 2);
    ++motion_b.bytes[logical_provider_offset];
    if (motion_peer_hash
        != Horse::RollbackHashMotionBankPeerState(motion_b))
    {
        std::printf("motion-bank provider cache affected peer hash\n");
        return 1;
    }
    const size_t logical_current_offset = Horse::RollbackMotionBankByteOffset(
        0, 0, 1);
    ++motion_b.bytes[
        logical_current_offset
            + 3 * Horse::kRollbackMotionBankMatrixBytes];
    if (motion_peer_hash
        != Horse::RollbackHashMotionBankPeerState(motion_b))
    {
        std::printf("motion-bank solved-pose exclusion failed\n");
        return 1;
    }
    ++motion_b.bytes[
        logical_current_offset];
    if (motion_peer_hash
        == Horse::RollbackHashMotionBankPeerState(motion_b))
    {
        std::printf("motion-bank gameplay-current sensitivity failed\n");
        return 1;
    }
    motion_b = motion_a;
    motion_b.current_slot[0][0] = -1;
    if (Horse::RollbackHashMotionBankPeerState(motion_b) != 0)
    {
        std::printf("motion-bank invalid slot was accepted\n");
        return 1;
    }
    motion_b = motion_a;
    for (size_t player = 0;
         player < Horse::kRollbackMotionBankPlayerCount; ++player)
    {
        for (size_t bank = 0;
             bank < Horse::kRollbackMotionBankCount; ++bank)
        {
            motion_b.current_slot[player][bank] = 1;
            motion_b.provider_slot[player][bank] = 2;
            const size_t bytes = Horse::kRollbackMotionBankBytes[bank];
            for (size_t slot = 0;
                 slot < Horse::kRollbackMotionBankBufferCount; ++slot)
            {
                const size_t source = Horse::RollbackMotionBankByteOffset(
                    player, bank, slot);
                const size_t destination = Horse::RollbackMotionBankByteOffset(
                    player, bank,
                    (slot + 1) % Horse::kRollbackMotionBankBufferCount);
                std::memcpy(
                    motion_b.bytes.data() + destination,
                    motion_a.bytes.data() + source, bytes);
            }
            const size_t provider = Horse::RollbackMotionBankByteOffset(
                player, bank, 2);
            std::memset(motion_b.bytes.data() + provider, 0xE1, bytes);
        }
    }
    const auto motion_b_before_authority = motion_b.bytes;
    std::vector<Horse::RollbackMotionBankAuthorityMessage> motion_messages;
    for (uint8_t fighter = 0;
         fighter < Horse::kRollbackMotionBankPlayerCount; ++fighter)
    {
        for (uint8_t bank = 0; bank < Horse::kRollbackMotionBankCount; ++bank)
        {
            for (uint8_t chunk = 0;
                 chunk < Horse::RollbackMotionBankAuthorityChunkCount(bank);
                 ++chunk)
            {
                Horse::RollbackMotionBankAuthorityMessage message {};
                if (!Horse::RollbackBuildMotionBankAuthorityMessage(
                        motion_a, 0, fighter, bank, chunk,
                        0x1111, 1, 1, 0x2222, message))
                {
                    std::printf("motion authority message build failed\n");
                    return 1;
                }
                motion_messages.push_back(message);
            }
        }
    }
    Horse::RollbackMotionBankAuthorityInbox host_first_motion_inbox {};
    for (const auto& message : motion_messages)
    {
        const auto disposition =
            Horse::RollbackAcceptInitialMotionBankAuthorityBeforeBoundary(
                host_first_motion_inbox, true, false, 0, 0x1111, message);
        if (disposition
            == Horse::RollbackInitialMotionBankAuthorityDisposition::Invalid)
        {
            std::printf("host-first initial motion skew retention failed\n");
            return 1;
        }
    }
    if (!host_first_motion_inbox.ready()
        || !host_first_motion_inbox.configured_for(
            0, 0x1111, 1, 1, 0x2222)
        || host_first_motion_inbox.configured_for(
            0, 0x1111, 1, 1, 0x9999))
    {
        std::printf("host-first initial motion identity validation failed\n");
        return 1;
    }
    Horse::RollbackMotionBankAuthorityInbox motion_inbox {};
    if (!motion_inbox.configure(0, 0x1111, 1, 1, 0x2222)) return 1;
    for (auto it = motion_messages.rbegin(); it != motion_messages.rend(); ++it)
    {
        const auto disposition = motion_inbox.accept(*it);
        if (disposition
                == Horse::RollbackMotionBankAuthorityInboxDisposition::Invalid
            || disposition
                == Horse::RollbackMotionBankAuthorityInboxDisposition::Future
            || disposition
                == Horse::RollbackMotionBankAuthorityInboxDisposition::Conflict)
        {
            std::printf("motion authority reverse assembly failed\n");
            return 1;
        }
    }
    bool applied_provider_images_match = true;
    bool current_images_unchanged = true;
    if (!motion_inbox.ready() || !motion_inbox.apply(motion_b))
    {
        std::printf("motion authority apply/idempotence failed\n");
        return 1;
    }
    for (size_t player = 0;
         player < Horse::kRollbackMotionBankPlayerCount; ++player)
    {
        for (size_t bank = 0;
             bank < Horse::kRollbackMotionBankCount; ++bank)
        {
            uint32_t age_a = 0;
            uint32_t age_b = 0;
            size_t offset_a = 0;
            size_t offset_b = 0;
            size_t bytes_a = 0;
            size_t bytes_b = 0;
            size_t current_b = 0;
            size_t current_bytes_b = 0;
            applied_provider_images_match = applied_provider_images_match
                && Horse::RollbackMotionBankLogicalPreviousLocation(
                    motion_a, player, bank, age_a, offset_a, bytes_a)
                && Horse::RollbackMotionBankLogicalPreviousLocation(
                    motion_b, player, bank, age_b, offset_b, bytes_b)
                && age_a == age_b && bytes_a == bytes_b
                && std::memcmp(
                    motion_a.bytes.data() + offset_a,
                    motion_b.bytes.data() + offset_b, bytes_a) == 0;
            current_images_unchanged = current_images_unchanged
                && Horse::RollbackMotionBankLogicalCurrentLocation(
                    motion_b, player, bank, current_b, current_bytes_b)
                && std::memcmp(
                    motion_b.bytes.data() + current_b,
                    motion_b_before_authority.data() + current_b,
                    current_bytes_b) == 0;
        }
    }
    if (!applied_provider_images_match || !current_images_unchanged
        || Horse::RollbackHashMotionBankPeerState(motion_a)
            != Horse::RollbackHashMotionBankPeerState(motion_b)
        || motion_inbox.accept(motion_messages.front())
            != Horse::RollbackMotionBankAuthorityInboxDisposition::Duplicate)
    {
        std::printf("motion authority apply/idempotence failed\n");
        return 1;
    }
    // Guest-first skew: the local boundary is already known before any
    // authority record arrives, so direct expected-round assembly is valid.
    Horse::RollbackMotionBankAuthorityInbox guest_first_motion_inbox {};
    if (!guest_first_motion_inbox.configure(0, 0x1111, 1, 1, 0x2222))
        return 1;
    for (const auto& message : motion_messages)
    {
        const auto disposition = guest_first_motion_inbox.accept(message);
        if (disposition
                == Horse::RollbackMotionBankAuthorityInboxDisposition::Invalid
            || disposition
                == Horse::RollbackMotionBankAuthorityInboxDisposition::Future
            || disposition
                == Horse::RollbackMotionBankAuthorityInboxDisposition::Conflict)
        {
            std::printf("guest-first initial motion assembly failed\n");
            return 1;
        }
    }
    if (!guest_first_motion_inbox.ready()) return 1;
    auto conflicting_motion = motion_messages.front();
    ++conflicting_motion.payload[0];
    if (motion_inbox.accept(conflicting_motion)
        != Horse::RollbackMotionBankAuthorityInboxDisposition::Conflict)
    {
        std::printf("motion authority conflict was accepted\n");
        return 1;
    }
    Horse::RollbackMotionBankAuthorityInbox future_motion_inbox {};
    if (!future_motion_inbox.configure(0, 0x1111, 1, 1, 0x2222)) return 1;
    auto future_motion = motion_messages.front();
    future_motion.round_generation = 2;
    if (future_motion_inbox.accept(future_motion)
        != Horse::RollbackMotionBankAuthorityInboxDisposition::Future)
    {
        std::printf("motion authority future generation accepted\n");
        return 1;
    }
    Horse::RollbackMotionBankAuthorityInbox wrapped_stale_motion_inbox {};
    if (!wrapped_stale_motion_inbox.configure(
            0, 0x1111, 2, 0, 0x2222)) return 1;
    auto wrapped_stale_motion = motion_messages.front();
    wrapped_stale_motion.round_generation = 1;
    wrapped_stale_motion.round_ordinal = 0xFFFFu;
    if (wrapped_stale_motion_inbox.accept(wrapped_stale_motion)
        != Horse::RollbackMotionBankAuthorityInboxDisposition::Stale)
    {
        std::printf("motion authority wrapped old generation not stale\n");
        return 1;
    }

    struct MockCarriedState
    {
        int secondary {0};
        int motion {0};
        int wind {0};
    };
    using CarriedResult = Horse::RollbackCarriedStateTransactionResult;
    const MockCarriedState original_carried {1, 2, 3};
    const MockCarriedState authorized_carried {4, 5, 6};
    const auto run_carried_transaction =
        [&](int failure_mode, MockCarriedState& live, int& preflight_calls,
            int& restore_calls, int& verify_calls) noexcept {
            const auto preflight = [&](const MockCarriedState& expected) noexcept {
                ++preflight_calls;
                if (failure_mode == 1 && expected.secondary == 4) return false;
                if (failure_mode == 5 && expected.secondary == 1) return false;
                return true;
            };
            const auto restore = [&](const MockCarriedState& expected) noexcept {
                ++restore_calls;
                live.secondary = expected.secondary;
                if (expected.secondary == 4
                    && (failure_mode == 2 || failure_mode == 4
                        || failure_mode == 5))
                    return false;
                live.motion = expected.motion;
                if (expected.secondary == 4 && failure_mode == 3)
                {
                    live.wind = 99;
                    return false;
                }
                if (expected.secondary == 1 && failure_mode == 4)
                    return false;
                live.wind = expected.wind;
                return true;
            };
            const auto verify = [&](const MockCarriedState& expected) noexcept {
                ++verify_calls;
                return live.secondary == expected.secondary
                    && live.motion == expected.motion
                    && live.wind == expected.wind;
            };
            return Horse::RollbackExecuteCarriedStateTransaction(
                authorized_carried, original_carried,
                preflight, restore, verify);
        };
    for (int mode = 0; mode <= 5; ++mode)
    {
        MockCarriedState live = original_carried;
        int preflight_calls = 0;
        int restore_calls = 0;
        int verify_calls = 0;
        const CarriedResult result = run_carried_transaction(
            mode, live, preflight_calls, restore_calls, verify_calls);
        const CarriedResult expected = mode == 0 ? CarriedResult::Applied
            : mode == 1 ? CarriedResult::RejectedBeforeMutation
            : (mode == 2 || mode == 3)
                ? CarriedResult::FailedRecovered
                : CarriedResult::FailedUnrecoverable;
        const bool published = result == CarriedResult::Applied;
        if (result != expected
            || (mode == 0 && (live.secondary != 4 || live.motion != 5
                || live.wind != 6))
            || (mode == 1 && (restore_calls != 0
                || live.secondary != 1 || live.motion != 2
                || live.wind != 3))
            || ((mode == 2 || mode == 3)
                && (live.secondary != 1 || live.motion != 2
                    || live.wind != 3))
            || (mode != 0 && published)
            || preflight_calls == 0
            || (mode != 1 && restore_calls == 0))
        {
            std::printf("combined carried-state transaction mode %d failed\n",
                mode);
            return 1;
        }
    }
    int secondary_preflights = 0;
    int motion_preflights = 0;
    int wind_preflights = 0;
    if (Horse::RollbackCarriedStateIncludesSecondaryHistory(1)
        || !Horse::RollbackCarriedStateIncludesSecondaryHistory(2)
        || Horse::RollbackPreflightCarriedStateComponents(
            [&]() noexcept { ++secondary_preflights; return false; },
            [&]() noexcept { ++motion_preflights; return true; },
            [&]() noexcept { ++wind_preflights; return true; })
        || secondary_preflights != 1 || motion_preflights != 1
        || wind_preflights != 1)
    {
        std::printf("combined carried-state component preflight skipped\n");
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
    command_a.bytes[0] = 0x11;
    command_b.bytes[0] = 0xEE;
    if (Horse::RollbackFastIntegrityHashBytes(
            command_a.bytes.data(), command_a.bytes.size())
        == Horse::RollbackFastIntegrityHashBytes(
            command_b.bytes.data(), command_b.bytes.size()))
    {
        std::printf("CCpu raw diagnostic did not retain local bytes\n");
        return 1;
    }
    if (Horse::HashRollbackSnapshotRangeCanonical(
            command_a, command_a.ranges[0])
            != Horse::HashRollbackSnapshotRangeCanonical(
                command_b, command_b.ranges[0]))
    {
        std::printf("CCpu range diagnostic pointer exclusion failed\n");
        return 1;
    }
    if (Horse::HashRollbackSnapshotCanonical(command_a)
            != Horse::HashRollbackSnapshotCanonical(command_b))
    {
        std::printf("CCpu canonical pointer exclusion failed\n");
        return 1;
    }
    ++command_b.bytes[0x30];
    if (Horse::HashRollbackSnapshotRangeCanonical(
            command_a, command_a.ranges[0])
            == Horse::HashRollbackSnapshotRangeCanonical(
                command_b, command_b.ranges[0]))
    {
        std::printf("CCpu range diagnostic sensitivity failed\n");
        return 1;
    }
    if (Horse::HashRollbackSnapshotCanonical(command_a)
            == Horse::HashRollbackSnapshotCanonical(command_b))
    {
        std::printf("CCpu canonical gameplay field sensitivity failed\n");
        return 1;
    }

    std::array<uint8_t, 0xC0> live_sched {};
    const uintptr_t live_chara[2] = {0x10101010, 0x20202020};
    const uintptr_t live_subvm[2] = {0x30303030, 0x40404040};
    for (size_t slot = 0; slot < 2; ++slot)
    {
        std::memcpy(live_sched.data() + slot * 0x60 + 0x10,
            &live_chara[slot], sizeof(uintptr_t));
        std::memcpy(live_sched.data() + slot * 0x60 + 0x50,
            &live_subvm[slot], sizeof(uintptr_t));
        live_sched[slot * 0x60 + 0x08] =
            static_cast<uint8_t>(slot + 1);
    }
    Horse::RollbackSnapshotFrame sched_restore {};
    sched_restore.schema_hash = 0xCC03;
    sched_restore.bytes.assign(live_sched.begin(), live_sched.end());
    Horse::RollbackSnapshotRange sched_range {};
    sched_range.address = reinterpret_cast<uintptr_t>(live_sched.data());
    sched_range.bytes = static_cast<uint32_t>(live_sched.size());
    sched_range.hash = Horse::RollbackFastIntegrityHashBytes(
        sched_restore.bytes.data(), sched_restore.bytes.size());
    sched_range.canonical_policy =
        Horse::RollbackCanonicalPolicy::LuxMoveSchedStateArray;
    sched_restore.ranges.push_back(sched_range);
    sched_restore.canonical_hash =
        Horse::HashRollbackSnapshotCanonical(sched_restore);
    sched_restore.integrity_hash =
        Horse::HashRollbackSnapshotFrame(sched_restore);
    sched_restore.hash = sched_restore.integrity_hash;
    live_sched[0x08] = 0xEE;
    live_sched[0x68] = 0xDD;
    const auto sched_ok =
        Horse::RestoreRollbackSnapshotBytesOnce(sched_restore);
    uintptr_t observed_chara = 0;
    uintptr_t observed_subvm = 0;
    std::memcpy(&observed_chara, live_sched.data() + 0x10,
        sizeof(observed_chara));
    std::memcpy(&observed_subvm, live_sched.data() + 0x50,
        sizeof(observed_subvm));
    if (!sched_ok.ok || live_sched[0x08] != 1
        || live_sched[0x68] != 2
        || observed_chara != live_chara[0]
        || observed_subvm != live_subvm[0])
    {
        std::printf("CCpu identity-preserving restore failed\n");
        return 1;
    }
    const uintptr_t replacement_subvm = 0x50505050;
    std::memcpy(live_sched.data() + 0x50, &replacement_subvm,
        sizeof(replacement_subvm));
    const auto sched_rejected =
        Horse::RestoreRollbackSnapshotBytesOnce(sched_restore);
    if (sched_rejected.ok
        || std::strcmp(sched_rejected.failure,
            "lux-move-sched-generation-mismatch") != 0)
    {
        std::printf("CCpu generation change was not rejected\n");
        return 1;
    }

    Horse::RollbackSnapshotFrame world_mode_a {};
    Horse::RollbackSnapshotFrame world_mode_b {};
    world_mode_a.schema_hash = world_mode_b.schema_hash = 0xCC02;
    world_mode_a.bytes.resize(0x14);
    world_mode_b.bytes.resize(0x14);
    world_mode_a.ranges.push_back({});
    world_mode_b.ranges.push_back({});
    constexpr uintptr_t image_a = 0x140000000ull;
    constexpr uintptr_t image_b = 0x7FF700000000ull;
    for (auto pair : {std::pair{&world_mode_a, image_a},
                      std::pair{&world_mode_b, image_b}})
    {
        pair.first->ranges[0].address = pair.second + 0x4843ED0ull;
        pair.first->ranges[0].bytes = 0x14;
        pair.first->ranges[0].canonical_policy =
            Horse::RollbackCanonicalPolicy::LuxBattleWorldModeControl;
        const uintptr_t current = pair.second + 0x4100D88ull;
        const uintptr_t queued = pair.second + 0x4100E60ull;
        const uint32_t transitioned = 1;
        std::memcpy(pair.first->bytes.data(), &current, sizeof(current));
        std::memcpy(pair.first->bytes.data() + 8, &queued, sizeof(queued));
        std::memcpy(pair.first->bytes.data() + 0x10,
                    &transitioned, sizeof(transitioned));
    }
    const uint64_t world_mode_hash =
        Horse::HashRollbackSnapshotCanonical(world_mode_a);
    if (Horse::HashRollbackSnapshotRangeCanonical(
            world_mode_a, world_mode_a.ranges[0])
            != Horse::HashRollbackSnapshotRangeCanonical(
                world_mode_b, world_mode_b.ranges[0]))
    {
        std::printf("world-mode range diagnostic ASLR normalization failed\n");
        return 1;
    }
    if (world_mode_hash == 0 || world_mode_hash !=
            Horse::HashRollbackSnapshotCanonical(world_mode_b))
    {
        std::printf("world-mode ASLR normalization failed\n");
        return 1;
    }
    ++world_mode_b.bytes[0x10];
    if (world_mode_hash ==
            Horse::HashRollbackSnapshotCanonical(world_mode_b))
    {
        std::printf("world-mode control sensitivity failed\n");
        return 1;
    }

    Horse::RollbackSnapshotFrame input_ring_a {};
    Horse::RollbackSnapshotFrame input_ring_b {};
    input_ring_a.schema_hash = input_ring_b.schema_hash = 0xCC03;
    constexpr size_t ring_entries = 0x3D;
    constexpr size_t ring_players = 2;
    constexpr size_t ring_bytes = ring_players * ring_entries
        * sizeof(uint64_t);
    constexpr size_t cursor_bytes = ring_players * sizeof(uint32_t);
    for (auto* frame : {&input_ring_a, &input_ring_b})
    {
        frame->bytes.resize(ring_bytes + cursor_bytes);
        frame->ranges.resize(2);
        frame->ranges[0].manifest_index = 0;
        frame->ranges[0].address = 0x14485E750ull;
        frame->ranges[0].bytes_offset = 0;
        frame->ranges[0].bytes = static_cast<uint32_t>(ring_bytes);
        frame->ranges[0].canonical_policy =
            Horse::RollbackCanonicalPolicy::LuxBattleInputRing;
        frame->ranges[1].manifest_index = 1;
        frame->ranges[1].address = 0x14485EB20ull;
        frame->ranges[1].bytes_offset = ring_bytes;
        frame->ranges[1].bytes = static_cast<uint32_t>(cursor_bytes);
        frame->ranges[1].canonical_policy =
            Horse::RollbackCanonicalPolicy::LuxBattleInputRingCursor;
    }
    const std::array<uint32_t, 2> cursor_a {19, 19};
    const std::array<uint32_t, 2> cursor_b {55, 55};
    std::memcpy(input_ring_a.bytes.data() + ring_bytes,
        cursor_a.data(), cursor_bytes);
    std::memcpy(input_ring_b.bytes.data() + ring_bytes,
        cursor_b.data(), cursor_bytes);
    for (size_t player = 0; player < ring_players; ++player)
    {
        for (size_t logical = 0; logical < ring_entries; ++logical)
        {
            const uint64_t input = 0x100000000ull * (player + 1)
                + logical;
            const size_t physical_a = (cursor_a[player] + logical)
                % ring_entries;
            const size_t physical_b = (cursor_b[player] + logical)
                % ring_entries;
            std::memcpy(input_ring_a.bytes.data()
                    + (player * ring_entries + physical_a)
                        * sizeof(input),
                &input, sizeof(input));
            std::memcpy(input_ring_b.bytes.data()
                    + (player * ring_entries + physical_b)
                        * sizeof(input),
                &input, sizeof(input));
        }
    }
    const uint64_t input_ring_hash =
        Horse::HashRollbackSnapshotCanonical(input_ring_a);
    const uint64_t input_ring_integrity_a =
        Horse::RollbackFastIntegrityHashBytes(
            input_ring_a.bytes.data(), input_ring_a.bytes.size());
    const uint64_t input_ring_integrity_b =
        Horse::RollbackFastIntegrityHashBytes(
            input_ring_b.bytes.data(), input_ring_b.bytes.size());
    if (input_ring_hash == 0 || input_ring_hash !=
            Horse::HashRollbackSnapshotCanonical(input_ring_b)
        || input_ring_integrity_a == input_ring_integrity_b
        || Horse::HashRollbackSnapshotRangeCanonical(
                input_ring_a, input_ring_a.ranges[0]) !=
            Horse::HashRollbackSnapshotRangeCanonical(
                input_ring_b, input_ring_b.ranges[0])
        || Horse::HashRollbackSnapshotRangeCanonical(
                input_ring_a, input_ring_a.ranges[1]) !=
            Horse::HashRollbackSnapshotRangeCanonical(
                input_ring_b, input_ring_b.ranges[1]))
    {
        std::printf("input-ring logical rotation normalization failed\n");
        return 1;
    }
    ++input_ring_b.bytes[(cursor_b[1] % ring_entries)
        * sizeof(uint64_t) + ring_entries * sizeof(uint64_t)];
    if (input_ring_hash ==
            Horse::HashRollbackSnapshotCanonical(input_ring_b))
    {
        std::printf("input-ring logical history sensitivity failed\n");
        return 1;
    }

    Horse::RollbackSnapshotFrame malformed_ring = input_ring_a;
    malformed_ring.ranges.pop_back();
    if (Horse::HashRollbackSnapshotCanonical(malformed_ring) != 0)
    {
        std::printf("input-ring missing cursor did not fail closed\n");
        return 1;
    }

    Horse::RollbackSnapshotFrame cooldown_a {};
    Horse::RollbackSnapshotFrame cooldown_b {};
    static_assert(Horse::RollbackCollisionCooldownRemaining(103, 100) == 1);
    static_assert(Horse::RollbackCollisionCooldownRemaining(104, 100) == 0);
    static_assert(Horse::RollbackCollisionCooldownRemaining(
        0xFFFFFFFCu, 0xFFFFFFFCu) == 4);
    static_assert(Horse::RollbackCollisionCooldownRemaining(
        0xFFFFFFFFu, 0xFFFFFFFCu) == 1);
    static_assert(Horse::RollbackCollisionCooldownRemaining(
        0xFFFFFFFDu, 0xFFFFFFFDu) == 0);
    static_assert(Horse::RollbackCollisionCooldownRemaining(
        0u, 0xFFFFFFFFu) == 0);
    if (Horse::RollbackCollisionCooldownRemaining(103, 100) != 1
        || Horse::RollbackCollisionCooldownRemaining(104, 100) != 0
        || Horse::RollbackCollisionCooldownRemaining(
            0xFFFFFFFCu, 0xFFFFFFFCu) != 4
        || Horse::RollbackCollisionCooldownRemaining(
            0xFFFFFFFFu, 0xFFFFFFFCu) != 1
        || Horse::RollbackCollisionCooldownRemaining(
            0xFFFFFFFDu, 0xFFFFFFFDu) != 0
        || Horse::RollbackCollisionCooldownRemaining(
            0u, 0xFFFFFFFFu) != 0)
    {
        std::printf("collision cooldown boundary arithmetic failed\n");
        return 1;
    }
    cooldown_a.schema_hash = cooldown_b.schema_hash = 0xCC04;
    for (auto* frame : {&cooldown_a, &cooldown_b})
    {
        frame->bytes.resize(3u * sizeof(uint32_t));
        frame->ranges.resize(3);
        frame->ranges[0] = {7, 0x14470D0C4ull, 0,
            sizeof(uint32_t), 0,
            Horse::RollbackCanonicalPolicy::LuxBattleNativeFrameCounter};
        frame->ranges[1] = {8, 0x14470DED8ull,
            sizeof(uint32_t), sizeof(uint32_t), 0,
            Horse::RollbackCanonicalPolicy::LuxBattleCollisionCooldown};
        frame->ranges[2] = {9, 0x1440F3CACull,
            2u * sizeof(uint32_t), sizeof(uint32_t), 0,
            Horse::RollbackCanonicalPolicy::LuxBattleCollisionOwner};
    }
    const auto set_cooldown = [](Horse::RollbackSnapshotFrame& frame,
                                  uint32_t current, uint32_t last,
                                  uint32_t owner) {
        std::memcpy(frame.bytes.data(), &current, sizeof(current));
        std::memcpy(frame.bytes.data() + sizeof(uint32_t),
            &last, sizeof(last));
        std::memcpy(frame.bytes.data() + 2u * sizeof(uint32_t),
            &owner, sizeof(owner));
        for (auto& range : frame.ranges)
        {
            range.hash = Horse::RollbackFastIntegrityHashBytes(
                frame.bytes.data() + range.bytes_offset, range.bytes);
        }
    };
    set_cooldown(cooldown_a, 3387, 3385, 1);
    set_cooldown(cooldown_b, 3424, 3422, 1);
    const uint64_t cooldown_hash =
        Horse::HashRollbackSnapshotCanonical(cooldown_a);
    if (cooldown_hash == 0 || cooldown_hash !=
            Horse::HashRollbackSnapshotCanonical(cooldown_b)
        || Horse::HashRollbackSnapshotFrame(cooldown_a)
            == Horse::HashRollbackSnapshotFrame(cooldown_b))
    {
        std::printf("equivalent collision cooldown normalization failed\n");
        return 1;
    }
    set_cooldown(cooldown_b, 3424, 3423, 1);
    if (cooldown_hash == Horse::HashRollbackSnapshotCanonical(cooldown_b))
    {
        std::printf("collision cooldown remaining sensitivity failed\n");
        return 1;
    }
    set_cooldown(cooldown_b, 3424, 3422, 2);
    if (cooldown_hash == Horse::HashRollbackSnapshotCanonical(cooldown_b))
    {
        std::printf("collision cooldown owner sensitivity failed\n");
        return 1;
    }
    set_cooldown(cooldown_a, 3387, 3383, 1);
    set_cooldown(cooldown_b, 3424, 3420, 2);
    const uint64_t expired_owner_hash =
        Horse::HashRollbackSnapshotCanonical(cooldown_a);
    if (expired_owner_hash == 0 || expired_owner_hash !=
            Horse::HashRollbackSnapshotCanonical(cooldown_b)
        || Horse::HashRollbackSnapshotFrame(cooldown_a)
            == Horse::HashRollbackSnapshotFrame(cooldown_b))
    {
        std::printf("expired collision owner normalization failed\n");
        return 1;
    }
    Horse::RollbackSnapshotFrame missing_cooldown_counter = cooldown_a;
    missing_cooldown_counter.ranges.erase(
        missing_cooldown_counter.ranges.begin());
    if (Horse::HashRollbackSnapshotCanonical(
            missing_cooldown_counter) != 0)
    {
        std::printf("collision cooldown missing counter did not fail closed\n");
        return 1;
    }
    Horse::RollbackSnapshotFrame missing_cooldown_owner = cooldown_a;
    missing_cooldown_owner.ranges.pop_back();
    if (Horse::HashRollbackSnapshotCanonical(
            missing_cooldown_owner) != 0)
    {
        std::printf("collision cooldown missing owner did not fail closed\n");
        return 1;
    }

    Horse::RollbackSnapshotFrame gameplay_globals_a {};
    Horse::RollbackSnapshotFrame gameplay_globals_b {};
    gameplay_globals_a.schema_hash = gameplay_globals_b.schema_hash = 0xCC05;
    for (auto* frame : {&gameplay_globals_a, &gameplay_globals_b})
    {
        frame->bytes.resize(2u * sizeof(uint32_t) + sizeof(uint8_t));
        frame->ranges.push_back({10, 0x14470DED0ull, 0,
            2u * sizeof(uint32_t), 0,
            Horse::RollbackCanonicalPolicy::AllBytes});
        frame->ranges.push_back({11, 0x14470DEDCull,
            2u * sizeof(uint32_t), sizeof(uint8_t), 0,
            Horse::RollbackCanonicalPolicy::AllBytes});
    }
    const uint64_t gameplay_globals_hash =
        Horse::HashRollbackSnapshotCanonical(gameplay_globals_a);
    ++gameplay_globals_b.bytes[0];
    if (gameplay_globals_hash ==
            Horse::HashRollbackSnapshotCanonical(gameplay_globals_b))
    {
        std::printf("input-ring base difference became noncanonical\n");
        return 1;
    }
    gameplay_globals_b.bytes = gameplay_globals_a.bytes;
    ++gameplay_globals_b.bytes.back();
    if (gameplay_globals_hash ==
            Horse::HashRollbackSnapshotCanonical(gameplay_globals_b))
    {
        std::printf("active frame-context difference became noncanonical\n");
        return 1;
    }

    std::vector<uint8_t> cooldown_live(0x61A230u);
    const uintptr_t owner_address = reinterpret_cast<uintptr_t>(
        cooldown_live.data());
    const uintptr_t counter_address = owner_address + 0x619418u;
    const uintptr_t cooldown_address = counter_address + 0xE14u;
    uint32_t live_owner = 1;
    uint32_t live_counter = 3387;
    uint32_t live_cooldown = 3385;
    std::memcpy(reinterpret_cast<void*>(owner_address), &live_owner, 4);
    std::memcpy(reinterpret_cast<void*>(counter_address), &live_counter, 4);
    std::memcpy(reinterpret_cast<void*>(cooldown_address), &live_cooldown, 4);
    Horse::RollbackSnapshotManifest cooldown_manifest {};
    cooldown_manifest.image_base = counter_address - 0x470D0C4u;
    cooldown_manifest.entries.push_back({"cooldown owner", owner_address,
        0, 4, Horse::RollbackCoverage::ExplicitSnapshot, "selftest",
        Horse::RollbackCanonicalPolicy::LuxBattleCollisionOwner,
        Horse::RollbackCoverageCapabilityId::ExplicitState});
    cooldown_manifest.entries.push_back({"native frame", counter_address,
        0, 4, Horse::RollbackCoverage::ExplicitSnapshot, "selftest",
        Horse::RollbackCanonicalPolicy::LuxBattleNativeFrameCounter,
        Horse::RollbackCoverageCapabilityId::ExplicitState});
    cooldown_manifest.entries.push_back({"collision cooldown", cooldown_address,
        0, 4, Horse::RollbackCoverage::ExplicitSnapshot, "selftest",
        Horse::RollbackCanonicalPolicy::LuxBattleCollisionCooldown,
        Horse::RollbackCoverageCapabilityId::ExplicitState});
    Horse::RollbackSnapshotFrame cooldown_captured {};
    const auto cooldown_capture = Horse::CaptureRollbackSnapshotBytes(
        cooldown_manifest, cooldown_captured);
    live_owner = 2;
    live_counter = 4000;
    live_cooldown = 3999;
    std::memcpy(reinterpret_cast<void*>(owner_address), &live_owner, 4);
    std::memcpy(reinterpret_cast<void*>(counter_address), &live_counter, 4);
    std::memcpy(reinterpret_cast<void*>(cooldown_address), &live_cooldown, 4);
    const auto cooldown_restore =
        Horse::RestoreFreshRollbackSnapshotBytesOnce(cooldown_captured);
    Horse::RollbackSnapshotFrame cooldown_recaptured {};
    const auto cooldown_recapture = Horse::CaptureRollbackSnapshotBytes(
        cooldown_manifest, cooldown_recaptured);
    std::memcpy(&live_owner, reinterpret_cast<void*>(owner_address), 4);
    std::memcpy(&live_counter, reinterpret_cast<void*>(counter_address), 4);
    std::memcpy(&live_cooldown, reinterpret_cast<void*>(cooldown_address), 4);
    if (!cooldown_capture.ok || !cooldown_restore.ok
        || !cooldown_recapture.ok || live_owner != 1
        || live_counter != 3387 || live_cooldown != 3385
        || cooldown_recaptured.integrity_hash
            != cooldown_captured.integrity_hash
        || cooldown_recaptured.canonical_hash
            != cooldown_captured.canonical_hash)
    {
        std::printf("collision cooldown raw restore verification failed\n");
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
    Horse::RollbackLifecycleEpoch stock_armed = manifest.epoch;
    stock_armed.presence = 8;
    stock_armed.native_stage_identity = 0x10009;
    stock_armed.round_ordinal = 3;
    stock_armed.input_log_frame = 20;
    Horse::RollbackLifecycleEpoch stock_next = stock_armed;
    stock_next.round_ordinal = 4;
    stock_next.input_log_frame = 0;
    Horse::RollbackLifecycleEpoch stock_first = stock_next;
    stock_first.input_log_frame = 1;
    Horse::RollbackLifecycleEpoch stock_late = stock_next;
    stock_late.input_log_frame = 21;
    Horse::RollbackLifecycleEpoch stock_current = stock_armed;
    stock_current.input_log_frame = 24;
    Horse::RollbackLifecycleEpoch stock_ranked = stock_next;
    stock_ranked.presence = 7;
    Horse::RollbackLifecycleEpoch stock_other_match = stock_next;
    ++stock_other_match.battle_manager;
    if (!Horse::RollbackStockAttachObservedMatchSame(
            stock_armed, stock_next)
        || Horse::RollbackStockAttachBoundaryEligible(
            stock_armed, stock_next, false)
        || Horse::RollbackStockAttachBoundaryEligible(
            stock_armed, stock_first, false)
        || !Horse::RollbackStockAttachBoundaryEligible(
            stock_armed, stock_current, true)
        || Horse::RollbackStockAttachBoundaryEligible(
            stock_armed, stock_late, false)
        || Horse::RollbackStockAttachBoundaryEligible(
            stock_armed, stock_ranked, false)
        || Horse::RollbackStockAttachBoundaryEligible(
            stock_armed, stock_other_match, false))
    {
        std::printf("stock attach boundary characterization failed\n");
        return 1;
    }
    manifest.entries.push_back({
        "test range a",
        reinterpret_cast<uintptr_t>(range_a.data()),
        0,
        static_cast<uint32_t>(range_a.size()),
        Horse::RollbackCoverage::ExplicitSnapshot,
        "self-test explicit range",
        Horse::RollbackCanonicalPolicy::AllBytes,
        Horse::RollbackCoverageCapabilityId::ExplicitState
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
        "self-test explicit offset range",
        Horse::RollbackCanonicalPolicy::AllBytes,
        Horse::RollbackCoverageCapabilityId::ExplicitState
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

    Horse::RollbackSnapshotFrame bounded_snapshot {};
    bounded_snapshot.bytes.reserve(1);
    bounded_snapshot.ranges.reserve(1);
    const bool bounded_preflight_refused =
        !Horse::RollbackSnapshotPreallocatedCaptureReady(
            manifest, bounded_snapshot);
    const auto* refused_bytes = bounded_snapshot.bytes.data();
    const auto* refused_ranges = bounded_snapshot.ranges.data();
    const size_t refused_bytes_capacity = bounded_snapshot.bytes.capacity();
    const size_t refused_ranges_capacity = bounded_snapshot.ranges.capacity();
    const auto bounded_refused = Horse::CaptureRollbackSnapshotBytes(
        manifest, bounded_snapshot, true);
    const bool bounded_refusal_preserved =
        refused_bytes == bounded_snapshot.bytes.data()
        && refused_ranges == bounded_snapshot.ranges.data()
        && refused_bytes_capacity == bounded_snapshot.bytes.capacity()
        && refused_ranges_capacity == bounded_snapshot.ranges.capacity();
    bounded_snapshot.bytes.reserve(capture.copied_bytes);
    bounded_snapshot.ranges.reserve(capture.copied_entries);
    const bool bounded_preflight_ready =
        Horse::RollbackSnapshotPreallocatedCaptureReady(
            manifest, bounded_snapshot);
    const auto bounded_capture = Horse::CaptureRollbackSnapshotBytes(
        manifest, bounded_snapshot, true);
    const auto* bounded_bytes = bounded_snapshot.bytes.data();
    const auto* bounded_ranges = bounded_snapshot.ranges.data();
    const auto bounded_recapture = Horse::CaptureRollbackSnapshotBytes(
        manifest, bounded_snapshot, true);
    if (!bounded_preflight_refused || !bounded_preflight_ready
        || bounded_refused.ok
        || std::strcmp(bounded_refused.failure,
            "snapshot-preallocated-capacity-exceeded") != 0
        || !bounded_refusal_preserved
        || !bounded_capture.ok || !bounded_recapture.ok
        || bounded_snapshot.bytes.data() != bounded_bytes
        || bounded_snapshot.ranges.data() != bounded_ranges)
    {
        std::printf("bounded capture failed refused=%s capture=%s recapture=%s\n",
            bounded_refused.failure, bounded_capture.failure,
            bounded_recapture.failure);
        return 1;
    }

    Horse::RollbackBreakableStageSnapshot stage_identity {};
    stage_identity.stage_actor_manager = 1;
    stage_identity.stage_layout_digest = 2;
    stage_identity.actor_set_digest = 3;
    stage_identity.records.resize(1);
    Horse::RollbackBreakableStageSnapshot bounded_stage {};
    const bool stage_preflight_refused =
        !Horse::RollbackBreakableStagePreallocatedCaptureReady(
            stage_identity, bounded_stage);
    bounded_stage.records.reserve(1);
    if (!stage_preflight_refused
        || !Horse::RollbackBreakableStagePreallocatedCaptureReady(
            stage_identity, bounded_stage))
    {
        std::printf("stage bounded capture preflight failed\n");
        return 1;
    }

    range_a.fill(0xEE);
    for (size_t i = 8; i < 24; ++i)
        range_b[i] = 0xDD;

    const auto fresh_restore =
        Horse::RestoreFreshRollbackSnapshotBytesOnce(snapshot);
    if (!fresh_restore.ok || fresh_restore.copied_entries != 2
        || fresh_restore.emergency_captured
        || fresh_restore.verification_ok
        || !bytes_equal(range_a.data(), original_a.data(), range_a.size())
        || !bytes_equal(range_b.data() + 8, original_b.data() + 8, 16))
    {
        std::printf("fresh restore failed ok=%d entries=%u failure=%s\n",
                    fresh_restore.ok ? 1 : 0,
                    fresh_restore.copied_entries,
                    fresh_restore.failure);
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
        "self-test overlap",
        Horse::RollbackCanonicalPolicy::AllBytes,
        Horse::RollbackCoverageCapabilityId::ExplicitState});
    const auto overlap_report =
        Horse::ValidateRollbackSnapshotManifest(overlap, false);
    const auto pending_report =
        Horse::ValidateRollbackSnapshotManifest(manifest, true);
    Horse::RollbackSnapshotManifest missing_capability = manifest;
    missing_capability.entries[0].capability =
        Horse::RollbackCoverageCapabilityId::None;
    const auto missing_capability_report =
        Horse::ValidateRollbackSnapshotManifest(missing_capability, false);
    bool incomplete_stage_rejected = true;
    const Horse::RollbackCoverageCapability complete_capability {
        Horse::RollbackCoverageCapabilityId::ExplicitState,
        Horse::RollbackCoverageStageCapture
            | Horse::RollbackCoverageStageRestore
            | Horse::RollbackCoverageStageVerify
            | Horse::RollbackCoverageStageTest,
        Horse::RollbackCoverageStageCapture
            | Horse::RollbackCoverageStageRestore
            | Horse::RollbackCoverageStageVerify
            | Horse::RollbackCoverageStageTest,
        "coverage-stage-selftest"};
    incomplete_stage_rejected =
        Horse::RollbackCoverageCapabilityComplete(complete_capability);
    for (uint32_t stage : {
             Horse::RollbackCoverageStageCapture,
             Horse::RollbackCoverageStageRestore,
             Horse::RollbackCoverageStageVerify,
             Horse::RollbackCoverageStageTest})
    {
        Horse::RollbackCoverageCapability incomplete = complete_capability;
        incomplete.implemented_stages &= ~stage;
        incomplete_stage_rejected = incomplete_stage_rejected
            && !Horse::RollbackCoverageCapabilityComplete(incomplete);
    }
    Horse::RollbackCoverageCapability unnamed = complete_capability;
    unnamed.test_name = "";
    incomplete_stage_rejected = incomplete_stage_rejected
        && !Horse::RollbackCoverageCapabilityComplete(unnamed);
    if (overlap_report.ok || overlap_report.overlapping_entries == 0
        || pending_report.ok
        || pending_report.pending_gameplay_entries != 1
        || missing_capability_report.ok
        || missing_capability_report.missing_capability_entries != 1
        || !incomplete_stage_rejected)
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
    uint32_t supported_presentation_entries = 0;
    for (const Horse::RollbackManifestEntry& entry
         : production_manifest.entries)
    {
        if (entry.coverage == Horse::RollbackCoverage::DynamicSnapshot
            && entry.capability
                == Horse::RollbackCoverageCapabilityId::PresentationDispatch
            && std::strcmp(
                entry.name,
                "Presentation object lifetime and thread affinity") == 0)
        {
            ++supported_presentation_entries;
        }
    }
    Horse::RollbackSnapshotManifest unpaired_ring_manifest =
        production_manifest;
    for (Horse::RollbackManifestEntry& entry
         : unpaired_ring_manifest.entries)
    {
        if (entry.canonical_policy
            == Horse::RollbackCanonicalPolicy::LuxBattleInputRingCursor)
        {
            entry.canonical_policy =
                Horse::RollbackCanonicalPolicy::AllBytes;
        }
    }
    const auto unpaired_ring_validation =
        Horse::ValidateRollbackSnapshotManifest(
            unpaired_ring_manifest, true);
    Horse::RollbackSnapshotManifest hidden_gameplay_manifest =
        production_manifest;
    for (Horse::RollbackManifestEntry& entry
         : hidden_gameplay_manifest.entries)
    {
        if (std::strcmp(entry.name,
                "g_LuxBattle_InputRingBaseOffset_PerPlayer") == 0)
        {
            entry.canonical_policy =
                Horse::RollbackCanonicalPolicy::
                    LuxBattleCollisionCooldown;
        }
    }
    const auto hidden_gameplay_validation =
        Horse::ValidateRollbackSnapshotManifest(
            hidden_gameplay_manifest, true);
    Horse::RollbackSnapshotManifest changed_abi = production_manifest;
    ++changed_abi.version;
    bool round_result_flow_covered = false;
    bool active_mode_state_exact = false;
    bool round_result_mode_state_exact = false;
    bool native_frame_counter_semantic = false;
    bool input_ring_base_exact = false;
    bool collision_cooldown_semantic = false;
    bool active_frame_context_exact = false;
    bool collision_owner_semantic = false;
    uint32_t rng_lcg_manifest_entries = 0;
    uint32_t rng_lfsr_manifest_entries = 0;
    uint32_t rng_lfsr_index_manifest_entries = 0;
    bool rng_lcg_manifest_exact = false;
    bool rng_lfsr_manifest_exact = false;
    bool rng_lfsr_index_manifest_exact = false;
    for (const Horse::RollbackManifestEntry& entry
         : production_manifest.entries)
    {
        if (std::strcmp(entry.name,
                        "g_LuxBattleRoundControlScalars") == 0)
        {
            round_result_flow_covered =
                entry.address == 0x14484639Cull
                && entry.offset == 0
                && entry.bytes == 0x14
                && entry.coverage
                    == Horse::RollbackCoverage::ExplicitSnapshot;
        }
        else if (std::strcmp(entry.name,
                    "g_LuxBattleWorldMode_Active_Mutable") == 0)
        {
            active_mode_state_exact =
                entry.address == 0x144100B40ull
                && entry.offset == 0
                && entry.bytes == 0x20
                && entry.coverage
                    == Horse::RollbackCoverage::ExplicitSnapshot
                && entry.canonical_policy
                    == Horse::RollbackCanonicalPolicy::AllBytes;
        }
        else if (std::strcmp(entry.name,
                    "g_LuxBattleWorldMode_RoundResult_Mutable") == 0)
        {
            round_result_mode_state_exact =
                entry.address == 0x144100D90ull
                && entry.offset == 0
                && entry.bytes == 0x30
                && entry.coverage
                    == Horse::RollbackCoverage::ExplicitSnapshot
                && entry.canonical_policy
                    == Horse::RollbackCanonicalPolicy::AllBytes;
        }
        else if (std::strcmp(entry.name,
                    "g_LuxBattle_InputRingBaseOffset_PerPlayer") == 0)
        {
            input_ring_base_exact = entry.address == 0x14470DED0ull
                && entry.bytes == 2u * sizeof(uint32_t)
                && entry.canonical_policy
                    == Horse::RollbackCanonicalPolicy::AllBytes;
        }
        else if (std::strcmp(entry.name,
                    "g_LuxBattle_FrameCounter") == 0)
        {
            native_frame_counter_semantic = entry.address == 0x14470D0C4ull
                && entry.bytes == sizeof(uint32_t)
                && entry.canonical_policy
                    == Horse::RollbackCanonicalPolicy::
                        LuxBattleNativeFrameCounter;
        }
        else if (std::strcmp(entry.name,
                    "g_LuxBattle_CollisionLastDispatchFrame") == 0)
        {
            collision_cooldown_semantic = entry.address == 0x14470DED8ull
                && entry.bytes == sizeof(uint32_t)
                && entry.canonical_policy
                    == Horse::RollbackCanonicalPolicy::
                        LuxBattleCollisionCooldown;
        }
        else if (std::strcmp(entry.name,
                    "g_LuxBattle_ActiveFrameContextIndex") == 0)
        {
            active_frame_context_exact = entry.address == 0x14470DEDCull
                && entry.bytes == sizeof(uint8_t)
                && entry.canonical_policy
                    == Horse::RollbackCanonicalPolicy::AllBytes;
        }
        else if (std::strcmp(entry.name,
                    "g_LuxBattle_CollisionLastDispatchOwner") == 0)
        {
            collision_owner_semantic = entry.address == 0x1440F3CACull
                && entry.bytes == sizeof(uint32_t)
                && entry.canonical_policy
                    == Horse::RollbackCanonicalPolicy::
                        LuxBattleCollisionOwner;
        }
        else if (std::strcmp(entry.name,
                    "g_dwLuxBattleLcgRngState") == 0)
        {
            ++rng_lcg_manifest_entries;
            rng_lcg_manifest_exact = entry.address == 0x14485EB28ull
                && entry.offset == 0
                && entry.bytes == sizeof(uint32_t)
                && entry.coverage
                    == Horse::RollbackCoverage::ExplicitSnapshot
                && entry.canonical_policy
                    == Horse::RollbackCanonicalPolicy::AllBytes
                && entry.capability
                    == Horse::RollbackCoverageCapabilityId::ExplicitState;
        }
        else if (std::strcmp(entry.name,
                    "g_adwLuxBattleLfsrState") == 0)
        {
            ++rng_lfsr_manifest_entries;
            rng_lfsr_manifest_exact = entry.address == 0x14485EB30ull
                && entry.offset == 0
                && entry.bytes == 100
                && entry.coverage
                    == Horse::RollbackCoverage::ExplicitSnapshot
                && entry.canonical_policy
                    == Horse::RollbackCanonicalPolicy::AllBytes
                && entry.capability
                    == Horse::RollbackCoverageCapabilityId::ExplicitState;
        }
        else if (std::strcmp(entry.name,
                    "g_dwLuxBattleLfsrIndex") == 0)
        {
            ++rng_lfsr_index_manifest_entries;
            rng_lfsr_index_manifest_exact =
                entry.address == 0x14485EB94ull
                && entry.offset == 0
                && entry.bytes == sizeof(uint32_t)
                && entry.coverage
                    == Horse::RollbackCoverage::ExplicitSnapshot
                && entry.canonical_policy
                    == Horse::RollbackCanonicalPolicy::AllBytes
                && entry.capability
                    == Horse::RollbackCoverageCapabilityId::ExplicitState;
        }
    }
    const bool rng_manifest_exact =
        rng_lcg_manifest_entries == 1
        && rng_lfsr_manifest_entries == 1
        && rng_lfsr_index_manifest_entries == 1
        && rng_lcg_manifest_exact
        && rng_lfsr_manifest_exact
        && rng_lfsr_index_manifest_exact;
    if (!rng_manifest_exact)
    {
        std::printf(
            "production RNG manifest characterization failed "
            "lcg=%u/%d lfsr=%u/%d index=%u/%d\n",
            rng_lcg_manifest_entries, rng_lcg_manifest_exact ? 1 : 0,
            rng_lfsr_manifest_entries, rng_lfsr_manifest_exact ? 1 : 0,
            rng_lfsr_index_manifest_entries,
            rng_lfsr_index_manifest_exact ? 1 : 0);
        return 1;
    }

    struct RngSnapshotFixture
    {
        uint32_t lcg {0};
        uint32_t native_padding {0};
        std::array<uint8_t, 100> lfsr {};
        uint32_t index {0};
    };
    RngSnapshotFixture rng_fixture {};
    rng_fixture.lcg = 0x13579BDFu;
    rng_fixture.native_padding = 0xDEADBEEFu;
    for (size_t i = 0; i < rng_fixture.lfsr.size(); ++i)
    {
        rng_fixture.lfsr[i] = static_cast<uint8_t>(
            (i * 37u + 0x5Bu) & 0xFFu);
    }
    rng_fixture.index = 5;
    const RngSnapshotFixture expected_rng = rng_fixture;

    Horse::RollbackSnapshotManifest rng_manifest {};
    rng_manifest.version = 0x524E4754u;
    rng_manifest.max_rollback_frames = 60;
    rng_manifest.entries.push_back({
        "g_dwLuxBattleLcgRngState",
        reinterpret_cast<uintptr_t>(&rng_fixture.lcg),
        0,
        sizeof(rng_fixture.lcg),
        Horse::RollbackCoverage::ExplicitSnapshot,
        "self-test RNG LCG state",
        Horse::RollbackCanonicalPolicy::AllBytes,
        Horse::RollbackCoverageCapabilityId::ExplicitState});
    rng_manifest.entries.push_back({
        "g_adwLuxBattleLfsrState",
        reinterpret_cast<uintptr_t>(rng_fixture.lfsr.data()),
        0,
        static_cast<uint32_t>(rng_fixture.lfsr.size()),
        Horse::RollbackCoverage::ExplicitSnapshot,
        "self-test RNG LFSR state",
        Horse::RollbackCanonicalPolicy::AllBytes,
        Horse::RollbackCoverageCapabilityId::ExplicitState});
    rng_manifest.entries.push_back({
        "g_dwLuxBattleLfsrIndex",
        reinterpret_cast<uintptr_t>(&rng_fixture.index),
        0,
        sizeof(rng_fixture.index),
        Horse::RollbackCoverage::ExplicitSnapshot,
        "self-test RNG LFSR index",
        Horse::RollbackCanonicalPolicy::AllBytes,
        Horse::RollbackCoverageCapabilityId::ExplicitState});

    Horse::RollbackSnapshotFrame rng_snapshot {};
    const auto rng_capture =
        Horse::CaptureRollbackSnapshotBytes(rng_manifest, rng_snapshot);
    rng_fixture.lcg = 0x2468ACE0u;
    rng_fixture.lfsr.fill(0xCC);
    rng_fixture.index = 8;
    const auto rng_restore =
        Horse::RestoreRollbackSnapshotBytes(rng_snapshot);
    Horse::RollbackSnapshotFrame rng_recaptured {};
    const auto rng_recapture =
        Horse::CaptureRollbackSnapshotBytes(rng_manifest, rng_recaptured);
    if (!rng_capture.ok || rng_capture.copied_entries != 3
        || rng_capture.copied_bytes != 108
        || !rng_restore.ok || !rng_restore.verification_ok
        || !rng_recapture.ok
        || rng_fixture.lcg != expected_rng.lcg
        || rng_fixture.lfsr != expected_rng.lfsr
        || rng_fixture.index != expected_rng.index
        || rng_snapshot.canonical_hash != rng_recaptured.canonical_hash
        || rng_snapshot.integrity_hash != rng_recaptured.integrity_hash)
    {
        std::printf(
            "RNG snapshot round-trip characterization failed "
            "capture=%d/%u/%u restore=%d/%d recapture=%d "
            "lcg=0x%08X index=%u\n",
            rng_capture.ok ? 1 : 0,
            rng_capture.copied_entries,
            rng_capture.copied_bytes,
            rng_restore.ok ? 1 : 0,
            rng_restore.verification_ok ? 1 : 0,
            rng_recapture.ok ? 1 : 0,
            rng_fixture.lcg,
            rng_fixture.index);
        return 1;
    }

    rng_fixture.index ^= 3u;
    Horse::RollbackSnapshotFrame rng_index_changed {};
    const auto rng_index_capture =
        Horse::CaptureRollbackSnapshotBytes(
            rng_manifest, rng_index_changed);
    rng_fixture = expected_rng;
    rng_fixture.lfsr[37] ^= 0x80u;
    Horse::RollbackSnapshotFrame rng_lfsr_changed {};
    const auto rng_lfsr_capture =
        Horse::CaptureRollbackSnapshotBytes(
            rng_manifest, rng_lfsr_changed);
    rng_fixture = expected_rng;
    rng_fixture.lcg ^= 0x80000000u;
    Horse::RollbackSnapshotFrame rng_lcg_changed {};
    const auto rng_lcg_capture =
        Horse::CaptureRollbackSnapshotBytes(
            rng_manifest, rng_lcg_changed);
    rng_fixture = expected_rng;
    if (!rng_index_capture.ok || !rng_lfsr_capture.ok
        || !rng_lcg_capture.ok
        || rng_index_changed.canonical_hash
            == rng_snapshot.canonical_hash
        || rng_lfsr_changed.canonical_hash
            == rng_snapshot.canonical_hash
        || rng_lcg_changed.canonical_hash
            == rng_snapshot.canonical_hash)
    {
        std::printf(
            "RNG canonical sensitivity characterization failed "
            "index=%d lfsr=%d lcg=%d\n",
            rng_index_capture.ok
                && rng_index_changed.canonical_hash
                    != rng_snapshot.canonical_hash ? 1 : 0,
            rng_lfsr_capture.ok
                && rng_lfsr_changed.canonical_hash
                    != rng_snapshot.canonical_hash ? 1 : 0,
            rng_lcg_capture.ok
                && rng_lcg_changed.canonical_hash
                    != rng_snapshot.canonical_hash ? 1 : 0);
        return 1;
    }

    struct WorldModeMutableTailFixture
    {
        uint64_t vtable {0};
        std::array<uint8_t, 0x20> mutable_tail {};
    };
    WorldModeMutableTailFixture world_mode_fixture {};
    world_mode_fixture.vtable = 0x140ABCDEFu;
    for (size_t i = 0; i < world_mode_fixture.mutable_tail.size(); ++i)
        world_mode_fixture.mutable_tail[i] =
            static_cast<uint8_t>(i * 13u + 7u);
    const auto expected_world_mode_tail =
        world_mode_fixture.mutable_tail;
    Horse::RollbackSnapshotManifest world_mode_manifest {};
    world_mode_manifest.version = 0x574D4F44u;
    world_mode_manifest.max_rollback_frames = 60;
    world_mode_manifest.entries.push_back({
        "world-mode mutable tail",
        reinterpret_cast<uintptr_t>(
            world_mode_fixture.mutable_tail.data()),
        0,
        static_cast<uint32_t>(
            world_mode_fixture.mutable_tail.size()),
        Horse::RollbackCoverage::ExplicitSnapshot,
        "self-test excludes immutable vtable and owns mutable tail",
        Horse::RollbackCanonicalPolicy::AllBytes,
        Horse::RollbackCoverageCapabilityId::ExplicitState});
    Horse::RollbackSnapshotFrame world_mode_baseline {};
    const auto world_mode_baseline_capture =
        Horse::CaptureRollbackSnapshotBytes(
            world_mode_manifest, world_mode_baseline);
    world_mode_fixture.vtable ^= 0x100000u;
    Horse::RollbackSnapshotFrame world_mode_vtable_changed {};
    const auto world_mode_vtable_capture =
        Horse::CaptureRollbackSnapshotBytes(
            world_mode_manifest, world_mode_vtable_changed);
    world_mode_fixture.mutable_tail[0] ^= 0x5Au;
    Horse::RollbackSnapshotFrame world_mode_tail_changed {};
    const auto world_mode_tail_capture =
        Horse::CaptureRollbackSnapshotBytes(
            world_mode_manifest, world_mode_tail_changed);
    const uint64_t live_vtable_before_restore =
        world_mode_fixture.vtable;
    const auto world_mode_restore =
        Horse::RestoreRollbackSnapshotBytesOnce(world_mode_baseline);
    if (!world_mode_baseline_capture.ok
        || !world_mode_vtable_capture.ok
        || !world_mode_tail_capture.ok
        || world_mode_vtable_changed.canonical_hash
            != world_mode_baseline.canonical_hash
        || world_mode_tail_changed.canonical_hash
            == world_mode_baseline.canonical_hash
        || !world_mode_restore.ok
        || world_mode_fixture.vtable != live_vtable_before_restore
        || world_mode_fixture.mutable_tail != expected_world_mode_tail)
    {
        std::printf(
            "world-mode mutable-tail snapshot contract failed "
            "base=%d vtable=%d tail=%d restore=%d\n",
            world_mode_baseline_capture.ok ? 1 : 0,
            world_mode_vtable_capture.ok ? 1 : 0,
            world_mode_tail_capture.ok ? 1 : 0,
            world_mode_restore.ok ? 1 : 0);
        return 1;
    }

    const bool production_gate = production_validation.live_ready
        && production_validation.ok
        && production_validation.pending_gameplay_entries == 0
        && supported_presentation_entries == 1
        && !unpaired_ring_validation.ok
        && unpaired_ring_validation.invalid_entries != 0
        && !hidden_gameplay_validation.ok
        && hidden_gameplay_validation.invalid_entries != 0;
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
    const Horse::RollbackBarrierPresentationVisibility barrier_intact =
        Horse::ComputeRollbackBarrierPresentationVisibility(1, 3);
    const Horse::RollbackBarrierPresentationVisibility barrier_broken =
        Horse::ComputeRollbackBarrierPresentationVisibility(3, 3);
    const Horse::RollbackBarrierPresentationVisibility barrier_invalid =
        Horse::ComputeRollbackBarrierPresentationVisibility(0, 0);
    Horse::RollbackBreakableStageSnapshot stage_a {};
    stage_a.stage_actor_manager = 0x1000;
    stage_a.records.push_back({
        Horse::RollbackBreakableActorKind::Wall,
        7,
        0,
        0x2000,
        0x5000,
        0x7000,
        0x8000,
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
    Horse::RollbackBreakableStageSnapshot stage_vtable_drift = stage_a;
    stage_vtable_drift.records[0].vtable = 0x6000;
    Horse::HashRollbackBreakableStageSnapshot(stage_vtable_drift);
    Horse::RollbackBreakableStageSnapshot stage_root_drift = stage_a;
    stage_root_drift.records[0].root_component = 0x7100;
    Horse::HashRollbackBreakableStageSnapshot(stage_root_drift);
    static Horse::RollbackBreakablePresentationHistory<128, 4>
        presentation_history {};
    const bool history_recorded = presentation_history.record(
        41, 0, stage_a);
    for (uint32_t frame = 1; frame <= 31; ++frame)
        presentation_history.record(41, frame, stage_a);
    const auto* delayed_confirmed = presentation_history.find(41, 0);
    Horse::RollbackBreakableStageSnapshot corrected_stage = stage_a;
    corrected_stage.records[0].scalar = 2;
    corrected_stage.records[0].fade_timer = 1.0f;
    corrected_stage.records[0].fade_rate = 0.0f;
    Horse::HashRollbackBreakableStageSnapshot(corrected_stage);
    const bool corrected_recorded = presentation_history.record(
        41, 31, corrected_stage);
    const auto* corrected_confirmed = presentation_history.find(41, 31);
    const bool presentation_history_contract = history_recorded
        && delayed_confirmed && delayed_confirmed->count == 1
        && delayed_confirmed->values[0].scalar == 1
        && corrected_recorded && corrected_confirmed
        && corrected_confirmed->canonical_hash
            == corrected_stage.canonical_hash
        && corrected_confirmed->values[0].scalar == 2
        && !presentation_history.find(42, 31);
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
        && barrier_intact.valid && barrier_intact.face_visible
        && barrier_intact.back_visible
        && !barrier_intact.breaking_visible
        && barrier_broken.valid && !barrier_broken.face_visible
        && !barrier_broken.back_visible
        && barrier_broken.breaking_visible
        && !barrier_invalid.valid
        && stage_a.canonical_hash == stage_b.canonical_hash
        && stage_a.integrity_hash != stage_b.integrity_hash
        && stage_a.stage_layout_digest
            == stage_vtable_drift.stage_layout_digest
        && stage_a.canonical_hash == stage_vtable_drift.canonical_hash
        && stage_a.actor_set_digest
            != stage_vtable_drift.actor_set_digest
        && stage_a.integrity_hash
            != stage_vtable_drift.integrity_hash
        && stage_a.stage_layout_digest
            == stage_root_drift.stage_layout_digest
        && stage_a.canonical_hash == stage_root_drift.canonical_hash
        && stage_a.actor_set_digest
            != stage_root_drift.actor_set_digest
        && stage_a.integrity_hash
            != stage_root_drift.integrity_hash
        && presentation_history_contract;
    Horse::RollbackHgCpuSnapshotFrame hgcpu_integrity_components {};
    hgcpu_integrity_components.byte_hash = 0x11;
    hgcpu_integrity_components.khit_topology_hash = 0x22;
    hgcpu_integrity_components.motion_bank_hash = 0x33;
    hgcpu_integrity_components.motion_tail_hash = 0x44;
    hgcpu_integrity_components.secondary_event_stack_hash = 0x55;
    hgcpu_integrity_components.chara_animation_hash = 0x66;
    hgcpu_integrity_components.skeleton_runtime_hash = 0x77;
    hgcpu_integrity_components.timer_node_hash = 0x88;
    const uint64_t animation_integrity_before =
        Horse::RollbackHashHgCpuIntegrityComponents(
            hgcpu_integrity_components);
    hgcpu_integrity_components.chara_animation_hash ^= 0x100;
    const uint64_t animation_integrity_after =
        Horse::RollbackHashHgCpuIntegrityComponents(
            hgcpu_integrity_components);
    const bool animation_integrity_covered =
        animation_integrity_before != animation_integrity_after;
    if (!production_gate || !abi_bound || !round_result_flow_covered
        || !active_mode_state_exact
        || !round_result_mode_state_exact
        || !native_frame_counter_semantic || !input_ring_base_exact
        || !collision_cooldown_semantic || !active_frame_context_exact
        || !collision_owner_semantic
        || !presentation_contract
        || !animation_integrity_covered
        || !canonical_restore_policy_ok
        || !canonical_restore_rejects_bytes
        || !round_sequence_states_ok)
    {
        std::printf(
            "production gate/ABI/presentation failed "
            "live=%d ok=%d pending=%u invalid=%u missing=%u overlap=%u first=%u entry=%s failure=%s "
            "abi=%d round_flow=%d "
            "active_mode=%d round_result_mode=%d "
            "presentation=%d split_globals=%d "
            "round_states=%d\n",
            production_validation.live_ready ? 1 : 0,
            production_validation.ok ? 1 : 0,
            production_validation.pending_gameplay_entries,
            production_validation.invalid_entries,
            production_validation.missing_capability_entries,
            production_validation.overlapping_entries,
            production_validation.first_entry,
            production_validation.first_entry < production_manifest.entries.size()
                ? production_manifest.entries[
                    production_validation.first_entry].name : "none",
            production_validation.failure,
            abi_bound ? 1 : 0,
            round_result_flow_covered ? 1 : 0,
            active_mode_state_exact ? 1 : 0,
            round_result_mode_state_exact ? 1 : 0,
            presentation_contract ? 1 : 0,
            (native_frame_counter_semantic && input_ring_base_exact
                && collision_cooldown_semantic
                && active_frame_context_exact
                && collision_owner_semantic) ? 1 : 0,
            round_sequence_states_ok ? 1 : 0);
        return 1;
    }

    std::printf("rollback snapshot self-test passed hash=0x%llX bytes=%u\n",
                static_cast<unsigned long long>(snapshot.hash),
                capture.copied_bytes);
    std::printf("rollback snapshot boundary refusals passed count=%u\n",
                boundary_refusals);
    std::printf(
        "rollback production manifest live coverage gate passed pending=%u abi_bound=%d "
        "schema_window12=0x%llX schema_window60=0x%llX\n",
        production_validation.pending_gameplay_entries,
        abi_bound ? 1 : 0,
        static_cast<unsigned long long>(default_window_manifest.schema_hash()),
        static_cast<unsigned long long>(production_manifest.schema_hash()));
    return 0;
}
