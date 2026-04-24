// ============================================================================
// Horse::GameImGui::XInputHook — block gamepad input from reaching the game
// while the overlay is open.
//
// Why
// ---
// SC6 reads its controller state via XInputGetState (see FUN_140e1ec00).
// Because XInput is a POLLING API rather than a Windows-message pipeline,
// our WndProcHook has nothing to consume — pressing A on the pad to click
// an ImGui button would also make the character throw a punch, pressing
// D-pad to navigate the menu would also move the character, etc.  That's
// the same class of UX failure that would exist if the Steam overlay let
// gameplay inputs through while it was open.
//
// What this does
// --------------
// Installs a PolyHook x64Detour on the real XInputGetState (from
// xinput1_4.dll).  The detour:
//
//   1. Always calls the trampoline first, to get whatever Steam Input
//      has emulated for this frame.
//   2. If g_overlay_visible is true, zeroes out the returned state's
//      button mask, triggers, and both thumbstick axes — leaves dwPacketNumber
//      alone so the caller thinks the controller is still connected and
//      reporting, just with "nothing pressed."
//   3. If g_overlay_visible is false, returns the state unmodified.
//
// Our own GamepadInput::poll_and_detect_back calls the stored trampoline
// DIRECTLY via XInputHook::get_state_raw — it sees the untouched state
// so UI navigation continues to work while the overlay is open.
//
// Steam Input interaction
// -----------------------
// Steam Input's own XInputGetState hook (which emulates PS4 → Xbox) is
// installed very early, typically before us.  Our detour's trampoline
// therefore points at Steam's hook, not directly at the real exported
// function — so calling `orig(...)` goes Steam → real DXGI driver.  That
// chain stays intact.  What we add on top is just the "zero the result
// when overlay is visible" gate.
//
// Scope
// -----
// This hook catches everything that goes through the same xinput1_4.dll
// XInputGetState export — SC6's own calls, plus any other XInput consumer
// in the process.  That's what we want: we don't need to special-case
// SC6 vs Steam Input vs anything else, we just gate *all* XInput state
// at the last step before it reaches readers.
//
// Failure modes
// -------------
// If x64Detour fails to install (pattern mismatch, page not writable,
// etc.), we log and fall back to no-op mode.  GamepadInput falls back to
// calling XInputGetState directly if the trampoline is null.  Users in
// that state will have both the ImGui nav AND gameplay inputs firing
// simultaneously — not ideal, but less bad than crashing.
// ============================================================================

#pragma once

#include "DX11State.hpp"  // g_overlay_visible

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <Xinput.h>

#include <polyhook2/Detour/x64Detour.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

namespace Horse::GameImGui
{
    class XInputHook
    {
    public:
        static XInputHook& instance()
        {
            static XInputHook s;
            return s;
        }

        // Install the x64Detour on xinput1_4.dll's XInputGetState.
        // Idempotent; subsequent calls return the cached success
        // status.  Forces xinput1_4.dll to be loaded (delay-loaded
        // by default via our CMakeLists /DELAYLOAD flag) — that's
        // fine because the overlay is being enabled right now and
        // we're going to start using XInput this frame anyway.
        bool install()
        {
            if (m_installed.load(std::memory_order_acquire))
            {
                return true;
            }

            HMODULE hXInput = GetModuleHandleW(L"xinput1_4.dll");
            if (!hXInput)
            {
                hXInput = LoadLibraryW(L"xinput1_4.dll");
            }
            if (!hXInput)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[GameImGui.XInputHook] xinput1_4.dll not "
                        "available — controller-gate disabled.\n"));
                return false;
            }

            void* target = reinterpret_cast<void*>(
                GetProcAddress(hXInput, "XInputGetState"));
            if (!target)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[GameImGui.XInputHook] GetProcAddress for "
                        "XInputGetState returned null.\n"));
                return false;
            }

            m_trampoline = 0;
            m_detour = std::make_unique<PLH::x64Detour>(
                reinterpret_cast<uint64_t>(target),
                reinterpret_cast<uint64_t>(&XInputHook::detour_xinputgetstate),
                &m_trampoline);
            if (!m_detour->hook())
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[GameImGui.XInputHook] x64Detour::hook() failed "
                        "on XInputGetState (target=0x{:X}).\n"),
                    reinterpret_cast<uintptr_t>(target));
                m_detour.reset();
                return false;
            }

            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[GameImGui.XInputHook] installed (target=0x{:X}, "
                    "trampoline=0x{:X})\n"),
                reinterpret_cast<uintptr_t>(target),
                static_cast<uintptr_t>(m_trampoline));
            return true;
        }

        // Uninstall on mod teardown.  Safe if install never succeeded.
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

        // Called by GamepadInput to read the REAL controller state,
        // bypassing our zero-out gate.  Falls back to the unhooked
        // XInputGetState if the trampoline isn't populated (e.g. if
        // install() failed).
        DWORD get_state_raw(DWORD pad_index, XINPUT_STATE* out_state)
        {
            if (m_trampoline && m_installed.load(std::memory_order_acquire))
            {
                auto orig = reinterpret_cast<
                    DWORD(WINAPI*)(DWORD, XINPUT_STATE*)>(m_trampoline);
                return orig(pad_index, out_state);
            }
            // Pre-install or install-failed path: call the public API
            // directly.  Over the first few frames before install()
            // fires, our poll might echo into the game — acceptable.
            return XInputGetState(pad_index, out_state);
        }

    private:
        XInputHook() = default;
        ~XInputHook() { uninstall(); }
        XInputHook(const XInputHook&)            = delete;
        XInputHook& operator=(const XInputHook&) = delete;

        // ---- The actual detour --------------------------------------
        static DWORD WINAPI detour_xinputgetstate(DWORD pad_index,
                                                  XINPUT_STATE* out_state)
        {
            auto& self = instance();
            auto orig = reinterpret_cast<
                DWORD(WINAPI*)(DWORD, XINPUT_STATE*)>(self.m_trampoline);
            const DWORD r = orig ? orig(pad_index, out_state)
                                  : XInputGetState(pad_index, out_state);

            // Only gate when the read actually succeeded.  If the
            // controller is unplugged we don't have anything to zero
            // anyway — let the caller see ERROR_DEVICE_NOT_CONNECTED.
            if (r == ERROR_SUCCESS && out_state &&
                g_overlay_visible.load(std::memory_order_relaxed))
            {
                // Preserve dwPacketNumber: if we zeroed it, the
                // caller might think the read didn't update and
                // skip re-processing.  Leave it alone so the game's
                // internal "did anything change?" logic still runs
                // with the new "everything released" state.
                out_state->Gamepad.wButtons       = 0;
                out_state->Gamepad.bLeftTrigger   = 0;
                out_state->Gamepad.bRightTrigger  = 0;
                out_state->Gamepad.sThumbLX       = 0;
                out_state->Gamepad.sThumbLY       = 0;
                out_state->Gamepad.sThumbRX       = 0;
                out_state->Gamepad.sThumbRY       = 0;
            }
            return r;
        }

        std::unique_ptr<PLH::x64Detour> m_detour;
        uint64_t                        m_trampoline{0};
        std::atomic<bool>               m_installed{false};
    };
}
