// ============================================================================
// Horse::RollbackMotionBankAuthority
//
// Fixed-size, pointer-free transfer of each fighter's logical previous-motion
// buffers at a frozen stock round boundary.
// ============================================================================

#pragma once

#include "RollbackMotionBankCanonical.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    static constexpr uint8_t kRollbackMotionBankAuthorityVersion = 1;
    static constexpr size_t kRollbackMotionBankAuthorityChunkBytes = 1024;
    static constexpr size_t kRollbackMotionBankAuthorityTotalBytes =
        kRollbackMotionBankPlayerCount
        * (kRollbackPrimaryMotionBankBytes
            + kRollbackSecondaryMotionBankBytes);

    static constexpr size_t RollbackMotionBankAuthorityBankOffset(
        size_t fighter, size_t bank) noexcept
    {
        return fighter
                * (kRollbackPrimaryMotionBankBytes
                    + kRollbackSecondaryMotionBankBytes)
            + (bank == 0 ? 0 : kRollbackPrimaryMotionBankBytes);
    }

    static constexpr uint8_t RollbackMotionBankAuthorityChunkCount(
        size_t bank) noexcept
    {
        return bank < kRollbackMotionBankCount
            ? static_cast<uint8_t>((kRollbackMotionBankBytes[bank]
                + kRollbackMotionBankAuthorityChunkBytes - 1)
                / kRollbackMotionBankAuthorityChunkBytes)
            : 0;
    }

    static_assert(
        RollbackMotionBankAuthorityChunkCount(0) <= 64
            && RollbackMotionBankAuthorityChunkCount(1) <= 64,
        "motion-bank authority receipt masks must cover every chunk");

#pragma pack(push, 1)
    struct RollbackMotionBankAuthorityMessage
    {
        uint8_t version {kRollbackMotionBankAuthorityVersion};
        uint8_t source_player_slot {0};
        uint8_t fighter_slot {0};
        uint8_t bank_slot {0};
        uint8_t chunk_index {0};
        uint8_t chunk_count {0};
        uint16_t payload_bytes {0};
        uint32_t round_ordinal {0};
        uint64_t session_epoch {0};
        uint64_t round_generation {0};
        uint64_t match_identity_digest {0};
        uint64_t bank_hash {0};
        std::array<uint8_t, kRollbackMotionBankAuthorityChunkBytes> payload {};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackMotionBankAuthorityMessage) == 1068);

    static inline uint64_t RollbackHashMotionBankAuthorityImage(
        uint8_t fighter, uint8_t bank, const uint8_t* bytes,
        size_t byte_count) noexcept
    {
        if (!bytes || fighter >= kRollbackMotionBankPlayerCount
            || bank >= kRollbackMotionBankCount
            || byte_count != kRollbackMotionBankBytes[bank])
        {
            return 0;
        }
        RollbackFastHash hash {};
        hash.add_scalar(fighter);
        hash.add_scalar(bank);
        hash.add_scalar(byte_count);
        hash.add_bytes(bytes, byte_count);
        return hash.finish();
    }

    static inline bool RollbackMotionBankAuthorityMessageValid(
        const RollbackMotionBankAuthorityMessage& message) noexcept
    {
        if (message.version != kRollbackMotionBankAuthorityVersion
            || message.source_player_slot >= 2
            || message.fighter_slot >= kRollbackMotionBankPlayerCount
            || message.bank_slot >= kRollbackMotionBankCount
            || (message.round_ordinal & 0xFFFF0000u) != 0
            || message.session_epoch == 0 || message.round_generation == 0
            || message.match_identity_digest == 0 || message.bank_hash == 0)
        {
            return false;
        }
        const uint8_t chunks = RollbackMotionBankAuthorityChunkCount(
            message.bank_slot);
        if (message.chunk_count != chunks || message.chunk_index >= chunks)
            return false;
        const size_t offset = static_cast<size_t>(message.chunk_index)
            * kRollbackMotionBankAuthorityChunkBytes;
        const size_t expected = (std::min)(
            kRollbackMotionBankAuthorityChunkBytes,
            kRollbackMotionBankBytes[message.bank_slot] - offset);
        return message.payload_bytes == expected;
    }

    static inline bool RollbackMotionBankAuthorityIsStaleDuringActiveRound(
        bool local_is_owner, uint8_t expected_remote_slot,
        uint64_t session_epoch, uint64_t match_identity_digest,
        uint64_t current_round_generation, uint32_t current_round_ordinal,
        const RollbackMotionBankAuthorityMessage& message) noexcept
    {
        return !local_is_owner && expected_remote_slot < 2
            && session_epoch != 0 && match_identity_digest != 0
            && current_round_generation != 0
            && (current_round_ordinal & 0xFFFF0000u) == 0
            && RollbackMotionBankAuthorityMessageValid(message)
            && message.source_player_slot == expected_remote_slot
            && message.session_epoch == session_epoch
            && message.match_identity_digest == match_identity_digest
            && (message.round_generation < current_round_generation
                || (message.round_generation == current_round_generation
                    && message.round_ordinal <= current_round_ordinal));
    }

    template <typename MotionHistory>
    static inline bool RollbackBuildMotionBankAuthorityMessage(
        const MotionHistory& history, uint8_t source_player_slot,
        uint8_t fighter_slot, uint8_t bank_slot, uint8_t chunk_index,
        uint64_t session_epoch, uint64_t round_generation,
        uint32_t round_ordinal, uint64_t match_identity_digest,
        RollbackMotionBankAuthorityMessage& message) noexcept
    {
        message = {};
        if (!history.ok || source_player_slot >= 2
            || fighter_slot >= kRollbackMotionBankPlayerCount
            || bank_slot >= kRollbackMotionBankCount)
        {
            return false;
        }
        const int provider = history.provider_slot[fighter_slot][bank_slot];
        if (provider < 0
            || provider >= static_cast<int>(kRollbackMotionBankBufferCount))
        {
            return false;
        }
        const size_t source = RollbackMotionBankByteOffset(
            fighter_slot, bank_slot, static_cast<size_t>(provider));
        const size_t bank_bytes = kRollbackMotionBankBytes[bank_slot];
        if (source + bank_bytes > history.bytes.size()) return false;

        message.source_player_slot = source_player_slot;
        message.fighter_slot = fighter_slot;
        message.bank_slot = bank_slot;
        message.chunk_index = chunk_index;
        message.chunk_count = RollbackMotionBankAuthorityChunkCount(bank_slot);
        message.round_ordinal = round_ordinal;
        message.session_epoch = session_epoch;
        message.round_generation = round_generation;
        message.match_identity_digest = match_identity_digest;
        message.bank_hash = RollbackHashMotionBankAuthorityImage(
            fighter_slot, bank_slot, history.bytes.data() + source,
            bank_bytes);
        if (chunk_index >= message.chunk_count || message.bank_hash == 0)
            return false;
        const size_t chunk_offset = static_cast<size_t>(chunk_index)
            * kRollbackMotionBankAuthorityChunkBytes;
        message.payload_bytes = static_cast<uint16_t>((std::min)(
            kRollbackMotionBankAuthorityChunkBytes,
            bank_bytes - chunk_offset));
        std::memcpy(message.payload.data(),
            history.bytes.data() + source + chunk_offset,
            message.payload_bytes);
        return RollbackMotionBankAuthorityMessageValid(message);
    }

    enum class RollbackMotionBankAuthorityInboxDisposition : uint8_t
    {
        Accepted,
        Duplicate,
        Complete,
        Stale,
        Future,
        Conflict,
        Invalid,
    };

    enum class RollbackInitialMotionBankAuthorityDisposition : uint8_t
    {
        Accepted,
        Duplicate,
        Complete,
        Invalid,
    };

    class RollbackMotionBankAuthorityInbox
    {
    public:
        void reset() noexcept { *this = {}; }

        bool configure(uint8_t source_player_slot, uint64_t session_epoch,
            uint64_t round_generation, uint32_t round_ordinal,
            uint64_t match_identity_digest) noexcept
        {
            if (source_player_slot >= 2 || session_epoch == 0
                || round_generation == 0
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

        RollbackMotionBankAuthorityInboxDisposition accept(
            const RollbackMotionBankAuthorityMessage& message) noexcept
        {
            if (!m_configured || !RollbackMotionBankAuthorityMessageValid(message)
                || message.source_player_slot != m_source_player_slot
                || message.session_epoch != m_session_epoch
                || message.match_identity_digest != m_match_identity_digest)
            {
                return RollbackMotionBankAuthorityInboxDisposition::Invalid;
            }
            if (message.round_generation < m_round_generation
                || (message.round_generation == m_round_generation
                    && message.round_ordinal < m_round_ordinal))
                return RollbackMotionBankAuthorityInboxDisposition::Stale;
            if (message.round_generation > m_round_generation
                || (message.round_generation == m_round_generation
                    && message.round_ordinal > m_round_ordinal))
                return RollbackMotionBankAuthorityInboxDisposition::Future;

            const size_t fighter = message.fighter_slot;
            const size_t bank = message.bank_slot;
            const uint64_t bit = uint64_t {1} << message.chunk_index;
            if (m_bank_hash[fighter][bank] != 0
                && m_bank_hash[fighter][bank] != message.bank_hash)
                return RollbackMotionBankAuthorityInboxDisposition::Conflict;
            const size_t destination = RollbackMotionBankAuthorityBankOffset(
                fighter, bank)
                + static_cast<size_t>(message.chunk_index)
                    * kRollbackMotionBankAuthorityChunkBytes;
            if ((m_received_mask[fighter][bank] & bit) != 0)
            {
                return std::memcmp(m_bytes.data() + destination,
                    message.payload.data(), message.payload_bytes) == 0
                    ? RollbackMotionBankAuthorityInboxDisposition::Duplicate
                    : RollbackMotionBankAuthorityInboxDisposition::Conflict;
            }
            m_bank_hash[fighter][bank] = message.bank_hash;
            std::memcpy(m_bytes.data() + destination,
                message.payload.data(), message.payload_bytes);
            m_received_mask[fighter][bank] |= bit;
            if (bank_ready(fighter, bank))
            {
                const size_t source = RollbackMotionBankAuthorityBankOffset(
                    fighter, bank);
                if (RollbackHashMotionBankAuthorityImage(
                        static_cast<uint8_t>(fighter),
                        static_cast<uint8_t>(bank), m_bytes.data() + source,
                        kRollbackMotionBankBytes[bank])
                    != m_bank_hash[fighter][bank])
                {
                    return RollbackMotionBankAuthorityInboxDisposition::Conflict;
                }
            }
            return ready()
                ? RollbackMotionBankAuthorityInboxDisposition::Complete
                : RollbackMotionBankAuthorityInboxDisposition::Accepted;
        }

        bool bank_ready(size_t fighter, size_t bank) const noexcept
        {
            if (fighter >= kRollbackMotionBankPlayerCount
                || bank >= kRollbackMotionBankCount)
                return false;
            const uint8_t chunk_count =
                RollbackMotionBankAuthorityChunkCount(bank);
            const uint64_t expected = chunk_count == 64
                ? UINT64_MAX
                : (uint64_t {1} << chunk_count) - 1u;
            return m_bank_hash[fighter][bank] != 0
                && m_received_mask[fighter][bank] == expected;
        }

        bool ready() const noexcept
        {
            for (size_t fighter = 0; fighter < kRollbackMotionBankPlayerCount;
                 ++fighter)
                for (size_t bank = 0; bank < kRollbackMotionBankCount; ++bank)
                    if (!bank_ready(fighter, bank)) return false;
            return true;
        }

        uint64_t bank_hash(size_t fighter, size_t bank) const noexcept
        {
            return fighter < kRollbackMotionBankPlayerCount
                    && bank < kRollbackMotionBankCount
                ? m_bank_hash[fighter][bank] : 0;
        }

        template <typename MotionHistory>
        bool apply(MotionHistory& history) const noexcept
        {
            if (!ready() || !history.ok) return false;
            for (size_t fighter = 0; fighter < kRollbackMotionBankPlayerCount;
                 ++fighter)
            {
                for (size_t bank = 0; bank < kRollbackMotionBankCount; ++bank)
                {
                    const int provider = history.provider_slot[fighter][bank];
                    if (provider < 0 || provider >= static_cast<int>(
                            kRollbackMotionBankBufferCount))
                        return false;
                    const size_t destination = RollbackMotionBankByteOffset(
                        fighter, bank, static_cast<size_t>(provider));
                    const size_t source = RollbackMotionBankAuthorityBankOffset(
                        fighter, bank);
                    const size_t bytes = kRollbackMotionBankBytes[bank];
                    if (destination + bytes > history.bytes.size()) return false;
                    std::memcpy(history.bytes.data() + destination,
                        m_bytes.data() + source, bytes);
                }
            }
            return true;
        }

        uint64_t round_generation() const noexcept { return m_round_generation; }

        bool configured_for(uint8_t source_player_slot,
            uint64_t session_epoch, uint64_t round_generation,
            uint32_t round_ordinal,
            uint64_t match_identity_digest) const noexcept
        {
            return m_configured
                && m_source_player_slot == source_player_slot
                && m_session_epoch == session_epoch
                && m_round_generation == round_generation
                && m_round_ordinal == round_ordinal
                && m_match_identity_digest == match_identity_digest;
        }

    private:
        uint8_t m_source_player_slot {0};
        uint64_t m_session_epoch {0};
        uint64_t m_round_generation {0};
        uint32_t m_round_ordinal {0};
        uint64_t m_match_identity_digest {0};
        std::array<uint8_t, kRollbackMotionBankAuthorityTotalBytes> m_bytes {};
        uint64_t m_bank_hash[kRollbackMotionBankPlayerCount]
            [kRollbackMotionBankCount] {};
        uint64_t m_received_mask[kRollbackMotionBankPlayerCount]
            [kRollbackMotionBankCount] {};
        bool m_configured {false};
    };

    static inline RollbackInitialMotionBankAuthorityDisposition
    RollbackAcceptInitialMotionBankAuthorityBeforeBoundary(
        RollbackMotionBankAuthorityInbox& inbox,
        bool session_contract_ready, bool local_is_owner,
        uint8_t expected_remote_slot, uint64_t session_epoch,
        const RollbackMotionBankAuthorityMessage& message) noexcept
    {
        if (!session_contract_ready || local_is_owner
            || expected_remote_slot >= 2 || session_epoch == 0
            || !RollbackMotionBankAuthorityMessageValid(message)
            || message.source_player_slot != expected_remote_slot
            || message.session_epoch != session_epoch
            || message.round_generation != 1)
        {
            return RollbackInitialMotionBankAuthorityDisposition::Invalid;
        }
        if (inbox.round_generation() != 0
            && inbox.round_generation() != 1)
        {
            return RollbackInitialMotionBankAuthorityDisposition::Invalid;
        }
        if (!inbox.configure(message.source_player_slot, message.session_epoch,
                message.round_generation, message.round_ordinal,
                message.match_identity_digest))
        {
            return RollbackInitialMotionBankAuthorityDisposition::Invalid;
        }
        switch (inbox.accept(message))
        {
        case RollbackMotionBankAuthorityInboxDisposition::Accepted:
            return RollbackInitialMotionBankAuthorityDisposition::Accepted;
        case RollbackMotionBankAuthorityInboxDisposition::Duplicate:
            return RollbackInitialMotionBankAuthorityDisposition::Duplicate;
        case RollbackMotionBankAuthorityInboxDisposition::Complete:
            return RollbackInitialMotionBankAuthorityDisposition::Complete;
        default:
            return RollbackInitialMotionBankAuthorityDisposition::Invalid;
        }
    }
}
