// ============================================================================
// Horse::HasSubProviderEntryHook — PolyHook x64Detour on
// LuxBattleChara_HasSubProviderEntryOfType0x3e @ image+0x3F2990.
//
// Why this hook exists
// --------------------
// SC6's runtime SlipOut gate is NOT the IsSlipEnabled UFunction (which we
// hooked first as a UE4SS post-hook).  The UFunction never fires during
// gameplay — verified empirically in the 2026-04-28 casual lobby test
// where 30 seconds of match play produced zero "FIRST IsSlipEnabled
// runtime override fired" log lines.
//
// Tracing the actual gate via Ghidra (see plate comments on
// execIsSlipEnabled @ 0x140973370 and LuxBattleChara_CachePerPlayer-
// ProviderState @ 0x140478a40):
//
//   bool LuxBattleChara_HasSubProviderEntryOfType0x3e(void* providerSet)
//
// ...is called from BOTH:
//   1. execIsSlipEnabled (the UFunction exec thunk — never gets used in
//      gameplay)
//   2. LuxBattleChara_CachePerPlayerProviderState (the chara init
//      function that runs PER-CLIENT for each player slot at match
//      start, caching the slip-allowed bit at chara+0x488)
//
// The cache at chara+0x488 is what gameplay tick code actually reads
// when deciding whether to allow a slip input.  So:
//
//   * If we PolyHook this function and force-return false when our
//     SlipOut policy is active, the cache gets written false on both
//     clients (since both run the cache function during their own
//     chara init).
//   * Both clients' charas have chara+0x488 = "slip allowed".
//   * Both clients' simulations agree on slip inputs.
//   * No desync.
//
// Polarity
// --------
// The function returns TRUE when the chara HAS a SubProvider entry of
// type 0x3e — which is the DISABLE_PLAYER_SLIP marker.  So:
//   true  -> slip is SUPPRESSED
//   false -> slip is ALLOWED
//
// User picks SlipOut policy -> we force return = false -> slip allowed.
//
// Why this is the right gate (vs. the Start hook)
// -----------------------------------------------
// The Start hook approach (writing BattleRule.SlipOut = false to the
// data-table cache before the rule registrar runs) only takes effect
// IF the launcher's Start function is actually called on the local
// client.  Empirically, the joiner in an online match doesn't go
// through the same lobby-launcher path the host does — the joiner
// receives a "match starting" signal and spawns the match without
// driving the launcher's BlueprintCallable Start API.  So our Start
// hook fires on the HOST but NOT on the JOINER, leaving the joiner
// with vanilla rules → desync on first slip input → match crashes.
//
// LuxBattleChara_HasSubProviderEntryOfType0x3e, by contrast, runs on
// every client during chara init regardless of host/joiner role.
// Hooking it covers both sides uniformly.
//
// Threading
// ---------
// Called on the game thread during chara initialisation (also called
// from execIsSlipEnabled which fires from BP / ProcessEvent).  Our
// detour just reads an atomic policy flag and returns a constant — no
// locking required.
//
// Why no asm-stub register preservation (unlike SetStartPositionHook)
// -------------------------------------------------------------------
// The function is called via standard UE4 conventions and its callers
// don't rely on volatile register preservation across the call.  A
// plain C++ detour is safe.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"
#include "OnlineRules.hpp"

#include <polyhook2/Detour/x64Detour.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

namespace Horse
{
    class HasSubProviderEntryHook
    {
    public:
        // RVA verified via Ghidra: LuxBattleChara_HasSubProviderEntryOfType0x3e
        // is at image base + 0x3F2990 in the current Steam build.
        static constexpr uintptr_t kRVA = 0x3F2990;

        static HasSubProviderEntryHook& instance()
        {
            static HasSubProviderEntryHook s;
            return s;
        }

        bool install()
        {
            if (m_installed.load(std::memory_order_acquire)) return true;

            const uintptr_t image_base = NativeBinding::imageBase();
            if (!image_base)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[HasSubProviderEntryHook] NativeBinding image base "
                        "not resolved — cannot install\n"));
                return false;
            }
            const uintptr_t target = image_base + kRVA;

            m_trampoline = 0;
            m_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(target),
                reinterpret_cast<uint64_t>(&HasSubProviderEntryHook::detour),
                &m_trampoline);

            if (!m_detour->hook())
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[HasSubProviderEntryHook] x64Detour::hook() failed "
                        "on LuxBattleChara_HasSubProviderEntryOfType0x3e "
                        "(target=0x{:X}). SlipOut override will be inert.\n"),
                    target);
                m_detour.reset();
                return false;
            }

            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[HasSubProviderEntryHook] installed (target=0x{:X}, "
                    "trampoline=0x{:X})\n"),
                target, static_cast<uintptr_t>(m_trampoline));
            return true;
        }

        void uninstall()
        {
            if (!m_installed.exchange(false)) return;
            if (m_detour)
            {
                m_detour->unHook();
                m_detour.reset();
            }
            m_trampoline = 0;
        }

        bool installed() const noexcept
        {
            return m_installed.load(std::memory_order_acquire);
        }

    private:
        HasSubProviderEntryHook() = default;
        ~HasSubProviderEntryHook() { uninstall(); }
        HasSubProviderEntryHook(const HasSubProviderEntryHook&) = delete;
        HasSubProviderEntryHook& operator=(
            const HasSubProviderEntryHook&) = delete;

        // ---- The detour --------------------------------------------------
        //
        // Ghidra-verified signature:
        //   bool __fastcall LuxBattleChara_HasSubProviderEntryOfType0x3e(
        //       void* providerSet);
        //
        // Some callers pass a second integer argument (player index) but
        // the function body ignores it — we mirror the single-pointer
        // signature.  The second register slot (RDX) just passes through
        // to the trampoline if any caller needs it.
        //
        // Diagnostic logging fires once per process (per fire-class) so
        // the user can tell from UE4SS.log whether the gate is being
        // queried at all and what we returned.
        static bool __fastcall detour(void* providerSet)
        {
            using Fn = bool(__fastcall*)(void*);
            Fn orig = reinterpret_cast<Fn>(instance().m_trampoline);

            const auto policy = OnlineRules::instance().current_policy();

            // SlipOut policy active: force return = false (slip allowed).
            // This is what overrides the cache write at chara+0x488 to
            // "slip allowed" on both clients.
            if (policy == HorsePolicy::SlipOut)
            {
                static std::atomic<int> s_override_seen{0};
                if (s_override_seen.fetch_add(1) == 0)
                {
                    RC::Output::send<RC::LogLevel::Default>(
                        STR("[HasSubProviderEntryHook] FIRST SlipOut override "
                            "fired (providerSet=0x{:X}) — forcing return=false "
                            "(slip allowed)\n"),
                        reinterpret_cast<uintptr_t>(providerSet));
                }
                return false;
            }

            // Vanilla / other policies: forward to the original.  Log
            // first passthrough fire for diagnostic confirmation that
            // the hook is active and being called.
            static std::atomic<int> s_passthrough_seen{0};
            if (s_passthrough_seen.fetch_add(1) == 0)
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[HasSubProviderEntryHook] FIRST passthrough fire "
                        "(providerSet=0x{:X}, policy={})\n"),
                    reinterpret_cast<uintptr_t>(providerSet),
                    static_cast<int>(policy));
            }

            return orig ? orig(providerSet) : false;
        }

        std::unique_ptr<PLH::x64Detour> m_detour;
        uint64_t                        m_trampoline{0};
        std::atomic<bool>               m_installed{false};
    };
}
