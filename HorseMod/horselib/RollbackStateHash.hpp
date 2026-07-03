// ============================================================================
// Horse::RollbackStateHash
//
// Small deterministic hash helpers for rollback lab manifests. This is not a
// cryptographic hash; it is a cheap per-frame equality signal for state blobs,
// evidence bundles, and future mismatch reports.
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

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

        template<typename T>
        void add_scalar(const T& v) noexcept
        {
            add_bytes(&v, sizeof(v));
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

