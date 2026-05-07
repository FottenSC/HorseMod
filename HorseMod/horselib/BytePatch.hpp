// ============================================================================
// Horse::BytePatch — RAII-style runtime instruction patcher.
//
// Why this exists
// ---------------
// Several SC6 features we want to mod (camera lock, VFX off, game pause,
// speed control) live BELOW the UFunction reflection layer.  The only way
// to reach them is to overwrite the relevant x64 instructions at known
// signatures inside the SC6 image.  This file is the "write N bytes at
// address X, remember the originals so we can put them back" primitive.
//
// Lifecycle
// ---------
// Default-constructed BytePatch is in the "unprepared" state and does
// nothing.  prepare() snapshots the original bytes from the live image —
// must be called once after sig-scanning has succeeded.  enable() flips
// the page protection, writes the replacement bytes, restores protection,
// and flushes the instruction cache (mandatory on x64 — the CPU may have
// already prefetched the original opcode bytes).  disable() does the same
// in reverse.  Destruction calls disable() automatically so a leaked
// patch can't permanently corrupt the running process.
//
// Why we re-snapshot at prepare() instead of taking the original from the
// caller: lets us survive future SC6 patches that move surrounding code
// without changing the AOB-matched instruction.  The caller hands us a
// pattern, we resolve to an address, we capture whatever's actually there
// — we never assume we already know the original encoding.
//
// Threading
// ---------
// enable()/disable() are NOT thread-safe with respect to other threads
// EXECUTING the patched instruction (you can in principle clobber an
// in-flight instruction stream).  In practice we only flip patches from
// the game thread during ImGui callbacks (Slate tick) — that's the same
// thread that runs the patched instructions, so the toggle and the
// execution can't race.  Don't toggle a patch from a worker thread.
// ============================================================================

#pragma once

#include <DynamicOutput/DynamicOutput.hpp>

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace Horse
{
    class BytePatch
    {
    public:
        BytePatch() = default;
        ~BytePatch() { disable(); }

        // Non-copyable (would invalidate the original-bytes snapshot if
        // both copies tried to disable).  Movable so a class can hold
        // patches by value.
        BytePatch(const BytePatch&) = delete;
        BytePatch& operator=(const BytePatch&) = delete;
        BytePatch(BytePatch&& other) noexcept { *this = std::move(other); }
        BytePatch& operator=(BytePatch&& other) noexcept
        {
            if (this == &other) return *this;
            // If we owned an active patch, restore it before stealing
            // the other's state — otherwise we'd leak the patched bytes.
            disable();
            m_addr      = std::exchange(other.m_addr,      nullptr);
            m_orig      = std::move(other.m_orig);
            m_replace   = std::move(other.m_replace);
            m_enabled   = std::exchange(other.m_enabled,   false);
            m_prepared  = std::exchange(other.m_prepared,  false);
            return *this;
        }

        // ----------------------------------------------------------------
        // Snapshot the original bytes from `addr`, remember the requested
        // replacement.  Doesn't write anything yet.  Returns true on
        // success, false if `addr` is null or unreadable.
        //
        // The replacement length must equal what we'll restore on
        // disable() — i.e. the snapshot size.  We take both up front so
        // mismatches surface here, not as a memcpy bug later.
        // ----------------------------------------------------------------
        bool prepare(void* addr, const uint8_t* replacement, size_t n)
        {
            if (!addr || !replacement || n == 0)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.BytePatch] prepare() got null/zero-length "
                        "args (addr=0x{:x} ptr={} n={})\n"),
                    reinterpret_cast<uintptr_t>(addr),
                    reinterpret_cast<uintptr_t>(replacement), n);
                return false;
            }
            // We can't easily probe readability without SEH; just copy
            // and hope.  The address came from a successful sig-scan,
            // so it's almost certainly inside committed .text.
            m_addr = addr;
            m_orig.assign(static_cast<const uint8_t*>(addr),
                          static_cast<const uint8_t*>(addr) + n);
            m_replace.assign(replacement, replacement + n);
            m_prepared = true;
            m_enabled  = false;
            return true;
        }

        // ----------------------------------------------------------------
        // Apply the replacement bytes to the live image.  No-op if
        // already enabled or if prepare() wasn't called.  Returns true
        // on success.
        //
        // Steps:
        //   1. VirtualProtect → PAGE_EXECUTE_READWRITE so we can write.
        //   2. memcpy the replacement.
        //   3. VirtualProtect → original protection (typically
        //      PAGE_EXECUTE_READ).
        //   4. FlushInstructionCache so the CPU re-fetches.
        //
        // We restore the OLD protection rather than always going to
        // PAGE_EXECUTE_READ in case a previous mod has set something
        // weirder.  Microsoft documents PAGE_EXECUTE_WRITECOPY as a
        // possible state for shared images.
        // ----------------------------------------------------------------
        bool enable()
        {
            if (m_enabled)  return true;
            if (!m_prepared)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.BytePatch] enable() called before prepare()\n"));
                return false;
            }
            if (!write_bytes(m_replace.data(), m_replace.size())) return false;
            m_enabled = true;
            return true;
        }

        // Restore the original bytes.  No-op if not enabled.  Always
        // tries even if prepare() snapshot is stale — better to put
        // SOMETHING back than leave the instruction stream patched.
        bool disable()
        {
            if (!m_enabled) return true;
            const bool ok = write_bytes(m_orig.data(), m_orig.size());
            // Drop the enabled flag even on partial failure — re-trying
            // on the same patch state is unlikely to help and could
            // mask the original error.
            m_enabled = false;
            return ok;
        }

        bool is_enabled()  const { return m_enabled;  }
        bool is_prepared() const { return m_prepared; }
        void* address()    const { return m_addr;     }
        size_t size()      const { return m_orig.size(); }

    private:
        // Shared write-with-VirtualProtect helper used by both enable
        // and disable.  Logs and returns false if VirtualProtect fails;
        // never throws.
        bool write_bytes(const uint8_t* src, size_t n)
        {
            if (!m_addr || !src || n == 0) return false;
            DWORD old_prot = 0;
            if (!::VirtualProtect(m_addr, n, PAGE_EXECUTE_READWRITE, &old_prot))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.BytePatch] VirtualProtect(RWX) failed at "
                        "0x{:x} (n={}) — GLE={}\n"),
                    reinterpret_cast<uintptr_t>(m_addr), n,
                    static_cast<uint64_t>(::GetLastError()));
                return false;
            }
            std::memcpy(m_addr, src, n);
            // Restore the original page protection.  A failure here
            // leaves the page PAGE_EXECUTE_READWRITE — the patch IS
            // applied, but the page is now writable when the engine
            // expects it not to be.  That's mostly harmless (no engine
            // code writes to .text on its own) but defeats one layer
            // of anti-tamper / Windows DEP intent and can show up to
            // anti-cheat scanners as "unexpected RWX in .text".  We
            // log loudly so the issue is visible if it ever happens
            // (in practice: requires the address to have been
            // unmapped between VirtualProtect calls, vanishingly rare).
            DWORD junk = 0;
            if (!::VirtualProtect(m_addr, n, old_prot, &junk))
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.BytePatch] VirtualProtect(restore prot=0x{:x}) "
                        "failed at 0x{:x} (n={}) — GLE={}; page left RWX. "
                        "Patch IS applied but the page protection is wrong.\n"),
                    static_cast<uint32_t>(old_prot),
                    reinterpret_cast<uintptr_t>(m_addr), n,
                    static_cast<uint64_t>(::GetLastError()));
                // Continue anyway — the patch itself worked.
            }
            ::FlushInstructionCache(::GetCurrentProcess(), m_addr, n);
            return true;
        }

        void*               m_addr     = nullptr;
        std::vector<uint8_t> m_orig;
        std::vector<uint8_t> m_replace;
        bool                m_enabled  = false;
        bool                m_prepared = false;
    };

} // namespace Horse
