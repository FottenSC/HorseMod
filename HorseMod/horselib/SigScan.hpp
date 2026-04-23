// ============================================================================
// Horse::SigScan — minimal AOB scanner over SoulcaliburVI.exe's .text.
//
// Why this exists
// ---------------
// We're porting a few CheatEngine AOB+bytepatch tricks (camera-lock, VFX
// off, game pause, …) into the mod.  None of those touch UFunction
// reflection — they patch raw instructions at known signatures inside
// the SC6 binary.  So we need a way to:
//
//   1. Resolve a CE-style hex pattern (e.g. "F2 0F 11 87 10 04 00 00 F2",
//      with "??" or "*" for don't-care bytes) to an absolute address in
//      the loaded SC6 image.
//   2. Restrict the search to executable code (the .text section) so we
//      don't false-match into data.
//
// UE4SS does provide its own sig scanner (RC::SinglePassScanner) but it's
// a batch-style API — load up a SignatureContainerMap, fire start_scan,
// read results out of containers.  Massively heavier than what we need
// for a one-off "where's this 9-byte AOB?" lookup.  This file is a 60-line
// naive scanner that's perfectly fast enough (a single linear pass over
// ~30 MB of .text completes in <50 ms on a modern CPU; we run it a
// handful of times during init, never on the hot path).
//
// Pattern syntax
// --------------
//   Whitespace separated hex bytes.  "??" or "*" = any byte (wildcard).
//   Case-insensitive on hex digits.  Empty pattern = error.
//
//     "F2 0F 11 87 10 04 00 00 F2"        // exact match
//     "F3 0F 10 05 ?? ?? ?? ?? F3 0F 59"  // 4 wildcard bytes (RIP-relative
//                                         // operand we don't care about)
//
// Threading / lifecycle
// ---------------------
// Pure read-only scan over loaded module memory.  No allocation, no UE4SS
// state.  Safe to call from any thread, but in practice we only scan
// during on_unreal_init (game thread, single-threaded).  Cache the
// resolved address in your patch object — re-scanning every frame would
// be wasteful (and also pointless: addresses are stable for the process
// lifetime).
// ============================================================================

#pragma once

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>

#include <Windows.h>
#include <winnt.h>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

namespace Horse
{
    // ------------------------------------------------------------------
    // CE-style pattern → (bytes, mask) parser.  Mask byte 0xFF means
    // "must match"; 0x00 means "wildcard, ignore the byte".  Returns
    // empty optional on parse failure.
    //
    // Anything that isn't whitespace, a hex digit, '?', or '*' is a parse
    // error — we'd rather fail loud than silently match the wrong place.
    // ------------------------------------------------------------------
    struct ParsedPattern
    {
        std::vector<uint8_t> bytes;
        std::vector<uint8_t> mask;   // parallel; 0xFF or 0x00 per byte
    };

    inline std::optional<ParsedPattern> parse_pattern(std::string_view p)
    {
        ParsedPattern out;
        out.bytes.reserve(p.size() / 2);
        out.mask .reserve(p.size() / 2);

        size_t i = 0;
        while (i < p.size())
        {
            // Skip whitespace.
            while (i < p.size() && std::isspace(static_cast<unsigned char>(p[i]))) ++i;
            if (i >= p.size()) break;

            // Wildcard: "??" or "*".  Both consume one logical "byte".
            if (p[i] == '*')
            {
                out.bytes.push_back(0);
                out.mask .push_back(0);
                ++i;
                continue;
            }
            if (i + 1 < p.size() && p[i] == '?' && p[i + 1] == '?')
            {
                out.bytes.push_back(0);
                out.mask .push_back(0);
                i += 2;
                continue;
            }

            // Two hex digits = one byte.
            if (i + 1 >= p.size()) return std::nullopt; // dangling nibble
            auto hex = [](char c, uint8_t& v) -> bool {
                if (c >= '0' && c <= '9') { v = static_cast<uint8_t>(c - '0');      return true; }
                if (c >= 'a' && c <= 'f') { v = static_cast<uint8_t>(c - 'a' + 10); return true; }
                if (c >= 'A' && c <= 'F') { v = static_cast<uint8_t>(c - 'A' + 10); return true; }
                return false;
            };
            uint8_t hi, lo;
            if (!hex(p[i], hi) || !hex(p[i + 1], lo)) return std::nullopt;
            out.bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
            out.mask .push_back(0xFF);
            i += 2;
        }

        if (out.bytes.empty()) return std::nullopt;
        return out;
    }

    // ------------------------------------------------------------------
    // Locate the .text section of a loaded module.  Returns (begin, end)
    // pointers, or {nullptr,nullptr} if anything looks wrong (no PE
    // header, no .text section, etc.).
    //
    // We restrict the search to .text because (a) it's where executable
    // instructions live so we won't false-positive on string literals
    // or pointer tables, and (b) it's smaller than the whole image, so
    // the linear scan is faster.
    // ------------------------------------------------------------------
    inline std::pair<const uint8_t*, const uint8_t*>
    module_text_range(HMODULE mod)
    {
        if (!mod) return {nullptr, nullptr};
        auto* base = reinterpret_cast<const uint8_t*>(mod);

        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return {nullptr, nullptr};

        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return {nullptr, nullptr};

        auto* sec = IMAGE_FIRST_SECTION(nt);
        for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++sec)
        {
            // Section names are space-padded, not null-terminated when
            // they fill all 8 bytes — ".text" + 3 NULs is the common case
            // but we compare bounded just in case.
            if (std::memcmp(sec->Name, ".text", 5) == 0
                && (sec->Name[5] == '\0' || sec->Name[5] == ' '))
            {
                const uint8_t* begin = base + sec->VirtualAddress;
                const uint8_t* end   = begin + sec->Misc.VirtualSize;
                return {begin, end};
            }
        }
        return {nullptr, nullptr};
    }

    // ------------------------------------------------------------------
    // Naive forward scan for `pat` within [begin, end).  Returns first
    // match address or nullptr.  We don't need Boyer-Moore — patterns
    // are short (<32 bytes), the search range is bounded to .text, and
    // we scan at most a few times per process.
    //
    // The caller is responsible for catching the "wildcard pattern is
    // ambiguous" case (multiple matches): we report the first one and
    // log if a second match would have hit too — silent ambiguity tends
    // to bite hard the first time SC6 patches and a sibling instruction
    // suddenly matches.
    // ------------------------------------------------------------------
    inline const uint8_t* scan(const uint8_t* begin, const uint8_t* end,
                               const ParsedPattern& pat,
                               bool warn_on_second_match = true)
    {
        if (!begin || !end || end <= begin) return nullptr;
        const size_t n = pat.bytes.size();
        if (n == 0 || end - begin < static_cast<ptrdiff_t>(n)) return nullptr;

        const uint8_t* found = nullptr;
        const uint8_t* p = begin;
        const uint8_t* last = end - n;
        while (p <= last)
        {
            // Inner compare with mask.  Branch-on-mismatch keeps the
            // hot path small for the (overwhelmingly common) "first
            // byte already differs" case.
            size_t i = 0;
            for (; i < n; ++i)
            {
                if ((pat.mask[i] & (p[i] ^ pat.bytes[i])) != 0) break;
            }
            if (i == n)
            {
                if (!found)
                {
                    found = p;
                    if (!warn_on_second_match) return found;
                    // Continue scanning to detect ambiguity.
                }
                else
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[Horse.SigScan] pattern matched MORE than once "
                            "(first 0x{:x}, second 0x{:x}) — first match "
                            "kept but the AOB is ambiguous; tighten it\n"),
                        reinterpret_cast<uintptr_t>(found),
                        reinterpret_cast<uintptr_t>(p));
                    return found;
                }
            }
            ++p;
        }
        return found;
    }

    // ------------------------------------------------------------------
    // High-level convenience: scan SoulcaliburVI.exe's .text for `pat`
    // and return the absolute address (or nullptr).  Logs on miss /
    // ambiguity so the caller doesn't have to.
    // ------------------------------------------------------------------
    inline void* sig_scan_sc6(std::string_view pattern_str,
                              const char* tag = "")
    {
        auto parsed = parse_pattern(pattern_str);
        if (!parsed)
        {
            RC::Output::send<RC::LogLevel::Error>(
                STR("[Horse.SigScan] {} bad pattern syntax\n"),
                RC::to_generic_string(tag));
            return nullptr;
        }

        HMODULE mod = ::GetModuleHandleW(L"SoulcaliburVI.exe");
        if (!mod)
        {
            RC::Output::send<RC::LogLevel::Error>(
                STR("[Horse.SigScan] {} GetModuleHandle(SoulcaliburVI.exe) "
                    "returned null\n"),
                RC::to_generic_string(tag));
            return nullptr;
        }

        auto [begin, end] = module_text_range(mod);
        if (!begin)
        {
            RC::Output::send<RC::LogLevel::Error>(
                STR("[Horse.SigScan] {} couldn't locate .text section\n"),
                RC::to_generic_string(tag));
            return nullptr;
        }

        const uint8_t* hit = scan(begin, end, *parsed);
        if (!hit)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[Horse.SigScan] {} pattern not found (game patched? "
                    "wrong build?)\n"),
                RC::to_generic_string(tag));
            return nullptr;
        }

        RC::Output::send<RC::LogLevel::Verbose>(
            STR("[Horse.SigScan] {} -> 0x{:x} (RVA 0x{:x})\n"),
            RC::to_generic_string(tag),
            reinterpret_cast<uintptr_t>(hit),
            reinterpret_cast<uintptr_t>(hit) - reinterpret_cast<uintptr_t>(mod));

        return const_cast<void*>(reinterpret_cast<const void*>(hit));
    }

} // namespace Horse
