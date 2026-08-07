// ============================================================================
// Horse::RollbackSecondaryEventStack
//
// Pointer-normalized rollback image for the fixed MoveVM secondary event
// stack and its mutable per-event round-robin cursors.
// ============================================================================

#pragma once

#include "RollbackStateHash.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    static constexpr uintptr_t kRollbackSecondaryEventStackCharaOffset = 0x95788;
    static constexpr size_t kRollbackSecondaryEventSlotBytes = 0x18;
    static constexpr size_t kRollbackSecondaryEventSlotCount = 0x18;
    static constexpr size_t kRollbackSecondaryEventSlotsBytes = 0x240;
    static constexpr size_t kRollbackSecondaryEventPointerBlockOffset = 0x240;
    static constexpr size_t kRollbackSecondaryEventPointerBlockBytes = 0x18;
    static constexpr size_t kRollbackSecondaryEventScalarOffset = 0x258;
    static constexpr size_t kRollbackSecondaryEventScalarBytes = 0x08;
    static constexpr size_t kRollbackSecondaryEventStackBytes = 0x260;
    static constexpr size_t kRollbackSecondaryEventHeaderCountOffset = 0x14;
    static constexpr size_t kRollbackSecondaryEventHeaderStride = 0x08;
    static constexpr size_t kRollbackSecondaryEventHeaderCursorOffset = 0x02;
    static constexpr size_t kRollbackSecondaryEventMaxHeaders = 0x100;

    struct RollbackSecondaryEventStackHistory
    {
        bool ok {false};
        uintptr_t chara[2] {};
        uintptr_t table_header[2] {};
        uintptr_t event_headers[2] {};
        uintptr_t event_payloads[2] {};
        std::array<
            std::array<uint8_t, kRollbackSecondaryEventStackBytes>,
            2> bytes {};
        uint32_t header_count[2] {};
        bool headers_truncated[2] {};
        std::array<
            std::array<uint16_t, kRollbackSecondaryEventMaxHeaders>,
            2> header_cursors {};
        uint64_t hash {0};

        void clear()
        {
            ok = false;
            std::memset(chara, 0, sizeof(chara));
            std::memset(table_header, 0, sizeof(table_header));
            std::memset(event_headers, 0, sizeof(event_headers));
            std::memset(event_payloads, 0, sizeof(event_payloads));
            for (auto& image : bytes) image.fill(0);
            std::memset(header_count, 0, sizeof(header_count));
            std::memset(headers_truncated, 0, sizeof(headers_truncated));
            for (auto& cursors : header_cursors) cursors.fill(0);
            hash = 0;
        }

        void recycle_for_capture()
        {
            ok = false;
            std::memset(chara, 0, sizeof(chara));
            std::memset(table_header, 0, sizeof(table_header));
            std::memset(event_headers, 0, sizeof(event_headers));
            std::memset(event_payloads, 0, sizeof(event_payloads));
            std::memset(header_count, 0, sizeof(header_count));
            std::memset(headers_truncated, 0, sizeof(headers_truncated));
            hash = 0;
        }
    };

    static inline uint64_t RollbackHashSecondaryEventStackHistory(
        const RollbackSecondaryEventStackHistory& history) noexcept
    {
        RollbackHash h {};
        h.add_scalar(history.ok);
        h.add_bytes(history.chara, sizeof(history.chara));
        h.add_bytes(history.table_header, sizeof(history.table_header));
        h.add_bytes(history.event_headers, sizeof(history.event_headers));
        h.add_bytes(history.event_payloads, sizeof(history.event_payloads));
        for (const auto& image : history.bytes)
            h.add_bytes(image.data(), image.size());
        h.add_bytes(history.header_count, sizeof(history.header_count));
        h.add_bytes(
            history.headers_truncated, sizeof(history.headers_truncated));
        for (size_t player = 0; player < 2; ++player)
        {
            h.add_bytes(
                history.header_cursors[player].data(),
                history.header_count[player] * sizeof(uint16_t));
        }
        return h.value;
    }

    static inline void RollbackAddSecondaryEventSlotStateCanonical(
        RollbackHash& hash,
        const RollbackSecondaryEventStackHistory& history,
        size_t player) noexcept
    {
        const auto& image = history.bytes[player];
        for (size_t slot = 0;
             slot < kRollbackSecondaryEventSlotCount;
             ++slot)
        {
            const size_t offset = slot * kRollbackSecondaryEventSlotBytes;
            hash.add_bytes(image.data() + offset, 0x08);
            // +0x08 is a process-local ALuxBattleChara* back-pointer.
            hash.add_bytes(image.data() + offset + 0x10, 0x08);
        }
        hash.add_bytes(
            image.data() + kRollbackSecondaryEventScalarOffset,
            kRollbackSecondaryEventScalarBytes);
    }

    static inline void RollbackAddSecondaryEventCursorCanonical(
        RollbackHash& hash,
        const RollbackSecondaryEventStackHistory& history,
        size_t player) noexcept
    {
        hash.add_scalar(history.header_count[player]);
        hash.add_scalar(history.headers_truncated[player]);
        hash.add_bytes(
            history.header_cursors[player].data(),
            history.header_count[player] * sizeof(uint16_t));
    }

    static inline uint64_t RollbackHashSecondaryEventSlotStateCanonical(
        const RollbackSecondaryEventStackHistory& history,
        size_t player) noexcept
    {
        if (!history.ok || player >= 2) return 0;
        RollbackHash hash {};
        RollbackAddSecondaryEventSlotStateCanonical(hash, history, player);
        return hash.value;
    }

    static inline uint64_t RollbackHashSecondaryEventCursorCanonical(
        const RollbackSecondaryEventStackHistory& history,
        size_t player) noexcept
    {
        if (!history.ok || player >= 2) return 0;
        RollbackHash hash {};
        RollbackAddSecondaryEventCursorCanonical(hash, history, player);
        return hash.value;
    }

    static inline uint64_t RollbackHashSecondaryEventStackCanonical(
        const RollbackSecondaryEventStackHistory& history) noexcept
    {
        if (!history.ok) return 0;
        RollbackHash hash {};
        for (size_t player = 0; player < 2; ++player)
        {
            RollbackAddSecondaryEventSlotStateCanonical(
                hash, history, player);
            RollbackAddSecondaryEventCursorCanonical(
                hash, history, player);
        }
        return hash.value;
    }
}
