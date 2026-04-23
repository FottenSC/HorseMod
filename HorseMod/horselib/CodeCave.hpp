// ============================================================================
// Horse::CodeCave — lazy near-module executable allocator + bump-pointer.
//
// Why this exists
// ---------------
// Some bytepatches need to do MORE than just NOP an instruction — they need
// to run the original instruction, then run a small custom payload, then
// jump back into the function (e.g. the VFX-off port: orig movss + write a
// sentinel constant + return).  That requires a code trampoline.
//
// On x64, the only way to install a trampoline-style hook at a 5-byte site
// is via a rel32 JMP (E9 + signed 32-bit displacement = ±2 GB reach).  So
// the trampoline page MUST live within ±2 GB of the patch site, otherwise
// we'd need a 14-byte indirect jump that doesn't fit in the 5-byte slot.
//
// This file solves the "find ±2 GB executable memory" problem by walking
// the address space near SoulcaliburVI.exe's image base in 64 KB
// (allocation-granularity) chunks and VirtualAlloc'ing the first free
// region we hit.  One page is enough for many small trampolines, so we
// expose a bump-pointer allocator on top — first caller pays the cost,
// subsequent callers just sub-allocate from the same page.
//
// Lifecycle
// ---------
// The cave page leaks for the lifetime of the process.  This is
// intentional: trampolines installed into the cave are referenced by
// patched JMP instructions inside SC6's .text.  If we freed the cave
// while a patch was still active, those JMPs would land in deallocated
// memory and crash on next execution.  The patches themselves get
// restored in their owners' destructors (BytePatch RAII), and after
// every patch is restored the cave is dead memory we don't need to
// touch — so just letting it sit until process exit is correct.
//
// Threading
// ---------
// allocate() takes a critical-section lock so concurrent callers
// don't corrupt the bump pointer.  In practice we only allocate during
// resolve() of various patch helpers, which all run on the game thread,
// but the lock is cheap and removes a class of bug.
// ============================================================================

#pragma once

#include <DynamicOutput/DynamicOutput.hpp>

#include <Windows.h>

#include <cstdint>
#include <mutex>

namespace Horse
{
    class CodeCave
    {
    public:
        // Allocate n bytes from the cave, with optional alignment (default
        // 16 — comfortably over-aligned for any x64 instruction we'd
        // emit).  Returns nullptr if (a) we can't find a free near-module
        // page on first call, or (b) the cave page is exhausted.
        //
        // For HorseMod's use cases we emit at most a few dozen bytes per
        // patch and have <10 patches total — a single 4 KB page is plenty.
        static void* allocate(size_t n, size_t align = 16)
        {
            auto& self = instance();
            std::scoped_lock lock(self.m_mu);

            if (!self.m_base && !self.ensure_page_locked())
            {
                return nullptr;
            }

            // Bump pointer with alignment.
            uintptr_t cur = reinterpret_cast<uintptr_t>(self.m_base) + self.m_offset;
            uintptr_t aligned = (cur + (align - 1)) & ~(align - 1);
            const size_t pad = aligned - cur;

            if (self.m_offset + pad + n > self.m_size)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.CodeCave] page exhausted: req={} pad={} "
                        "have={} of {}\n"),
                    n, pad, self.m_offset, self.m_size);
                return nullptr;
            }

            self.m_offset += pad + n;
            return reinterpret_cast<void*>(aligned);
        }

        // For diagnostics / tests.
        static void* base() { return instance().m_base; }
        static size_t used() { return instance().m_offset; }

    private:
        CodeCave() = default;
        static CodeCave& instance()
        {
            static CodeCave s;
            return s;
        }

        // Find a near-module free region and VirtualAlloc a page there.
        // Walks UP from (sc6_base + 64K) toward (sc6_base + 2GB), trying
        // each 64 KB-aligned address.  Falls back to walking DOWN if the
        // upward search fails.  Logs and returns false on total failure.
        //
        // Why prefer "up": Windows tends to place loaded modules low in
        // the address space and leave the high range free, so probing
        // upward usually hits free memory on the first or second try.
        //
        // We restrict to the inner ±2 GB minus a 64 KB safety margin so
        // a 5-byte rel32 JMP from anywhere inside SC6's .text can reach
        // any address in the cave (and back).  rel32's actual range is
        // ±0x80000000 — leaving 0x10000 of headroom keeps us safely off
        // the cliff in case we underestimate .text size.
        bool ensure_page_locked()
        {
            HMODULE mod = ::GetModuleHandleW(L"SoulcaliburVI.exe");
            if (!mod)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.CodeCave] no SoulcaliburVI.exe module\n"));
                return false;
            }

            const uintptr_t base = reinterpret_cast<uintptr_t>(mod);
            const uintptr_t kStep = 0x10000;        // alloc granularity
            const uintptr_t kReach = 0x7FFE0000;    // 2 GB minus 64K safety

            auto try_alloc_in = [&](uintptr_t lo, uintptr_t hi,
                                    intptr_t step) -> void*
            {
                for (uintptr_t a = lo; (step > 0 ? a < hi : a > hi);
                     a = static_cast<uintptr_t>(static_cast<intptr_t>(a) + step))
                {
                    MEMORY_BASIC_INFORMATION mbi{};
                    if (!::VirtualQuery(reinterpret_cast<LPCVOID>(a), &mbi,
                                        sizeof(mbi)))
                    {
                        continue;
                    }
                    if (mbi.State != MEM_FREE) continue;
                    if (mbi.RegionSize < 0x1000) continue;

                    void* p = ::VirtualAlloc(
                        reinterpret_cast<LPVOID>(a),
                        0x1000,
                        MEM_RESERVE | MEM_COMMIT,
                        PAGE_EXECUTE_READWRITE);
                    if (p) return p;
                }
                return nullptr;
            };

            // Probe upward first.
            void* page = try_alloc_in(base + kStep, base + kReach,
                                      static_cast<intptr_t>(kStep));
            if (!page)
            {
                // Then downward — guard against underflow on tiny base.
                const uintptr_t lo_floor = base > kReach ? base - kReach : kStep;
                page = try_alloc_in(base - kStep, lo_floor,
                                    -static_cast<intptr_t>(kStep));
            }

            if (!page)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.CodeCave] no free near-module page within "
                        "+/-2GB of 0x{:x}\n"), base);
                return false;
            }

            m_base   = page;
            m_size   = 0x1000;
            m_offset = 0;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.CodeCave] page @ 0x{:x} (delta {:+} from "
                    "module base)\n"),
                reinterpret_cast<uintptr_t>(page),
                static_cast<int64_t>(reinterpret_cast<uintptr_t>(page) - base));
            return true;
        }

        std::mutex m_mu;
        void*      m_base   = nullptr;
        size_t     m_size   = 0;
        size_t     m_offset = 0;
    };

    // ------------------------------------------------------------------
    // Encode a 5-byte rel32 JMP (E9 + signed 32-bit displacement) at
    // `at` targeting `target`.  Caller is responsible for ensuring `at`
    // is writeable (use BytePatch's prepare/enable for the live image,
    // or write straight in if `at` is in our own cave).
    //
    // Returns false if the displacement doesn't fit in int32 — i.e. the
    // target is outside ±2 GB of `at`.  CodeCave guarantees this for
    // anything inside SC6's .text, but we still check defensively.
    // ------------------------------------------------------------------
    inline bool encode_jmp_rel32(void* at, void* target, uint8_t out[5])
    {
        const int64_t disp = reinterpret_cast<int64_t>(target)
                           - (reinterpret_cast<int64_t>(at) + 5);
        if (disp < INT32_MIN || disp > INT32_MAX)
        {
            RC::Output::send<RC::LogLevel::Error>(
                STR("[Horse.encode_jmp_rel32] disp 0x{:x} doesn't fit "
                    "in int32 (at=0x{:x} target=0x{:x})\n"),
                static_cast<uint64_t>(disp),
                reinterpret_cast<uintptr_t>(at),
                reinterpret_cast<uintptr_t>(target));
            return false;
        }
        out[0] = 0xE9;
        const int32_t rel = static_cast<int32_t>(disp);
        std::memcpy(&out[1], &rel, sizeof(rel));
        return true;
    }

} // namespace Horse
