#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace Horse::Deterministic
{
class LocalImageChecksum final
{
public:
    void Add(const void* data, std::size_t size) noexcept
    {
        const auto* bytes = static_cast<const std::byte*>(data);
#if defined(_MSC_VER) && defined(_M_X64)
        if (HasHardwareCrc32())
        {
            while (size >= sizeof(std::uint64_t))
            {
                std::uint64_t word{};
                std::memcpy(&word, bytes, sizeof(word));
                hash_ = _mm_crc32_u64(hash_, word);
                bytes += sizeof(word);
                size -= sizeof(word);
            }
            while (size-- != 0)
                hash_ = _mm_crc32_u8(static_cast<std::uint32_t>(hash_),
                    std::to_integer<std::uint8_t>(*bytes++));
            return;
        }
#endif
        constexpr std::uint64_t prime1 = 0xC2B2AE3D27D4EB4Full;
        constexpr std::uint64_t prime2 = 0x165667B19E3779F9ull;
        while (size >= sizeof(std::uint64_t))
        {
            std::uint64_t word{};
            std::memcpy(&word, bytes, sizeof(word));
            word ^= seed;
            word *= prime1;
            word = std::rotl(word, 31);
            word *= prime2;
            hash_ ^= word;
            hash_ = std::rotl(hash_, 27) * prime1 + prime2;
            bytes += sizeof(word);
            size -= sizeof(word);
        }
        while (size-- != 0)
        {
            hash_ ^= std::to_integer<std::uint8_t>(*bytes++);
            hash_ = std::rotl(hash_, 11) * prime1;
        }
    }

    [[nodiscard]] std::uint64_t Finish() const noexcept
    {
        auto result = hash_;
        result ^= result >> 33;
        result *= 0xFF51AFD7ED558CCDull;
        result ^= result >> 29;
        result *= 0xC4CEB9FE1A85EC53ull;
        result ^= result >> 32;
        return result;
    }

private:
#if defined(_MSC_VER) && defined(_M_X64)
    [[nodiscard]] static bool HasHardwareCrc32() noexcept
    {
        static const bool supported = []() noexcept {
            int registers[4]{};
            __cpuid(registers, 1);
            return (registers[2] & (1 << 20)) != 0;
        }();
        return supported;
    }
#endif

    static constexpr std::uint64_t seed = 0x9E3779B185EBCA87ull;
    std::uint64_t hash_{seed};
};
}
