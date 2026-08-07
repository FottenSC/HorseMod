// ============================================================================
// Horse::RollbackSecondaryEventAuthority
//
// Pointer-free transfer of SC6's carried MoveVM secondary-event state at a
// frozen stock round boundary. The Steam lobby owner is authoritative; the
// receiver retains its own process-local pointers.
// ============================================================================

#pragma once

#include "RollbackSecondaryEventStack.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    static constexpr uint8_t kRollbackSecondaryEventAuthorityVersion = 1;
    static constexpr size_t kRollbackSecondaryEventCanonicalSlotBytes = 0x10;

#pragma pack(push, 1)
    struct RollbackSecondaryEventAuthorityMessage
    {
        uint8_t version {kRollbackSecondaryEventAuthorityVersion};
        uint8_t source_player_slot {0};
        uint8_t fighter_slot {0};
        uint8_t reserved {0};
        uint32_t round_ordinal {0};
        uint64_t session_epoch {0};
        uint64_t round_generation {0};
        uint64_t match_identity_digest {0};
        uint64_t state_hash {0};
        uint32_t header_count {0};
        std::array<
            uint8_t,
            kRollbackSecondaryEventSlotCount
                * kRollbackSecondaryEventCanonicalSlotBytes> slots {};
        std::array<uint8_t, kRollbackSecondaryEventScalarBytes> scalars {};
        std::array<uint16_t, kRollbackSecondaryEventMaxHeaders> cursors {};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackSecondaryEventAuthorityMessage) == 948);

    static inline uint64_t RollbackHashSecondaryEventAuthorityMessage(
        const RollbackSecondaryEventAuthorityMessage& message) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(message.header_count);
        hash.add_bytes(message.slots.data(), message.slots.size());
        hash.add_bytes(message.scalars.data(), message.scalars.size());
        hash.add_bytes(
            message.cursors.data(),
            message.header_count * sizeof(uint16_t));
        return hash.value;
    }

    static inline bool RollbackSecondaryEventAuthorityMessageValid(
        const RollbackSecondaryEventAuthorityMessage& message) noexcept
    {
        return message.version == kRollbackSecondaryEventAuthorityVersion
            && message.source_player_slot < 2
            && message.fighter_slot < 2
            && message.reserved == 0
            && (message.round_ordinal & 0xFFFF0000u) == 0
            && message.session_epoch != 0
            && message.round_generation > 1
            && message.match_identity_digest != 0
            && message.state_hash != 0
            && message.header_count <= kRollbackSecondaryEventMaxHeaders
            && RollbackHashSecondaryEventAuthorityMessage(message)
                == message.state_hash;
    }

    enum class RollbackSecondaryEventAuthorityActiveDisposition : uint8_t
    {
        DiscardStale,
        Fatal,
    };

    static inline RollbackSecondaryEventAuthorityActiveDisposition
    ClassifyRollbackSecondaryEventAuthorityDuringActiveRound(
        bool local_is_owner,
        uint8_t expected_remote_slot,
        uint64_t session_epoch,
        uint64_t match_identity_digest,
        uint64_t current_round_generation,
        uint32_t current_round_ordinal,
        const RollbackSecondaryEventAuthorityMessage& message) noexcept
    {
        if (local_is_owner || expected_remote_slot >= 2
            || session_epoch == 0 || match_identity_digest == 0
            || current_round_generation <= 1
            || (current_round_ordinal & 0xFFFF0000u) != 0
            || !RollbackSecondaryEventAuthorityMessageValid(message)
            || message.source_player_slot != expected_remote_slot
            || message.session_epoch != session_epoch
            || message.match_identity_digest != match_identity_digest
            || message.round_generation > current_round_generation
            || (message.round_generation == current_round_generation
                && message.round_ordinal > current_round_ordinal))
        {
            return RollbackSecondaryEventAuthorityActiveDisposition::Fatal;
        }
        return RollbackSecondaryEventAuthorityActiveDisposition::DiscardStale;
    }

    static inline bool RollbackBuildSecondaryEventAuthorityMessage(
        const RollbackSecondaryEventStackHistory& history,
        uint8_t source_player_slot,
        uint8_t fighter_slot,
        uint64_t session_epoch,
        uint64_t round_generation,
        uint32_t round_ordinal,
        uint64_t match_identity_digest,
        RollbackSecondaryEventAuthorityMessage& message) noexcept
    {
        message = {};
        if (!history.ok || source_player_slot >= 2 || fighter_slot >= 2
            || session_epoch == 0 || round_generation <= 1
            || (round_ordinal & 0xFFFF0000u) != 0
            || match_identity_digest == 0
            || history.headers_truncated[fighter_slot]
            || history.header_count[fighter_slot]
                > kRollbackSecondaryEventMaxHeaders)
        {
            return false;
        }

        message.source_player_slot = source_player_slot;
        message.fighter_slot = fighter_slot;
        message.round_ordinal = round_ordinal;
        message.session_epoch = session_epoch;
        message.round_generation = round_generation;
        message.match_identity_digest = match_identity_digest;
        message.header_count = history.header_count[fighter_slot];
        const auto& image = history.bytes[fighter_slot];
        for (size_t slot = 0; slot < kRollbackSecondaryEventSlotCount; ++slot)
        {
            const size_t source = slot * kRollbackSecondaryEventSlotBytes;
            const size_t destination =
                slot * kRollbackSecondaryEventCanonicalSlotBytes;
            std::memcpy(message.slots.data() + destination,
                        image.data() + source, 0x08);
            std::memcpy(message.slots.data() + destination + 0x08,
                        image.data() + source + 0x10, 0x08);
        }
        std::memcpy(message.scalars.data(),
                    image.data() + kRollbackSecondaryEventScalarOffset,
                    message.scalars.size());
        std::copy_n(history.header_cursors[fighter_slot].begin(),
                    message.header_count, message.cursors.begin());
        message.state_hash =
            RollbackHashSecondaryEventAuthorityMessage(message);
        return RollbackSecondaryEventAuthorityMessageValid(message);
    }

    static inline bool RollbackApplySecondaryEventAuthorityMessage(
        const RollbackSecondaryEventAuthorityMessage& message,
        RollbackSecondaryEventStackHistory& history) noexcept
    {
        if (!RollbackSecondaryEventAuthorityMessageValid(message)
            || !history.ok
            || history.headers_truncated[message.fighter_slot]
            || history.header_count[message.fighter_slot]
                != message.header_count)
        {
            return false;
        }

        auto& image = history.bytes[message.fighter_slot];
        for (size_t slot = 0; slot < kRollbackSecondaryEventSlotCount; ++slot)
        {
            const size_t source =
                slot * kRollbackSecondaryEventCanonicalSlotBytes;
            const size_t destination = slot * kRollbackSecondaryEventSlotBytes;
            std::memcpy(image.data() + destination,
                        message.slots.data() + source, 0x08);
            // destination + 0x08 is a process-local fighter back-pointer.
            std::memcpy(image.data() + destination + 0x10,
                        message.slots.data() + source + 0x08, 0x08);
        }
        std::memcpy(image.data() + kRollbackSecondaryEventScalarOffset,
                    message.scalars.data(), message.scalars.size());
        std::copy_n(message.cursors.begin(), message.header_count,
                    history.header_cursors[message.fighter_slot].begin());
        history.hash = RollbackHashSecondaryEventStackHistory(history);
        return RollbackHashSecondaryEventSlotStateCanonical(
                    history, message.fighter_slot) != 0
            && RollbackHashSecondaryEventCursorCanonical(
                    history, message.fighter_slot) != 0;
    }

    enum class RollbackSecondaryEventAuthorityInboxDisposition : uint8_t
    {
        Accepted,
        Duplicate,
        Stale,
        Future,
        Conflict,
        Invalid,
    };

    enum class RollbackSecondaryEventAuthorityReceiveWindow : uint8_t
    {
        Closed,
        TerminalAccepted,
        StockInterRound,
        FrozenRound,
    };

    struct RollbackSecondaryEventAuthorityExpectedRound
    {
        bool valid {false};
        uint64_t generation {0};
        uint32_t ordinal {0};
    };

    static constexpr RollbackSecondaryEventAuthorityExpectedRound
    ResolveRollbackSecondaryEventAuthorityExpectedRound(
        RollbackSecondaryEventAuthorityReceiveWindow window,
        bool terminal_identity_accepted,
        uint64_t current_generation,
        uint32_t current_ordinal) noexcept
    {
        const bool pending_next =
            window == RollbackSecondaryEventAuthorityReceiveWindow::
                StockInterRound
            || (window
                    == RollbackSecondaryEventAuthorityReceiveWindow::
                        TerminalAccepted
                && terminal_identity_accepted);
        const bool frozen =
            window == RollbackSecondaryEventAuthorityReceiveWindow::
                FrozenRound;
        if (current_generation == 0 || (!pending_next && !frozen))
            return {};
        return {
            true,
            current_generation + (pending_next ? 1u : 0u),
            (current_ordinal + (pending_next ? 1u : 0u)) & 0xFFFFu,
        };
    }

    static constexpr bool RollbackSecondaryEventAuthorityMayPublishBaseline(
        bool local_is_owner,
        uint64_t round_generation,
        bool owner_records_ready,
        bool guest_records_applied_and_verified) noexcept
    {
        if (round_generation == 1) return true;
        if (round_generation <= 1) return false;
        return local_is_owner
            ? owner_records_ready
            : guest_records_applied_and_verified;
    }

    class RollbackSecondaryEventAuthorityInbox
    {
    public:
        void reset() noexcept
        {
            m_source_player_slot = 0;
            m_session_epoch = 0;
            m_round_generation = 0;
            m_round_ordinal = 0;
            m_match_identity_digest = 0;
            m_messages = {};
            m_valid = {};
            m_configured = false;
        }

        bool configure(
            uint8_t source_player_slot,
            uint64_t session_epoch,
            uint64_t round_generation,
            uint32_t round_ordinal,
            uint64_t match_identity_digest) noexcept
        {
            if (source_player_slot >= 2 || session_epoch == 0
                || round_generation <= 1
                || (round_ordinal & 0xFFFF0000u) != 0
                || match_identity_digest == 0)
            {
                return false;
            }
            if (m_configured)
            {
                return m_source_player_slot == source_player_slot
                    && m_session_epoch == session_epoch
                    && m_round_generation == round_generation
                    && m_round_ordinal == round_ordinal
                    && m_match_identity_digest == match_identity_digest;
            }
            m_source_player_slot = source_player_slot;
            m_session_epoch = session_epoch;
            m_round_generation = round_generation;
            m_round_ordinal = round_ordinal;
            m_match_identity_digest = match_identity_digest;
            m_configured = true;
            return true;
        }

        RollbackSecondaryEventAuthorityInboxDisposition accept(
            const RollbackSecondaryEventAuthorityMessage& message) noexcept
        {
            if (!m_configured
                || !RollbackSecondaryEventAuthorityMessageValid(message)
                || message.source_player_slot != m_source_player_slot
                || message.session_epoch != m_session_epoch
                || message.match_identity_digest != m_match_identity_digest)
            {
                return RollbackSecondaryEventAuthorityInboxDisposition::
                    Invalid;
            }
            if (message.round_generation < m_round_generation
                || (message.round_generation == m_round_generation
                    && message.round_ordinal < m_round_ordinal))
            {
                return RollbackSecondaryEventAuthorityInboxDisposition::Stale;
            }
            if (message.round_generation > m_round_generation
                || (message.round_generation == m_round_generation
                    && message.round_ordinal > m_round_ordinal))
            {
                return RollbackSecondaryEventAuthorityInboxDisposition::Future;
            }
            const size_t fighter = message.fighter_slot;
            if (m_valid[fighter])
            {
                return std::memcmp(
                    &m_messages[fighter], &message, sizeof(message)) == 0
                    ? RollbackSecondaryEventAuthorityInboxDisposition::Duplicate
                    : RollbackSecondaryEventAuthorityInboxDisposition::Conflict;
            }
            m_messages[fighter] = message;
            m_valid[fighter] = true;
            return RollbackSecondaryEventAuthorityInboxDisposition::Accepted;
        }

        bool ready() const noexcept { return m_valid[0] && m_valid[1]; }

        const RollbackSecondaryEventAuthorityMessage& message(
            size_t fighter) const noexcept
        {
            return m_messages[fighter < 2 ? fighter : 0];
        }

        uint64_t round_generation() const noexcept
        {
            return m_round_generation;
        }

    private:
        uint8_t m_source_player_slot {0};
        uint64_t m_session_epoch {0};
        uint64_t m_round_generation {0};
        uint32_t m_round_ordinal {0};
        uint64_t m_match_identity_digest {0};
        std::array<RollbackSecondaryEventAuthorityMessage, 2> m_messages {};
        std::array<bool, 2> m_valid {};
        bool m_configured {false};
    };

    enum class RollbackSecondaryEventAuthorityTransactionResult : uint8_t
    {
        Applied,
        RecoveredOriginal,
        Unrecoverable,
    };

    template <typename WriteAndVerify>
    static inline RollbackSecondaryEventAuthorityTransactionResult
    RollbackCommitSecondaryEventAuthorityTransaction(
        const RollbackSecondaryEventStackHistory& original,
        const RollbackSecondaryEventStackHistory& authorized,
        WriteAndVerify&& write_and_verify) noexcept
    {
        if (write_and_verify(authorized))
            return RollbackSecondaryEventAuthorityTransactionResult::Applied;
        if (write_and_verify(original))
        {
            return RollbackSecondaryEventAuthorityTransactionResult::
                RecoveredOriginal;
        }
        return RollbackSecondaryEventAuthorityTransactionResult::Unrecoverable;
    }
}
