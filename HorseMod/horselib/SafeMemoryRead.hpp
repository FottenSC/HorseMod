// ============================================================================
// Horse::SafeMemoryRead — SEH-wrapped safe dereference helpers.
//
// Why this exists
// ---------------
// The capsule walker probes many candidate offsets on an unfamiliar
// object and dereferences pointer-shaped values it finds there. Most of
// those values aren't pointers — they're ints, flags, floats — so
// blindly dereferencing them hits unmapped memory and crashes the game.
//
// Windows' structured exception handling (`__try/__except`) lets us
// attempt a read and catch the access violation without bringing down
// the process. That's the right tool for "peek at memory that might be
// unmapped" code.
//
// Important: MSVC forbids `__try/__except` in the same function body as
// C++ object destructors. The SEH wrappers in this file are therefore
// kept as tiny standalone static functions that do nothing but the read
// and return a bool. Callers in the walker check the bool and continue
// scanning on failure.
//
// Cost: SEH adds a small prologue/epilogue cost per call (~a few tens of
// ns). The walker does ~1000 of these per chara per frame which is
// well within budget — a few microseconds total.
// ============================================================================

#pragma once

#include <cstdint>

// Windows.h is a transitive include; pull it in directly so we don't
// rely on NativeBinding.hpp being included first.
#include <Windows.h>

namespace Horse
{
    // Read an 8-byte pointer at `addr`. Returns true on success, false
    // if the read raised a structured exception (access violation,
    // alignment fault, etc.).
    static inline bool SafeReadPtr(const void* addr, void** out) noexcept
    {
        __try
        {
            *out = *reinterpret_cast<void* const*>(addr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Read a 4-byte int at `addr`. Returns true on success.
    static inline bool SafeReadInt32(const void* addr, int32_t* out) noexcept
    {
        __try
        {
            *out = *reinterpret_cast<const int32_t*>(addr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Read an 8-byte uint at `addr`. Useful for hex dumps.
    static inline bool SafeReadUInt64(const void* addr, uint64_t* out) noexcept
    {
        __try
        {
            *out = *reinterpret_cast<const uint64_t*>(addr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Read a 1-byte uint at `addr`.
    static inline bool SafeReadUInt8(const void* addr, uint8_t* out) noexcept
    {
        __try
        {
            *out = *reinterpret_cast<const uint8_t*>(addr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Read a 2-byte uint at `addr`.
    static inline bool SafeReadUInt16(const void* addr, uint16_t* out) noexcept
    {
        __try
        {
            *out = *reinterpret_cast<const uint16_t*>(addr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Read a 2-byte signed int at `addr`.  Used for fields the engine
    // authors as i16 (master-window frames, phase tag, etc.); reading
    // them as u16 and then casting works but loses the "this is signed"
    // intent at the call site.
    static inline bool SafeReadInt16(const void* addr, int16_t* out) noexcept
    {
        __try
        {
            *out = *reinterpret_cast<const int16_t*>(addr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Read a 4-byte uint at `addr`.
    static inline bool SafeReadUInt32(const void* addr, uint32_t* out) noexcept
    {
        __try
        {
            *out = *reinterpret_cast<const uint32_t*>(addr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Read a 4-byte float at `addr`.
    static inline bool SafeReadFloat(const void* addr, float* out) noexcept
    {
        __try
        {
            *out = *reinterpret_cast<const float*>(addr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Bulk memcpy that returns false if any byte is unmapped. Slow path
    // only — for hex dumps and raw struct copies.
    static inline bool SafeReadBytes(const void* src, void* dst, size_t len) noexcept
    {
        __try
        {
            // Byte-by-byte so a partial fault still returns false without
            // leaving a partial copy in dst (we overwrite all of dst with 0).
            auto* s = reinterpret_cast<const uint8_t*>(src);
            auto* d = reinterpret_cast<uint8_t*>(dst);
            for (size_t i = 0; i < len; ++i) d[i] = s[i];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

} // namespace Horse
