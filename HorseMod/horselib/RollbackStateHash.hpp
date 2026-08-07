// ============================================================================
// Horse::RollbackStateHash
//
// Small deterministic hash helpers for rollback lab manifests. This is not a
// cryptographic hash; it is a cheap per-frame equality signal for state blobs,
// evidence bundles, and future mismatch reports.
// ============================================================================

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64))
#include <intrin.h>
#endif

namespace Horse
{
    struct RollbackHash
    {
        uint64_t value {14695981039346656037ull};

        void add_bytes(const void* data, size_t size) noexcept
        {
            if (!data || size == 0) return;
            const auto* p = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i)
            {
                value ^= static_cast<uint64_t>(p[i]);
                value *= 1099511628211ull;
            }
        }

        void add_zero_bytes(size_t size) noexcept
        {
            // For a zero byte FNV-1a reduces to one multiplication. Apply the
            // same recurrence in O(log n) for canonicalized zero spans.
            uint64_t accumulated = 1;
            uint64_t factor = 1099511628211ull;
            while (size != 0)
            {
                if ((size & 1u) != 0)
                    accumulated *= factor;
                factor *= factor;
                size >>= 1u;
            }
            value *= accumulated;
        }

        template<typename T>
        void add_scalar(const T& v) noexcept
        {
            add_bytes(&v, sizeof(v));
        }
    };

    struct RollbackFastHash
    {
        uint64_t state[4] {
            0x9E3779B97F4A7C15ull,
            0xD1B54A32D192ED03ull,
            0x94D049BB133111EBull,
            0x8538ECB5BD456EA3ull,
        };
        uint64_t tail {0};
        uint64_t total_bytes {0};
        uint64_t word_count {0};
        uint8_t tail_bytes {0};

        static uint64_t mix(uint64_t value) noexcept
        {
            value ^= value >> 30u;
            value *= 0xBF58476D1CE4E5B9ull;
            value ^= value >> 27u;
            value *= 0x94D049BB133111EBull;
            return value ^ (value >> 31u);
        }

        void add_word(uint64_t word) noexcept
        {
            uint64_t& lane = state[word_count++ & 3u];
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64))
            // Four independent CRC32C lanes cover every input word while
            // hiding the instruction's dependency latency. Finalization
            // mixes all four lanes and the exact byte/word counts into the
            // 64-bit integrity value. The current SC6 x64 target requires a
            // CPU generation with SSE4.2; non-MSVC analysis builds retain the
            // portable mixer below.
            lane = _mm_crc32_u64(lane, word);
#else
            lane ^= mix(word + 0xD6E8FEB86659FD93ull);
            lane = ((lane << 27u) | (lane >> 37u))
                * 0x9E3779B185EBCA87ull + 0xC2B2AE3D27D4EB4Full;
#endif
        }

        void add_bytes(const void* data, size_t size) noexcept
        {
            if (!data || size == 0) return;
            const auto* bytes = static_cast<const uint8_t*>(data);
            total_bytes += size;
            if (tail_bytes != 0)
            {
                const size_t take = (std::min)(
                    size, static_cast<size_t>(8u - tail_bytes));
                std::memcpy(reinterpret_cast<uint8_t*>(&tail) + tail_bytes,
                    bytes, take);
                tail_bytes = static_cast<uint8_t>(tail_bytes + take);
                bytes += take;
                size -= take;
                if (tail_bytes == 8u)
                {
                    add_word(tail);
                    tail = 0;
                    tail_bytes = 0;
                }
            }
            while (size >= sizeof(uint64_t))
            {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64))
                // Align the logical lane first, then update all four
                // independent CRC lanes per iteration. This preserves exact
                // chunk invariance while avoiding a branch/indexed lane
                // lookup for every word in the large rollback snapshots.
                if ((word_count & 3u) == 0u
                    && size >= 4u * sizeof(uint64_t))
                {
                    uint64_t words[4] {};
                    std::memcpy(words, bytes, sizeof(words));
                    state[0] = _mm_crc32_u64(state[0], words[0]);
                    state[1] = _mm_crc32_u64(state[1], words[1]);
                    state[2] = _mm_crc32_u64(state[2], words[2]);
                    state[3] = _mm_crc32_u64(state[3], words[3]);
                    word_count += 4u;
                    bytes += sizeof(words);
                    size -= sizeof(words);
                    continue;
                }
#endif
                uint64_t word = 0;
                std::memcpy(&word, bytes, sizeof(word));
                add_word(word);
                bytes += sizeof(word);
                size -= sizeof(word);
            }
            if (size != 0)
            {
                std::memcpy(&tail, bytes, size);
                tail_bytes = static_cast<uint8_t>(size);
            }
        }

        void add_zero_bytes(size_t size) noexcept
        {
            if (size == 0) return;
            total_bytes += size;
            if (tail_bytes != 0)
            {
                const size_t take = (std::min)(
                    size, static_cast<size_t>(8u - tail_bytes));
                tail_bytes = static_cast<uint8_t>(tail_bytes + take);
                size -= take;
                if (tail_bytes == 8u)
                {
                    add_word(tail);
                    tail = 0;
                    tail_bytes = 0;
                }
            }
            while (size >= sizeof(uint64_t))
            {
                add_word(0);
                size -= sizeof(uint64_t);
            }
            if (size != 0) tail_bytes = static_cast<uint8_t>(size);
        }

        template<typename T>
        void add_scalar(const T& value) noexcept
        {
            add_bytes(&value, sizeof(value));
        }

        uint64_t finish() const noexcept
        {
            uint64_t value = mix(state[0])
                ^ ((mix(state[1]) << 13u) | (mix(state[1]) >> 51u))
                ^ ((mix(state[2]) << 29u) | (mix(state[2]) >> 35u))
                ^ ((mix(state[3]) << 47u) | (mix(state[3]) >> 17u));
            if (tail_bytes != 0)
                value ^= mix(tail ^ (static_cast<uint64_t>(tail_bytes) << 56u));
            value ^= mix(total_bytes);
            value ^= mix(word_count);
            value = mix(value);
            return value ? value : 1;
        }
    };

    static inline uint64_t RollbackHashBytes(
        const void* data,
        size_t size) noexcept
    {
        RollbackHash h{};
        h.add_bytes(data, size);
        return h.value;
    }

    // Fast local-integrity hash for large opaque buffers. Cross-peer hashes
    // continue to use RollbackHash/FNV so their existing schema is unchanged.
    // Reading a word at a time removes the serial multiply dependency from the
    // 0x28018-byte HgCpu buffer captured on every owned frame.
    static inline uint64_t RollbackFastIntegrityHashBytes(
        const void* data,
        size_t size) noexcept
    {
        if (!data || size == 0) return 1;
        RollbackFastHash hash {};
        hash.add_bytes(data, size);
        return hash.finish();
    }

    static inline uint64_t RollbackHashCombine(
        uint64_t a,
        uint64_t b) noexcept
    {
        RollbackHash h{};
        h.add_scalar(a);
        h.add_scalar(b);
        return h.value;
    }
}
